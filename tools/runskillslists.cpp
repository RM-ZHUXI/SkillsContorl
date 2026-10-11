/**
 * @file        runskillslists.cpp
 * @brief       技能链线程管理器实现
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-17
 * @version     1.0.0
 * @par         每条技能链一条工作线程，线程之间通过公共内存交换数据
 *
 * @version     2.0.0
 * @par         1. 生命周期以 start/stop 为边界：start() 里对链内每个技能 load + init()，
 *                 stop() 里对这些技能 end() + 卸载 DLL（所以每次 start 都会重新 init 一次）
 *              2. while 模式改成固定节拍：第 n 轮的开始时刻 = 首轮起点 + n × interval
 *              3. 同一个技能被多条链同时使用时按引用计数共享（见 setShareSkillAcrossChains）
 */

#include "tools/runskillslists.hpp"

#include <algorithm>
#include <set>
#include <string>

namespace sc::tools
{

namespace
{
    /// run() 单次执行超过这个毫秒数就告警（约定：run() 里不允许死循环 / 长阻塞）
    constexpr long long kRunWarnMs = 3000;

    /// 节拍偏移超过这个比例就告警（判断"这一轮是不是没按节拍跑"）
    constexpr long long kTickWarnRatioPercent = 25;

    /**
     * @brief 把一串链名拼成日志用的字符串
     */
    std::string joinNames(const std::set<std::string>& names)
    {
        std::string out;
        for (const auto& n : names)
        {
            if (!out.empty()) out += ", ";
            out += n;
        }
        return out;
    }

    /**
     * @brief 状态字符串（给日志用）
     */
    const char* stateText(bool stopFlag)
    {
        return stopFlag ? "被 stop 停止" : "自然结束";
    }
}

RunSkillsLists::RunSkillsLists(Memory* memory)
: memory_(memory)
{
    if (memory_ == nullptr)
        logger()->warn("[RunSkillsLists] 传入的 memory 为空，技能将无法读写公共内存");
}

RunSkillsLists::~RunSkillsLists()
{
    // 先停线程（每条链都会走一遍 end），再收掉所有还活着的技能，最后析构加载器
    stopAll();
    releaseAll();

    std::lock_guard<std::mutex> lk(mtx_);
    chains_.clear();
}

// 启动 / 停止

bool RunSkillsLists::start(const SkillList& list)
{
    // 技能链处理
    if (list.name.empty())
    {
        logger()->warn("[RunSkillsLists] 技能链名称为空，无法启动");
        return false;
    }
    if (list.skills.empty())
    {
        logger()->warn("[RunSkillsLists] 技能链 {} 里没有技能，无法启动", list.name);
        return false;
    }
    if (!sc::tools::STATUS)
    {
        logger()->warn("[RunSkillsLists] 全局 STATUS=false，技能链 {} 启动后会立刻退出", list.name);
    }

    // 处理运行模式
    SkillList chainList = list;
    if (!skillListModeValid(chainList.mode))
    {
        logger()->warn("[RunSkillsLists] 技能链 {} 的 mode 非法({})，按 once 处理", list.name, chainList.mode);
        chainList.mode = "once";
    }
     
    // 处理切片时间小于0的
    if (chainList.interval < 0)
    {
        chainList.interval = 1000;
    }

    std::unique_lock<std::mutex> sessionLock(sessionMtx_);

    // 加入一条链运行对象指针map
    std::shared_ptr<Chain> chain;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        auto it = chains_.find(list.name);
        if (it != chains_.end())
        {
            if (it->second->state.running || it->second->state.starting)
            {
                logger()->warn("[RunSkillsLists] 技能链已在运行: {}", list.name);
                return false;
            }
            chain = it->second;
        }
        else
        {
            chain = std::make_shared<Chain>();
            chains_[list.name] = chain;
        }

        chain->stopFlag = false;        // 新的一次会话：清掉上一次 stop 留下的标志
        chain->state.starting = true;   // 占位：防止并发重复启动
    }

    // 等待旧线程完毕
    if (chain->worker.joinable())
        chain->worker.join();

    std::vector<std::shared_ptr<LoadSkills>> session;
    std::size_t inited = 0;
    std::string why;

