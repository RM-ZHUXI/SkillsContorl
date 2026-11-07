#ifndef AIMER_HPP
#define AIMER_HPP

#include <Eigen/Dense>
#include <chrono>
#include <list>

#include "command.hpp"
#include "target.hpp"

namespace autoAim
{

/**
 * @brief 瞄准点
 */
struct AimPoint
{
  bool valid;              ///< 是否有效（false 表示当前无可用瞄准点，不应开火）
  Eigen::Vector4d xyza;    ///< 瞄准点：x, y, z（世界系位置，m）+ yaw（朝向，rad）
};

/**
 * @brief 瞄准器
 *
 * 从跟踪目标（Target）中选择要打的装甲板（选板），结合弹道模型计算
 * 云台 yaw/pitch 和飞行时间补偿，生成 io::Command 控制指令。
 *
 * 主要职责：
 *   - 选板：在多块装甲板中选当前最该打的那块（choose_aim_point）；
 *   - 弹道解算：根据弹速、目标运动预测，算提前量；
 *   - 指令生成：输出 yaw/pitch/开火标志等控制量。
 *
 * @note 非线程安全：lock_id_ 等状态可变，多线程需外部加锁。
 */
class Aimer
{
public:
  AimPoint debug_aim_point;  ///< 调试用：最近一次选中的瞄准点（供可视化/日志）

  /**
   * @brief 构造瞄准器
   * @param config_path 配置文件路径（偏移量、延迟时间、角度阈值等）
   */
  explicit Aimer(const std::string & config_path);

  /**
   * @brief 瞄准主入口（默认射击模式）
   *
   * @param targets      跟踪目标列表（可能多个目标，内部选优先级最高的）
   * @param timestamp    当前时间戳（用于预测目标运动到当前时刻）
   * @param bullet_speed 弹速（m/s），影响弹道和提前量
   * @param to_now       true 预测到当前时刻；false 预测到"发射后命中时刻"
   * @return             控制指令（yaw/pitch/开火等）
   */
  io::Command aim(
    std::list<Target> targets, std::chrono::steady_clock::time_point timestamp, double bullet_speed,
    bool to_now = true);

private:
  double yaw_offset_;                              ///< yaw 固定偏移（机械/标定误差补偿，rad）
  std::optional<double> left_yaw_offset_, right_yaw_offset_;  ///< 左右两侧的 yaw 偏移（可选，分侧标定）
  double pitch_offset_;                            ///< pitch 固定偏移（rad）

  double comming_angle_;       ///< 装甲板"迎面而来"的角度阈值（rad），用于选板
  double leaving_angle_;       ///< 装甲板"背离而去"的角度阈值（rad），用于选板
  double lock_id_ = -1;        ///< 当前锁定的装甲板 id（-1 表示未锁定），用于保持选板稳定
  double high_speed_delay_time_;  ///< 高弹速下的延迟时间（s），补偿系统延迟
  double low_speed_delay_time_;   ///< 低弹速下的延迟时间（s）
  double decision_speed_;         ///< 弹速阈值（m/s），决定用高/低弹速延迟参数

  /**
   * @brief 选择瞄准点（选板）
   *
   * 从目标的多块装甲板中，结合目标旋转状态、当前锁定 id、迎面/背离角度，
   * 选出当前最该打的那块板，返回其位姿。
   *
   * @param target 跟踪目标（含 EKF 状态、各板位姿）
   * @return       瞄准点（valid=false 表示无合适板）
   */
  AimPoint choose_aim_point(const Target & target);
};

}  // namespace autoAim

#endif  // AIMER_HPP