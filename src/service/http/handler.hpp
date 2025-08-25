#pragma once

#include <absl/strings/escaping.h>
#include <cpr/cpr.h>

#include <filesystem>
#include <fstream>
#include <utility>
#include <utils/executor.hpp>

#include "protocol.hpp"
#include "storage/local_sqlite.h"
#include "storage/hn_hnsw.hpp"
#include "utils//time.hpp"
#include "utils/guard.hpp"
#include "utils/rss.hpp"
#include "utils/strings.hpp"
#include "utils/ollama.hpp"

namespace ling::http {

using RouteHandler = void (*)(const HttpRequest&, const HttpResponsePtr&, const std::function<void(HttpResponsePtr)>&);
using DoneCallback = std::function<void(HttpResponsePtr)>;

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

static void log_req(const HttpRequest& req) {
  std::string peer = fmt::format("{}:{}", req.from.first, req.from.second);
  spdlog::info("{} {} from {}", req.action, req.raw_q, peer);
  //
  std::string user_agent = req.headers.at(header::UserAgent);
  std::string now_str = utils::time_now_str();
  auto action = req.action;
  auto path = req.q.path;
  //
  utils::default_executor().async_execute([peer, action, path, user_agent, now_str]() {
    std::string sql = fmt::format(
      R"(INSERT INTO access_log (peer, http_action, query_path, user_agent, created_time) VALUES ("{}", "{}", "{}", "{}", "{}"))",
      peer, action, path, user_agent, now_str);
    if (storage::LocalSqlite::singleton().exec(sql) != 1) {
      spdlog::warn("Failed to log req");
    }
  });
}

// 限流响应 429
static void rate_limited_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  DoneCallbackGuard guard{cb, resp};
  resp->with_code(HttpStatusCode::TOO_MANY_REQUESTS);
}

// 兜底，静态文件请求处理
static void static_file_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  std::string path = std::string(req.q.path);
  if (path[path.size() - 1] == '/') {
    path += "index.html";
  }
  DoneCallbackGuard guard{cb, resp};  // guard
  std::filesystem::path file_path{"." + path};
  if (!exists(file_path)) {
    resp->with_body(CODE2MSG[HttpStatusCode::NOT_FOUND]);
    resp->with_code(HttpStatusCode::NOT_FOUND);
    return;
  }
  std::string suffix_type = utils::find_suffix_type(path);
  if (suffix_type == "html" || suffix_type == "htm") {
    log_req(req);
  }
  std::ifstream file_stream;
  file_stream.open(file_path);
  utils::DeferGuard defer_guard([&]() { file_stream.close(); });
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

// 工具类
// /tool/echo/
static void simple_echo_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  DoneCallbackGuard guard{cb, resp};  // guard
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
  resp->with_header(header::ContentType, content_type::JSON);
}

// /tool/base64
static void base64_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  DoneCallbackGuard guard{cb, resp};
  auto src = req.body;
  if (src.empty()) {
    const auto v_iter = req.q.params.find("src");
    if (v_iter == req.q.params.end()) {
      resp->with_body(CODE2MSG[HttpStatusCode::BAD_REQUEST]);
      resp->with_code(HttpStatusCode::BAD_REQUEST);
      return;
    }
    src = v_iter->second;
  }
  const bool for_web = req.q.params.find("for_web") != req.q.params.end();
  const bool to_decode = req.q.params.find("to_decode") != req.q.params.end();
  //
  std::string target;
  if (to_decode) {  // decode
    for_web ? absl::WebSafeBase64Unescape(src, &target) : absl::Base64Unescape(src, &target);
  } else {  // encode
    for_web ? absl::WebSafeBase64Escape(src, &target) : absl::Base64Escape(src, &target);
  }
  //
  nlohmann::json resp_json;
  resp_json["src"] = src;
  resp_json["target"] = target;
  resp->with_body(resp_json.dump(2));
  resp->with_header(header::ContentType, FILE_SUFFIX_TYPE_M_CONTENT_TYPE["json"].type_name);
  resp->with_code(HttpStatusCode::OK);
}

