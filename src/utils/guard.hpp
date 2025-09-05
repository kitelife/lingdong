#pragma once

#include <utility>
#include <functional>

namespace ling::utils {

class MemoryGuard {
public:
  explicit MemoryGuard(void* mem_addr): addr_(mem_addr) {}
  MemoryGuard(const MemoryGuard&) = delete;
  MemoryGuard& operator=(const MemoryGuard&) = delete;

  ~MemoryGuard() {
    if (addr_ != nullptr) {
      free(addr_);
    }
  }
private:
  void* addr_;
};

template<typename T>
class TypedMemoryGuard {
public:
  explicit TypedMemoryGuard(T* mem_addr): addr_(mem_addr) {}
  TypedMemoryGuard(const TypedMemoryGuard&) = delete;
  TypedMemoryGuard& operator=(const TypedMemoryGuard&) = delete;

  ~TypedMemoryGuard() {
    if (addr_ != nullptr) {
      free(addr_);
    }
  }

  T *operator->() { return addr_; }

private:
  T* addr_;
};

class DeferGuard {
public:
  explicit DeferGuard(std::function<void()> defer_func): func_(std::move(defer_func)) {}
  DeferGuard(const DeferGuard&) = delete;
  DeferGuard& operator=(const DeferGuard&) = delete;

  ~DeferGuard() {
    func_();
  }
private:
  std::function<void()> func_;
};

}