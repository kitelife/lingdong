#pragma once

#include <filesystem>
#include <fstream>
#include <utility>

#include "utils/strings.hpp"
#include "utils/guard.hpp"
#include "utils//time.hpp"
#include "storage/local_sqlite.h"

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

// TODO: 基于线程池异步处理
static void log_req(const HttpRequest& req) {
  std::string peer = fmt::format("{}:{}", req.from.first, req.from.second);
  spdlog::info("{} {} from {}", req.action, req.raw_q, peer);
  std::string user_agent = req.headers.at(header::UserAgent.name);
  std::string now_str = utils::time_now_str();
  //
  std::string sql = fmt::format(R"(INSERT INTO access_log (peer, http_action, query_path, user_agent, created_time) VALUES ("{}", "{}", "{}", "{}", "{}"))",
    peer, req.action, req.q.path, user_agent, now_str);
  if (storage::LocalSqlite::singleton().exec(sql) != 1) {
    spdlog::warn("Failed to log req");
  }
}

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
  DeferGuard defer_guard([&]() {file_stream.close();});
  if (file_stream.fail()) {
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  std::string resp_content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
  resp->with_body(resp_content);
  std::string suffix_type = utils::find_suffix_type(path);
  //
  if (suffix_type == "html" || suffix_type == "htm") {
    log_req(req);
  }
  //
  if (!suffix_type.empty() && FILE_SUFFIX_TYPE_M_CONTENT_TYPE.contains(suffix_type)) {
    resp->with_header(header::ContentType.name, FILE_SUFFIX_TYPE_M_CONTENT_TYPE[suffix_type].type_name);
  }
}

// 获取 pv & uv 统计数据
static void access_stat_handler(const HttpRequest& req, const HttpResponsePtr& resp, const std::function<void(HttpResponsePtr)>& cb) {

}

// 工具类
// /tool/echo/
static void simple_echo_handler(const HttpRequest& req, const HttpResponsePtr& resp, const std::function<void(HttpResponsePtr)>& cb) {
  DoneCallbackGuard guard {cb, resp}; // guard
  nlohmann::ordered_json resp_json;
  resp_json["From"] = fmt::format("{}:{}", req.from.first, req.from.second);
  resp_json["Action"] = req.action;
  resp_json["Query"] = req.raw_q;
  resp_json["HttpVersion"] = req.http_version;
  auto headers = inja::json({});
  for (auto [k, v] : req.headers) {
    headers[k] = v;
  }
  resp_json["Headers"] = headers;
  resp_json["Body"] = req.body;
  const std::string resp_content = resp_json.dump(2);
  // spdlog::debug("resp_content: {}", resp_content);
  resp->with_body(resp_content);
  resp->with_code(HttpStatusCode::OK);
  resp->with_header(header::ContentType.name, "application/json,charset=utf-8");
}

// 为一些没有提供 rss 的博客/站点提供 rss 生成服务
static void rss_provider_handler(const HttpRequest& req, const HttpResponsePtr& resp, const std::function<void(HttpResponsePtr)>& cb) {

}

}