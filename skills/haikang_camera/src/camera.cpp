/**
 * @file camera.cpp
 * @brief MV-CS060-10UC-PRO 相机封装（Linux / 海康 MVS SDK）
 * @author 时新博 a17398481273@163.com
 * @date 2026/09/20
 * @version 1.0.3
 */

#include "camera.hpp"

#include "tools/logger.hpp"      // 技能要写日志就得显式包含（skillscontrol.hpp 只保证 Memory）

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <stdexcept>

#include <yaml-cpp/yaml.h>

#if defined(_WIN32)
    #include <windows.h>     // GetModuleHandleExA / GetModuleFileNameA
#elif defined(__linux__)
    #include <dlfcn.h>       // dladdr
#endif

namespace hardware
{

namespace
{
/**
 * @brief 定位 config.yaml
 * @return 配置文件路径
 * @note  先按本 .dll / .so 自己的位置推导，拿不到再退回相对路径。
 *        这样从任何目录启动主程序都能找到配置。
 * @note  双平台：Windows 用 GetModuleHandleEx(FROM_ADDRESS)+GetModuleFileName，
 *        Linux 用 dladdr。旧版本这一整段被 #if defined(__linux__) 包着，
 *        Windows 上永远走"相对当前工作目录"的兜底 —— 只有恰好在发布根目录下
 *        启动才对，换个工作目录就读不到配置。
 */
std::string locateConfigFileImpl()
{
    std::string modulePath;

#if defined(_WIN32)
    HMODULE hMod = nullptr;
    // 用本函数的地址反查"我属于哪个模块"，拿到的就是本技能 DLL 自己的句柄
    if (GetModuleHandleExA(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(&locateConfigFileImpl),
            &hMod))
    {
        char buf[MAX_PATH] = {0};
        if (GetModuleFileNameA(hMod, buf, MAX_PATH) > 0)
            modulePath = buf;
    }
#elif defined(__linux__)
    Dl_info info{};
    if (dladdr(reinterpret_cast<void *>(&locateConfigFileImpl), &info) != 0 &&
        info.dli_fname != nullptr)
    {
        modulePath = info.dli_fname;

        // dladdr 返回的是"当初 dlopen 时用的路径"，主程序用的是相对路径，
        // 所以这里补成绝对路径 —— 否则配置路径仍然依赖进程的工作目录。
        if (!modulePath.empty() && modulePath[0] != '/')
        {
            std::error_code ec;
            const std::filesystem::path cwd = std::filesystem::current_path(ec);
            if (!ec) modulePath = (cwd / modulePath).lexically_normal().string();
        }
    }
#endif

    if (!modulePath.empty())
    {
        // 用 std::filesystem 拼路径、把 "." / ".." 交给操作系统解析，
        // 这样 build/bin（Windows 产物）和 build/lib（Linux 产物）两种位置都能覆盖。
        const std::filesystem::path dir =
            std::filesystem::path(modulePath).parent_path();

        const std::filesystem::path candidates[] = {
            dir / ".." / ".." / "config.yaml",          // 常规：<技能目录>/build/bin 或 build/lib
            dir / ".." / ".." / ".." / "config.yaml",   // 多一层构建目录（如 build/Release/bin）
            dir / "config.yaml",                        // 与 .dll / .so 同级
        };

        for (const auto &c : candidates)
        {
            std::ifstream probe(c);
            if (probe.good()) return c.string();
        }
    }

    // 兜底：相对当前工作目录（从发布根目录启动时有效）
    return "skills/haikang_camera/config.yaml";
}
} // anonymous namespace

// ============================================================
// 构造 / 析构
// ============================================================

/**
 * @brief 构造函数：只做"读配置 + 打开设备"
 * @note  刻意不做任何界面/显示操作（imshow 之类）：
 *        本函数运行在技能链的工作线程里，而且服务器/虚拟机可能没有图形环境，
 *        在 init 阶段开窗口既会抛异常也可能直接终止进程。
 */
camera::camera(sc::tools::Memory *mem)
{
    this->memory = mem;

    configPath = locateConfigFileImpl();

    sc::tools::logger()->info("[hardware camera] 摄像头初始化 (config: {})", configPath);

    std::string err;
    if (!loadConfig(configPath, err))
    {
        sc::tools::logger()->error("[hardware camera] 读取配置失败: {}", err);
        return;                       // start() 会因为句柄为空而返回 false
    }

    if (name != "MV-CS060-10UC-PRO")
    {
        sc::tools::logger()->warn("[hardware camera] 未识别的相机型号 {}（当前只支持 MV-CS060-10UC-PRO）", name);
        return;
    }

    if (!init_MV_CS060_10UC_PRO(cameraIndex))
    {
        sc::tools::logger()->error("[hardware camera] 摄像头初始化失败");
    }
}

/**
 * @brief 析构：释放设备资源
 * @note  ★ 必须持锁：release_locked() 的约定是"调用方已持有 MV_CS060_10UC_PRO_mutex"。
 *        析构可能发生在工作线程还在 image() 里取图的时候（停链 → 释放技能 →
 *        析构全局 unique_ptr<hardware::camera>），裸调等于让"关闭句柄"和"取图"
 *        并发操作同一份状态，轻则报错重则崩在 SDK 里。
 */
camera::~camera()
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);
    release_locked();
}

