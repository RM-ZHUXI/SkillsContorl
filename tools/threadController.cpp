#include "tools/threadController.hpp"

namespace sc::tools
{
/**
 * @brief ThreadItem 部分
 */

ThreadItem::~ThreadItem(){
    if (!thread_.joinable()) return;
    std::lock_guard<std::mutex> joinLock(mtx_join_);

    if (thread_.joinable()) {
        {
            std::lock_guard<std::mutex> lk(info_.mtx_stop);
            info_.stop = true; 
        }
        thread_.join();          
    }
}

void ThreadItem::run()
{
    if (!function_) throw std::runtime_error("[ThreadItem] 运行函数为空");

    {
        std::lock_guard<std::mutex> lk(info_.mtx_run);
        if (info_.workerId == std::this_thread::get_id()) return;
    }

    std::lock_guard<std::mutex> joinLock(mtx_join_);  

    if (thread_.joinable()) {                      
        bool running;
        {
            std::lock_guard<std::mutex> lk(info_.mtx_run);
            running = info_.run;
        }
        if (running) return;                     
        thread_.join();                           
    }
        

    {
        { std::lock_guard<std::mutex> lk(info_.mtx_run);  info_.run  = true;  }   
        { std::lock_guard<std::mutex> lk(info_.mtx_stop); info_.stop = false; }                 
    }

    try {
        thread_ = std::thread([this] {
            {
                std::lock_guard<std::mutex> lk(info_.mtx_run);
                info_.workerId = std::this_thread::get_id();  
            }
            struct Guard {                                  
                ThreadInfo& i;
                ~Guard() {
                    std::lock_guard<std::mutex> lk(i.mtx_run);
                    i.run = false;
                    i.workerId = std::thread::id{};
                }
            } g{info_};
            try 
            { 
                function_(&info_); 
            } 
            catch (const std::exception& e) 
            {
                logger()->error(std::string(e.what()));
            } 
            catch (...)
            {
                logger()->error("线程出现未知错误");
            }
        });
    } catch (...) {
        std::lock_guard<std::mutex> lk(info_.mtx_run);      
        info_.run = false;
        info_.workerId = std::thread::id{};
        throw;
    }
}

void ThreadItem::stop()
{
    {
        std::lock_guard<std::mutex> lk(info_.mtx_run);
        if (info_.workerId == std::this_thread::get_id()) return;
    }

    {
        std::lock_guard<std::mutex> lk(info_.mtx_stop);
        info_.stop = true;
    }

    {
        std::lock_guard<std::mutex> joinLock(mtx_join_);
        if (thread_.joinable()) thread_.join();
    }

    {
        std::lock_guard<std::mutex> lk(info_.mtx_run);
        info_.run = false;
    }
}

bool ThreadItem::isRun()
{
    std::lock_guard<std::mutex> lock(info_.mtx_run);
    return info_.run;
}

/**
 * @brief ThreadController 部分
 */

void ThreadController::addThread(std::string name,std::function<void(ThreadInfo*)> function)
{
    if(this->thread_.find(name) != this->thread_.end())
    {
        throw std::runtime_error("[ThreadController] 添加 " + name + " 已经在线程中存在" );        
    }

    this->thread_[name] = std::make_unique<ThreadItem>(name,std::move(function));
}

ThreadItem& ThreadController::get(const std::string &name)
{
    auto it = thread_.find(name);
    if (it == thread_.end() || !it->second) throw std::runtime_error("[ThreadController] 获得 " + name + " 不存在" );
    return *it->second;
}

bool ThreadController::erase(const std::string &name)
{
    auto item = thread_.find(name);
    if(item == thread_.end() || !item->second)return false;
    if(item->second->isRun()) return false;
    this->thread_.erase(name);
    return true;
}

} // namespace sc::tools