#include "functions/calibrate_headeye.hpp"

namespace sc::functions
{

static cv::Matx33d quat2mat(double w, double x, double y, double z)
{
    const double n = std::sqrt(w * w + x * x + y * y + z * z);
    if (n <= 1e-12) return cv::Matx33d::eye();
    w /= n; x /= n; y /= n; z /= n;

    return cv::Matx33d(
        1 - 2 * (y * y + z * z), 2 * (x * y - w * z),     2 * (x * z + w * y),
        2 * (x * y + w * z),     1 - 2 * (x * x + z * z), 2 * (y * z - w * x),
        2 * (x * z - w * y),     2 * (y * z + w * x),     1 - 2 * (x * x + y * y));
}

CalibrateHeadEye::CalibrateHeadEye(std::string config)
{
    auto yaml = sc::tools::load(config);
    this->commonConfigPath_ = sc::tools::read<std::string>(yaml,"common_configPath");
    this->path_ = sc::tools::read<std::string>(yaml,"calibrate_headeye_path");
    this->R_gimbal2imubodyName_ = sc::tools::read<std::string>(yaml,"calibrate_headeye_R_gimbal2imubody_name");
    this->R_camera2gimbalName_ = sc::tools::read<std::string>(yaml,"calibrate_headeye_R_camera2gimbal_name");
    this->t_camera2gimbalName_ = sc::tools::read<std::string>(yaml,"calibrate_headeye_t_camera2gimbal_name");
    this->rows_ = sc::tools::read<int>(yaml,"calibrate_headeye_rows");
    this->cols_ = sc::tools::read<int>(yaml,"calibrate_headeye_cols");
    this->distance_ = sc::tools::read<int>(yaml,"calibrate_headeye_distance_mm");    
    this->matrixName_ = sc::tools::read<std::string>(yaml,"calibrate_camera_matrix_name");
    this->coeffsName_ = sc::tools::read<std::string>(yaml,"calibrate_camera_coeffs_name");
}

void CalibrateHeadEye::setCommonConfigPath(std::string commonConfigPath)
{
    commonConfigPath_ = std::move(commonConfigPath);
}

std::string CalibrateHeadEye::getCommonConfigPath()
{
    return commonConfigPath_;
}

void CalibrateHeadEye::setPath(std::string path)
{
    path_ = std::move(path);
}

std::string CalibrateHeadEye::getPath()
{
    return path_;
}

void CalibrateHeadEye::setRGimbal2ImuBodyName(std::string rGimbal2ImuBodyName)
{
    R_gimbal2imubodyName_ = std::move(rGimbal2ImuBodyName);
}

std::string CalibrateHeadEye::getRGimbal2ImuBodyName()
{
    return R_gimbal2imubodyName_;
}

void CalibrateHeadEye::setRCamera2GimbalName(std::string rCamera2GimbalName)
{
    R_camera2gimbalName_ = std::move(rCamera2GimbalName);
}

std::string CalibrateHeadEye::getRCamera2GimbalName()
{
    return R_camera2gimbalName_;
}

void CalibrateHeadEye::setTCamera2GimbalName(std::string tCamera2GimbalName)
{
    t_camera2gimbalName_ = std::move(tCamera2GimbalName);
}

std::string CalibrateHeadEye::getTCamera2GimbalName()
{
    return t_camera2gimbalName_;
}

std::string CalibrateHeadEye::getMatrixName()
{
    return matrixName_;
}

void CalibrateHeadEye::setMatrixName(std::string matrixName)
{
    matrixName_ = std::move(matrixName);
}

std::string CalibrateHeadEye::getCoeffsName()
{
    return coeffsName_;
}

void CalibrateHeadEye::setCoeffsName(std::string coeffsName)
{
    coeffsName_ = std::move(coeffsName);
}

void CalibrateHeadEye::setCols(int cols)
{
    cols_ = cols;
}

int CalibrateHeadEye::getCols()
{
    return cols_;
}

void CalibrateHeadEye::setRows(int rows)
{
    rows_ = rows;
}

int CalibrateHeadEye::getRows()
{
    return rows_;
}

void CalibrateHeadEye::setDistance(int distance)
{
    distance_ = distance;
}

int CalibrateHeadEye::getDistance()
{
    return distance_;
}

std::string CalibrateHeadEye::start()
{
    std::error_code ec;
    if (!std::filesystem::exists(path_, ec) ||
        !std::filesystem::is_directory(path_, ec)) {
        sc::tools::logger()->error("[CalibrateHeadEye] 路径不存在或不是目录: {}", path_);
        return "手眼标定失败：路径不存在或不是目录 " + path_;
    }

    YAML::Node root = sc::tools::load(commonConfigPath_);
    if (!root[R_gimbal2imubodyName_]) {
        return "手眼标定失败：公共配置中缺少 " + R_gimbal2imubodyName_;
    }
    if (!root[matrixName_] || !root[coeffsName_]) {
        return "手眼标定失败：公共配置中缺少 " + matrixName_ + " 或 " + coeffsName_;
    }

    auto R_gimbal2imubody_data = root[R_gimbal2imubodyName_].as<std::vector<double>>();
    auto camera_matrix_data    = root[matrixName_].as<std::vector<double>>();
    auto distort_coeffs_data   = root[coeffsName_].as<std::vector<double>>();

    if (R_gimbal2imubody_data.size() != 9) {
        return "手眼标定失败：" + R_gimbal2imubodyName_ + " 长度不为 9";
    }
    if (camera_matrix_data.size() != 9) {
        return "手眼标定失败：" + matrixName_ + " 长度不为 9";
    }

    const cv::Matx33d R_gimbal2imubody(
        R_gimbal2imubody_data[0], R_gimbal2imubody_data[1], R_gimbal2imubody_data[2],
        R_gimbal2imubody_data[3], R_gimbal2imubody_data[4], R_gimbal2imubody_data[5],
        R_gimbal2imubody_data[6], R_gimbal2imubody_data[7], R_gimbal2imubody_data[8]);

    const cv::Matx33d camera_matrix(camera_matrix_data.data());
    const cv::Mat distort_coeffs(distort_coeffs_data);

    const cv::Size pattern_size(cols_, rows_);
    const float center_distance = static_cast<float>(distance_);  // mm

    std::vector<cv::Point3f> centers_3d;
    centers_3d.reserve(pattern_size.area());
    for (int i = 0; i < pattern_size.height; ++i)
        for (int j = 0; j < pattern_size.width; ++j)
            centers_3d.emplace_back(j * center_distance, i * center_distance, 0.0f);

    std::vector<cv::Mat> R_gimbal2world_list, t_gimbal2world_list;
    std::vector<cv::Mat> rvecs, tvecs;

    int successCount = 0;
    int failCount = 0;
    std::string failedList;

    std::error_code dir_ec;
    for (const auto & entry : std::filesystem::directory_iterator(path_, dir_ec)) {
        if (dir_ec) break;

        auto ext = entry.path().extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (ext != ".jpg" && ext != ".jpeg" && ext != ".png" && ext != ".bmp")
            continue;

        const std::string img_path = entry.path().string();
        const std::string q_path = entry.path().parent_path().string() + "/" +
                                   entry.path().stem().string() + ".txt";

        std::error_code file_ec;
        if (!std::filesystem::exists(q_path, file_ec)) {
            ++failCount;
            failedList += img_path + "(缺少四元数txt); ";
            sc::tools::logger()->warn("[CalibrateHeadEye] 缺少四元数文件: {}", q_path);
            continue;
        }

        std::ifstream q_file(q_path);
        double w, x, y, z;
        if (!(q_file >> w >> x >> y >> z)) {
            ++failCount;
            failedList += img_path + "(四元数格式错误); ";
            sc::tools::logger()->warn("[CalibrateHeadEye] 四元数格式错误: {}", q_path);
            continue;
        }

        cv::Mat img = cv::imread(img_path);
        if (img.empty()) {
            ++failCount;
            failedList += img_path + "(无法读取); ";
            sc::tools::logger()->warn("[CalibrateHeadEye] 无法读取图片: {}", img_path);
            continue;
        }

        const cv::Matx33d R_imubody2imuabs = quat2mat(w, x, y, z);
        const cv::Matx33d R_gimbal2world =
            R_gimbal2imubody.t() * R_imubody2imuabs * R_gimbal2imubody;

        std::vector<cv::Point2f> centers_2d;
        const bool found = cv::findCirclesGrid(
            img, pattern_size, centers_2d,
            cv::CALIB_CB_SYMMETRIC_GRID | cv::CALIB_CB_CLUSTERING);

        if (!found) {
            ++failCount;
            failedList += img_path + "(未检测到圆点板); ";
            sc::tools::logger()->warn("[CalibrateHeadEye] 未检测到圆点板: {}", img_path);
            continue;
        }

        const cv::Mat t_gimbal2world = (cv::Mat_<double>(3, 1) << 0, 0, 0);
        cv::Mat rvec, tvec;
        cv::solvePnP(
            centers_3d, centers_2d, camera_matrix, distort_coeffs, rvec, tvec,
            false, cv::SOLVEPNP_IPPE);

        R_gimbal2world_list.emplace_back(cv::Mat(R_gimbal2world));
        t_gimbal2world_list.emplace_back(t_gimbal2world.clone());
        rvecs.emplace_back(rvec.clone());
        tvecs.emplace_back(tvec.clone());
        ++successCount;
    }

    if (successCount < 3) {
        std::string msg = "手眼标定失败：有效图片不足3张，成功" +
                          std::to_string(successCount) + "张，失败" +
                          std::to_string(failCount) + "张";
        if (!failedList.empty()) msg += "，失败列表: " + failedList;
        sc::tools::logger()->error("[CalibrateHeadEye] {}", msg);
        return msg;
    }

    cv::Mat R_camera2gimbal, t_camera2gimbal;
    cv::calibrateHandEye(
        R_gimbal2world_list, t_gimbal2world_list, rvecs, tvecs,
        R_camera2gimbal, t_camera2gimbal);
    t_camera2gimbal /= 1e3;  // mm -> m

    YAML::Node matNode(YAML::NodeType::Sequence);
    for (int r = 0; r < R_camera2gimbal.rows; ++r)
        for (int c = 0; c < R_camera2gimbal.cols; ++c)
            matNode.push_back(R_camera2gimbal.at<double>(r, c));
    matNode.SetStyle(YAML::EmitterStyle::Flow);

    YAML::Node tNode(YAML::NodeType::Sequence);
    for (int r = 0; r < t_camera2gimbal.rows; ++r)
        for (int c = 0; c < t_camera2gimbal.cols; ++c)
            tNode.push_back(t_camera2gimbal.at<double>(r, c));
    tNode.SetStyle(YAML::EmitterStyle::Flow);

    if (root[R_camera2gimbalName_]) {
        sc::tools::overwrite(root, R_camera2gimbalName_, matNode);
    } else {
        root[R_camera2gimbalName_] = matNode;
    }
    if (root[t_camera2gimbalName_]) {
        sc::tools::overwrite(root, t_camera2gimbalName_, tNode);
    } else {
        root[t_camera2gimbalName_] = tNode;
    }
    sc::tools::save(commonConfigPath_, root);

    std::string msg = "手眼标定成功：成功" + std::to_string(successCount) +
                      "张，失败" + std::to_string(failCount) + "张";
    if (!failedList.empty()) msg += "，失败列表: " + failedList;
    msg += "，已写入 " + R_camera2gimbalName_ + " 和 " + t_camera2gimbalName_;

    msg += "\n" + R_camera2gimbalName_ + ": [";
    for (int r = 0; r < R_camera2gimbal.rows; ++r) {
        for (int c = 0; c < R_camera2gimbal.cols; ++c) {
            if (r != 0 || c != 0) msg += ", ";
            msg += std::to_string(R_camera2gimbal.at<double>(r, c));
        }
    }
    msg += "]";

    msg += "\n" + t_camera2gimbalName_ + ": [";
    for (int r = 0; r < t_camera2gimbal.rows; ++r) {
        for (int c = 0; c < t_camera2gimbal.cols; ++c) {
            if (r != 0 || c != 0) msg += ", ";
            msg += std::to_string(t_camera2gimbal.at<double>(r, c));
        }
    }
    msg += "]";

    sc::tools::logger()->info("[CalibrateHeadEye] {}", msg);
    return msg;
}

} // namespace sc::functions 