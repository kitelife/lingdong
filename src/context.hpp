#pragma once

#include "config.hpp"

namespace ling {

using namespace std::filesystem;
using RenderCtx = inja::json;

class Context final {
public:
  static std::shared_ptr<Context>& singleton() {
    static std::shared_ptr<Context> ctx_ = std::make_shared<Context>();
    return ctx_;
  }
  //
  Context() {
    render_ctx_["_HEAD_PLUGIN_PARTS"] = inja::json::array();
    render_ctx_["_AFTER_POST_CONTENT_PLUGIN_PARTS"] = inja::json::array();
    render_ctx_["_AFTER_FOOTER_PLUGIN_PARTS"] = inja::json::array();
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