/**
 * @file main.hpp
 * @brief camera_test 技能入口声明
 * @author 时新博 a17398481273@163.com
 * @date 2026/10/07
 * @version 1.4.0
 *
 * @par v1.4.0 关键变化
 *      导出宏统一改用框架 include/skillscontrol.hpp 里的 SKILL_API。
 *      旧版本自己手写了一份 `#ifdef CAMERA_TEST_EXPORTS` 的判断：虽然 CMake 给目标
 *      CAMERA_TEST 自动定义的宏恰好就是 CAMERA_TEST_EXPORTS（所以一直没出事），
 *      但只要改了目标名、或者换个构建系统，SKILL_API 就会退化成 __declspec(dllimport)，
 *      编出来的 DLL 里导出名不再是干净的 "init"，宿主会报「动态库缺少 init 导出符号」。
 *      两个技能现在写法一致，也不用再各自维护一份导出宏。
 */

#ifndef SKILL_CAMERA_TEST_MAIN_HPP
#define SKILL_CAMERA_TEST_MAIN_HPP

#include "skillscontrol.hpp"     // SKILL_API(extern "C" + dllexport/visibility) + sc::tools::Memory

// 注意：SKILL_API 自带 extern "C"，所以下面不要再套一层 extern "C" { }

/**
 * @brief 技能初始化：只读配置，不创建 GUI
 * @param memory 宿主注入的公共内存
 * @note  签名必须与宿主加载器约定一致
 */
SKILL_API void init(sc::tools::Memory *memory);

/**
 * @brief 每轮调用一次：按需启动显示线程 + 投递一帧
 * @note  一次调用只干一件事，干完立刻返回；循环由技能链负责
 */
SKILL_API void run();

/**
 * @brief 收尾：停止显示线程、关窗口、销毁 Displayer
 */
SKILL_API void end();

#endif // SKILL_CAMERA_TEST_MAIN_HPP
