/**
 * @file        memory.cpp
 * @brief       Memory 实现（唯一一份，编译进 libtools.dll）
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-09
 * @version     3.0.0
 *
 * @note
 *   - 所有模板的定义都在这里，末尾显式实例化。
 *   - registerTypeAll 用变参宏展开，避免类型里的逗号被当宏分隔符。
 */

#include "tools/memory.hpp"
#include "tools/logger.hpp"

// ---- 标准库 ----
#include <map>
#include <vector>
#include <deque>
#include <list>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <array>
#include <utility>
#include <tuple>
#include <complex>
#include <string>
#include <mutex>
#include <typeindex>
#include <cstring>
#include <stdexcept>

// ---- OpenCV ----
#include <opencv2/opencv.hpp>

namespace sc::tools
{

// ============================================================
// pimpl
// ============================================================
struct Memory::Impl
{
    mutable std::mutex              mtx_;
    std::map<std::string, std::any> content;
};

// ============================================================
// 类型注册表
// ============================================================
namespace {

std::unordered_map<std::type_index, Memory::TypeInfo>& registry()
{
    static std::unordered_map<std::type_index, Memory::TypeInfo> r;
    return r;
}

std::mutex& registryMutex()
{
    static std::mutex m;
    return m;
}

bool findTypeInfo(const std::type_info& t, Memory::TypeInfo& out)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    auto it = registry().find(std::type_index(t));
    if (it == registry().end()) return false;
    out = it->second;   // 拷贝，避免解锁后悬空
    return true;
}

std::string builtinTypeName(const std::type_info& t)
{
    if (t == typeid(bool))               return "bool";
    if (t == typeid(char))               return "char";
    if (t == typeid(signed char))        return "signed char";
    if (t == typeid(unsigned char))      return "unsigned char";
    if (t == typeid(short))              return "short";
    if (t == typeid(unsigned short))     return "unsigned short";
    if (t == typeid(int))                return "int";
    if (t == typeid(unsigned int))       return "unsigned int";
    if (t == typeid(long))               return "long";
    if (t == typeid(unsigned long))      return "unsigned long";
    if (t == typeid(long long))          return "long long";
    if (t == typeid(unsigned long long)) return "unsigned long long";
    if (t == typeid(float))              return "float";
    if (t == typeid(double))             return "double";
    if (t == typeid(long double))        return "long double";
    if (t == typeid(wchar_t))            return "wchar_t";
    if (t == typeid(char*))              return "char*";
    if (t == typeid(const char*))        return "const char*";
    if (t == typeid(std::string))        return "string";
    return "";
}

size_t builtinTypeSize(const std::any& a)
{
    const std::type_info& t = a.type();

    if (t == typeid(bool))               return sizeof(bool);
    if (t == typeid(char))               return sizeof(char);
    if (t == typeid(signed char))        return sizeof(signed char);
    if (t == typeid(unsigned char))      return sizeof(unsigned char);
    if (t == typeid(short))              return sizeof(short);
    if (t == typeid(unsigned short))     return sizeof(unsigned short);
    if (t == typeid(int))                return sizeof(int);
    if (t == typeid(unsigned int))       return sizeof(unsigned int);
    if (t == typeid(long))               return sizeof(long);
    if (t == typeid(unsigned long))      return sizeof(unsigned long);
    if (t == typeid(long long))          return sizeof(long long);
    if (t == typeid(unsigned long long)) return sizeof(unsigned long long);
    if (t == typeid(float))              return sizeof(float);
    if (t == typeid(double))             return sizeof(double);
    if (t == typeid(long double))        return sizeof(long double);
    if (t == typeid(wchar_t))            return sizeof(wchar_t);

    if (t == typeid(std::string))
        return std::any_cast<const std::string&>(a).size();

    if (t == typeid(const char*))
    {
        const char* s = std::any_cast<const char*>(a);
        return s ? std::strlen(s) : 0;
    }

    if (t == typeid(char*))
    {
        const char* s = std::any_cast<char*>(a);
        return s ? std::strlen(s) : 0;
    }

    return 0;   // 未知类型，交给注册表
}

} // anonymous namespace

// ============================================================
// 模板定义（只编译这一份）
// ============================================================

