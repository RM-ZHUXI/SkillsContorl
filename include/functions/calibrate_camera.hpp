/**
 * @file        assign.hpp
 * @brief       负责交互skill链管理器
 * @author      o_O a17398481273@163.com
 * @date        2026-10-14
 * @version     1.0.0
 * @par         小功能点 -- 摄像头标定
 */

#ifndef CALIBRATE_CAMERA_HPP
#define CALIBRATE_CAMERA_HPP

#include <string>
#include <opencv2/opencv.hpp>

#include "tools/logger.hpp"
#include "tools/memory.hpp"
#include "tools/yaml.hpp"

namespace sc::functions
{

#if defined(_WIN32)
    #define API __declspec(dllexport)
#else
    #define API __attribute__((visibility("default")))
#endif

class API CalibrateCamera
{
private:
    std::string commonConfigPath_;     ///< 公共配置文件路径
    std::string path_;                 ///< 默认读取/储存标定照片路径
    std::string matrixName_;           ///< matrix在公共配置文件中的键名称
    std::string coeffsName_;           ///< coeffs在公共配置文件中的键名称
    int cols_;                         ///< 默认标定板行
    int rows_;                         ///< 默认标定板列
    int distance_;                     ///< 默认标定板距离(mm)

private:
    /**
     * @brief 搜索路径中的图片进行标定程序,并且直接写入commonConfig键中
     * @return 返回识别结果字符串，用于前端显示
     */
    std::string runPath();

public:
    /**
     * @brief 构造函数
     */
    CalibrateCamera(std::string config);

    /**
     * @brief 得到公共配置文件路径
     * @return 返回公共配置文件路径
     */
    std::string getCommonConfigPath();

    /**
     * @brief 设置公共配置文件路径
     */
    void setCommonConfigPath(std::string commonConfigPath);

    /**
     * @brief 得到读取/储存标定照片路径
     * @return 返回读取/储存标定照片路径
     */
    std::string getPath();

    /**
     * @brief 设置默认读取/储存标定照片路径
     */
    void setPath(std::string path);

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
     * @brief 得到标定板列数
     * @return 返回标定板列数
     */
    int getCols();

    /**
     * @brief 设置标定板列数
     */
    void setCols(int cols);

    /**
     * @brief 得到标定板行数
     * @return 返回标定板行数
     */
    int getRows();

    /**
     * @brief 设置标定板行数
     */
    void setRows(int rows);

    /**
     * @brief 得到标定板距离(mm)
     * @return 返回标定板距离(mm)
     */
    int getDistance();

    /**
     * @brief 设置默认标定板距离(mm)
     */
    void setDistance(int distance);

    /**
     * @brief 开始标定
     * @return 返回识别结果字符串，用于前端显示
     */
    std::string start();
}; // CalibrateCamera;

} // namespace functions

#endif