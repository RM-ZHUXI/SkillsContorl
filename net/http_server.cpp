/**
 * @file        http_server.cpp
 * @brief       http服务器请求net实现文件
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-13
 * @version     1.0.0
 *
 * @version     1.1.0
 * @par         业务接口全部 JSON 化，并接上技能链线程管理器
 */

#include "net/http_server.hpp"
#include "tools/logger.hpp"

#include <utility>
#include <cstdlib>

#ifdef _WIN32
    // 注意顺序：winsock2.h 已在 http_server.hpp 里先包含（也定义了 WIN32_LEAN_AND_MEAN）
    #include <windows.h>
    #include <shellapi.h>   // WIN32_LEAN_AND_MEAN 会把 shellapi.h 排除掉，这里显式包含以使用 ShellExecuteA
#endif

namespace sc::net
{

namespace
{

/**
 * @brief 读取整个文件到字符串
 * @return 成功返回文件内容；失败返回空字符串
 */
std::string readFile(const std::string& path)
{
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs)
        return "";

    std::ostringstream oss;
    oss << ifs.rdbuf();
    return oss.str();
}

/**
 * @brief 根据扩展名返回 Content-Type
 */
std::string mimeType(const std::string& path)
{
    auto endsWith = [&](const std::string& suffix) {
        return path.size() >= suffix.size() &&
               path.compare(path.size() - suffix.size(),
                            suffix.size(), suffix) == 0;
    };

    if (endsWith(".html") || endsWith(".htm")) return "text/html; charset=utf-8";
    if (endsWith(".css"))  return "text/css; charset=utf-8";
    if (endsWith(".js"))   return "application/javascript; charset=utf-8";
    if (endsWith(".json")) return "application/json; charset=utf-8";
    if (endsWith(".png"))  return "image/png";
    if (endsWith(".jpg") || endsWith(".jpeg")) return "image/jpeg";
    if (endsWith(".gif"))  return "image/gif";
    if (endsWith(".svg"))  return "image/svg+xml";
    if (endsWith(".ico"))  return "image/x-icon";
    if (endsWith(".txt"))  return "text/plain; charset=utf-8";
    return "application/octet-stream";
}

} // namespace

// ============================================================
// 内部工具
// ============================================================

void HttpServer::replyJson(HttpHand& hand, const nlohmann::json& body, int status)
{
    hand.setStatus(status);
    hand.setHeader("Content-Type", "application/json; charset=utf-8");
    hand.setBody(body.dump());      // 中文按 utf-8 直接输出，前端 JSON.parse 没问题
}

bool HttpServer::parseBody(const std::string& body, nlohmann::json& out, std::string& err)
{
    if (body.empty())
    {
        out = nlohmann::json::object();   // 空体当空对象，GET 也能复用同一套逻辑
        return true;
    }

    try
    {
        out = nlohmann::json::parse(body);
        return true;
    }
    catch (const std::exception& e)
    {
        // nlohmann 的解析错误信息里带 "last read: '<出错附近的原文>'"，请求体很大时
        // 这段会把整个请求原样拼回来（实测 3MB 的请求会得到 3MB 的响应体），
        // 所以先把换行清掉再截断，然后才回给前端
        std::string msg = e.what();
        for (char& c : msg)
            if (c == '\n' || c == '\r') c = ' ';
        if (msg.size() > 200)
            msg = msg.substr(0, 200) + "...(已截断)";

        err = "请求体不是合法 JSON: " + msg;
        return false;
    }
}

// ============================================================
// 内置 API handler：公共内存
// ============================================================

void HttpServer::apiGetMemory(std::string body, HttpHand& hand)
{
    (void)body;   // 请求体未使用，避免未使用参数警告

    replyJson(hand, memory_.toJsonObject());
}

