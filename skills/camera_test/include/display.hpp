/**
 * @file display.hpp
 * @author 时新博 a17398481273@163.com
 * @brief 图像显示窗口实现
 * @version 1.4.0
 * @date 2026/10/10
 *
 * @par 设计要点
 *      - 所有 GUI 调用（namedWindow/imshow/waitKey/destroyWindow）都只在
 *        windowThread_ 这一个线程里执行，绝不在调用者线程直接调 GUI；
 *      - **窗口线程和窗口本身在 Displayer 生命周期内只创建一次**：
 *        OpenCV 的 Win32 HighGUI 后端经不起「多线程反复建窗口/销窗口」——
 *        第二次在**新线程**上 namedWindow 会永久阻塞，紧接着的 join 就永远等不到；
 *      - start()/stop() 只改状态标志，不做任何阻塞式 GUI 操作：
 *        start() 恢复渲染，stop() 暂停渲染（窗口保留）；
 *      - 真正的收尾（关窗 + 回收线程）只在 shutdown() 里做，而且带超时兜底，
 *        绝不会把调用方（技能链线程 / 宿主退出流程）拖死；
 *      - 失败后 failed() 返回 true，调用者据此不再重试，避免日志刷屏。
 *
 * @par v1.4.0 修复的问题（实测复现）
 *      旧实现每次 end() 都会 disp.reset() → 销毁 Displayer → 关窗 + join 窗口线程；
 *      下次 run() 又重新 new 一个 Displayer → 在一个**新的线程**上重新 namedWindow。
 *      实测第 1 次建窗 268ms 正常，第 2 次 3.1s 后失败且窗口线程卡在 HighGUI 里，
 *      随后 end() 里的 join 永久阻塞 → 网页上「停止」按钮卡死、请求超时，
 *      链再也停不下来（这就是"无法关闭技能"）。
 */

#ifndef VIEWER_DISPLAY_HPP
#define VIEWER_DISPLAY_HPP

#include <atomic>
#include <cstddef>
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
        std::string sourceKey   = "image";          ///< 从公共内存取图的 key
        bool        window      = true;             ///< 是否开窗口
        int         windowWidth = 0;                ///< 缩放宽（0=不缩放）
        std::string windowName  = "camera_test";    ///< 窗口标题
    };

    Displayer(sc::tools::Memory *mem, const Config &cfg);
    ~Displayer();

    Displayer(const Displayer &)            = delete;
    Displayer& operator=(const Displayer &) = delete;

    /**
     * @brief 启动 / 恢复渲染
     * @return true 窗口已就绪；false 失败（cfg_.window=false、曾经失败过、或建窗口失败）
     * @note  幂等：窗口已存在时只清掉「暂停」标志并立即返回。
     *        本函数不做任何 HighGUI 调用 —— 建窗口是 windowThread_ 自己的事。
     *        失败时最多等待 2s（等 GUI 线程把失败标志置起来），不会无限等。
     */
    bool start();

    /**
     * @brief 暂停渲染（窗口与 GUI 线程都保留）
     * @note  ★ 不阻塞、不 join、不关窗。以前这里是"停线程 + 关窗"，
     *        那样每次重启链都要在新线程上重建窗口，会卡死 HighGUI（见文件头说明）。
     */
    void stop();

    /**
     * @brief 彻底收尾：通知 GUI 线程自己关窗退出，然后回收线程
     * @note  只在 Displayer 析构（= DLL 卸载 / 进程退出）时调用。
     *        带超时兜底：万一 HighGUI 卡在某个调用里出不来，也绝不无限等待。
     */
    void shutdown();

    /**
     * @brief 投递一帧（非阻塞，clone 后交给显示线程）
     */
    void show(const cv::Mat &frame);

    /// @brief 窗口是否正在运行（窗口已建好）
    bool running() const { return windowRunning_.load(); }

    /// @brief 窗口是否曾经失败（失败后不再重试）
    bool failed()  const { return windowFailed_.load();  }

    /// @brief 已显示帧数
    std::size_t frames() const { return frames_.load(); }

private:
    cv::Mat toBgr8(const cv::Mat &src);
    cv::Mat makeWaitingImage() const;
    void    windowLoop();

private:
    sc::tools::Memory *mem_ = nullptr;
    Config             cfg_;

    // 状态标志
    std::atomic<bool>        shouldExit_{false};    ///< 真正退出线程（shutdown 置 true）
    std::atomic<bool>        windowStop_{false};    ///< 暂停渲染（线程仍 pump 消息）
    std::atomic<bool>        windowRunning_{false}; ///< 窗口已就绪
    std::atomic<bool>        windowFailed_{false};  ///< 窗口创建失败（不再重试）
    std::atomic<bool>        exited_{false};        ///< GUI 线程已经跑完（shutdown 用来判断能否 join）
    std::atomic<std::size_t> frames_{0};            ///< 已显示帧数

    std::thread windowThread_;
    std::mutex  frameMtx_;
    cv::Mat     latest_;                            ///< 最新一帧
};

} // namespace viewer

#endif // VIEWER_DISPLAY_HPP
