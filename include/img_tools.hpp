#ifndef IMG_TOOLS_HPP
#define IMG_TOOLS_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace tools
{
/**
 * @brief 在图像上画一个实心圆点
 *
 * 常用于标记关键点、中心点等。
 *
 * @param img    目标图像（就地绘制）
 * @param point  圆心坐标（像素）
 * @param color  颜色（BGR），默认红色 {0,0,255}
 * @param radius 圆半径（像素），默认 3
 *
 * @note 用 cv::circle 填充绘制，img 被直接修改。
 */
void draw_point(
  cv::Mat & img, const cv::Point & point, const cv::Scalar & color = {0, 0, 255}, int radius = 3);

/**
 * @brief 在图像上按顺序连接一组整型点（画折线/多边形轮廓）
 *
 * 常用于画检测框、四角点围成的四边形等。
 *
 * @param img       目标图像（就地绘制）
 * @param points    点集（整数坐标），按顺序连接
 * @param color     颜色（BGR），默认红色 {0,0,255}
 * @param thickness 线宽（像素），默认 2
 *
 * @note 内部通常用 cv::drawContours 或 cv::polylines 实现。
 */
void draw_points(
  cv::Mat & img, const std::vector<cv::Point> & points, const cv::Scalar & color = {0, 0, 255},
  int thickness = 2);

/**
 * @brief 在图像上按顺序连接一组浮点型点（重载）
 *
 * 与整型版本语义相同，但接受 cv::Point2f，适合画关键点、亚像素角点。
 * 内部通常四舍五入转成 cv::Point 再绘制。
 *
 * @param img       目标图像（就地绘制）
 * @param points    点集（浮点坐标），按顺序连接
 * @param color     颜色（BGR），默认红色 {0,0,255}
 * @param thickness 线宽（像素），默认 2
 */
void draw_points(
  cv::Mat & img, const std::vector<cv::Point2f> & points, const cv::Scalar & color = {0, 0, 255},
  int thickness = 2);

/**
 * @brief 在图像上绘制文字
 *
 * 常用于调试时标注编号、置信度、坐标等信息。
 *
 * @param img        目标图像（就地绘制）
 * @param text       文字内容
 * @param point      文字左下角起点坐标（OpenCV putText 约定，不是左上角）
 * @param color      文字颜色（BGR），默认黄色 {0,255,255}
 * @param font_scale 字体缩放系数，默认 1.0
 * @param thickness  字体线宽，默认 2
 *
 * @note 内部用 cv::putText，字体为 FONT_HERSHEY_SIMPLEX。
 */
void draw_text(
  cv::Mat & img, const std::string & text, const cv::Point & point,
  const cv::Scalar & color = {0, 255, 255}, double font_scale = 1.0, int thickness = 2);

}  // namespace tools

#endif  // IMG_TOOLS_HPP