static void rss_register_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  DoneCallbackGuard guard{cb, resp};
  //
  std::string rss_url;
  std::string rss_title;
  std::string rss_tag;
  // json
  if (req.headers.at(header::ContentType) == content_type::JSON) {
    auto j = nlohmann::json::parse(req.body);
    if (j.contains("url")) {
      rss_url = j["url"];
    }
    if (j.contains("title")) {
      rss_title = j["title"];
    }
    if (j.contains("tag")) {
      rss_tag = j["tag"];
    }
  } else if (req.headers.at(header::ContentType) == content_type::X_WWW_FORM_URL_ENCODED) {
    UrlEncodedFormBody body;
    body.parse(req.body);
    rss_url = body.params["url"];
    rss_title = body.params["title"];
    rss_tag = body.params["tag"];
  }
  if (rss_url.empty()) {
    resp->with_code(HttpStatusCode::BAD_REQUEST);
    return;
  }
  nlohmann::json resp_json;
  resp->with_header(header::ContentType, content_type::JSON);
  resp->with_code(HttpStatusCode::OK);
  //
  auto& si = storage::LocalSqlite::singleton();
  auto qr_ptr = si.query(fmt::format(R"(SELECT id FROM rss_subscription WHERE url="{}")", rss_url));
  if (!qr_ptr->rows.empty()) {
    resp_json["code"] = 100, resp_json["msg"] = "已订阅过";
    resp->with_body(resp_json.dump(2));
    return;
  }
  //
  cpr::Response r = cpr::Get(cpr::Url{rss_url});
  if (r.status_code != cpr::status::HTTP_OK) {
    resp_json["code"] = 101;
    resp_json["msg"] = "failure to access this url";
    resp->with_body(resp_json.dump(2));
    return;
  }
  utils::RSS rss;
  rss.parse(r.text);
  if (rss.parse_result_code() != 0) {
    resp_json["code"] = rss.parse_result_code();
    resp_json["msg"] = rss.parse_result_msg();
    resp->with_body(resp_json.dump(2));
    return;
  }
  // spdlog::debug("rss: {}", r.text);
  if (rss_title.empty()) {
    rss_title = rss.title();
  }
  //
  si.exec(fmt::format(R"(INSERT INTO rss_subscription (title, url, tag, updated_time) VALUES ("{}", "{}", "{}", "{}"))",
                      rss_title, rss_url, rss_tag, rss.updated_time()));
  auto id_qr_ptr = si.query(fmt::format(R"(SELECT id FROM rss_subscription WHERE url="{}")", rss_url));
  if (id_qr_ptr->rows.empty()) {
    throw std::runtime_error{"failure to add rss subscription"};
  }
  auto row0 = id_qr_ptr->rows[0];
  int64_t subscription_id = std::any_cast<int64_t>(row0[0].second);
  //
  bool has_err = false;
  auto trans_ptr = si.with_transaction();
  try {
    for (const auto& entry : rss.entries()) {
      std::string insert_sql = fmt::format(
          R"(INSERT INTO rss_item (subscription_id, title, url, content, has_read, updated_time) VALUES ({}, "{}", "{}", "{}", 0, "{}"))",
          subscription_id, entry.title, entry.link, entry.content, entry.updated_time);
      si.exec(insert_sql);
    }
    trans_ptr->commit();
  } catch (std::runtime_error& err) {
    trans_ptr->rollback();
    spdlog::error("failure to add rss subscription, err: {}", err.what());
    has_err = true;
  }
  if (has_err) {
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    resp_json["code"] = 500;
    resp_json["msg"] = "Internal Error";
  } else {
    resp_json["code"] = 0;
    resp_json["msg"] = "success";
  }
  resp->with_body(resp_json.dump(2));
}

static void search_hacker_news_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  using namespace ling::utils;
  DoneCallbackGuard guard{cb, resp};
  auto j = nlohmann::json::parse(req.body);
  if (!j.contains("query")) {
    resp->with_code(HttpStatusCode::BAD_REQUEST);
    return;
  }
  auto query = j["query"].get<std::string>();
  uint32_t top_k = 100;
  if (j.contains("top_k")) {
    top_k = j["top_k"].get<uint32_t>();
  }
  //
  auto& hn = storage::HackNewsHnsw::singleton();
  auto& meta = hn.meta();
  //
  Ollama oll_model {meta.model_name};
  if (!oll_model.is_model_serving()) {
    spdlog::error("please run model '{}' on ollama first", meta.model_name);
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  std::vector<std::string> model_input;
  model_input.emplace_back(query);
  Embeddings embs = oll_model.generate_embeddings(model_input);
  if (embs.size() != 1) {
    spdlog::error("failure to generate embedding for '{}' with model '{}'", query, meta.model_name);
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  Embedding& query_emb = embs[0];
  if (query_emb.size() != meta.dim) {
    spdlog::error("dim not equal: {} != {}", query_emb.size(), meta.dim);
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    return;
  }
  auto qr = hn.search(query_emb, top_k);
  //
  nlohmann::json resp_data {};
  resp_data["result"] = nlohmann::json::array();
  for (auto& [item_ptr, score] : qr) {
    nlohmann::json one {};
    one["id"] = item_ptr->id;
    one["title"] = item_ptr->title;
    one["url"] = item_ptr->url;
    one["score"] = item_ptr->score;
    one["ann_score"] = score;
    resp_data["result"].emplace_back(one);
  }
  resp->with_code(HttpStatusCode::OK);
  resp->with_header(header::ContentType, FILE_SUFFIX_TYPE_M_CONTENT_TYPE["json"].type_name);
  resp->with_body(resp_data.dump(2));
}

// 为一些没有提供 rss 的博客/站点提供 rss 生成服务
static void rss_provider_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  // TODO:
}

// 获取 pv & uv 统计数据
static void access_stat_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  // TODO:
}

// https://bytebytego.com/courses/system-design-interview/design-a-url-shortener
static void url_shortener_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  // TODO:
}

// 计算器
// - 中缀表示法
// - 前缀表示法 / 波兰表示法
// - 后缀表示法 / 逆波兰表示法
// 支持 整数&浮点数&大整数
static void calculator_handler(const HttpRequest& req, const HttpResponsePtr& resp, const DoneCallback& cb) {
  // TODO:
}

}  // namespace ling::http