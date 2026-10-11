/**
 * @file        runskillslists.hpp
 * @brief       技能链线程管理器（多线程调度 skill 链）
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-17
 * @version     1.0.0
 * @par         每条 skill 链一条工作线程，链的 mode 只有两个值：
 *              once  只跑一次（按 list 顺序跑完一遍线程结束）
 *              while 一直循环（每轮之间 sleep interval 毫秒，直到 stop）
 *              公共内存的读写全部通过 sc::tools::Memory，技能之间靠它传数据。
 *
 * @version     2.0.0
 * @par         1. 生命周期改成"以 start/stop 为边界"，不再是"进程级只 init 一次"：
 *                 - start()：对链内每个技能**加载 DLL + 调用 init()**（每一次 start 都会调）
 *                 - stop() ：对这次 start 拿到的技能**调用 end()** 并卸载 DLL
 *                 所以 "start → 干活 → stop → 再 start" 是两次完整的 init/end，
 *                 技能可以在 init 里重新打开摄像头/设备，在 end 里释放掉。
 *              2. while 模式的 interval 从"跑完再等 x 毫秒"改成**固定节拍**：
 *                 第 n 轮的开始时刻 = 首轮起点 + n × interval，
 *                 也就是"每 x 毫秒开始跑一轮"；单轮超时会丢弃积压的节拍（不补跑），
 *                 绝不会出现"越跑越慢"或"醒来后连补好几轮"的情况。
 *
 * @note 生命周期（很重要，别踩）：
 *       1. init：**每次 start 调一次**。重复 init 的风险由本管理器兜住：
 *          同一个技能被两条链同时使用时（同一个 DLL 只有一份全局状态），
 *          默认只 init 一次、两条链共用（见 setShareSkillAcrossChains）。
 *       2. run：每轮对链里的每个技能调用一次 run()。
 *       3. end：**每次 stop 调一次**（且只有最后一个使用者真正 end + 卸载 DLL）。
 *       4. 停止链 = 让线程跑完当前这一轮后退出，不会强行杀线程；
 *          stop() 返回时，end() 一定已经执行完。
 *       5. 技能的 run() 必须"干一次活就返回"，循环交给链的 mode=while + interval。
 */

#ifndef RUNSKILLSLISTS_HPP
#define RUNSKILLSLISTS_HPP

#include <atomic>
#include <chrono>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include <nlohmann/json.hpp>

#include "tools/assign.hpp"
#include "tools/loadskills.hpp"
#include "tools/logger.hpp"
#include "tools/memory.hpp"
#include "tools/type.hpp"
#include "tools/tools_api.hpp"

namespace sc::tools
{

/**
 * @brief 一条技能链的运行状态快照（给网页显示用）
 */
struct RunState
{
    std::string name;                        ///< 技能链名称
    std::string mode;                        ///< 运行模式(once / while)
    bool        running = false;             ///< 是否正在运行
    bool        starting = false;            ///< 正在启动/重启的中间态(含 init 阶段)
    std::size_t loop = 0;                    ///< 已完成的轮次(once 模式最多 1)
    std::string current;                     ///< 当前正在跑的技能名
    std::size_t skillCount = 0;              ///< 链内技能数量
    std::string error;                       ///< 最近一次错误(空表示没有)
    std::string startTime;                   ///< 启动时间
    std::string updateTime;                  ///< 最近一次状态更新时间
    int         interval = 0;                ///< 目标节拍(ms)，0 = 全速循环
    std::size_t inited = 0;                  ///< 本次 start 真正 init 的技能数
    long long   roundMs = 0;                 ///< 上一轮所有 run() 的总耗时(ms)
    long long   tickMs = 0;                  ///< 上一轮起点 -> 本轮起点的实际间隔(ms)
    std::size_t skipped = 0;                 ///< 因单轮超时被丢弃的节拍数(累计)
};

/**
 * @brief 技能链线程管理器
 *
 * @code
 *   sc::tools::RunSkillsLists runner(&memory);
 *   runner.start(assign.getList("test"));   // 每次 start：load + init()
 *   runner.stop("test");                    // 每次 stop ：end() + 卸载
 *   runner.statusJson();                    // 给网页的状态
 * @endcode
 */
class TOOLS_API RunSkillsLists
{
public:
    RunSkillsLists() = default;

    /**
     * @brief 构造函数
     * @param memory 公共内存（技能通过它交换数据）
     */
    explicit RunSkillsLists(Memory* memory);

    /**
     * @brief 析构函数
     * @note  会先停掉所有链并 join 线程，再对还活着的技能调用 end() 并卸载 DLL
     */
    ~RunSkillsLists();

    RunSkillsLists(const RunSkillsLists&)            = delete;
    RunSkillsLists& operator=(const RunSkillsLists&) = delete;

    /**
     * @brief  启动一条技能链（每次调用都会重新走一遍 init）
     * @param  list 技能链(可来自 Assign::getList)
     * @return true 已启动；false 名称为空、该链已在运行、或某个技能的 init 失败
     * @note   启动过程 = 按 list 顺序 load DLL + init()，全部成功才起线程；
     *         中途失败会回滚（已经 init 成功的技能立刻 end + 卸载），
     *         并把原因写进 state.error（网页 /api/GetRunStatus 能看到）。
     *         同一条链重复 start 会被拒绝，必须先 stop()。
     */
    bool start(const SkillList& list);

    /**
     * @brief 停止一条技能链、回收线程，并对本次 init 过的技能调用 end()
     * @param name 技能链名称
     * @return true 停掉了(或本来就已停止)；false 该链不存在
     * @note  返回时 end() 已经执行完（最后一个使用者才会真正 end + 卸载 DLL）
     */
    bool stop(const std::string& name);