template<typename T>
void Memory::registerType(
    std::function<std::string(const std::any&)> typeName,
    std::function<size_t(const std::any&)>      sizeFn)
{
    TypeInfo info;
    info.typeName = typeName ? std::move(typeName)
                             : [](const std::any&) { return std::string(typeid(T).name()); };
    info.size     = sizeFn ? std::move(sizeFn)
                           : [](const std::any&) -> size_t { return sizeof(T); };
    registerTypeImpl(typeid(T), std::move(info));
}

template<typename T>
void Memory::insert(const std::string& name, const T& value)
{
    insertAny(name, std::any(value));  
}

template<typename T>
void Memory::insertOrAssign(const std::string& name, const T& value)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    impl_->content.insert_or_assign(name, std::any(value));
}

template<typename T>
bool Memory::get(const std::string& name, T& out, std::string* why) const
{
    std::any a;
    if (!getAny(name, a, why)) return false;

    if (const T* p = std::any_cast<T>(&a))
    {
        out = *p;
        return true;
    }

    if constexpr (std::is_same_v<T, std::string>)
    {
        if (const char* const* p1 = std::any_cast<const char*>(&a))
        {
            if (*p1) { out = *p1; return true; }
        }
        if (char* const* p2 = std::any_cast<char*>(&a))
        {
            if (*p2) { out = *p2; return true; }
        }
    }

    const std::string actual = typeNameOf(a.type());
    const std::string expect = typeNameOf(typeid(T));
    if (why)
        *why = "[Memory] 类型不匹配: name=" + name +
               ", 实际类型=" + actual + ", 期望类型=" + expect;
    reportTypeMismatch(name, actual, expect);
    return false;
}

template<typename T>
T Memory::get(const std::string& name) const
{
    T out{};
    std::string why;
    if (!get<T>(name, out, &why))
        throw std::runtime_error("[Memory] 获取失败: " + why);
    return out;
}

