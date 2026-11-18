/**
 * @file        findskills.hpp
 * @brief       skills查找工具头文件,只需负责把查找出来的文件写入skills_file并且保留上一次的SkillInfo
 * @author      o_O a17398481273@163.com
 * @date        2026-11-17
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
    std::string skills_url;                       ///< skill路径
    std::string skills_file;                      ///< skill统计文件
    std::string last_query_time;                  ///< 上一次更新时间
    std::map<std::string,SkillConfigInfo> skills; ///< skill集合
    int last_query_count;                         ///< skill个数
    sc::tools::Time timer;                        ///< 时间管理类

    /**
     * @brief       查找skills文件
     * @param       root_path 搜索路径
     * @note        直接写入skills_list字段
     */
    void find_skills_file(const std::string& root_path);

    /**
     * @brief       读取单个文件获取skill信息
     * @param       file_path 文件路径(不包含文件名称)
     * @return      SkillConfigInfo skills信息
     */
    SkillConfigInfo read_skills_info(const std::string& file_path);

public:

    /**
     * @brief       构造函数
     * @param       configs 配置文件路径
     * @note        配置文件中包含skills_url和skills_file字段
     */
    FindSkills(std::string configs);

    /**
     * @brief       更新skills到json文件中
     */
    bool update();

    /**
     * @brief       获取skills列表
     * @return      std::map<std::string,SkillInfo> skills列表
     */
    std::map<std::string,SkillConfigInfo> get_skills();
};

} // namespace tools

#endif // FINDSKILLS_HPP