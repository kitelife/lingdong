#pragma once

#include "protocol.hpp"
#include "handler.hpp"
#include "context.hpp"
#include "utils/rate_limit.hpp"

namespace ling::http {

inline static tsl::robin_map<std::string, RouteHandler> routes {
  {"/view_cnt/", access_stat_handler},
  {"/tool/echo/", simple_echo_handler},
  {"/tool/base64/", base64_handler},
  {"/rss/provider/", rss_provider_handler},
  {"/rss/register/", rss_register_handler},
  {"", static_file_handler},
};

class Router {
public:
  static Router& singleton() {
    static Router router;
    router.init();
    return router;
  }

  Router() = default;
  ~Router() = default;

  void init();
  void route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb);

private:
  std::unique_ptr<utils::RateLimiter> rate_limiter_ptr_;
  std::atomic_bool inited_ {false};
  std::mutex lock_;
};

inline void Router::init() {
  if (inited_) {
    return;
  }
  std::lock_guard guard {lock_};
  const auto& server_conf = Context::singleton()->with_config()->server_conf;
  rate_limiter_ptr_ = std::make_unique<utils::TokenBucketRateLimiter>(server_conf.global_rate_limit, server_conf.per_client_rate_limit);
  inited_ = true;
}

inline void Router::route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) {
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  if (!rate_limiter_ptr_->permit(req.from.first)) {
    rate_limited_handler(req, resp_ptr, cb);
    return;
  }
  const auto route_path = std::string(req.q.path);
  if (!routes.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  log_req(req);
  routes[route_path](req, resp_ptr, cb);
}

}