    if (!acquireSkillsLocked(chainList, session, inited, why))
    {
        releaseSkillsLocked(chainList.name, session);

        std::lock_guard<std::mutex> lk(mtx_);
        chain->state.starting = false;
        chain->state.running  = false;
        chain->state.inited   = 0;
        chain->state.error    = why;
        touch(chain->state);

        logger()->error("[RunSkillsLists] 技能链 {} 启动失败（已回滚已初始化的技能）: {}",
                        chainList.name, why);
        return false;
    }

    bool spawn = false;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        if (chain->stopFlag || !sc::tools::STATUS)
        {
            chain->state.starting = false;
            chain->state.running  = false;
            chain->state.inited   = 0;
            chain->state.error    = chain->stopFlag ? "初始化期间收到停止请求，已取消启动"
                                                    : "初始化期间程序要求退出，已取消启动";
            touch(chain->state);
        }
        else
        {
            chain->list    = chainList;
            chain->session = session;     // 工作线程起来之前写完，之后线程里只读

            chain->state            = RunState{};
            chain->state.name       = chainList.name;
            chain->state.mode       = chainList.mode;
            chain->state.interval   = chainList.interval;
            chain->state.inited     = inited;
            chain->state.skillCount = chainList.skills.size();
            chain->state.running    = true;
            chain->state.starting   = false;
            chain->state.startTime  = timer_.nowDate() + " " + timer_.nowTime();
            chain->state.updateTime = chain->state.startTime;

            chain->worker = std::thread([this, chain] { workerMain(chain); });
            spawn = true;
        }
    }

    if (!spawn)
    {
        releaseSkillsLocked(chainList.name, session);

        std::lock_guard<std::mutex> lk(mtx_);
        logger()->warn("[RunSkillsLists] 技能链 {} {}: {}", chainList.name,
                       chain->state.error, "已 end 并卸载技能");
        return false;
    }

    logger()->info("[RunSkillsLists] 启动技能链 {} (mode={}, 节拍={}ms, {} 个技能, 本次 init {} 个)",
                   chainList.name, chainList.mode, chainList.interval,
                   chainList.skills.size(), inited);
    return true;
}

bool RunSkillsLists::stop(const std::string& name)
{
    std::shared_ptr<Chain> chain;
    {
        std::lock_guard<std::mutex> lk(mtx_);

        auto it = chains_.find(name);
        if (it == chains_.end())
            return false;

        chain = it->second;
        chain->stopFlag = true;         // 只发停止请求，线程跑完当前技能自然退出
    }

    if (chain->worker.joinable())
        chain->worker.join();

    // ------------------------------------------------------------
    // ★ stop = 本次会话结束：对这次 init 过的技能调用 end()（最后一个使用者才真正 end + 卸载）
    //   1. join 之后才拿 sessionMtx_（工作线程自然结束时也要抢它，先 join 才不会有等待环）
    //   2. "把会话换出来 + 标记不在运行 + 归还技能"必须在同一把锁里做完，
    //      否则 start() 可能在换出与归还之间插进来复用同一个技能槽（见 start() 的注释）
    //   3. 返回时 end() 已经执行完
    // ------------------------------------------------------------
    {
        std::lock_guard<std::mutex> slk(sessionMtx_);

        std::vector<std::shared_ptr<LoadSkills>> session;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            session.swap(chain->session);

            chain->state.running  = false;
            chain->state.starting = false;
            chain->state.current.clear();
            chain->state.inited   = 0;
            chain->state.tickMs   = 0;
            touch(chain->state);
        }

        if (!session.empty())
            releaseSkillsLocked(name, session);
    }

    return true;
}

std::size_t RunSkillsLists::stopAll()
{
    std::vector<std::string> names;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        for (const auto& kv : chains_)
            names.push_back(kv.first);
    }

    std::size_t n = 0;
    for (const auto& name : names)
        if (stop(name)) ++n;

    return n;
}

bool RunSkillsLists::remove(const std::string& name)
{
    if (!has(name)) return false;

    stop(name);     // 先停（含 end + 卸载），再删记录

    std::lock_guard<std::mutex> lk(mtx_);
    chains_.erase(name);
    return true;
}

// ============================================================
// 查询
// ============================================================

bool RunSkillsLists::running(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);

    auto it = chains_.find(name);
    return it != chains_.end() && it->second->state.running;
}

