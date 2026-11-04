/**
 * @file detector.hpp
 * @brief 基础识别器
 * @author o_O a17398481273@163.com
 * @date 2026/11/02
 * @version 1.0.0
 */

#ifndef DETECTOR_HPP
#define DETECTOR_HPP

#include <list>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

#include "armor.hpp"
#include "classifier.hpp"

namespace autoAim
{

/**
 * @brief 传统视觉装甲板检测器
 * @note 本类只负责检测（图像 → Armor），不涉及位姿解算。
 */
class Detector
{
public:
  /**
   * @brief 构造传统检测器
   * @param config_path 配置文件路径（阈值、角度/比例约束、保存路径等）
   * @param debug       是否开启调试（显示二值图、绘制结果、保存图像）
   * @note 配置文件需要
   * threshold             double
   * max_angle_error       double
   * max_lightbar_ratio    double
   * min_lightbar_length   double
   * min_armor_ratio       double
   * max_armor_ratio       double
   * max_side_ratio        double
   * min_confidence        double
   * max_rectangular_error double
   */
  Detector(const std::string & config_path, bool debug = true);

  /**
   * @brief 检测图像中的所有装甲板
   * @param bgr_img     输入 BGR 图像
   * @param frame_count 帧号（用于调试显示/保存，-1 表示无有效帧号）
   * @return            检测到的装甲板链表
   */
  std::list<Armor> detect(const cv::Mat & bgr_img, int frame_count = -1);

  /**
   * @brief 检测单个指定装甲板（用于跟踪中的局部验证/重检测）
   *
   * 在给定 Armor 的邻域内做检测，成功则就地更新 armor 并返回 true。
   *
   * @param armor   输入/输出的装甲板（成功时被更新）
   * @param bgr_img 输入 BGR 图像
   * @return true 表示在该区域成功检测到装甲板
   */
  bool detect(Armor & armor, const cv::Mat & bgr_img);

private:
  Classifier classifier_;  ///< 数字/图案分类器

  double threshold_;                                ///< 颜色分割阈值（二值化阈值）
  double max_angle_error_;                          ///< 灯条角度误差上限（超过则判为不合格灯条）
  double min_lightbar_ratio_, max_lightbar_ratio_;  ///< 灯条长宽比的合法区间
  double min_lightbar_length_;                      ///< 灯条最小长度（过滤过短噪声）
  double min_armor_ratio_, max_armor_ratio_;        ///< 装甲板 ratio 的合法区间（区分大小装甲板/过滤误配）
  double max_side_ratio_;                           ///< 左右灯条长度比上限（超过说明左右不对称，配对错误）
  double min_confidence_;                           ///< 最小置信度（分类器/检测结果的置信度下限）
  double max_rectangular_error_;                    ///< 矩形误差上限（灯条偏离垂直方向的最大允许角度）

  bool debug_;                                      ///< 调试开关
  std::string save_path_;                           ///< 调试图像/裁剪图案的保存路径

  /**
   * @brief 用 PCA 回归灯条角点，修正灯条端点/方向
   *
   * 参考 https://github.com/CSU-FYT-Vision/FYT2024_vision
   * 对灯条轮廓做主成分分析，得到更稳定的长轴方向与端点，减小噪声影响。
   *
   * @param lightbar 待修正的灯条（就地修改）
   * @param gray_img 灰度图（用于 PCA 计算）
   */
  void lightbar_points_corrector(Lightbar & lightbar, const cv::Mat & gray_img) const;

  /**
   * @brief 灯条几何合法性校验
   * @param lightbar 待校验灯条
   * @return true 表示通过（长宽比、长度、角度误差等在阈值内）
   */
  bool check_geometry(const Lightbar & lightbar) const;

  /**
   * @brief 装甲板几何合法性校验
   * @param armor 待校验装甲板
   * @return true 表示通过（ratio、side_ratio、rectangular_error 等在阈值内）
   */
  bool check_geometry(const Armor & armor) const;

  /**
   * @brief 校验装甲板编号/名称是否合法
   * @param armor 待校验装甲板
   * @return true 表示合法
   */
  bool check_name(const Armor & armor) const;

  /**
   * @brief 校验装甲板大小类型是否合法
   * @param armor 待校验装甲板
   * @return true 表示合法
   */
  bool check_type(const Armor & armor) const;

  /**
   * @brief 根据灯条轮廓区域的颜色判定装甲板颜色
   * @param bgr_img 原始 BGR 图像
   * @param contour 灯条轮廓
   * @return 判定出的颜色（red / blue / extinguish / purple）
   */
  Color get_color(const cv::Mat & bgr_img, const std::vector<cv::Point> & contour) const;

  /**
   * @brief 裁剪装甲板图案（数字区域），供分类器识别编号
   * @param bgr_img 原始 BGR 图像
   * @param armor   装甲板（提供位置/角点）
   * @return        裁剪出的图案图像
   */
  cv::Mat get_pattern(const cv::Mat & bgr_img, const Armor & armor) const;

  /**
   * @brief 根据几何量判定装甲板大小类型（small / big）
   * @param armor 装甲板
   * @return      判定出的大小类型
   */
  ArmorType get_type(const Armor & armor);

  /**
   * @brief 计算中心点的归一化坐标
   * @param bgr_img 原始图像（提供宽高）
   * @param center  像素坐标中心点
   * @return        归一化坐标（范围约 [0,1]）
   */
  cv::Point2f get_center_norm(const cv::Mat & bgr_img, const cv::Point2f & center) const;

  /**
   * @brief 保存单个装甲板的裁剪图案/调试信息到 save_path_
   * @param armor 待保存装甲板
   */
  void save(const Armor & armor) const;

  /**
   * @brief 调试显示：二值图 + 检测框 + 灯条 + 装甲板
   * @param binary_img  颜色分割后的二值图
   * @param bgr_img     原始 BGR 图像
   * @param lightbars   检测到的灯条
   * @param armors      检测到的装甲板
   * @param frame_count 帧号（用于窗口标题/保存文件名）
   */
  void show_result(
    const cv::Mat & binary_img, const cv::Mat & bgr_img, const std::list<Lightbar> & lightbars,
    const std::list<Armor> & armors, int frame_count) const;
};


}  // namespace autoAim

#endif  // DETECTOR_HPP