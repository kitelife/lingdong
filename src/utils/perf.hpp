#pragma once

#include <string>
#include <chrono>
#include <utility>

#include "spdlog/spdlog.h"

namespace ling::utils {

class ScopedTimer {
public:
  explicit ScopedTimer(std::string name): name_(std::move(name)) {
    start_ = std::chrono::steady_clock::now();
  }

  void end() {
    auto du = std::chrono::steady_clock::now() - start_;
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(du).count();
    spdlog::info("{}, time elapsed {} ms", name_, ms);
    end_ = true;
  }

  ~ScopedTimer() {
    if (!end_) {
      end();
    }
  }

private:
  std::string name_;
  std::chrono::time_point<std::chrono::steady_clock> start_;
  bool end_ = false;
};

}