// ============================================================
// 配置
// ============================================================

std::string camera::locateConfigFile()
{
    return locateConfigFileImpl();
}

bool camera::loadConfig(const std::string &path, std::string &err)
{
    try
    {
        YAML::Node y = YAML::LoadFile(path);

        name = y["camera_name"] ? y["camera_name"].as<std::string>() : std::string("MV-CS060-10UC-PRO");

        cameraIndex    = y["camera_index"]    ? y["camera_index"].as<int>()      : 0;
        cameraExposure = y["camera_exposure"] ? y["camera_exposure"].as<double>() : 30000.0;
        grabTimeoutMs  = y["grab_timeout_ms"] ? y["grab_timeout_ms"].as<int>()    : 300;

        if (grabTimeoutMs <= 0) grabTimeoutMs = 300;

        return true;
    }
    catch (const std::exception &e)
    {
        // 只返回失败，不抛、不 exit：让 init() 去决定这条技能链失败
        err = std::string(e.what()) + " (" + path + ")";
        return false;
    }
}

// ============================================================
// 对外接口（加锁一次）
// ============================================================

bool camera::start()
{
    if (name != "MV-CS060-10UC-PRO")
    {
        sc::tools::logger()->error("[hardware camera] 相机型号不匹配，无法启动: {}", name);
        return false;
    }
    return start_MV_CS060_10UC_PRO();
}

void camera::stop()
{
    if (name != "MV-CS060-10UC-PRO") return;
    stop_MV_CS060_10UC_PRO();
}

void camera::release()
{
    if (name != "MV-CS060-10UC-PRO") return;
    release_MV_CS060_10UC_PRO();
}

cv::Mat camera::image()
{
    if (name != "MV-CS060-10UC-PRO")
    {
        return cv::Mat();
    }
    return image_MV_CS060_10UC_PRO();
}

// ============================================================
// MV_CS060_10UC_PRO：内部实现（不加锁 / 只加一次锁）
// ============================================================

/**
 * @brief 停止采集（不加锁，调用方必须已持有 MV_CS060_10UC_PRO_mutex）
 */
void camera::stop_locked()
{
    if (MV_CS060_10UC_PRO_handle != nullptr && MV_CS060_10UC_PRO_isGrabbing.load())
    {
        MV_CC_StopGrabbing(MV_CS060_10UC_PRO_handle);
        MV_CS060_10UC_PRO_isGrabbing = false;
    }
}

/**
 * @brief 释放资源（不加锁，调用方必须已持有 MV_CS060_10UC_PRO_mutex）
 * @note  幂等：重复调用不会重复关闭句柄
 */
void camera::release_locked()
{
    if (MV_CS060_10UC_PRO_handle == nullptr) return;

    stop_locked();                       
    MV_CC_CloseDevice(MV_CS060_10UC_PRO_handle);
    MV_CC_DestroyHandle(MV_CS060_10UC_PRO_handle);
    MV_CS060_10UC_PRO_handle = nullptr;
    MV_CS060_10UC_PRO_isGrabbing = false;
}