// ============================================================
// ============================================================
//                   registerTypeAll
// ============================================================
// ============================================================
void Memory::registerTypeAll()
{
    static std::once_flag once;
    std::call_once(once, []()
    {
        // ------------------------------------------------------------
        // 变参宏：类型里含逗号也不会被切分
        // 参数顺序：NAME 在前（字符串），类型放变参
        // ------------------------------------------------------------
        #define REG_SIMPLE(NAME, ...)                                           \
            Memory::registerType<__VA_ARGS__>(                                  \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any&) { return sizeof(__VA_ARGS__); })

        #define REG_VEC(NAME, ...)                                              \
            Memory::registerType<std::vector<__VA_ARGS__>>(                     \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::vector<__VA_ARGS__>&>(a)    \
                           .size() * sizeof(__VA_ARGS__);                       \
                })

        #define REG_DEQUE(NAME, ...)                                            \
            Memory::registerType<std::deque<__VA_ARGS__>>(                      \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::deque<__VA_ARGS__>&>(a)     \
                           .size() * sizeof(__VA_ARGS__);                       \
                })

        #define REG_LIST(NAME, ...)                                             \
            Memory::registerType<std::list<__VA_ARGS__>>(                       \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::list<__VA_ARGS__>&>(a)      \
                           .size() * sizeof(__VA_ARGS__);                       \
                })

        #define REG_SET(NAME, ...)                                              \
            Memory::registerType<std::set<__VA_ARGS__>>(                        \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::set<__VA_ARGS__>&>(a)       \
                           .size() * sizeof(__VA_ARGS__);                       \
                })

        #define REG_USET(NAME, ...)                                             \
            Memory::registerType<std::unordered_set<__VA_ARGS__>>(              \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::unordered_set<__VA_ARGS__>&>(a) \
                           .size() * sizeof(__VA_ARGS__);                       \
                })

        #define REG_MAP_NUM(NAME, K, V)                                         \
            Memory::registerType<std::map<K, V>>(                               \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::map<K, V>&>(a).size()       \
                           * (sizeof(K) + sizeof(V));                           \
                })

        #define REG_UMAP_NUM(NAME, K, V)                                        \
            Memory::registerType<std::unordered_map<K, V>>(                     \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any& a) {                                         \
                    return std::any_cast<const std::unordered_map<K, V>&>(a)    \
                           .size() * (sizeof(K) + sizeof(V));                   \
                })

        #define REG_ARRAY(NAME, T, N)                                           \
            Memory::registerType<std::array<T, N>>(                             \
                [](const std::any&) { return std::string(NAME); },              \
                [](const std::any&) { return sizeof(T) * (N); })

        // ---------- vector：数值类型 ----------
        REG_VEC("vector<int>",                int);
        REG_VEC("vector<unsigned int>",       unsigned int);
        REG_VEC("vector<long long>",          long long);
        REG_VEC("vector<unsigned long long>", unsigned long long);
        REG_VEC("vector<float>",              float);
        REG_VEC("vector<double>",             double);
        REG_VEC("vector<char>",               char);
        REG_VEC("vector<signed char>",        signed char);
        REG_VEC("vector<unsigned char>",      unsigned char);
        REG_VEC("vector<short>",              short);
        REG_VEC("vector<unsigned short>",     unsigned short);

        Memory::registerType<std::vector<std::string>>(
            [](const std::any&) { return std::string("vector<string>"); },
            [](const std::any& a) {
                const auto& v = std::any_cast<const std::vector<std::string>&>(a);
                size_t total = 0;
                for (const auto& s : v) total += s.size();
                return total;
            });

        Memory::registerType<std::vector<bool>>(
            [](const std::any&) { return std::string("vector<bool>"); },
            [](const std::any& a) {
                return (std::any_cast<const std::vector<bool>&>(a).size() + 7) / 8;
            });

        Memory::registerType<std::vector<std::vector<int>>>(
            [](const std::any&) { return std::string("vector<vector<int>>"); },
            [](const std::any& a) {
                const auto& vv = std::any_cast<const std::vector<std::vector<int>>&>(a);
                size_t total = 0;
                for (const auto& v : vv) total += v.size() * sizeof(int);
                return total;
            });

        Memory::registerType<std::vector<std::vector<double>>>(
            [](const std::any&) { return std::string("vector<vector<double>>"); },
            [](const std::any& a) {
                const auto& vv = std::any_cast<const std::vector<std::vector<double>>&>(a);
                size_t total = 0;
                for (const auto& v : vv) total += v.size() * sizeof(double);
                return total;
            });

        // ---------- deque ----------
        REG_DEQUE("deque<int>",    int);
        REG_DEQUE("deque<double>", double);
        REG_DEQUE("deque<float>",  float);

        // ---------- list ----------
        REG_LIST("list<int>",    int);
        REG_LIST("list<double>", double);
        REG_LIST("list<float>",  float);

        // ---------- set ----------
        REG_SET("set<int>",    int);
        REG_SET("set<double>", double);

        Memory::registerType<std::set<std::string>>(
            [](const std::any&) { return std::string("set<string>"); },
            [](const std::any& a) {
                const auto& s = std::any_cast<const std::set<std::string>&>(a);
                size_t total = 0;
                for (const auto& e : s) total += e.size();
                return total;
            });

        // ---------- unordered_set ----------
        REG_USET("unordered_set<int>", int);

        // ---------- map<string, V> ----------
        Memory::registerType<std::map<std::string, int>>(
            [](const std::any&) { return std::string("map<string,int>"); },
            [](const std::any& a) {
                const auto& m = std::any_cast<const std::map<std::string, int>&>(a);
                size_t total = 0;
                for (const auto& kv : m) total += kv.first.size() + sizeof(int);
                return total;
            });

        Memory::registerType<std::map<std::string, double>>(
            [](const std::any&) { return std::string("map<string,double>"); },
            [](const std::any& a) {
                const auto& m = std::any_cast<const std::map<std::string, double>&>(a);
                size_t total = 0;
                for (const auto& kv : m) total += kv.first.size() + sizeof(double);
                return total;
            });

        Memory::registerType<std::map<std::string, std::string>>(
            [](const std::any&) { return std::string("map<string,string>"); },
            [](const std::any& a) {
                const auto& m = std::any_cast<const std::map<std::string, std::string>&>(a);
                size_t total = 0;
                for (const auto& kv : m) total += kv.first.size() + kv.second.size();
                return total;
            });

        REG_MAP_NUM("map<int,int>",    int, int);
        REG_MAP_NUM("map<int,double>", int, double);

        Memory::registerType<std::unordered_map<std::string, int>>(
            [](const std::any&) { return std::string("unordered_map<string,int>"); },
            [](const std::any& a) {
                const auto& m = std::any_cast<const std::unordered_map<std::string, int>&>(a);
                size_t total = 0;
                for (const auto& kv : m) total += kv.first.size() + sizeof(int);
                return total;
            });

        REG_UMAP_NUM("unordered_map<int,int>",    int, int);
        REG_UMAP_NUM("unordered_map<int,double>", int, double);

        // ---------- array ----------
        REG_ARRAY("array<int,2>",    int,    2);
        REG_ARRAY("array<int,3>",    int,    3);
        REG_ARRAY("array<int,4>",    int,    4);
        REG_ARRAY("array<float,2>",  float,  2);
        REG_ARRAY("array<float,3>",  float,  3);
        REG_ARRAY("array<float,4>",  float,  4);
        REG_ARRAY("array<double,3>", double, 3);
        REG_ARRAY("array<double,4>", double, 4);

        // ---------- pair ----------
        REG_SIMPLE("pair<int,int>",       std::pair<int, int>);
        REG_SIMPLE("pair<int,double>",    std::pair<int, double>);
        REG_SIMPLE("pair<double,double>", std::pair<double, double>);
        REG_SIMPLE("pair<float,float>",   std::pair<float, float>);

        Memory::registerType<std::pair<std::string, int>>(
            [](const std::any&) { return std::string("pair<string,int>"); },
            [](const std::any& a) {
                const auto& p = std::any_cast<const std::pair<std::string, int>&>(a);
                return p.first.size() + sizeof(int);
            });

        Memory::registerType<std::pair<std::string, std::string>>(
            [](const std::any&) { return std::string("pair<string,string>"); },
            [](const std::any& a) {
                const auto& p = std::any_cast<const std::pair<std::string, std::string>&>(a);
                return p.first.size() + p.second.size();
            });

        // ---------- tuple ----------
        REG_SIMPLE("tuple<int,int>",              std::tuple<int, int>);
        REG_SIMPLE("tuple<double,double>",        std::tuple<double, double>);
        REG_SIMPLE("tuple<int,int,int>",          std::tuple<int, int, int>);
        REG_SIMPLE("tuple<double,double,double>", std::tuple<double, double, double>);

        // ---------- complex ----------
        REG_SIMPLE("complex<float>",  std::complex<float>);
        REG_SIMPLE("complex<double>", std::complex<double>);

        // ============================================================
        //                       OpenCV
        // ============================================================
        Memory::registerType<cv::Mat>(
            [](const std::any&) { return std::string("cv::Mat"); },
            [](const std::any& a) {
                const cv::Mat& m = std::any_cast<const cv::Mat&>(a);
                return m.total() * m.elemSize();
            });

        REG_SIMPLE("cv::Point",   cv::Point);
        REG_SIMPLE("cv::Point2f", cv::Point2f);
        REG_SIMPLE("cv::Point2d", cv::Point2d);
        REG_SIMPLE("cv::Point3f", cv::Point3f);
        REG_SIMPLE("cv::Point3d", cv::Point3d);
        REG_SIMPLE("cv::Point3i", cv::Point3i);

        REG_SIMPLE("cv::Size",   cv::Size);
        REG_SIMPLE("cv::Size2f", cv::Size2f);
        REG_SIMPLE("cv::Size2d", cv::Size2d);

        REG_SIMPLE("cv::Rect",   cv::Rect);
        REG_SIMPLE("cv::Rect2f", cv::Rect2f);
        REG_SIMPLE("cv::Rect2d", cv::Rect2d);

        REG_SIMPLE("cv::Scalar",      cv::Scalar);
        REG_SIMPLE("cv::RotatedRect", cv::RotatedRect);

        REG_SIMPLE("cv::Vec2b", cv::Vec2b);
        REG_SIMPLE("cv::Vec3b", cv::Vec3b);
        REG_SIMPLE("cv::Vec4b", cv::Vec4b);
        REG_SIMPLE("cv::Vec2f", cv::Vec2f);
        REG_SIMPLE("cv::Vec3f", cv::Vec3f);
        REG_SIMPLE("cv::Vec4f", cv::Vec4f);
        REG_SIMPLE("cv::Vec2d", cv::Vec2d);
        REG_SIMPLE("cv::Vec3d", cv::Vec3d);
        REG_SIMPLE("cv::Vec4d", cv::Vec4d);

        REG_SIMPLE("cv::Matx22f", cv::Matx22f);
        REG_SIMPLE("cv::Matx33f", cv::Matx33f);
        REG_SIMPLE("cv::Matx44f", cv::Matx44f);
        REG_SIMPLE("cv::Matx22d", cv::Matx22d);
        REG_SIMPLE("cv::Matx33d", cv::Matx33d);
        REG_SIMPLE("cv::Matx44d", cv::Matx44d);

        REG_SIMPLE("cv::KeyPoint", cv::KeyPoint);
        REG_SIMPLE("cv::DMatch",   cv::DMatch);

        REG_VEC("vector<cv::Point>",       cv::Point);
        REG_VEC("vector<cv::Point2f>",     cv::Point2f);
        REG_VEC("vector<cv::Point2d>",     cv::Point2d);
        REG_VEC("vector<cv::Point3f>",     cv::Point3f);
        REG_VEC("vector<cv::Point3d>",     cv::Point3d);
        REG_VEC("vector<cv::Rect>",        cv::Rect);
        REG_VEC("vector<cv::Rect2f>",      cv::Rect2f);
        REG_VEC("vector<cv::Size>",        cv::Size);
        REG_VEC("vector<cv::Vec2f>",       cv::Vec2f);
        REG_VEC("vector<cv::Vec3b>",       cv::Vec3b);
        REG_VEC("vector<cv::Vec4f>",       cv::Vec4f);
        REG_VEC("vector<cv::Scalar>",      cv::Scalar);
        REG_VEC("vector<cv::KeyPoint>",    cv::KeyPoint);
        REG_VEC("vector<cv::DMatch>",      cv::DMatch);
        REG_VEC("vector<cv::RotatedRect>", cv::RotatedRect);

        Memory::registerType<std::vector<cv::Mat>>(
            [](const std::any&) { return std::string("vector<cv::Mat>"); },
            [](const std::any& a) {
                const auto& v = std::any_cast<const std::vector<cv::Mat>&>(a);
                size_t total = 0;
                for (const auto& m : v) total += m.total() * m.elemSize();
                return total;
            });

        Memory::registerType<std::vector<std::vector<cv::Point>>>(
            [](const std::any&) { return std::string("vector<vector<cv::Point>>"); },
            [](const std::any& a) {
                const auto& vv =
                    std::any_cast<const std::vector<std::vector<cv::Point>>&>(a);
                size_t total = 0;
                for (const auto& v : vv) total += v.size() * sizeof(cv::Point);
                return total;
            });

        Memory::registerType<std::vector<std::vector<cv::Point2f>>>(
            [](const std::any&) { return std::string("vector<vector<cv::Point2f>>"); },
            [](const std::any& a) {
                const auto& vv =
                    std::any_cast<const std::vector<std::vector<cv::Point2f>>&>(a);
                size_t total = 0;
                for (const auto& v : vv) total += v.size() * sizeof(cv::Point2f);
                return total;
            });

        Memory::registerType<std::map<std::string, cv::Mat>>(
            [](const std::any&) { return std::string("map<string,cv::Mat>"); },
            [](const std::any& a) {
                const auto& m = std::any_cast<const std::map<std::string, cv::Mat>&>(a);
                size_t total = 0;
                for (const auto& kv : m)
                    total += kv.first.size() + kv.second.total() * kv.second.elemSize();
                return total;
            });

        #undef REG_SIMPLE
        #undef REG_VEC
        #undef REG_DEQUE
        #undef REG_LIST
        #undef REG_SET
        #undef REG_USET
        #undef REG_MAP_NUM
        #undef REG_UMAP_NUM
        #undef REG_ARRAY
    });
}

