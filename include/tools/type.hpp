/**
 * @file        type.hpp
 * @brief       全局状态机包装类
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-15
 * 
 * @version     1.0.0
 *
 * @version     1.1.0
 * @par         新增 SkillList 的 JSON 双向序列化
 *              1. 兼容 skillsList.json 里的 "list" 键（而不是 "skills"）
 *              2. list 数组里既能写技能名字符串，也能写完整 skill 对象
 *              3. 新增 interval 字段（while 模式的轮间隔毫秒）
 *              4. 解析 skill 对象时字段可以缺省（前端只提交 {"name":"xxx"} 也认），
 *                 缺的字段由 FindSkills 在 Assign::resolve 里补全
 *
 * @version     1.2.0
 * @par         interval 语义修正 + 解析容错
 *              1. interval = 0 表示"不等待、全速循环"（原来 0 会被悄悄改成 1000，
 *                 表现就是界面上轮间隔怎么调都还是 1000 ms）；只有负数才回落默认值
 *              2. 容忍手改配置把数字写成字符串（"interval": "300"、"version": 1），
 *                 以前会直接抛 json 异常导致整条链（甚至全部链）加载失败
 */

#ifndef TYPE_HPP
#define TYPE_HPP 

#include <iostream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <regex>

#include "yaml.hpp"
#include "logger.hpp"
#include "time.hpp"
#include "tools_api.hpp"

/**
 * @brief 把一个 json 值宽松地转成字符串
 * @param v   json 值
 * @param def 无法转换时返回的默认值
 * @return 字符串；数字/布尔会被转成字符串
 */
inline std::string jsonValStr(const nlohmann::json& v, const std::string& def = std::string())
{
    if (v.is_string())          return v.get<std::string>();
    if (v.is_number_integer())  return std::to_string(v.get<long long>());
    if (v.is_number_unsigned()) return std::to_string(v.get<unsigned long long>());
    if (v.is_number_float())    return std::to_string(v.get<double>());
    if (v.is_boolean())         return v.get<bool>() ? "true" : "false";
    return def;
}

/**
 * @brief 从 json 对象里宽松地取一个字符串字段
 * @param j    json 对象
 * @param key  字段名
 * @param def  取不到时的默认值
 * @return 字符串；数字/布尔会被转成字符串，其它类型返回 def
 * @note  手改 json 配置时很容易把 "version": "1.0.0" 写成 "version": 1.0，
 *        直接用 nlohmann 的 value() 取字符串会抛 type_error 把整条链带崩，
 *        所以统一走这个宽松取值
 */
inline std::string jsonStr(const nlohmann::json& j, const char* key,
                           const std::string& def = std::string())
{
    if (!j.is_object() || !j.contains(key)) return def;
    return jsonValStr(j[key], def);
}

/**
 * @brief 从 json 对象里宽松地取一个整数字段
 * @param j   json 对象
 * @param key 字段名
 * @param def 取不到时的默认值
 * @return 整数；字符串会尝试解析（"300" -> 300），解析不了返回 def
 */
inline int jsonInt(const nlohmann::json& j, const char* key, int def)
{
    if (!j.is_object() || !j.contains(key)) return def;

    const nlohmann::json& v = j[key];
    try
    {
        if (v.is_number_integer())  return static_cast<int>(v.get<long long>());
        if (v.is_number_unsigned()) return static_cast<int>(v.get<unsigned long long>());
        if (v.is_number_float())    return static_cast<int>(v.get<double>());
        if (v.is_string())          return std::stoi(v.get<std::string>());
    }
    catch (...) { /* 落到默认值 */ }

    return def;
}

/**
 * @brief skills信息
 */
struct Skill
{
    std::string name;                                           ///< 名称
    std::string description;                                    ///< 描述
    std::string version;                                        ///< 版本
    std::string url;                                            ///< 运行开始dll库链接位置
    std::vector<std::string> author;                            ///< 作者                               
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(Skill, name, description, version, author, url)

/**
 * @brief 技能链（skill 链）
 */
struct SkillList
{
    std::string name;                         ///< 名称(唯一ID)
    std::string mode = "once";                ///< 模式 once / while
    int         interval = 1000;              ///< while 轮间隔(毫秒)，0 = 不等待，<0 按 1000
    std::vector<Skill> skills;                ///< 技能列表(按顺序执行)
};

/**
 * @brief 技能链 -> JSON
 * @note  输出 {"name","mode","interval","list":[skill 对象...]}
 *        与 configs/skillsList.json 的格式保持一致
 */
inline void to_json(nlohmann::json& j, const SkillList& s)
{
    j = nlohmann::json{
        {"name",     s.name},
        {"mode",     s.mode},
        {"interval", s.interval},
        {"list",     s.skills}
    };
}

/**
 * @brief JSON -> 技能链
 * @note  list 数组里的元素允许两种写法：
 *        1. 字符串  -> 只填 skill.name，稍后由 Assign 用 FindSkills 补全（见 Assign::resolve）
 *        2. 对象    -> 直接按 Skill 解析
 *        缺省字段用默认值补齐，不抛异常（方便前端只提交必要字段）；
 *        数字/字符串写混了也认（走 jsonStr / jsonInt 宽松解析）
 */
inline void from_json(const nlohmann::json& j, SkillList& s)
{
    if (!j.is_object()) return;

    s.name     = jsonStr(j, "name");
    s.mode     = jsonStr(j, "mode", "once");
    s.interval = jsonInt(j, "interval", 1000);
    s.skills.clear();

    if (!j.contains("list") || !j["list"].is_array()) return;

    for (const auto& item : j["list"])
    {
        if (item.is_string())
        {
            Skill sk;                       // 只带名字，等 Assign 去查
            sk.name = item.get<std::string>();
            s.skills.push_back(std::move(sk));
        }
        else if (item.is_object())
        {
            // 这里不能用 item.get<Skill>()：宏生成的 from_json 要求 5 个字段齐全，
            // 前端只提交 {"name":"xxx"} 时就会抛 key 'description' not found
            Skill sk;
            sk.name        = jsonStr(item, "name");
            sk.description = jsonStr(item, "description");
            sk.version     = jsonStr(item, "version");
            sk.url         = jsonStr(item, "url");

            if (item.contains("author") && item["author"].is_array())
            {
                for (const auto& a : item["author"])
                {
                    const std::string one = jsonValStr(a);
                    if (!one.empty()) sk.author.push_back(one);
                }
            }

            s.skills.push_back(std::move(sk));
        }
    }
}

/**
 * @brief 判断技能链模式是否合法
 * @param mode 模式字符串
 * @return true 合法（只认 once / while）
 */
inline bool skillListModeValid(const std::string& mode)
{
    return mode == "once" || mode == "while";
}

namespace sc::tools
{
    extern TOOLS_API bool STATUS;            ///< 程序是否允许运行
    extern TOOLS_API bool LOAD_STATUS;       ///< 程序是否加载完毕
}

#endif
