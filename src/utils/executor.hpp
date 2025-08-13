#pragma once

#include <functional>
#include <thread>
#include <utility>

#include <spdlog/spdlog.h>

#include "blocking_queue.hpp"

namespace ling::utils {

using AsyncTask = std::function<void()>;

class Executor {
public:
  explicit Executor(std::string name = "default",
                    unsigned int capacity = 1024,
                    unsigned int worker_num = std::thread::hardware_concurrency())
      : name_(std::move(name)), capacity_(capacity), worker_num_(worker_num) {
    assert(capacity > 0);
    assert(worker_num > 0);
    task_queue_.reserve(capacity_);
    workers_.reserve(worker_num);
    for (unsigned int idx = 0; idx < worker_num; idx++) {
      workers_[idx] = std::thread([&, idx]() {
        unsigned int worker_id = idx;
        while (!done_) {
          AsyncTask t;
          if (const auto status = task_queue_.pop(t); !status) {
            continue;
          }
          try {
            t();
          } catch (std::runtime_error& err) {
            spdlog::error("Executor-{}-worker-{} occur async task error: {}", name_, worker_id, err.what());
          }
        }
        spdlog::info("Executor-{}-worker-{} exit", name_, worker_id);
      });
    }
  }

  bool async_execute(const AsyncTask& t);
  void join();

private:
  std::string name_;
  unsigned int capacity_;
  unsigned int worker_num_;
  //
  BlockingQueue<AsyncTask> task_queue_;
  std::vector<std::thread> workers_;
  std::atomic_bool done_{false};
};

inline bool Executor::async_execute(const AsyncTask& t) {
  if (done_) {
    return false;
  }
  task_queue_.push(t);
  return true;
}

inline void Executor::join() {
  if (done_) {
    return;
  }
  if (auto expect = false; !done_.compare_exchange_strong(expect, true)) {
    return;
  }
  task_queue_.close();
  for (auto& wt : workers_) {
    wt.join();
  }
}

static Executor& default_executor() {
  static Executor executor;
  return executor;
}

}  // namespace ling::utils