#pragma once

#include "plugin.h"

namespace ling::plugin {

// https://www.mathjax.org/

class Mathjax final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
};

inline bool Mathjax::init(ContextPtr& context_ptr) {
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  auto html = R"(<script>
    MathJax = {
        tex: {
            inlineMath: [['$', '$'], ['\\(', '\\)']]
        },
        svg: {
            fontCache: 'global'
        }
    };
</script>
<script type="text/javascript" id="MathJax-script" async src="https://cdn.jsdelivr.net/npm/mathjax@3/es5/tex-chtml.js"></script>)";
  auto& render_ctx = context_ptr->with_render_ctx();
  render_ctx[to_string(FeInjectPos::PLUGIN_AFTER_FOOTER_PARTS)].emplace_back(html);
  return true;
}

static PluginRegister<Mathjax> mathjax_register_ {"Mathjax"};

}