bool RunSkillsLists::has(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(mtx_);
    return chains_.find(name) != chains_.end();
}

std::size_t RunSkillsLists::runningCount() const
{
    std::lock_guard<std::mutex> lk(mtx_);

    std::size_t n = 0;
    for (const auto& kv : chains_)
        if (kv.second->state.running) ++n;

    return n;
}

std::vector<RunState> RunSkillsLists::states() const
{
    std::lock_guard<std::mutex> lk(mtx_);

    std::vector<RunState> out;
    out.reserve(chains_.size());

    for (const auto& kv : chains_)
        out.push_back(kv.second->state);

    return out;
}

std::vector<std::string> RunSkillsLists::names() const
{
    std::lock_guard<std::mutex> lk(mtx_);

    std::vector<std::string> out;
    out.reserve(chains_.size());

    for (const auto& kv : chains_)
        out.push_back(kv.first);

    return out;
}

nlohmann::json RunSkillsLists::statusJson() const
{
    using json = nlohmann::json;

    json arr = json::array();
    std::size_t runningCount = 0;

    for (const auto& s : states())
    {
        if (s.running) ++runningCount;

        arr.push_back({
            {"name",        s.name},
            {"mode",        s.mode},
            {"running",     s.running},
            {"starting",    s.starting},
            {"loop",        s.loop},
            {"current",     s.current},
            {"skill_count", s.skillCount},
            {"error",       s.error},
            {"start_time",  s.startTime},
            {"update_time", s.updateTime},
            // 节拍观测：interval=目标节拍，tick_ms=上一轮 start->start 实际间隔，
            // round_ms=上一轮总耗时，inited=本次会话 init 成功的技能数，skipped=丢弃的节拍数
            {"interval",    s.interval},
            {"inited",      s.inited},
            {"round_ms",    s.roundMs},
            {"tick_ms",     s.tickMs},
            {"skipped",     s.skipped}
        });
    }

    json j;
    j["running"] = runningCount;
    j["lists"]   = std::move(arr);
    return j;
}

// ============================================================
// 内部：线程主体（固定节拍调度）
// ============================================================

