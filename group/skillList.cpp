#include "group/skillList.hpp"

/**
 * @brief 检查标志位并且退出的
 * @return false (正常退出)
 */
#define THREAD_CHECK_STOP(list)                         \
do {                                                    \
    std::lock_guard<std::mutex> lock((list)->mtx_stop); \
    if ((list)->stop) return false;                     \
} while (0)

/**
 * @brief 保存异常
 */
#define THREAD_SET_ERROR(msg)              \
do {                                       \
    std::lock_guard<std::mutex> lock(this->mtx_error); \
    this->error = (msg);                   \
} while (0)

using SteadyClock = std::chrono::steady_clock;

namespace sc::group
{

bool SkillList::set(const std::vector<std::string> &list)
{
    if(this->info_.thread->isRun())
    {
        return false;
    }

    std::set<std::string> total_now;

    for(auto e:this->info_.skills)
        total_now.insert(e->skill.name);

    /**
     * @brief 判断新加入的是否有被使用
     */
    for(auto &e:list)
    {
        if(total_now.find(e) == total_now.end())
        {
            auto item = tools::SC_ALL_SKILL_INFO.find(e);
            if(item == tools::SC_ALL_SKILL_INFO.end())
            {
                return false;
            }
            
            if((*item).second.used)
            {
                return false;
            }
        }
    }    

    for(auto e:this->info_.skills)
    {
        e->used = false;
    }  
    this->info_.skills.clear();

    /**
     * @brief 加入链
     */
    for(auto &e:list)
    {
        auto it = tools::SC_ALL_SKILL_INFO.find(e);
        auto &temp = it->second;
        temp.used = true;
        this->info_.skills.push_back(&(it->second));
    }    
    return true;
};

void SkillList::func(tools::ThreadInfo* input)
{
    const std::string name     = this->info_.name;                                     ///< 名称
    const bool        once     = (this->info_.mode == SkillListMode::once);            ///< 模式
    const int         interval = this->info_.interval > 0 ? this->info_.interval : 0;  ///< 预设时间
    const auto        period   = std::chrono::milliseconds(interval);                  ///< 预设时间(std::chrono::milliseconds)
    // 时间戳
    auto tick = SteadyClock::now();
    auto prev = tick;

    if (!runInit(input))
    {
        runEnd(input);
        return;
    }

    if(once)
    {
        runOnce(input);
    }
    else
    {
        while(1)
        {
            // 运行等待
            if (!sleepUntil(input, tick)) break;

            // 读取stop
            {
                std::lock_guard<std::mutex> lock(input->mtx_stop);
                if(input->stop)
                {
                    input->stop = false;
                    break;
                }
            }

            // 记录开始时间
            const auto roundStart = SteadyClock::now();

            // 运行一次链结构
            runOnce(input);

            // 记录结束时间
            const auto roundEnd = SteadyClock::now();

            // 计算时间差
            const int runOnceTime = std::chrono::duration_cast<std::chrono::milliseconds>(roundEnd - roundStart).count();
            prev = roundStart;

            this->time = runOnceTime;
            
            //logger()->info("[runthread] {} runOnce 耗时 {} ms", name, runOnceTime);
            // 判断时间
            if(runOnceTime < interval)
            {
                tick = roundStart + period;
            }
            else
            {
                tick = SteadyClock::now();
            }
        }
    }

    // 跑end()
    runEnd(input);

    // 修改stop状态-flase
    {
        std::lock_guard<std::mutex> lock(input->mtx_stop);
        input->stop = false;
    }   
}

bool SkillList::sleepUntil(tools::ThreadInfo *list,std::chrono::steady_clock::time_point until)
{
    constexpr auto step = std::chrono::milliseconds(50);

    while (true)
    {
        // 读一止标志
        THREAD_CHECK_STOP(list);

        const auto now = std::chrono::steady_clock::now();
        if (now >= until) return true;   

        const auto left = until - now;
        std::this_thread::sleep_for(left < step ? left : step);
    }
}

bool SkillList::runOnce(tools::ThreadInfo *list)
{
    try
    {
        for(auto e:this->info_.skills)
        {
            e->loadskill.runOnce();
        }
    }
    catch(const std::exception& e)
    {
        tools::logger()->error("[runskills] {} 链运行错误 : {}",this->info_.name,e.what());
        THREAD_SET_ERROR(e.what());
        return false;
    }   
    catch(...)
    {
        tools::logger()->error("[runskills] {} 链运行错误 : 位置错误",this->info_.name);
        return false;
    }
    return true;
}

bool SkillList::runInit(tools::ThreadInfo *list)
{
    try
    {
        for(auto e:this->info_.skills)
        {
            e->loadskill.load(this->info_.mem);
            THREAD_CHECK_STOP(list);
        }
    }
    catch(const std::exception& e)
    {
        tools::logger()->error("[runskills] {} 链init运行错误 : {}",this->info_.name,e.what());
        THREAD_SET_ERROR(e.what());
        return false;
    }     
    catch (...)
    {
        tools::logger()->error("[runskills] {} 链init运行错误 : 未知错误",this->info_.name);
        return false;
    } 
    return true;
}

bool SkillList::runEnd(tools::ThreadInfo *list)
{
    bool res = true;
    for (auto& e : this->info_.skills)
    {
        try
        {
            e->loadskill.end();
        }
        catch (const std::exception& ed)
        {
            tools::logger()->error("[runskills] {} 技能 {} end 错误 : {}", this->info_.name,e->skill.name, ed.what());
            THREAD_SET_ERROR(ed.what());
            res = false;
        }
        catch (...)
        {
            tools::logger()->error("[runskills] {} 技能 {} end 错误 : 未知错误", this->info_.name,e->skill.name);
            res = false;
        }
    }
    return res;
}

} // namespace sc::group