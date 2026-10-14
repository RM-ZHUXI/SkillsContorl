/**
 * @file main.hpp
 * @brief camera_test 技能：把公共内存里的画面按需开窗显示出来
 * @author o_O a17398481273@163.com
 * @date 2026/10/14
 * @version 1.5.0
 */

#ifndef CAMERA_TEST_MAIN_HPP
#define CAMERA_TEST_MAIN_HPP

#include "tools/memory.hpp"

// ---- 技能导出宏 ----
#if defined(_WIN32)
    #define SKILL_API extern "C" __declspec(dllexport)
#else
    #define SKILL_API extern "C" __attribute__((visibility("default")))
#endif

/**
 * @brief 技能初始化（只读配置，不创建 GUI）
 * @param memory 由 skillcontorl 注入的公共内存管理器
 */
SKILL_API void init(sc::tools::Memory *memory);

/**
 * @brief 每轮调用：按需启动显示 + 投递一帧
 */
SKILL_API void run();

/**
 * @brief 收尾：暂停显示（不关窗、不销毁 Displayer）
 */
SKILL_API void end();

#endif // CAMERA_TEST_MAIN_HPP