void RunSkillsLists::workerMain(const std::shared_ptr<Chain>& chain)
{
    const std::string name     = chain->list.name;
    const bool        once     = (chain->list.mode == "once");
    const int         interval = chain->list.interval > 0 ? chain->list.interval : 0;
    const auto        period   = std::chrono::milliseconds(interval);

    using clock = std::chrono::steady_clock;
    auto lastTickWarn = clock::time_point{};      // 节拍告警限流（最多每秒一条）

    try
    {
        auto tick = clock::now();    
        auto prev = tick;

        for (;;)
        {
            if (!sleepUntil(chain->stopFlag, tick)) break;
            if (chain->stopFlag || !sc::tools::STATUS) break;

            const auto roundStart = clock::now();
            const long long tickMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                         roundStart - prev).count();
            prev = roundStart;

            const bool ok = runOneLoop(chain);

            const long long roundMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                                          clock::now() - roundStart).count();

            std::size_t loopNo = 0;
            {
                std::lock_guard<std::mutex> lk(mtx_);
                chain->state.tickMs  = tickMs;
                chain->state.roundMs = roundMs;
                touch(chain->state);
                loopNo = chain->state.loop;
            }

            if (!ok) break;                       // 技能抛异常：停（error 已写进状态）
            if (once) break;                      // once：只跑一遍
            if (!sc::tools::STATUS) break;        // 程序要求退出

            // ---- 全速循环（节拍 0）：不等待，只让出时间片 ----
            if (interval <= 0)
            {
                std::this_thread::yield();
                tick = clock::now();              // 锚点跟着"现在"走，下一轮立刻开始
                continue;
            }

            // ---- 固定节拍：下一个节拍点 = 本轮节拍点 + interval ----
            tick += period;

            const auto now = clock::now();

            if (now < tick)
            {
                // 正常情况：本轮跑得比节拍快，剩下的时间在下一轮开头 sleepUntil 里睡掉。
                // 这里只做"节拍有没有被拖慢"的自检（启动后前 3 轮 + 偏差过大时限流告警）。
                if (loopNo <= 3)
                {
                    logger()->info("[RunSkillsLists] 技能链 {} 节拍自检 第 {} 轮: "
                                   "上一轮耗时 {} ms, start->start 间隔 {} ms (目标 {} ms)",
                                   name, loopNo, roundMs, tickMs, interval);
                }
                else if (tickMs > interval + std::max<long long>(interval * kTickWarnRatioPercent / 100, 20) &&
                         (lastTickWarn.time_since_epoch().count() == 0 ||
                          now - lastTickWarn >= std::chrono::seconds(1)))
                {
                    lastTickWarn = now;
                    logger()->warn("[RunSkillsLists] 技能链 {} 节拍被拖慢: 第 {} 轮 start->start "
                                   "{} ms (目标 {} ms, 上一轮耗时 {} ms)",
                                   name, loopNo, tickMs, interval, roundMs);
                }
                continue;
            }

            const long long late = std::chrono::duration_cast<std::chrono::milliseconds>(now - tick).count();

            if (late >= interval)
            {
                const std::size_t dropped = static_cast<std::size_t>(late / interval);
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    chain->state.skipped += dropped;
                }

                if (lastTickWarn.time_since_epoch().count() == 0 ||
                    now - lastTickWarn >= std::chrono::seconds(1))
                {
                    lastTickWarn = now;
                    logger()->warn("[RunSkillsLists] 技能链 {} 单轮耗时 {} ms 超过节拍 {} ms，"
                                   "丢弃 {} 个节拍（不补跑，下一轮立即开始）",
                                   name, roundMs, interval, dropped);
                }

                tick = now;     // 锚点拉回"现在"，下一轮立刻开始
            }
            // 迟到不足一拍：不睡觉，下一轮立刻开始，把欠的那点时间用掉
        }
    }
    catch (const std::exception& e)
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            chain->state.error = e.what();
        }
        logger()->error("[RunSkillsLists] 技能链 {} 异常退出: {}", name, e.what());
    }
    catch (...)
    {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            chain->state.error = "未知异常";
        }
        logger()->error("[RunSkillsLists] 技能链 {} 未知异常退出", name);
    }

    // ------------------------------------------------------------
    // 线程自然结束（once 跑完 / 出错 / STATUS=false 而没人调 stop）时，
    // 自己把技能会话还回去；被 stop 停掉的由 stop() 归还。
    // 两边都用 swap，谁先拿到 session 谁负责 end + 卸载，绝不会重复 end。
    // ------------------------------------------------------------
    if (!chain->stopFlag)
    {
        std::vector<std::shared_ptr<LoadSkills>> session;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            session.swap(chain->session);
        }

        if (!session.empty())
        {
            std::lock_guard<std::mutex> lk(sessionMtx_);
            releaseSkillsLocked(name, session);
        }
    }

    std::size_t loopCount = 0;
    {
        std::lock_guard<std::mutex> lk(mtx_);
        chain->state.running = false;
        chain->state.current.clear();
        chain->state.inited  = 0;
        touch(chain->state);
        loopCount = chain->state.loop;
    }

    logger()->info("[RunSkillsLists] 技能链 {} 线程结束（{}），共完成 {} 轮",
                   name, stateText(chain->stopFlag.load()), loopCount);
}

bool RunSkillsLists::runOneLoop(const std::shared_ptr<Chain>& chain)
{
    const std::size_t count = chain->list.skills.size();

    for (std::size_t i = 0; i < count; ++i)
    {
        const Skill& skill = chain->list.skills[i];

        // 每跑一个技能前检查一次停止标志：技能内部是长耗时函数时，
        // 也只能等它自己返回（不做强行杀线程）
        if (chain->stopFlag || !sc::tools::STATUS)
            return true;

        {
            std::lock_guard<std::mutex> lk(mtx_);
            chain->state.current = skill.name;
            touch(chain->state);
        }

        // 会话里的加载器与 list.skills 一一对应（start() 里装配，线程里只读）
        std::shared_ptr<LoadSkills> loader =
            (i < chain->session.size()) ? chain->session[i] : nullptr;

        if (!loader)
        {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                chain->state.error = "技能 " + skill.name + " 本次启动没有 init 成功，无法运行";
                touch(chain->state);
            }
            logger()->error("[RunSkillsLists] 技能 {} 没有可用的加载器，技能链 {} 停止",
                            skill.name, chain->list.name);
            return false;
        }

        try
        {
            // 计时只为在技能违背约定时给出提示，不改变任何行为：
            // std::thread 没有"超时打断"，run() 里的死循环只能靠约定避免，
            // 这里至少让它在日志里留下痕迹。
            const auto t0 = std::chrono::steady_clock::now();
            loader->runOnce();               
            const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                                std::chrono::steady_clock::now() - t0).count();

            if (ms >= kRunWarnMs)
            {
                logger()->warn("[RunSkillsLists] 技能 {} 单次 run() 耗时 {} ms，"
                               "疑似在 run() 里写了死循环或长阻塞"
                               "（约定：run() 必须干一次活就返回，循环交给技能链的 mode=while）",
                               skill.name, ms);
            }
        }
        catch (const std::exception& e)
        {
            {
                std::lock_guard<std::mutex> lk(mtx_);
                chain->state.error = skill.name + ": " + e.what();
                touch(chain->state);
            }
            logger()->error("[RunSkillsLists] 技能 {} 执行失败: {}", skill.name, e.what());
            return false;
        }
    }

    {
        std::lock_guard<std::mutex> lk(mtx_);
        ++chain->state.loop;
        chain->state.current.clear();
        touch(chain->state);
    }

    return true;
}

