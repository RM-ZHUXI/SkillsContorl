/**
 * @file        memory.hpp
 * @brief       公共内存管理器
 * @author      o_O a17398481273@163.com
 * @date        2026-11-21
 */

#ifndef GROUP_MEMORY_HPP
#define GROUP_MEMORY_HPP

#include "tools_api.hpp"

#include <iostream>
#include <map>
#include <any>
#include <stdexcept>

namespace sc::group
{

class TOOLS_API memory
{
private:
std::map<std::string,std::any> content_; ///< 核心储存体

public:
/**
 * @brief 默认构造函数
 */
memory() = default;

/**
 * @brief  添加新键值
 * @param  name  插入键值名称
 * @return false 插入失败，有键值存在/其他问题
 * @return true  正常插入
 * @note   默认初始为0
 */
bool set(std::string name);

/**
 * @brief  添加/覆盖键值
 * @param  name    插入键值名称
 * @param  content 插入内容
 * @return false   插入失败，有键值存在/其他问题
 * @return true    正常插入
 */
bool set(std::string name,std::any content);

/**
 * @brief  删除键值
 * @param  name    插入键值名称
 * @return false   插入失败，有键值存在/其他问题
 * @return true    正常插入
 */
bool erase(std::string name);

/**
 * @brief  获取键值
 * @param  name    插入键值名称
 * @param  content 目标引用
 * @return false   获取失败，键值不存在/类型不匹配
 * @return true    获取成功
 */
template<typename T>
bool get(std::string name,T& content)
{
    auto it = content_.find(name);
    if (it == content_.end())
        return false;

    const T* p = std::any_cast<T>(&it->second);
    if (p == nullptr)
        return false;

    content = *p;
    return true;
}

/**
 * @brief 取得对象
 * @param name     插入键值名称
 * @return 目标对象
 * @throw  std::out_of_range   键值不存在
 * @throw  std::bad_any_cast   类型不匹配
 */
template<typename T>
T operator [](const std::string name)
{
    auto it = content_.find(name);
    if (it == content_.end())
        throw std::out_of_range("[group::memory] 键值不存在: " + name);

    return std::any_cast<T>(it->second);
}


};// namespace memeory

} // namespace sc::group


#endif