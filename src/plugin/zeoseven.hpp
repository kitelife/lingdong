#pragma once

#include <spdlog/spdlog.h>

#include "plugin.h"

namespace ling::plugin {

// https://fonts.zeoseven.com/

class ZeoSeven final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
};

inline bool ZeoSeven::init(ContextPtr& context_ptr) {
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  const auto& conf = context_ptr->with_config();
  const auto& raw_toml = conf->raw_toml_;
  auto font_id = toml::find_or<uint32_t>(raw_toml, "ZeoSeven",  "font_id", 0);
  auto font_name = toml::find_or<std::string>(raw_toml, "ZeoSeven", "font_name", "");
  auto font_weight = toml::find_or<std::string>(raw_toml, "ZeoSeven", "font_weight", "normal");
  if (font_id == 0 || font_name.empty()) {
    spdlog::warn("not specify zeoseven font");
    return false;
  }
  auto css = fmt::format(R"(<style>
@import url("https://fontsapi.zeoseven.com/{}/main/result.css");
body {{
    font-family: {};
    font-weight: {};
}}
</style>)", font_id, font_name, font_weight);
  auto& render_ctx = context_ptr->with_render_ctx();
  render_ctx[to_string(FeInjectPos::PLUGIN_HEAD_PARTS)].emplace_back(css);
  return true;
}

static PluginRegister<ZeoSeven> zeoseven_register_ {"ZeoSeven"};

}