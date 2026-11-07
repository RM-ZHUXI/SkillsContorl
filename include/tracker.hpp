#ifndef TRACKER_HPP
#define TRACKER_HPP

#include <Eigen/Dense>
#include <chrono>
#include <list>
#include <string>

#include "armor.hpp"
#include "solver.hpp"
#include "target.hpp"
#include "thread_safe_queue.hpp"

namespace autoAim
{
/**
 * @brief 目标跟踪器
 *
 * 将每帧检测到的装甲板（Armor）关联为连续的目标轨迹（Target），
 * 维护"未跟踪 / 跟踪中 / 短暂丢失 / 永久丢失"的状态机，
 * 并负责选择打击目标、更新目标位姿、处理目标切换。
 *
 * @note 依赖 Solver 做位姿解算，依赖 Target 做轨迹滤波/预测。
 * @note 非线程安全：内部有状态机成员，多线程调用需外部加锁。
 */
class Tracker
{
public:
  /**
   * @brief 构造跟踪器
   * @param config_path 配置文件路径（丢失阈值、最小检测次数、敌方颜色等）
   * @param solver      位姿解算器引用（跟踪过程中解算装甲板位姿）
   */
  Tracker(const std::string & config_path, Solver & solver);

  /**
   * @brief 获取当前跟踪状态
   * @return 状态字符串（如 "tracking" / "temp_lost" / "lost" / "idle" 等）
   */
  std::string state() const;

  /**
   * @brief 跟踪主入口（单目标/普通模式）
   *
   * 输入本帧检测到的装甲板列表，关联到当前目标，更新状态机，
   * 返回当前所有有效目标列表。
   *
   * @param armors         本帧检测到的装甲板（可能被修改，如解算位姿）
   * @param t              当前帧时间戳（steady_clock）
   * @param use_enemy_color 是否按敌方颜色过滤（true 只跟踪敌方装甲板）
   * @return               跟踪中的目标列表
   */
  std::list<Target> track(
    std::list<Armor> & armors, std::chrono::steady_clock::time_point t,
    bool use_enemy_color = true);

private:
  Solver & solver_;                                       ///< 位姿解算器引用（解算装甲板世界位姿）
  Color enemy_color_;                                     ///< 敌方颜色（红/蓝），用于过滤目标
  int min_detect_count_;                                  ///< 确认目标所需的最小连续检测次数（防误检）
  int max_temp_lost_count_;                               ///< 允许的最大"短暂丢失"帧数（超过则判永久丢失）
  int detect_count_;                                      ///< 当前连续检测计数
  int temp_lost_count_;                                   ///< 当前连续丢失计数
  int outpost_max_temp_lost_count_;                       ///< 前哨站的最大短暂丢失帧数（结构特殊，容忍更久）
  int normal_temp_lost_count_;                            ///< 普通装甲板的最大短暂丢失帧数
  std::string state_;                                     ///< 当前状态
  std::string pre_state_;                                 ///< 上一状态
  Target target_;                                         ///< 当前跟踪的目标轨迹
  std::chrono::steady_clock::time_point last_timestamp_;  ///< 上一帧时间戳（算 dt）
  ArmorPriority omni_target_priority_;                    ///< 全向感知模式下选中目标的优先级

  /**
   * @brief 状态机更新
   * @param found 本帧是否检测到目标
   *
   */
  void state_machine(bool found);

  /**
   * @brief 设置新目标（首次锁定或切换目标）
   * @param armors 本帧装甲板列表
   * @param t      当前时间戳
   * @return       是否成功设置目标
   */
  bool set_target(std::list<Armor> & armors, std::chrono::steady_clock::time_point t);

  /**
   * @brief 更新当前目标（关联本帧检测，更新轨迹/滤波）
   * @param armors 本帧装甲板列表
   * @param t      当前时间戳
   * @return       是否成功更新（本帧是否匹配到目标）
   */
  bool update_target(std::list<Armor> & armors, std::chrono::steady_clock::time_point t);
};

}  // namespace autoAim

#endif  // AUTO_AIM__TRACKER_HPP