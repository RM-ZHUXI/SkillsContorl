/**
 * @file main.hpp
 * @brief gimbal 技能入口声明
 * @author o_O a17398481273@163.com
 * @date 2026/10/20
 * @version 1.0.0
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