#ifndef EXTENDED_KALMAN_FILTER_HPP
#define EXTENDED_KALMAN_FILTER_HPP

#include <Eigen/Dense>
#include <deque>
#include <functional>
#include <map>

namespace tools
{
/**
 * @brief 扩展卡尔曼滤波器（EKF）
 * @note 非线程安全：x/P 和数据成员可变，多线程需外部加锁。
 */
class ExtendedKalmanFilter
{
public:
  Eigen::VectorXd x;  ///< 状态向量（维度由构造时 x0 决定）
  Eigen::MatrixXd P;  ///< 状态协方差矩阵（x 维 × x 维）

  ExtendedKalmanFilter() = default;

  /**
   * @brief 构造 EKF
   * @param x0    初始状态
   * @param P0    初始协方差
   * @param x_add 状态加法函数（默认 a + b）。
   *              对普通向量是加法；对"角度"等特殊状态可自定义（如角度归一化加法）。
   */
  ExtendedKalmanFilter(
    const Eigen::VectorXd & x0, const Eigen::MatrixXd & P0,
    std::function<Eigen::VectorXd(const Eigen::VectorXd &, const Eigen::VectorXd &)> x_add =
      [](const Eigen::VectorXd & a, const Eigen::VectorXd & b) { return a + b; });

  /**
   * @brief 线性预测：x = F·x，P = F·P·Fᵀ + Q
   * @param F 状态转移矩阵
   * @param Q 过程噪声协方差
   * @return  预测后的状态 x
   */
  Eigen::VectorXd predict(const Eigen::MatrixXd & F, const Eigen::MatrixXd & Q);

  /**
   * @brief 非线性预测：x = f(x)，P = F·P·Fᵀ + Q（F 由外部提供或近似）
   * @param F 状态转移雅可比矩阵
   * @param Q 过程噪声协方差
   * @param f 非线性状态转移函数
   * @return  预测后的状态 x
   */
  Eigen::VectorXd predict(
    const Eigen::MatrixXd & F, const Eigen::MatrixXd & Q,
    std::function<Eigen::VectorXd(const Eigen::VectorXd &)> f);

  /**
   * @brief 线性更新：标准卡尔曼更新
   * @param z          量测向量
   * @param H          量测矩阵
   * @param R          量测噪声协方差
   * @param z_subtract 量测残差函数（默认 a - b）。
   *                   对角度等特殊量测可自定义（如角度差归一化）。
   * @return           更新后的状态 x
   */
  Eigen::VectorXd update(
    const Eigen::VectorXd & z, const Eigen::MatrixXd & H, const Eigen::MatrixXd & R,
    std::function<Eigen::VectorXd(const Eigen::VectorXd &, const Eigen::VectorXd &)> z_subtract =
      [](const Eigen::VectorXd & a, const Eigen::VectorXd & b) { return a - b; });

  /**
   * @brief 非线性更新：用 h(x) 计算量测残差
   * @param z          量测向量
   * @param H          量测雅可比矩阵
   * @param R          量测噪声协方差
   * @param h          非线性量测函数（由状态 x 算量测）
   * @param z_subtract 量测残差函数（默认 a - b）
   * @return           更新后的状态 x
   */
  Eigen::VectorXd update(
    const Eigen::VectorXd & z, const Eigen::MatrixXd & H, const Eigen::MatrixXd & R,
    std::function<Eigen::VectorXd(const Eigen::VectorXd &)> h,
    std::function<Eigen::VectorXd(const Eigen::VectorXd &, const Eigen::VectorXd &)> z_subtract =
      [](const Eigen::VectorXd & a, const Eigen::VectorXd & b) { return a - b; });

  std::map<std::string, double> data;      ///< 卡方检验数据（NIS/NEES 等指标，按名字存）
  std::deque<int> recent_nis_failures{0};  ///< 近期 NIS 失败次数（滑动窗口）
  size_t window_size = 100;                ///< 滑动窗口大小
  double last_nis;                         ///< 最近一次的 NIS 值

private:
  Eigen::MatrixXd I;    ///< 单位矩阵（缓存，避免每次构造）

  /**
   * @brief 状态加法函数
   */
  std::function<Eigen::VectorXd(const Eigen::VectorXd &, const Eigen::VectorXd &)> x_add;

  int nees_count_ = 0;   ///< NEES（归一化估计误差平方）统计计数
  int nis_count_ = 0;    ///< NIS（归一化新息平方）统计计数
  int total_count_ = 0;  ///< 总更新次数
};

}  // namespace tools

#endif  // EXTENDED_KALMAN_FILTER_HPP