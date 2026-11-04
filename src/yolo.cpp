#include "yolo.hpp"

#include "yolov5.hpp"

namespace autoAim
{
YOLO::YOLO(const std::string & config_path, bool debug)
{
  auto yaml = sc::tools::load(config_path);
  auto yolo_name = yaml["yolo_name"].as<std::string>();

  if (yolo_name == "yolov5") {
    yolo_ = std::make_unique<YOLOV5>(config_path, debug);
  }
  else {
    throw std::runtime_error("Unknown yolo name: " + yolo_name + "!");
  }

}

std::list<Armor> YOLO::detect(const cv::Mat & img, int frame_count)
{
  return yolo_->detect(img, frame_count);
}

std::list<Armor> YOLO::postprocess(double scale, cv::Mat & output, const cv::Mat & bgr_img, int frame_count)
{
  return yolo_->postprocess(scale, output, bgr_img, frame_count);
}

}  // namespace autoAim