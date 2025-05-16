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

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <nlohmann/json.hpp>

#include "plugin.h"

namespace ling::plugin {

using json = nlohmann::json;

static std::string base_url = "https://sm.ms/api/v2";

class SmmsUploadHistory {};
class SmmsUploadResult {};

using HistoryVec = std::vector<SmmsUploadHistory>;

class SmmsOpenAPI {
public:
  SmmsOpenAPI(std::string username, std::string password): username_(std::move(username)), password_(std::move(password)) {};
  std::string fetch_api_token();
  HistoryVec fetch_upload_history();
  SmmsUploadResult upload(const std::string image_path);

private:
  std::string username_;
  std::string password_;
  std::string api_token_;
};

inline std::string SmmsOpenAPI::fetch_api_token() {
  if (api_token_ != "") {
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
  return HistoryVec{};
}

inline SmmsUploadResult SmmsOpenAPI::upload(const std::string image_path) {
  return SmmsUploadResult{};
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