// ============================================================
// 内部：技能会话（load + init / end + 卸载）
// ============================================================

std::string RunSkillsLists::skillKey(const Skill& skill)
{
    // 归一化规则必须和 LoadSkills 构造函数里的那套保持一致：
    // Windows 把 / 换成 \，非 Windows 把 \ 换成 /，最后补上动态库扩展名
#ifdef _WIN32
    const char        from = '/';
    const char        to   = '\\';
    const std::string ext  = ".dll";
#else
    const char        from = '\\';
    const char        to   = '/';
    const std::string ext  = ".so";
#endif

    std::string key = skill.url;
    for (char& c : key)
        if (c == from) c = to;

    if (key.size() < ext.size() ||
        key.compare(key.size() - ext.size(), ext.size(), ext) != 0)
    {
        key += ext;
    }

    return key;
}

bool RunSkillsLists::acquireSkillsLocked(const SkillList& list,
                                        std::vector<std::shared_ptr<LoadSkills>>& session,
                                        std::size_t& inited,
                                        std::string& why)
{
    // 初始化
    session.clear();
    inited = 0;
    session.reserve(list.skills.size());

    for (const Skill& skill : list.skills)
    {
        const std::string key = skillKey(skill);

        // DLL已经被其他链init
        auto it = slots_.find(key);
        if (it != slots_.end() && it->second.loader)
        {
            const bool mine = (it->second.chains.count(list.name) != 0);

            if (!mine && !it->second.chains.empty() && !shareSkill_)
            {
                why = "技能 " + skill.name + " 已被技能链 " + joinNames(it->second.chains) +
                      " 初始化（独占模式：同一个技能不能被两条链同时使用，请先停掉那条链）";
                return false;
            }

            if (!mine)
            {
                // 同一个 DLL 只有一份全局状态，重复 init 会把别人正在用的资源重建/覆盖掉，
                // 所以这里直接复用：只有第一个使用者 init，最后一个使用者 stop 时才 end
                const std::string owners = joinNames(it->second.chains);
                it->second.chains.insert(list.name);

                logger()->info("[RunSkillsLists] 技能 {} 已由技能链 {} init 过，技能链 {} 直接复用"
                               "（同一个 DLL 只有一份全局状态，两条链共用同一份实例；"
                               "如需独占请调用 setShareSkillAcrossChains(false)）",
                               skill.name, owners, list.name);
            }

            session.push_back(it->second.loader);
            continue;
        }

        // ---- 情况二：第一次使用 / 上一次已经 end 并卸载过 -> 加载 DLL + init() ----
        std::shared_ptr<LoadSkills> loader;
        try
        {
            loader = std::make_shared<LoadSkills>(skill);   // 加载失败会抛
        }
        catch (const std::exception& e)
        {
            why = "技能 " + skill.name + " 动态库加载失败: " + e.what();
            return false;
        }

        if (loader->path() != key)
        {
            logger()->warn("[RunSkillsLists] 技能路径归一化结果与 LoadSkills 不一致: "
                           "key={}, path={}（skillKey() 的规则需要和 LoadSkills 保持同步）",
                           key, loader->path());
        }


        // 真正init()
        try
        {
            loader->load(memory_);
        }
        catch (const std::exception& e)
        {
            why = "技能 " + skill.name + " 初始化(init)失败: " + e.what();
            return false;
        }

        SkillSlot slot;
        slot.loader = loader;
        slot.chains.insert(list.name);
        slots_[key] = std::move(slot);

        session.push_back(loader);
        ++inited;

        logger()->info("[RunSkillsLists] 技能 {} init 完成（本次新建会话, key={}）", skill.name, key);
    }

    return true;
}

