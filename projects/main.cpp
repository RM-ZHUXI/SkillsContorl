/**
 * @file        main.cpp
 * @brief       程序入口点，负责初始化系统并启动主循环
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-09
 * @version     1.0.0
 *
 * @version     1.1.0
 * @par         1. 技能不再在启动时直接跑完，改为交给 RunSkillsLists 线程管理器，
 *                 由网页/接口按技能链的方式启动与停止
 *              2. 接上 Assign（技能链）与 RunSkillsLists（线程管理器）
 *              3. 注册 Ctrl+C 处理，退出时先停技能链再关 http
 *
 * @version     1.2.0
 * @par         1. 技能生命周期以 start/stop 为边界（配合 RunSkillsLists 2.0）：
 *                 每一次「启动技能链」都会对链内技能重新 load + init()，
 *                 每一次「停止技能链」都会调用 end() 并卸载动态库；
 *                 所以「停止 → 再启动」= 设备的完整重新初始化，而不是进程起来只 init 一次。
 *              2. 新增可选参数 --chain=a,b（简写 -c=a,b）：启动后自动把这几条链跑起来，
 *                 用来在没有网页/接口的情况下验证 init/end 与节拍；缺省仍然完全由网页控制。
 *              3. 退出顺序明确成三步：STATUS=false（技能自觉收手）→ stopAll()（每条链都走 end + 卸载）
 *                 → 关 http 服务。
 *              4. 可选配置 skills_exclusive: true —— 同一个技能不允许被两条链同时使用
 *                 （默认 false：多条链共用同一份技能实例，只有第一个使用者 init、
 *                  最后一个使用者 stop 时才 end）。
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>

#include "net/net.hpp"          // 先包含网络头（内部先引 winsock2.h，避免与 windows.h 顺序告警）
#include "tools/tools.hpp"
#include "functions/funcions.hpp"

namespace tools = sc::tools;

tools::Time timer;       ///< 定时器对象
std::string configPath;  ///< 配置文件路径
std::string autoChains;  ///< 启动后自动启动的技能链（逗号分隔，可为空）
tools::Memory memory;    ///< 内存管理器(公共内存)

const std::string keys = 
    "{help h usage ? | | 输出命令行参数说明 }"
    "{chain c | | 启动后自动启动的技能链名(逗号分隔)，缺省由网页/接口控制 }"
    "{@config | configs/config.yaml | 配置文件路径 }";
    
/**
 * @brief 初始化
 */
void init(int argc, char * argv[])
{
    /**
     * @brief 日志初始化
     */
    tools::logger();

    /**
     * @brief 参数解析
     */
    cv::CommandLineParser parser(argc, argv, keys);
    if (parser.has("help")) {
        parser.printMessage();
        return;
    }
    configPath = parser.get<std::string>("@config");
    autoChains = parser.get<std::string>("chain");
    if (!parser.check()) {
        parser.printErrors();
        return;
    }

    /**
     * @brief 公共内存类型注册（内置类型 + 容器 + cv 类型，进程内只跑一次）
     */
    tools::Memory::registerTypeAll();

    tools::logger()->info("[main] start with : {}", configPath);
    tools::logger()->info("[main] start at : {}", timer.nowDate() + " " + timer.nowTime());
}

/**
 * @brief 退出
 */
void exit() {
    tools::logger()->info("[main] exit at : {}", timer.nowDate() + " " + timer.nowTime());
}

namespace {
    std::atomic<bool> g_running{true};
    void onExit(int) { g_running = false; }

    /**
     * @brief 把 "a,b; c" 拆成 {"a","b","c"}（逗号/分号/空格都算分隔符，空项丢掉）
     */
    std::vector<std::string> splitNames(const std::string& text)
    {
        std::vector<std::string> out;
        std::string cur;

        for (char c : text)
        {
            if (c == ',' || c == ';' || c == ' ' || c == '\t')
            {
                if (!cur.empty()) { out.push_back(cur); cur.clear(); }
            }
            else
            {
                cur += c;
            }
        }

        if (!cur.empty()) out.push_back(cur);
        return out;
    }
}

int main(int argc, char * argv[])
{
    try
    {  
        /**
         * @brief 初始化
         */
        init(argc, argv);

        /**
         * @brief 技能查找对象初始化（会扫描 skills 目录并写回 skills.json）
         */
        tools::FindSkills findSkills(configPath);

        
        /**
         * @brief 注册所有dll;
         */
        if(!tools::loadAll(findSkills.get_skills()))
        {
            tools::logger()->error("[main] skills动态库加载失败");
            return 0;
        }      

        /**
         * @brief 技能链管理器初始化（读取 skillsList.json）
         */
        tools::Assign assign(configPath, findSkills);

        /**
         * @brief 技能链线程管理器初始化
         */
        tools::Runskills runskills(timer,&memory,assign);

        /**
         * @brief 注册标定程序
         */
        sc::functions::CalibrateCamera calibratecamera(configPath);

        /**
         * @brief 可选：config.yaml 里 skills_exclusive: true 时，同一个技能不允许被两条链同时使用
         * @note  技能的全局变量在 DLL 里，一个 DLL 只有一份；两条链同时跑同一个技能时，
         *        它们的 run() 会并发操作同一份全局对象（比如同一个相机句柄）。
         *        默认 false（共用），技能不是线程安全或设备需要独占时打开它。
         */
        // {
        //     const YAML::Node cfg = tools::load(configPath);
        //     if (cfg["skills_exclusive"])
        //     {
        //         const bool exclusive = cfg["skills_exclusive"].as<bool>();
        //         runSkillsLists.setShareSkillAcrossChains(!exclusive);
        //         tools::logger()->info("[main] 技能独占模式: {}",
        //                               exclusive ? "开（同一个技能不能被两条链同时启动）"
        //                                         : "关（默认：多条链共用同一份技能实例）");
        //     }
        // }

        /**
         * @brief web可视化初始端
         * @note  网页通过 /api 下的接口监测公共内存、编辑技能链并启停它们
         */ 
        sc::net::HttpServer httpServer(configPath, memory, findSkills, assign, runskills,calibratecamera);
        httpServer.start();

        /**
         * @brief 允许技能运行，并接管 Ctrl+C 优雅退出
         */
        tools::STATUS = true;
        std::signal(SIGINT, onExit);
        std::signal(SIGTERM, onExit);

        tools::logger()->info("[main] 就绪：内存 {} 项，技能 {} 个，技能链 {} 条",
                              memory.size(),
                              findSkills.get_skills().size(),
                              assign.getLists().size());

        while (g_running)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
        
        /**
         * @brief 退出：先停技能链（等线程收尾 + 逐个 end + 卸载）再关 http
         * @note  STATUS=false 先让技能的 run() 自觉收手，
         *        stopAll() 返回时所有技能的 end() 都已经执行完
         */
        tools::STATUS = false;

        httpServer.stop();
        exit();
    }
    catch (std::exception &e) {
        tools::logger()->error(e.what());
    }

    // 告诉退出守卫：这次是我们自己走完流程退出的
    // （如果没走到这里就退了，说明有技能/第三方库直接调用了 exit()，日志里会有明确告警）
    tools::markNormalExit();
    return 0;
}
