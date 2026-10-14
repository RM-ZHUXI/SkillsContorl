#include "functions/calibrate_camera.hpp"

namespace sc::functions
{

CalibrateCamera::CalibrateCamera(std::string config)
{
    auto yaml = sc::tools::load(config);
    this->commonConfigPath_ = sc::tools::read<std::string>(yaml,"common_configPath");
    this->path_ = sc::tools::read<std::string>(yaml,"calibrate_camera_path");
    this->matrixName_ = sc::tools::read<std::string>(yaml,"calibrate_camera_matrix_name");
    this->coeffsName_ = sc::tools::read<std::string>(yaml,"calibrate_camera_coeffs_name");
    this->rows_ = sc::tools::read<int>(yaml,"calibrate_camera_cols");
    this->cols_ = sc::tools::read<int>(yaml,"calibrate_camera_rows");
    this->distance_ = sc::tools::read<int>(yaml,"calibrate_camera_center_distance_mm");
}

std::string CalibrateCamera::getCommonConfigPath()
{
    return commonConfigPath_;
}

void CalibrateCamera::setCommonConfigPath(std::string commonConfigPath)
{
    commonConfigPath_ = std::move(commonConfigPath);
}

std::string CalibrateCamera::getPath()
{
    return path_;
}

void CalibrateCamera::setPath(std::string path)
{
    path_ = std::move(path);
}

std::string CalibrateCamera::getMatrixName()
{
    return matrixName_;
}

void CalibrateCamera::setMatrixName(std::string matrixName)
{
    matrixName_ = std::move(matrixName);
}

std::string CalibrateCamera::getCoeffsName()
{
    return coeffsName_;
}

void CalibrateCamera::setCoeffsName(std::string coeffsName)
{
    coeffsName_ = std::move(coeffsName);
}

int CalibrateCamera::getCols()
{
    return cols_;
}

void CalibrateCamera::setCols(int cols)
{
    cols_ = cols;
}

int CalibrateCamera::getRows()
{
    return rows_;
}

void CalibrateCamera::setRows(int rows)
{
    rows_ = rows;
}

int CalibrateCamera::getDistance()
{
    return distance_;
}

void CalibrateCamera::setDistance(int distance)
{
    distance_ = distance;
}

std::string CalibrateCamera::runPath()
{
    // 圆点板 pattern_size 是「圆心个数」，行列含义与棋盘格一致
    const cv::Size patternSize(cols_, rows_);
    const float squareSize = static_cast<float>(distance_);  // 相邻圆心间距(mm)

    std::vector<cv::Point3f> objCorners;
    objCorners.reserve(patternSize.area());
    for (int i = 0; i < patternSize.height; ++i)
        for (int j = 0; j < patternSize.width; ++j)
            objCorners.emplace_back(j * squareSize, i * squareSize, 0.0f);

    std::vector<std::vector<cv::Point3f>> objectPoints;
    std::vector<std::vector<cv::Point2f>> imagePoints;

    int successCount = 0;
    int failCount = 0;
    std::string failedList;
    cv::Size imageSize;

    std::error_code ec;
    if (!std::filesystem::exists(path_, ec) ||
        !std::filesystem::is_directory(path_, ec)) {
        sc::tools::logger()->error("[Calibrate] 路径不存在或不是目录: {}", path_);
        return "标定失败：路径不存在或不是目录 " + path_;
    }

    for (const auto & entry : std::filesystem::directory_iterator(path_, ec)) {
        if (ec) break;

        std::string ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
            continue;

        const std::string file = entry.path().string();

        // 圆点板检测用彩色图即可（灰度也行，但彩色对反光/对比度更友好一点）
        cv::Mat img = cv::imread(file, cv::IMREAD_COLOR);
        if (img.empty()) {
            ++failCount;
            failedList += file + "; ";
            sc::tools::logger()->warn("[Calibrate] 无法读取图片: {}", file);
            continue;
        }

        std::vector<cv::Point2f> corners;
        const bool found = cv::findCirclesGrid(
            img, patternSize, corners,
            cv::CALIB_CB_SYMMETRIC_GRID | cv::CALIB_CB_CLUSTERING);

        if (!found) {
            ++failCount;
            failedList += file + "; ";
            sc::tools::logger()->warn("[Calibrate] 未检测到圆点板: {}", file);
            continue;
        }

        // 圆点板不需要 cornerSubPix（圆心本身已经是亚像素精度）

        imagePoints.push_back(corners);
        objectPoints.push_back(objCorners);
        if (imageSize.empty()) imageSize = img.size();
        ++successCount;
    }

    if (imagePoints.size() < 3) {
        std::string msg = "标定失败：有效图片不足3张，识别成功" +
                          std::to_string(successCount) + "张，识别失败" +
                          std::to_string(failCount) + "张";
        if (!failedList.empty()) msg += "，失败文件: " + failedList;
        sc::tools::logger()->error("[Calibrate] {}", msg);
        return msg;
    }

    cv::Mat cameraMatrix, distCoeffs;
    std::vector<cv::Mat> rvecs, tvecs;
    auto criteria = cv::TermCriteria(
        cv::TermCriteria::COUNT + cv::TermCriteria::EPS, 100, DBL_EPSILON);
    const double rms = cv::calibrateCamera(
        objectPoints, imagePoints, imageSize,
        cameraMatrix, distCoeffs, rvecs, tvecs,
        cv::CALIB_FIX_K3, criteria);

    // ---------- 写入 commonConfig ----------
    YAML::Node root = sc::tools::load(commonConfigPath_);

    // camera_matrix：展平成 9 元素的一维数组（行优先），Flow 风格 → 一行输出
    YAML::Node matNode(YAML::NodeType::Sequence);
    for (int r = 0; r < cameraMatrix.rows; ++r)
        for (int c = 0; c < cameraMatrix.cols; ++c)
            matNode.push_back(cameraMatrix.at<double>(r, c));
    matNode.SetStyle(YAML::EmitterStyle::Flow);

    // distort_coeffs：一维数组，同样用 Flow 风格 → 一行输出
    YAML::Node coeffNode(YAML::NodeType::Sequence);
    for (int i = 0; i < static_cast<int>(distCoeffs.total()); ++i)
        coeffNode.push_back(distCoeffs.at<double>(i));
    coeffNode.SetStyle(YAML::EmitterStyle::Flow);

    if (root[matrixName_]) {
        sc::tools::overwrite(root, matrixName_, matNode);
    } else {
        root[matrixName_] = matNode;
    }

    if (root[coeffsName_]) {
        sc::tools::overwrite(root, coeffsName_, coeffNode);
    } else {
        root[coeffsName_] = coeffNode;
    }

    sc::tools::save(commonConfigPath_, root);

    // ---------- 组装返回字符串 ----------
    std::string msg = "标定成功：识别成功" + std::to_string(successCount) +
                      "张，识别失败" + std::to_string(failCount) + "张";
    if (!failedList.empty()) msg += "，失败文件: " + failedList;
    msg += "，RMS重投影误差: " + std::to_string(rms) + " 像素";
    msg += "，已写入 " + matrixName_ + "(m) 和 " + coeffsName_ + "(d)";

    // 附上具体标定结果（camera_matrix 展平，distort_coeffs 顺序输出）
    msg += "\n" + matrixName_ + ": [";
    for (int r = 0; r < cameraMatrix.rows; ++r) {
        for (int c = 0; c < cameraMatrix.cols; ++c) {
            if (r != 0 || c != 0) msg += ", ";
            msg += std::to_string(cameraMatrix.at<double>(r, c));
        }
    }
    msg += "]";

    msg += "\n" + coeffsName_ + ": [";
    for (int i = 0; i < static_cast<int>(distCoeffs.total()); ++i) {
        if (i != 0) msg += ", ";
        msg += std::to_string(distCoeffs.at<double>(i));
    }
    msg += "]";

    sc::tools::logger()->info("[Calibrate] {}", msg);
    return msg;
}

std::string CalibrateCamera::start()
{
    return this->runPath();
}

} // namespace function