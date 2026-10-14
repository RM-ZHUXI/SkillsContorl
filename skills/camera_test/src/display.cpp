/**
 * @file display.cpp
 * @author o_O a17398481273@163.com
 * @brief 图像显示窗口实现（单 GUI 线程、可反复 start/stop）
 * @version 1.5.0
 * @date 2026/10/14
 */

#include "display.hpp"

#include "tools/logger.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <system_error>

namespace viewer
{

Displayer::Displayer(sc::tools::Memory *mem, const Config &cfg)
    : mem_(mem), cfg_(cfg)
{
}

Displayer::~Displayer()
{
    shutdown();     // 幂等：只有析构（DLL 卸载 / 进程退出）才真正收尾
}

bool Displayer::start()
{
    if (!cfg_.window)
    {
        // 配置里没开窗口：不算失败，只是没有窗口
        return false;
    }

    // 已经失败过：不再重试，避免调用者每轮都进来刷日志
    if (windowFailed_.load())
    {
        return false;
    }

    // ★ 窗口线程一辈子只起一次。
    //   旧实现每次 end() 都把 Displayer 销毁、下次重新 new 一个新的 Displayer，
    //   于是每次启动都在**新线程**上重新 namedWindow —— OpenCV 的 Win32 HighGUI
    //   后端扛不住这种用法：第二次建窗会永久阻塞，随后 end() 里的 join 就永远等不到。
    if (!windowThread_.joinable())
    {
        shouldExit_    = false;
        windowStop_    = false;
        windowRunning_ = false;
        windowFailed_  = false;
        exited_        = false;
        windowThread_  = std::thread(&Displayer::windowLoop, this);
    }

    // 窗口已经建好：这里只负责"恢复渲染"，不碰任何 HighGUI 函数
    if (windowRunning_.load())
    {
        windowStop_ = false;
        return true;
    }

    // 等 windowThread_ 把窗口建起来（建不起来它会置 windowFailed_）
    for (int i = 0; i < 100; ++i)
    {
        if (windowRunning_.load())
        {
            windowStop_ = false;
            return true;
        }
        if (windowFailed_.load()) break;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    return windowRunning_.load();
}

void Displayer::stop()
{
    // ★ 只暂停渲染：窗口和 GUI 线程都保留（不 join、不关窗）。
    //   关窗/回收线程留给 shutdown()，这样调用方（技能链线程）永远不会被
    //   一个卡在 HighGUI 里的窗口线程拖住 —— "停止链"必须是能立刻返回的。
    windowStop_ = true;
}

void Displayer::shutdown()
{
    // ★ 只置 shouldExit_，**不要**在这里再置 windowStop_。
    //   置了 windowStop_ 会让 GUI 线程先拐进"暂停"分支去做一次 imshow/setWindowTitle，
    //   而收尾阶段的这两下 GUI 调用是最容易卡住的（实测线程就死在那儿，
    //   导致下面等 3s 也等不到 exited_，只能放弃等待）。
    //   不碰 windowStop_ 时，线程走的是它一直在走的正常渲染路径，一个 waitKey(15)
    //   的周期内就能看到 shouldExit_ 并自己关窗退出。
    shouldExit_ = true;

    if (!windowThread_.joinable()) return;

    // 等 GUI 线程自己收尾（正常情况一个 waitKey 周期内就退出了）。
    // 带超时兜底：OpenCV 的 HighGUI 在 Windows 上偶尔会把窗口线程卡在内部调用里
    // （实测 waitKey/imshow/destroyWindow 都有机会卡住），绝不能因此把宿主的
    // 退出流程一起拖住，所以等不到就 detach 走人。
    for (int i = 0; i < 50 && !exited_.load(); ++i)      // 最多约 1s
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }

    if (exited_.load())
    {
        windowThread_.join();
    }
    else
    {
        sc::tools::logger()->warn("[camera_test] 显示线程卡在 HighGUI 里没退出，已放弃等待"
                                  "（不影响停止链：end() 不 join 显示线程；窗口随进程回收）");
        windowThread_.detach();       // 宁可留一个僵线程，也不能让调用方卡死
    }

    windowRunning_ = false;

    std::lock_guard<std::mutex> lk(frameMtx_);
    latest_ = cv::Mat();
}

void Displayer::show(const cv::Mat &frame)
{
    if (frame.empty() || !windowRunning_.load()) return;

    try
    {
        cv::Mat bgr = toBgr8(frame);
        if (bgr.empty())
        {
            sc::tools::logger()->warn("[camera_test] 不支持的图像类型，无法显示: type={}",
                                      frame.type());
            return;
        }

        // 太大的话等比缩一下，避免窗口/内存压力过大
        if (cfg_.windowWidth > 0 && bgr.cols > cfg_.windowWidth)
        {
            const double s = static_cast<double>(cfg_.windowWidth) /
                             static_cast<double>(bgr.cols);
            cv::resize(bgr, bgr, cv::Size(), s, s, cv::INTER_AREA);
        }

        std::lock_guard<std::mutex> lk(frameMtx_);
        latest_ = bgr.clone();          // clone：外部缓冲可能被复用
    }
    catch (const std::exception &e)
    {
        // 显示失败绝不能把技能/宿主带崩
        sc::tools::logger()->warn("[camera_test] 投递帧失败: {}", e.what());
    }
}

// ============================================================
// 内部实现
// ============================================================

cv::Mat Displayer::toBgr8(const cv::Mat &src)
{
    if (src.empty()) return cv::Mat();

    cv::Mat out;

    switch (src.channels())
    {
        case 1: cv::cvtColor(src, out, cv::COLOR_GRAY2BGR); break;
        case 3: out = src; break;                         // 相机写进内存的就是 BGR
        case 4: cv::cvtColor(src, out, cv::COLOR_BGRA2BGR); break;
        default: return cv::Mat();
    }

    if (out.depth() != CV_8U)
    {
        cv::Mat tmp;
        out.convertTo(tmp, CV_8U);                        // 深度不一致就拉伸到 8 位
        out = tmp;
    }

    return out;
}

cv::Mat Displayer::makeWaitingImage() const
{
    cv::Mat img(360, 640, CV_8UC3, cv::Scalar(24, 27, 34));

    cv::putText(img, "waiting for image in memory ...",
                cv::Point(40, 180), cv::FONT_HERSHEY_SIMPLEX, 0.8,
                cv::Scalar(200, 200, 200), 2, cv::LINE_AA);

    cv::putText(img, "source key: " + cfg_.sourceKey,
                cv::Point(40, 220), cv::FONT_HERSHEY_SIMPLEX, 0.6,
                cv::Scalar(140, 140, 140), 1, cv::LINE_AA);

    return img;
}

// ============================================================
// 空格键保存当前帧
// ============================================================

bool Displayer::saveCurrentFrame()
{
    if (cfg_.imageSave.empty())
    {
        sc::tools::logger()->warn("[camera_test] 未配置 image_save，无法保存画面");
        return false;
    }

    if (lastShownFrame_.empty())
    {
        sc::tools::logger()->warn("[camera_test] 当前没有可保存的画面");
        return false;
    }

    try
    {
        // 确保保存目录存在（多级一起建）
        std::error_code ec;
        std::filesystem::create_directories(cfg_.imageSave, ec);
        if (ec)
        {
            sc::tools::logger()->warn("[camera_test] 创建保存目录失败: {} ({})",
                                      cfg_.imageSave, ec.message());
            return false;
        }

        // 时间戳文件名：yyyymmdd_HHMMSS_mmm.jpg
        const auto now = std::chrono::system_clock::now();
        const auto t   = std::chrono::system_clock::to_time_t(now);
        const auto ms  = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                now.time_since_epoch()).count() % 1000);

