//
// Created by xiayf on 2025/5/9.
//
#pragma once

#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include "../config.hpp"
#include "plugin.h"

// 为了执行 static 语句
#include "plantuml.hpp"
#include "mermaid.hpp"
#include "small_image.hpp"
#include "smms.hpp"
//

namespace ling::plugin {

class Plugins final : public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const ParserPtr& parser_ptr) override;
  bool destroy() override;

private:
  std::map<std::string, PluginPtr> plugins_;
};

inline bool Plugins::init(ConfigPtr config_ptr) {
  for (const auto& pn : config_ptr->plugins) {
    if (plugin_factory_m[pn] == nullptr) {
      spdlog::error("Has no plugin named {}", pn);
      continue;
    }
    auto plugin_ptr = plugin_factory_m[pn]();
    if (!plugin_ptr->init(config_ptr)) {
      spdlog::error("Failed to init plugin {}", pn);
      continue;
    }
    plugins_[pn] = plugin_ptr;
  }
  return true;
}

inline bool Plugins::run(const ParserPtr& parser_ptr) {
  for (const auto& [pn, plugin] : plugins_) {
    if (!plugin->run(parser_ptr)) {
      spdlog::error("Failed to run plugin {0} on {}", pn);
    }
  }
  return true;
}

inline bool Plugins::destroy() {
  for (const auto& [pn, plugin] : plugins_) {
    if (!plugin->destroy()) {
      spdlog::error("Failed to destroy plugin {}", pn);
    }
  }
  return true;
}

}