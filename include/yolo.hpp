/**
 * @file yolo.hpp
 * @brief 模型选择类
 * @author o_O a17398481273@163.com
 * @date 2026/11/02
 * @version 1.0.0
 */

#ifndef YOLO_HPP
#define YOLO_HPP

#include <opencv2/opencv.hpp>
#include "armor.hpp"
#include "skillscontrol.hpp"

namespace autoAim
{

/**
 * @brief YOLO 检测器抽象基类（策略接口）
 *
 * @note 所有方法均为纯虚函数，派生类必须全部实现。
 * @note 具体实现的线程安全性由派生类自行保证，多线程调用前需确认。
 */
class YOLOBase
{
public:
  /**
   * @brief 对输入图像执行完整检测
   *
   * @param img         输入图像（BGR，只读）
   * @param frame_count 帧号（-1 表示无有效帧号）
   * @return            检测到的装甲板链表
   */
  virtual std::list<Armor> detect(const cv::Mat & img, int frame_count) = 0;

  /**
   * @brief 将神经网络原始输出解码为装甲板列表
   *
   * @param scale       缩放比例（模型输出坐标 × scale = 原图坐标）
   * @param output      神经网络原始输出张量（非 const，后处理可能原地修改）
   * @param bgr_img     原始 BGR 图像（只读，用于裁剪图案/颜色校验/调试绘制）
   * @param frame_count 帧号，用于跟踪/去重/日志
   * @return            解码后的装甲板链表
   */
  virtual std::list<Armor> postprocess(double scale, cv::Mat & output, const cv::Mat & bgr_img, int frame_count) = 0;

  /**
   * @brief 虚析构函数
   *
   */
  virtual ~YOLOBase() = default;
};

/**
 * @brief yolo识别装甲板类
 * 
 */
class YOLO
{
public:

/**
 * @brief YOLO构造类
 * 
 * @param config_path 配置文件路径
 * @param debug       是否开启调试模式
 * @note  config必须存在 yolo_name （选择yolo模式）
 */
YOLO(const std::string & config_path, bool debug = true);

/**
 * @brief 识别装甲板主要方法
 * 
 * @param img               输入图像
 * @param frame_count       帧号
 * @return std::list<Armor> 返回装甲板链
 */
std::list<Armor> detect(const cv::Mat & img, int frame_count = -1);

/**
 * @brief 后处理主要方法
 *
 * @param scale             缩放比例
 * @param output            神经网络原始输出
 * @param bgr_img           原始 BGR 图像
 * @param frame_count       帧号
 * @return std::list<Armor> 返回装甲板链表
 */
std::list<Armor> postprocess(double scale, cv::Mat & output, const cv::Mat & bgr_img, int frame_count);

private:

std::unique_ptr<YOLOBase> yolo_; ///< yolo检测器抽象基类（策略接口）
};    


} // namespace autoAim

#endif