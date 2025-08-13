#pragma once

#include <atomic>
#include <chrono>

#include <tsl/robin_map.h>

namespace ling::utils {
using namespace std::chrono;

// https://bytebytego.com/courses/system-design-interview/design-a-rate-limiter
// https://en.wikipedia.org/wiki/Token_bucket

class TimedLimit {
public:
  TimedLimit() = default;
  TimedLimit(steady_clock::time_point refill_time, uint32_t limit_val) : last_refill_time(refill_time), limit(limit_val) {}
  //
  steady_clock::time_point last_refill_time;
  std::atomic_uint limit{0};
};

using TimedLimitPtr = std::unique_ptr<TimedLimit>;

class RateLimiter {
public:
  virtual ~RateLimiter() = default;
  virtual bool permit() = 0;
  virtual bool permit(const std::string& client) = 0;
};

class TokenBucketRateLimiter final : public RateLimiter {
public:
  TokenBucketRateLimiter(const uint32_t global_limit, const uint32_t per_client_limit)
      : global_limit_set_(global_limit), per_client_limit_set_(per_client_limit) {
    global_limiter_.limit = global_limit_set_;
    global_limiter_.last_refill_time = steady_clock::now();
    //
    last_cleanup_time_ = steady_clock::now();
  }
  bool permit() override;
  bool permit(const std::string& client) override;

private:
  static bool permit(TimedLimit& limiter, const uint32_t& limit_set);
  void try_cleanup();

  uint32_t global_limit_set_{0};
  uint32_t per_client_limit_set_{0};
  //
  TimedLimit global_limiter_;
  //
  std::mutex lock_;
  tsl::robin_map<std::string, TimedLimitPtr> per_client_limiter_;
  //
  steady_clock::time_point last_cleanup_time_;
};

inline bool TokenBucketRateLimiter::permit(TimedLimit& limiter, const uint32_t& limit_set) {
  if ((steady_clock::now() - limiter.last_refill_time) >= seconds(1)) {
    // refill
    do {
      auto old_limit = limiter.limit.load();
      if (limiter.limit.compare_exchange_strong(old_limit, limit_set)) {
        limiter.last_refill_time = steady_clock::now();
        break;
      }
    } while (true);
  }
  do {
    auto old_limit = limiter.limit.load();
    if (old_limit == 0) {
      return false;
    }
    if (limiter.limit.compare_exchange_strong(old_limit, old_limit - 1)) {
      break;
    }
  } while (true);
  return true;
}

inline bool TokenBucketRateLimiter::permit() {
  return permit(global_limiter_, global_limit_set_);
}

inline bool TokenBucketRateLimiter::permit(const std::string& client) {
  try_cleanup();
  //
  if (!permit()) {
    return false;
  }
  //
  if (!per_client_limiter_.contains(client)) {
    std::lock_guard guard{lock_};
    if (!per_client_limiter_.contains(client)) {
      per_client_limiter_.insert({client, std::make_unique<TimedLimit>(steady_clock::now(), per_client_limit_set_)});
    }
  }
  return permit(*(per_client_limiter_[client]), per_client_limit_set_);
}

inline void TokenBucketRateLimiter::try_cleanup() {
  const auto& now = steady_clock::now();
  // 流量低峰期做清理
  if ((now - last_cleanup_time_) < hours(1) || (now - global_limiter_.last_refill_time) < seconds(10)) {
    return;
  }
  std::lock_guard guard {lock_};
  std::vector<std::string> expired_keys;
  for (const auto& [client, timed_limit] : per_client_limiter_) {
    if ((now - timed_limit->last_refill_time) >= hours(1)) {
      expired_keys.emplace_back(client);
    }
  }
  for (const auto& k : expired_keys) {
    per_client_limiter_.erase(k);
  }
  if (!expired_keys.empty()) {
    spdlog::debug("cleanup {} expired keys", expired_keys.size());
  }
}

} // namespace ling::utils