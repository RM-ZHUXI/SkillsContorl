#include "tools/runskills.hpp"

using SteadyClock = std::chrono::steady_clock;

namespace sc::tools
{



Runskills::Runskills(Time &time,Memory *memory,Assign &assign) : mem_(memory) , assign_(assign) , time_(time){} 

TOOLS_API bool loadAll(std::vector<Skill> skills)
{
    std::string now;
    try
    {
        for(auto &e:skills)
        {
            now = e.name;

            // ★ 必须"就地构造"，绝对不要写成 loadskills[e.name] = l; 这种先建临时再赋值的形式：
            //   LoadSkills 持有 LoadLibrary 拿到的句柄（拥有型资源，引用计数一开始只有 1），
            //   临时对象一析构就会 FreeLibrary，引用计数直接归零 => **DLL 当场被卸载**，
            //   而 map 里留下一个已经失效的 handle。
            //   症状就是：启动阶段一路打印「动态库加载成功」，等到真正 start 技能链时
            //   resolveSymbols() 才在失效句柄上 GetProcAddress，报「缺少 init 导出符号」。
            //   try_emplace 直接在 map 节点里构造，不产生任何临时/拷贝/移动。
            if(!loadskills.try_emplace(e.name, e).second)
            {
                logger()->warn("[loadAll] 技能 {} 重复注册，忽略后一个: {}", e.name, e.url);
            }
        }
    }
    catch(const std::exception& e)
    {
        logger()->error("[loadAll] error : {}", e.what()); 
        return false;
    }
    return true;
}

void Runskills::runthread(SkillsChain &chain)
{
    const std::string name     = chain.list.name;                                    ///< 名称
    const bool        once     = (chain.list.mode == "once");                        ///< 模式
    const int         interval = chain.list.interval > 0 ? chain.list.interval : 0;  ///< 预设时间
    const auto        period   = std::chrono::milliseconds(interval);                ///< 预设时间(std::chrono::milliseconds)
    // 时间戳
    auto tick = SteadyClock::now();
    auto prev = tick;
    
    // 修改state状态-true
    {
        std::lock_guard<std::mutex> lock(chain.mtx_state);
        chain.state = true;
    }   

    if(once)
    {
        runOnce(chain);
        // 修改run状态-flase
        {
            std::lock_guard<std::mutex> lock(chain.mtx_run);
            chain.run = false;
        }   
    }
    else
    {
        while(1)
        {
            // 运行等待
            if (!sleepUntil(chain, tick) || !sc::tools::STATUS) break;

            // 读取run
            {
                std::lock_guard<std::mutex> lock(chain.mtx_run);
                if(!chain.run)break;
            }

            // 记录开始时间
            const auto roundStart = SteadyClock::now();

            // 运行一次链结构
            runOnce(chain);

            // 计算时间差
            const int runOnceTime = std::chrono::duration_cast<std::chrono::milliseconds>(roundStart - prev).count();
            prev = roundStart;

            // 记录时间差
            {
                std::lock_guard<std::mutex> lock(chain.mtx_time);
                chain.time = runOnceTime;
            }
            
            //logger()->info("[runthread] {} runOnce 耗时 {} ms", name, runOnceTime);
            // 判断时间
            if(runOnceTime < interval)
            {
                tick = roundStart + period;
            }
            else
            {
                tick = SteadyClock::now();
            }
        }
    }

    // 跑end()
    runEnd(chain);

    // 修改state状态-flase
    {
        std::lock_guard<std::mutex> lock(chain.mtx_state);
        chain.state = false;
    }   
}

bool Runskills::sleepUntil(SkillsChain &chain,std::chrono::steady_clock::time_point until)
{
    constexpr auto step = std::chrono::milliseconds(50);

    while (true)
    {
        // 读一止标志
        {
            std::lock_guard<std::mutex> lock(chain.mtx_run);
            if (!chain.run) return false;   
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= until) return true;   

        const auto left = until - now;
        std::this_thread::sleep_for(left < step ? left : step);
    }
}

bool Runskills::runOnce(SkillsChain &chain)
{
    try
    {
        for(auto e:chain.list.skills)
        {
            loadskills[e.name].runOnce();
        }
    }
    catch(const std::exception& e)
    {
        logger()->error("[runskills] {} 链运行错误 : {}",chain.list.name,e.what());
        {
            std::lock_guard<std::mutex> lock(chain.mtx_error);
            chain.error = e.what();
        }
        return false;
    }   
    return true;
}

bool Runskills::runInit(SkillsChain &chain)
{
    try
    {
        for(auto e:chain.list.skills)
        {
            loadskills[e.name].load(mem_);
        }
    }
    catch(const std::exception& e)
    {
        logger()->error("[runskills] {} 链init运行错误 : {}",chain.list.name,e.what());
        {
            std::lock_guard<std::mutex> lock(chain.mtx_error);
            chain.error = e.what();
        }
        return false;
    }      
    return true;
}

bool Runskills::runEnd(SkillsChain &chain)
{
    bool res = true;
    for (auto& e : chain.list.skills)
    {
        try
        {
            loadskills[e.name].end();
        }
        catch (const std::exception& ed)
        {
            logger()->error("[runskills] {} 技能 {} end 错误 : {}", chain.list.name,e.name, ed.what());
            {
                std::lock_guard<std::mutex> lock(chain.mtx_error);
                chain.error = ed.what();
            }
            res = false;
        }
    }
    return res;
}

bool Runskills::start(const SkillList& list)
{
    SkillsChain *chainptr = nullptr;
    
    // 查询链是否存在
    {
        std::lock_guard<std::mutex> lock(mtx_chain);

        // 如果当前链存在
        if(chains_.find(list.name) != chains_.end())
        {
            chainptr = &chains_[list.name];
        }    
    }

    // 不存在链
    if(!chainptr)
    {
        std::lock_guard<std::mutex> lock(mtx_chain);
        auto [it, inserted] = chains_.try_emplace(list.name, list);
        chainptr = &(it->second);
    }

    // 判断空指针
    if(chainptr == nullptr)
    {
        logger()->error("[runskills] 插入失败");
        return false;
    }

    // 查询链状态
    {
        std::lock_guard<std::mutex> lock(chainptr->mtx_state);
        if(chainptr->state)
        {
            logger()->error("[runskills] 链 {} 正在运行,启动失败",list.name);
            return false;
        }
    }

    // ★ 回收上一条已经跑完但还没 join 的线程。
    //   给一个仍然 joinable 的 std::thread 赋值会直接 std::terminate()：
    //   "启动 → 停止 → 再启动"就会踩到（stop() 正常路径现在会 join，这里再兜一层，
    //   防止线程恰好在线程对象被复用之前才结束这种时序）。
    if (chainptr->thread.joinable())
    {
        chainptr->thread.join();
    }

    // 清掉上一轮的遗留错误，免得网页上一直挂着上次的失败原因
    {
        std::lock_guard<std::mutex> lock(chainptr->mtx_error);
        chainptr->error.clear();
    }

    // 运行init()
    if(!runInit(*chainptr))
    {
        logger()->error("[runskills] {}链init()失败",list.name);
        return false;
    }

    // 修改run状态
    {
        std::lock_guard<std::mutex> lock(chainptr->mtx_run);
        chainptr->run = true;
    }

    // ★ 立刻把 state 置成"运行中"（runthread 进去后还会再置一次，幂等）。
    //   不能只等 worker 线程去置：从 start() 返回到 worker 真正跑起来之间有一个窗口，
    //   这期间 runningCount() 会少算一条（/api/StartSkillChain 响应里的 running 就是 0），
    //   更要命的是紧接着调 stop() 会被判成"链已经停止"直接返回失败 ——
    //   网页上表现就是"刚点了启动又点停止 → 提示该技能链没有运行记录"。
    {
        std::lock_guard<std::mutex> lock(chainptr->mtx_state);
        chainptr->state = true;
    }

    // 添加线程
    chainptr->thread = std::thread(&Runskills::runthread, this, std::ref(*chainptr));

    // 返回
    return true;
}

bool Runskills::stop(const std::string& name)
{
    std::thread *worker = nullptr;

    // 查链 + 发停止信号，全部收在这个作用域里（只在这里持 mtx_chain）
    {
        std::lock_guard<std::mutex> lock(mtx_chain);

        auto it = chains_.find(name);
        if (it == chains_.end())
        {
            return false;                                   // 链不存在
        }

        SkillsChain &chain = it->second;

        {
            std::lock_guard<std::mutex> lockState(chain.mtx_state);
            if (!chain.state)
            {
                logger()->error("[runskills] {} 链已经停止,停止失败", name);
                return false;
            }
        }

        {
            std::lock_guard<std::mutex> lockRun(chain.mtx_run);
            // 软停止信号：runthread 在 sleepUntil 里每 50ms 看一次，或在两轮之间看到
            chain.run = false;
        }

        worker = &chain.thread;
    }

    // ★ 放开 mtx_chain 之后再 join，原因有两个：
    //   1) join 要等 worker 跑完当前这一轮、再跑完 runEnd()（里面会调技能的 end()，
    //      比如关相机、关窗口）。持着 mtx_chain 做这件事，会把所有链相关接口一起堵住；
    //   2) 更关键：线程虽然跑完了，但没 join 的 std::thread 仍然是 joinable 的，
    //      下次 start() 给它赋新线程会直接 std::terminate()。
    //      也就是"启动 → 停止 → 再启动"必崩。所以这里必须真正回收线程。
    //   注：worker 指向 map 节点内部，本类没有任何 erase(chains_) 的地方，
    //       所以放开锁之后这个指针依然有效。
    if (worker != nullptr && worker->joinable())
    {
        worker->join();                                     // 返回时 end() 一定已经执行完
    }

    return true;
}

std::size_t Runskills::stopAll()
{
    // 先只收集"正在运行"的链名，放开 mtx_chain 之后再逐条 stop()。
    // ★ 不能在持有 mtx_chain 的循环里直接调 stop()：stop() 内部还要 lock(mtx_chain)，
    //   非递归 mutex 二次加锁 = 死锁。原实现就是这样，所以 {"all":true} 的停止请求
    //   一次都没成功过 —— 点"全部停止"没任何反应，链会一直跑到进程被杀。
    std::vector<std::string> running;
    {
        std::lock_guard<std::mutex> lock(mtx_chain);
        for (auto& [name, chain] : chains_)
        {
            std::lock_guard<std::mutex> lockState(chain.mtx_state);
            if (chain.state) running.push_back(name);
        }
    }

    size_t total = 0;
    for (const auto& name : running)
    {
        if (stop(name)) ++total;
    }

    return total;
}

std::size_t Runskills::runningCount()
{
    size_t total = 0;

    // ★ 这里必须"只数不改"。
    //   原实现是从 stopAll() 整段复制过来的，循环体里调的是 stop(name)：
    //     1) stop() 内部还要 lock(mtx_chain)，而这里已经持有 mtx_chain ——
    //        std::mutex 不可重入，二次加锁 = **立即死锁**；
    //     2) 语义也错：一个"查运行条数"的函数把链全停了。
    //   而 /api/StartSkillChain 在回响应之前正好要调它，于是 HTTP 处理线程永久卡死
    //   并一直占着 mtx_chain → /api/GetRunStatus、/api/StopSkillChain 全都跟着卡死
    //   → 网页上就是"点了停止没反应、整个页面像死了"。这是"无法关闭技能"的根因。
    std::lock_guard<std::mutex> lock(mtx_chain);
    for (auto& [name, chain] : chains_)
    {
        std::lock_guard<std::mutex> lockState(chain.mtx_state);
        if (chain.state) ++total;
    }

    return total;
}

std::vector<std::string> Runskills::names()
{
    std::vector<std::string> res;
    
    {
        std::lock_guard<std::mutex> lock(this->mtx_chain);
        for(auto &e:this->chains_)
        {
            std::lock_guard<std::mutex> lock(e.second.mtx_state);
            if(e.second.state)res.push_back(e.second.list.name);
        }
    }

    return res;
}

nlohmann::json Runskills::statusJson()
{
    using json = nlohmann::json;
    json arr = json::array();
    std::size_t runningCount = 0;

    {
        std::lock_guard<std::mutex> lock(this->mtx_chain);
        for(auto &e:this->chains_)
        {
            bool run = false;
            bool state = false;
            int time = 0;
            std::string error;
            {
                std::lock_guard<std::mutex> lock(e.second.mtx_state);
                state = e.second.state;
            }
            {
                std::lock_guard<std::mutex> lock(e.second.mtx_run);
                run = e.second.run;
            }
            {
                std::lock_guard<std::mutex> lock(e.second.mtx_time);
                time = e.second.time;
            }
            {
                std::lock_guard<std::mutex> lock(e.second.mtx_error);
                error = e.second.error;
            }

            if (state) ++runningCount;

            arr.push_back({
                {"name",        e.second.list.name},
                {"mode",        e.second.list.mode},
                {"running",     state},
                {"starting",    run},
                {"loop",        0},
                {"current",     "null"},
                {"skill_count", e.second.list.skills.size()},
                {"error",       error},
                {"start_time",  time},
                {"update_time", time},
                {"interval",    0},
                {"inited",      0},
                {"round_ms",    time},
                {"tick_ms",     time},
                {"skipped",     0}
            });
        }
    }

    json j;
    j["running"] = runningCount;
    j["lists"]   = std::move(arr);
    return j;
}

} // namespace sc::tools