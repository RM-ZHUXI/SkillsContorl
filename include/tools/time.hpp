/**
 * @file        time.hpp
 * @brief       时间工具类
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-08
 * @version     1.0.0
 */

#ifndef TIME_HPP
#define TIME_HPP

#include <chrono>
#include <ctime>
#include <string>
#include <sstream>
#include <iomanip>

#include "tools/tools_api.hpp"

namespace sc::tools
{

/**
 * @brief 时间工具类
 */
class TOOLS_API Time
{
private:
    long long last_time_ms = 0; ///< 上一次时间戳ms

public:
    /**
     * @brief 构造函数
     */
    Time()
    {
        last_time_ms = nowms();
    }

    /**
     * @brief 获取当前时间戳（秒级）
     * @return long long 当前时间戳（从1970-01-01 00:00:00 UTC开始的秒数）
     */
    long long nows();

    /**
     * @brief 获取当前时间戳(毫秒级)
     * @return long long 当前时间戳(ms)
     */
    long long nowms();

    /**
     * @brief 获取当前时间戳(微秒级)
     * @return long long 当前时间戳(us)
     */
    long long nowus();

    /**
     * @brief 获取当前时间戳(纳秒级)
     * @return long long 当前时间戳(ns)
     */
    long long nowns();

    /**
     * @brief 获取当前日期
     * @return std::string 当前日期(xxxx-xx-xx)
     */
    std::string nowDate();

    /**
     * @brief 获取当前时间
     * @return std::string 当前时间(xx:xx:xx)
     */
    std::string nowTime();

    /**
     * @brief 清理定时器
     */
    void clear();

    /**
     * @brief 得到毫秒表时间并且重置毫秒表
     * @return long long 上一次调用时间时间戳(ms)
     * @note  第一次调用返回定时器开始时间于调用时间差，之后每次调用返回间隔时间(ms)
     */
    long long getTime();

};// class time

} // namespace tools

#endif