void HttpServer::apiGetMemoryValues(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json req;
    std::string err;

    // 允许 body 里带 {"names":["a","b"]} 只取部分键；不带就取全部
    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    std::vector<std::string> names;
    if (req.is_object() && req.contains("names") && req["names"].is_array())
        names = req["names"].get<std::vector<std::string>>();

    // 这里直接复用 Assign 的 JSON 转换（公共内存 <-> json 的唯一实现处）
    replyJson(hand, tools::Assign::readMemoryValues(memory_, names));
}

void HttpServer::apiSetMemory(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json req;
    std::string err;

    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    // 兼容两种写法：{"items":{...}} 和直接 {...}
    json items = req;
    if (req.is_object() && req.contains("items"))
        items = req["items"];

    json report;
    const bool ok = tools::Assign::assignMemory(memory_, items, report);

    report["status"] = ok ? "ok" : "error";
    if (!ok && !report.contains("message"))
        report["message"] = "部分键写入失败，请看 failed 列表";

    tools::logger()->info("[Http] SetMemory 写入 {} 项，失败 {} 项",
                   report.value("written", 0), report["failed"].size());

    replyJson(hand, report, ok ? 200 : 400);
}

void HttpServer::apiDelMemory(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json req;
    std::string err;

    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    std::size_t erased = 0;

    if (req.is_object() && req.contains("prefix") && req["prefix"].is_string())
    {
        // 按前缀批量删（例如技能卸载前清掉它写进内存的数据）
        erased = memory_.erasePrefix(req["prefix"].get<std::string>());
    }
    else
    {
        std::vector<std::string> names;
        if (req.is_object() && req.contains("names") && req["names"].is_array())
            names = req["names"].get<std::vector<std::string>>();
        else if (req.is_array())
            names = req.get<std::vector<std::string>>();

        if (names.empty())
        {
            replyJson(hand, { {"status", "error"}, {"message", "需要 names 数组或 prefix 字符串"} }, 400);
            return;
        }

        erased = tools::Assign::eraseMemory(memory_, names);
    }

    tools::logger()->info("[Http] DelMemory 删除 {} 项", erased);

    replyJson(hand, { {"status", "ok"}, {"erased", erased} });
}

// ============================================================
// 内置 API handler：技能
// ============================================================

void HttpServer::apiGetSkills(std::string body, HttpHand& hand)
{
    (void)body;   // 请求体未使用，避免未使用参数警告

    std::vector<Skill> skills = findSkills_.get_skills();
    replyJson(hand, nlohmann::json(skills));
}

void HttpServer::apiUpdateSkills(std::string body, HttpHand& hand)
{
    (void)body;   

    /*
     * 只有"没有任何技能链在运行"时才允许重新扫描技能目录：
     * 运行时技能 DLL 正在被使用，随便改动技能表容易让链里引用的技能失效
     */ 
    const std::size_t running = runSkillsLists_.runningCount();

    if (running > 0)
    {
        nlohmann::json result;
        result["status"]  = "error";
        result["message"] = "有 " + std::to_string(running) + " 条技能链正在运行，请先「全部停止」再更新技能";
        result["running"] = running;
        replyJson(hand, result, 400);
        return;
    }

    const bool ok = findSkills_.update(true);

    if (ok)
        assign_.update(); 

    nlohmann::json result;
    result["status"]      = ok ? "ok" : "error";
    result["skills_size"] = findSkills_.get_skills().size();
    if (!ok) result["message"] = "技能扫描失败，详情见 logs/ 日志";

    replyJson(hand, result, ok ? 200 : 400);
}

// ============================================================
// 内置 API handler：摄像头标定
// ============================================================

namespace
{
// 把 CalibrateCamera 的 9 个字段打包成 JSON（本文件内部使用，不对外暴露）
nlohmann::json calibConfigToJson(functions::CalibrateCamera& c)
{
    return {
        {"commonConfigPath", c.getCommonConfigPath()},
        {"path",             c.getPath()},
        {"matrixName",       c.getMatrixName()},
        {"coeffsName",       c.getCoeffsName()},
        {"cols",             c.getCols()},
        {"rows",             c.getRows()},
        {"distance",         c.getDistance()}
    };
}
} // namespace

