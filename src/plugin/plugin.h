//
// Created by xiayf on 2025/4/11.
//
#pragma once

#include "../context.hpp"
#include "../parser/markdown.h"

namespace ling::plugin {

class Plugin {
public:
  virtual ~Plugin() = default;
  virtual bool init(ContextPtr context_ptr) {
    return true;
  }
  virtual bool run(const MarkdownPtr& md_ptr) {
    return true;
  };
  virtual bool destroy() {
    return true;
  }

  bool is_initialized() const {
    return inited_;
  }

protected:
  bool inited_ = false;
};

using PluginPtr = std::shared_ptr<Plugin>;
using PluginPtrCreator = std::function<PluginPtr()>;
static std::unordered_map<std::string, PluginPtrCreator> plugin_factory_m;

template<class PluginType>
PluginPtr plugin_creator() {
  return std::make_shared<PluginType>();
}

template<class PluginType>
class PluginRegister {
public:
  explicit PluginRegister(const std::string& plugin_name) {
    plugin_factory_m[plugin_name] = plugin_creator<PluginType>;
  }
};

} // namespace ling::plugin