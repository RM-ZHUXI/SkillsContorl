#include "tools/state.hpp"

namespace sc::tools
{
    bool SC_ALL_SO_READY = false;
    std::map<std::string,SkillInfo> SC_ALL_SKILL_INFO;

    void updateSkill()
    {
        try
        {
            findSkills.update();
            for(auto &e: findSkills.get_skills())
            {
                try
                {
                    if(SC_ALL_SKILL_INFO.find(e.second.name) == SC_ALL_SKILL_INFO.end())
                    {
                        SC_ALL_SKILL_INFO[e.second.name].skill = std::move(e.second);
                        SC_ALL_SKILL_INFO[e.second.name].reload();
                    }                    
                }
                catch(const std::exception& e)
                {
                    logger()->error(std::string(e.what()));
                }
            }
        }
        catch(const std::exception& e)
        {
            logger()->error("[State] skill状态更新失败 : " + std::string(e.what()));
        }
    }

    

}