    /**
     * @brief 停止所有技能链（每条都会走 end()）
     * @return 被停止的链数量
     */
    std::size_t stopAll();

    /**
     * @brief 移除技能链的运行记录（会先停止它，因此也会走 end()）
     */
    bool remove(const std::string& name);

    /**
     * @brief 指定链是否在运行
     */
    bool running(const std::string& name) const;

    /**
     * @brief 是否存在该链的运行记录
     */
    bool has(const std::string& name) const;

    /**
     * @brief 正在运行的链数量
     */
    std::size_t runningCount() const;

    /**
     * @brief 所有链的状态快照
     */
    std::vector<RunState> states() const;

    /**
     * @brief 状态 -> JSON（给 /api/GetRunStatus 用）
     * @return {"running":n,"lists":[{...RunState...}, ...]}
     */
    nlohmann::json statusJson() const;

    /**
     * @brief 所有链名
     */
    std::vector<std::string> names() const;

    /**
     * @brief 设置"同一个技能被多条链同时使用"时的策略
     * @param on true(默认) 共用一份：只有第一个使用者 init，最后一个使用者 stop 时才 end；
     *           false 独占    ：后启动的那条链直接启动失败（错误写进 state.error）
     * @note  技能的全局变量在 DLL 里，一个 DLL 只有一份；两条链同时跑同一个技能时，
     *        它们的 run() 会并发操作同一份全局对象（例如同一个相机句柄）。
     *        如果技能不是线程安全的，或者两条链各自需要独占设备，就设成 false。
     */
    void setShareSkillAcrossChains(bool on) { shareSkill_ = on; }

    /// @brief 当前"多链共用同一个技能"的策略
    bool shareSkillAcrossChains() const { return shareSkill_; }

private:
    /**
     * @brief 一个技能槽：这个 DLL 现在有没有被 init、被哪些链用着
     * @note  受 sessionMtx_ 保护
     */
    struct SkillSlot
    {
        std::shared_ptr<LoadSkills> loader;   ///< 加载器(已 init)
        std::set<std::string>       chains;   ///< 持有它的链名集合(空 = 没人用)
    };

    /**
     * @brief 一条链的运行时对象
     */
    struct Chain
    {
        SkillList             list;               ///< 链定义
        RunState              state;              ///< 状态快照(受 mtx_ 保护)
        std::thread           worker;             ///< 工作线程
        std::atomic<bool>     stopFlag{false};    ///< 停止请求

        /**
         * @brief 本次 start 的技能会话（与 list.skills 一一对应，顺序一致）
         * @note  只在"工作线程不存在"的时候写（起线程前 / join 之后），
         *        工作线程里只读，所以读它不需要加锁。
         */
        std::vector<std::shared_ptr<LoadSkills>> session;
    };

    /**
     * @brief 技能的唯一键（归一化路径 + 扩展名），用来判断"是不是同一个 DLL"
     * @note  规则必须和 LoadSkills 构造函数里的归一化保持一致
     */
    static std::string skillKey(const Skill& skill);

    /**
     * @brief 为一条链建立技能会话：逐个 load + init()
     * @param list    技能链
     * @param session 输出：与 list.skills 一一对应的加载器
     * @param inited  输出：本次真正执行了 init() 的技能数（复用的不算）
     * @param why     输出：失败原因
     * @return false 表示某个技能 init 失败（调用方必须用 releaseSkillsLocked 回滚）
     * @note  调用方必须持有 sessionMtx_
     */
    bool acquireSkillsLocked(const SkillList& list,
                             std::vector<std::shared_ptr<LoadSkills>>& session,
                             std::size_t& inited,
                             std::string& why);

    /**
     * @brief 归还技能会话：没人用了就 end() + 卸载 DLL
     * @param chainName 链名(日志用)
     * @param session   技能会话，函数内部会清空它（把引用还回去）
     * @note  调用方必须持有 sessionMtx_
     */
    void releaseSkillsLocked(const std::string& chainName,
                             std::vector<std::shared_ptr<LoadSkills>>& session);

    /**
     * @brief 工作线程主体：按固定节拍反复跑链里的技能
     */
    void workerMain(const std::shared_ptr<Chain>& chain);

    /**
     * @brief 跑一轮链
     * @return false 表示这一轮中途出错（错误已写进 state.error）
     */
    bool runOneLoop(const std::shared_ptr<Chain>& chain);

    /**
     * @brief 更新状态时间戳（调用方需持有 mtx_）
     */
    void touch(RunState& st);

    /**
     * @brief 睡到指定时刻（可被 stop 打断）
     * @param stopFlag 停止标志
     * @param until    目标时刻(steady_clock)
     * @return true 正常睡到点；false 被 stop 打断
     * @note  内部每 50ms 醒一次检查 stop，最后一段按剩余时间精确睡，
     *        所以节拍不会因为"最多等到 50ms 的整数倍"而漂移。
     */
    static bool sleepUntil(const std::atomic<bool>& stopFlag,
                           std::chrono::steady_clock::time_point until);

    /**
     * @brief 卸载所有还活着的技能（先 end 再释放加载器）
     */
    void releaseAll();

private:
    Memory*                                             memory_;             ///< 公共内存
    mutable std::mutex                                  mtx_;                ///< 保护 chains_ 与状态快照
    std::mutex                                          sessionMtx_;
    std::map<std::string, std::shared_ptr<Chain>>       chains_;             ///< 链名 -> 运行时对象
    std::map<std::string, SkillSlot>                    slots_;              ///< 技能键 -> 技能槽(已 init)
    std::atomic<bool>                                   shareSkill_{true};   ///< 多链共用同一技能的策略
    Time                                                timer_;              ///< 时间戳工具
}; // class RunSkillsLists

} // namespace sc::tools

#endif // RUNSKILLSLISTS_HPP
