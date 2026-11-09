/**
 * @file        calibrate_headeye.hpp
 * @brief       手眼标定类
 * @author      o_O a17398481273@163.com
 * @date        2026-11-09
 * @version     1.0.0
 */

#ifndef CALIBRATE_HEADEYE_HPP
#define CALIBRATE_HEADEYE_HPP 

#include <string>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <opencv2/opencv.hpp>

#include "tools/logger.hpp"
#include "tools/yaml.hpp"

#if defined(_WIN32)
    #define API __declspec(dllexport)
#else
    #define API __attribute__((visibility("default")))
#endif

namespace sc::functions
{
/**
 * @brief 手眼标定类
 */
class API CalibrateHeadEye
{
private:
    std::string commonConfigPath_;     ///< 公共配置文件路径
    std::string path_;                 ///< 默认读取/储存标定照片路径
    std::string R_gimbal2imubodyName_; ///< R_gimbal2imubody在公共配置文件中的键名称
    std::string R_camera2gimbalName_;  ///< R_camera2gimbal在公共配置文件中的键名称
    std::string t_camera2gimbalName_;  ///< t_camera2gimbal在公共配置文件中的键名称
    std::string matrixName_;           ///< matrix在公共配置文件中的键名称
    std::string coeffsName_;           ///< coeffs在公共配置文件中的键名称

    int cols_;                         ///< 默认标定板行
    int rows_;                         ///< 默认标定板列
    int distance_;                     ///< 默认标定板距离(mm)

public:
/**
 * @brief 构造函数
 * 
 */
CalibrateHeadEye() = default;

/**
 * @brief 构造函数(配置文件)
 */
CalibrateHeadEye(std::string config);

/**
 * @brief 设置公共配置文件路径
 */
void setCommonConfigPath(std::string commonConfigPath);

/**
 * @brief 得到公共配置文件路径
 * @return 返回公共配置文件路径
 */
std::string getCommonConfigPath();

/**
 * @brief 设置默认读取/储存标定照片路径
 */
void setPath(std::string path);

/**
 * @brief 得到读取/储存标定照片路径
 * @return 返回读取/储存标定照片路径
 */
std::string getPath();

/**
 * @brief 设置 R_gimbal2imubody 在公共配置文件中的键名称
 */
void setRGimbal2ImuBodyName(std::string rGimbal2ImuBodyName);

/**
 * @brief 得到 R_gimbal2imubody 在公共配置文件中的键名称
 * @return 返回 R_gimbal2imubody 键名称
 */
std::string getRGimbal2ImuBodyName();

/**
 * @brief 设置 R_camera2gimbal 在公共配置文件中的键名称
 */
void setRCamera2GimbalName(std::string rCamera2GimbalName);

/**
 * @brief 得到 R_camera2gimbal 在公共配置文件中的键名称
 * @return 返回 R_camera2gimbal 键名称
 */
std::string getRCamera2GimbalName();

/**
 * @brief 设置 t_camera2gimbal 在公共配置文件中的键名称
 */
void setTCamera2GimbalName(std::string tCamera2GimbalName);

/**
 * @brief 得到 t_camera2gimbal 在公共配置文件中的键名称
 * @return 返回 t_camera2gimbal 键名称
 */
std::string getTCamera2GimbalName();

/**
 * @brief 设置默认标定板行
 */
void setCols(int cols);

/**
 * @brief 得到默认标定板行
 * @return 返回默认标定板行
 */
int getCols();

/**
 * @brief 设置默认标定板列
 */
void setRows(int rows);

/**
 * @brief 得到默认标定板列
 * @return 返回默认标定板列
 */
int getRows();

/**
 * @brief 设置默认标定板距离(mm)
 */
void setDistance(int distance);

/**
 * @brief 得到默认标定板距离(mm)
 * @return 返回默认标定板距离(mm)
 */
int getDistance();

/**
 * @brief 得到 matrix 在公共配置文件中的键名称
 * @return 返回 matrix 在公共配置文件中的键名称
 */
std::string getMatrixName();

/**
 * @brief 设置 matrix 在公共配置文件中的键名称
 */
void setMatrixName(std::string matrixName);

/**
 * @brief 得到 coeffs 在公共配置文件中的键名称
 * @return 返回 coeffs 在公共配置文件中的键名称
 */
std::string getCoeffsName();

/**
 * @brief 设置 coeffs 在公共配置文件中的键名称
 */
void setCoeffsName(std::string coeffsName);

/**
 * @brief 开始标定
 * @return 返回识别结果字符串，用于前端显示
 */
std::string start();



};

} // namespace sc::functions

#endif // CALIBRATE_HEADEYE_HPP