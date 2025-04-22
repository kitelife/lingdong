#pragma once

#include <string>
#include <vector>
#include <filesystem>

#include <toml.hpp>

class Config {
public:
  Config() = default;
  void parse();

public:
  std::string site_title;
  std::string site_url;
  std::string site_desc;
  std::string site_ico;
  std::vector<std::string> exclude_source_entries;
  std::vector<std::pair<std::string, std::string>> navigation;
  //
  std::string theme;
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
  const std::string theme_dir = toml::find_or_default<std::string>(raw_toml_, "theme_dir");
  const std::string theme_name = toml::find_or_default<std::string>(raw_toml_, "theme_name");
  theme = (std::filesystem::path(theme_dir) / std::filesystem::path(theme_name)).string();
  //
  toml::find_or_default<std::vector<std::string>>(raw_toml_, "plugins");
  dist_dir = toml::find_or<std::string>(raw_toml_, "dist_dir", "dist");
}

using ConfigPtr = std::shared_ptr<Config>;