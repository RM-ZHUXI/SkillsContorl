/**
 * @file armor.hpp
 * @brief 装甲板分类
 * @author o_O a17398481273@163.com
 * @date 2026/11/02
 * @version 1.0.0
 */

#ifndef ARMOR_HPP
#define ARMOR_HPP

#include <Eigen/Dense>
#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace autoAim
{

/**
 * @brief 颜色选择
 * 
 */
enum Color
{
  red,         ///< 红色
  blue,        ///< 蓝色
  extinguish,  ///< 熄灭/未激活
  purple       ///< 紫色
};
const std::vector<std::string> COLORS = {"red", "blue", "extinguish", "purple"};

/**
 * @brief 装甲板大小
 * 
 */
enum ArmorType
{
  big,         ///< 大装甲板
  small        ///< 小装甲板
};
const std::vector<std::string> ARMOR_TYPES = {"big", "small"};

/**
 * @brief 装甲兵名称
 * 
 */
enum ArmorName
{
  one,        ///< 1号
  two,        ///< 2号
  three,      ///< 3号
  four,       ///< 4号
  five,       ///< 5号
  sentry,     ///< 哨兵
  outpost,    ///< 前哨站
  base,       ///< 基地
  not_armor   ///< 非装甲板
};
const std::vector<std::string> ARMOR_NAMES = {"one","two","three","four","five","sentry","outpost","base","not_armor"};

/**
 * @brief 装甲板打击优先级
 *
 */
enum ArmorPriority
{
  first = 1,  ///< 最高优先级（1）
  second,     ///< 第二优先级（2）
  third,      ///< 第三优先级（3）
  forth,      ///< 第四优先级（4）
  fifth       ///< 第五优先级（5）
};

/**
 * @brief 属性查找表
 * 
 */
// clang-format off
const std::vector<std::tuple<Color, ArmorName, ArmorType>> armor_properties = {
  {blue, sentry, small},     {red, sentry, small},     {extinguish, sentry, small},
  {blue, one, small},        {red, one, small},        {extinguish, one, small},
  {blue, two, small},        {red, two, small},        {extinguish, two, small},
  {blue, three, small},      {red, three, small},      {extinguish, three, small},
  {blue, four, small},       {red, four, small},       {extinguish, four, small},
  {blue, five, small},       {red, five, small},       {extinguish, five, small},
  {blue, outpost, small},    {red, outpost, small},    {extinguish, outpost, small},
  {blue, base, big},         {red, base, big},         {extinguish, base, big},      {purple, base, big},       
  {blue, base, small},       {red, base, small},       {extinguish, base, small},    {purple, base, small},    
  {blue, three, big},        {red, three, big},        {extinguish, three, big}, 
  {blue, four, big},         {red, four, big},         {extinguish, four, big},  
  {blue, five, big},         {red, five, big},         {extinguish, five, big}};
// clang-format on

/**
 * @brief 灯条（Light Bar）
 */
struct Lightbar
{
  std::size_t id;                   ///< 灯条编号（在同一帧内唯一，用于与其它灯条配对）
  Color color;                      ///< 灯条颜色
  cv::Point2f center;               ///< 灯条中心点坐标
  cv::Point2f top;                  ///< 灯条上端点坐标
  cv::Point2f bottom;               ///< 灯条下端点坐标
  cv::Point2f top2bottom;           ///< 从上端点到下端点的向量（top - bottom），表示灯条方向与长度
  std::vector<cv::Point2f> points;  ///< 灯条轮廓点集（通常为拟合后的四个角点或原始轮廓点）
  double angle;                     ///< 灯条角度（相对于竖直/水平方向的倾斜角，单位通常为度）
  double angle_error;               ///< 角度误差
  double length;                    ///< 灯条长度（上端点到下端点的距离）
  double width;                     ///< 灯条宽度（灯条短边方向的厚度）
  double ratio;                     ///< 长宽比（length / width）
  cv::RotatedRect rotated_rect;     ///< 灯条的最小外接旋转矩形（OpenCV 的 RotatedRect，含中心、尺寸、角度）

  /**
   * @brief 构造灯条
   * @param rotated_rect 灯条的最小外接旋转矩形，用于初始化几何属性
   * @param id           灯条编号
   */
  Lightbar(const cv::RotatedRect & rotated_rect, std::size_t id);

  /**
   * @brief 默认构造函数
   *
   * 创建一个空灯条，成员未初始化，供容器/数组等场景使用。
   * @warning 默认构造的灯条成员值未定义，使用前必须显式赋值。
   */
  Lightbar() {};
};

/**
 * @brief 装甲板（Armor）
 */
struct Armor
{
  Color color;                        ///< 装甲板颜色
  Lightbar left, right;               ///< 左、右灯条
  cv::Point2f center;                 ///< 装甲板中心
  cv::Point2f center_norm;            ///< 归一化中心坐标
  std::vector<cv::Point2f> points;    ///< 装甲板关键点/轮廓点

  double ratio;                       ///< 两灯条中点连线长度 / 长灯条长度，用于判断装甲板宽高比例是否合理
  double side_ratio;                  ///< 长灯条与短灯条长度之比，用于判断左右灯条长度是否对称
  double rectangular_error;           ///< 灯条与中点连线夹角同 π/2 的差值，衡量装甲板是否近似矩形

  ArmorType type;                     ///< 装甲板大小类型
  ArmorName name;                     ///< 装甲板编号/身份（1~5 号、哨兵、前哨站、基地等）
  ArmorPriority priority;             ///< 打击优先级（数值越小越优先）
  int class_id;                       ///< YOLO 分类头输出的类别 id（对应 armor_properties 表下标）
  cv::Rect box;                       ///< 检测框（轴对齐矩形，图像像素坐标）
  cv::Mat pattern;                    ///< 装甲板图案/数字的裁剪图像（用于数字识别或二次校验）
  double confidence;                  ///< 检测置信度（0~1）
  bool duplicated;                    ///< 是否重复检测（用于去重，true 表示被判定为重复目标）

  Eigen::Vector3d xyz_in_gimbal;      ///< 云台坐标系下位置（单位：m）
  Eigen::Vector3d xyz_in_world;       ///< 世界坐标系下位置（单位：m）
  Eigen::Vector3d ypr_in_gimbal;      ///< 云台坐标系下姿态 yaw/pitch/roll（单位：rad）
  Eigen::Vector3d ypr_in_world;       ///< 世界坐标系下姿态 yaw/pitch/roll（单位：rad）
  Eigen::Vector3d ypd_in_world;       ///< 世界坐标系下球坐标（yaw/pitch/distance）

  double yaw_raw;                     ///< 原始 yaw 角（单位：rad，未经过滤波/补偿）

  /**
   * @brief 由左右灯条构造装甲板
   *
   * 传统几何方法：根据两根灯条的位置、长度、角度推断装甲板中心、
   * 宽高比、矩形误差等，并据此判定 type/name 等属性。
   * @param left  左灯条
   * @param right 右灯条
   */
  Armor(const Lightbar & left, const Lightbar & right);

  /**
   * @brief 由 YOLO 检测结果构造装甲板（无偏移）
   * @param class_id        分类类别 id
   * @param confidence      置信度
   * @param box             检测框
   * @param armor_keypoints 装甲板关键点
   */
  Armor(
    int class_id, float confidence, const cv::Rect & box, std::vector<cv::Point2f> armor_keypoints);

  /**
   * @brief 由 YOLO 检测结果构造装甲板（带图像偏移）
   *
   * 在裁剪/缩放后的图像上推理时，用 offset 将坐标还原到原图坐标系。
   * @param class_id        分类类别 id
   * @param confidence      置信度
   * @param box             检测框
   * @param armor_keypoints 装甲板关键点
   * @param offset          坐标偏移量（原图中的裁剪原点）
   */
  Armor(
    int class_id, float confidence, const cv::Rect & box, std::vector<cv::Point2f> armor_keypoints,
    cv::Point2f offset);

  /**
   * @brief 由颜色 id + 编号 id + 检测结果构造装甲板
   * @param color_id        颜色类别 id
   * @param num_id          编号类别 id
   * @param confidence      置信度
   * @param box             检测框
   * @param armor_keypoints 装甲板关键点
   */
  Armor(
    int color_id, int num_id, float confidence, const cv::Rect & box,
    std::vector<cv::Point2f> armor_keypoints);

  /**
   * @brief 由颜色 id + 编号 id + 检测结果构造装甲板（带图像偏移）
   * @param color_id        颜色类别 id
   * @param num_id          编号类别 id
   * @param confidence      置信度
   * @param box             检测框
   * @param armor_keypoints 装甲板关键点
   * @param offset          坐标偏移量（原图中的裁剪原点）
   */
  Armor(
    int color_id, int num_id, float confidence, const cv::Rect & box,
    std::vector<cv::Point2f> armor_keypoints, cv::Point2f offset);
};

}  // namespace autoAim

#endif  // ARMOR_HPP