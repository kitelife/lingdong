#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include <toml.hpp>
#include <inja/inja.hpp>

namespace ling {

using namespace std::filesystem;

class Giscus final {
public:
  Giscus() = default;
  void parse(toml::basic_value<toml::type_config> raw_toml_);

  bool enable = false;
  std::string repo;
  std::string repo_id;
  std::string category;
  std::string category_id;
};

inline void Giscus::parse(toml::basic_value<toml::type_config> raw_toml_) {
  enable = toml::find_or_default<bool>(raw_toml_, "giscus", "enable");
  spdlog::debug(enable ? "giscus enabled" : "giscus disabled");
  if (!enable) {
    return;
  }
  repo = toml::find_or_default<std::string>(raw_toml_, "giscus", "repo");
  repo_id = toml::find_or_default<std::string>(raw_toml_, "giscus", "repo_id");
  category = toml::find_or_default<std::string>(raw_toml_, "giscus", "category");
  category_id = toml::find_or_default<std::string>(raw_toml_, "giscus", "category_id");
}

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
  std::vector<std::string> exclude_source_entries;
  std::vector<std::pair<std::string, std::string>> navigation;
  Giscus giscus;
  //
  ThemePtr theme_ptr;
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
  exclude_source_entries = toml::find_or_default<std::vector<std::string>>(raw_toml_, "exclude_source_entries");
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
  giscus.parse(raw_toml_);
  //
  const std::string theme_dir = toml::find_or_default<std::string>(raw_toml_, "theme_dir");
  const std::string theme_name = toml::find_or_default<std::string>(raw_toml_, "theme_name");
  theme_ptr = std::make_shared<Theme>((path(theme_dir) / path(theme_name)).string());
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