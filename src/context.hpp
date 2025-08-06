#pragma once

#include "config.hpp"

namespace ling {

using namespace std::filesystem;
using RenderCtx = inja::json;

enum class FeInjectPos {
  PLUGIN_HEAD_PARTS,
  PLUGIN_AFTER_POST_CONTENT_PARTS,
  PLUGIN_AFTER_FOOTER_PARTS,
};

inline std::string to_string(const FeInjectPos p) {
  switch (p) {
    case FeInjectPos::PLUGIN_HEAD_PARTS:
      return "PLUGIN__HEAD_PARTS";
    case FeInjectPos::PLUGIN_AFTER_POST_CONTENT_PARTS:
      return "PLUGIN__AFTER_POST_CONTENT_PARTS";
    case FeInjectPos::PLUGIN_AFTER_FOOTER_PARTS:
      return "PLUGIN__AFTER_FOOTER_PARTS";
    default:
      return "";
  }
}

class Context final {
public:
  static std::shared_ptr<Context>& singleton() {
    static std::shared_ptr<Context> ctx_ = std::make_shared<Context>();
    return ctx_;
  }
  //
  Context() {
    render_ctx_[to_string(FeInjectPos::PLUGIN_HEAD_PARTS)] = inja::json::array();
    render_ctx_[to_string(FeInjectPos::PLUGIN_AFTER_POST_CONTENT_PARTS)] = inja::json::array();
    render_ctx_[to_string(FeInjectPos::PLUGIN_AFTER_FOOTER_PARTS)] = inja::json::array();
  }

  ConfigPtr& with_config() {
    return config_ptr_;
  }

  RenderCtx& with_render_ctx() {
    return render_ctx_;
  }

private:
  ConfigPtr config_ptr_ = std::make_shared<Config>();
  RenderCtx render_ctx_;
};

using ContextPtr = std::shared_ptr<Context>;

}