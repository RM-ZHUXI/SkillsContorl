/**
 * @file        findskills.hpp
 * @brief       skills查找工具头文件
 * @author      时新博 a17398481273@163.com
 * @date        2026-09-17
 * 
 * @version     1.0.1
 * @par         更新update方法，增加全局程序运行状态监测
 * 
 * @version     1.0.2
 * @par         添加get_skill方法
 * 
 * @version     1.2.0
 * @par         将skill方法更新为map而不是version
 *              要求skill不允许重名，添加重名检查  
 */
#ifndef FINDSKILLS_HPP
#define FINDSKILLS_HPP

#include <iostream>
#include <vector>
#include <string>
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <regex>

#include "yaml.hpp"
#include "logger.hpp"
#include "time.hpp"
#include "tools_api.hpp"
#include "type.hpp"

namespace sc::tools
{

class TOOLS_API FindSkills
{
private:
    std::string skills_url;
    std::string skills_file;
    std::map<std::string,Skill> skills_map;
    std::string last_query_time;
    int last_query_count;

    sc::tools::Time timer;

    /**
     * @brief       查找skills文件
     * @param       root_path 搜索路径
     * @note        直接写入skills_list字段
     */
    void find_skills_file(const std::string& root_path);

    /**
     * @brief       读取单个文件获取skill信息
     * @param       file_path 文件路径(不包含文件名称)
     * @return      Skill skills信息
     */
    Skill read_skills_info(const std::string& file_path);

public:

    /**
     * @brief       构造函数
     * @param       configs 配置文件路径
     * @note        配置文件中包含skills_url和skills_file字段
     */
    FindSkills(std::string configs);

    /**
     * @brief       更新skills到json文件中
     * @param       force 运行中(STATUS=true)时是否也强制重新扫描，默认 false
     * @return      bool 更新成功返回true
     * @note        默认在 STATUS=true（程序运行中）时拒绝重扫，避免技能正在被使用时改动技能表；
     *              HTTP 层在确认"没有任何技能链在运行"之后会用 force=true 调用，
     *              否则界面上的「更新技能」按钮会因为 STATUS 恒为 true 而永远失败。
     */
    bool update(bool force = false);

    /**
     * @brief       获取skills列表
     * @return      std::vector<Skill> skills列表
     */
    std::vector<Skill> get_skills();

    /**
     * @brief       获取指定skills
     * @param       name skills名称
     * @return      Skill skills的详细信息
     * @throw       std::runtime_error skills不存在
     */
    Skill get_skill_once(std::string name);
};

} // namespace tools

#endif // FINDSKILLS_HPP