bool camera::init_MV_CS060_10UC_PRO(int cameraIndex)
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);

    if (MV_CS060_10UC_PRO_handle != nullptr)
    {
        return true;                            
    }

    MV_CC_DEVICE_INFO_LIST deviceList;
    memset(&deviceList, 0, sizeof(MV_CC_DEVICE_INFO_LIST));

    int ret = MV_CC_EnumDevices(MV_USB_DEVICE, &deviceList);
    if (ret != MV_OK)
    {
        sc::tools::logger()->error("[hardware init] USB列表获取失败, ret = {}", ret);
        return false;
    }

    if (deviceList.nDeviceNum == 0 || cameraIndex >= static_cast<int>(deviceList.nDeviceNum))
    {
        sc::tools::logger()->error("[hardware init] MV_CS060_10UC_PRO未找到 (设备数={}, 请求索引={})",
                                   deviceList.nDeviceNum, cameraIndex);
        return false;
    }

    ret = MV_CC_CreateHandle(&MV_CS060_10UC_PRO_handle, deviceList.pDeviceInfo[cameraIndex]);
    if (ret != MV_OK || MV_CS060_10UC_PRO_handle == nullptr)
    {
        sc::tools::logger()->error("[hardware init] 句柄创建失败, ret = {}", ret);
        MV_CS060_10UC_PRO_handle = nullptr;
        return false;
    }

    ret = MV_CC_OpenDevice(MV_CS060_10UC_PRO_handle);
    if (ret != MV_OK)
    {
        sc::tools::logger()->error("[hardware init] 打开设备失败, ret = {}", ret);
        MV_CC_DestroyHandle(MV_CS060_10UC_PRO_handle);
        MV_CS060_10UC_PRO_handle = nullptr;
        return false;
    }

    // 曝光设置失败只告警，不阻断（部分机型/权限下不支持写）
    MV_CC_SetEnumValue(MV_CS060_10UC_PRO_handle, "ExposureAuto", MV_EXPOSURE_AUTO_MODE_OFF);
    ret = MV_CC_SetFloatValue(MV_CS060_10UC_PRO_handle, "ExposureTime", cameraExposure);
    if (ret != MV_OK)
    {
        sc::tools::logger()->warn("[hardware init] 设置曝光失败, ret = {}", ret);
    }

    // ★ 宽高必须"先初始化再查询再校验"：
    //   老代码是 MVCC_INTVALUE width, height; （未初始化）且不检查返回值，
    //   查询失败时 nCurValue 是栈上垃圾值，后面 new unsigned char[w*h*3] 会直接爆。
    //   注意 SDK 里 nCurValue 是 unsigned int，先转成有符号再判 <=0，避免"永假比较"。
    MVCC_INTVALUE width{};
    MVCC_INTVALUE height{};
    const bool queryOk = (MV_CC_GetIntValue(MV_CS060_10UC_PRO_handle, "Width",  &width)  == MV_OK) &&
                         (MV_CC_GetIntValue(MV_CS060_10UC_PRO_handle, "Height", &height) == MV_OK);
    const long long w = static_cast<long long>(width.nCurValue);
    const long long h = static_cast<long long>(height.nCurValue);

    if (!queryOk || w <= 0 || h <= 0)
    {
        sc::tools::logger()->error("[hardware init] 获取分辨率失败 (w={}, h={})", w, h);
        MV_CC_CloseDevice(MV_CS060_10UC_PRO_handle);
        MV_CC_DestroyHandle(MV_CS060_10UC_PRO_handle);
        MV_CS060_10UC_PRO_handle = nullptr;
        return false;
    }

    MV_CS060_10UC_PRO_width  = static_cast<int>(w);
    MV_CS060_10UC_PRO_height = static_cast<int>(h);

    sc::tools::logger()->info("[hardware init] 初始化成功: {}x{}, 曝光 {} us, 取图超时 {} ms",
                              MV_CS060_10UC_PRO_width, MV_CS060_10UC_PRO_height,
                              cameraExposure, grabTimeoutMs);

    return true;
}

bool camera::start_MV_CS060_10UC_PRO()
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);

    if (MV_CS060_10UC_PRO_handle == nullptr)
    {
        sc::tools::logger()->error("[hardware start] 设备未初始化，无法开始采集");
        return false;
    }

    if (MV_CS060_10UC_PRO_isGrabbing.load())
    {
        return true;                                      // 已经在采集
    }

    int ret = MV_CC_StartGrabbing(MV_CS060_10UC_PRO_handle);
    if (ret != MV_OK)
    {
        sc::tools::logger()->error("[hardware start] 开始采集失败, ret = {}", ret);
        return false;
    }

    MV_CS060_10UC_PRO_isGrabbing = true;
    return true;
}

void camera::stop_MV_CS060_10UC_PRO()
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);
    stop_locked();
}

void camera::release_MV_CS060_10UC_PRO()
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);
    release_locked();                                 
}

