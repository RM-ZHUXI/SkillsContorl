#include "main.hpp"

#include "tools/logger.hpp"
#include "tools/yaml.hpp"
#include "math_tools.hpp"
#include "solver.hpp"

namespace
{
    sc::tools::Memory *mem = nullptr;
    std::unique_ptr<autoAim::Solver> solver;

}

/**
 * @brief 程序初始化：只读配置，不创建 GUI
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[camera_test] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;


    auto yaml = sc::tools::load("skills/predict/config.yaml");
    std::string commonConfig = sc::tools::read<std::string>(yaml,"comom_config_path");
    
    solver = std::make_unique<autoAim::Solver>(commonConfig);
    
    mem->insertOrAssign<std::vector<float>>("gimbal.g_q",std::vector<float>(4,0.0f));

}

SKILL_API void run()
{
    std::vector<float> q = mem->get<std::vector<float>>("gimbal.g_q");

    solver->set_R_gimbal2world(q);

    Eigen::Vector3d gimbal_pos = tools::eulers(solver->R_gimbal2world(), 2, 1, 0);
}

SKILL_API void end()
{

}