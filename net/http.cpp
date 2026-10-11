/**
 * @file        http.cpp
 * @brief       http服务器(多线程)net实现文件
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-13
 * @version     1.0.0
 */

#include "net/http.hpp"

#include <nlohmann/json.hpp>

#include <iostream>
#include <cstring>
#include <cctype>
#include <string>
#include <atomic>
#include <csignal>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <fstream>
#include <sstream>

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #pragma comment(lib, "ws2_32.lib")

    using socklen_t = int;
    #ifndef _SSIZE_T_DEFINED
    typedef long long ssize_t;
    #define _SSIZE_T_DEFINED
    #endif

    #define CLOSE_SOCKET(fd) closesocket(fd)
#else
    #include <sys/socket.h>
    #include <sys/select.h>       // select()/fd_set/timeval（accept 循环的超时等待）
    #include <netinet/in.h>
    #include <arpa/inet.h>
    #include <unistd.h>
    #include <cerrno>
    #define CLOSE_SOCKET(fd) ::close(fd)
#endif

namespace sc::net
{

namespace
{
    std::atomic<bool> g_stopFlag{false};

    void onSignal(int)
    {
        g_stopFlag = true;
    }

    // ---- 静态文件相关辅助 ----

    // 根据扩展名返回 Content-Type
    std::string mimeType(const std::string& path)
    {
        auto endsWith = [&](const char* suffix) {
            size_t n = std::strlen(suffix);
            return path.size() >= n &&
                   path.compare(path.size() - n, n, suffix) == 0;
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
        if (endsWith(".pdf"))  return "application/pdf";
        if (endsWith(".wasm")) return "application/wasm";
        return "application/octet-stream";
    }

    // 去掉 URL 中的查询串 (?xxx) 与片段 (#xxx)
    std::string stripQuery(const std::string& path)
    {
        size_t pos = path.find_first_of("?#");
        return (pos == std::string::npos) ? path : path.substr(0, pos);
    }

    // 防止路径穿越：禁止 ".."，并规范 "/"
    bool isSafePath(const std::string& path)
    {
        if (path.find("..") != std::string::npos) return false;
        if (path.find('\0') != std::string::npos) return false;
        return true;
    }

    /// 一次请求体的上限（JSON 接口用，防止异常请求把内存吃光）
    constexpr std::size_t kMaxRequestSize = 2 * 1024 * 1024;

    /**
     * @brief 从请求头里解析 Content-Length（大小写不敏感）
     * @param head 请求头文本（不含空行）
     * @return body 期望长度；没写 Content-Length 时返回 0
     */
    std::size_t parseContentLength(const std::string& head)
    {
        static const std::string key = "content-length:";

        std::string lower;
        lower.reserve(head.size());
        for (char c : head)
            lower.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));

        std::size_t pos = lower.find(key);
        if (pos == std::string::npos) return 0;

        pos += key.size();
        while (pos < lower.size() && (lower[pos] == ' ' || lower[pos] == '\t')) ++pos;

        std::size_t value = 0;
        while (pos < lower.size() && std::isdigit(static_cast<unsigned char>(lower[pos])))
        {
            value = value * 10 + static_cast<std::size_t>(lower[pos] - '0');
            ++pos;
        }
        return value;
    }
}

// ============================================================
// 构造 / 析构
// ============================================================

Http::Http(const std::string& config)
    : port_(8080)
    , backlog_(128)
    , listenFd_(-1)
    , running_(false)
    , webRoot_("./www")
{
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
    {
        throw std::runtime_error("[Http] WSAStartup failed");
    }
#endif

    auto yaml = tools::load(config);
    if (yaml["http_port"])    port_    = yaml["http_port"].as<int>();
    if (yaml["http_backlog"]) backlog_ = yaml["http_backlog"].as<int>();
    if (yaml["http_webroot"]) webRoot_ = yaml["http_webroot"].as<std::string>();

    // 去掉末尾的 '/'
    while (!webRoot_.empty() && (webRoot_.back() == '/' || webRoot_.back() == '\\'))
    {
        webRoot_.pop_back();
    }

    tools::logger()->info("[Http] config loaded, port={}, backlog={}, webroot={}",
                          port_, backlog_, webRoot_);
}

