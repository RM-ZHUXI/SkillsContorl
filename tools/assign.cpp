/**
 * @file        assign.cpp
 * @brief       技能链管理器 + 公共内存的 JSON 赋值
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-17
 * @version     1.1.0
 */

#include "tools/assign.hpp"

namespace sc::tools
{

// ============================================================
// 内部工具
// ============================================================
namespace
{

/**
 * @brief std::any -> json
 * @param a 公共内存里的值
 * @return json 可识别的值；不支持的类型返回一句说明字符串
 * @note  只覆盖 memory.cpp 里显式实例化过的常用类型，
 *        新增类型时这里和 memory.cpp 的 TOOLS_INSTANTIATE_IO 列表要一起补
 */
nlohmann::json anyToJson(const std::any& a)
{
    using json = nlohmann::json;
    const std::type_info& t = a.type();

    // ---- 标量 ----
    if (t == typeid(bool))               return json(std::any_cast<bool>(a));
    if (t == typeid(char))               return json(std::string(1, std::any_cast<char>(a)));
    if (t == typeid(int))                return json(std::any_cast<int>(a));
    if (t == typeid(unsigned int))       return json(std::any_cast<unsigned int>(a));
    if (t == typeid(long long))          return json(std::any_cast<long long>(a));
    if (t == typeid(unsigned long long)) return json(std::any_cast<unsigned long long>(a));
    if (t == typeid(float))              return json(std::any_cast<float>(a));
    if (t == typeid(double))             return json(std::any_cast<double>(a));
    if (t == typeid(std::string))        return json(std::any_cast<const std::string&>(a));

    // ---- 数组 ----
    if (t == typeid(std::vector<int>))                return json(std::any_cast<const std::vector<int>&>(a));
    if (t == typeid(std::vector<unsigned int>))       return json(std::any_cast<const std::vector<unsigned int>&>(a));
    if (t == typeid(std::vector<long long>))          return json(std::any_cast<const std::vector<long long>&>(a));
    if (t == typeid(std::vector<unsigned long long>)) return json(std::any_cast<const std::vector<unsigned long long>&>(a));
    if (t == typeid(std::vector<float>))              return json(std::any_cast<const std::vector<float>&>(a));
    if (t == typeid(std::vector<double>))             return json(std::any_cast<const std::vector<double>&>(a));
    if (t == typeid(std::vector<std::string>))        return json(std::any_cast<const std::vector<std::string>&>(a));

    // ---- 字典 ----
    if (t == typeid(std::map<std::string, int>))         return json(std::any_cast<const std::map<std::string, int>&>(a));
    if (t == typeid(std::map<std::string, double>))      return json(std::any_cast<const std::map<std::string, double>&>(a));
    if (t == typeid(std::map<std::string, std::string>)) return json(std::any_cast<const std::map<std::string, std::string>&>(a));

    // 其它类型（cv::Mat 之类）给个可读提示，前端不至于显示成空白
    return json("<" + Memory::typeNameOf(t) + " 暂不支持转 json>");
}

/**
 * @brief 按 json 的值类型写入公共内存
 * @param mem  公共内存
 * @param name 键名
 * @param v    值
 * @param err  失败原因
 * @return true 写入成功
 */
bool writeOne(Memory& mem, const std::string& name, const nlohmann::json& v, std::string& err)
{
    try
    {
        if (v.is_boolean())
        {
            mem.insertOrAssign(name, v.get<bool>());
            return true;
        }

        // 注意：nlohmann 的 is_number_integer() 对无符号也是 true，所以无符号要先判
        if (v.is_number_unsigned())
        {
            mem.insertOrAssign(name, v.get<unsigned long long>());
            return true;
        }
        if (v.is_number_integer())
        {
            mem.insertOrAssign(name, v.get<long long>());
            return true;
        }
        if (v.is_number_float())
        {
            mem.insertOrAssign(name, v.get<double>());
            return true;
        }
        if (v.is_string())
        {
            mem.insertOrAssign(name, v.get<std::string>());
            return true;
        }

        if (v.is_array())
        {
            if (v.empty())
            {
                err = "空数组无法推断类型，请至少给一个元素";
                return false;
            }

            bool allString = true;
            bool allNumber = true;
            bool hasFloat  = false;

            for (const auto& e : v)
            {
                if (!e.is_string()) allString = false;
                if (!e.is_number()) allNumber = false;
                if (e.is_number_float()) hasFloat = true;
            }

            if (allString)
            {
                mem.insertOrAssign(name, v.get<std::vector<std::string>>());
                return true;
            }
            if (allNumber)
            {
                if (hasFloat)
                    mem.insertOrAssign(name, v.get<std::vector<double>>());
                else
                    mem.insertOrAssign(name, v.get<std::vector<long long>>());
                return true;
            }

            err = "数组元素类型不一致，只支持全字符串或全数字";
            return false;
        }

        err = std::string("不支持的值类型: ") + v.type_name();
        return false;
    }
    catch (const std::exception& e)
    {
        err = e.what();
        return false;
    }
}

} // anonymous namespace

// ============================================================
// 构造 / 技能链加载
// ============================================================

Assign::Assign(std::string &config,FindSkills &find)
: config_(config), find_(find)
{
    if (!update())
        logger()->warn("[Assign] 技能链初始加载失败，请检查 skillsList 配置文件");
}

std::string Assign::listFilePath()
{
    auto YAML = load(config_);
    return read<std::string>(YAML, "skillsList_file");
}

/**
 * @brief 把技能链里"只有名字"的技能补全成完整 Skill
 * @note  list 支持两种写法：技能名字符串 / 完整 skill 对象。
 *        字符串走这里用 FindSkills 查；查不到时如果对象自带 url 就放行并告警。
 * @throw std::runtime_error 元素没有 name，且也没有可用的 url
 */
void Assign::resolve(SkillList& sl)
{
    for (auto& sk : sl.skills)
    {
        if (sk.name.empty())
            throw std::runtime_error("[Assign] 技能链 " + sl.name + " 中有元素缺少 name");

        try
        {
            // 以 FindSkills 扫描到的信息为准，避免前端提交过期的 url
            sk = find_.get_skill_once(sk.name);
        }
        catch (const std::exception& e)
        {
            if (sk.url.empty())
                throw std::runtime_error("[Assign] 技能链 " + sl.name +
                                         " 引用了不存在的技能: " + sk.name);

            logger()->warn("[Assign] 技能链 {} 的技能 {} 不在扫描列表里，按提交的 url 使用: {}",
                           sl.name, sk.name, sk.url);
        }
    }
}

bool Assign::update()
{
    // LOAD_STATUS 表示"技能链是否加载完毕"：加载过程中置 false，
    // 全部成功才置 true，中途失败保持 false（前端据此提示"请先停止运行再更新"）
    sc::tools::LOAD_STATUS = false;

    list_.clear();

    const std::string path = listFilePath();

    std::ifstream ifs(path);
    if (!ifs.is_open())
    {
        logger()->error("[Assign] 无法打开 skillsList 文件: " + path);
        return false;
    }

    try
    {
        json root = json::parse(ifs);

        if (root.contains("skills_lists") && root["skills_lists"].is_array())
        {
            for (const auto& item : root["skills_lists"])
            {
                // 单条链出错只跳过这一条并告警，不再把整份配置判定为加载失败
                // （以前一条链写错，其它链会跟着一起丢）
                try
                {
                    SkillList s = item.get<SkillList>();   // 见 type.hpp 的 from_json

                    if (s.name.empty())
                    {
                        logger()->warn("[Assign] 技能链缺少 name 字段，已跳过");
                        continue;
                    }

                    if (list_.find(s.name) != list_.end())
                    {
                        logger()->warn("[Assign] 技能链重名，已跳过: " + s.name);
                        continue;
                    }

                    resolve(s);                            // 名字 -> 完整 Skill

                    if (s.mode.empty() || !skillListModeValid(s.mode))
                    {
                        logger()->warn("[Assign] 技能链 {} 的 mode 非法({})，按 once 处理", s.name, s.mode);
                        s.mode = "once";
                    }

                    // interval：0 = 不等待（全速循环），只有负数才回落默认值
                    if (s.interval < 0)
                    {
                        logger()->warn("[Assign] 技能链 {} 的 interval 为负({})，按默认 1000 处理",
                                       s.name, s.interval);
                        s.interval = 1000;
                    }

                    list_[s.name] = std::move(s);
                }
                catch (const std::exception& e)
                {
                    logger()->warn(std::string("[Assign] 跳过一条无法解析的技能链: ") + e.what());
                }
            }
        }
        else
        {
            logger()->warn("[Assign] " + path + " 里没有 skills_lists 数组");
        }
    }
    catch (const std::exception& e)
    {
        logger()->error(std::string("[Assign] 解析 skillsList 失败: ") + e.what());
        return false;
    }

    logger()->info("[Assign] 技能链加载完成，共 {} 条", list_.size());

    sc::tools::LOAD_STATUS = true;
    return true;
}

bool Assign::save(const std::map<std::string, SkillList>& list)
{
    const std::string path = listFilePath();

    try
    {
        json root;
        root["skills_lists"] = json::array();

        for (const auto& [key, sl] : list)
        {
            json item = sl;                 // 见 type.hpp 的 to_json
            root["skills_lists"].push_back(std::move(item));
        }

        std::ofstream f(path);
        if (!f.is_open())
        {
            logger()->error("[Assign] 打开文件失败: " + path);
            return false;
        }

        f << root.dump(4);
        f.close();
        return true;
    }
    catch (const std::exception& e)
    {
        logger()->error(std::string("[Assign] 写入失败: ") + e.what());
        return false;
    }
}

bool Assign::set(std::map<std::string, SkillList>& list)
{
    sc::tools::LOAD_STATUS = false;

    for (auto& [key, sl] : list)
    {
        try
        {
            resolve(sl);
        }
        catch (const std::exception& e)
        {
            logger()->warn(e.what());
            return false;
        }
    }

    if (!save(list)) return false;

    return this->update();
}

// ============================================================
// JSON 嵌合接口
// ============================================================

std::map<std::string, SkillList> Assign::getLists() const
{
    return list_;
}

nlohmann::json Assign::getListsJson() const
{
    json arr = json::array();
    for (const auto& [key, sl] : list_)
        arr.push_back(sl);              // 走 type.hpp 的 to_json
    return arr;
}

std::string Assign::get_all_lists_json()
{
    // 兼容旧接口：直接返回 json 字符串
    return getListsJson().dump(4);
}

bool Assign::setListsJson(const json& arr)
{
    if (!arr.is_array())
    {
        logger()->warn("[Assign] setListsJson 需要数组");
        return false;
    }

    std::map<std::string, SkillList> next;

    try
    {
        for (const auto& item : arr)
        {
            SkillList sl = item.get<SkillList>();

            if (sl.name.empty())
            {
                logger()->warn("[Assign] 技能链缺少 name 字段");
                return false;
            }
            if (next.find(sl.name) != next.end())
            {
                logger()->warn("[Assign] 技能链重名: " + sl.name);
                return false;
            }

            resolve(sl);

            if (!skillListModeValid(sl.mode))
            {
                logger()->warn("[Assign] 技能链 {} 的 mode 非法({})，按 once 处理", sl.name, sl.mode);
                sl.mode = "once";
            }

            // interval：0 = 不等待（全速循环），只有负数才回落默认值
            // （原来写成 <=0 会被悄悄改成 1000，界面上就表现为"轮间隔调不动"）
            if (sl.interval < 0)
            {
                logger()->warn("[Assign] 技能链 {} 的 interval 为负({})，按默认 1000 处理",
                               sl.name, sl.interval);
                sl.interval = 1000;
            }

            next[sl.name] = std::move(sl);
        }
    }
    catch (const std::exception& e)
    {
        logger()->warn(std::string("[Assign] 解析技能链失败: ") + e.what());
        return false;
    }

    // 先落盘再更新内存，保证文件和内存一致
    if (!save(next)) return false;

    return this->update();
}

bool Assign::hasList(const std::string& name) const
{
    return list_.find(name) != list_.end();
}

SkillList Assign::getList(const std::string& name) const
{
    auto it = list_.find(name);
    if (it == list_.end())
        throw std::runtime_error("[Assign] 技能链不存在: " + name);

    return it->second;
}

bool Assign::upsertList(const SkillList& sl)
{
    std::map<std::string, SkillList> next = list_;
    next[sl.name] = sl;
    return set(next);
}

bool Assign::removeList(const std::string& name)
{
    std::map<std::string, SkillList> next = list_;

    if (next.erase(name) == 0)
    {
        logger()->warn("[Assign] 要删除的技能链不存在: " + name);
        return false;
    }

    return set(next);
}

// ============================================================
// 公共内存的 JSON 赋值
// ============================================================

bool Assign::assignMemory(Memory& mem, const json& items, json& report)
{
    report = json{ {"written", 0}, {"failed", json::array()} };

    if (!items.is_object())
    {
        report["message"] = "items 必须是 JSON 对象，形如 {\"键\": 值}";
        return false;
    }

    std::size_t written = 0;

    for (auto it = items.begin(); it != items.end(); ++it)
    {
        std::string err;
        if (writeOne(mem, it.key(), it.value(), err))
        {
            ++written;
        }
        else
        {
            report["failed"].push_back({ {"name", it.key()}, {"message", err} });
            logger()->warn("[Assign] 写入公共内存失败: {} ({})", it.key(), err);
        }
    }

    report["written"] = written;

    return report["failed"].empty();
}

nlohmann::json Assign::readMemoryValues(Memory& mem, const std::vector<std::string>& names)
{
    nlohmann::json arr = nlohmann::json::array();

    const std::vector<std::string> keys = names.empty() ? mem.keys() : names;

    for (const auto& k : keys)
    {
        nlohmann::json item;
        item["name"]   = k;
        item["type"]   = mem.getTypeName(k);
        item["size"]   = mem.getSize(k);

        std::any a;
        if (mem.getAny(k, a))
        {
            item["exists"] = true;
            item["value"]  = anyToJson(a);
        }
        else
        {
            item["exists"] = false;
            item["value"]  = nullptr;
        }

        arr.push_back(std::move(item));
    }

    return arr;
}

std::size_t Assign::eraseMemory(Memory& mem, const std::vector<std::string>& names)
{
    std::size_t n = 0;
    for (const auto& k : names)
        if (mem.erase(k)) ++n;
    return n;
}

} // namespace sc::tools
