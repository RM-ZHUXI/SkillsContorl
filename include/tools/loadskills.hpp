/**
 * @file        loadskills.hpp
 * @brief       skill运行工具
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-09
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
    Skill       skill;
    DllHandle   handle;     ///< 动态库标头
    std::string libPath;    ///< 归一化后的动态库路径（报错信息用）
    bool        unloaded;   ///< 是否已经显式卸载
    bool        inited;     ///< init() 是否已经执行过
    bool        initTried;  ///< 是否尝试过 init（用来区分"还没 init"和"init 失败过"）

    void*       initAddr;   ///< init 函数地址
    void*       runAddr;    ///< run 函数地址
    void*       endAddr;    ///< end 函数地址(可为空)
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
     * @note  C++ 里 init(tools::Memory&) 和 init(tools::Memory*) 是两个不同的重载，
     *        只要实现签名和头文件声明不一致，导出名就会被修饰成
     *        _Z4initPN5tools6MemoryE，直接按 "init" 查找会失败。
     *        这里做一次兼容回退并告警，避免"技能偷偷没初始化、memory 一直取不到"。
     */
    void* findSymbol(const std::vector<std::string>& candidates, std::string& hitName);

    /**
     * @brief 解析 init/run/end 三个入口符号
     * @note  缺 init / run 会抛异常，缺 end 只告警
     */
    void resolveSymbols();

public:
    /*
     * @brief 默认构造函数
     */
    LoadSkills() = default;

    /**
     * @brief       构造函数
     * @param       skill skill信息
     * @note        动态库加载失败会抛异常，不会静默返回
     */
    explicit LoadSkills(Skill skill);

    /**
     * @brief       析构函数
     * @note        默认**不**卸载动态库，原因见 unload()
     */
    ~LoadSkills();

    /**
     * @brief       禁止拷贝
     * @note        handle 是**拥有型**资源：LoadLibrary 的引用计数只加了 1。
     *              一拷贝就变成两个对象各自持同一个 handle，析构时各 FreeLibrary 一次，
     *              引用计数多减一次 => DLL 在你还拿着 handle 的时候就被真正卸载了。
     *              之后 GetProcAddress 只会拿到 nullptr，表现为日志里先出现
     *              「动态库加载成功」，紧接着 start 链时又报「缺少 init 导出符号」。
     *              需要转移所有权请用移动（见下），或者放进 shared_ptr。
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
     *              —— 技能链循环运行时，每轮都 init 会因为 insert 重名而抛异常。
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
     *      -> void run();                          // 必须存在（extern "C"）
     *      -> void end();                          // 可选（extern "C"）
     * @param memory 内存管理器，原样传给技能
     */
    void run(tools::Memory *memory);

    /**
     * @brief 显式卸载动态库
     * @warning 卸载前必须清掉本技能写进 memory 的数据（memory->erasePrefix(...)）。
     *          原因：memory 里的值（std::any）是在技能 DLL 里构造的，析构函数位于
     *          技能 DLL 的代码段；DLL 一卸载，之后 clear()/~Memory()->erase()
     *          就会跳进已卸载的内存导致崩溃。
     * @note  析构函数默认**不**卸载，就是为了避开这个坑。
     */
    void unload();

    /// @brief 是否已经 load() 过（init 已执行）
    bool loaded() const { return inited; }

    /// @brief 当前加载的动态库路径
    const std::string& path() const { return libPath; }

    /// @brief 技能信息
    const Skill& info() const { return skill; }
};// class LoadSkills

} // namespace tools

#endif