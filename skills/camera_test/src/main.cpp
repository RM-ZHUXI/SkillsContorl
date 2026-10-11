/**
 * @file main.cpp
 * @brief camera_test 技能：把公共内存里的画面**按需开窗显示出来**
 * @author 时新博 a17398481273@163.com
 * @date 2026/10/07
 * @version 1.4.0
 *
 * @par v1.4.0 关键变化
 *      end() 不再销毁 Displayer、不再关窗，只"暂停渲染"。
 *      旧实现每次停止链都关窗 + join 窗口线程，下次启动再新建 Displayer →
 *      在一个**新线程**上重新 namedWindow；OpenCV 的 Win32 HighGUI 后端扛不住这种
 *      反复建/销窗口（实测第 2 次建窗 3.1s 后失败并永久阻塞），
 *      于是 end() 里的 join 永远等不到 → 网页上「停止」按钮卡死、链停不下来。
 *      现在窗口与 GUI 线程在整个 DLL 生命周期里只创建一次，停止链立即可返回。
 *
 * @par 它做什么
 *      - init()：只读配置，不创建 GUI —— 保证 init 永不失败，链一定能启动；
 *      - run() ：按需创建显示（打不开只 log 警告，不影响链），然后投递一帧；
 *      - end() ：停显示线程、关窗口、销毁 Displayer，让 GUI 资源在 DLL 卸载前释放。
 *
 * @par 为什么窗口由技能自己起线程
 *      技能链的 run() 跑在工作线程里，跨线程开 GUI（Qt/GTK）会被判为非法甚至
 *      直接 abort，而技能崩了会把宿主进程一起带走。所以所有 GUI 调用都放在
 *      本技能自己创建的显示线程（viewer::Displayer 内部）里。
 *
 * @par v1.3.0 关键变化
 *      窗口创建从 init() 挪到 run()：init 失败 = 链失败，而 GUI 是不可靠的
 *      （可能没 DISPLAY、可能窗口残留），不该绑在链的生死线上。
 *
 * @par 约定
 *      run() 干一次活就返回，绝不死循环；循环交给技能链的 mode=while + interval。
 */

#include "main.hpp"

#include "tools/logger.hpp"

#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <yaml-cpp/yaml.h>

#if defined(_WIN32)
    #include <windows.h>
#elif defined(__linux__)
    #include <dlfcn.h>
#endif

#include "display.hpp"

namespace
{
    /**
     * @brief skillcontorl 注入进来的内存管理器
     * @note  只有在 init() 成功执行后才有值，run()/end() 里必须先判空
     */
    sc::tools::Memory *mem = nullptr;

    /// 显示器（按需在 run() 里创建；★ 一旦创建就活到 DLL 卸载，见 end() 的说明）
    std::unique_ptr<viewer::Displayer> disp;

    /**
     * @brief 本次技能会话（最近一次 init 之后）是否已经恢复/启动过显示
     * @note  用它保证 start() 每次会话只调一次：
     *        用户按 ESC/q 主动暂停后，不该被下一轮 run() 立刻又恢复渲染。
     */
    bool dispResumed = false;

    /// 配置
    viewer::Displayer::Config cfg;

    /**
     * @brief 定位 config.yaml
     * @return 配置路径；优先按本模块自己的位置推导，找不到再退回相对路径
     * @note  双平台：Windows 用 GetModuleFileName，Linux 用 dladdr。
     */
    std::string locateConfigFile()
    {
        std::string modulePath;

#if defined(_WIN32)
        HMODULE hMod = nullptr;
        if (GetModuleHandleExA(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCSTR>(&locateConfigFile),
                &hMod))
        {
            char buf[MAX_PATH] = {0};
            if (GetModuleFileNameA(hMod, buf, MAX_PATH) > 0)
                modulePath = buf;
        }
#elif defined(__linux__)
        Dl_info info{};
        if (dladdr(reinterpret_cast<void *>(&locateConfigFile), &info) != 0 &&
            info.dli_fname != nullptr)
        {
            modulePath = info.dli_fname;

            // dladdr 返回的可能是相对路径，补成绝对路径
            if (!modulePath.empty() && modulePath[0] != '/')
            {
                std::error_code ec;
                const std::filesystem::path cwd = std::filesystem::current_path(ec);
                if (!ec) modulePath = (cwd / modulePath).lexically_normal().string();
            }
        }
#endif

        if (!modulePath.empty())
        {
            const std::filesystem::path p(modulePath);
            const std::filesystem::path dir = p.parent_path();

            const std::filesystem::path candidates[] = {
                dir / ".." / ".." / "config.yaml",   // 发布布局：<技能目录>/config.yaml
                dir / "config.yaml",                 // 与 .dll / .so 同级
            };

            for (const auto &c : candidates)
            {
                std::ifstream probe(c);
                if (probe.good()) return c.string();
            }
        }

        // 兜底：相对当前工作目录
        return "skills/camera_test/config.yaml";
    }