cv::Mat camera::image_MV_CS060_10UC_PRO()
{
    std::lock_guard<std::mutex> lock(MV_CS060_10UC_PRO_mutex);

    if (!MV_CS060_10UC_PRO_isGrabbing.load() || MV_CS060_10UC_PRO_handle == nullptr)
    {
        sc::tools::logger()->error("[hardware image] MV_CS060_10UC_PRO 摄像头未开启或句柄无效");
        return cv::Mat();
    }

    const int w = MV_CS060_10UC_PRO_width;
    const int h = MV_CS060_10UC_PRO_height;
    if (w <= 0 || h <= 0)
    {
        sc::tools::logger()->error("[hardware image] 分辨率非法: {}x{}", w, h);
        return cv::Mat();
    }

    // 复用缓冲（3 通道按最大需要留；Mono8 用不满也无妨）
    const std::size_t need = static_cast<std::size_t>(w) * static_cast<std::size_t>(h) * 3u;
    if (frameBuf.size() < need)
    {
        frameBuf.resize(need);
    }

    MV_FRAME_OUT_INFO_EX frameInfo{};
    const int ret = MV_CC_GetOneFrameTimeout(MV_CS060_10UC_PRO_handle,
                                             frameBuf.data(),
                                             static_cast<unsigned int>(need),
                                             &frameInfo,
                                             static_cast<unsigned int>(grabTimeoutMs));
    if (ret != MV_OK)
    {
        // 取图失败不写公共内存（由 run() 决定保留上一帧），只报错
        sc::tools::logger()->error("[hardware image] 获取图像失败, ret = {}", ret);
        return cv::Mat();
    }

    // SDK 里 nWidth/nHeight 是 unsigned short，转成有符号再判，避免"永假比较"
    const int fw = static_cast<int>(frameInfo.nWidth);
    const int fh = static_cast<int>(frameInfo.nHeight);
    if (fw <= 0 || fh <= 0)
    {
        sc::tools::logger()->error("[hardware image] 帧信息异常: {}x{}", fw, fh);
        return cv::Mat();
    }

    const bool isMono  = (frameInfo.enPixelType == PixelType_Gvsp_Mono8);
    const bool isBayer = (frameInfo.enPixelType == PixelType_Gvsp_BayerGB8 ||
                          frameInfo.enPixelType == PixelType_Gvsp_BayerRG8 ||
                          frameInfo.enPixelType == PixelType_Gvsp_BayerGR8 ||
                          frameInfo.enPixelType == PixelType_Gvsp_BayerBG8);
    const std::size_t channels = (isMono || isBayer) ? 1u : 3u;

    // 防越界：SDK 报的尺寸不能超过我们给的缓冲
    const std::size_t got = static_cast<std::size_t>(frameInfo.nWidth) *
                            static_cast<std::size_t>(frameInfo.nHeight) * channels;
    if (got > frameBuf.size())
    {
        sc::tools::logger()->error("[hardware image] 帧尺寸超出缓冲: {}x{}x{} > {} 字节",
                                   frameInfo.nWidth, frameInfo.nHeight, channels, frameBuf.size());
        return cv::Mat();
    }

    cv::Mat raw(frameInfo.nHeight, frameInfo.nWidth, (channels == 1u ? CV_8UC1 : CV_8UC3), frameBuf.data());
    cv::Mat image;

    switch (frameInfo.enPixelType)
    {
        case PixelType_Gvsp_Mono8:
            image = raw.clone();                                  // 浅拷贝到 Mat 头，clone 出一份独立数据
            break;

        case PixelType_Gvsp_RGB8_Packed:
            cv::cvtColor(raw, image, cv::COLOR_RGB2BGR);
            break;

        case PixelType_Gvsp_BGR8_Packed:
            image = raw.clone();
            break;

        // ★ 斜对角映射，别同名对同名（见文件头 @par 第 7 条）：
        //   海康 BayerRG8(RGGB) 要用 COLOR_BayerBG2BGR，因为 OpenCV 的
        //   COLOR_BayerBG2BGR 官方注释就是 "equivalent to RGGB Bayer pattern"。
        case PixelType_Gvsp_BayerGB8:                            // GBRG → OpenCV GR
            cv::cvtColor(raw, image, cv::COLOR_BayerGR2BGR);
            break;

        case PixelType_Gvsp_BayerRG8:                            // RGGB → OpenCV BG
            cv::cvtColor(raw, image, cv::COLOR_BayerBG2BGR);
            break;

        case PixelType_Gvsp_BayerGR8:                            // GRBG → OpenCV GB
            cv::cvtColor(raw, image, cv::COLOR_BayerGB2BGR);
            break;

        case PixelType_Gvsp_BayerBG8:                            // BGGR → OpenCV RG
            cv::cvtColor(raw, image, cv::COLOR_BayerRG2BGR);
            break;

        default:
            sc::tools::logger()->error("[hardware image] 不支持的像素格式: {}",
                                       static_cast<int>(frameInfo.enPixelType));
            return cv::Mat();
    }

    return image;
}

} // namespace hardware