// ============================================================
// 类型注册底层
// ============================================================
void Memory::registerTypeImpl(const std::type_info& t, TypeInfo info)
{
    std::lock_guard<std::mutex> lock(registryMutex());
    registry()[std::type_index(t)] = std::move(info);
}

std::string Memory::typeNameOf(const std::type_info& t)
{
    std::string name = builtinTypeName(t);
    if (!name.empty()) return name;

    TypeInfo info;
    if (findTypeInfo(t, info) && info.typeName)
    {
        try { return info.typeName(std::any{}); }
        catch (...) { /* 落到下面 */ }
    }

    return t.name();
}

namespace {

std::string anyTypeName(const std::any& a)
{
    const std::string builtin = builtinTypeName(a.type());
    if (!builtin.empty()) return builtin;

    Memory::TypeInfo info;
    if (findTypeInfo(a.type(), info) && info.typeName)
    {
        try { return info.typeName(a); }
        catch (...) { /* 落到下面 */ }
    }

    return a.type().name();
}

size_t anySize(const std::any& a)
{
    size_t n = builtinTypeSize(a);
    if (n != 0) return n;

    Memory::TypeInfo info;
    if (findTypeInfo(a.type(), info) && info.size)
        return info.size(a);

    return 0;
}

} // anonymous namespace

