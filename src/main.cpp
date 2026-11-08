#include "main.hpp"              // init / run / end 声明（同时带来 SKILL_API、Memory）

#include "gimbal.hpp"
#include "tools/logger.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>

namespace
{
    sc::tools::Memory *mem = nullptr;

    std::unique_ptr<skill::board::Gimbal> gimbal;

    std::chrono::steady_clock::time_point t;

    // 控制周期节流（50Hz）
    std::chrono::steady_clock::time_point last_ctrl_time;
    constexpr double kCtrlPeriodSec   = 0.02;
    constexpr float  kMaxAngularSpeed = 0.2f;

    inline long long time_point_to_longlong(std::chrono::steady_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
    }

    inline std::chrono::steady_clock::time_point longlong_to_time_point(int64_t us) {
        return std::chrono::steady_clock::time_point(std::chrono::microseconds(us));
    }
}

/**
 * @brief 程序初始化
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[gimbal] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;

    mem->insertOrAssign<int>("gimbal.mode", 0);
    mem->insertOrAssign<int>("gimbal.error", 0);
    mem->insertOrAssign<std::vector<float>>("gimbal.g_q", std::vector<float>(4, 0));

    mem->insertOrAssign<float>("gimbal.yaw_v", 0);
    mem->insertOrAssign<float>("gimbal.pitch_v", 0);

    mem->insertOrAssign<int>("gimbal.fire_rpm", 0);
    mem->insertOrAssign<int>("gimbal.fire_count", 0);

    mem->insertOrAssign<std::vector<float>>("gimbal.c_q", std::vector<float>(4, 0));
    mem->insertOrAssign<float>("gimbal.speed", 0);

    mem->insertOrAssign<bool>("gimbal.online", false);

    mem->insertOrAssign<double>("aim.dyaw", 0.0);
    mem->insertOrAssign<double>("aim.dpitch", 0.0);
    mem->insertOrAssign<bool>("aim.shoot", false);

    mem->insertOrAssign<double>("gimbal.last_dyaw", 0.0);
    mem->insertOrAssign<double>("gimbal.last_dpitch", 0.0);

    mem->insertOrAssign<float>("gimbal.yaw_rad", 0);
    mem->insertOrAssign<float>("gimbal.pitch_rad", 0);
    mem->insertOrAssign<float>("gimbal.pitch_deg", 0);
    mem->insertOrAssign<float>("gimbal.yaw_deg", 0);

    t = std::chrono::steady_clock::now();
    mem->insertOrAssign<long long>("gimbal.t", time_point_to_longlong(t));

    last_ctrl_time = std::chrono::steady_clock::now();

    gimbal = std::make_unique<skill::board::Gimbal>("skills/gimbal/config.yaml");
}

/**
 * @brief 每轮取一帧写进公共内存
 */
