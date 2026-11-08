#include "main.hpp"

#include <nlohmann/json.hpp>
#include <string>
#include <list>
#include <vector>
#include <memory>

#include "tools/logger.hpp"
#include "img_tools.hpp"
#include "armor.hpp"

namespace
{
    sc::tools::Memory *mem = nullptr;
    std::unique_ptr<autoAim::YOLO> detect;
    bool debug = false;

    // 序列化：将 std::list<Armor> 转成 JSON 字符串
    std::string serialize_armors(const std::list<autoAim::Armor>& armors) {
        nlohmann::json j_array = nlohmann::json::array();
        
        for (const auto& a : armors) {
            nlohmann::json j_armor;
            j_armor["class_id"]   = a.class_id;
            j_armor["confidence"] = a.confidence;
            
            // 存枚举的整型值
            j_armor["color"]      = static_cast<int>(a.color);
            j_armor["name"]       = static_cast<int>(a.name);
            j_armor["type"]       = static_cast<int>(a.type);
            j_armor["priority"]   = static_cast<int>(a.priority);
            
            // 存几何数据
            j_armor["center"] = {a.center.x, a.center.y};
            j_armor["box"]    = {a.box.x, a.box.y, a.box.width, a.box.height};
            
            nlohmann::json j_points = nlohmann::json::array();
            for (const auto& p : a.points) {
                j_points.push_back({p.x, p.y});
            }
            j_armor["points"] = j_points;

            j_array.push_back(j_armor);
        }
        
        return j_array.dump(); // 压缩成字符串
    }

    // 反序列化：将 JSON 字符串转成 std::list<Armor>
    std::list<autoAim::Armor> deserialize_armors(const std::string& str) {
        std::list<autoAim::Armor> armors;
        if (str.empty() || str == "[]") return armors;

        // 安全解析，不抛出异常
        auto j_array = nlohmann::json::parse(str, nullptr, false);
        if (j_array.is_discarded() || !j_array.is_array()) {
            return armors; 
        }

        for (const auto& j_armor : j_array) {
            int class_id = j_armor["class_id"].get<int>();
            float conf   = j_armor["confidence"].get<float>();
            
            cv::Rect box(
                j_armor["box"][0].get<int>(), 
                j_armor["box"][1].get<int>(), 
                j_armor["box"][2].get<int>(), 
                j_armor["box"][3].get<int>()
            );
            
            std::vector<cv::Point2f> points;
            for (const auto& j_p : j_armor["points"]) {
                points.emplace_back(j_p[0].get<float>(), j_p[1].get<float>());
            }

            // 使用 Armor 已有的构造函数（避免无默认构造的问题）
            autoAim::Armor armor(class_id, conf, box, points);

            // 覆盖属性（防止构造函数的内部查表逻辑与源数据有出入）
            armor.color    = static_cast<autoAim::Color>(j_armor["color"].get<int>());
            armor.name     = static_cast<autoAim::ArmorName>(j_armor["name"].get<int>());
            armor.type     = static_cast<autoAim::ArmorType>(j_armor["type"].get<int>());
            armor.priority = static_cast<autoAim::ArmorPriority>(j_armor["priority"].get<int>());
            armor.center   = cv::Point2f(j_armor["center"][0].get<float>(), j_armor["center"][1].get<float>());

            armors.push_back(armor);
        }

        return armors;
    }
} // namespace


SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr) {
        sc::tools::logger()->error("[detect] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;
    auto yaml = sc::tools::load("skills/detect/config.yaml");
    debug = sc::tools::read<bool>(yaml, "debug");

    detect = std::make_unique<autoAim::YOLO>("skills/detect/config.yaml", debug);

    mem->insertOrAssign<int>("detect.frame", 0);

    mem->insertOrAssign<std::string>("detect.result", "[]");

    if (debug) {
        mem->insertOrAssign("detect.debug", cv::Mat());
    }
}

SKILL_API void run()
{
    if (!mem || !detect) return;

    cv::Mat img = mem->get<cv::Mat>("image");
    int fnow = mem->get<int>("detect.frame");
    fnow++;

    auto armors = detect->detect(img, fnow); 
    
    mem->insertOrAssign("detect.frame", fnow);

    std::string result_str = serialize_armors(armors);
    mem->insertOrAssign<std::string>("detect.result", result_str);

    if (debug) {
        cv::Mat vis = img.clone();
        for (const auto & a : armors) {
            tools::draw_points(vis, a.points, {0, 255, 0}, 2);
            tools::draw_text(vis, autoAim::ARMOR_NAMES[a.name], a.center, {0, 255, 0});
        }
        mem->insertOrAssign("detect.debug", vis);
    }
}

SKILL_API void end()
{
    detect.reset(); 
}