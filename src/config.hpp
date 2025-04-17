#pragma once

#include <string>
#include <vector>
#include <toml.hpp>

class Config {
public:
  Config() = default;
  void parse();

public:
  std::string site_name;
  std::string theme;
  std::vector<std::string> plugins;
  std::string dist_dir;
  //
  toml::basic_value<toml::type_config> raw_toml_;
};

inline void Config::parse() {
  site_name = toml::find_or_default<std::string>(raw_toml_, "site_name");
  theme = toml::find_or_default<std::string>(raw_toml_, "theme");
  toml::find_or_default<std::vector<std::string>>(raw_toml_, "plugins");
  dist_dir = toml::find_or<std::string>(raw_toml_, "dist_dir", "dist");
}

using ConfigPtr = std::shared_ptr<Config>;