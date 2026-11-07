#ifndef TARGET_HPP
#define TARGET_HPP

#include <Eigen/Dense>
#include <chrono>
#include <optional>
#include <queue>
#include <string>
#include <vector>

#include "armor.hpp"
#include "extended_kalman_filter.hpp"

namespace autoAim
{
/**
 * @brief 目标轨迹（跨帧）
 * @note 非线程安全：EKF 和计数器可变，多线程需外部加锁。
 */
class Target
{
public:
  ArmorName name;           ///< 目标编号/身份（one~five、sentry、outpost、base 等）
  ArmorType armor_type;     ///< 装甲板大小类型（small / big）
  ArmorPriority priority;   ///< 打击优先级
  bool jumped;              ///< 是否发生过装甲板跳变（目标旋转导致当前面向的板切换）
  int last_id;              ///< 最近匹配的装甲板 id（仅调试用）

  Target() = default;

  /**
   * @brief 由首个装甲板初始化目标
   * @param armor      首个检测到的装甲板
   * @param t          时间戳
   * @param radius     目标旋转半径（m，用于初始化 EKF 的 r）
   * @param armor_num  装甲板数量（2 平衡 / 3 前哨站·基地 / 4 普通）
   * @param P0_dig     初始协方差对角元
   */
  Target(
    const Armor & armor, std::chrono::steady_clock::time_point t, double radius, int armor_num,
    Eigen::VectorXd P0_dig);

  /**
   * @brief 用简化参数直接构造目标（调试/仿真用）
   * @param x    初始 x 位置
   * @param vyaw 初始角速度
   * @param radius 旋转半径
   * @param h    装甲板高度
   */
  Target(double x, double vyaw, double radius, double h);

  /**
   * @brief 由「外部已算好的 EKF 状态」重建目标 —— 技能化专用
   *
   * @details 用途：估计器做成技能 .so 后，Target 活在估计器的动态库里，
   *          决策器（Aimer）在自己的动态库里拿不到该对象。于是估计器把状态发到公共内存，
   *          决策器用本构造函数重建等价目标再交给 Aimer::aim()。
   *          状态定义与 ekf_x() 完全一致：
   *              x = [x, vx, y, vy, z, vz, a, w, r, l, h]
   *
   * @param ekf_x        11 维状态
   * @param ekf_P        11x11 协方差（技能间只传对角阵：p_diag.asDiagonal()）
   * @param target_name  目标编号
   * @param target_type  装甲板大小
   * @param target_priority 打击优先级
   * @param jumped       是否已发生装甲板跳变（影响 Aimer 选板分支）
   * @param armor_num    装甲板数量（2/3/4）
   * @param t            该状态对应时刻（后续 predict 以它为基准算 dt）
   * @param update_count 已累积量测更新次数，只影响 convergened()；
   *                     跨 DLL 传过来的通常已跟踪上，默认 100（视为已收敛）
   *
   * @note 仅"把状态装进去"，预测/更新/量测方程仍走原有实现。
   */
  Target(
    const Eigen::VectorXd & ekf_x, const Eigen::MatrixXd & ekf_P, ArmorName target_name,
    ArmorType target_type, ArmorPriority target_priority, bool jumped, int armor_num,
    std::chrono::steady_clock::time_point t, int update_count = 100);

  /// @brief 装甲板数量（决策器重建目标时用；原有代码行为不变）
  int armor_num() const { return armor_num_; }

  /**
   * @brief 按时间戳预测到时刻 t（内部算 dt 再调 predict(dt)）
   * @param t 目标时刻
   */
  void predict(std::chrono::steady_clock::time_point t);

  /**
   * @brief 按时间增量 dt 预测
   * @param dt 时间增量（s）
   */
  void predict(double dt);

  /**
   * @brief 用新的装甲板量测更新 EKF
   * @param armor 本帧匹配到的装甲板（含位姿/ypd 等）
   */
  void update(const Armor & armor);

  /// @brief 获取 EKF 当前状态向量 x（11 维）
  Eigen::VectorXd ekf_x() const;

  /// @brief 获取内部 EKF 对象（只读）
  const tools::ExtendedKalmanFilter & ekf() const;

  /**
   * @brief 计算各装甲板在目标坐标系下的位姿 (x, y, z, yaw)
   *
   * 根据当前状态 x 推算每个装甲板（id = 0..armor_num-1）的世界坐标与朝向，
   * 供 Aimer 选板、弹道解算使用。
   *
   * @return armor_num 个 (x, y, z, yaw) 向量
   */
  std::vector<Eigen::Vector4d> armor_xyza_list() const;

  /// @brief 判断 EKF 是否发散（协方差过大或状态异常）
  bool diverged() const;

  /// @brief 判断 EKF 是否已收敛（更新次数足够 + 协方差足够小）
  bool convergened();

  bool isinit = false;   ///< 是否已初始化（外部标志）

  /// @brief 检查并返回是否已初始化
  bool checkinit();

private:
  int armor_num_;        ///< 装甲板数量（2 平衡 / 3 前哨站·基地 / 4 普通）
  int switch_count_;     ///< 装甲板跳变计数（切换次数）
  int update_count_;     ///< 量测更新累计次数

  bool is_switch_, is_converged_;   ///< 是否正在跳变、是否已收敛

  tools::ExtendedKalmanFilter ekf_;  ///< 扩展卡尔曼滤波器
  std::chrono::steady_clock::time_point t_;  ///< 上次预测/更新的时间戳

  /**
   * @brief 用装甲板的 ypd（yaw/pitch/distance/angle）做量测更新
   * @param armor 装甲板量测
   * @param id    装甲板编号（0..armor_num-1）
   */
  void update_ypda(const Armor & armor, int id);  // yaw pitch distance angle

  /**
   * @brief 量测方程：由状态 x 和装甲板 id 推算该装甲板的世界坐标
   * @param x  状态向量
   * @param id 装甲板编号
   * @return   该装甲板的世界坐标 (x, y, z)
   */
  Eigen::Vector3d h_armor_xyz(const Eigen::VectorXd & x, int id) const;

  /**
   * @brief 量测方程对状态 x 的雅可比矩阵
   * @param x  状态向量
   * @param id 装甲板编号
   * @return   雅可比矩阵
   */
  Eigen::MatrixXd h_jacobian(const Eigen::VectorXd & x, int id) const;
};

}  // namespace autoAim

#endif  // TARGET_HPP