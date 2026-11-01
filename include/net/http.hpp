/**
 * @file        http.hpp
 * @brief       http服务器(多线程)net头文件
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-13
 * @version     1.0.0
 */

#ifndef HTTP_HPP
#define HTTP_HPP

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
#endif

#include <string>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <mutex>
#include <thread>
#include <atomic>

#include "tools/yaml.hpp"
#include "tools/logger.hpp"

namespace sc::net
{

#if defined(_WIN32)
    #define API __declspec(dllexport)
#else
    #define API __attribute__((visibility("default")))
#endif

    
/**
 * @brief HTTP 响应封装，handler 通过它写回响应
 */
class HttpHand
{
public:
    HttpHand() : statusCode_(200) {}

    /**
     * @brief 设置 HTTP 状态码
     */
    void setStatus(int code) { statusCode_ = code; }

    /**
     * @brief 设置响应头（覆盖同名头）
     */
    void setHeader(const std::string& key, const std::string& value)
    {
        headers_[key] = value;
    }

    /**
     * @brief 设置响应体
     */
    void setBody(const std::string& body) { body_ = body; }

    int getStatus() const { return statusCode_; }

    const std::string& getBody() const { return body_; }

    const std::unordered_map<std::string, std::string>& getHeaders() const
    {
        return headers_;
    }

    /**
     * @brief 序列化成 HTTP 响应报文
     */
    std::string toString() const
    {
        std::string statusText;
        switch (statusCode_)
        {
            case 200: statusText = "200 OK"; break;
            case 201: statusText = "201 Created"; break;
            case 204: statusText = "204 No Content"; break;
            case 400: statusText = "400 Bad Request"; break;
            case 404: statusText = "404 Not Found"; break;
            case 413: statusText = "413 Payload Too Large"; break;
            case 500: statusText = "500 Internal Server Error"; break;
            default:  statusText = std::to_string(statusCode_); break;
        }

        std::string resp = "HTTP/1.1 " + statusText + "\r\n";

        if (headers_.find("Content-Type") == headers_.end())
            resp += "Content-Type: text/plain; charset=utf-8\r\n";

        // 一律禁用缓存：网页是本地控制台，接口数据也是实时状态。
        // 不禁缓存的话浏览器会拿旧的 index.html 直接渲染，出现
        // "代码已经改了但界面上还是老文案/老按钮" 的假象。
        if (headers_.find("Cache-Control") == headers_.end())
            resp += "Cache-Control: no-store, no-cache, must-revalidate\r\n";

        if (headers_.find("Pragma") == headers_.end())
            resp += "Pragma: no-cache\r\n";

        if (headers_.find("Expires") == headers_.end())
            resp += "Expires: 0\r\n";

        for (const auto& kv : headers_)
            resp += kv.first + ": " + kv.second + "\r\n";

        resp += "Content-Length: " + std::to_string(body_.size()) + "\r\n";
        resp += "Connection: close\r\n";
        resp += "\r\n";
        resp += body_;

        return resp;
    }

private:
    int statusCode_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

/**
 * @brief 路由处理函数类型
 * @param body     请求体
 * @param httphand 响应封装，handler 通过它设置状态码/头/体
 */
using HttpHandler = std::function<void(std::string body, HttpHand& httphand)>;

class API Http
{
public:
    /**
     * @brief       构造函数
     * @param       config  yaml 配置文件路径
     */
    explicit Http(const std::string& config);

    ~Http();

    Http(const Http&) = delete;
    Http& operator=(const Http&) = delete;

    /**
     * @brief       注册路由
     * @param       path     路由路径，如 "/api/xxx"
     * @param       handler  处理函数
     */
    void registerHandler(const std::string& path, HttpHandler handler);

    /**
     * @brief       启动服务器（非阻塞）
     * @note        内部会开启接收线程，主线程立即返回
     * @return      true 启动成功；false 启动失败
     */
    bool start();

    /**
     * @brief       停止服务器
     * @note        关闭监听 socket 并等待接收线程退出
     */
    void stop();

    /**
     * @brief       当前监听端口
     * @return      uint16_t 端口号（构造时从 config 的 http_port 读取）
     */
    uint16_t port() const { return port_; }

private:
    /**
     * @brief 接收线程主循环：accept 连接并派发给客户端线程
     */
    void acceptLoop();

    /**
     * @brief 处理单个客户端连接（运行在独立线程中）
     */
    void handleClient(int clientFd);

    /**
     * @brief 客户端线程入口：包裹 handleClient，负责计数与异常隔离
     */
    void handleClientThread(int clientFd);

    /**
     * @brief 设置 socket 接收超时（毫秒）
     */
    static void setRecvTimeout(int fd, int milliseconds);

    /**
     * @brief 循环发送直到全部写出
     */
    ssize_t sendAll(int fd, const char* data, size_t len);

    /**
     * @brief 根据路径查找 handler
     */
    HttpHandler* findHandler(const std::string& path);

    /**
     * @brief 静态文件处理
     */
    bool serveStatic(const std::string& urlPath, HttpHand& hand);
private:
    uint16_t          port_;          ///< 监听端口
    int               backlog_;       ///< listen 队列长度
    int               listenFd_;      ///< 监听 socket
    std::atomic<bool> running_;       ///< 运行标志
    std::string       webRoot_;       ///< 网页根目录

    std::unordered_map<std::string, HttpHandler> handlers_;
    std::mutex                                   handlersMutex_;

    std::atomic<int>  activeThreads_{0};   ///< 活跃客户端线程数
    std::thread       acceptThread_;       ///< 接收线程
};

} // namespace sc::net

#endif // HTTP_HPP