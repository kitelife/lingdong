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
#include <tsl/robin_map.h>

#include "plugin.h"
#include "../utils/strings.hpp"

namespace ling::plugin {

using json = nlohmann::json;
using namespace std::filesystem;

static std::string BASE_URL = "https://sm.ms/api/v2";
static std::string CACHE_DIR = ".smms_cache";
static std::string UPLOAD_HISTORY_CACHE_FILE = "upload_history.json";

class SmmsUploadHistory {
public:
  std::string file_name;
  std::string store_name;
  std::string hash;
  uint32_t size {0};
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

  void to(nlohmann::json& j) const {
    j["filename"] = file_name;
    j["storename"] = store_name;
    j["hash"] = hash;
    j["size"] = size;
    j["created_at"] = created_at;
    j["url"] = url;
    j["delete"] = url4del;
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
  SmmsOpenAPI() = default;
  SmmsOpenAPI(std::string username, std::string password): username_(std::move(username)), password_(std::move(password)) {};
  void set_api_token(const std::string& token) {
    this->api_token_ = token;
  }
  std::string fetch_api_token();
  HistoryVec fetch_upload_history();
  SmmsUploadResult upload(const std::string& image_path);
  SmmsUploadResult upload(const path& image_path);
  bool del(const std::string& hash);

private:
  std::string username_;
  std::string password_;
  std::string api_token_;
};

inline std::string SmmsOpenAPI::fetch_api_token() {
  if (!api_token_.empty()) {
    return api_token_;
  }
  const auto url = BASE_URL + "/token";
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
  const auto url = BASE_URL + "/upload_history";
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
  const path p {image_path};
  return upload(p);
}

inline SmmsUploadResult SmmsOpenAPI::upload(const path& image_path) {
  SmmsUploadResult upload_result {};
  upload_result.success = false;
  const auto api_token = fetch_api_token();
  const auto url = BASE_URL + "/upload";
  //
  cpr::Response r = cpr::Post(cpr::Url{url},
    cpr::Header{{"Content-Type", "multipart/form-data"}, {"Authorization", api_token}},
    cpr::Multipart{{"smfile", cpr::File(image_path.string(), image_path.filename())}, {"format", "json"}});
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
  const auto url = BASE_URL + "/delete/" + hash;
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
  bool destroy() override;

private:
  bool load_upload_history();
  bool cache_upload_history();

private:
  ConfigPtr config_;
  SmmsOpenAPI api_;
  //
  tsl::robin_map<std::string, SmmsUploadHistory> upload_history_;
};

inline bool Smms::init(ConfigPtr config_ptr) {
  const auto api_token = toml::find_or<std::string>(config_->raw_toml_, "smms", "api_token", "");
  if (api_token.empty()) {
    const auto username = toml::find_or<std::string>(config_->raw_toml_, "smms", "username", "");
    const auto password = toml::find_or<std::string>(config_->raw_toml_, "smms", "password", "");
    if (username.empty() || password.empty()) {
      spdlog::warn("Must specify username and password for smms plugin");
      return false;
    }
    api_ = SmmsOpenAPI{username, password};
  } else {
    api_ = SmmsOpenAPI{};
    api_.set_api_token(api_token);
  }
  load_upload_history();
  //
  inited_ = true;
  return true;
}

inline bool Smms::run(const MarkdownPtr& md_ptr) {
  if (!inited_) {
    return false;
  }
  for (const auto& ele : md_ptr->elements()) {
    if (ele == nullptr) {
      continue;
    }
    auto* img_ptr = dynamic_cast<Image*>(ele.get());
    if (img_ptr == nullptr) {
      continue;
    }
    const auto& uri = img_ptr->uri;
    if (uri.find("https://") != std::string::npos || uri.find("http://") != std::string::npos) {
      continue;
    }
    auto img_path = path(uri);
    if (upload_history_.contains(img_path.filename())) {
      continue;
    }
    const auto& r = api_.upload(img_path);
    if (!r.success) {
      continue;
    }
    upload_history_[img_path.filename()] = r.history;
    //
    img_ptr->uri = r.history.url;
  }
  return true;
}

inline bool Smms::destroy() {
  return cache_upload_history();
}

inline bool Smms::load_upload_history() {
  const path cache_path {CACHE_DIR};
  if (!exists(cache_path)) {
    create_directories(cache_path);
  }
  HistoryVec vec;
  path history_fp = cache_path / UPLOAD_HISTORY_CACHE_FILE;
  if (exists(history_fp)) {
    const auto jh = nlohmann::json::parse(utils::read_file_all(history_fp));
    for (const auto& j : jh) {
      vec.emplace_back();
      vec.back().from(j);
    }
  } else {
    vec = api_.fetch_upload_history();
  }
  for (const auto& v : vec) {
    upload_history_[v.file_name] = v;
  }
  return true;
}

inline bool Smms::cache_upload_history() {
  nlohmann::json hj = json::array();
  for (const auto& [fst, snd] : upload_history_) {
    hj.emplace_back();
    snd.to(hj.back());
  }
  const std::string hs = hj.dump();
  path history_fp = path(CACHE_DIR) / UPLOAD_HISTORY_CACHE_FILE;
  std::ofstream hfs(history_fp, std::ios::trunc);
  if (!hfs.is_open()) {
    spdlog::error("Failed to open cache file {}", history_fp.string());
    return false;
  }
  hfs << hs;
  hfs.flush();
  hfs.close();
  return true;
}

static PluginRegister<Smms> smms_register_ {"Smms"};

}