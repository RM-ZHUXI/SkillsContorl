#include "tools/loadskills.hpp"
#include "tools/logger.hpp"

#include <stdexcept>
#include <utility>

namespace sc::tools
{

namespace {

/**
 * @brief 技能入口的候选导出名
 * @note  第一项是规范名（extern "C" 导出）。后面的 Itanium ABI 修饰名是兜底：
 *        技能头文件声明与实现签名不一致时（& 与 * 混用是被踩过最多的坑），
 *        导出名会变成 _Z4initPN5tools6MemoryE，按 "init" 查找就会漏掉。
 *        Memory* 和 Memory& 在 ABI 上都按指针传递，所以兜底调用是安全的。
 */
const std::vector<std::string>& initNames()
{
    static const std::vector<std::string> names = {
        "init",
        "_Z4initPN5tools6MemoryE",   // init(tools::Memory*)
        "_Z4initRN5tools6MemoryE",   // init(tools::Memory&)
    };
    return names;
}

const std::vector<std::string>& runNames()
{
    static const std::vector<std::string> names = { "run", "_Z3runv" };
    return names;
}

const std::vector<std::string>& endNames()
{
    static const std::vector<std::string> names = { "end", "_Z3endv" };
    return names;
}

std::string join(const std::vector<std::string>& v)
{
    std::string s;
    for (std::size_t i = 0; i < v.size(); ++i)
    {
        if (i) s += " / ";
        s += v[i];
    }
    return s;
}

} // anonymous namespace

std::string LoadSkills::normalizePath(const std::string& path)
{
    std::string result = path;
#ifdef _WIN32
    const char from = '/', to = '\\';
#else
    const char from = '\\', to = '/';
#endif
    for (char& c : result)
        if (c == from) c = to;
    return result;
}

LoadSkills::LoadSkills(Skill skill)
    : skill(std::move(skill))
    , handle(nullptr)
    , unloaded(false)
    , inited(false)
    , initTried(false)
    , initAddr(nullptr)
    , runAddr(nullptr)
    , endAddr(nullptr)
{

    libPath = normalizePath(this->skill.url);
    if (libPath.size() < sizeof(LIB_EXTENSION) - 1 ||
        libPath.compare(libPath.size() - (sizeof(LIB_EXTENSION) - 1),
                        sizeof(LIB_EXTENSION) - 1,
                        LIB_EXTENSION) != 0)
    {
        libPath += LIB_EXTENSION;
    }

    // 加载动态库
    handle = LOAD_LIB(libPath);
    
    if (handle == nullptr)
    {
        throw std::runtime_error(
            "[LoadSkills] 动态库加载失败: " + libPath +
            " 错误: " + GET_ERROR());
    }

    logger()->info("[LoadSkills] 动态库加载成功: {}", libPath);
}

void* LoadSkills::findSymbol(const std::vector<std::string>& candidates,
                            std::string& hitName)
{
    hitName.clear();
    for (const std::string& name : candidates)
    {
        void* addr = reinterpret_cast<void*>(GET_FUNC(handle, name.c_str()));
        if (addr != nullptr)
        {
            hitName = name;
            return addr;
        }
    }
    return nullptr;
}

/**
 * @brief 解析 init / run / end 三个入口
 * @note  缺 init 或 run 直接抛异常；缺 end 只告警（end 是可选入口）
 */
void LoadSkills::resolveSymbols()
{
    initAddr = findSymbol(initNames(), initName);
    runAddr  = findSymbol(runNames(),  runName);
    endAddr  = findSymbol(endNames(),  endName);

    if (initAddr == nullptr)
    {
        throw std::runtime_error(
            "[LoadSkills] 动态库缺少 init 导出符号 (已尝试: " + join(initNames()) + "): " + libPath +
            "；请检查技能 include/*.hpp 的声明与实现签名是否完全一致，"
            "并确认在 extern \"C\" 里导出 void init(tools::Memory *memory)");
    }

    if (runAddr == nullptr)
    {
        throw std::runtime_error(
            "[LoadSkills] 动态库缺少必须的 run() 函数 (已尝试: " + join(runNames()) + "): " +
            libPath + " 错误: " + GET_ERROR());
    }

    if (initName != initNames().front() || runName != runNames().front())
    {
        logger()->warn("[LoadSkills] {} 的入口用了被 C++ 修饰的导出名 (init={}, run={}), "
                       "建议在头文件里用 extern \"C\" 声明 void init(tools::Memory *memory); "
                       "否则换编译器(MSVC)或改签名时会再次找不到",
                       skill.name, initName, runName);
    }

    if (endAddr == nullptr)
        logger()->warn("[LoadSkills] {} 没有 end() 入口，跳过收尾", skill.name);
}

/**
 * @brief 加载并初始化
 */
void LoadSkills::load(tools::Memory *memory)
{
    using InitFunc = void (*)(Memory*);

    if (memory == nullptr)throw std::runtime_error("[LoadSkills] 传入的 memory 为空: " + libPath);

    resolveSymbols();

    logger()->info("[LoadSkills] {} 入口: init={}, run={}, end={}",
                    skill.name, initName, runName, endName.empty() ? "<无>" : endName);

    auto initFunc = reinterpret_cast<InitFunc>(initAddr);

    initTried = true;
    try
    {
        initFunc(memory);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("[LoadSkills] 技能 " + skill.name + " 的 init() 抛异常: " + e.what());
    }
    catch (...)
    {
        throw std::runtime_error("[LoadSkills] 技能 " + skill.name + " 的 init() 抛出未知异常");
    }

    inited = true;
}

/**
 * @brief 只执行 run()
 */
void LoadSkills::runOnce()
{
    using RunFunc = void (*)();

    if (!inited)
        throw std::runtime_error("[LoadSkills] 技能尚未 load() 就调用了 runOnce(): " + libPath);

    auto runFunc = reinterpret_cast<RunFunc>(runAddr);

    runFunc(); // 这里负责运行但是不处理抛出问题
}

/**
 * @brief 只执行 end()
 * @note  end 在收尾/卸载阶段调用
 */
void LoadSkills::end()
{
    using EndFunc = void (*)();

    if (!inited || endAddr == nullptr) return;

    auto endFunc = reinterpret_cast<EndFunc>(endAddr);

    try
    {
        endFunc();
    }
    catch (const std::exception& e)
    {
        logger()->warn("[LoadSkills] 技能 {} 的 end() 抛异常（已忽略）: {}", skill.name, e.what());
    }
    catch (...)
    {
        logger()->warn("[LoadSkills] 技能 {} 的 end() 抛出未知异常（已忽略）", skill.name);
    }
}

void LoadSkills::run(tools::Memory *memory)
{
    load(memory);
    runOnce();
    end();

    logger()->info("[LoadSkills] {} 运行结束", skill.name);
}

void LoadSkills::unload()
{
    if (handle == nullptr || unloaded) return;
    if (!inited && initTried)
    {
        logger()->warn("[LoadSkills] {} 的 init 曾经失败，保留在内存中不卸载"
                       "（避免析构半初始化对象时把进程带崩）", libPath);
        return;
    }

    FREE_LIB(handle);
    handle = nullptr;
    unloaded = true;
}

LoadSkills::LoadSkills(LoadSkills&& other) noexcept
    : skill(std::move(other.skill))
    , handle(other.handle)
    , libPath(std::move(other.libPath))
    , unloaded(other.unloaded)
    , inited(other.inited)
    , initTried(other.initTried)
    , initAddr(other.initAddr)
    , runAddr(other.runAddr)
    , endAddr(other.endAddr)
    , initName(std::move(other.initName))
    , runName(std::move(other.runName))
    , endName(std::move(other.endName))
{
    // 所有权已经交出去：让被移出的对象彻底变成空壳，
    // 它的析构函数（以及误调用的 load/runOnce/end/unload）都不会再碰这个 handle。
    other.handle    = nullptr;
    other.unloaded  = true;
    other.inited    = false;
    other.initTried = false;
    other.initAddr  = nullptr;
    other.runAddr   = nullptr;
    other.endAddr   = nullptr;
}

LoadSkills& LoadSkills::operator=(LoadSkills&& other) noexcept
{
    if (this == &other) return *this;

    // 先把自己原来持有的句柄按既有策略还回去（unload 内部会跳过"init 曾失败"的库）
    unload();

    skill     = std::move(other.skill);
    handle    = other.handle;
    libPath   = std::move(other.libPath);
    unloaded  = other.unloaded;
    inited    = other.inited;
    initTried = other.initTried;
    initAddr  = other.initAddr;
    runAddr   = other.runAddr;
    endAddr   = other.endAddr;
    initName  = std::move(other.initName);
    runName   = std::move(other.runName);
    endName   = std::move(other.endName);

    other.handle    = nullptr;
    other.unloaded  = true;
    other.inited    = false;
    other.initTried = false;
    other.initAddr  = nullptr;
    other.runAddr   = nullptr;
    other.endAddr   = nullptr;

    return *this;
}

LoadSkills::~LoadSkills()
{
    unload();
}

} // namespace tools