        std::tm tm{};
    #if defined(_WIN32)
        localtime_s(&tm, &t);
    #else
        localtime_r(&t, &tm);
    #endif

        char name[64];
        std::snprintf(name, sizeof(name),
                      "%04d%02d%02d_%02d%02d%02d_%03d.jpg",
                      tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                      tm.tm_hour, tm.tm_min, tm.tm_sec,
                      ms);

        const std::filesystem::path out =
            std::filesystem::path(cfg_.imageSave) / name;

        if (!cv::imwrite(out.string(), lastShownFrame_))
        {
            sc::tools::logger()->warn("[camera_test] 保存画面失败: {}", out.string());
            return false;
        }

        // 顺手把绝对路径打出来，方便用户确认写到了哪儿
        std::error_code ec2;
        const auto abs = std::filesystem::absolute(out, ec2);
        sc::tools::logger()->info("[camera_test] 已保存画面: {}",
                                  ec2 ? out.string() : abs.string());
        return true;
    }
    catch (const std::exception &e)
    {
        sc::tools::logger()->warn("[camera_test] 保存画面异常: {}", e.what());
        return false;
    }
}

// ============================================================
// GUI 线程
// ============================================================

void Displayer::windowLoop()
{
    try
    {
        // ★ 整条技能生命周期里，namedWindow 只在这里被调用一次。
        //   所有 GUI 调用都在本线程上，绝不跨线程。
        cv::namedWindow(cfg_.windowName, cv::WINDOW_AUTOSIZE);

        const cv::Mat waiting = makeWaitingImage();
        cv::imshow(cfg_.windowName, waiting);      // 先显示占位图
        cv::waitKey(1);

        windowRunning_ = true;
        sc::tools::logger()->info("[camera_test] 显示窗口已开启: {}", cfg_.windowName);

        while (!shouldExit_.load())
        {
            // 暂停期间：只 pump 消息，**不做任何 imshow/setWindowTitle**，
            // **也不处理任何按键**（包括空格）。这正是 "end() 之后 GUI 停止工作" 的保证。
            //   收尾阶段那两下 GUI 调用是最容易卡住的：实测只要在暂停时重画一次
            //   （哪怕只是画占位图），窗口线程就会卡在 HighGUI 里出不来，
            //   于是 ~Displayer() 里的等待超时、窗口残留。宁可暂停时画面定住不动。
            if (windowStop_.load())
            {
                cv::waitKey(15);
                continue;
            }

            cv::Mat frame;
            {
                std::lock_guard<std::mutex> lk(frameMtx_);
                if (!latest_.empty())
                {
                    frame = latest_;
                    latest_ = cv::Mat();
                }
            }

            if (!frame.empty())
            {
                cv::imshow(cfg_.windowName, frame);
                cv::setWindowTitle(cfg_.windowName,
                                   cfg_.windowName + "  #" + std::to_string(frames_.load()));
                lastShownFrame_ = frame;   // ★ 保留最近显示的帧（供空格键保存）
                ++frames_;
            }

            // waitKey 既刷新窗口，也用来收按键：ESC / q 只停渲染；空格保存当前帧
            const int key = cv::waitKey(15);
            if (key == 27 || key == 'q' || key == 'Q')
            {
                sc::tools::logger()->info("[camera_test] 收到按键，停止渲染（窗口保留）");
                windowStop_ = true;
            }
            else if (key == ' ')
            {
                // ★ 空格键：把当前显示的这一帧保存到磁盘
                saveCurrentFrame();
            }
        }
    }
    catch (const std::exception &e)
    {
        // 没有 DISPLAY / OpenCV 未编 GUI 支持时会走到这里
        sc::tools::logger()->error("[camera_test] 显示窗口不可用: {}（需要图形环境；"
                                   "无显示环境请用别的技能把画面写成文件或走接口）", e.what());
        windowFailed_ = true;
    }
    catch (...)
    {
        sc::tools::logger()->error("[camera_test] 显示窗口未知异常");
        windowFailed_ = true;
    }

    windowRunning_ = false;

    // ★ 这里**故意不调用 cv::destroyWindow**。
    //   实测（DIAG 日志）本线程能正常退出渲染循环，但一进 cv::destroyWindow 就
    //   永久阻塞："准备关窗"打出来了、"关窗完成"永远打不出来 —— 于是 exited_
    //   永远为 false，shutdown() 只能超时后放弃等待，窗口也残留。
    //   现在整个 DLL 生命周期里窗口只创建一次、不销毁：既不会卡，
    //   也不会再有"反复建/销窗口"带来的 HighGUI 问题。
    //   窗口在进程退出时由系统统一回收（对 GUI 程序这是常规做法）。
    exited_ = true;      // 通知 shutdown()：可以安全 join 了
}

} // namespace viewer