void HttpServer::apiGetCalibrateConfig(std::string body, HttpHand& hand)
{
    (void)body;

    nlohmann::json j;
    j["status"] = "ok";
    j["config"] = calibConfigToJson(calibratecamera_);
    replyJson(hand, j);
}

void HttpServer::apiSetCalibrateConfig(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    // 标定运行中不允许改配置，避免线程里读到半新半旧的参数
    if (calibRunning_.load())
    {
        replyJson(hand, { {"status", "error"},
                          {"message", "标定正在运行，暂不能修改配置"} }, 400);
        return;
    }

    json req;
    std::string err;
    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }
    if (!req.is_object())
    {
        replyJson(hand, { {"status", "error"}, {"message", "body 应为 JSON 对象"} }, 400);
        return;
    }

    // 逐字段更新，缺省字段保持原值
    try
    {
        if (req.contains("commonConfigPath") && req["commonConfigPath"].is_string())
            calibratecamera_.setCommonConfigPath(req["commonConfigPath"].get<std::string>());
        if (req.contains("path") && req["path"].is_string())
            calibratecamera_.setPath(req["path"].get<std::string>());
        if (req.contains("matrixName") && req["matrixName"].is_string())
            calibratecamera_.setMatrixName(req["matrixName"].get<std::string>());
        if (req.contains("coeffsName") && req["coeffsName"].is_string())
            calibratecamera_.setCoeffsName(req["coeffsName"].get<std::string>());
        if (req.contains("cols") && req["cols"].is_number_integer())
            calibratecamera_.setCols(req["cols"].get<int>());
        if (req.contains("rows") && req["rows"].is_number_integer())
            calibratecamera_.setRows(req["rows"].get<int>());
        if (req.contains("distance") && req["distance"].is_number_integer())
            calibratecamera_.setDistance(req["distance"].get<int>());
    }
    catch (const std::exception& e)
    {
        replyJson(hand, { {"status", "error"},
                          {"message", std::string("设置失败：") + e.what()} }, 400);
        return;
    }

    tools::logger()->info("[Http] 标定配置已更新");

    json j;
    j["status"] = "ok";
    j["config"] = calibConfigToJson(calibratecamera_);   // 回显最新配置
    replyJson(hand, j);
}

void HttpServer::apiStartCalibrate(std::string body, HttpHand& hand)
{
    (void)body;

    // CAS：false -> true，已在跑就直接拒掉（防止并发触发两条标定线程）
    bool expected = false;
    if (!calibRunning_.compare_exchange_strong(expected, true))
    {
        replyJson(hand, { {"status", "error"},
                          {"message", "标定正在进行中，请等待完成"} }, 400);
        return;
    }

    {
        std::lock_guard<std::mutex> lk(calibMutex_);
        calibState_  = "running";
        calibResult_.clear();
    }

    tools::logger()->info("[Http] 启动标定线程");

    // 独立线程执行；detach 后由 calibRunning_ / calibState_ 做生命周期跟踪
    std::thread([this]() {
        std::string result;
        std::string state;

        try
        {
            result = calibratecamera_.start();      // 同步阻塞，跑完才返回
            state  = "done";
        }
        catch (const std::exception& e)
        {
            result = std::string("标定异常: ") + e.what();
            state  = "failed";
        }
        catch (...)
        {
            result = "标定异常: 未知错误";
            state  = "failed";
        }

        {
            std::lock_guard<std::mutex> lk(calibMutex_);
            calibState_  = state;
            calibResult_ = result;
        }

        calibRunning_.store(false);
        tools::logger()->info("[Http] 标定线程结束，state={}，result={}", state, result);
    }).detach();

    replyJson(hand, { {"status", "ok"},
                      {"message", "标定已启动，请轮询 GetCalibrateStatus 获取结果"},
                      {"state", "running"} });
}

