#pragma once

#include "protocol.hpp"
#include "handler.hpp"

namespace ling::http {

inline tsl::robin_map<std::string, RouteHandler> routes {
  {"/view_cnt/", access_stat_handler},
  {"/tool/echo/", simple_echo_handler},
  {"/tool/base64/", base64_handler},
  {"/rss/provider/", rss_provider_handler},
  {"/rss/register/", rss_register_handler},
  {"", static_file_handler},
};

inline void http_route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) {
  /*
  if (spdlog::get_level() == spdlog::level::debug) {
    std::string req_str;
    req.to_string(req_str);
    spdlog::debug("HTTP Request:\n{}", req_str);
  }
  */
  // TODO: 限流 ratelimit
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  const auto route_path = std::string(req.q.path);
  if (!routes.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  log_req(req);
  routes[route_path](req, resp_ptr, cb);
}

}