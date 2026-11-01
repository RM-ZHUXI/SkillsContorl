#include "gimbal.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <termios.h>
#include <cstring>
#include <iostream>

namespace skill::board
{

Gimbal::Gimbal(const std::string& config_path)
{
    try
    {
        auto config = YAML::LoadFile(config_path);
        port_name_ = sc::tools::read<std::string>(config,"port");
        baudrate_ = sc::tools::read<int>(config,"baudrate");
    }
    catch (const std::exception& e)
    {
        sc::tools::logger()->error("[Gimbal] 读取配置文件失败: {}", e.what());
        throw std::runtime_error("[Gimbal] 读取配置文件失败: " + config_path);
    }

    if (!openPort())
    {
        sc::tools::logger()->error("[Gimbal] 串口打开失败: {}", port_name_);
        throw std::runtime_error("[Gimbal] 串口打开失败: " + port_name_);
    }
    else
    {
        sc::tools::logger()->info("[Gimbal] 串口初始化成功: {} @ {}", port_name_, baudrate_);
    }
    
    last_rx_time_ = std::chrono::steady_clock::now();
}

Gimbal::~Gimbal()
{
    closePort();
}

bool Gimbal::openPort()
{
    fd_ = open(port_name_.c_str(), O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd_ == -1) return false;
    
    //cfcntl(fd_, F_SETFL, 0); // 恢复阻塞模式 

    struct termios options;
    tcgetattr(fd_, &options);

    // 设置波特率
    speed_t speed;
    switch (baudrate_)
    {
        case 115200: speed = B115200; break;
        case 921600: speed = B921600; break;
        case 1500000: speed = B1500000; break;
        default: speed = B115200; break;
    }
    cfsetispeed(&options, speed);
    cfsetospeed(&options, speed);

    // 8N1，无流控，原始模式
    options.c_cflag &= ~PARENB;
    options.c_cflag &= ~CSTOPB;
    options.c_cflag &= ~CSIZE;
    options.c_cflag |= CS8;
    options.c_cflag |= (CLOCAL | CREAD);
    options.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    options.c_iflag &= ~(IXON | IXOFF | IXANY);
    options.c_oflag &= ~OPOST;

    tcsetattr(fd_, TCSANOW, &options);
    return true;
}

void Gimbal::closePort()
{
    if (fd_ != -1)
    {
        close(fd_);
        fd_ = -1;
    }
}

void Gimbal::receive()
{
    if (fd_ == -1) return;

    // 1. 读取新数据到缓冲区尾部
    int bytes_read = read(fd_, rx_buffer_ + rx_len_, sizeof(rx_buffer_) - rx_len_);
    if (bytes_read <= 0) return;
    
    rx_len_ += bytes_read;

    // 2. 解析缓冲区数据
    parseBuffer();
}

void Gimbal::parseBuffer()
{
    size_t pos = 0;
    while (rx_len_ - pos >= sizeof(RecvPacket))
    {
        // 检查帧头 'Z'(0x5A) 'X'(0x58)
        if (rx_buffer_[pos] == 0x5A && rx_buffer_[pos + 1] == 0x58)
        {
            RecvPacket* packet = reinterpret_cast<RecvPacket*>(&rx_buffer_[pos]);
            uint16_t expected_crc = calcCRC16(reinterpret_cast<uint8_t*>(packet), 52);
            uint16_t received_crc = packet->crc16[0] | (packet->crc16[1] << 8);

            if (expected_crc == received_crc)
            {
                // 解析成功，更新数据
                std::memcpy(&telemetry_, packet, sizeof(RecvPacket));
                last_rx_time_ = std::chrono::steady_clock::now();
                pos += sizeof(RecvPacket); // 移动到下一帧
                continue;
            }
        }
        // 帧头不对或 CRC 错误，逐字节滑动
        pos++;
    }

    // 3. 清理已解析的数据，保留未完整的半包
    if (pos > 0)
    {
        rx_len_ -= pos;
        std::memmove(rx_buffer_, rx_buffer_ + pos, rx_len_);
    }
}

bool Gimbal::sendControl(uint8_t mode, float yaw_d, float pitch_d)
{
    if (fd_ == -1) return false;

    ControlPacket packet;
    packet.head[0] = 0x5A; // 'Z'
    packet.head[1] = 0x58; // 'X'
    packet.mode = mode;
    packet.yaw_d = yaw_d;
    packet.pitch_d = pitch_d;

    // 计算前 11 字节的 CRC
    uint16_t crc = calcCRC16(reinterpret_cast<uint8_t*>(&packet), 11);
    packet.crc16[0] = crc & 0xFF;        // 低字节
    packet.crc16[1] = (crc >> 8) & 0xFF; // 高字节

    ssize_t written = write(fd_, &packet, sizeof(ControlPacket));
    return written == sizeof(ControlPacket);
}

bool Gimbal::online() const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_rx_time_).count();
    return elapsed <= 200; // 200ms 超时
}

uint16_t Gimbal::calcCRC16(const uint8_t* data, size_t len)
{
    // 多项式 0x8408，初值 0xFFFF，输入反射，无末尾异或
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i)
    {
        crc ^= data[i];
        for (int j = 0; j < 8; ++j)
        {
            if (crc & 0x0001)
            {
                crc = (crc >> 1) ^ 0x8408;
            }
            else
            {
                crc >>= 1;
            }
        }
    }
    return crc;
}

} // namespace skill::board