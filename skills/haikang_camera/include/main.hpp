/**
 * @file main.hpp
 * @brief haikang_camera 技能入口声明
 * @author 时新博 a17398481273@163.com
 * @date 2026/10/07
 * @version 1.0.5
 *
 * @par 为什么直接复用框架的 SKILL_API
 *      include/skillscontrol.hpp 里已经把技能入口该有的东西定义好了：
 *          Windows : extern "C" __declspec(dllexport)
 *          其它    : extern "C" __attribute__((visibility("default")))
 *      技能自己再手写一份导出宏，就必须自己判断"我是在编 DLL 还是在用 DLL"，
 *      而这个目标由 CMake 自动定义的宏是 HAIKANG_CAMERA_EXPORTS（= <目标名>_EXPORTS）。
 *      旧版本的这份头文件是从 camera_test 抄过来的，判的却是 CAMERA_TEST_EXPORTS，
 *      于是 SKILL_API 会退化成 __declspec(dllimport)：一旦它被 include 到定义 init()
 *      的那个 .cpp 里，导出的符号就不再是干净的 "init"，宿主 LoadSkills 会报
 *      「动态库缺少 init 导出符号」。这里统一走框架的宏，避免再踩。
 */

#ifndef SKILL_HAIKANG_CAMERA_MAIN_HPP
#define SKILL_HAIKANG_CAMERA_MAIN_HPP

#include "skillscontrol.hpp"     // SKILL_API(extern "C" + dllexport/visibility) + sc::tools::Memory

// 注意：SKILL_API 自带 extern "C"，所以下面不要再套一层 extern "C" { }
// （套了也能编过，但重复声明容易让人误以为导出是由外层块决定的）

/**
 * @brief 技能初始化：定位配置 + 打开相机 + 开始采集
 * @param memory 宿主注入的公共内存
 * @note  相机打不开时本函数抛异常，宿主会把整条技能链标记为启动失败
 */
SKILL_API void init(sc::tools::Memory *memory);

/**
 * @brief 每轮调用一次：取一帧写进公共内存的 "image" 键
 * @note  一次调用只干一件事，干完立刻返回；循环由技能链的 mode=while + interval 负责
 */
SKILL_API void run();

/**
 * @brief 收尾：停止采集并释放设备句柄
 */
SKILL_API void end();

#endif // SKILL_HAIKANG_CAMERA_MAIN_HPP
