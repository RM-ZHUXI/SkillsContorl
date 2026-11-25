#include "tools/loadskills.hpp"
#include "tools/logger.hpp"

#include <stdexcept>
#include <utility>

namespace sc::tools
{

namespace {

/**
 * @brief 兜底名称
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

/**
 * @brief 转义斜杠
 */
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

void LoadSkills::reload(std::string skillName,std::string url)
{
    if(this->handle != nullptr)
    {
        unload();
    }

    this->name = skillName;

    libPath = normalizePath(url);

    handle = LOAD_LIB(libPath);
    
    if (handle == nullptr)
    {
        throw std::runtime_error("[LoadSkills] 动态库加载失败: " + libPath + " 错误: " + GET_ERROR());
    }

    logger()->info("[LoadSkills] 动态库加载成功: {}", libPath);
}

LoadSkills::LoadSkills(std::string skillName,std::string url)
    : name(skillName)
    , handle(nullptr)
    , initAddr(nullptr)
    , runAddr(nullptr)
    , endAddr(nullptr)
{
    this->reload(skillName,url);
}

void* LoadSkills::findSymbol(const std::vector<std::string>& candidates,std::string& hitName)
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
        throw std::runtime_error("[LoadSkills] 动态库缺少 init 导出符号 (已尝试: " + join(initNames()) + "): " + libPath + "；请检查技能 include/*.hpp 的声明与实现签名是否完全一致，" + " extern \"C\" 里导出 void init(tools::Memory *memory)");
    }

    if (runAddr == nullptr)
    {
        throw std::runtime_error("[LoadSkills] 动态库缺少必须的 run() 函数 (已尝试: " + join(runNames()) + "): " + libPath + " 错误: " + GET_ERROR());
    }

    if (initName != initNames().front() || runName != runNames().front())
    {
        logger()->warn("[LoadSkills] {} 的入口用了被 C++ 修饰的导出名 (init={}, run={}), 建议在头文件里用 extern \"C\" 声明 void init(tools::Memory *memory); ",name, initName, runName);
    }

    if (endAddr == nullptr)logger()->info("[LoadSkills] {} 没有 end() 入口，跳过收尾", name);
}

/**
 * @brief 加载并初始化
 */
void LoadSkills::load(group::memory *memory)
{
    using InitFunc = void (*)(group::memory*);

    if (memory == nullptr)throw std::runtime_error("[LoadSkills] 传入的 memory 为空: " + libPath);

    resolveSymbols();

    logger()->info("[LoadSkills] {} 入口: init={}, run={}, end={}",name, initName, runName, endName.empty() ? "<无>" : endName);

    auto initFunc = reinterpret_cast<InitFunc>(initAddr);

    try
    {
        initFunc(memory);
    }
    catch (const std::exception& e)
    {
        throw std::runtime_error("[LoadSkills] 技能 " + name + " 的 init() 抛异常: " + e.what());
    }
    catch (...)
    {
        throw std::runtime_error("[LoadSkills] 技能 " + name + " 的 init() 抛出未知异常");
    }
}

/**
 * @brief 只执行 run()
 */
void LoadSkills::runOnce()
{
    using RunFunc = void (*)();

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

    if (endAddr == nullptr) return;

    auto endFunc = reinterpret_cast<EndFunc>(endAddr);

    try
    {
        endFunc();
    }
    catch (const std::exception& e)
    {
        logger()->warn("[LoadSkills] 技能 {} 的 end() 抛异常（已忽略）: {}", name, e.what());
    }
    catch (...)
    {
        logger()->warn("[LoadSkills] 技能 {} 的 end() 抛出未知异常（已忽略）", name);
    }
}

void LoadSkills::run(group::memory *memory)
{
    load(memory);
    runOnce();
    end();

    logger()->info("[LoadSkills] {} 运行结束", name);
}

void LoadSkills::unload()
{
    if (handle == nullptr) return;
    FREE_LIB(handle);
    handle = nullptr;
}

LoadSkills::LoadSkills(LoadSkills&& other) noexcept
    : name(std::move(other.name))
    , handle(other.handle)
    , libPath(std::move(other.libPath))
    , initAddr(other.initAddr)
    , runAddr(other.runAddr)
    , endAddr(other.endAddr)
    , initName(std::move(other.initName))
    , runName(std::move(other.runName))
    , endName(std::move(other.endName))
{
    other.handle    = nullptr;
    other.initAddr  = nullptr;
    other.runAddr   = nullptr;
    other.endAddr   = nullptr;
}

LoadSkills& LoadSkills::operator=(LoadSkills&& other) noexcept
{
    if (this == &other) return *this;

    unload();

    name     = std::move(other.name);
    handle    = other.handle;
    libPath   = std::move(other.libPath);
    initAddr  = other.initAddr;
    runAddr   = other.runAddr;
    endAddr   = other.endAddr;
    initName  = std::move(other.initName);
    runName   = std::move(other.runName);
    endName   = std::move(other.endName);

    other.handle    = nullptr;
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