void RunSkillsLists::releaseSkillsLocked(const std::string& chainName,
                                        std::vector<std::shared_ptr<LoadSkills>>& session)
{
    std::set<std::string> done;     // 同一条链里重复引用的技能只归还一次

    for (auto& loader : session)
    {
        if (!loader) continue;

        const std::string key = skillKey(loader->info());
        if (!done.insert(key).second) continue;

        auto it = slots_.find(key);
        if (it == slots_.end())
        {
            logger()->warn("[RunSkillsLists] 技能链 {} 归还技能 {} 时找不到技能槽（可能已被卸载）",
                           chainName, loader->info().name);
            continue;
        }

        it->second.chains.erase(chainName);

        if (!it->second.chains.empty())
        {
            logger()->info("[RunSkillsLists] 技能链 {} 停止：技能 {} 还被技能链 {} 使用，"
                           "本次不调用 end()（最后一个使用者停止时才 end + 卸载）",
                           chainName, loader->info().name, joinNames(it->second.chains));
            continue;
        }

        // ---- 最后一个使用者：end() 只在这里被调用（也就是 stop 的时候）----
        const std::string skillName = it->second.loader ? it->second.loader->info().name
                                                        : loader->info().name;

        try
        {
            it->second.loader->end();       // end 是可选入口，异常在 loadskills 里已被吞掉
        }
        catch (const std::exception& e)
        {
            logger()->warn("[RunSkillsLists] 技能 {} 的 end() 出错（已忽略）: {}", skillName, e.what());
        }

        logger()->info("[RunSkillsLists] 技能 {} 已收尾：end() 执行完成并卸载动态库 ({})",
                       skillName, key);

        slots_.erase(it);   // 引用归零 -> ~LoadSkills -> FreeLibrary
    }

    session.clear();        // 引用在锁内归还，保证卸载也发生在 sessionMtx_ 保护下
}

void RunSkillsLists::releaseAll()
{
    std::lock_guard<std::mutex> lk(sessionMtx_);

    for (auto& kv : slots_)
    {
        SkillSlot& slot = kv.second;
        if (!slot.loader) continue;

        if (!slot.chains.empty())
            logger()->warn("[RunSkillsLists] 析构时技能 {} 仍被技能链 {} 使用（正常流程不该出现）",
                           slot.loader->info().name, joinNames(slot.chains));

        try
        {
            slot.loader->end();
        }
        catch (const std::exception& e)
        {
            logger()->warn("[RunSkillsLists] 析构收尾时技能 {} 的 end() 出错（已忽略）: {}",
                           slot.loader->info().name, e.what());
        }

        logger()->info("[RunSkillsLists] 析构收尾：技能 {} end() 完成并卸载动态库", 
                       slot.loader->info().name);

        slot.loader.reset();    // -> ~LoadSkills -> FreeLibrary
    }

    slots_.clear();
}

// ============================================================
// 内部：小工具
// ============================================================

void RunSkillsLists::touch(RunState& st)
{
    st.updateTime = timer_.nowDate() + " " + timer_.nowTime();
}

bool RunSkillsLists::sleepUntil(const std::atomic<bool>& stopFlag,
                                std::chrono::steady_clock::time_point until)
{
    constexpr auto step = std::chrono::milliseconds(50);   // 每 50ms 醒一次，保证 stop 能及时生效

    while (!stopFlag)
    {
        const auto now = std::chrono::steady_clock::now();
        if (now >= until) return true;

        const auto left = until - now;
        // 最后一段按"剩余时间"精确睡（而不是整 50ms），所以节拍不会被 50ms 的粒度拖长
        std::this_thread::sleep_for(left < step ? left : step);
    }

    return false;   // 被 stop 打断
}

} // namespace sc::tools
