//
// Created by xiayf on 2025/5/9.
//
#pragma once

#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include "context.hpp"
#include "plugin.h"

// 为了执行 static 语句
#include "mermaid.hpp"
#include "plantuml.hpp"
#include "smms.hpp"
#include "typst_cmarker_pdf.hpp"
#include "giscus.hpp"
#include "highlight.hpp"
#include "mathjax.hpp"
#include "fekatex.hpp"
#include "gtalk.hpp"
#include "bemathjax.hpp"
//

namespace ling::plugin {

class Plugins final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
  bool run(const MarkdownPtr& md_ptr) override;
  bool destroy() override;

private:
  std::map<std::string, PluginPtr> plugins_;
};

inline bool Plugins::init(ContextPtr& context_ptr) {
  for (const auto& pn : context_ptr->with_config()->plugins) {
    if (plugin_factory_m[pn] == nullptr) {
      spdlog::error("Has no plugin named {}", pn);
      continue;
    }
    auto plugin_ptr = plugin_factory_m[pn]();
    if (!plugin_ptr->init(context_ptr)) {
      spdlog::error("Failed to init plugin {}", pn);
      continue;
    }
    plugins_[pn] = plugin_ptr;
  }
  return true;
}

inline bool Plugins::run(const MarkdownPtr& md_ptr) {
  for (const auto& [pn, plugin] : plugins_) {
    if (!plugin->run(md_ptr)) {
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