/**
 * @file        skillList.hpp
 * @brief       公共内存管理器
 * @author      o_O a17398481273@163.com
 * @date        2026-11-21
 */

#ifndef GROUP_SKILLLIST_HPP
#define GROUP_SKILLLIST_HPP

#include <memory>

#include "tools/loadskills.hpp"
#include "tools_api.hpp"
#include "tools/state.hpp"

namespace sc::group
{

/**
 * @brief 模式设置
 */
enum SkillListMode
{
    once = 0,    ///< 只需要运行一次                                           
    loop = 1     ///< 一直运行
};

/**
 * @brief skill链信息(新版)
 */
struct SkillListInfo
{
    std::string name;                                       ///< 名称(唯一ID)
    SkillListMode mode = SkillListMode::once;               ///< 模式 once / while
    int         interval = 1000;                            ///< while 轮间隔(毫秒)，0 = 不等待，<0 按 1000
    std::vector<tools::SkillInfo*> skills;                  ///< 技能列表(按顺序执行)
};

class TOOLS_API SkillList
{
private:
SkillListInfo info_;              ///< skill信息

public:
/**
 * @brief 默认构造
 */
SkillList(const std::string &name)
{
    info_.name = name;
}

/**
 * @brief  设置skills链
 * @param  name 名称链
 * @return true  成功设置
 * @return false 设置失败(没有skill/skill被使用/skill正在运行/未知错误)
 */
bool set(const std::string &name)
{
    this->info_.name = name;
}

/**
 * @brief 设置skills链
 * @param std::vector<std::string> list 名称链
 * @return true  成功设置
 * @return false 设置失败(没有skill/skill被使用/未知错误)
 */
bool set(const std::vector<std::string> &list);


/**
 * @brief  设置skills链
 * @param  name 名称链
 * @param  std::vector<std::string> list 名称链
 * @return true  成功设置
 * @return false 设置失败(没有skill/skill被使用/未知错误)
 */
bool set(const std::string &name,const std::vector<std::string> &list)
{
    this->set(name);
    this->set(list);
}

/**
 * @brief 构系函数
 */
~SkillList()
{
    clear();
}

/**
 * @brief 清除skill链
 */
void clear()
{
    for(auto &e:info_.skills)
    {
        e->used = false;
    }
}

/**
 * @brief 返回技能链信息
 */
SkillListInfo getInfo()
{
    return info_;
}

};

} // namespace sc::group

#endif