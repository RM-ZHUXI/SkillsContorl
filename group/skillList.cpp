#include "group/skillList.hpp"

namespace sc::group
{

bool SkillList::set(const std::vector<std::string> &list)
{
    /**
     * @brief 当前线程是否正在运行
     */
    std::string thread_name = "list." + this->info_.name;
    if(tools::threadController.exist(thread_name))
    {
        if(tools::threadController[thread_name].isRun())
        {
            return false;
        }
    }

    /**
     * @brief 置位
     */
    for(auto &e:this->info_.skills)
    {
        e->used = false;
    }    

    /**
     * @brief 判断新加入的是否有被使用
     */
    for(auto &e:list)
    {
        auto it = tools::SC_ALL_SKILL_INFO.find(e);
        if (it != tools::SC_ALL_SKILL_INFO.end()) {
            auto &temp = it->second;
            if(temp.used)return false;
        }
        else
        {
            return false;
        }
    }    

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
};

} // namespace sc::group