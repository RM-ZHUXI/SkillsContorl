/**
 * @file        tools_api.hpp
 * @brief       libtools.dll 跨动态库导入导出宏
 * @author      o_O a17398481273@163.com
 * @date        2026-09-11
 */

#ifndef TOOLS_API_HPP
#define TOOLS_API_HPP

/**
 * @brief 跨动态库导入导出宏
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
