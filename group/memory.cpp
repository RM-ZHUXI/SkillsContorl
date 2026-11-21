#include "group/memory.hpp"

#include <utility>

namespace sc::group
{

bool memory::set(std::string name)
{
    if (content_.find(name) != content_.end())
        return false;

    content_.emplace(std::move(name), std::any(0));
    return true;
}

bool memory::set(std::string name, std::any content)
{
    content_.insert_or_assign(std::move(name), std::move(content));
    return true;
}

bool memory::erase(std::string name)
{
    return content_.erase(name) > 0;
}

} // namespace sc::group
