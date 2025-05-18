#pragma once
/*
 * 自动将本地图片上传到 https://smms.app/，并替换页面中的图片链接
 * SM.MS API： https://doc.sm.ms/
 */

/*
 * 1、从本地检测缓存状态（哪些图片已经上传，对应的 url）
 * 2、从 open api 获取上传历史
 * 3、未上传的图片通过 open api，并将状态缓存下来
 */

#include <utility>
#include <filesystem>

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "plugin.h"

namespace ling::plugin {

using json = nlohmann::json;
using path = std::filesystem::path;

static std::string base_url = "https://sm.ms/api/v2";
static path account_file_path {".smms_account.json"};
static path token_file_path {".smms_token.json"};

class SmmsUploadHistory {
public:
  std::string file_name;
  std::string store_name;
  std::string hash;
  uint32_t size;
  std::string created_at;
  std::string url;
  std::string url4del;

public:
  void from(const nlohmann::json& j) {
    file_name = j["filename"].get<std::string>();
    store_name = j["storename"].get<std::string>();
    hash = j["hash"].get<std::string>();
    size = j["size"].get<uint32_t>();
    if (j.contains("created_at")) {
      created_at = j["created_at"].get<std::string>();
    }
    url = j["url"].get<std::string>();
    url4del = j["delete"].get<std::string>();
  }

