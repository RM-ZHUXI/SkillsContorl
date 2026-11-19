#include "tools/state.hpp"

namespace sc::tools
{
    bool SC_ALL_SO_READY = false;
    std::map<std::string,SkillInfo> SC_ALL_SKILL_INFO;
    
    ThreadController threadController;
    FindSkills findSkills(CONFIG);

    void updateSkill()
    {
        try
        {
            SC_ALL_SO_READY = false;
            findSkills.update();
            for (auto &e : findSkills.get_skills())
            {
                try
                {
                    const std::string name = e.second.name;      

                    if (name.empty())                           
                    {
                        logger()->warn("[updageSkill] 跳过无名 skill");
                        continue;
                    }

                    auto it = SC_ALL_SKILL_INFO.find(name);         
                    if (it != SC_ALL_SKILL_INFO.end()) continue;     

                    auto res = SC_ALL_SKILL_INFO.emplace(name, SkillInfo{});
                    res.first->second.skill = std::move(e.second);  
                    res.first->second.reload();
                }
                catch (const std::exception& e)
                {
                    logger()->error(std::string(e.what()));
                }
            }


            SC_ALL_SO_READY = true;
            for(auto &e:SC_ALL_SKILL_INFO)
            {
                if(!e.second.load)
                {
                    SC_ALL_SO_READY = false;
                    break;
                }
            }
        }
        catch(const std::exception& e) // 主要是update();
        {
            logger()->error("[State] skill状态更新失败 : " + std::string(e.what()));
        }
    }


}