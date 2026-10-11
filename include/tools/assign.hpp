/**
 * @file        assign.hpp
 * @brief       负责交互skill链管理器
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-17
 * @version     1.0.0
 * @par         实现基础线性流程线程控制
 *
 * @version     1.1.0
 * @par         1. 修复技能链解析：补充 name 字段、list 支持"技能名"和"skill对象"两种写法
 *              2. 与 JSON 嵌合：技能链整表以 JSON 数组进出（getListsJson / setListsJson）
 *              3. 新增公共内存的 JSON 赋值接口：assignMemory / readMemoryValues / eraseMemory
 *                 供 net/http_server 直接使用，不用再来回拼字符串
 */

#ifndef ASSIGN_HPP
#define ASSIGN_HPP

#include <iostream>
#include <thread>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

#include "tools/type.hpp"
#include "tools/findskills.hpp"
#include "tools/memory.hpp"
#include "tools/tools_api.hpp"

namespace sc::tools
{

class TOOLS_API Assign
{
using json = nlohmann::json;

private:
    std::map<std::string,SkillList> list_;  ///< 技能链集合(以名称为唯一ID)
    std::string config_;                    ///< 配置文件(yaml)
    FindSkills &find_;                      ///< 技能查找类

    /**
     * @brief 读取配置里的技能链文件路径
     * @return std::string 技能链 json 文件路径
     */
    std::string listFilePath();

    /**
     * @brief 补全技能链里的技能信息
     * @param sl 技能链(其中的 skill 可能只有 name)
     * @note  list 里写的是技能名字符串时，这里用 FindSkills 查成完整的 Skill 对象；
     *        查不到直接抛异常，由调用方决定是整条链失败还是跳过
     */
    void resolve(SkillList& sl);

    /**
     * @brief 把技能链集合写回 json 文件
     * @param list 技能链集合
     * @return true 写入成功
     */
    bool save(const std::map<std::string, SkillList>& list);

public:
    /**
     * @brief 构造函数
     * @param config 配置文件
     * @param find 技能查找类
     * @note  构造时会自动 update() 一次，失败只告警不抛异常
     */
    Assign(std::string &config,FindSkills &find);

    /**
     * @brief 更新技能链(从 json 文件重新加载)
     * @return true 更新成功
     * @return false 更新失败(请检查是否重名/skill内容是否存在)
     */
    bool update();

    /**
     * @brief   设置技能链
     * @param   list 技能链集合
     * @return  true 设置成功
     * @return  false 设置失败(请检查是否重名/skill内容是否存在)
     * @note    内部调用update()方法产生bool
     */
    bool set(std::map<std::string,SkillList> &list);

    /**
     * @brief 获取技能链集合
     * @return json 技能链集合
     */
    std::string get_all_lists_json();

    // JSON 嵌合接口（供 http_server 直接使用）
    
    /**
     * @brief 技能链集合 -> JSON 数组
     * @return json [{"name","mode","interval","list":[skill...]}, ...]
     */
    json getListsJson() const;

    /**
     * @brief JSON 数组 -> 技能链集合（覆盖式）
     * @param arr 技能链数组，元素格式同 getListsJson()
     * @return true 解析并落盘成功
     * @note  解析时会用 FindSkills 把 "技能名" 补全成完整 skill；
     *        解析失败或技能不存在时返回 false，原技能链保持不变
     */
    bool setListsJson(const json& arr);

    /**
     * @brief 获取技能链集合副本
     */
    std::map<std::string, SkillList> getLists() const;

    /**
     * @brief 是否存在指定技能链
     */
    bool hasList(const std::string& name) const;

    /**
     * @brief 获取指定技能链
     * @throw std::runtime_error 技能链不存在
     */
    SkillList getList(const std::string& name) const;

    /**
     * @brief 新增/覆盖一条技能链并落盘
     */
    bool upsertList(const SkillList& sl);

    /**
     * @brief 删除一条技能链并落盘
     */
    bool removeList(const std::string& name);

    // ============================================================
    // 公共内存的 JSON 赋值（静态工具，不依赖技能链）
    // ============================================================

    /**
     * @brief 把 JSON 键值表写进公共内存
     * @param mem   公共内存
     * @param items JSON 对象 {"键": 值, ...}
     * @param report 输出报告 {"written":n,"failed":[{"name","message"},...]}
     * @return true 全部写入成功
     * @note  支持 bool / 整数 / 无符号 / 浮点 / 字符串 / 数值数组 / 字符串数组，
     *        已存在的键走 insertOrAssign（覆盖），不会像 insert 那样抛重名异常
     */
    static bool assignMemory(Memory& mem, const json& items, json& report);

    /**
     * @brief 读取公共内存的值（名字 + 类型 + 大小 + 值）
     * @param mem   公共内存
     * @param names 要读取的键；为空表示全部
     * @return json 数组 [{"name","type","size","value"}, ...]
     * @note  取不到值的键会带上 "exists":false，方便前端区分"空值"和"没这项"
     */
    static json readMemoryValues(Memory& mem, const std::vector<std::string>& names);

    /**
     * @brief 按键名批量删除
     * @return 实际删除条数
     */
    static std::size_t eraseMemory(Memory& mem, const std::vector<std::string>& names);

}; // class Assign

}  // namespace sc::tools


#endif // ASSIGN_HPP
