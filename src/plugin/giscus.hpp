#pragma once

#include "plugin.h"

#include <fmt/core.h>

namespace ling::plugin {

class Giscus final : public Plugin {
public:
  bool init(ContextPtr context_ptr) override;
};

inline bool Giscus::init(ContextPtr context_ptr) {
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
  const auto& giscus_conf = context_ptr->with_config()->giscus;
  auto html = fmt::format(tpl, giscus_conf.repo, giscus_conf.repo_id, giscus_conf.category, giscus_conf.category_id);
  //
  auto& render_ctx = context_ptr->with_render_ctx();
  render_ctx["_AFTER_POST_CONTENT_PLUGIN_PARTS"].emplace_back(html);
  //
  inited_ = true;
  return true;
}

static PluginRegister<Giscus> giscus_register_ {"Giscus"};
}