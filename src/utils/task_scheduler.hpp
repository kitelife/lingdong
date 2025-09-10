#pragma once

#include <functional>
#include <chrono>
#include <future>
#include <queue>
#include <thread>
#include <utility>

#include "croncpp.h"

#include "executor.hpp"

namespace ling::utils {
using namespace std::chrono;

using TaskFunc = std::function<void()>;

class TimerTask {
public:
  virtual ~TimerTask() = default;

  virtual bool schedule_next_after_exec() {
    return false;
  }

  virtual milliseconds next_time() = 0;

  virtual void exec() {
    func_();
    ++exec_cnt_;
  }

  std::atomic_uint32_t& exec_count() {
    return exec_cnt_;
  }

protected:
  TaskFunc func_;
  std::atomic_uint32_t exec_cnt_ = 0;
};

using TimerTaskPtr = std::shared_ptr<TimerTask>;
using TaskTimeEvent = std::pair<milliseconds, TimerTaskPtr>;

struct GreaterComTaskTimeEvent {
  bool operator()(const TaskTimeEvent& lhs, const TaskTimeEvent& rhs) const {
    if (lhs.first - rhs.first == microseconds::zero()) {
      return lhs.second->exec_count() > rhs.second->exec_count();
    }
    return lhs.first > rhs.first;
  }
};

class FixedRateTask final : public TimerTask {
public:
  FixedRateTask(const TaskFunc& task_func, const milliseconds delay, const milliseconds period) :
    first_delay_(delay), period_(period) {
    func_ = task_func;
  }

  milliseconds next_time() override;

private:
  milliseconds first_delay_;
  milliseconds period_;
};

inline milliseconds FixedRateTask::next_time() {
  return std::chrono::duration_cast<milliseconds>(
      (system_clock::now() + (exec_cnt_ == 0 ? first_delay_ : period_)).time_since_epoch());
}

class FixedIntervalTask final : public TimerTask {
public:
  FixedIntervalTask(const TaskFunc& task_func, const milliseconds delay, const milliseconds period) :
    first_delay_(delay), period_(period) {
    func_ = task_func;
  }

  bool schedule_next_after_exec() override;
  milliseconds next_time() override;

private:
  milliseconds first_delay_;
  milliseconds period_;
};

inline bool FixedIntervalTask::schedule_next_after_exec() {
  return true;
}

inline milliseconds FixedIntervalTask::next_time() {
  return std::chrono::duration_cast<milliseconds>(
      (system_clock::now() + (exec_cnt_ == 0 ? first_delay_ : period_)).time_since_epoch());
}

// https://crontab.guru/
// https://man7.org/linux/man-pages/man5/crontab.5.html
class CronTask final : public TimerTask {
public:
  CronTask(const TaskFunc& task_func, std::string cron) : cron_(std::move(cron)) {
    func_ = task_func;
  }

  milliseconds next_time() override;

private:
  bool parse();
  std::string cron_;
  milliseconds next_time_ {0};
};

inline bool CronTask::parse() {
  try {
    auto parsed_cron = cron::make_cron(cron_);
    std::time_t now = system_clock::to_time_t(system_clock::now());
    std::time_t next = cron::cron_next(parsed_cron, now);
    next_time_ = duration_cast<milliseconds>(system_clock::from_time_t(next).time_since_epoch());
  } catch (cron::bad_cronexpr const & ex) {
    spdlog::error("illegal cron: {}, error: {}", cron_, ex.what());
    next_time_ = milliseconds(0);
    return false;
  }
  return true;
}

inline milliseconds CronTask::next_time() {
  if (!parse()) {
    return milliseconds(0);
  }
  return next_time_;
}

class TaskScheduler {
public:
  static TaskScheduler& singleton() {
    static TaskScheduler singleton_ {"default-task-scheduler"};
    return singleton_;
  }

  explicit TaskScheduler(const std::string& name);
  void schedule(const TaskFunc& task, milliseconds delay, milliseconds period);
  void schedule_at_fixed_rate(const TaskFunc& task, milliseconds delay, milliseconds period);
  void schedule_with_cron(const TaskFunc& task, const std::string& cron);
  void stop();

private:
  std::string name_;
  std::priority_queue<TaskTimeEvent, std::vector<TaskTimeEvent>, GreaterComTaskTimeEvent> event_q_ {};
  std::thread loop_;
  std::unique_ptr<Executor> runner_;
  //
  std::mutex lock_;
  std::condition_variable cond_;
  //
  std::atomic_bool stop_{false};
};

inline TaskScheduler::TaskScheduler(const std::string& name) {
  name_ = name;
  runner_ = std::make_unique<Executor>(name_);
  loop_ = std::thread([this]() {
    while (!stop_) {
      std::unique_lock ul(lock_);
      if (this->event_q_.empty()) {
        this->cond_.wait(ul);
      }
      auto task_event = this->event_q_.top();
      bool timeout = true;
      if (task_event.first > std::chrono::duration_cast<milliseconds>(system_clock::now().time_since_epoch())) {
        auto cv_status = this->cond_.wait_until(ul, system_clock::time_point(task_event.first));
        if (cv_status != std::cv_status::timeout) {
          timeout = false;
        }
      }
      if (timeout) {
        this->event_q_.pop();
        runner_->async_execute([&]() {
          task_event.second->exec();
          //
          if (task_event.second->schedule_next_after_exec()) {
            if (auto nt = task_event.second->next_time(); nt <= milliseconds(0)) {
              spdlog::error("illegal next time: {}ms", nt.count());
            } else {
              event_q_.emplace(nt, task_event.second);
            }
          }
        });
        if (!task_event.second->schedule_next_after_exec()) {
          if (auto nt = task_event.second->next_time(); nt <= milliseconds(0)) {
            spdlog::error("illegal next time: {}ms", nt.count());
          } else {
            event_q_.emplace(nt, task_event.second);
          }
        }
      }
    }
  });
}

inline void TaskScheduler::schedule(const TaskFunc& task, milliseconds delay, milliseconds period) {
  auto task_ptr = std::make_shared<FixedIntervalTask>(task, delay, period);
  event_q_.emplace(task_ptr->next_time(), task_ptr);
  cond_.notify_one();
}

inline void TaskScheduler::schedule_at_fixed_rate(const TaskFunc& task, milliseconds delay, milliseconds period) {
  auto task_ptr = std::make_shared<FixedRateTask>(task, delay, period);
  event_q_.emplace(task_ptr->next_time(), task_ptr);
  cond_.notify_one();
}

inline void TaskScheduler::schedule_with_cron(const TaskFunc& task, const std::string& cron) {
  auto task_ptr = std::make_shared<CronTask>(task, cron);
  event_q_.emplace(task_ptr->next_time(), task_ptr);
  cond_.notify_one();
}

inline void TaskScheduler::stop() {
  stop_ = true;
  loop_.join();
  runner_->join();
}

}