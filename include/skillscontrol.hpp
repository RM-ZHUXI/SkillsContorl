/**
 * @file        skillscontrol.hpp
 * @brief       技能侧统一入口头文件
 * @note        技能 DLL 只需要 Memory 这一个跨库接口。
 *
 *              不要在这里 include "tools/tools.hpp"：那会把 logger/spdlog、
 *              yaml-cpp、opencv 一起拖进技能 DLL —— 技能 DLL 因此需要自己链接
 *              fmt/spdlog（否则链接期报 fmt::v12::vformat 未定义），
 *              而以前之所以"看起来能编过"，是因为 libtools.dll 当时自动导出了
 *              全部符号（含 fmt/libstdc++ 内部符号），技能是从那儿蹭到的，
 *              这种隐式耦合一旦 libtools 改成显式导出就会立刻断掉。
 *
 *              技能如果确实要用日志/配置，请显式包含对应头文件，并在技能的
 *              CMakeLists.txt 里补上 fmt::fmt / spdlog::spdlog 等依赖。
 */

#ifndef SKILLSCONTROL_HPP
#define SKILLSCONTROL_HPP

/**
 * @brief       技能侧统一入口头文件
 * @note        init run end 函数均需要SKILL_API 修饰
 */

#ifdef _WIN32
    #define SKILL_API extern "C" __declspec(dllexport)
#else
    #define SKILL_API extern "C" __attribute__((visibility("default")))
#endif

#include "tools/tools.hpp"
#include "net/net.hpp"

#endif // SKILLSCONTROL_HPP
