
/**
 * @file        skillGroup.hpp
 * @brief       技能组管理器
 * @author      o_O a17398481273@163.com
 * @date        2026-11-26
 */

#ifndef GROUP_SKILLGROUP_HPP
#define GROUP_SKILLGROUP_HPP

#include <string>
#include <map>
#include <vector>
#include <stdexcept>
#include <utility>

#include "skillList.hpp"
#include "memory.hpp"

namespace sc::group
{

class SkillGroup
{
public:
    using GroupSkill = std::map<std::string, SkillList>;
    using iterator               = GroupSkill::iterator;
    using const_iterator         = GroupSkill::const_iterator;
    using reverse_iterator       = GroupSkill::reverse_iterator;
    using const_reverse_iterator = GroupSkill::const_reverse_iterator;

private:
    std::string name_;                  ///< Group名称
    sc::group::memory memory_;          ///< 当前Group的内存管理器
    GroupSkill skillList_;              ///< 技能链集合

public:
    /**
     * @brief 构造函数
     * @param name Group名称
     */
    explicit SkillGroup(std::string name) : name_(std::move(name)){}

    /**
     * @brief 析构函数
     */
    ~SkillGroup() = default;

    SkillGroup(const SkillGroup&) = delete;
    SkillGroup& operator=(const SkillGroup&) = delete;

    SkillGroup(SkillGroup&&) = delete;
    SkillGroup& operator=(SkillGroup&&) = delete;


    //================================
    //          基础信息
    //================================

    /**
     * @brief 获取Group名称
     */
    const std::string& name() const
    {
        return name_;
    }


    //================================
    //          查找技能链
    //================================

    /**
     * @brief 获取指定SkillList
     * @param name SkillList名称
     * @return 成功返回迭代器，失败返回end()
     */
    iterator get(const std::string& name)
    {
        return skillList_.find(name);
    }

    /**
     * @brief const版本获取SkillList
     */
    const_iterator get(const std::string& name) const
    {
        return skillList_.find(name);
    }


    //================================
    //          创建技能链
    //================================

    /**
     * @brief 创建新的SkillList
     * @param name SkillList名称
     * @param list 技能链(技能名称集合)
     * @return 创建成功返回迭代器，失败返回end()
     */
    iterator addList(const std::string& name,const std::vector<std::string>& list)
    {
        if (skillList_.find(name) != skillList_.end())
            return end();

        auto result = skillList_.try_emplace(name,name,&memory_);

        if (!result.second)
            return end();

        if (!list.empty())
        {
            if (!result.first->second.set(list))
            {
                skillList_.erase(result.first);
                return end();
            }
        }

        return result.first;
    }


    //================================
    //          删除技能链
    //================================

    /**
     * @brief 删除指定SkillList
     * @param name SkillList名称
     * @return true删除成功，false不存在
     */
    bool erase(const std::string& name)
    {
        return skillList_.erase(name) > 0;
    }

    /**
     * @brief 获取指定SkillList，不存在则创建
     * @param name SkillList名称
     * @return SkillList引用
     * @throw std::runtime_error 创建失败
     */
    SkillList& operator[](const std::string& name)
    {
        auto item = get(name);

        if (item != end())
            return item->second;

        auto result = addList(name, {});

        if (result == end())
            throw std::runtime_error(
                "[SkillGroup] 创建 SkillList 失败: " + name
            );

        return result->second;
    }


    //================================
    //          正向迭代器
    //================================

    /**
     * @brief 获取起始迭代器
     */
    iterator begin()
    {
        return skillList_.begin();
    }

    /**
     * @brief 获取结束迭代器
     */
    iterator end()
    {
        return skillList_.end();
    }

    /**
     * @brief const起始迭代器
     */
    const_iterator begin() const
    {
        return skillList_.begin();
    }

    /**
     * @brief const结束迭代器
     */
    const_iterator end() const
    {
        return skillList_.end();
    }


    //================================
    //          反向迭代器
    //================================

    /**
     * @brief 获取反向起始迭代器
     */
    reverse_iterator rbegin()
    {
        return skillList_.rbegin();
    }

    /**
     * @brief 获取反向结束迭代器
     */
    reverse_iterator rend()
    {
        return skillList_.rend();
    }

    /**
     * @brief const反向起始迭代器
     */
    const_reverse_iterator rbegin() const
    {
        return skillList_.rbegin();
    }

    /**
     * @brief const反向结束迭代器
     */
    const_reverse_iterator rend() const
    {
        return skillList_.rend();
    }


    //================================
    //          const迭代器
    //================================

    const_iterator cbegin() const
    {
        return skillList_.cbegin();
    }

    const_iterator cend() const
    {
        return skillList_.cend();
    }

    const_reverse_iterator crbegin() const
    {
        return skillList_.crbegin();
    }

    const_reverse_iterator crend() const
    {
        return skillList_.crend();
    }


    //================================
    //          容量查询
    //================================

    /**
     * @brief 获取SkillList数量
     */
    std::size_t size() const
    {
        return skillList_.size();
    }

    /**
     * @brief 判断是否为空
     */
    bool empty() const
    {
        return skillList_.empty();
    }

    /**
     * @brief 清空所有SkillList
     */
    void clear()
    {
        skillList_.clear();
    }

};

} // namespace sc::group

#endif // GROUP_SKILLGROUP_HPP
