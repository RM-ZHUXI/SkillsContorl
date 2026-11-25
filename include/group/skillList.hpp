/**
 * @file        skillList.hpp
 * @brief       公共内存管理器
 * @author      o_O a17398481273@163.com
 * @date        2026-11-25
 */

#ifndef GROUP_SKILLLIST_HPP
#define GROUP_SKILLLIST_HPP

#include "tools/loadskills.hpp"
#include "tools_api.hpp"
#include "tools/state.hpp"
#include "memory.hpp"

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
    int         interval = 1000;                            ///< while 轮间隔(毫秒)，0 = 不等待，<0 按 0
    std::vector<tools::SkillInfo*> skills;                  ///< 技能列表(按顺序执行)
    
    std::string threadName;                                 ///< 线程池所对应的名称/如果后面reset了name但是threadName不变
    tools::ThreadItem *thread = nullptr;                    ///< 指向线程

    memory *mem = nullptr;                                  ///< 公共内存池
};

class TOOLS_API SkillList
{
private:
SkillListInfo info_;              ///< skill信息

int         time  = 0;             ///< 时间管理器
std::string error;                ///< 上一次错误情况
std::mutex  mtx_error;            ///< 错误情况互斥锁     
private:
/**
 * @brief 线程功能函数
 */
void func(tools::ThreadInfo* input);

/**
 * @brief 睡到指定时刻（可被 stop 打断）
 * @param list     线程信息
 * @param until    目标时刻(steady_clock)
 * @return true 正常睡到点；false 被 stop 打断
 */
static bool sleepUntil(tools::ThreadInfo *list,std::chrono::steady_clock::time_point until);

/**
 * @brief  跑一轮
 * @param  list     线程信息
 * @return true 正常运行 false run异常记录/停止线程
 * @note   运行chain中的所有run()
 */
bool runOnce(tools::ThreadInfo *list);

/**
 * @brief  跑chain链的init
 * @param  list     线程信息
 * @return true 正常运行 false run异常记录logger但是不会影响主线程
 * @note   运行chain中的所有init()
 */
bool runInit(tools::ThreadInfo *list);

/**
 * @brief  跑chain链的end
 * @param  list     线程信息
 * @return true 正常运行 false run异常记录logger但是不会影响主线程
 * @note   运行chain中的所有end()
 */
bool runEnd(tools::ThreadInfo *list);

public:
/**
 * @brief 默认构造
 * @param name list名称
 * @param mem  memory公共内存
 * @note  skillList加入线程池统一名称为"list.xxx"
 */
SkillList(const std::string &name,memory *mem)
{
    info_.name = name;
    info_.mem = mem;

    std::string thname = "list." + name;
    info_.threadName = thname;
    tools::threadController.addThread(thname,
        [this](tools::ThreadInfo* input)
        {
            this->func(input);
        });
    this->info_.thread = &tools::threadController[thname];
}

/**
 * @brief  设置skills链
 * @param  name 名称链
 * @return true  成功设置
 * @return false 设置失败(没有skill/skill被使用/skill正在运行/未知错误)
 */
bool set(const std::string &name)
{
    if(this->isRun())
    {
        return false;
    }

    this->info_.name = name;
    return true;
}

/**
 * @brief  设置skills链
 * @param  std::vector<std::string> list 名称链
 * @return true  成功设置
 * @return false 设置失败(没有skill/skill被使用/未知错误)
 * @note   没有做线程防护
 */
bool set(const std::vector<std::string> &list);


/**
 * @brief 构系函数
 * @note  删除线程池对象
 */
~SkillList()
{
    if (info_.thread)info_.thread->stop();
    clear();
    tools::threadController.erase(info_.threadName);
    info_.thread = nullptr;
}

/**
 * @brief 清除skill链，但是不删除线程池对象
 */
bool clear()
{
    if(this->info_.thread->isRun())
    {
        return;
    }
    for(auto &e:info_.skills)
    {
        e->used = false;
    }
    this->info_.skills.clear();
}

/**
 * @brief 返回技能链信息
 */
SkillListInfo& getInfo()
{
    return info_;
}

/**
 * @brief 运行当前链
 */
void run()
{
    this->info_.thread->run();
}

/**
 * @brief 停止链
 */
void stop()
{
    this->info_.thread->stop();
}

/**
 * @brief 是否正在运行
 * @return true  正在运行
 * @return false 没有运行 
 */
bool isRun()
{
    return this->info_.thread->isRun();
}

};

} // namespace sc::group

#endif