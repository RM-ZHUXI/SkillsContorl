/**
 * @file        tools_api.hpp
 * @brief       libtools.dll 跨动态库导入导出宏
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-11
 * @version     1.0.0
 */

#ifndef TOOLS_API_HPP
#define TOOLS_API_HPP

/**
 * @brief 跨动态库导入导出宏
 *
 * @note  libtools.dll 自己编译时由 CMake 定义 TOOLS_EXPORTS（见 tools/CMakeLists.txt），
 *        走 dllexport；exe / net / skills 编译时是 dllimport。
 *
 * @warning 一旦某个目标里出现了显式 dllexport，MinGW 的"自动导出全部符号"
 *          (--export-all-symbols) 就会被关掉。所以**凡是需要跨 libtools.dll
 *          使用的类/函数都必须挂这个宏**，漏掉就会在启动时报
 *          "找不到程序入口点 xxx"。
 */
#if defined(_WIN32)
    #if defined(TOOLS_EXPORTS)
        #define TOOLS_API __declspec(dllexport)
    #else
        #define TOOLS_API __declspec(dllimport)
    #endif
#else
    #define TOOLS_API __attribute__((visibility("default")))
#endif

#endif // TOOLS_API_HPP