// ============================================================
// 构造 / 析构
// ============================================================
Memory::Memory()  : impl_(std::make_unique<Impl>())
{
    registerTypeAll();
}

Memory::~Memory() = default;

void Memory::insertAny(const std::string& name, std::any value)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    if (impl_->content.find(name) != impl_->content.end())
        throw std::runtime_error("[Memory] 内存申请失败:名称已存在 -> " + name);
    impl_->content.emplace(name, std::move(value));
}

void Memory::insertString(const std::string& name, const std::string& value)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    if (impl_->content.find(name) != impl_->content.end())
        throw std::runtime_error("[Memory] 内存申请失败:名称已存在 -> " + name);
    impl_->content.emplace(name, std::any(value));
}

void Memory::insert(const std::string& name, const std::string& value)
{
    insertString(name, value);
}

void Memory::insert(const std::string& name, const char* value)
{
    insertString(name, value ? std::string(value) : std::string());
}

void Memory::insert(const std::string& name, char* value)
{
    insertString(name, value ? std::string(value) : std::string());
}

bool Memory::getAny(const std::string& name, std::any& out) const
{
    return getAny(name, out, nullptr);
}

bool Memory::getAny(const std::string& name, std::any& out, std::string* why) const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    auto it = impl_->content.find(name);
    if (it == impl_->content.end())
    {
        if (why)
            *why = "[Memory] 不存在: name=" + name +
                   " (当前共 " + std::to_string(impl_->content.size()) + " 项)";
        logger()->debug("[Memory] 取值失败: 名称不存在 -> {}", name);
        return false;
    }

    out = it->second;
    return true;
}