SKILL_API void run()
{
    if (mem == nullptr || gimbal == nullptr)
    {
        sc::tools::logger()->error("[gimbal] init失败");
        return;
    }

    gimbal->receive();

    skill::board::RecvPacket pack = gimbal->getTelemetry();

    mem->insertOrAssign<int>("gimbal.mode", static_cast<int>(pack.mode));
    mem->insertOrAssign<int>("gimbal.fire_error", static_cast<int>(pack.fire_error));

    mem->insertOrAssign<std::vector<float>>("gimbal.g_q", std::vector<float>(pack.g_q, pack.g_q + 4));

    mem->insertOrAssign<float>("gimbal.yaw_v", pack.yaw_velocity);
    mem->insertOrAssign<float>("gimbal.pitch_v", pack.pitch_velocity);

    mem->insertOrAssign<int>("gimbal.fire_rpm", static_cast<int>(pack.friction_rpm));
    mem->insertOrAssign<int>("gimbal.fire_count", static_cast<int>(pack.fired_ammo_count));

    mem->insertOrAssign<std::vector<float>>("gimbal.c_q", std::vector<float>(pack.c_q, pack.c_q + 4));
    mem->insertOrAssign<float>("gimbal.speed", pack.speed);

    mem->insertOrAssign<bool>("gimbal.online", gimbal->online());

    t = std::chrono::steady_clock::now();
    mem->insertOrAssign<long long>("gimbal.t", time_point_to_longlong(t));

    // 四元数 → 欧拉角
    float w = pack.g_q[0];
    float x = pack.g_q[1];
    float y = pack.g_q[2];
    float z = pack.g_q[3];

    float sinp = 2.0f * (w * y - z * x);
    if (std::abs(sinp) >= 1.0f) {
        sinp = std::copysign(1.0f, sinp);
    }
    float pitch_rad = std::asin(sinp);

    float yaw_rad = std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));

    const float rad2deg = 180.0f / static_cast<float>(M_PI);
    float pitch_deg = pitch_rad * rad2deg;
    float yaw_deg   = yaw_rad   * rad2deg;

    mem->insertOrAssign<float>("gimbal.yaw_rad",   yaw_rad);
    mem->insertOrAssign<float>("gimbal.pitch_rad", pitch_rad);
    mem->insertOrAssign<float>("gimbal.pitch_deg", pitch_deg);
    mem->insertOrAssign<float>("gimbal.yaw_deg",   yaw_deg);

    // 控制周期节流：不到 50Hz 只更新 telemetry，不发控制
    const auto now = std::chrono::steady_clock::now();
    double dt = std::chrono::duration<double>(now - last_ctrl_time).count();
    if (dt < kCtrlPeriodSec) {
        return;
    }
    if (dt > 0.05) dt = 0.05;
    last_ctrl_time = now;

    double dyaw        = mem->get<double>("aim.dyaw");
    double dpitch      = mem->get<double>("aim.dpitch");
    double last_dyaw   = mem->get<double>("gimbal.last_dyaw");
    double last_dpitch = mem->get<double>("gimbal.last_dpitch");

    // 原代码漏了 abs，负值恒 <= 0.01，导致负方向每帧重置
    if (std::abs(last_dyaw)   <= 0.01) last_dyaw   = dyaw;
    if (std::abs(last_dpitch) <= 0.01) last_dpitch = dpitch;

    if (pack.mode == 0)
    {
        gimbal->sendControl(0, static_cast<float>(dyaw), static_cast<float>(dpitch));

        last_dyaw   = 0;
        last_dpitch = 0;
        mem->insertOrAssign<double>("gimbal.last_dyaw",   0.0);
        mem->insertOrAssign<double>("gimbal.last_dpitch", 0.0);
    }
    else if (pack.mode == 1)
    {
        auto signStep = [](double &v, float step) -> float {
            if (std::abs(v) <= 0.01) {
                v = 0;
                return 0.0f;
            }
            if (std::abs(v) >= step) {
                if (v < 0) { v += step; return -step; }
                else       { v -= step; return  step; }
            }
            float ret = static_cast<float>(v);
            v = 0;
            return ret;
        };

        const float step = kMaxAngularSpeed * static_cast<float>(dt);
        float dy = signStep(last_dyaw,   step);
        float dp = signStep(last_dpitch, step);

        //sc::tools::logger()->debug("org last_dyaw:{} last_dpitch:{}", last_dyaw, last_dpitch);
        //sc::tools::logger()->debug("send dy:{} dp:{}", dy, dp);

        mem->insertOrAssign<double>("gimbal.last_dyaw",   last_dyaw);
        mem->insertOrAssign<double>("gimbal.last_dpitch", last_dpitch);

        if (!gimbal->sendControl(static_cast<uint8_t>(1), dy, dp))
        {
            sc::tools::logger()->error("[gimbal] 数据传输错误");
        }
    }
}

/**
 * @brief 收尾
 */
SKILL_API void end()
{
    if (gimbal == nullptr) return;

    gimbal.reset();

    sc::tools::logger()->info("[gimbal] 已停止采集");
}