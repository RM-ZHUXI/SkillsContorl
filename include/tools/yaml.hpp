#ifndef TOOLS__YAML_HPP
#define TOOLS__YAML_HPP

#include <yaml-cpp/yaml.h>

#include <stdexcept>
#include <string>
#include <fstream>

#include "tools/logger.hpp"

namespace sc::tools
{
/**
 * @brief 读取 yaml 文件
 * @param path 文件路径（程序按当前工作目录解析相对路径）
 * @return YAML::Node 根节点
 * @throw std::runtime_error 文件不存在/打不开/解析失败
 * @warning 以前这里失败会直接 exit(1)：技能里调用它时会把**整个宿主进程**干掉
 *          （日志停在 "[YAML] Failed to load file" 然后程序凭空消失）。
 *          现在统一抛异常，由调用方决定怎么处理：
 *          - 主程序里由 main 的 try/catch 兜住并打印错误后退出；
 *          - 技能里由 RunSkillsLists 兜住，只把这条技能链标记为失败，进程继续活着。
 */
inline YAML::Node load(const std::string & path)
{
  try {
    return YAML::LoadFile(path);
  } catch (const YAML::BadFile & e) {
    logger()->error("[YAML] 无法打开文件: {} ({})", path, e.what());
    throw std::runtime_error("[YAML] 无法打开文件: " + path);
  } catch (const YAML::ParserException & e) {
    logger()->error("[YAML] 解析失败: {} ({})", path, e.what());
    throw std::runtime_error("[YAML] 解析失败: " + path);
  } catch (const std::exception & e) {
    logger()->error("[YAML] 读取异常: {} ({})", path, e.what());
    throw std::runtime_error("[YAML] 读取异常: " + path);
  }
}

/**
 * @brief 从 yaml 节点里取一个字段
 * @param yaml 根节点
 * @param key  字段名
 * @return T 字段值
 * @throw std::runtime_error 字段不存在或类型不匹配
 * @warning 同样不再 exit(1)，理由见 load()
 */
template <typename T>
inline T read(const YAML::Node & yaml, const std::string & key)
{
  if (!yaml[key]) {
    logger()->error("[YAML] 缺少字段: {}", key);
    throw std::runtime_error("[YAML] 缺少字段: " + key);
  }

  try {
    return yaml[key].as<T>();
  } catch (const std::exception & e) {
    logger()->error("[YAML] 字段 {} 类型不匹配: {}", key, e.what());
    throw std::runtime_error("[YAML] 字段 " + key + " 类型不匹配");
  }
}

/**
 * @brief 覆盖 yaml 节点里某个已存在字段的值
 * @param yaml 根节点（必须是可修改的引用）
 * @param key  字段名
 * @param value 新值
 * @throw std::runtime_error 字段不存在或写入失败
 * @note 只修改内存中的节点，不会自动落盘；需要保存请再调用 save()，会删除注释
 */
template <typename T>
inline void overwrite(YAML::Node & yaml, const std::string & key, const T & value)
{
  if (!yaml[key]) {
    logger()->error("[YAML] 缺少字段，无法覆盖: {}", key);
    throw std::runtime_error("[YAML] 缺少字段，无法覆盖: " + key);
  }

  try {
    yaml[key] = value;
  } catch (const std::exception & e) {
    logger()->error("[YAML] 覆盖字段 {} 失败: {}", key, e.what());
    throw std::runtime_error("[YAML] 覆盖字段 " + key + " 失败");
  }
}

/**
 * @brief 把 yaml 节点写回文件
 * @param path 文件路径
 * @param yaml 根节点
 * @throw std::runtime_error 写入失败
 * @note 会删除注释
 */
inline void save(const std::string & path, const YAML::Node & yaml)
{
  try {
    YAML::Emitter out;
    out << yaml;
    std::ofstream fout(path);
    if (!fout.is_open()) {
      throw std::runtime_error("[YAML] 无法写入文件: " + path);
    }
    fout << out.c_str();
  } catch (const std::exception & e) {
    logger()->error("[YAML] 保存失败: {} ({})", path, e.what());
    throw std::runtime_error("[YAML] 保存失败: " + path);
  }
}

}  // namespace tools

#endif  // TOOLS__YAML_HPP