void Memory::reportTypeMismatch(const std::string& name,
                                const std::string& actual,
                                const std::string& expect) const
{
    logger()->warn("[Memory] 取值失败: 类型不匹配 -> name={}, 实际={}, 期望={}",
                   name, actual, expect);
}

// ============================================================
// 其它
// ============================================================
bool Memory::erase(const std::string& name)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    return impl_->content.erase(name) == 1;
}

std::size_t Memory::erasePrefix(const std::string& prefix)
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    std::size_t n = 0;
    for (auto it = impl_->content.begin(); it != impl_->content.end(); )
    {
        if (it->first.compare(0, prefix.size(), prefix) == 0)
        {
            it = impl_->content.erase(it);
            ++n;
        }
        else
        {
            ++it;
        }
    }
    return n;
}

bool Memory::exist(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    return impl_->content.find(name) != impl_->content.end();
}

std::string Memory::getTypeName(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    auto it = impl_->content.find(name);
    if (it == impl_->content.end()) return "";
    return anyTypeName(it->second);
}

std::size_t Memory::getSize(const std::string& name) const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    auto it = impl_->content.find(name);
    if (it == impl_->content.end()) return 0;
    return anySize(it->second);
}

std::size_t Memory::size() const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    return impl_->content.size();
}

std::vector<std::string> Memory::keys() const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    std::vector<std::string> out;
    out.reserve(impl_->content.size());
    for (const auto& kv : impl_->content)
        out.push_back(kv.first);
    return out;
}

void Memory::clear()
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    impl_->content.clear();
}

nlohmann::json Memory::toJsonObject() const
{
    std::lock_guard<std::mutex> lk(impl_->mtx_);
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& kv : impl_->content)
    {
        arr.push_back({
            {"name", kv.first},
            {"type", anyTypeName(kv.second)},
            {"size", anySize(kv.second)}
        });
    }
    return arr;
}

std::string Memory::toJson(int indent) const
{
    return toJsonObject().dump(indent);
}

// ============================================================
// ============================================================
//           显式实例化：所有模板符号的唯一实例
// ============================================================
// ============================================================

#define TOOLS_INSTANTIATE_IO(...)                                                       \
    template TOOLS_API void Memory::insert<__VA_ARGS__>(                                \
        const std::string&, const __VA_ARGS__&);                                        \
    template TOOLS_API void Memory::insertOrAssign<__VA_ARGS__>(                        \
        const std::string&, const __VA_ARGS__&);                                        \
    template TOOLS_API bool Memory::get<__VA_ARGS__>(                                   \
        const std::string&, __VA_ARGS__&, std::string*) const;                          \
    template TOOLS_API __VA_ARGS__ Memory::get<__VA_ARGS__>(                            \
        const std::string&) const;

