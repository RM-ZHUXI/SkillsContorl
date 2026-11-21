/**
 * @file        threadController.hpp
 * @brief       类型声明
 * @author      o_O a17398481273@163.com
 * 
 * @date        2026-11-21
 * @pre
 * 线程管理器       
 */

#ifndef THREADCONTROLLER_HPP
#define THREADCONTROLLER_HPP

#include <thread>
#include <string>
#include <functional>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <map>
#include "logger.hpp"
#include "tools_api.hpp"

namespace sc::tools
{

/**
 * @brief 线程运行信息
 */
struct ThreadInfo
{
    bool run = false;                ///< 是否正在运行
    bool stop = false;               ///< 停止指令
    unsigned long long lasttime = 0; ///< 上一次运行开始时间 (while可选)
    unsigned long long runtime = 0;  ///< 运行一次的时间     (while可选)
    std::string error = "";          ///< 上次运行错误的代码 

    std::mutex mtx_run;              ///< 运行标志位互斥锁
    std::mutex mtx_stop;             ///< 停止指令互斥锁
    std::mutex mtx_lasttime;         ///< 上次运行开始时间互斥锁
    std::mutex mtx_runtime;          ///< 运行一次的时间互斥锁
    std::mutex mtx_error;            ///< 上次运行错误的代码互斥锁
    std::thread::id workerId;        ///< 自己的工作线程
};

/**
 * @brief 线程对象
 * @note  需要加入的线程函数为void(ThreadInfo*)
 *        并且所有值更改的时候需要打互斥锁
 *        而且需要在真正获取到stop的值的时候自己设置
 *        stop -> false
 */
class ThreadItem
{
private:
std::string name_;                            ///< 线程名称
std::thread thread_;                          ///< 线程对象
std::function<void(ThreadInfo*)> function_;   ///< 功能函数
ThreadInfo info_;                             ///< 线程信息

std::mutex mtx_join_;                         ///< joinable锁

public:
/**
 * @brief 构造函数
 */
ThreadItem(std::string name,std::function<void(ThreadInfo*)> function) 
 : name_(std::move(name)) , function_(std::move(function)) {};

/**
 * @brief 运行线程
 * @throw 运行失败原因
 */
void run();

/**
 * @brief 结束进程
 * @note  需要内部函数写stop停止位置每一次读取
 */
void stop();

/**
 * @brief 进程是否正常运行
 * @return true  正在运行
 * @return false 没有运行
 */
bool isRun();

/**
 * @brief 构系函数
 */
~ThreadItem();

}; // class ThreadItem

/**
 * @brief 线程管理类
 */
class TOOLS_API ThreadController
{
private:
std::map<std::string,std::unique_ptr<ThreadItem>> thread_; // 线程集合 名称->线程

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
void addThread(std::string name,std::function<void(ThreadInfo*)> function);

/**
 * @brief  获取线程元素
 * @param  name 线程名称
 * @return ThreadItem& 线程元素引用
 * @throw  不存在线程
 */
ThreadItem& get(const std::string &name);

/**
 * @brief  []获得元素
 * @param  name 线程名称
 * @return ThreadItem& 线程元素引用
 * @throw  不存在线程
 */
ThreadItem& operator [](const std::string &name)
{
    return this->get(name);
}

/**
 * @brief  删除线程
 * @return true  删除成功
 * @return false 删除失败(线程还在跑/没有键)
 */
bool erase(const std::string &name);

/**
 * @brief 线程是否存在
 * @return false 不存在
 * @return true  存在
 */
bool exist(const std::string &name)
{
    auto e = this->thread_.find(name);
    if(e == this->thread_.end())return false;
    else return true;
}
};// class ThreadController

} // namespace sc::tools
#endif // THREADCONTROLLER_HPP