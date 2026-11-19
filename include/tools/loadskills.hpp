/**
 * @file        loadskills.hpp
 * @brief       skill运行工具
 * @author      o_O a17398481273@163.com
 * @date        2026-11-18
 * 
 * @version     1.2.0
 * @par         添加强制关闭函数 stop()        
 *
 * @version     1.3.0
 * @par         拆分入口调用，支持被线程管理器反复调用
 *              load() 解析符号并执行一次 init()
 *              runOnce() 只执行 run()
 *              end() 只执行 end()
 *              run() 仍然等于 load()+runOnce()+end()，旧调用方不受影响
 */

#ifndef LOADSKILLS_HPP
#define LOADSKILLS_HPP

#include "findskills.hpp"
#include "memory.hpp"
#include "tools_api.hpp"

#include <string>
#include <vector>

#ifdef _WIN32
    #include <windows.h>
    using DllHandle = HMODULE;
    #define LOAD_LIB(path) LoadLibraryA(path.c_str())
    #define GET_FUNC(handle, name) GetProcAddress(handle, name)
    #define FREE_LIB(handle) FreeLibrary(handle)
    #define LIB_EXTENSION ".dll"
    #define GET_ERROR() std::to_string(GetLastError())
#else
    #include <dlfcn.h>
    using DllHandle = void*;
    #define LOAD_LIB(path) dlopen(path.c_str(), RTLD_LAZY)
    #define GET_FUNC(handle, name) dlsym(handle, name)
    #define FREE_LIB(handle) dlclose(handle)
    #define LIB_EXTENSION ".so"
    #define GET_ERROR() (dlerror() ? dlerror() : "Unknown error")
#endif

namespace sc::tools
{

class TOOLS_API LoadSkills
{
private:
    std::string name;       ///< skill名称
    DllHandle   handle;     ///< 动态库标头
    std::string libPath;    ///< 归一化后的动态库路径

    void*       initAddr;   ///< init 函数地址
    void*       runAddr;    ///< run 函数地址
    void*       endAddr;    ///< end 函数地址
    std::string initName;   ///< 实际命中的 init 导出名
    std::string runName;    ///< 实际命中的 run 导出名
    std::string endName;    ///< 实际命中的 end 导出名

    /**
     * @brief windows/linux路径处理
     * @param path 输入路径
     */
    std::string normalizePath(const std::string& path);

    /**
     * @brief 按候选名查找导出符号
     * @param candidates 候选名，按优先级排列
     * @param hitName    实际命中的名字（没命中为空）
     * @return 函数地址，全部没命中返回 nullptr
     */
    void* findSymbol(const std::vector<std::string>& candidates, std::string& hitName);

    /**
     * @brief 解析 init/run/end 三个入口符号
     * @note  缺 init / run 会抛异常，缺 end 只告警
     */
    void resolveSymbols();

    /**
     * @brief 
     */
public:
    /**
     * @brief       加载/重新加载
     * @param       skillName skill名称
     * @param       url       动态库位置
     * @note        动态库加载失败会抛异常，不会静默返回
     */
    void reload(std::string skillName,std::string url);

    /*
     * @brief 默认构造函数
     */
    LoadSkills() = default;

    /**
     * @brief       构造函数
     * @param       skillName skill名称
     * @param       url       动态库位置
     * @note        动态库加载失败会抛异常，不会静默返回
     */
    explicit LoadSkills(std::string skillName,std::string url);

    /**
     * @brief       析构函数
     * @note        默认卸载动态库
     */
    ~LoadSkills();

    /**
     * @brief       禁止拷贝
     */
    LoadSkills(const LoadSkills&)            = delete;
    LoadSkills& operator=(const LoadSkills&) = delete;

    /**
     * @brief       移动构造：把 DLL 所有权交给新对象
     * @note        被移出的对象 handle 置空、unloaded 置 true，析构是空操作
     */
    LoadSkills(LoadSkills&& other) noexcept;
    LoadSkills& operator=(LoadSkills&& other) noexcept;

    /**
     * @brief       加载并初始化
     * @param       memory 内存管理器，原样传给技能的 init
     * @note        解析入口符号 + 调用一次 init()。重复调用是安全的
     */
    void load(tools::Memory *memory);

    /**
     * @brief       执行一次 run()
     * @note        必须先 load()，否则抛异常
     */
    void runOnce();

    /**
     * @brief       执行 end()
     */
    void end();

    /**
     * @brief       一次性运行函数（= load + runOnce + end）
     * @note
     * 运行 -> void init(tools::Memory *memory);   // 必须存在（extern "C"）
     *      -> void run();                         // 必须存在（extern "C"）
     *      -> void end();                         // 可选（extern "C"）
     * @param memory 内存管理器，原样传给技能
     */
    void run(tools::Memory *memory);

    /**
     * @brief 显式卸载动态库
     * @warning 卸载前必须清掉本技能写进 memory 的数据（memory->erasePrefix(...)）。
     */
    void unload();

    /// @brief 当前加载的动态库路径
    const std::string& path() const { return libPath; }
};// class LoadSkills

/**
 * @brief skill信息(新版)
 */
struct SkillInfo
{
    SkillConfigInfo skill;                  ///< skill配置文件信息
    bool load = false;                      ///< 是否被加载
    bool run = false;                       ///< 是否正在运行
    sc::tools::LoadSkills loadskill;        ///< 加载函数

    /**
     * @brief 重新载入动态库
     * @throw 重新载入动态库异常情况或者skill信息异常
     */
    void reload()
    {        
        try
        {
            this->load = false;
            if(this->skill.name.empty() || this->skill.url.empty())throw std::runtime_error("[SkillInfo] reload信息错误");
            this->loadskill.reload(this->skill.name,skill.url);
            this->load = true;
        }
        catch(const std::exception& e)
        {
            throw;
        }
        
    }

    SkillInfo() = default;
    SkillInfo(const SkillInfo&)            = delete;
    SkillInfo& operator=(const SkillInfo&) = delete;
    SkillInfo(SkillInfo&&) noexcept        = default;
    SkillInfo& operator=(SkillInfo&&) noexcept = default;
};

/**
 * @brief skill链信息(新版)
 */
struct SkillListInfo
{
    std::string name;                         ///< 名称(唯一ID)
    std::string mode = "once";                ///< 模式 once / while
    int         interval = 1000;              ///< while 轮间隔(毫秒)，0 = 不等待，<0 按 1000
    std::vector<SkillInfo*> skills;           ///< 技能列表(按顺序执行)
};


} // namespace tools

#endif