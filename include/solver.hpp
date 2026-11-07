/**
 * @file solver.hpp
 * @brief 基于同济自瞄的装甲板位置解析管理器
 * @author o_O a17398481273@163.com
 * @date 2026/11/01
 * @version 1.0.0
 * @note 正常使用识别程序需要gimbal(下位机程序)程序
 */


#ifndef SOLVER_HPP
#define SOLVER_HPP

#include <Eigen/Dense> 
#include <Eigen/Geometry>
#include <opencv2/core/eigen.hpp>

#include "armor.hpp"

namespace autoAim
{

/**
 * @brief 装甲板位姿解算器
 *
 * @note 解算结果写入 Armor 的 xyz_in_world / ypr_in_world 等字段。
 * @note 非线程安全：set_R_gimbal2world 与 solve 并发调用需外部加锁。
 * @note 配置文件中需要有R_gimbal2imubody，R_camera2gimbal，t_camera2gimbal键
 *       (在公共配置文件中)
 */
class Solver
{
public:
  /**
   * @brief 构造解算器
   * @param config_path 配置文件路径（相机内参、畸变系数、外参等）
   */
  explicit Solver(const std::string & config_path);

  /**
   * @brief 获取当前云台到世界的旋转矩阵
   * @return 3x3 旋转矩阵 R_gimbal2world
   */
  Eigen::Matrix3d R_gimbal2world() const;

  /**
   * @brief 设置云台到世界的旋转
   * @param q 云台姿态四元数（gimbal -> world）
   */
  void set_R_gimbal2world(const Eigen::Quaterniond & q);

  /**
   * @brief 设置云台到世界的旋转
   * 
   * @param q 云台姿态四元数（gimbal -> world）
   */
  void set_R_gimbal2world(const std::vector<float> & q);

  /**
   * @brief 解算单个装甲板的 3D 位姿
   *
   * 读取 armor.points（图像关键点），结合相机模型与当前云台姿态，
   * 计算装甲板在云台系/世界系下的位置与姿态，并写回 armor 的
   * xyz_in_gimbal / xyz_in_world / ypr_in_gimbal / ypr_in_world / ypd_in_world 等字段。
   *
   * @param armor 待解算装甲板（输入关键点，输出位姿；就地修改）
   */
  void solve(Armor & armor) const;

  /**
   * @brief 将世界系下的装甲板位姿重投影回图像像素坐标
   *
   * 用于验证解算结果、计算重投影误差、或可视化预测框。
   *
   * @param xyz_in_world 装甲板中心在世界系下的位置
   * @param yaw          装甲板朝向（世界系 yaw 角，rad）
   * @param type         装甲板大小类型（决定物理尺寸）
   * @param name         装甲板编号/身份（决定关键点布局）
   * @return             重投影得到的四个像素点
   */
  std::vector<cv::Point2f> reproject_armor(
    const Eigen::Vector3d & xyz_in_world, double yaw, ArmorType type, ArmorName name) const;

  /**
   * @brief 计算前哨站（outpost）的重投影误差
   *
   * 前哨站结构特殊，给定 pitch 假设下，评估解算位姿与实际检测的吻合程度，
   * 用于优化 pitch 或筛选解。
   *
   * @param armor 装甲板（含检测点与解算结果）
   * @param picth 假设的 pitch 角（rad）——注意参数名拼写为 picth
   * @return      重投影误差（像素）
   */
  double oupost_reprojection_error(Armor armor, const double & picth);

  /**
   * @brief 将世界系 3D 点批量投影到像素坐标
   * @param worldPoints 世界系 3D 点集
   * @return            对应的像素点集
   */
  std::vector<cv::Point2f> world2pixel(const std::vector<cv::Point3f> & worldPoints);

private:
  cv::Mat camera_matrix_;             ///< 相机内参矩阵 K（3x3）
  cv::Mat distort_coeffs_;            ///< 相机畸变系数（k1,k2,p1,p2,k3...）
  Eigen::Matrix3d R_gimbal2imubody_;  ///< 云台到 IMU 本体的旋转（安装/标定外参）
  Eigen::Matrix3d R_camera2gimbal_;   ///< 相机到云台的旋转外参
  Eigen::Vector3d t_camera2gimbal_;   ///< 相机到云台的平移外参（m）
  Eigen::Matrix3d R_gimbal2world_;    ///< 云台到世界的旋转（由 set_R_gimbal2world 更新）


  /**
   * @brief 优化装甲板 yaw 角
   *
   * 在 PnP 初值基础上，沿 yaw 方向搜索使重投影误差最小的角度，
   * 修正装甲板朝向估计（PnP 对 yaw 通常不敏感/不稳定）。
   *
   * @param armor 待优化装甲板（修改 yaw 相关字段）
   */
  void optimize_yaw(Armor & armor) const;

  /**
   * @brief 计算装甲板在给定 yaw / 倾角下的重投影误差
   * @param armor    装甲板（含检测点）
   * @param yaw      假设的 yaw 角（rad）
   * @param inclined 倾角（rad，用于前哨站等倾斜目标）
   * @return         重投影误差（像素）
   */
  double armor_reprojection_error(const Armor & armor, double yaw, const double & inclined) const;

  /**
   * @brief SJTU 代价函数（参考上海交大开源方案的重投影代价）
   *
   * 比较参考像素点 cv_refs 与投影点 cv_pts 的差异，结合倾角 inclined
   * 计算综合代价，用于 yaw/倾角优化。
   *
   * @param cv_refs  参考像素点（检测到的关键点）
   * @param cv_pts   投影得到的像素点
   * @param inclined 倾角（rad）
   * @return         代价（越小越好）
   */
  double SJTU_cost(
    const std::vector<cv::Point2f> & cv_refs, const std::vector<cv::Point2f> & cv_pts,
    const double & inclined) const;
};

}  // namespace autoAim

#endif  // SOLVER_HPP