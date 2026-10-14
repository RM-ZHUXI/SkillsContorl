/**
 * @file        http_server.hpp
 * @brief       http服务器请求net头文件
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-13
 * @version     1.0.0
 * @note        第二次封装 http.hpp，提供更上层的 http 服务器接口
 */

#ifndef HTTP_SERVER_HPP
#define HTTP_SERVER_HPP

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <winsock2.h>
    #include <ws2tcpip.h>
    #include <shellapi.h>
#endif

#include <filesystem>
#include <system_error>
#include <string>
#include <nlohmann/json.hpp>
#include <atomic>
#include <mutex>
#include <thread>

#include "../tools/tools.hpp"
#include "net/http.hpp"
#include "../functions/calibrate_camera.hpp"

namespace sc::net
{

class HttpServer
{
public:
    /**
     * @brief       构造函数（内部会一次性注册所有路由）
     * @param       config  配置文件路径
     * @param       memory  内存管理器引用
     * @param       findSkills  技能查找器引用
     * @param       assign  技能链管理器引用
     * @param       runSkillsLists_ 链运行管理器引用
     * @param       calibratecamera 摄像头标定程序
     */
    HttpServer(const std::string& config, tools::Memory& memory,tools::FindSkills &findSkills, tools::Assign &assign,tools::Runskills &runSkillsLists_,functions::CalibrateCamera calibratecamera);
    
    ~HttpServer() = default;

    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    /**
     * @brief       启动服务器（非阻塞）
     * @return      true 启动成功；false 启动失败
     * @note        若配置 http_auto_open 为 true（缺省），启动成功后会自动
     *              用系统默认浏览器打开控制台网页
     */
    bool start();

    /**
     * @brief       停止服务器
     */
    void stop();

    /**
     * @brief 控制台网页地址
     * @return 形如 "http://127.0.0.1:8080/"
     * @note  端口取自 config.yaml 的 http_port；用 127.0.0.1 而不是 localhost，
     *        避免某些系统把 localhost 先解析成 IPv6(::1) 而服务端只监听了 IPv4
     */
    std::string consoleUrl() const;

    /**
     * @brief 用系统默认浏览器打开控制台网页
     * @return true 已成功唤起浏览器
     * @note  Windows 用 ShellExecuteA；其它平台用 xdg-open / open
     */
    bool openConsole();

    /// @brief 配置里是否允许启动后自动打开控制台（http_auto_open，缺省 true）
    bool autoOpenConsole() const { return autoOpen_; }

    /**
     * @brief       对外仍保留注册接口，允许追加路由
     * @param       path     路由路径，如 "/api/xxx"
     * @param       handler  处理函数
     */
    void registerHandler(const std::string& path, HttpHandler handler);

private:
    // ---------------- 公共内存 ----------------

    /**
     * @brief 内置 API：获取内存信息（类型 + 大小）
     */
    void apiGetMemory(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：获取内存信息 + 具体值
     */
    void apiGetMemoryValues(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：把 JSON 键值表写进公共内存
     * @note  body: {"items":{"键":值,...}}，也可以直接给 {"键":值,...}
     */
    void apiSetMemory(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：删除公共内存的键
     * @note  body: {"names":["k1","k2"]} 或 {"prefix":"skill."}
     */
    void apiDelMemory(std::string body, HttpHand& hand);

    // ---------------- 技能 ----------------

    /**
     * @brief 内置 API：获取技能信息
     */
    void apiGetSkills(std::string body, HttpHand& hand);
    
    /**
     * @brief 内置 API：更新技能信息
     */
    void apiUpdateSkills(std::string body, HttpHand& hand);

    // ---------------- 技能链 ----------------

    /**
     * @brief 内置 API：更新技能链
     */
    void apiUpdateSkillChain(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：获取技能链
     */
    void apiGetSkillChain(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：启动技能链
     * @note  body: {"name":"test"} 或 {"all":true}
     */
    void apiStartSkillChain(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：停止技能链
     * @note  body: {"name":"test"} 或 {"all":true}
     */
    void apiStopSkillChain(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：技能链运行状态
     */
    void apiGetRunStatus(std::string body, HttpHand& hand);

    // ---------------- 摄像头标定 ----------------

    /**
     * @brief 内置 API：读取当前标定配置
     */
    void apiGetCalibrateConfig(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：修改标定配置（只更新传入的字段）
     * @note  body 例：{"path":"photos","cols":9,"rows":6,"distance":25,
     *                  "openCamera":false,"matrixName":"K","coeffsName":"D"}
     */
    void apiSetCalibrateConfig(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：异步启动标定
     * @note  起一个线程执行 CalibrateCamera::start()，立即返回；
     *        结果由 GetCalibrateStatus 轮询获取
     */
    void apiStartCalibrate(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：查询标定状态 / 结果
     * @note  返回 { state: idle|running|done|failed, running: bool, result: "..." }
     */
    void apiGetCalibrateStatus(std::string body, HttpHand& hand);

    /**
     * @brief 内置 API：用系统文件管理器打开标定图片路径
     * @note  供前端「打开文件夹」按钮调用；后端调用 explorer / xdg-open
     */
    void apiOpenCalibrateFolder(std::string body, HttpHand& hand);

    // ---------------- 内部工具 ----------------

    /**
     * @brief 统一写回 JSON 响应
     * @param hand   响应封装
     * @param body   响应体
     * @param status HTTP 状态码
     */
    static void replyJson(HttpHand& hand, const nlohmann::json& body, int status = 200);

    /**
     * @brief 解析请求体 JSON
     * @param body 请求体
     * @param out  解析结果
     * @param err  失败原因
     * @return true 解析成功
     * @note  请求体为空时返回一个空对象，方便 GET 复用同一套流程
     */
    static bool parseBody(const std::string& body, nlohmann::json& out, std::string& err);

    /**
     * @brief 集中注册所有业务路由
     */
    void registerAll();

private:
    tools::FindSkills&    findSkills_;           ///< 技能查找器引用
    tools::Memory&        memory_;               ///< 内存管理器引用
    tools::Assign&        assign_;               ///< 技能链管理器引用
    tools::Runskills&     runSkillsLists_;       ///< 技能链线程管理器引用(废弃)
    Http                  http_;                 ///< 底层 http 服务器
    functions::CalibrateCamera calibratecamera_; ///< 摄像头标定程序
    bool                  autoOpen_ = true;      ///< 启动后是否自动打开控制台网页

    // ---- 标定异步状态 ----
    std::atomic<bool> calibRunning_{false};   ///< 标定线程是否在跑（CAS 互斥用）
    std::mutex        calibMutex_;            ///< 保护 calibState_ / calibResult_
    std::string       calibState_ = "idle";   ///< idle / running / done / failed
    std::string       calibResult_;           ///< 标定结束返回的字符串
};

} // namespace sc::net

#endif // HTTP_SERVER_HPP