void HttpServer::apiGetCalibrateStatus(std::string body, HttpHand& hand)
{
    (void)body;

    nlohmann::json j;
    {
        std::lock_guard<std::mutex> lk(calibMutex_);
        j["state"]   = calibState_;
        j["running"] = (calibState_ == "running");
        if (!calibResult_.empty())
            j["result"] = calibResult_;
    }
    j["status"] = "ok";
    replyJson(hand, j);
}

void HttpServer::apiOpenCalibrateFolder(std::string body, HttpHand& hand)
{
    (void)body;

    // 取当前配置里的标定图片路径
    std::string folder = calibratecamera_.getPath();
    if (folder.empty())
    {
        replyJson(hand, { {"status", "error"}, {"message", "标定图片路径为空，请先保存配置"} }, 400);
        return;
    }

    // 用绝对路径：explorer / xdg-open 对相对路径的解析基于各自的 cwd，
    // 跟服务进程的 cwd 不一定一样，相对路径会打开错地方
    std::error_code ec;
    const std::filesystem::path absPath = std::filesystem::absolute(folder, ec);
    if (ec || !std::filesystem::exists(absPath, ec) || !std::filesystem::is_directory(absPath, ec))
    {
        replyJson(hand, { {"status", "error"},
                          {"message", "路径不存在或不是目录: " + folder} }, 400);
        return;
    }

    const std::string abs = absPath.string();
    bool ok = false;

#ifdef _WIN32
    // explorer 打开文件夹；ShellExecuteA 用 "open" 也行，但 explorer 直接调用更稳
    const std::string cmd = "explorer \"" + abs + "\"";
    // std::system 在 Windows 下会弹黑框，用 ShellExecuteA 更干净
    HINSTANCE ret = ShellExecuteA(nullptr, "open", abs.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ok = (reinterpret_cast<INT_PTR>(ret) > 32);
#else
    const std::string cmd = "xdg-open \"" + abs + "\" >/dev/null 2>&1 &";
    ok = (std::system(cmd.c_str()) == 0);
#endif

    if (ok)
    {
        tools::logger()->info("[Http] 已打开标定文件夹: {}", abs);
        replyJson(hand, { {"status", "ok"}, {"path", abs} });
    }
    else
    {
        tools::logger()->warn("[Http] 打开标定文件夹失败: {}", abs);
        replyJson(hand, { {"status", "error"},
                          {"message", "打开失败: " + abs} }, 500);
    }
}

// ============================================================
// 内置 API handler：技能链
// ============================================================

void HttpServer::apiGetSkillChain(std::string body, HttpHand& hand)
{
    (void)body;

    nlohmann::json result;
    result["status"]       = "ok";
    result["skills_lists"] = assign_.getListsJson();
    result["loaded"]       = tools::LOAD_STATUS;

    replyJson(hand, result);
}

void HttpServer::apiUpdateSkillChain(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json arr;
    std::string err;

    if (!parseBody(body, arr, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    // 允许 {"skills_lists":[...]} 和直接给数组两种写法
    if (arr.is_object() && arr.contains("skills_lists"))
        arr = arr["skills_lists"];

    if (!arr.is_array())
    {
        replyJson(hand, { {"status", "error"}, {"message", "body 应为技能链数组"} }, 400);
        return;
    }

    const bool ok = assign_.setListsJson(arr);

    json result;
    result["status"] = ok ? "ok" : "error";
    if (!ok) result["message"] = "保存失败：技能链重名、mode 非法或引用了不存在的技能（详见日志）";
    result["skills_lists"] = assign_.getListsJson();

    replyJson(hand, result, ok ? 200 : 400);
}

void HttpServer::apiStartSkillChain(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json req;
    std::string err;

    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    // 更新技能链
    assign_.update();

    json started = json::array();
    json failed  = json::array();

    auto startOne = [&](const std::string& name)
    {
        if (!assign_.hasList(name))
        {
            failed.push_back({ {"name", name}, {"message", "技能链不存在"} });
            return;
        }
        if (runSkillsLists_.start(assign_.getList(name)))
            started.push_back(name);
        else
            failed.push_back({ {"name", name}, {"message", "已在运行或启动失败"} });
    };

    if (req.is_object() && req.value("all", false))
    {
        // 按配置里的技能链全部启动（每条链各自一条线程）
        for (const auto& kv : assign_.getLists())
            startOne(kv.first);
    }
    else if (req.is_object() && req.contains("name") && req["name"].is_string())
    {
        startOne(req["name"].get<std::string>());
    }
    else
    {
        replyJson(hand, { {"status", "error"}, {"message", "需要 {\"name\":\"链名\"} 或 {\"all\":true}"} }, 400);
        return;
    }

    json result;
    result["status"]  = failed.empty() ? "ok" : "error";
    result["started"] = started;
    result["failed"]  = failed;
    result["running"] = runSkillsLists_.runningCount();

    replyJson(hand, result, failed.empty() ? 200 : 400);
}

void HttpServer::apiStopSkillChain(std::string body, HttpHand& hand)
{
    using json = nlohmann::json;

    json req;
    std::string err;

    if (!parseBody(body, req, err))
    {
        replyJson(hand, { {"status", "error"}, {"message", err} }, 400);
        return;
    }

    json stopped = json::array();

    if (req.is_object() && req.value("all", false))
    {
        // ★ 先记下"当前正在跑的链名"，再停。
        //   stopAll() 现在会等每条链的线程收尾（含技能 end()），返回时所有链的
        //   running 都已经是 false，那之后再去问 names() 就什么都不剩了 ——
        //   那样响应里的 stopped 永远是空数组，前端也就没法告诉用户停了哪几条。
        stopped = runSkillsLists_.names();
        runSkillsLists_.stopAll();
    }
    else if (req.is_object() && req.contains("name") && req["name"].is_string())
    {
        const std::string name = req["name"].get<std::string>();
        if (runSkillsLists_.stop(name))
            stopped.push_back(name);
        else
        {
            replyJson(hand, { {"status", "error"}, {"message", "该技能链没有运行记录: " + name} }, 400);
            return;
        }
    }
    else
    {
        replyJson(hand, { {"status", "error"}, {"message", "需要 {\"name\":\"链名\"} 或 {\"all\":true}"} }, 400);
        return;
    }

    tools::logger()->info("[Http] StopSkillChain 停止 {} 条", stopped.size());

    replyJson(hand, { {"status", "ok"}, {"stopped", stopped}, {"running", runSkillsLists_.runningCount()} });
}

void HttpServer::apiGetRunStatus(std::string body, HttpHand& hand)
{
    (void)body;

    nlohmann::json result = runSkillsLists_.statusJson();
    result["status"] = "ok";

    replyJson(hand, result);
}

// ============================================================
// 构造 / 析构
// ============================================================

HttpServer::HttpServer(const std::string& config, tools::Memory& memory,tools::FindSkills &findSkills, tools::Assign &assign,tools::Runskills &runSkillsLists_,functions::CalibrateCamera calibratecamera)
    : findSkills_(findSkills)
    , memory_(memory)
    , assign_(assign)
    , runSkillsLists_(runSkillsLists_)
    , http_(config)
    , calibratecamera_(calibratecamera)
{
    // http_auto_open：启动后要不要自动拉起浏览器（缺省 true，配置里没写就是开）
    try
    {
        auto yaml = tools::load(config);
        if (yaml["http_auto_open"])
            autoOpen_ = yaml["http_auto_open"].as<bool>();
    }
    catch (const std::exception& e)
    {
        tools::logger()->warn("[Http] 读取 http_auto_open 失败，按默认(自动打开)处理: {}", e.what());
    }

    // 构造时一次性注册所有路由
    registerAll();
}

// ============================================================
// 注册
// ============================================================

void HttpServer::registerAll()
{
    // ---- 公共内存 ----
    registerHandler("/api/GetMemory",       [this](std::string body, HttpHand& hand){apiGetMemory(std::move(body), hand);});
    registerHandler("/api/GetMemoryValues", [this](std::string body, HttpHand& hand){apiGetMemoryValues(std::move(body), hand);});
    registerHandler("/api/SetMemory",       [this](std::string body, HttpHand& hand){apiSetMemory(std::move(body), hand);});
    registerHandler("/api/DelMemory",       [this](std::string body, HttpHand& hand){apiDelMemory(std::move(body), hand);});

    // ---- 技能 ----
    registerHandler("/api/GetSkills",    [this](std::string body, HttpHand& hand){apiGetSkills(std::move(body), hand);});
    registerHandler("/api/UpdateSkills", [this](std::string body, HttpHand& hand){apiUpdateSkills(std::move(body), hand);});

    // ---- 技能链 ----
    registerHandler("/api/GetSkillChain",    [this](std::string body, HttpHand& hand){apiGetSkillChain(std::move(body), hand);});
    registerHandler("/api/UpdateSkillChain", [this](std::string body, HttpHand& hand){apiUpdateSkillChain(std::move(body), hand);});
    registerHandler("/api/StartSkillChain",  [this](std::string body, HttpHand& hand){apiStartSkillChain(std::move(body), hand);});
    registerHandler("/api/StopSkillChain",   [this](std::string body, HttpHand& hand){apiStopSkillChain(std::move(body), hand);});
    registerHandler("/api/GetRunStatus",     [this](std::string body, HttpHand& hand){apiGetRunStatus(std::move(body), hand);});

    // ---- 摄像头标定 ----
    registerHandler("/api/GetCalibrateConfig",[this](std::string body, HttpHand& hand){apiGetCalibrateConfig(std::move(body), hand);});
    registerHandler("/api/SetCalibrateConfig",[this](std::string body, HttpHand& hand){apiSetCalibrateConfig(std::move(body), hand);});
    registerHandler("/api/StartCalibrate",[this](std::string body, HttpHand& hand){apiStartCalibrate(std::move(body), hand);});
    registerHandler("/api/GetCalibrateStatus",[this](std::string body, HttpHand& hand){apiGetCalibrateStatus(std::move(body), hand);});
    registerHandler("/api/OpenCalibrateFolder",[this](std::string body, HttpHand& hand){apiOpenCalibrateFolder(std::move(body), hand);});
}

void HttpServer::registerHandler(const std::string& path, HttpHandler handler)
{
    http_.registerHandler(path, std::move(handler));
}

// ============================================================
// 启动 / 停止
// ============================================================

bool HttpServer::start()
{
    const bool ok = http_.start();

    if (!ok) return false;

    // 监听已经就绪（listen 已完成，连接会排在 backlog 里），可以直接拉起浏览器
    if (autoOpen_)
    {
        openConsole();
    }
    else
    {
        // 关掉自动打开时也把地址打出来，方便手动复制
        tools::logger()->info("[Http] 控制台地址: {} （http_auto_open=false，未自动打开）", consoleUrl());
    }

    return true;
}

void HttpServer::stop()
{
    http_.stop();
}

std::string HttpServer::consoleUrl() const
{
    // 用 127.0.0.1 而不是 localhost：某些系统会先把 localhost 解析成 IPv6 的 ::1，
    // 而本服务器只监听了 IPv4，浏览器要多绕一次才能连上
    return "http://127.0.0.1:" + std::to_string(http_.port()) + "/";
}

bool HttpServer::openConsole()
{
    const std::string url = consoleUrl();
    bool ok = false;

#ifdef _WIN32
    // ShellExecuteA 返回值 > 32 才算成功（<=32 是错误码）
    HINSTANCE ret = ShellExecuteA(nullptr, "open", url.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    ok = (reinterpret_cast<INT_PTR>(ret) > 32);
#else
    const std::string cmd = "xdg-open \"" + url + "\" >/dev/null 2>&1 &";
    ok = (std::system(cmd.c_str()) == 0);
#endif

    if (ok)
        tools::logger()->info("[Http] 已用默认浏览器打开控制台: {}", url);
    else
        tools::logger()->warn("[Http] 自动打开浏览器失败，请手动访问: {}", url);

    return ok;
}

} // namespace sc::net
