/**
 * @file        state.hpp
 * @brief       系统级状态量
 * @author      o_O a17398481273@163.com
 *
 * @date        2026-11-16
 * @pre
 * init()初始化撰写
 */
#ifndef TOOLS_STATE_HPP
#define TOOLS_STATE_HPP

#include "tools_api.hpp"
#include "type.hpp"
#include "findskills.hpp"
#include "logger.hpp"

#include <map>
#include <string>

namespace sc::tools
{
/**
 * @brief 固定参数
 */
const std::string CONFIG = "configs/config.yaml";                  ///< 配置文件路径

/**
 * @brief 状态量
 */
extern bool TOOLS_API SC_ALL_SO_READY;                                ///< 所有SKILL是否载入准备完毕
extern std::map<std::string,SkillInfo> TOOLS_API SC_ALL_SKILL_INFO;   ///< 所有SKILL信息 名称->SkillInfo

/**
 * @brief 功能类
 */
extern FindSkills findSkills(CONFIG);

/**
 * @brief 刷新skill方法
 * @par
 * 直接写入 SC_ALL_SKILL_INFO,并且对于新的skill加载到内存
 */
void updateSkill();

} // namespace sc::tools

#endif // TOOLS_STATE_HPP