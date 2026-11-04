/**
 * @file yolov5.hpp
 * @brief yolov5模型类
 * @author o_O a17398481273@163.com
 * @date 2026/11/02
 * @version 1.0.0
 */

 #ifndef YOLOV5_HPP
#define YOLOV5_HPP

#include <list>
#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <string>
#include <vector>

#include "armor.hpp"
#include "detector.hpp"
#include "yolo.hpp"

namespace autoAim
{

/**
 * @brief YOLOv5 检测器实现（基于 OpenVINO 推理后端）
 */
class YOLOV5 : public YOLOBase
{
public:
  /**
   * @brief 构造 YOLOv5 检测器
   * @param config_path 配置文件路径（模型路径、设备、阈值、ROI/传统开关等）
   * @param debug       是否开启调试（保存图像、绘制检测框等）
   * @note config必须存在
   * yolov5_model_path std::string
   * device            std::string
   * threshold         double
   * min_confidence    double
   * roi-x             int 
   * roi-y             int
   * roi-width         int 
   * roi-height        int
   * use_roi           bool
   * use_traditional   bool
   */
  YOLOV5(const std::string & config_path, bool debug);

  /**
   * @brief 执行完整检测（预处理 + 推理 + 后处理）
   * @param bgr_img     输入 BGR 图像
   * @param frame_count 帧号（用于跟踪/去重/日志，-1 表示无有效帧号）
   * @return            检测到的装甲板链表
   */
  std::list<Armor> detect(const cv::Mat & bgr_img, int frame_count) override;

  /**
   * @brief 将 OpenVINO 推理输出解码为装甲板列表
   * @param scale       缩放比例（模型输出坐标 × scale = 原图坐标）
   * @param output      网络原始输出张量（非 const，可能被原地修改）
   * @param bgr_img     原始 BGR 图像（用于裁剪 pattern / 颜色校验 / 调试）
   * @param frame_count 帧号
   * @return            解码后的装甲板链表
   */
  std::list<Armor> postprocess(
    double scale, cv::Mat & output, const cv::Mat & bgr_img, int frame_count) override;

private:
  std::string device_;                 ///< 推理设备
  std::string model_path_;             ///< 模型储存目录        
  std::string save_path_;              ///< 检测结果保存目录 
  std::string debug_path_;             ///< 与调试图像输出目录      
  bool debug_;                         ///< 调试开关
  bool use_roi_;                       ///< 是否启用 ROI 推理
  bool use_traditional_;               ///< 是否启用传统视觉辅助

  const int class_num_ = 13;           ///< 模型分类类别数（与 armor_properties 表长度对应）
  const float nms_threshold_ = 0.3;    ///< NMS 交并比阈值，去除重叠框
  const float score_threshold_ = 0.7;  ///< 置信度阈值，低于此值的检测被过滤
  double min_confidence_;              ///< 最小置信度（可配置）
  double binary_threshold_;            ///< 二值化阈值（传统视觉用）

  ov::Core core_;                      ///< OpenVINO 运行时核心对象
  ov::CompiledModel compiled_model_;   ///< 编译后的模型（加载到指定 device 上）

  cv::Rect roi_;                       ///< ROI 区域（启用 use_roi_ 时使用）
  cv::Point2f offset_;                 ///< ROI 原点在原图中的偏移，用于坐标还原
  cv::Mat tmp_img_;                    ///< 临时图像缓存（预处理/绘制中间结果）

  Detector detector_;                  ///< 传统灯条检测器（use_traditional_ 时启用）

  friend class MultiThreadDetector;    ///< 允许多线程检测器访问私有成员（复用模型/避免重复加载）

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
   * @brief 计算中心点的归一化坐标
   * @param bgr_img 原始图像（提供宽高）
   * @param center  图像坐标系下的中心点
   * @return        归一化坐标（相对图像宽高，范围约 [0,1]）
   */
  cv::Point2f get_center_norm(const cv::Mat & bgr_img, const cv::Point2f & center) const;

  /**
   * @brief 解析网络输出为装甲板（postprocess 的核心实现）
   * @param scale       缩放比例
   * @param output      网络原始输出
   * @param bgr_img     原始图像
   * @param frame_count 帧号
   * @return            装甲板链表
   */
  std::list<Armor> parse(double scale, cv::Mat & output, const cv::Mat & bgr_img, int frame_count);

  /**
   * @brief 保存单个装甲板（裁剪图案/调试图像到 save_path_）
   * @param armor 待保存装甲板
   */
  void save(const Armor & armor) const;

  /**
   * @brief 在原图上绘制检测结果（框、关键点、编号、置信度等）
   * @param img         原始图像
   * @param armors      检测结果
   * @param frame_count 帧号（用于命名调试图像）
   */
  void draw_detections(const cv::Mat & img, const std::list<Armor> & armors, int frame_count) const;

  /**
   * @brief Sigmoid 激活函数
   * @param x 输入
   * @return  1 / (1 + e^{-x})
   */
  double sigmoid(double x);
};
}  // namespace autoAim

#endif  //YOLOV5_HPP