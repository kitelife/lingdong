#pragma once

#include <filesystem>
#include <fstream>

#include "protocol.hpp"

namespace ling::http {

using RouteHandler = void(*)(const HttpRequest&, HttpResponsePtr, const std::function<void(HttpResponsePtr)>&);

static std::string find_suffix_type(const std::string& file_path) {
  std::string suffix_type;
  if (file_path.empty()) {
    return suffix_type;
  }
  size_t idx = file_path.size()-1;
  while (idx > 0) {
    if (file_path[idx] == '.') {
      suffix_type = file_path.substr(idx+1);
      break;
    }
    idx--;
  }
  return suffix_type;
}

static void static_file_handler(const HttpRequest& req, HttpResponsePtr resp, const std::function<void(HttpResponsePtr)>& cb) {
  std::string path = std::string(req.path);
  if (path[path.size()-1] == '/') {
    path += "index.html";
  }
  std::filesystem::path file_path {"." + path};
  if (!exists(file_path)) {
    resp->with_code(HttpStatusCode::NOT_FOUND);
    cb(resp);
    return;
  }
  std::ifstream file_stream;
  file_stream.open(file_path);
  if (file_stream.fail()) {
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    cb(resp);
    return;
  }
  std::string resp_content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
  resp->with_body(resp_content.data(), resp_content.size());
  std::string suffix_type = find_suffix_type(path);
  if (!suffix_type.empty() && FILE_SUFFIX_TYPE_M_CONTENT_TYPE.contains(suffix_type)) {
    resp->with_header(header::ContentType.name, FILE_SUFFIX_TYPE_M_CONTENT_TYPE[suffix_type].type_name);
  }
  //
  cb(resp);
}

static tsl::robin_map<std::string, RouteHandler> routes {
    {"", static_file_handler}
};

static void http_route(HttpRequest& req, const std::function<void(HttpResponsePtr)>& cb) {
  //
  std::string req_str;
  req.to_string(req_str);
  spdlog::debug("HTTP Request:\n{}", req_str);
  //
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  const auto route_path = std::string(req.path);
  if (!routes.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  routes[route_path](req, resp_ptr, cb);
}

}