#include "main.hpp"

#include "tools/logger.hpp"

namespace
{
    sc::tools::Memory *mem = nullptr;
}

/**
 * @brief 程序初始化：只读配置，不创建 GUI
 */
SKILL_API void init(sc::tools::Memory *memory)
{
    if (memory == nullptr)
    {
        sc::tools::logger()->error("[camera_test] init 收到空 memory 指针，技能不会工作");
        return;
    }

    mem = memory;
}

SKILL_API void run()
{

}

SKILL_API void end()
{

}