#include "threadController.hpp"

namespace sc::tools
{
/**
 * @brief ThreadItem 部分
 */

void ThreadItem::run()
{3
    if(function == nullptr)
    {
        throw std::runtime_error("[ThreadItem] 运行函数为空");
    }

    this->thread = std::thread(this->function,&(this->info));
}


} // namespace sc::tools