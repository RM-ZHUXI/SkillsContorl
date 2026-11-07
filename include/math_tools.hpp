/**
 * @file math_utils.hpp
 * @brief 数学与坐标变换工具函数集合
 *
 * 提供角度归一化、四元数/旋转矩阵与欧拉角互转、直角坐标与球坐标互转
 * （含雅可比矩阵）、时间差、向量夹角等通用工具，供位姿解算、滤波、
 * 弹道计算等模块使用。
 */

#ifndef MATH_TOOLS_HPP
#define MATH_TOOLS_HPP

#include <Eigen/Geometry>
#include <chrono>

namespace tools
{


/**
 * @brief 将弧度值限制在 (-π, π]
 *
 * 用于角度归一化，避免 yaw/pitch 在跨 ±π 边界时出现跳变（如 179° → -179°）。
 *
 * @param angle 输入角度（rad）
 * @return      归一化到 (-π, π] 的角度（rad）
 */
double limit_rad(double angle);

/**
 * @brief 四元数转欧拉角（可指定旋转顺序与内旋/外旋）
 *
 * 轴编号：x = 0, y = 1, z = 2。
 * 例如"先绕 z、再绕 y、最后绕 x"（ZYX 顺序）对应 axis0=2, axis1=1, axis2=0。
 *
 * @param q        输入四元数
 * @param axis0    第一个旋转轴（0/1/2）
 * @param axis1    第二个旋转轴
 * @param axis2    第三个旋转轴
 * @param extrinsic true 为外旋（绕固定轴），false 为内旋（绕自身轴，默认）
 * @return         欧拉角向量（rad），分量顺序与 axis0/1/2 对应
 *
 * @see https://github.com/evbernardes/quaternion_to_euler
 */
Eigen::Vector3d eulers(
  Eigen::Quaterniond q, int axis0, int axis1, int axis2, bool extrinsic = false);

/**
 * @brief 旋转矩阵转欧拉角（可指定旋转顺序与内旋/外旋）
 *
 * 轴编号与顺序约定同上。
 *
 * @param R         输入旋转矩阵（3x3）
 * @param axis0     第一个旋转轴（0/1/2）
 * @param axis1     第二个旋转轴
 * @param axis2     第三个旋转轴
 * @param extrinsic true 为外旋，false 为内旋（默认）
 * @return          欧拉角向量（rad）
 */
Eigen::Vector3d eulers(Eigen::Matrix3d R, int axis0, int axis1, int axis2, bool extrinsic = false);

/**
 * @brief 欧拉角转旋转矩阵（ZYX 顺序：先绕 z，再绕 y，最后绕 x）
 *
 * @param ypr 欧拉角向量，依次为 yaw、pitch、roll（rad）
 * @return    对应的 3x3 旋转矩阵
 *
 * @note 与 Solver 中"ypr"字段的约定一致：R = Rz(yaw) * Ry(pitch) * Rx(roll)。
 */
Eigen::Matrix3d rotation_matrix(const Eigen::Vector3d & ypr);

/**
 * @brief 直角坐标系转球坐标系
 *
 * ypd 为 yaw、pitch、distance 的缩写：
 *   - yaw      = atan2(y, x)          水平方位角
 *   - pitch    = atan2(z, sqrt(x²+y²)) 俯仰角
 *   - distance = ||xyz||              到原点距离
 *
 * @param xyz 直角坐标 (x, y, z)，单位：m
 * @return    球坐标 (yaw, pitch, distance)，角度单位 rad，距离单位 m
 */
Eigen::Vector3d xyz2ypd(const Eigen::Vector3d & xyz);

/**
 * @brief 直角坐标转球坐标的雅可比矩阵
 *
 * 用于扩展卡尔曼滤波（EKF）等需要线性化的场景：
 * J = ∂(yaw, pitch, distance) / ∂(x, y, z)，3x3 矩阵。
 *
 * @param xyz 直角坐标 (x, y, z)
 * @return    3x3 雅可比矩阵
 */
Eigen::MatrixXd xyz2ypd_jacobian(const Eigen::Vector3d & xyz);

/**
 * @brief 球坐标系转直角坐标系
 *
 * 是 xyz2ypd 的逆变换：
 *   x = d * cos(pitch) * cos(yaw)
 *   y = d * cos(pitch) * sin(yaw)
 *   z = d * sin(pitch)
 *
 * @param ypd 球坐标 (yaw, pitch, distance)
 * @return    直角坐标 (x, y, z)，单位：m
 */
Eigen::Vector3d ypd2xyz(const Eigen::Vector3d & ypd);

/**
 * @brief 球坐标转直角坐标的雅可比矩阵
 *
 * J = ∂(x, y, z) / ∂(yaw, pitch, distance)，3x3 矩阵。
 *
 * @param ypd 球坐标 (yaw, pitch, distance)
 * @return    3x3 雅可比矩阵
 */
Eigen::MatrixXd ypd2xyz_jacobian(const Eigen::Vector3d & ypd);

/**
 * @brief 计算两个时间点之差 a - b
 *
 * @param a 被减时间点（steady_clock）
 * @param b 减数时间点（steady_clock）
 * @return  时间差，单位：秒
 */
double delta_time(
  const std::chrono::steady_clock::time_point & a, const std::chrono::steady_clock::time_point & b);

/**
 * @brief 计算两个二维向量的夹角
 *
 * 总是返回 [0, π]，与向量顺序无关（参考 SJTU 开源实现）。
 *
 * @param vec1 向量 1
 * @param vec2 向量 2
 * @return      夹角（rad），范围 [0, π]
 */
double get_abs_angle(const Eigen::Vector2d & vec1, const Eigen::Vector2d & vec2);

/**
 * @brief 返回输入值的平方
 *
 * @tparam T 任意支持乘法的类型
 * @param a  输入值
 * @return   a * a
 */
template <typename T>
T square(T const & a)
{
  return a * a;
};

/**
 * @brief 将输入值限制在 [min, max] 区间内
 *
 * @param input 输入值
 * @param min   下限
 * @param max   上限
 * @return      截断后的值：小于 min 返回 min，大于 max 返回 max，否则返回 input
 */
double limit_min_max(double input, double min, double max);
}  // namespace tools

#endif  // MATH_TOOLS_HPP