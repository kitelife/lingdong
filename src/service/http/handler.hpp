#pragma once

#include <filesystem>
#include <fstream>
#include <utility>

#include "../../utils/strings.hpp"

namespace ling::http {

using RouteHandler = void(*)(const HttpRequest&, const HttpResponsePtr&, const std::function<void(HttpResponsePtr)>&);
using DoneCallback = std::function<void(HttpResponsePtr)>;

class DoneCallbackGuard {
public:
  explicit DoneCallbackGuard(DoneCallback cb, HttpResponsePtr resp_ptr): cb_func_(std::move(cb)), resp_ptr_(std::move(resp_ptr)) {}
  ~DoneCallbackGuard() {
    if (resp_ptr_ != nullptr) {
      cb_func_(resp_ptr_);
    }
  }
private:
  DoneCallback cb_func_;
  HttpResponsePtr resp_ptr_;
};

// 兜底，静态文件请求处理
static void static_file_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  std::string path = std::string(req.q.path);
  if (path[path.size()-1] == '/') {
    path += "index.html";
  }
  DoneCallbackGuard guard {cb, resp}; // guard
  std::filesystem::path file_path {"." + path};
  if (!exists(file_path)) {
    resp->with_code(HttpStatusCode::NOT_FOUND);
    return;
  }
  std::ifstream file_stream;
  file_stream.open(file_path);
  if (file_stream.fail()) {
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  std::string resp_content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
  resp->with_body(resp_content.data(), resp_content.size());
  std::string suffix_type = utils::find_suffix_type(path);
  if (!suffix_type.empty() && FILE_SUFFIX_TYPE_M_CONTENT_TYPE.contains(suffix_type)) {
    resp->with_header(header::ContentType.name, FILE_SUFFIX_TYPE_M_CONTENT_TYPE[suffix_type].type_name);
  }
}

// 获取 pv & uv 统计数据
static void access_stat_handler(const HttpRequest& req, const HttpResponsePtr& resp, const std::function<void(HttpResponsePtr)>& cb) {

}

static tsl::robin_map<std::string, RouteHandler> NAME2HANDLER {
  {"default", static_file_handler},
  {"access_stat", access_stat_handler},
};

}