Http::~Http()
{
    stop();

    // 等待所有客户端线程结束（避免 WSACleanup 时还有线程在跑）。
    // 最多等 5 秒：个别 API 请求可能卡在技能侧（例如 StopSkillChain 在等一个
    // 卡住的技能线程），不应该因此让整个进程退不掉、端口一直占着 —— 那正是历史上
    // "程序结束了 8080 还被占用"的表现之一。
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (activeThreads_.load() > 0 && std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    if (activeThreads_.load() > 0)
    {
        tools::logger()->warn("[Http] 仍有 {} 个客户端线程未结束，放弃等待直接退出"
                              "（多半是某个 API 请求卡在技能侧了）", activeThreads_.load());
    }

#ifdef _WIN32
    WSACleanup();
#endif
}

// ============================================================
// 路由注册 / 查找
// ============================================================

void Http::registerHandler(const std::string& path, HttpHandler handler)
{
    std::lock_guard<std::mutex> lock(handlersMutex_);
    handlers_[path] = std::move(handler);
    tools::logger()->info("[Http] register handler: {}", path);
}

HttpHandler* Http::findHandler(const std::string& path)
{
    std::lock_guard<std::mutex> lock(handlersMutex_);
    auto it = handlers_.find(path);
    return (it == handlers_.end()) ? nullptr : &it->second;
}

// ============================================================
// 启动 / 停止
// ============================================================

bool Http::start()
{
    std::signal(SIGINT,  onSignal);
    std::signal(SIGTERM, onSignal);

    // 防止重复启动
    if (running_.exchange(true))
    {
        tools::logger()->warn("[Http] already running");
        return true;
    }

    g_stopFlag = false;

    // ---- socket ----
    listenFd_ = ::socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ < 0)
    {
        tools::logger()->error("[Http] socket() failed");
        running_ = false;
        return false;
    }

    // ---- SO_REUSEADDR ----
    int opt = 1;
    ::setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&opt), sizeof(opt));

    // ---- bind ----
    sockaddr_in addr{};
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons(port_);

    if (::bind(listenFd_, reinterpret_cast<sockaddr*>(&addr),
               sizeof(addr)) < 0)
    {
        tools::logger()->error("[Http] bind() failed on port {}", port_);
        CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
        running_ = false;
        return false;
    }

    // ---- listen ----
    if (::listen(listenFd_, backlog_) < 0)
    {
        tools::logger()->error("[Http] listen() failed");
        CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
        running_ = false;
        return false;
    }

    tools::logger()->info("[Http] listening on port {}", port_);

    // ---- 开接收线程，主线程立即返回 ----
    acceptThread_ = std::thread(&Http::acceptLoop, this);

    return true;
}

void Http::stop()
{
    // 已经停了，避免重复 join
    if (!running_.exchange(false))
    {
        return;
    }

    g_stopFlag = true;

    tools::logger()->info("[Http] 正在停止: 关闭监听端口 {}", port_);

    // 关闭监听 socket，让阻塞中的 accept()/select() 尽快返回
    if (listenFd_ >= 0)
    {
        // ★ 先 shutdown 再 close：
        //   POSIX 下单纯 close() 不能唤醒阻塞在 accept() 里的线程（fd 关了，
        //   但阻塞中的系统调用仍持有该 socket 的引用），shutdown() 才会让它立刻返回。
        //   accept 循环那边还有 200ms 超时兜底，这里是双保险。
#ifdef _WIN32
        ::shutdown(listenFd_, SD_BOTH);
#else
        ::shutdown(listenFd_, SHUT_RDWR);
#endif
        CLOSE_SOCKET(listenFd_);
        listenFd_ = -1;
    }

    // 等一下接收线程；如果日志停在这一行，说明 accept 线程没能退出（正是历史上
    // "程序关不掉、8080 一直被占"的现象），照着上面两处修复排查即可。
    tools::logger()->info("[Http] 等待 accept 线程退出...");

    if (acceptThread_.joinable())
    {
        acceptThread_.join();
    }

    tools::logger()->info("[Http] stopped");
}

// ============================================================
// 接收线程
// ============================================================

