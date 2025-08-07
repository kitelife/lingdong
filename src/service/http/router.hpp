#pragma once

#include "protocol.hpp"
#include "handler.hpp"

namespace ling::http {

inline tsl::robin_map<std::string, RouteHandler> routes {
    {"", static_file_handler}
};

inline void http_route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) {
  if (spdlog::get_level() == spdlog::level::debug) {
    std::string req_str;
    req.to_string(req_str);
    spdlog::debug("HTTP Request:\n{}", req_str);
  }
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  const auto route_path = std::string(req.path);
  if (!routes.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  routes[route_path](req, resp_ptr, cb);
}

}