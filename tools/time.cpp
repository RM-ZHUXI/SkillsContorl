/**
 * @file        time.cpp
 * @brief       时间工具类实现类
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-08
 * @version     1.0.0
 */


#include "tools/time.hpp"

namespace sc::tools
{
    long long Time::nows()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
            now.time_since_epoch()
        ).count();
        return timestamp;
    }
    long long Time::nowms()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()
        ).count();
        return timestamp;
    }

    long long Time::nowus()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count();
        return timestamp;
    }

    long long Time::nowns()
    {
        auto now = std::chrono::system_clock::now();
        auto timestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
            now.time_since_epoch()
        ).count();
        return timestamp;
    }

    std::string Time::nowDate()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        
        std::tm tm_now;
        #ifdef _WIN32
                localtime_s(&tm_now, &time_t_now);
        #else
                localtime_r(&time_t_now, &tm_now);
        #endif
        
        std::stringstream ss;
        ss << std::put_time(&tm_now, "%Y-%m-%d");
        return ss.str();
    }

    std::string Time::nowTime()
    {
        auto now = std::chrono::system_clock::now();
        auto time_t_now = std::chrono::system_clock::to_time_t(now);
        
        std::tm tm_now;
        #ifdef _WIN32
                localtime_s(&tm_now, &time_t_now);
        #else
                localtime_r(&time_t_now, &tm_now);
        #endif
        
        std::stringstream ss;
        ss << std::put_time(&tm_now, "%H:%M:%S");
        return ss.str();
    }

    void Time::clear()
    {
        last_time_ms = 0;
    }

    long long Time::getTime()
    {
        long long current_time_ms = nowms();
        long long res = current_time_ms - last_time_ms;
        last_time_ms = current_time_ms;
        return res;
    }
}