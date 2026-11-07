#include "main.hpp"              // init / run / end 声明（同时带来 SKILL_API、Memory）

#include "gimbal.hpp"            
#include "tools/logger.hpp"      

#include <memory>
#include <stdexcept>
#include <cmath>

namespace
{
    /**
     * @brief skillcontorl 注入进来的内存管理器
     * @note  只有在 init() 成功执行后才有值，run()/end() 里必须先判空
     */
    sc::tools::Memory *mem = nullptr;

    /**
     * @brief gimbal 对象
     * @note  随本 .dll / .so 的加载状态存活。
     *        每次「启动技能链」init() 会重新构造并打开设备，
     *        每次「停止技能链」end() 会 stop + release，所以停→再启动 = 设备重新初始化。
     */
    std::unique_ptr<skill::board::Gimbal> gimbal;

    /**
     * @brief 获取时间戳
     * 
     */
    std::chrono::steady_clock::time_point t;

    // time_point 转 long long (微秒)
    inline long long time_point_to_longlong(std::chrono::steady_clock::time_point tp) {
        return std::chrono::duration_cast<std::chrono::microseconds>(tp.time_since_epoch()).count();
    }

    // long long (微秒) 转 time_point
    inline std::chrono::steady_clock::time_point longlong_to_time_point(int64_t us) {
        return std::chrono::steady_clock::time_point(std::chrono::microseconds(us));
    }

}

/**
 * @brief 程序初始化
 * @note  签名必须和 include/main.hpp 的声明、以及宿主查找的 "init" 导出名完全一致
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[gimbal] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;

    /**
     * @brief 接收数据预处理
     * 
     */
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

    /**
     * @brief 发送数据预处理
     * 
     */
    mem->insertOrAssign<double>("aim.dyaw", 0.0);
    mem->insertOrAssign<double>("aim.dpitch", 0.0);
    mem->insertOrAssign<bool>("aim.shoot", false);
    
    mem->insertOrAssign<double>("gimbal.last_dyaw",0.0);
    mem->insertOrAssign<double>("gimbal.last_dpitch",0.0);

    mem->insertOrAssign<float>("gimbal.yaw_rad", 0);     
    mem->insertOrAssign<float>("gimbal.pitch_rad", 0);
    mem->insertOrAssign<float>("gimbal.pitch_deg", 0);     
    mem->insertOrAssign<float>("gimbal.yaw_deg", 0);

    t = std::chrono::steady_clock::now();
    mem->insertOrAssign<long long>("gimbal.t",time_point_to_longlong(t));

    gimbal = std::make_unique<skill::board::Gimbal>("skills/gimbal/config.yaml");
}

/**
 * @brief 每轮取一帧写进公共内存
 * @note  一次调用只干一件事，干完立刻返回
 */
#include <iostream>

SKILL_API void run()
{
    if (mem == nullptr || gimbal == nullptr)
    {
        sc::tools::logger()->error("[gimbal] init失败");
        return;
    }
    
    gimbal->receive(); 

    skill::board::RecvPacket pack = gimbal->getTelemetry();
    /**
     * @brief 接收数据
     * 
     */
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
    mem->insertOrAssign<long long>("gimbal.t",time_point_to_longlong(t));

    /**
     * @brief debug
     * 
     */
    float w = pack.g_q[0];
    float x = pack.g_q[1];
    float y = pack.g_q[2];
    float z = pack.g_q[3];
    // Pitch (俯仰角，绕 Y 轴) 
    float sinp = 2.0f * (w * y - z * x);
    if (std::abs(sinp) >= 1.0f) {
        sinp = std::copysign(1.0f, sinp);
    }
    float pitch_rad = std::asin(sinp);

    // Yaw (偏航角，绕 Z 轴)
    float yaw_rad = std::atan2(2.0f * (w * z + x * y), 1.0f - 2.0f * (y * y + z * z));

    float rad2deg = 180.0f / M_PI;
    float pitch_deg = pitch_rad * rad2deg;
    float yaw_deg = yaw_rad * rad2deg;

    mem->insertOrAssign<float>("gimbal.yaw_rad", yaw_rad);     
    mem->insertOrAssign<float>("gimbal.pitch_rad", pitch_rad);
    mem->insertOrAssign<float>("gimbal.pitch_deg", pitch_deg);     
    mem->insertOrAssign<float>("gimbal.yaw_deg", yaw_deg);

    /**
     * @brief 发送数据
     * 
     */
    double dyaw = mem->get<double>("aim.dyaw");
    double dpitch = mem->get<double>("aim.dpitch");
    double control = mem->get<bool>("aim.shoot");
    double last_dyaw = mem->get<double>("gimbal.last_dyaw");
    double last_dpitch = mem->get<double>("gimbal.last_dpitch");

    if(last_dyaw <=0.01)last_dyaw = dyaw;
    if(last_dpitch <=0.01)last_dpitch = dpitch;

    if(pack.mode == 0)
    {
        gimbal->sendControl(0,(float)dyaw,(float)dpitch);
        last_dpitch = 0;
        last_dyaw = 0;
    }
    else if(pack.mode == 1)
    {
        auto signStep = [](double &v, float step) -> float { 
            
            if(abs(v) <= 0.01)
            {
                v = 0;
                return 0.0f;
            }
            if(abs(v) >= step)
            {
                if(v < 0)
                {
                    v += step;
                    return -step;
                }
                else
                {
                    v -= step;
                    return step;
                }
            } 
            else
            {
                v = 0;
                return v;
            }
            return 0.0f;
        };
        float dy = signStep(last_dyaw, 0.01f);
        float dp = signStep(last_dpitch, 0.01f);
        sc::tools::logger()->debug("org last_dyaw:{} last_dpitch:{}",last_dyaw,last_dpitch);
        sc::tools::logger()->debug("send dy:{} dp:{}",dy,dp);

        mem->insertOrAssign<double>("gimbal.last_dyaw",last_dyaw);
        mem->insertOrAssign<double>("gimbal.last_dpitch",last_dpitch);
        if(!gimbal->sendControl((uint8_t)1,(float)dy,(float)dp))
        {
            sc::tools::logger()->error("[gimbal] 数据传输错误");
        }
    }

}
/**
 * @brief 收尾：停采集 + 释放设备
 * @note  显式 release() 让句柄在"停止链"时就关掉；~camera() 里还会兜一次（幂等）。
 */
SKILL_API void end()
{
    if (gimbal == nullptr) return;

    gimbal.reset(); 

    sc::tools::logger()->info("[gimbal] 已停止采集");
}