#pragma once

#include "plugin.h"

namespace ling::plugin {

class Highlight final : public Plugin {
public:
  bool init(ContextPtr context_ptr) override;
};

inline bool Highlight::init(ContextPtr context_ptr) {
  auto& render_ctx = context_ptr->with_render_ctx();
  //
  auto css_part = R"(<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/styles/default.min.css">)";
  render_ctx["_HEAD_PLUGIN_PARTS"].emplace_back(css_part);
  //
  auto js_part = R"(<script src="https://cdnjs.cloudflare.com/ajax/libs/highlight.js/11.9.0/highlight.min.js"></script>
<script>hljs.highlightAll();</script>)";
  render_ctx["_AFTER_FOOTER_PLUGIN_PARTS"].emplace_back(js_part);
  //
  inited_ = true;
  return true;
}

static PluginRegister<Highlight> highlight_register_ {"Highlight"};

}