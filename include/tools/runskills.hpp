/**
 * @file        memory.hpp
 * @brief       内存工具头文件（pimpl + 全部托管给 libtools.dll）
 * @author      o_O a17398481273@163.com
 * @date        2026-10-07
 * @version     0.0.1
 *
 * @pre         为了处理AI写的一坨而处理的run
 */


#ifndef RUNSKILLS_HPP
#define RUNSKILLS_HPP

#include <vector>
#include <string>
#include <thread>

#include "logger.hpp"
#include "yaml.hpp"
#include "type.hpp"
#include "loadskills.hpp"
#include "memory.hpp"
#include "assign.hpp"

namespace sc::tools
{

/**
 * @brief 链管理结构体
 */
struct SkillsChain
{
    SkillList    list;          ///< 运行开始前最终链构成
    std::thread  thread;        ///< 运行线程
    bool         run = false;   ///< 是否运行
    bool         state = false; ///< 运行状态   
    int          time = 0;      ///< 花费时间每轮ms
    std::string  error;         ///< 上一个错误码

    std::mutex    mtx_error;    ///< 错误码锁
    std::mutex    mtx_state;    ///< 运行状态锁
    std::mutex    mtx_time;     ///< 时间更改锁
    std::mutex    mtx_run;      ///< 更改运行锁

    SkillsChain() = default;  
    explicit SkillsChain(const SkillList& l) : list(l) {}
    explicit SkillsChain(SkillList&& l)      : list(std::move(l)) {}
};

/**
 * @brief 全局dll统一句柄
 * @note  全局dll管理句柄,仅仅只有loadAll在程序开始前统一注册
 *        skills'name -> LoadSkills
 */
inline std::map<std::string,LoadSkills> loadskills;

/**
 * @brief  加载所有dll库
 * @param  skills 所有的skills
 * @return 加载dll是否成功
 * @note   必须在程序开始前运行，不然会导致dll加载失败
 *         而且如果有新的dll加入，或者dll更改需要重新启动程序
 */
TOOLS_API bool loadAll(std::vector<Skill> skills);


class TOOLS_API Runskills
{
private:
    Memory *mem_;                                        ///< 公共内存指针
    Assign &assign_;                                     ///< 链管理器
    Time   &time_;                                       ///< 时间管理器
    std::map<std::string,SkillsChain> chains_;           ///< 链管理

    std::mutex    mtx_chain;                             ///< 链管理锁

private:
    /**
     * @brief 运行链线程函数
     * @param chain 运行链引用
     */
    void runthread(SkillsChain &chain);


    /**
     * @brief 睡到指定时刻（可被 stop 打断）
     * @param stopFlag 停止标志
     * @param until    目标时刻(steady_clock)
     * @return true 正常睡到点；false 被 stop 打断
     */
    static bool sleepUntil(SkillsChain &chain,std::chrono::steady_clock::time_point until);

    /**
     * @brief  跑一轮
     * @param  chain 运行链引用
     * @return true 正常运行 false run异常记录logger但是不会影响主线程
     * @note   运行chain中的所有run()
     */
    bool runOnce(SkillsChain &chain);

    /**
     * @brief  跑chain链的init
     * @param  chain 运行链引用
     * @return true 正常运行 false run异常记录logger但是不会影响主线程
     * @note   运行chain中的所有init()
     */
    bool runInit(SkillsChain &chain);

    /**
     * @brief  跑chain链的end
     * @param  chain 运行链引用
     * @return true 正常运行 false run异常记录logger但是不会影响主线程
     * @note   运行chain中的所有end()
     */
    bool runEnd(SkillsChain &chain);

public:

    /**
     * @brief 构造函数
     */
    Runskills(Time &time,Memory *memory,Assign &assign);

    /**
     * @brief  启动一条技能链
     * @param  list 技能链
     * @return true 已启动；false 名称为空、该链已在运行、或某个技能的 init 失败
     */
    bool start(const SkillList& chain);

    /**
     * @brief 停止一条技能链、回收线程
     * @param name 技能链名称
     * @return true 停掉了(或本来就已停止)；false 该链不存在
     * @note  不会回收dll
     *        这里只是软发送关闭指令，并不是直接杀死
     *        调用end()子线程函数里面实现
     */
    bool stop(const std::string& name);

    /**
     * @brief 停止所有技能链
     * @return 被停止的链数量
     */
    std::size_t stopAll();

    /**
     * @brief 正在运行的链数量
     */
    std::size_t runningCount();

    /**
     * @brief 所有链名
     */
    std::vector<std::string> names();

    /**
     * @brief 状态 -> JSON（给 /api/GetRunStatus 用）
     * @return {"running":n,"lists":[{...RunState...}, ...]}
     */
    nlohmann::json statusJson();

}; // class runskills

}  // namespace tools

#endif