void Http::acceptLoop()
{
    tools::logger()->info("[Http] accept loop started");

    while (!g_stopFlag)
    {
        // ★ 用 select 带超时等待新连接，而不是直接阻塞在 accept() 上。
        //   原因（Linux 上的经典坑）：一个线程阻塞在 accept() 时，另一个线程
        //   close() 同一个监听 fd 并不能唤醒它 —— accept 会一直不返回，
        //   于是 stop() 里的 join 永久卡住：进程退不掉、监听端口一直占着，
        //   下一次启动就报 "bind() failed on port 8080"。
        //   这里最多等 200ms 就回来检查一次 g_stopFlag，任何平台都能及时退出。
        const int fd = listenFd_;
        if (fd < 0) break;                                  // 已经被 stop() 关掉了

        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(fd, &rfds);

        timeval tv;
        tv.tv_sec  = 0;
        tv.tv_usec = 200 * 1000;                            // 200ms

        const int sel = ::select(fd + 1, &rfds, nullptr, nullptr, &tv);
        if (sel == 0)
        {
            continue;                                       // 超时：回去检查停止标志
        }
        if (sel < 0)
        {
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif
            if (g_stopFlag || listenFd_ < 0) break;          // fd 已关闭，正常收尾
            tools::logger()->error("[Http] select() failed");
            continue;
        }

        sockaddr_in cliAddr{};
        socklen_t   cliLen = sizeof(cliAddr);

        int clientFd = ::accept(fd,
                                reinterpret_cast<sockaddr*>(&cliAddr),
                                &cliLen);
        if (clientFd < 0)
        {
            if (g_stopFlag) break;

            // 被信号中断不算错
#ifdef _WIN32
            if (WSAGetLastError() == WSAEINTR) continue;
#else
            if (errno == EINTR) continue;
#endif

            tools::logger()->error("[Http] accept() failed");
            continue;
        }

        // 给客户端设置接收超时，防止 recv 永久阻塞导致线程泄漏
        setRecvTimeout(clientFd, 5000);

        // 开启新线程处理，接收线程立即回去 accept 下一个连接
        activeThreads_.fetch_add(1);
        std::thread(&Http::handleClientThread, this, clientFd).detach();
    }

    tools::logger()->info("[Http] accept loop exited");
}

// ============================================================
// 客户端线程
// ============================================================

void Http::handleClientThread(int clientFd)
{
    try
    {
        handleClient(clientFd);
    }
    catch (const std::exception& e)
    {
        tools::logger()->error("[Http] client thread exception: {}",
                               e.what());
    }
    catch (...)
    {
        tools::logger()->error("[Http] client thread unknown exception");
    }

    activeThreads_.fetch_sub(1);
}

void Http::setRecvTimeout(int fd, int milliseconds)
{
#ifdef _WIN32
    DWORD timeout = static_cast<DWORD>(milliseconds);
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO,
                 reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec  = milliseconds / 1000;
    tv.tv_usec = (milliseconds % 1000) * 1000;
    ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
}

// ============================================================
// 请求处理
// ============================================================

ssize_t Http::sendAll(int fd, const char* data, size_t len)
{
    size_t sent = 0;
    while (sent < len)
    {
        ssize_t n = ::send(fd, data + sent,
                           static_cast<int>(len - sent), 0);
        if (n <= 0)
            return n;
        sent += static_cast<size_t>(n);
    }
    return static_cast<ssize_t>(sent);
}

// ============================================================
// 静态文件服务
// ============================================================

bool Http::serveStatic(const std::string& urlPath, HttpHand& hand)
{
    std::string rel = stripQuery(urlPath);

    // URL 解码（处理 %20 等）
    std::string decoded;
    decoded.reserve(rel.size());
    for (size_t i = 0; i < rel.size(); ++i)
    {
        if (rel[i] == '%' && i + 2 < rel.size())
        {
            auto hex = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return c - 'a' + 10;
                if (c >= 'A' && c <= 'F') return c - 'A' + 10;
                return -1;
            };
            int hi = hex(rel[i + 1]);
            int lo = hex(rel[i + 2]);
            if (hi >= 0 && lo >= 0)
            {
                decoded.push_back(static_cast<char>((hi << 4) | lo));
                i += 2;
                continue;
            }
        }
        decoded.push_back(rel[i]);
    }

    if (!isSafePath(decoded))
    {
        hand.setStatus(403);
        hand.setBody("403 Forbidden");
        return true;
    }

    // 默认首页
    if (decoded.empty() || decoded == "/")
        decoded = "/index.html";

    // 拼接本地路径
    std::string filePath = webRoot_ + decoded;

    // 打开文件（二进制）
    std::ifstream ifs(filePath, std::ios::binary);
    if (!ifs.is_open())
    {
        // 尝试目录下的 index.html
        std::ifstream dirIndex(filePath + "/index.html", std::ios::binary);
        if (dirIndex.is_open())
        {
            std::ostringstream ss;
            ss << dirIndex.rdbuf();
            hand.setStatus(200);
            hand.setHeader("Content-Type", "text/html; charset=utf-8");
            hand.setBody(ss.str());
            return true;
        }

        hand.setStatus(404);
        hand.setBody("404 Not Found: " + decoded);
        return true;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();

    hand.setStatus(200);
    hand.setHeader("Content-Type", mimeType(filePath));
    hand.setBody(ss.str());
    return true;
}

