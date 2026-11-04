/**
 * @file classifier.hpp
 * @brief 分类器
 * @author o_O a17398481273@163.com
 * @date 2026/11/02
 * @version 1.0.0
 */

#ifndef CLASSIFIER_HPP
#define CLASSIFIER_HPP

#include <opencv2/opencv.hpp>
#include <openvino/openvino.hpp>
#include <string>

#include "armor.hpp"

namespace autoAim
{
/**
 * @brief 装甲板数字/图案分类器
 * 提供两种推理后端：
 *   - classify()   ：基于 OpenCV DNN（cv::dnn::Net）
 *   - ovclassify() ：基于 OpenVINO（ov::Core / ov::CompiledModel）
 *
 * @note 被 Detector 与 YOLO 系列共用（Detector 中持有 Classifier 成员），
 *       用于将检测结果从"几何上的装甲板"补全为"带编号的装甲板"。
 * 
 * @note 配置文件需要有
 *       classify_model std::string
 */
class Classifier
{
public:
  /**
   * @brief 构造分类器
   * @param config_path 配置文件路径（模型路径、设备、输入尺寸、阈值等）
   */
  explicit Classifier(const std::string & config_path);

  /**
   * @brief 使用 OpenCV DNN 后端对装甲板图案进行分类
   *
   * 读取 armor.pattern，前向推理，将识别出的编号写回 armor.name
   * （可能同时更新 confidence）。
   *
   * @param armor 待分类装甲板（就地修改 name / confidence 等字段）
   */
  void classify(Armor & armor);

  /**
   * @brief 使用 OpenVINO 后端对装甲板图案进行分类
   *
   * 与 classify() 语义相同，仅推理框架不同；通常推理更快，
   * 适合部署阶段使用。
   *
   * @param armor 待分类装甲板（就地修改 name / confidence 等字段）
   */
  void ovclassify(Armor & armor);

private:
  cv::dnn::Net net_;                  ///< OpenCV DNN 模型网络（classify() 使用）
  ov::Core core_;                     ///< OpenVINO 运行时核心对象（ovclassify() 使用）
  ov::CompiledModel compiled_model_;  ///< OpenVINO 编译后的模型（加载到指定 device）
};
}  // namespace autoAim

#endif  // CLASSIFIER_HPP