#ifndef SHOOTER_HPP
#define SHOOTER_HPP

#include <string>

#include "command.hpp"
#include "aimer.hpp"

namespace autoAim
{
/**
 * @brief 射击决策器
 *
 * 根据瞄准器（Aimer）的瞄准状态、当前目标、云台位置和距离容差，
 * 判断是否满足开火条件，输出射击指令
 *
 * @note 非线程安全：last_command_ 可变，多线程调用需外部加锁。
 */
class Shooter
{
public:
  /**
   * @brief 构造射击决策器
   * @param config_path 配置文件路径（距离阈值、容差、自动开火开关等）
   * @note 配置文件中应该有
   * first_tolerance    double  角度容差（单位：度)(严格，小)
   * second_tolerance   double  角度容差（单位：度)(宽松，大)
   * judge_distance     double  距离阈值（单位：米）
   * auto_fire          double  是否开启自动射击
   */
  Shooter(const std::string & config_path);

  /**
   * @brief 判断是否开火并输出射击指令
   *
   * @param command    当前控制指令（含云台/发射机构状态）
   * @param aimer      瞄准器（提供瞄准误差/是否锁定等信息）
   * @param targets    当前跟踪目标列表
   * @param gimbal_pos 云台当前位置（世界系，m）
   * @return           true 表示满足开火条件（已发出射击指令）
   *
   * @note 内部会更新 last_command_，避免重复/抖动开火。
   */
  bool shoot(
    const io::Command & command, const Aimer & aimer,
    const std::list<Target> & targets, const Eigen::Vector3d & gimbal_pos);

private:
  io::Command last_command_;  ///< 上一次的控制指令（用于防抖/边沿检测，避免连续重复开火）
  double judge_distance_;     ///< 有效射击距离阈值（m），超过则不射
  double first_tolerance_;    ///< 一级容差（较严格，满足即开火）
  double second_tolerance_;   ///< 二级容差（较宽松，用于放宽/延迟开火）
  bool auto_fire_;            ///< 是否启用自动开火（false 时只瞄准不射击）
};
}  // namespace autoAim

#endif  // SHOOTER_HPP