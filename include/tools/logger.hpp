#ifndef TOOLS__LOGGER_HPP
#define TOOLS__LOGGER_HPP

#include <spdlog/spdlog.h>

#include "tools/tools_api.hpp"

namespace sc::tools
{
/**
 * @brief 获取全局日志器（控制台 + logs/xxx.log，级别 debug）
 * @note  首次调用时会自动切换控制台代码页、安装退出/崩溃守护并创建日志文件
 */
TOOLS_API std::shared_ptr<spdlog::logger> logger();

/**
 * @brief 把 Windows 控制台切到 UTF-8 代码页（65001）
 * @return 切换前的输出代码页；0 表示没有控制台（输出被重定向）或非 Windows
 * @note  控制台默认是 936(GBK)，而 spdlog 与技能里的 std::cout 输出的是 UTF-8 字节流，
 *        不切代码页就会被按 GBK 解释，中文显示成「鏇存柊skills鏁版嵁」这类乱码。
 *        日志文件本身一直是正确的 UTF-8，只有屏幕显示受影响。
 *        该函数在 logger() 首次调用时已自动执行，一般不需要手动调用。
 */
TOOLS_API int setupConsoleUtf8();

/**
 * @brief 标记"本次是正常退出"
 * @note  main 走到收尾流程时调用。进程退出时会检查这个标记：
 *        - 有标记：什么都不做；
 *        - 没标记：说明是**别处直接调用了 exit()/_exit() 或崩了**（常见于技能里调用了
 *          会 exit 的第三方 SDK、或自己的错误处理里写了 exit），会在 stderr 打一条告警，
 *          便于区分"我主动退出"和"被别人杀了"。
 */
TOOLS_API void markNormalExit();

}  // namespace tools

#endif  // TOOLS__LOGGER_HPP