#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

namespace ling::utils {

using std::chrono::steady_clock;
using std::chrono::milliseconds;

template <typename T>
class BlockingQueue {
public:
  BlockingQueue() = default;
  explicit BlockingQueue(size_t cap) : capacity_(cap) {}

  bool push(const T& data) {
    if (closed_) {
      return false;
    }
    std::unique_lock ul(lock_);
    while (queue_.size() >= capacity_ && !closed_) {
      if (full_cond_.wait_for(ul, milliseconds(50)) == std::cv_status::timeout) {
        return false;
      }
    }
    if (closed_) {
      return false;
    }
    queue_.push(data);
    empty_cond_.notify_one();
    return true;
  }

  void reserve(size_t cap) {
    capacity_ = cap;
  }

  size_t size() {
    std::unique_lock ul(lock_);
    return queue_.size();
  }

  bool pop(T& t) {
    if (closed_) {
      return false;
    }
    std::unique_lock ul(lock_);
    while (queue_.empty() && !closed_) {
      if (empty_cond_.wait_for(ul, milliseconds(50)) == std::cv_status::timeout) {
        return false;
      }
    }
    if (queue_.empty()) {
      return false;
    }
    t = queue_.front();
    queue_.pop();
    full_cond_.notify_one();
    return true;
  }

  void close() {
    closed_ = true;
    full_cond_.notify_all();
    empty_cond_.notify_all();
  }

private:
  std::queue<T> queue_;
  size_t capacity_ {1024};

  std::mutex lock_;
  std::condition_variable empty_cond_;
  std::condition_variable full_cond_;
  //
  std::atomic_bool closed_ {false};
};

}