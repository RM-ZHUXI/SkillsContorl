#ifndef TRAJECTORY_HPP
#define TRAJECTORY_HPP

namespace tools
{
/**
 * @brief 弹道解算结果
 *
 * 根据弹丸初速度、目标水平距离和竖直高度，在"不考虑空气阻力"的假设下，
 * 解算出发射所需的 pitch 角和弹丸飞行时间。
 *
 * 物理模型：斜抛运动（抛物线），只受重力，忽略空气阻力。
 */
struct Trajectory
{
  bool unsolvable;  ///< 是否无解（目标超出射程/初速度不足以到达，true 时 pitch/fly_time 无意义）
  double fly_time;  ///< 弹丸飞行时间（s），用于目标运动预测/提前量
  double pitch;     ///< 发射 pitch 角（rad），抬头为正

  /**
   * @brief 解算弹道
   *
   * 已知初速度 v0、目标水平距离 d、目标竖直高度 h（相对枪口），
   * 解算发射 pitch 和飞行时间。不考虑空气阻力。
   *
   * @param v0 子弹初速度（m/s）
   * @param d  目标水平距离（m）
   * @param h  目标竖直高度（m，相对枪口，向上为正）
   *
   * @note 无解时 unsolvable = true（如 d 超出最大射程，或 v0 太小打不到）。
   */
  Trajectory(const double v0, const double d, const double h);
};
}  // namespace tools

#endif  // TRAJECTORY_HPP