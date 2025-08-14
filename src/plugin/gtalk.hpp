#pragma once

#include "plugin.h"

#include <fmt/format.h>
#include <toml.hpp>
#include <spdlog/spdlog.h>

namespace ling::plugin {

// https://github.com/gitalk/gitalk

class Gtalk final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
};

inline bool Gtalk::init(ContextPtr& context_ptr) {
  auto& conf_toml = context_ptr->with_config()->raw_toml_;
  bool enable = toml::find_or_default<bool>(conf_toml, "gtalk", "enable");
  spdlog::debug(enable ? "gtalk enabled" : "gtalk disabled");
  if (!enable) {
    return false;
  }
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  auto client_id = toml::find_or_default<std::string>(conf_toml, "gtalk", "client_id");
  auto client_secret = toml::find_or_default<std::string>(conf_toml, "gtalk", "client_secret");
  auto repo = toml::find_or_default<std::string>(conf_toml, "gtalk", "repo");
  auto owner = toml::find_or_default<std::string>(conf_toml, "gtalk", "owner");
  auto admin = toml::find_or_default<std::string>(conf_toml, "gtalk", "admin");
  //
  auto css_part = R"(<link rel="stylesheet" href="https://unpkg.com/gitalk/dist/gitalk.css">)";
  auto js_part = fmt::format(R"(<div class="comment">
<div id="gitalk-container"></div>
<script defer src="https://unpkg.com/gitalk/dist/gitalk.min.js"></script>
const gitalk = new Gitalk({
  clientID: '{}',
  clientSecret: '{}',
  repo: '{}',
  owner: '{}',
  admin: ['{}'],
  id: location.pathname,
  distractionFreeMode: false
});
gitalk.render('gitalk-container');
</div>)", client_id, client_secret, repo, owner, admin);
  //
  auto& render_ctx = context_ptr->with_render_ctx();
  render_ctx[to_string(FeInjectPos::PLUGIN_HEAD_PARTS)].emplace_back(css_part);
  render_ctx[to_string(FeInjectPos::PLUGIN_AFTER_POST_CONTENT_PARTS)].emplace_back(js_part);
  //
  return true;
}

static PluginRegister<Gtalk> gtalk_register_ {"Gtalk"};

}