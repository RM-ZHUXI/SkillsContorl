/**
 * @file display.hpp
 * @author o_O a17398481273@163.com
 * @brief 图像显示窗口实现（单 GUI 线程、可反复 start/stop）
 * @version 1.5.0
 * @date 2026/10/14
 */

#ifndef VIEWER_DISPLAY_HPP
#define VIEWER_DISPLAY_HPP

#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include <opencv2/opencv.hpp>

#include "tools/memory.hpp"

namespace viewer
{

class Displayer
{
public:
    struct Config
    {
        std::string sourceKey   = "image";                    ///< 从公共内存哪个键取图
        bool        window      = true;                       ///< 是否开窗口
        int         windowWidth = 1280;                       ///< 窗口最大宽度(px)，0=不缩放
        std::string windowName  = "camera_test · 图像显示";   ///< 窗口标题
        std::string imageSave   = "";                         ///< 空格键保存画面的目录
    };

    /**
     * @brief 构造
     * @param mem 公共内存管理器（由 skillcontorl 注入）
     * @param cfg 配置
     */
    Displayer(sc::tools::Memory *mem, const Config &cfg);

    /**
     * @brief 析构：真正收尾（join 窗口线程 + 窗口随进程回收）
     */
    ~Displayer();

    Displayer(const Displayer &)            = delete;
    Displayer &operator=(const Displayer &) = delete;

    /**
     * @brief 启动/恢复显示
     * @return true 表示窗口可用；false 表示配置关了窗口或建窗失败
     */
    bool start();

    /**
     * @brief 暂停渲染（不 join、不关窗，调用方立刻返回）
     */
    void stop();

    /**
     * @brief 收尾：等窗口线程退出，超时则 detach（留给 DLL 卸载 / 进程退出）
     */
    void shutdown();

    /**
     * @brief 投递一帧（由技能链线程调用）
     * @param frame 任意 OpenCV Mat，内部会转 BGR8 并克隆
     */
    void show(const cv::Mat &frame);

    /// @brief 是否建窗失败过（失败后不再重试）
    bool failed() const { return windowFailed_.load(); }

    /// @brief 已显示帧数
    long frames() const { return frames_.load(); }

private:
    cv::Mat toBgr8(const cv::Mat &src);        ///< 转 BGR8（灰度/ BGRA / 深度都能吃）
    cv::Mat makeWaitingImage() const;          ///< 窗口占位图
    bool    saveCurrentFrame();                ///< 保存当前显示帧到磁盘
    void    windowLoop();                      ///< GUI 线程主循环

private:
    sc::tools::Memory *mem_ = nullptr;         ///< 公共内存（未使用，保留给后续扩展）
    Config             cfg_;                   ///< 配置副本

    mutable std::mutex frameMtx_;              ///< 保护 latest_
    cv::Mat            latest_;                ///< 待显示的最新帧

    std::thread        windowThread_;          ///< GUI 线程
    std::atomic<bool>  shouldExit_{false};     ///< 通知 GUI 线程退出
    std::atomic<bool>  windowStop_{false};     ///< 暂停渲染（不退出线程）
    std::atomic<bool>  windowRunning_{false};  ///< 窗口是否已建好
    std::atomic<bool>  windowFailed_{false};   ///< 建窗是否失败过
    std::atomic<bool>  exited_{false};         ///< GUI 线程是否已退出
    std::atomic<long>  frames_{0};             ///< 已显示帧数

    cv::Mat            lastShownFrame_;        ///< 最近显示的帧（仅 GUI 线程访问，空格键保存用）
};

} // namespace viewer

#endif // VIEWER_DISPLAY_HPP