  friend std::ostream& operator<<(std::ostream& os, const SmmsUploadHistory& h) {
    return os << "{\"file_name\": " << h.file_name
        << ", \"store_name\": " << h.store_name
        << ",\"hash\": " << h.hash
        << ",\"size\": " << h.size
        << ",\"created_at\": " << h.created_at
        << ",\"url\": " << h.url
        << ",\"delete\": " << h.url4del << "}";
  }
};

class SmmsUploadResult {
public:
  bool success;
  SmmsUploadHistory history;
};

using HistoryVec = std::vector<SmmsUploadHistory>;

class SmmsOpenAPI {
public:
  SmmsOpenAPI(std::string username, std::string password): username_(std::move(username)), password_(std::move(password)) {};
  std::string fetch_api_token();
  HistoryVec fetch_upload_history();
  SmmsUploadResult upload(const std::string& image_path);
  bool del(const std::string& hash);

private:
  static std::string fetch_local_token();

private:
  std::string username_;
  std::string password_;
  std::string api_token_;
};

inline std::string SmmsOpenAPI::fetch_local_token() {
  if (!std::filesystem::exists(account_file_path)) {
    return "";
  }
  std::ifstream account_file(account_file_path);
  if (!account_file.is_open()) {
    return "";
  }
  const auto j_account = nlohmann::json::parse(account_file);
  if (!j_account.contains("token")) {
    return "";
  }
  return j_account["token"].get<std::string>();
}

inline std::string SmmsOpenAPI::fetch_api_token() {
  if (!api_token_.empty()) {
    return api_token_;
  }
  const auto local_saved_token = fetch_local_token();
  if (!local_saved_token.empty()) {
    api_token_ = local_saved_token;
    return api_token_;
  }
  const auto url = base_url + "/token";
  cpr::Response r = cpr::Post(cpr::Url{url},
    cpr::Parameters{{"username", username_}, {"password", password_}});
  if (r.status_code != 200) {
    spdlog::error("Failed to fetch api token, code: {}, resp: {}", r.status_code, r.text);
    return "";
  }
  const auto rj = json::parse(r.text);
  if (!rj.contains("success") || !rj["success"].is_boolean() || rj["success"].get<bool>() == false) {
    spdlog::error("Failed to fetch api token, resp: {}", r.text);
    return "";
  }
  if (!rj.contains("data") || !rj["data"].is_object() || !rj["data"].contains("token")) {
    spdlog::error("Failed to fetch api token, illegal resp: {}", r.text);
    return "";
  }
  api_token_ = rj["data"]["token"].get<std::string>();
  return api_token_;
}

inline HistoryVec SmmsOpenAPI::fetch_upload_history() {
  const auto api_token = fetch_api_token();
  const auto url = base_url + "/upload_history";
  HistoryVec history_vec {};
  //
  const auto fetch_one_page = [api_token, url, &history_vec](const int page) -> std::pair<bool, int> {
    // page 传参好像有点问题
    cpr::Response r = cpr::Get(cpr::Url{url},
    cpr::Header{{"Authorization", api_token}});
    if (r.status_code != 200) {
      spdlog::error("Failed to fetch upload history, status_code: {}, resp: {}",  r.status_code, r.text);
      return std::make_pair(false, 0);
    }
    const auto rj = json::parse(r.text);
    if (!rj.contains("success") || !rj["success"].is_boolean() || rj["success"].get<bool>() == false) {
      spdlog::error("Failed to fetch upload history, resp: {}", r.text);
      return std::make_pair(false, 0);
    }
    if (!rj.contains("data") || !rj["data"].is_array()) {
      spdlog::error("Failed to fetch upload history, illegal resp: {}", r.text);
      return std::make_pair(false, 0);
    }
    for (const auto& h : rj["data"]) {
      history_vec.push_back(SmmsUploadHistory{});
      auto& hi = history_vec.back();
      hi.from(h);
    }
    const uint32_t current_page = rj["CurrentPage"].get<uint32_t>();
    const uint32_t total_pages = rj["TotalPages"].get<uint32_t>();
    uint32_t next_page = 0;
    if (total_pages > current_page) {
      next_page = current_page + 1;
    }
    return std::make_pair(true, next_page);
  };
  //
  int next_page = 1;
  while (next_page > 0) {
    auto [fst, snd] = fetch_one_page(next_page);
    if (!fst) {
      break;
    }
    next_page = snd;
  }
  return history_vec;
}

inline SmmsUploadResult SmmsOpenAPI::upload(const std::string& image_path) {
  SmmsUploadResult upload_result {};
  upload_result.success = false;
  //
  const path p {image_path};
  const auto api_token = fetch_api_token();
  const auto url = base_url + "/upload";
  //
  cpr::Response r = cpr::Post(cpr::Url{url},
    cpr::Header{{"Content-Type", "multipart/form-data"}, {"Authorization", api_token}},
    cpr::Multipart{{"smfile", cpr::File(p.string(), p.filename())}, {"format", "json"}});
  if (r.status_code != 200) {
    spdlog::error("Failed to fetch upload, status_code: {}, resp: {}", r.status_code, r.text);
    return upload_result;
  }
  const auto rj = json::parse(r.text);
  if (!rj.contains("success") || !rj["success"].is_boolean() || rj["success"].get<bool>() == false ||
    !rj.contains("data") || !rj["data"].is_object()) {
    spdlog::error("Failed to fetch upload image, resp: {}", r.text);
    return upload_result;
  }
  upload_result.success = true;
  upload_result.history.from(rj["data"]);
  return upload_result;
}

inline bool SmmsOpenAPI::del(const std::string& hash) {
  const auto api_token = fetch_api_token();
  const auto url = base_url + "/delete/" + hash;
  cpr::Response r = cpr::Get(cpr::Url{url},
    cpr::Header{{"Authorization", api_token}},
    cpr::Parameters({{"format", "json"}}));
  if (r.status_code != 200) {
    spdlog::error("Failed to delete image, hash: {}, status_code: {}, resp: {}", hash, r.status_code, r.text);
    return false;
  }
  const auto rj = json::parse(r.text);
  if (!rj.contains("success") || !rj["success"].is_boolean() || rj["success"].get<bool>() == false) {
    spdlog::error("Failed to delete image, resp: {}", r.text);
    return false;
  }
  return true;
}

class Smms final : public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const MarkdownPtr& md_ptr) override;

private:
  ConfigPtr config_;
};

inline bool Smms::init(ConfigPtr config_ptr) {
  inited_ = true;
  return true;
}


inline bool Smms::run(const MarkdownPtr& md_ptr) {
  return true;
}

static PluginRegister<Smms> smms_register_ {"Smms"};

}