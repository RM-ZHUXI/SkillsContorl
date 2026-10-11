/**
 * @file        memory.hpp
 * @brief       内存工具头文件（pimpl + 全部托管给 libtools.dll）
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-09
 * @version     3.0.0
 *
 * @note
 *   - pimpl：Memory 只含 unique_ptr<Impl>，布局稳定，跨 DLL 安全
 *   - 所有模板（insert<T> / get<T> / insertOrAssign<T> / registerType<T>）
 *     的定义都放在 memory.cpp，编译进 libtools.dll。
 *     调用方只看到声明，链接到 libtools.dll 里的唯一实例。
 *     → std::any 永远在 libtools.dll 里构造，_Manager 永远指向 libtools.dll，
 *       技能 DLL 卸载后 memory 里的数据仍然安全。
 *   - 新增类型时在 memory.cpp 里加一行 TOOLS_INSTANTIATE_IO(TYPE)
 */

#ifndef MEMORY_HPP
#define MEMORY_HPP

#include <string>
#include <vector>
#include <memory>
#include <any>
#include <functional>
#include <typeinfo>
#include <type_traits>
#include <cstddef>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "tools/tools_api.hpp"

namespace sc::tools
{

class TOOLS_API Memory
{
public:
    // ============================================================
    // 自定义类型的类型信息注册
    // ============================================================
    struct TypeInfo
    {
        std::function<std::string(const std::any&)> typeName;
        std::function<size_t(const std::any&)>      size;
    };

    /**
     * @brief 注册类型信息（实现在 libtools.dll）
     * @note  模板定义在 .cpp 里，这里只有声明。
     *        需要新类型时在 memory.cpp 的显式实例化列表里补一行。
     */
    template<typename T>
    static void registerType(
        std::function<std::string(const std::any&)> typeName = nullptr,
        std::function<size_t(const std::any&)>      sizeFn   = nullptr);

    /**
     * @brief 注册所有内置常用类型（进程内只跑一次）
     */
    static void registerTypeAll();

    // pimpl 前向声明（放 public 只是为了让辅助函数能引用，实现细节仍在 .cpp）
    struct Impl;

    static std::string typeNameOf(const std::type_info& t);

    // 构造 / 析构
    Memory();
    ~Memory();
    Memory(const Memory&)            = delete;
    Memory& operator=(const Memory&) = delete;

    /**
     * @brief 字符串写入
     * @note  二进制特殊插入
     */
    void insert(const std::string& name, const std::string& value);
    void insert(const std::string& name, const char*        value);
    void insert(const std::string& name, char*              value);
    template<std::size_t N>
    void insert(const std::string& name, const char (&value)[N])
    {
        insert(name, std::string(value));
    }

    // ============================================================
    // 通用写入：模板只声明，定义在 libtools.dll
    // ============================================================
    template<typename T>
    void insert(const std::string& name, const T& value);

    /**
     * @brief 插入并且注册公共变量
     * @note  如果没有公共变量就创建新变量，否则会清零重新覆盖
     *        不会改变类型
     */
    template<typename T>
    void insertOrAssign(const std::string& name, const T& value);

    template<typename T>
    bool get(const std::string& name, T& out, std::string* why = nullptr) const;

    template<typename T>
    T get(const std::string& name) const;


    void insertString(const std::string& name, const std::string& value);

    /**
     * @brief 向内存池插入任意类型的值
     * @param name  键名，全局唯一
     * @param value 要存储的值，按值传入
     * @throws std::runtime_error 当 name 已存在时抛出
     */
    void insertAny(const std::string& name, std::any value);

    bool getAny(const std::string& name, std::any& out) const;
    bool getAny(const std::string& name, std::any& out, std::string* why) const;

    void reportTypeMismatch(const std::string& name,
                            const std::string& actual,
                            const std::string& expect) const;


    bool        erase(const std::string& name);
    std::size_t erasePrefix(const std::string& prefix);
    bool        exist(const std::string& name) const;
    std::string getTypeName(const std::string& name) const;
    std::size_t getSize(const std::string& name) const;
    std::size_t size() const;
    std::vector<std::string> keys() const;
    void        clear();

    nlohmann::json toJsonObject() const;
    std::string    toJson(int indent = 4) const;

private:
    static void registerTypeImpl(const std::type_info& t, TypeInfo info);

    std::unique_ptr<Impl> impl_;
};

}  // namespace tools

#endif // MEMORY_HPP