#pragma once

#include "protocol.hpp"
#include "handler.hpp"
#include "utils/rate_limit.hpp"
#include "utils/guard.hpp"

namespace ling::http {
class Router {
public:
  virtual ~Router() = default;
  virtual void route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) = 0;
  virtual bool add_routes(std::vector<std::pair<std::pair<HTTP_METHOD, std::string>, RouteHandler>> routes) = 0;
};

struct MapBasedRouterConf {
  uint32_t global_rate_limit;
  uint32_t per_client_rate_limit;
  std::function<void(HttpRequest&)> func_log_req;
};

class MapBasedRouter final : public Router {
public:
  explicit MapBasedRouter(const MapBasedRouterConf& conf);
  void route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) override;
  bool add_routes(std::vector<std::pair<std::pair<HTTP_METHOD, std::string>, RouteHandler>> routes) override;

private:
  // 限流响应 429
  static void rate_limited_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb);
  // 兜底，静态文件请求处理
  void static_file_handler(HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) const;

private:
  std::unique_ptr<utils::RateLimiter> rate_limiter_ptr_;
  std::atomic_bool inited_{false};
  //
  std::function<void(HttpRequest&)> func_log_req_;
  tsl::robin_map<std::string, RouteHandler> routes_{};
  std::mutex routes_mutex_;
};

inline MapBasedRouter::MapBasedRouter(const MapBasedRouterConf& conf) {
  if (inited_) {
    return;
  }
  func_log_req_ = conf.func_log_req;
  rate_limiter_ptr_ = std::make_unique<utils::TokenBucketRateLimiter>(conf.global_rate_limit,
                                                                      conf.per_client_rate_limit);
  inited_ = true;
}

inline bool MapBasedRouter::add_routes(
    std::vector<std::pair<std::pair<HTTP_METHOD, std::string>, RouteHandler>> routes) {
  for (const auto& [action_path, handler] : routes) {
    if (routes_.contains(action_path.second)) {
      return false;
    }
    std::lock_guard lock(routes_mutex_);
    routes_[action_path.second] = handler;
  }
  return true;
}

inline void MapBasedRouter::route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) {
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  if (!rate_limiter_ptr_->permit(req.from.first)) {
    rate_limited_handler(req, resp_ptr, cb);
    return;
  }
  const auto route_path = std::string(req.q.path);
  if (!routes_.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  if (func_log_req_) {
    func_log_req_(req);
  }
  routes_[route_path](req, resp_ptr, cb);
}

inline void MapBasedRouter::rate_limited_handler(const HttpRequest& req,
                                                 const HttpResponsePtr& resp,
                                                 const DoneCallback& cb) {
  DoneCallbackGuard guard{cb, resp};
  resp->with_code(HttpStatusCode::TOO_MANY_REQUESTS);
}

inline void MapBasedRouter::static_file_handler(HttpRequest& req,
                                                const HttpResponsePtr& resp,
                                                const DoneCallback& cb) const {
  std::string path = std::string(req.q.path);
  if (path[path.size() - 1] == '/') {
    path += "index.html";
  }
  DoneCallbackGuard guard{cb, resp}; // guard
  std::filesystem::path file_path{"." + path};
  if (!exists(file_path)) {
    resp->with_body(CODE2MSG[HttpStatusCode::NOT_FOUND]);
    resp->with_code(HttpStatusCode::NOT_FOUND);
    return;
  }
  std::string suffix_type = utils::find_suffix_type(path);
  if (suffix_type == "html" || suffix_type == "htm") {
    if (func_log_req_) {
      func_log_req_(req);
    }
  }
  std::ifstream file_stream;
  file_stream.open(file_path);
  utils::DeferGuard defer_guard([&]() {
    file_stream.close();
  });
  if (file_stream.fail()) {
    resp->with_body(CODE2MSG[HttpStatusCode::INTERNAL_ERR]);
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  std::string resp_content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
  resp->with_body(resp_content);
  //
  if (!suffix_type.empty() && FILE_SUFFIX_TYPE_M_CONTENT_TYPE.contains(suffix_type)) {
    resp->with_header(header::ContentType, FILE_SUFFIX_TYPE_M_CONTENT_TYPE[suffix_type].type_name);
  }
}
}