#include "tools/findskills.hpp"

namespace sc::tools
{
    FindSkills::FindSkills(std::string configs)
    {
        auto config = tools::load(configs);
        this->skills_url = tools::read<std::string>(config, "skills_url");
        this->skills_file = tools::read<std::string>(config, "skills_file");
        this->update();
    }
    bool FindSkills::update()
    {

        this->last_query_time = this->timer.nowDate() + " " + this->timer.nowTime();

        logger()->info("[FindSkills] 更新skills数据");

        find_skills_file(skills_url);

        using json = nlohmann::json;

        json res;

        res["last_query_time"] =  this->last_query_time;
        res["skills_size"] = this->last_query_count;
        json skills_json = json::array(); 
        for(const auto& e:skills)
        {
            json item;
            item["author"] = e.second.author;
            item["description"] = e.second.description;
            item["name"] = e.second.name;
            item["url"] = e.second.url;
            item["version"] = e.second.version;
            skills_json.push_back(item);
        }
        res["skills"] = skills_json;
        try {
            std::ofstream file(skills_file);
            if (!file.is_open()) {
                throw std::runtime_error("[FindSkills] 无法打开文件: " + skills_file);
            }
            file << res.dump(4);
            file.close();
        } catch (const std::exception& e) {
            throw std::runtime_error("[FindSkills] 无法保存文件: " + skills_file);
        }
        return true;
    }

    void FindSkills::find_skills_file(const std::string& root_path)
    {
        namespace fs = std::filesystem;

        fs::path root(root_path);

        if (!fs::exists(root)) 
            throw std::runtime_error("[FindSkills] skill路径不存在");
        
        if (!fs::is_directory(root)) 
            throw std::runtime_error("[FindSkills] skill路径不是目录");
        
         for (const auto& entry : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied)) {
            if (!fs::is_directory(entry.status())) {
                continue;
            }
            
            fs::path config_path = entry.path() / "config.json";
            
            if (fs::exists(config_path) && fs::is_regular_file(config_path)) {
                SkillConfigInfo s = read_skills_info(entry.path().string());
                if(this->skills.find(s.name) == this->skills.end())
                    this->skills[s.name] = std::move(s);
                else 
                    throw std::runtime_error("[FindSkills] 更新失败skill已存在: " + s.name);
            }
        }
        
        this->last_query_count = this->skills.size();
    }

    SkillConfigInfo FindSkills::read_skills_info(const std::string& file_path)
    {
        using json = nlohmann::json;
        try
        {
            std::filesystem::path cfg_path = std::filesystem::path(file_path) / "config.json";
            std::ifstream file(cfg_path);
            
            if (!file.is_open()) 
            {
                throw std::runtime_error("[FindSkills] 无法打开文件: " + cfg_path.string());
            }

            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            file.close();

            json config = json::parse(content);

            if (!config.contains("name") || !config["name"].is_string() ||
                !config.contains("version") || !config["version"].is_string() ||
                !config.contains("description") || !config["description"].is_string() ||
                !config.contains("author") || !config["author"].is_array() ||
                !config.contains("url") || !config["url"].is_string())
            {
                throw std::runtime_error("[FindSkills] json格式错误: 缺少必要字段或类型不匹配 (" + cfg_path.string() + ")");
            }

            SkillConfigInfo s;
            s.name = config["name"].get<std::string>();
            s.version = config["version"].get<std::string>();
            s.description = config["description"].get<std::string>();
            
            for (const auto& author : config["author"])
            {
                if (author.is_string()) {
                    s.author.push_back(author.get<std::string>());
                }
            }
            /**
             * @brief 处理文件名
             */
            {
                std::string url_str = config["url"].get<std::string>();
                
                std::filesystem::path full_path = std::filesystem::path(file_path) / url_str;

                #ifdef _WIN32
                    const std::string expected_ext = ".dll";
                #elif defined(__linux__)
                    const std::string expected_ext = ".so";
                #endif

                if (full_path.extension() != expected_ext) {
                    full_path += expected_ext;
                }

                s.url = full_path.string();
            }

            tools::logger()->info("[FindSkills] 寻找到 skill: " + s.name);

            return s;
        }
        catch (const nlohmann::json::parse_error& e) 
        {
                throw std::runtime_error("[FindSkills] 文件:" + file_path + " 解析错误:" + std::to_string(e.byte));
        }
        catch (std::exception& e)
        {
            throw std::runtime_error("[FindSkills] 读取文件失败: " + std::string(e.what()));
        }
    }

    std::map<std::string,SkillConfigInfo> FindSkills::get_skills()
    {
        return skills;
    }
} // namespace tools

