#ifndef IMG_TOOLS_HPP
#define IMG_TOOLS_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>

namespace tools
{
/**
 * @brief 在图像上绘制单个圆点
 *
 * 常用于标记关键点、中心点等。
 *
 * @param img    目标图像（就地绘制）
 * @param point  点的坐标
 * @param color  绘制颜色（BGR），默认红色 {0,0,255}
 * @param radius 圆的半径（像素），默认 3
 */
void draw_point(
  cv::Mat & img, const cv::Point & point, const cv::Scalar & color = {0, 0, 255}, int radius = 3);

/**
 * @brief 在图像上绘制一组整型点（按顺序连成折线/多边形）
 *
 * 常用于画轮廓、四角点围成的四边形等。
 *
 * @param img       目标图像（就地绘制）
 * @param points    点集（整数坐标），按顺序连接
 * @param color     绘制颜色（BGR），默认红色 {0,0,255}
 * @param thickness 线条粗细（像素），默认 2
 */
void draw_points(
  cv::Mat & img, const std::vector<cv::Point> & points, const cv::Scalar & color = {0, 0, 255},
  int thickness = 2);

/**
 * @brief 在图像上绘制一组浮点型点（按顺序连成折线/多边形）
 *
 * 与上一重载语义相同，但接受 cv::Point2f（浮点坐标），
 * 适合画关键点、亚像素角点等。内部通常四舍五入后绘制。
 *
 * @param img       目标图像（就地绘制）
 * @param points    点集（浮点坐标），按顺序连接
 * @param color     绘制颜色（BGR），默认红色 {0,0,255}
 * @param thickness 线条粗细（像素），默认 2
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
 * @param text       要绘制的文字内容
 * @param point      文字左下角起点坐标
 * @param color      文字颜色（BGR），默认黄色 {0,255,255}
 * @param font_scale 字体缩放系数，默认 1.0
 * @param thickness  字体线条粗细，默认 2
 */
void draw_text(
  cv::Mat & img, const std::string & text, const cv::Point & point,
  const cv::Scalar & color = {0, 255, 255}, double font_scale = 1.0, int thickness = 2);

}  // namespace tools

#endif  // IMG_TOOLS_HPP