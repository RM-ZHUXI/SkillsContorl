#include "tools/logger.hpp"

#include <fmt/chrono.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
#endif

#ifdef __linux__
    #include <csignal>
    #include <execinfo.h>
#endif

namespace sc::tools
{
std::shared_ptr<spdlog::logger> logger_ = nullptr;

namespace
{
/// 是否走了正常退出流程（main 收尾时由 markNormalExit() 置位）
bool g_normalExit = false;

/**
 * @brief 直接往 stderr 写一段文本
 * @note  退出/崩溃阶段不能用 spdlog（静态对象可能已经析构）
 */
void writeStderr(const char* text)
{
    if (text == nullptr) return;

#ifdef _WIN32
    ::_write(2, text, static_cast<unsigned int>(std::strlen(text)));
#else
    ssize_t n = ::write(2, text, std::strlen(text));
    (void)n;
#endif
}

/**
 * @brief 进程退出时的检查（atexit 注册）
 * @note  没调用过 markNormalExit() 就退出了，说明是别处直接调用了 exit()/_exit()：
 *        这种情况日志会"停在某一行就没有了"，最难查。这里补一条明确提示。
 */
void onProcessExit()
{
    if (g_normalExit) return;

    writeStderr("\n[logger] 进程被异常终止：没有走正常退出流程。\n"
                "[logger] 最常见原因：某个技能（或它调用的第三方库/SDK）直接调用了 exit()。\n"
                "[logger] 排查：看日志最后一行是哪个技能的输出；"
                "Linux 下可 gdb --args ./skillcontorl 复现后 bt 看调用栈。\n");
}

#ifdef __linux__
/**
 * @brief Linux 致命信号处理：把调用栈打到 stderr
 * @note  崩溃时日志会戛然而止，只有调用栈能告诉你死在哪一层。
 *        backtrace_symbols_fd 不分配内存，适合放在信号处理里。
 *        想看到函数名需要在链接时加 -rdynamic（见根 CMakeLists.txt）。
 */
void onFatalSignal(int sig)
{
    const char* name = "UNKNOWN";
    switch (sig)
    {
        case SIGSEGV: name = "SIGSEGV(段错误)"; break;
        case SIGABRT: name = "SIGABRT(abort/terminate)"; break;
        case SIGBUS:  name = "SIGBUS"; break;
        case SIGFPE:  name = "SIGFPE(算术异常)"; break;
        case SIGILL:  name = "SIGILL(非法指令)"; break;
        default: break;
    }

    void* frames[64];
    const int n = backtrace(frames, 64);

    char head[384];
    const int len = std::snprintf(head, sizeof(head),
        "\n[logger] 收到致命信号 %s -> 进程即将终止，调用栈如下:\n"
        "[logger] （用 addr2line -Cfipe <程序或.so> <地址> 可定位到具体行）\n", name);
    if (len > 0) writeStderr(head);

    if (n > 0) backtrace_symbols_fd(frames, n, 2);

    // 恢复默认处理并重新触发信号，保留系统的 core dump 行为
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}
#endif
}  // namespace

/**
 * @brief 把 Windows 控制台切到 UTF-8 代码页（65001）
 * @return 切换前的输出代码页；0 表示没有附加控制台（输出被重定向）
 * @note  控制台默认是 936(GBK)，而程序（spdlog + 技能里的 std::cout）输出的是
 *        UTF-8 字节流，不切代码页就会被按 GBK 解释，中文变成
 *        「鏇存柊skills鏁版嵁」这种乱码。日志文件本身一直是正确的 UTF-8，
 *        只有屏幕上的控制台显示受影响。
 * @note  输出被重定向到文件/管道时这两个 API 会失败，此时保持原样即可
 *        （重定向出去的内容本来就是 UTF-8，用 UTF-8 方式打开就是对的）。
 */
int setupConsoleUtf8()
{
#ifdef _WIN32
    const UINT oldCp = GetConsoleOutputCP();
    if (oldCp == 0) return 0;                 // 没有控制台，跳过

    if (oldCp != CP_UTF8)
    {
        SetConsoleOutputCP(CP_UTF8);          // 输出的 UTF-8 字节按 UTF-8 解释
        SetConsoleCP(CP_UTF8);                // 输入同理（便于将来做交互式控制台）
    }

    return static_cast<int>(oldCp);
#else
    return 0;
#endif
}

void markNormalExit()
{
    g_normalExit = true;
}

void set_logger()
{
  // 先修控制台代码页，再创建日志：保证第一条日志就已经不乱码
  const int oldCp = setupConsoleUtf8();

  auto file_name = fmt::format("logs/{:%Y-%m-%d_%H-%M-%S}.log", std::chrono::system_clock::now());
  auto file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(file_name, true);
  file_sink->set_level(spdlog::level::debug);

  auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
  console_sink->set_level(spdlog::level::debug);

  logger_ = std::make_shared<spdlog::logger>("", spdlog::sinks_init_list{file_sink, console_sink});
  logger_->set_level(spdlog::level::debug);
  logger_->flush_on(spdlog::level::info);

  // ---- 退出守卫：区分"正常退出"与"被 exit()/崩溃带走" ----
  std::atexit(onProcessExit);

#ifdef __linux__
  // ---- 崩溃守护：致命信号时把调用栈打到 stderr，避免日志戛然而止查不到死因 ----
  std::signal(SIGSEGV, onFatalSignal);
  std::signal(SIGABRT, onFatalSignal);
  std::signal(SIGBUS,  onFatalSignal);
  std::signal(SIGFPE,  onFatalSignal);
  std::signal(SIGILL,  onFatalSignal);
#endif

  //if (oldCp > 0 && oldCp != 65001)
    //logger_->info("[logger] 控制台输出代码页 {} -> 65001(UTF-8)，中文显示不再乱码", oldCp);
}

std::shared_ptr<spdlog::logger> logger()
{
  if (!logger_) set_logger();
  return logger_;
}

}  // namespace tools
