#pragma once

#include "plugin.h"

#include <fmt/core.h>

namespace ling::plugin {

// https://giscus.app/zh-CN

class Giscus final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
};

inline bool Giscus::init(ContextPtr& context_ptr) {
  auto conf_toml = context_ptr->with_config()->raw_toml_;
  auto enable = toml::find_or_default<bool>(conf_toml, "giscus", "enable");
  spdlog::debug(enable ? "giscus enabled" : "giscus disabled");
  if (!enable) {
    return false;
  }
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  auto tpl = R"(<div class="comment">
     <script src="https://giscus.app/client.js"
        data-repo="{}"
        data-repo-id="{}"
        data-category="{}"
        data-category-id="{}"
        data-mapping="pathname"
        data-strict="0"
        data-reactions-enabled="1"
        data-emit-metadata="0"
        data-input-position="top"
        data-theme="light"
        data-lang="zh-CN"
        data-loading="lazy"
        crossorigin="anonymous"
        async>
      </script>
    </div>)";
  auto repo = toml::find_or_default<std::string>(conf_toml, "giscus", "repo");
  auto repo_id = toml::find_or_default<std::string>(conf_toml, "giscus", "repo_id");
  auto category = toml::find_or_default<std::string>(conf_toml, "giscus", "category");
  auto category_id = toml::find_or_default<std::string>(conf_toml, "giscus", "category_id");
  auto html = fmt::format(tpl, repo, repo_id, category, category_id);
  //
  auto& render_ctx = context_ptr->with_render_ctx();
  render_ctx[to_string(FeInjectPos::PLUGIN_AFTER_POST_CONTENT_PARTS)].emplace_back(html);
  return true;
}

static PluginRegister<Giscus> giscus_register_ {"Giscus"};
}