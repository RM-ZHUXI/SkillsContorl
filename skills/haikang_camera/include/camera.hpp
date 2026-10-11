#ifndef CAMERA_HPP
#define CAMERA_HPP

#include <atomic>
#include <iostream>
#include <mutex>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "MvCameraControl.h"
#include "skillscontrol.hpp"

namespace hardware
{
class camera
{
private:
    cv::Mat img;                                            ///< 最近一帧（cv::Mat 拷贝是浅拷贝）
    std::vector<unsigned char> frameBuf;                    ///< 取图缓冲（复用，避免每帧 new[]/delete[] 十几 MB）

    // ---- MV_CS060_10UC_PRO ----
    void*             MV_CS060_10UC_PRO_handle  = nullptr;  ///< 设备句柄
    std::mutex        MV_CS060_10UC_PRO_mutex;              ///< 互斥锁（公开接口只加一次锁，内部一律走 *_locked）
    std::atomic<bool> MV_CS060_10UC_PRO_isGrabbing{false};  ///< 采集状态
    int               MV_CS060_10UC_PRO_width   = 0;        ///< 宽度（由 SDK 查询，失败不写）
    int               MV_CS060_10UC_PRO_height  = 0;        ///< 高度

    // ---- 配置（构造时从 config.yaml 读一次，后面不再读文件）----
    std::string name;                                       ///< 相机型号
    int         cameraIndex    = 0;                         ///< 设备索引
    double      cameraExposure = 30000.0;                   ///< 曝光时间(us)
    int         grabTimeoutMs  = 300;                       ///< 单帧取图超时(ms)，应 ≤ 技能链轮间隔
    std::string configPath;                                 ///< 实际使用的 config.yaml 路径（排查用）

    sc::tools::Memory *memory = nullptr;

    // ============ 加锁版本：调用方必须已经持有 MV_CS060_10UC_PRO_mutex ============
    // 之所以要拆开，是因为 release 里需要调用 stop：如果两者都各自加锁，
    // 同一个 std::mutex 被同一线程二次加锁会**死锁**（std::mutex 不可重入）。
    void stop_locked();
    void release_locked();

    /**
     * @brief 找到 config.yaml 的路径
     * @return 绝对路径（优先），找不到时返回相对路径兜底
     * @note  用 dladdr 反查本 .so 自己的位置，这样从任何工作目录启动都能找到配置，
     *        不再依赖"必须在发布根目录启动"。
     */
    static std::string locateConfigFile();

    /**
     * @brief 读取配置
     * @param path config.yaml 路径
     * @param err  失败原因
     * @return true 读取成功
     * @note  自己用 YAML::LoadFile 读（不走框架的 sc::tools::load），
     *        并且失败只返回 false —— 绝不能让读配置把宿主进程带走。
     */
    bool loadConfig(const std::string& path, std::string& err);

public:
    /*
     * 函数名称 : init_MV_CS060_10UC_PRO()
     * 函数作用 : MV-CS060-10UC-PRO摄像头初始化
     * 输入 : int cameraIndex = 0 USB参数
     * 输出 : bool 是否成功
     */
    bool init_MV_CS060_10UC_PRO(int cameraIndex = 0);

    /*
     * 函数名称 : start_MV_CS060_10UC_PRO()
     * 函数作用 : MV-CS060-10UC-PRO摄像头开始采集
     * 输入 : 无
     * 输出 : bool 是否成功
     */
    bool start_MV_CS060_10UC_PRO();

    /*
     * 函数名称 : stop_MV_CS060_10UC_PRO()
     * 函数作用 : MV-CS060-10UC-PRO摄像头停止采集
     * 输入 : 无
     * 输出 : 无
     */
    void stop_MV_CS060_10UC_PRO();

    /*
     * 函数名称 : release_MV_CS060_10UC_PRO()
     * 函数作用 : MV-CS060-10UC-PRO摄像头释放资源（停采集 + 关设备 + 销毁句柄）
     * 输入 : 无
     * 输出 : 无
     */
    void release_MV_CS060_10UC_PRO();

    /*
     * 函数名称 : image_MV_CS060_10UC_PRO()
     * 函数作用 : MV-CS060-10UC-PRO摄像头获取图像
     * 输入 : 无
     * 输出 : cv::Mat 返回图像矩阵，失败返回空 Mat
     */
    cv::Mat image_MV_CS060_10UC_PRO();

    /*
     * 函数名称 : camera()
     * 函数作用 : 摄像头初始化函数（读配置 + 打开设备，不做任何界面显示）
     * 输入 : sc::tools::Memory* 公共内存（由 init() 注入）
     * 输出 : 无
     */
    camera(sc::tools::Memory *memory);

    /**
     * @brief 默认构造
     * @note  memory 一定要在 init() 里面传入
     */
    camera() = default;

    /*
     * 函数名称 : ~camera()
     * 函数作用 : 摄像头析构函数，处理垃圾资源
     * 输入 : 无
     * 输出 : 无
     */
    ~camera();

    /*
     * 函数名称 : start()
     * 函数作用 : 摄像头开始采集
     * 输入 : 无
     * 输出 : bool 是否开启成功
     */
    bool start();

    /*
     * 函数名称 : stop()
     * 函数作用 : 摄像头停止采集
     * 输入 : 无
     * 输出 : 无
     */
    void stop();

    /*
     * 函数名称 : release()
     * 函数作用 : 摄像头释放资源
     * 输入 : 无
     * 输出 : 无
     */
    void release();

    /*
     * 函数名称 : image()
     * 函数作用 : 取一帧图像
     * 输入 : 无
     * 输出 : cv::Mat 图像矩阵（失败为空 Mat）
     */
    cv::Mat image();

    /// @brief 当前使用的配置文件路径（排查用）
    const std::string& configFile() const { return configPath; }
}; // class camera


}; // namespace hardware

#endif // CAMERA_HPP
