/**
 * @file main.cpp
 * @brief 摄像头采集技能入口（init / run / end）—— 只负责把画面写进公共内存
 * @author 时新博 a17398481273@163.com
 * @date 2026/10/07
 * @version 1.0.5
 *
 * @par 职责（刻意做到最小）
 *      每轮把采集到的画面写进公共内存的 "image" 键，别的一概不做：
 *        - 不弹窗（技能跑在工作线程里，且服务器/虚拟机常无图形环境，开 GUI 会崩）；
 *        - 不存图、不写统计键、不做预览；
 *        - 想「看」画面 → 交给 camera_test 技能（开窗口显示公共内存里的图）。
 *
 * @par 约定
 *      run() 干一次活就返回，**绝不死循环**；循环交给技能链的 mode=while + interval。
 *
 * @par v1.0.5 关键变化
 *      1. 入口符号统一由 include/main.hpp 声明，宏直接用 skillscontrol.hpp 的 SKILL_API。
 *         旧版本 main.hpp 是从 camera_test 抄过来的，判的是 CAMERA_TEST_EXPORTS，
 *         而 CMake 给本目标定义的其实是 HAIKANG_CAMERA_EXPORTS；导出能成立纯属
 *         「camera.hpp → skillscontrol.hpp 把 SKILL_API 宏重定义了一遍」的巧合，
 *         换一次包含顺序就会编出没有 init/run/end 的 DLL。
 *      2. end() 里显式 stop() + release()：停链时就把设备句柄关掉，
 *         不再只 stop() 采集、把句柄留给 DLL 卸载。（release() 幂等，析构里还会兜一次。）
 *
 * @note  导出必须是 extern "C" 的 init(sc::tools::Memory*) / run() / end()，
 *        否则导出名会被 C++ 修饰，宿主 tools/loadskills.cpp 就解析不到。
 */

#include "main.hpp"              // init / run / end 声明（同时带来 SKILL_API、Memory）

#include "camera.hpp"            // hardware::camera
#include "tools/logger.hpp"      // 技能要写日志就得显式包含（skillscontrol.hpp 只保证 Memory）

#include <memory>
#include <stdexcept>

namespace
{
    /**
     * @brief skillcontorl 注入进来的内存管理器
     * @note  只有在 init() 成功执行后才有值，run()/end() 里必须先判空
     */
    sc::tools::Memory *mem = nullptr;

    /**
     * @brief 相机对象
     * @note  随本 .dll / .so 的加载状态存活。
     *        每次「启动技能链」init() 会重新构造并打开设备，
     *        每次「停止技能链」end() 会 stop + release，所以停→再启动 = 设备重新初始化。
     */
    std::unique_ptr<hardware::camera> cap;
}

/**
 * @brief 程序初始化：定位配置 + 打开相机 + 开始采集
 * @note  签名必须和 include/main.hpp 的声明、以及宿主查找的 "init" 导出名完全一致
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[haikang_camera] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;

    // 构造相机：内部完成"定位 config.yaml → 读配置 → 打开设备"，不做任何界面显示
    cap = std::make_unique<hardware::camera>(memory);

    // ★ 相机起不来就让 init 失败：
    //   框架会捕获这个异常 → 该技能链被标记为失败 → 网页 /api/GetRunStatus 的 error 字段可见，
    //   而不是"链在跑、每轮都取空图、页面上什么都看不出来"。
    if (!cap->start())
    {
        throw std::runtime_error(
            "[haikang_camera] 相机启动失败：请检查相机是否插好/是否被别的程序占用，"
            "以及 skills/haikang_camera/config.yaml 是否可读（详细原因见日志）");
    }

    sc::tools::logger()->info("[haikang_camera] init 完成");
}

/**
 * @brief 每轮取一帧写进公共内存
 * @note  一次调用只干一件事，干完立刻返回（循环由技能链的 mode=while + interval 负责）
 */
SKILL_API void run()
{
    if (mem == nullptr || cap == nullptr)
    {
        sc::tools::logger()->error("[haikang_camera] init失败");
    }

    cv::Mat frame = cap->image();
    if (frame.empty())
    {
        return;
    }

    mem->insertOrAssign("image", frame);
}

/**
 * @brief 收尾：停采集 + 释放设备
 * @note  显式 release() 让句柄在"停止链"时就关掉；~camera() 里还会兜一次（幂等）。
 */
SKILL_API void end()
{
    if (cap == nullptr) return;

    cap->stop();
    cap->release();

    sc::tools::logger()->info("[haikang_camera] 已停止采集");
}
