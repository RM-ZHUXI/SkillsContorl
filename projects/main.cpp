/**
 * @file        main.cpp
 * @brief       程序入口点，负责初始化系统并启动主循环
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-09
 * @version     1.0.0
 */

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <string>
#include <thread>
#include <vector>
#include <opencv2/opencv.hpp>

#include "net/net.hpp"          
#include "tools/tools.hpp"
#include "functions/funcions.hpp"

namespace tools = sc::tools;

tools::Time timer;       ///< 定时器对象
std::string configPath;  ///< 配置文件路径
std::string autoChains;  ///< 启动后自动启动的技能链（逗号分隔，可为空）

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
     * @brief 加载所有刷新并且加载所有skill
     */
    sc::tools::updateSkill();

    /**
     * @brief 公共内存类型注册
     */
    //tools::Memory::registerTypeAll();

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
         * @brief debug
         */
        sc::tools::logger()->debug("skill_info 总数 : {}",sc::tools::SC_ALL_SKILL_INFO.size());
        for(auto &e : sc::tools::SC_ALL_SKILL_INFO)
        {
            sc::tools::logger()->debug("skill 名称 : {} url : {} load: {}",e.second.skill.name,e.second.skill.url,e.second.load);
        }


    }
    catch(const std::exception& e)
    {
        sc::tools::logger()->error("[main] 严重错误 : {}" + std::string(e.what()));
    }

    
    return 0;
}
