#pragma once

#include <utility>

namespace ling {

class MemoryGuard {
public:
  explicit MemoryGuard(void* mem_addr): addr_(mem_addr) {}
  ~MemoryGuard() {
    if (addr_ != nullptr) {
      free(addr_);
    }
  }
private:
  void* addr_;
};

class DeferGuard {
public:
  explicit DeferGuard(std::function<void()> defer_func): func_(std::move(defer_func)) {}
  ~DeferGuard() {
    func_();
  }
private:
  std::function<void()> func_;
};

}