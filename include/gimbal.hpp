/**
 * @file gimbal.hpp
 * @brief ZX 新通信协议解析与云台控制
 * @author o_O a17398481273@163.com
 * @date 2026/10/20
 * @version 2.0.0
 */

#ifndef GIMBAL_HPP
#define GIMBAL_HPP

#include "tools/logger.hpp"
#include "tools/yaml.hpp"
#include <string>
               
#include <cstdint>
#include <chrono>
#include <cmath> 

namespace skill::board
{

/**
 * @brief 接收数据数据包
 * 
 */
#pragma pack(push, 1)
struct RecvPacket
{
    uint8_t head[2];             ///< 固定 'Z', 'X'
    uint8_t mode;                ///< 0 无视觉任务, 1 普通自瞄, 2 小符, 3 大符
    float g_q[4];                ///< w, x, y, z (BMI088 姿态)
    float yaw_velocity;          ///< rad/s
    float pitch_velocity;        ///< rad/s
    uint16_t friction_rpm;       ///< rpm
    uint16_t fired_ammo_count;   ///< 已发弹量
    uint8_t fire_error;          ///< 未发弹原因 (0~9)
    float c_q[4];                ///< w, x, y, z (底盘速度方向)
    float speed;                 ///< m/s
    uint8_t crc16[2];            ///< 前 52 字节的 CRC
};
#pragma pack(pop)

/**
 * @brief 发送数据包
 * 
 */
#pragma pack(push, 1)
struct ControlPacket
{
    uint8_t head[2];             ///< 固定 'Z', 'X'
    uint8_t mode;                ///< 0 无控制, 1 只控云台, 2 控云台并允许发弹, 3 弹盘反拨
    float yaw_d;                 ///< 相对已有目标的 yaw 增长量, rad
    float pitch_d;               ///< 相对已有目标的 pitch 增长量, rad
    uint8_t crc16[2];            ///< 前 11 字节的 CRC
};
#pragma pack(pop)

class Gimbal
{
public:
    explicit Gimbal(const std::string& config_path);
    ~Gimbal();

    Gimbal(const Gimbal&) = delete;
    Gimbal& operator=(const Gimbal&) = delete;

    /**
     * @brief 非阻塞接收并解析串口数据,推荐 1~2ms 周期
     */
    void receive();

    /**
     * @brief 发送控制帧。注意：这是增量控制，重复发送相同的增量会累加！
     * @param mode 0 无控制, 1 只控云台, 2 控云台允许发弹, 3 反拨
     * @param yaw_d yaw 增量 (rad)
     * @param pitch_d pitch 增量 (rad)
     * @return true 发送成功
     */
    bool sendControl(uint8_t mode, float yaw_d, float pitch_d);

    /**
     * @brief 检查下位机是否在线 (200ms 无有效帧)
     */
    bool online() const;

    /**
     * @brief 获得最新数据
     * 
     * @return const RecvPacket& 
     */
    const RecvPacket& getTelemetry() const { return telemetry_; }

private:
    /**
     * @brief 打开并配置串口
     * 
     * 使用 termios 配置波特率、8N1 数据格式、无流控，并设置为原始模式。
     * 
     * @return true 串口打开且配置成功
     * @return false 打开失败或配置失败
     */
    bool openPort();

    /**
     * @brief 关闭串口并释放文件描述符
     */
    void closePort();

    /**
     * @brief 解析接收缓冲区数据（处理半包与粘包）
     * 
     * 在缓冲区中逐字节查找帧头 "ZX"，若长度足够且 CRC 校验通过则提取数据，
     * 否则滑动窗口继续查找。解析完成后清理已处理的数据，保留未完整的半包。
     */
    void parseBuffer();

    /**
     * @brief 计算 CRC16 校验码 (X-25 算法)
     * 
     * 采用多项式 0x8408（0x1021 反射），初值 0xFFFF，输入反射，无末尾异或。
     * 
     * @param data 指向需要计算校验的数据起始地址
     * @param len 数据长度（字节数）
     * @return uint16_t 计算出的 16 位 CRC 校验值
     */
    uint16_t calcCRC16(const uint8_t* data, size_t len);

    std::string port_name_; ///< 串口名称
    int baudrate_;          ///< 波特率
    int fd_{-1};            ///< Linux 串口文件描述符 (File Descriptor)，初始化为 -1 表示未打开或打开失败

    // 接收缓冲区
    uint8_t rx_buffer_[2048];
    size_t rx_len_{0};

    RecvPacket telemetry_{};                             ///< 接收包
    std::chrono::steady_clock::time_point last_rx_time_; ///< 上一次通讯时间
};

} // namespace skill::board

#endif // GIMBAL_HPP