#define TOOLS_INSTANTIATE_REG(...)                                                      \
    template TOOLS_API void Memory::registerType<__VA_ARGS__>(                          \
        std::function<std::string(const std::any&)>,                                    \
        std::function<size_t(const std::any&)>);

// ---------- 基础类型 ----------
TOOLS_INSTANTIATE_IO(int)
TOOLS_INSTANTIATE_IO(unsigned int)
TOOLS_INSTANTIATE_IO(long long)
TOOLS_INSTANTIATE_IO(unsigned long long)
TOOLS_INSTANTIATE_IO(float)
TOOLS_INSTANTIATE_IO(double)
TOOLS_INSTANTIATE_IO(bool)
TOOLS_INSTANTIATE_IO(char)
TOOLS_INSTANTIATE_IO(std::string)

// ---------- vector ----------
TOOLS_INSTANTIATE_IO(std::vector<int>)
TOOLS_INSTANTIATE_IO(std::vector<unsigned int>)
TOOLS_INSTANTIATE_IO(std::vector<long long>)
TOOLS_INSTANTIATE_IO(std::vector<unsigned long long>)
TOOLS_INSTANTIATE_IO(std::vector<float>)
TOOLS_INSTANTIATE_IO(std::vector<double>)
TOOLS_INSTANTIATE_IO(std::vector<char>)
TOOLS_INSTANTIATE_IO(std::vector<signed char>)
TOOLS_INSTANTIATE_IO(std::vector<unsigned char>)
TOOLS_INSTANTIATE_IO(std::vector<short>)
TOOLS_INSTANTIATE_IO(std::vector<unsigned short>)
TOOLS_INSTANTIATE_IO(std::vector<std::string>)
TOOLS_INSTANTIATE_IO(std::vector<bool>)
TOOLS_INSTANTIATE_IO(std::vector<std::vector<int>>)
TOOLS_INSTANTIATE_IO(std::vector<std::vector<double>>)

// ---------- deque / list ----------
TOOLS_INSTANTIATE_IO(std::deque<int>)
TOOLS_INSTANTIATE_IO(std::deque<double>)
TOOLS_INSTANTIATE_IO(std::deque<float>)

TOOLS_INSTANTIATE_IO(std::list<int>)
TOOLS_INSTANTIATE_IO(std::list<double>)
TOOLS_INSTANTIATE_IO(std::list<float>)

// ---------- set / unordered_set ----------
TOOLS_INSTANTIATE_IO(std::set<int>)
TOOLS_INSTANTIATE_IO(std::set<double>)
TOOLS_INSTANTIATE_IO(std::set<std::string>)
TOOLS_INSTANTIATE_IO(std::unordered_set<int>)

// ---------- map / unordered_map ----------
TOOLS_INSTANTIATE_IO(std::map<std::string, int>)
TOOLS_INSTANTIATE_IO(std::map<std::string, double>)
TOOLS_INSTANTIATE_IO(std::map<std::string, std::string>)
TOOLS_INSTANTIATE_IO(std::map<int, int>)
TOOLS_INSTANTIATE_IO(std::map<int, double>)

TOOLS_INSTANTIATE_IO(std::unordered_map<std::string, int>)
TOOLS_INSTANTIATE_IO(std::unordered_map<int, int>)
TOOLS_INSTANTIATE_IO(std::unordered_map<int, double>)

// ---------- array ----------
TOOLS_INSTANTIATE_IO(std::array<int, 2>)
TOOLS_INSTANTIATE_IO(std::array<int, 3>)
TOOLS_INSTANTIATE_IO(std::array<int, 4>)
TOOLS_INSTANTIATE_IO(std::array<float, 2>)
TOOLS_INSTANTIATE_IO(std::array<float, 3>)
TOOLS_INSTANTIATE_IO(std::array<float, 4>)
TOOLS_INSTANTIATE_IO(std::array<double, 3>)
TOOLS_INSTANTIATE_IO(std::array<double, 4>)

// ---------- pair / tuple / complex ----------
TOOLS_INSTANTIATE_IO(std::pair<int, int>)
TOOLS_INSTANTIATE_IO(std::pair<int, double>)
TOOLS_INSTANTIATE_IO(std::pair<double, double>)
TOOLS_INSTANTIATE_IO(std::pair<float, float>)
TOOLS_INSTANTIATE_IO(std::pair<std::string, int>)
TOOLS_INSTANTIATE_IO(std::pair<std::string, std::string>)

