#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include <toml.hpp>
#include <inja/inja.hpp>

namespace ling {

using namespace std::filesystem;

class Theme final {
public:
  explicit Theme(path theme_path) : base_path_(std::move(theme_path)) {
    static_path_ = base_path_ / path("static");
    template_path_ = base_path_ / path("templates");
  }

public:
  path static_path_;
  path template_path_;
  //
  path template_index{"index.html"};
  path template_post{"post.html"};
  path template_posts{"posts.html"};
  path template_rss{"rss.xml"};

private:
  path base_path_;
};

using ThemePtr = std::shared_ptr<Theme>;

class Storage {
public:
  Storage() = default;
  void parse(const toml::basic_value<toml::type_config>& raw_toml_);

  std::string db_file_path;
  std::vector<std::string> init_sql;
};

inline void Storage::parse(const toml::basic_value<toml::type_config>& raw_toml_) {
  db_file_path = toml::find_or_default<std::string>(raw_toml_, "storage", "db_file_path");
  init_sql = toml::find_or_default<std::vector<std::string>>(raw_toml_, "storage", "init_sql");
}

using StoragePtr = std::shared_ptr<Storage>;

class ServerConf {
public:
  ServerConf() = default;
  void parse(const toml::basic_value<toml::type_config>& raw_toml_);

  uint32_t global_rate_limit;
  uint32_t per_client_rate_limit;
};

inline void ServerConf::parse(const toml::basic_value<toml::type_config>& raw_toml_) {
  global_rate_limit = toml::find_or_default<uint32_t>(raw_toml_, "server", "global_rate_limit_per_sec");
  per_client_rate_limit = toml::find_or_default<uint32_t>(raw_toml_, "server", "per_client_rate_limit_per_sec");
}

using ServerConfPtr = std::shared_ptr<ServerConf>;

class Config {
public:
  Config() = default;
  void parse();
  void assemble(inja::json& render_params);

public:
  std::string site_title;
  std::string site_url;
  std::string site_desc;
  std::string site_ico;
  std::vector<std::pair<std::string, std::string>> navigation;
  //
  ThemePtr theme_ptr;
  Storage storage {};
  ServerConf server_conf {};
  std::vector<std::string> plugins;
  std::string dist_dir;
  //
  toml::basic_value<toml::type_config> raw_toml_;
};

inline void Config::parse() {
  site_title = toml::find_or_default<std::string>(raw_toml_, "site_title");
  site_url = toml::find_or_default<std::string>(raw_toml_, "site_url");
  site_desc = toml::find_or_default<std::string>(raw_toml_, "site_desc");
  site_ico = toml::find_or_default<std::string>(raw_toml_, "site_ico");
  //
  auto vv_navigation = toml::find_or_default<std::vector<std::vector<std::string>>>(raw_toml_, "navigation");
  navigation.reserve(vv_navigation.size());
  for (const auto& pair : vv_navigation) {
    if (pair.size() != 2) {
      continue;
    }
    navigation.emplace_back(pair[0], pair[1]);
  }
  //
  const std::string theme_dir = toml::find_or_default<std::string>(raw_toml_, "theme_dir");
  const std::string theme_name = toml::find_or_default<std::string>(raw_toml_, "theme_name");
  theme_ptr = std::make_shared<Theme>((path(theme_dir) / path(theme_name)).string());
  //
  storage.parse(raw_toml_);
  server_conf.parse(raw_toml_);
  //
  plugins = toml::find_or_default<std::vector<std::string>>(raw_toml_, "plugins");
  dist_dir = toml::find_or<std::string>(raw_toml_, "dist_dir", "dist");
}

inline void Config::assemble(inja::json& render_params) {
  render_params["SITE_TITLE"] = site_title;
  render_params["SITE_URL"] = site_url;
  render_params["SITE_DESC"] = site_desc;
  if (!site_ico.empty()) {
    render_params["SITE_ICO"] = site_ico;
  }
  //
  size_t idx = 0;
  for (const auto& [fst, snd] : navigation) {
    render_params["navigation"][idx] = {{"url", snd}, {"name", fst}};
    idx++;
  }
}

using ConfigPtr = std::shared_ptr<Config>;
}