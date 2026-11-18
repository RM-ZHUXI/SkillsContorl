/**
 * @file        threadController.hpp
 * @brief       类型声明
 * @author      o_O a17398481273@163.com
 * 
 * @date        2026-11-18
 * @pre
 * 线程管理器       
 */

#ifndef THREADCONTROLLER_HPP
#define THREADCONTROLLER_HPP

#include <thread>
#include <string>
#include <functional>
#include <mutex>

#include "logger.hpp"
#include "tools_api.hpp"

namespace sc::tools
{

/**
 * @brief 线程运行信息
 */
struct ThreadInfo
{
    bool run;                        ///< 是否正在运行
    unsigned long long lasttime;     ///< 上一次运行开始时间 (while可选)
    unsigned long long runtime;      ///< 运行一次的时间     (while可选)
    std::string error;               ///< 上次运行错误的代码 

    std::mutex mtx_run;              ///< 运行标志位互斥锁
    std::mutex mtx_lasttime;         ///< 上次运行开始时间互斥锁
    std::mutex mtx_runtime;          ///< 运行一次的时间互斥锁
    std::mutex mtx_error;            ///< 上次运行错误的代码互斥锁
};

/**
 * @brief 线程对象
 */
class ThreadItem
{
private:
std::string name;                            ///< 线程名称
std::thread thread;                          ///< 线程对象
std::function<void(ThreadInfo*)> function;   ///< 功能函数
ThreadInfo info;                             ///< 线程信息

public:
/**
 * @brief 构造函数
 */
ThreadItem() = default;

/**
 * @brief 运行线程
 * @throw 运行失败原因
 */
void run();
}; // class ThreadItem

/**
 * @brief 线程管理类
 */
class TOOLS_API ThreadController
{
private:
std::map<std::string,std::thread> thread_; // 线程集合 名称->线程

public:

/**
 * @brief 默认构造函数
 */
ThreadController() = default;

/**
 * @brief 添加线程
 * @param name 名称
 * @param std::function<void(Threadinfo*)> 对应函数
 * @throw 添加失败原因
 */
void addThread(std::string name,std::function<void(ThreadInfo*)>);



};// class ThreadController

} // namespace sc::tools
#endif // THREADCONTROLLER_HPP