#include "main.hpp"

#include "tools/logger.hpp"
#include "img_tools.hpp"
#include "armor.hpp"

namespace
{
    sc::tools::Memory *mem = nullptr;
    std::unique_ptr<autoAim::YOLO> detect;
    bool debug = false;
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
    auto yaml = sc::tools::load("skills/detect/config.yaml");
    debug = sc::tools::read<bool>(yaml,"debug");

    mem = memory;
    detect = std::make_unique<autoAim::YOLO>("skills/detect/config.yaml", true);

    mem->insertOrAssign<int>("autoAim.frame",0);
    
    if(debug)
    {
        mem->insertOrAssign("autoAim.debug",cv::Mat());
    }
}

SKILL_API void run()
{
    cv::Mat img = mem->get<cv::Mat>("image");
    int fnow = mem->get<int>("autoAim.frame");
    fnow++;
    auto armor = detect->detect(img,fnow);
    mem->insertOrAssign("autoAim.frame",fnow);
    if(debug)
    {
        for (const auto & armor : armor) {
            tools::draw_points(img, armor.points, {0, 255, 0}, 2);
            tools::draw_text(img, autoAim::ARMOR_NAMES[armor.name], armor.center, {0, 255, 0});
        }
        mem->insertOrAssign("autoAim.debug",img);
    }
}

SKILL_API void end()
{
    detect.reset(); 
}