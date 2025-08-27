#pragma once

#include <utility>

#include "protocol.hpp"

namespace ling::http {

using DoneCallback = std::function<void(HttpResponsePtr)>;
using RouteHandler = void (*)(const HttpRequest&, const HttpResponsePtr&, const DoneCallback&);

class DoneCallbackGuard {
public:
  explicit DoneCallbackGuard(DoneCallback cb, HttpResponsePtr resp_ptr)
      : cb_func_(std::move(cb)), resp_ptr_(std::move(resp_ptr)) {}
  ~DoneCallbackGuard() {
    if (resp_ptr_ != nullptr) {
      cb_func_(resp_ptr_);
    }
  }

private:
  DoneCallback cb_func_;
  HttpResponsePtr resp_ptr_;
};

}  // namespace ling::http