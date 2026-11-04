/**
 * @file main.hpp
 * @brief detect 识别装甲板
 * @author o_O a17398481273@163.com
 * @date 2026/11/01
 * @version 1.0.0
 * @note 正常使用识别程序需要gimbal(下位机程序)程序
 */

#ifndef MAIN_HPP
#define MAIN_HPP

#include "tools/memory.hpp"
#include "yolo.hpp"

#if defined(_WIN32)
    #define SKILL_API extern "C" __declspec(dllexport)
#else
    #define SKILL_API extern "C" __attribute__((visibility("default")))
#endif

/**
 * @brief 技能初始化
 * @param memory 由 skillcontorl 注入的公共内存管理器
 */
SKILL_API void init(sc::tools::Memory *memory);

/**
 * @brief 每轮调用
 */
SKILL_API void run();

/**
 * @brief 收尾
 */
SKILL_API void end();

#endif // MAIN_HPP