    /**
     * @brief 读取配置
     * @note  自己用 YAML::LoadFile 读，失败只打印并用默认值 —— 绝不能让读配置把宿主带走。
     */
    void loadConfig(const std::string &path)
    {
        try
        {
            YAML::Node y = YAML::LoadFile(path);

            cfg.sourceKey   = y["source_key"]   ? y["source_key"].as<std::string>()   : cfg.sourceKey;
            cfg.window      = y["window"]       ? y["window"].as<bool>()              : cfg.window;
            cfg.windowWidth = y["window_width"] ? y["window_width"].as<int>()         : cfg.windowWidth;
            cfg.windowName  = y["window_name"]  ? y["window_name"].as<std::string>()  : cfg.windowName;

            if (cfg.windowWidth < 0) cfg.windowWidth = 0;     // 0 = 不缩放
        }
        catch (const std::exception &e)
        {
            sc::tools::logger()->error("[camera_test] 读取配置失败，使用默认值: {} ({})",
                                       e.what(), path);
        }
    }
}

/**
 * @brief 程序初始化：只读配置，不创建 GUI
 * @note  init 失败 = 链失败，而 GUI 不可靠（可能没 DISPLAY、可能窗口残留），
 *        所以 init 里绝不做 GUI 相关的事。
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[camera_test] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;

    const std::string configPath = locateConfigFile();
    loadConfig(configPath);

    // 新的一次会话：允许下一次 run() 重新恢复渲染
    dispResumed = false;

    sc::tools::logger()->info("[camera_test] init 完成（窗口将在 run() 里按需创建，"
                              "key={}, window={}）",
                              cfg.sourceKey, cfg.window);
}

/**
 * @brief 每轮：按需启动显示 + 投递一帧
 * @note  一次调用只干一件事，干完立刻返回；循环交给技能链。
 */
SKILL_API void run()
{
    if (mem == nullptr) return;

    // 配置里没开窗口：什么都不做
    if (!cfg.window) return;

    // ★ 按需创建显示对象：窗口没起就起一个，起了就投帧。
    //   这样即使窗口打不开，链也能正常跑，只是没有显示。
    //   注意：这个 Displayer 一旦创建就**一直活着**（end() 不再销毁它），
    //   因为它内部的窗口线程与窗口本身只能创建一次，见 display.hpp 的说明。
    if (!disp)
    {
        disp = std::make_unique<viewer::Displayer>(mem, cfg);
    }

    // 建窗失败过：不再重试（也就不会有日志刷屏）
    if (disp->failed()) return;

    // 每次会话只需"启动/恢复"一次，之后每轮只管投帧。
    // 这样用户按 ESC/q 主动暂停后，不会被下一轮 run() 立刻又打开。
    if (!dispResumed)
    {
        if (!disp->start())
        {
            // 首次失败：只告警一次，链继续跑
            sc::tools::logger()->warn("[camera_test] 显示窗口打不开，本次跳过显示"
                                      "（链继续跑，只是没有窗口；详见前面日志）");
            return;
        }
        dispResumed = true;
    }

    cv::Mat frame;
    if (!mem->get(cfg.sourceKey, frame) || frame.empty())
    {
        return;                             // 还没有图：窗口继续显示占位图 / 上一帧
    }

    disp->show(frame);
}

/**
 * @brief 收尾：暂停显示（不关窗、不销毁 Displayer）
 * @note  ★ 只暂停，不销毁 —— 原因见函数体里的说明（反复建/销窗口会让 HighGUI 卡死，
 *        进而导致"停止链"永久阻塞）。
 */
SKILL_API void end()
{
    if (disp == nullptr)
    {
        sc::tools::logger()->info("[camera_test] end() 被调用（显示未创建）");
        return;
    }

    disp->stop();                           // 只暂停渲染：不 join、不关窗、不阻塞
    dispResumed = false;                    // 下次会话允许恢复渲染
    // ★ 这里不再 disp.reset()：销毁 Displayer 会关窗 + join 窗口线程，而下次启动
    //   又会在一个新线程上重新 namedWindow —— OpenCV 的 Win32 HighGUI 扛不住反复
    //   建/销窗口（实测第 2 次建窗会永久阻塞），结果就是"停止链"卡死、再也停不下来。
    //   只暂停渲染即可；真正的关窗与线程回收交给 ~Displayer()（DLL 卸载 / 进程退出）。

    sc::tools::logger()->info("[camera_test] 已暂停显示（累计显示 {} 帧，窗口保留）",
                              disp->frames());
}