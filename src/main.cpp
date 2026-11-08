#include "main.hpp"

#include "tools/logger.hpp"
#include "tools/yaml.hpp"
#include "math_tools.hpp"
#include "solver.hpp"
#include "tracker.hpp"

namespace
{
    sc::tools::Memory *mem = nullptr;
    std::unique_ptr<autoAim::Solver> solver;
    std::unique_ptr<autoAim::Tracker> tracker;
    std::chrono::steady_clock::time_point t = std::chrono::steady_clock::now();

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

    // 序列化：std::list<Target> -> std::string
    std::string serialize_targets(const std::list<autoAim::Target>& targets, std::chrono::steady_clock::time_point t) {
        nlohmann::json j_array = nlohmann::json::array();

        // 获取当前时间戳（微秒），传给下游重建时用
        auto t_us = std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();

        for (const auto& tgt : targets) {
            nlohmann::json j_tgt;
            
            // 1. 基本属性
            j_tgt["name"]       = static_cast<int>(tgt.name);
            j_tgt["armor_type"] = static_cast<int>(tgt.armor_type);
            j_tgt["priority"]   = static_cast<int>(tgt.priority);
            j_tgt["jumped"]     = tgt.jumped;
            j_tgt["armor_num"]  = tgt.armor_num();
            j_tgt["t_us"]       = t_us;

            // 2. EKF 状态向量 x (11维)
            Eigen::VectorXd x = tgt.ekf_x();
            std::vector<double> x_vec(x.data(), x.data() + x.size());
            j_tgt["ekf_x"] = x_vec;

            // 3. EKF 协方差对角线 P (11维，Target 构造函数只传对角阵)
            Eigen::VectorXd p_diag = tgt.ekf().P.diagonal();
            std::vector<double> p_vec(p_diag.data(), p_diag.data() + p_diag.size());
            j_tgt["ekf_P_diag"] = p_vec;

            j_array.push_back(j_tgt);
        }
        return j_array.dump();
    }

    // 反序列化：std::string -> std::list<Target>（下游用）
    std::list<autoAim::Target> deserialize_targets(const std::string& str) {
        std::list<autoAim::Target> targets;
        if (str.empty() || str == "[]") return targets;

        auto j_array = nlohmann::json::parse(str, nullptr, false);
        if (j_array.is_discarded() || !j_array.is_array()) return targets;

        for (const auto& j_tgt : j_array) {
            int name_id   = j_tgt["name"].get<int>();
            int type_id   = j_tgt["armor_type"].get<int>();
            int prio_id   = j_tgt["priority"].get<int>();
            bool jumped   = j_tgt["jumped"].get<bool>();
            int armor_num = j_tgt["armor_num"].get<int>();
            long long t_us= j_tgt["t_us"].get<long long>();

            // 恢复 EKF x
            std::vector<double> x_vec = j_tgt["ekf_x"].get<std::vector<double>>();
            Eigen::VectorXd ekf_x(x_vec.size());
            for (size_t i = 0; i < x_vec.size(); ++i) ekf_x[i] = x_vec[i];

            // 恢复 EKF P
            std::vector<double> p_vec = j_tgt["ekf_P_diag"].get<std::vector<double>>();
            Eigen::VectorXd p_diag(p_vec.size());
            for (size_t i = 0; i < p_vec.size(); ++i) p_diag[i] = p_vec[i];
            Eigen::MatrixXd ekf_P = p_diag.asDiagonal();

            // 恢复时间戳
            auto t = std::chrono::steady_clock::time_point(std::chrono::microseconds(t_us));

            // 调用 Target 的重建构造函数
            autoAim::Target tgt(
                ekf_x, ekf_P,
                static_cast<autoAim::ArmorName>(name_id),
                static_cast<autoAim::ArmorType>(type_id),
                static_cast<autoAim::ArmorPriority>(prio_id),
                jumped, armor_num, t, 100
            );
            targets.push_back(tgt);
        }
        return targets;
    }
    
    // time_point 转 long long (微秒)
    inline long long time_point_to_longlong(std::chrono::steady_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
    }

    // long long (微秒) 转 time_point
    inline std::chrono::steady_clock::time_point longlong_to_time_point(int64_t us) {
        return std::chrono::steady_clock::time_point(std::chrono::microseconds(us));
    }

}

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
    tracker = std::make_unique<autoAim::Tracker>("skills/predict/config.yaml",*solver);

    /**
     * @brief 公共内存输入
     * 
     */
    mem->insertOrAssign<std::vector<float>>("gimbal.g_q",std::vector<float>(4,0.0f));
    mem->insertOrAssign<long long>("gimbal.t",0);
    mem->insertOrAssign<std::string>("detect.result","[]");
    mem->insertOrAssign<std::string>("predict.result","");
}

SKILL_API void run()
{
    std::list<autoAim::Armor> armors = deserialize_armors(mem->get<std::string>("detect.result"));

    std::vector<float> q = mem->get<std::vector<float>>("gimbal.g_q");

    /**
     * @brief 时间更新
     * 
     */
    std::chrono::steady_clock::time_point t_now = longlong_to_time_point(mem->get<long long>("gimbal.t"));
    if(t_now <= t)t = std::chrono::steady_clock::now();
    else t = t_now;

    solver->set_R_gimbal2world(q);

    Eigen::Vector3d gimbal_pos = tools::eulers(solver->R_gimbal2world(), 2, 1, 0);

    std::list<autoAim::Target> targets = tracker->track(armors, t);

    mem->insertOrAssign<std::string>("predict.result",serialize_targets(targets,t));

}

SKILL_API void end()
{
    solver.reset();
    tracker.reset();
}