#ifndef THREAD_SAFE_QUEUE_HPP
#define THREAD_SAFE_QUEUE_HPP

#include <condition_variable>
#include <functional>
#include <iostream>
#include <mutex>
#include <queue>

namespace tools
{
/**
 * @brief 线程安全队列
 *
 * 用 std::mutex 保护内部 std::queue，用 condition_variable 实现
 * "空队列时 pop 阻塞等待"。支持多生产者/多消费者。
 *
 * @tparam T             元素类型
 * @tparam PopWhenFull   true：队列满时丢弃最旧元素再 push（环形缓冲语义）；
 *                       false：队列满时调用 full_handler_ 并丢弃新元素
 *
 * @note 非拷贝安全：内部有 mutex，禁止拷贝（编译器默认生成的拷贝构造会失败）。
 * @note pop 会阻塞直到队列非空；empty() 立即返回，不阻塞。
 */
template <typename T, bool PopWhenFull = false>
class ThreadSafeQueue
{
public:
  /**
   * @brief 构造队列
   * @param max_size     最大容量
   * @param full_handler 队列满且 PopWhenFull=false 时的回调（默认空）
   */
  ThreadSafeQueue(
    size_t max_size, std::function<void(void)> full_handler = [] {})
  : max_size_(max_size), full_handler_(full_handler)
  {
  }

  /**
   * @brief 入队
   *
   * 队列满时：
   *   - PopWhenFull=true：弹出队头（最旧）再入队，保证新数据进来；
   *   - PopWhenFull=false：调用 full_handler_，**丢弃本次新元素**，直接返回。
   *
   * @param value 待入队元素（拷贝）
   */
  void push(const T & value)
  {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.size() >= max_size_) {
      if (PopWhenFull) {
        queue_.pop();          // 丢弃最旧
      } else {
        full_handler_();       // 通知调用方"满了"
        return;                // 丢弃新元素
      }
    }

    queue_.push(value);
    not_empty_condition_.notify_all();   // 唤醒等待的 pop
  }

  /**
   * @brief 出队（阻塞式，输出参数版）
   *
   * 队列空时阻塞，直到有元素。取出队头写入 value。
   *
   * @param[out] value 出队元素
   */
  void pop(T & value)
  {
    std::unique_lock<std::mutex> lock(mutex_);

    not_empty_condition_.wait(lock, [this] { return !queue_.empty(); });

    // 走到这里队列一定非空（wait 的谓词保证），下面这个检查是冗余的
    if (queue_.empty()) {
      std::cerr << "Error: Attempt to pop from an empty queue." << std::endl;
      return;
    }

    value = queue_.front();
    queue_.pop();
  }

  /**
   * @brief 出队（阻塞式，返回值版）
   * @return 出队元素（移动构造）
   */
  T pop()
  {
    std::unique_lock<std::mutex> lock(mutex_);

    not_empty_condition_.wait(lock, [this] { return !queue_.empty(); });

    T value = std::move(queue_.front());
    queue_.pop();
    return std::move(value);   // 这里的 std::move 是多余的（返回值优化）
  }

  /**
   * @brief 取队头（阻塞式，不移除）
   * @return 队头元素的拷贝
   */
  T front()
  {
    std::unique_lock<std::mutex> lock(mutex_);

    not_empty_condition_.wait(lock, [this] { return !queue_.empty(); });

    return queue_.front();
  }

  /**
   * @brief 取队尾（非阻塞，输出参数版）
   * @param[out] value 队尾元素
   */
  void back(T & value)
  {
    std::unique_lock<std::mutex> lock(mutex_);

    if (queue_.empty()) {
      std::cerr << "Error: Attempt to access the back of an empty queue." << std::endl;
      return;
    }

    value = queue_.back();
  }

  /**
   * @brief 判空（非阻塞）
   * @return true 表示空
   */
  bool empty()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    return queue_.empty();
  }

  /**
   * @brief 清空队列
   *
   * 清空后 notify_all 唤醒所有等待者——但它们醒来发现还是空，会继续 wait。
   * （这个 notify 实际意义不大，因为清空后队列仍空。）
   */
  void clear()
  {
    std::unique_lock<std::mutex> lock(mutex_);
    while (!queue_.empty()) {
      queue_.pop();
    }
    not_empty_condition_.notify_all();
  }

private:
  std::queue<T> queue_;                          ///< 内部队列
  size_t max_size_;                              ///< 最大容量
  mutable std::mutex mutex_;                     ///< 互斥锁（mutable 允许 const 方法加锁）
  std::condition_variable not_empty_condition_;  ///< "非空"条件变量
  std::function<void(void)> full_handler_;       ///< 满队列回调
};

}  // namespace tools

#endif  // THREAD_SAFE_QUEUE_HPP