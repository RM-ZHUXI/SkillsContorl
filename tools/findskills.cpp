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
    bool FindSkills::update(bool force)
    {
        // STATUS=true 表示程序已经在跑技能了，默认不允许重扫技能表；
        // 「没有任何技能链在运行」时由 HTTP 层用 force=true 放行，
        // 否则界面上的「更新技能」按钮会因为 STATUS 恒为 true 而永远失败
        if(STATUS && !force)
        {
            logger()->warn("[FindSkills] 程序运行中，拒绝重新扫描技能（需要时可传 force=true 强制）");
            return false;
        }

        this->skills_map.clear();
        this->last_query_time = this->timer.nowDate() + " " + this->timer.nowTime();

        logger()->info("[FindSkills] 更新skills数据");

        find_skills_file(skills_url);

        using json = nlohmann::json;

        json res;

        res["last_query_time"] =  this->last_query_time;
        res["skills_size"] = this->last_query_count;
        res["skills"] = this->get_skills();
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
                Skill s = read_skills_info(entry.path().string());
                if(this->skills_map.find(s.name) == this->skills_map.end())
                    this->skills_map[s.name] = s;
                else 
                    throw std::runtime_error("[FindSkills] skill已存在: " + s.name);
            }
        }
        
        this->last_query_count = this->skills_map.size();
    }
    Skill FindSkills::read_skills_info(const std::string& file_path)
    {
        using json = nlohmann::json;
        try
        {
            std::ifstream file(file_path + "/config.json");
            if (!file.is_open()) 
            {
                 throw std::runtime_error("[FindSkills] 读取文件失败: " + file_path);
            }

            std::string content((std::istreambuf_iterator<char>(file)),std::istreambuf_iterator<char>());
            file.close();

            json config = json::parse(content);

            Skill s;

            s.name = config["name"];
            s.version = config["version"];
            s.description = config["description"];
            const auto& author_field = config["author"];
            s.url = (std::filesystem::path(file_path) / config["url"].get<std::string>()).string();

            for(auto& author : author_field)
            {
                s.author.push_back(author.get<std::string>());
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
    std::vector<Skill> FindSkills::get_skills()
    {
        std::vector<Skill> skills_list;
        for(auto e : this->skills_map)
        {
            skills_list.push_back(e.second);
        }
        return skills_list;
    }
    Skill FindSkills::get_skill_once(std::string name)
    {
        if(this->skills_map.find(name) == this->skills_map.end())
            throw std::runtime_error("[FindSkills] " + name + "-skill不存在");
        
        return this->skills_map[name];
    }
} // namespace tools