// ============================================================
// 客户端主处理
// ============================================================

void Http::handleClient(int clientFd)
{
    // ---- 读完整请求 ----
    // 注意：JSON 接口的 body 可能被 TCP 拆成多个包，只 recv 一次会丢 body
    //       （表现为 SetMemory 写入 0 项、UpdateSkillChain 收到空数组），
    //       所以这里先收到请求头结束符，再按 Content-Length 把 body 收满。
    std::string raw;
    char tmp[4096];
    std::size_t headerEnd = std::string::npos;
    std::size_t declared  = 0;          ///< Content-Length 声明的 body 长度

    while (raw.size() < kMaxRequestSize)
    {
        ssize_t n = ::recv(clientFd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;

        raw.append(tmp, static_cast<std::size_t>(n));

        if (headerEnd == std::string::npos)
            headerEnd = raw.find("\r\n\r\n");

        if (headerEnd != std::string::npos)
        {
            declared = parseContentLength(raw.substr(0, headerEnd));
            const std::size_t want = headerEnd + 4 + declared;
            if (raw.size() >= want) break;      // body 已收满
        }
    }

    if (raw.empty())
    {
        CLOSE_SOCKET(clientFd);
        return;
    }

    // ---- 请求体超过上限：明确回 413，而不是把截断后的 JSON 拿去解析 ----
    // 不这样处理的话，超长请求会走到 JSON 解析失败分支，
    // 而 nlohmann 的解析错误信息里带 "last read: '<整段输入>'"，
    // 结果响应体会把几 MB 的请求原样回显回去（实测确实会）。
    if (headerEnd != std::string::npos && declared > 0 &&
        (declared + headerEnd + 4) > kMaxRequestSize)
    {
        HttpHand tooLarge;
        tooLarge.setStatus(413);
        tooLarge.setHeader("Content-Type", "application/json; charset=utf-8");
        tooLarge.setBody(nlohmann::json{
            {"status",  "error"},
            {"message", "请求体 " + std::to_string(declared / 1024) +
                        " KB 超过上限 " + std::to_string(kMaxRequestSize / 1024) +
                        " KB：大块数据请让技能直接写公共内存，不要走 HTTP JSON"}
        }.dump());

        const std::string resp = tooLarge.toString();
        sendAll(clientFd, resp.data(), resp.size());
        CLOSE_SOCKET(clientFd);

        tools::logger()->warn("[Http] 请求体过大({} KB)，已拒绝", declared / 1024);
        return;
    }

    // ---- 解析请求行: METHOD PATH VERSION ----
    std::string method, path, version;
    {
        const char* p   = raw.c_str();
        const char* end = p + raw.size();

        auto readToken = [&](std::string& out) {
            while (p < end && (*p == ' ' || *p == '\t')) ++p;
            const char* start = p;
            while (p < end && *p != ' ' && *p != '\r' && *p != '\n') ++p;
            out.assign(start, p);
        };

        readToken(method);
        readToken(path);
        readToken(version);
    }

    //tools::logger()->info("[Http] {} {} {}", method, path, version);

    // ---- 切出 body ----
    std::string body;
    if (headerEnd != std::string::npos && headerEnd + 4 < raw.size())
        body = raw.substr(headerEnd + 4);

    HttpHand hand;

    // ---- 判断是否为 /api/ 接口 ----
    // 非 "/api/" 开头的一律走静态文件
    const bool isApi = (path.compare(0, 5, "/api/") == 0);

    if (isApi)
    {
        // 路由匹配前先剥掉 ?查询串 和 #片段，方便前端带时间戳防缓存（如 ?_=123）
        HttpHandler* handler = findHandler(stripQuery(path));
        if (handler && *handler)
        {
            try
            {
                (*handler)(body, hand);
            }
            catch (const std::exception& e)
            {
                hand.setStatus(500);
                hand.setBody(std::string("Handler exception: ") + e.what());
                tools::logger()->error("[Http] handler exception: {}", e.what());
            }
            catch (...)
            {
                hand.setStatus(500);
                hand.setBody("Unknown handler exception");
                tools::logger()->error("[Http] unknown handler exception");
            }
        }
        else
        {
            hand.setStatus(404);
            hand.setBody("404 Not Found: " + path);
        }
    }
    else
    {
        // 静态文件
        serveStatic(path, hand);
    }

    // ---- 发送响应 ----
    std::string response = hand.toString();
    sendAll(clientFd, response.data(), response.size());

    // ---- 关闭连接 ----
    CLOSE_SOCKET(clientFd);
}

} // namespace sc::net