TOOLS_INSTANTIATE_IO(std::tuple<int, int>)
TOOLS_INSTANTIATE_IO(std::tuple<double, double>)
TOOLS_INSTANTIATE_IO(std::tuple<int, int, int>)
TOOLS_INSTANTIATE_IO(std::tuple<double, double, double>)

TOOLS_INSTANTIATE_IO(std::complex<float>)
TOOLS_INSTANTIATE_IO(std::complex<double>)

// ---------- OpenCV 核心 ----------
TOOLS_INSTANTIATE_IO(cv::Mat)
TOOLS_INSTANTIATE_IO(cv::Point)
TOOLS_INSTANTIATE_IO(cv::Point2f)
TOOLS_INSTANTIATE_IO(cv::Point2d)
TOOLS_INSTANTIATE_IO(cv::Point3f)
TOOLS_INSTANTIATE_IO(cv::Point3d)
TOOLS_INSTANTIATE_IO(cv::Point3i)

TOOLS_INSTANTIATE_IO(cv::Size)
TOOLS_INSTANTIATE_IO(cv::Size2f)
TOOLS_INSTANTIATE_IO(cv::Size2d)

TOOLS_INSTANTIATE_IO(cv::Rect)
TOOLS_INSTANTIATE_IO(cv::Rect2f)
TOOLS_INSTANTIATE_IO(cv::Rect2d)

TOOLS_INSTANTIATE_IO(cv::Scalar)
TOOLS_INSTANTIATE_IO(cv::RotatedRect)

TOOLS_INSTANTIATE_IO(cv::Vec2b)
TOOLS_INSTANTIATE_IO(cv::Vec3b)
TOOLS_INSTANTIATE_IO(cv::Vec4b)
TOOLS_INSTANTIATE_IO(cv::Vec2f)
TOOLS_INSTANTIATE_IO(cv::Vec3f)
TOOLS_INSTANTIATE_IO(cv::Vec4f)
TOOLS_INSTANTIATE_IO(cv::Vec2d)
TOOLS_INSTANTIATE_IO(cv::Vec3d)
TOOLS_INSTANTIATE_IO(cv::Vec4d)

TOOLS_INSTANTIATE_IO(cv::Matx22f)
TOOLS_INSTANTIATE_IO(cv::Matx33f)
TOOLS_INSTANTIATE_IO(cv::Matx44f)
TOOLS_INSTANTIATE_IO(cv::Matx22d)
TOOLS_INSTANTIATE_IO(cv::Matx33d)
TOOLS_INSTANTIATE_IO(cv::Matx44d)

TOOLS_INSTANTIATE_IO(cv::KeyPoint)
TOOLS_INSTANTIATE_IO(cv::DMatch)

// ---------- vector<OpenCV> ----------
TOOLS_INSTANTIATE_IO(std::vector<cv::Point>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Point2f>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Point2d>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Point3f>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Point3d>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Rect>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Rect2f>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Size>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Vec2f>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Vec3b>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Vec4f>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Scalar>)
TOOLS_INSTANTIATE_IO(std::vector<cv::KeyPoint>)
TOOLS_INSTANTIATE_IO(std::vector<cv::DMatch>)
TOOLS_INSTANTIATE_IO(std::vector<cv::RotatedRect>)
TOOLS_INSTANTIATE_IO(std::vector<cv::Mat>)
TOOLS_INSTANTIATE_IO(std::vector<std::vector<cv::Point>>)
TOOLS_INSTANTIATE_IO(std::vector<std::vector<cv::Point2f>>)
TOOLS_INSTANTIATE_IO(std::map<std::string, cv::Mat>)

// ---------- registerType ----------
TOOLS_INSTANTIATE_REG(int)
TOOLS_INSTANTIATE_REG(double)
TOOLS_INSTANTIATE_REG(float)
TOOLS_INSTANTIATE_REG(bool)
TOOLS_INSTANTIATE_REG(std::string)
TOOLS_INSTANTIATE_REG(cv::Mat)
TOOLS_INSTANTIATE_REG(cv::Point)
TOOLS_INSTANTIATE_REG(cv::Point2f)
TOOLS_INSTANTIATE_REG(cv::Rect)

#undef TOOLS_INSTANTIATE_IO
#undef TOOLS_INSTANTIATE_REG

}  // namespace tools