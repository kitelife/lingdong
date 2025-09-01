#pragma once

#include <cpr/api.h>
#include <spdlog/spdlog.h>

#include "plugin.h"
#include "utils/helper.hpp"

namespace ling::plugin {

// https://docs.mathjax.org/en/latest/server/examples.html

class BeMathJax final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
  bool run(const MarkdownPtr& md_ptr) override;
  bool destroy() override;

private:
  static bool is_mathjax_installed();
  static bool install_mathjax();
  static bool run_mathjax_render_server(uint32_t port);

  std::string mathjax_svg(const std::string& tex, bool is_block=false);
  uint32_t port_ {0};
};

static path MJS_FILE_PATH {"./.mathjax_server.mjs"};

static std::string NODE_HTTP_SERVER_CODE = R"(const http = require('http');
import MathJax from 'mathjax';
await MathJax.init({
  loader: {load: ['input/tex', 'output/svg']}
});

const adaptor = MathJax.startup.adaptor;

const server = http.createServer(async (request, response) => {
    let url = new URL(`http://${process.env.HOST ?? 'localhost'}${request.url}`);
    if (!url.searchParams.has("mathjax_tex")) {
        response.writeHead(400, { 'Content-Type': 'application/json' });
        response.end(JSON.stringify({
            data: '请求缺少必要参数',
        }));
        return;
    }
    let mathjax_tex = url.searchParams.get("mathjax_tex");
    let with_outer = url.searchParams.get("with_outer");
    console.log(mathjax_tex);
    let svg_view = await MathJax.tex2svgPromise(mathjax_tex, {display: true});
    if (with_outer == "true") {
       svg_view = adaptor.outerHTML(svg_view);
    } else {
       svg_view = adaptor.innerHTML(svg_view);
    }
    console.log(svg_view);
    response.writeHead(200, { 'Content-Type': 'application/json' });
    response.end(JSON.stringify({
        svg: svg_view,
    }));
});
let port = 8181;
if (process.argv.length >= 3) {
    port = parseInt(process.argv[2]);
}
server.listen(port);)";

inline bool BeMathJax::init(ContextPtr& context_ptr) {
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  if (!utils::is_npm_exist() || !utils::is_jq_exist()) {
    spdlog::error("npm or jq has been installed");
    return false;
  }
  if (!is_mathjax_installed()) {
    spdlog::info("try to install mathjax");
    if (!install_mathjax()) {
      spdlog::error("failure to install mathjax");
      return false;
    }
  }
  auto& conf_ptr = context_ptr->with_config();
  port_ = toml::find_or<uint32_t>(conf_ptr->raw_toml_, "BeMathJax", "server_port", 8181);
  auto status = run_mathjax_render_server(port_);
  std::this_thread::sleep_for(std::chrono::seconds(5));
  return status;
}

inline bool BeMathJax::run_mathjax_render_server(uint32_t port) {
  if (!exists(MJS_FILE_PATH) && !utils::write_file(MJS_FILE_PATH, NODE_HTTP_SERVER_CODE)) {
    spdlog::error("failure to write file {}", MJS_FILE_PATH.string());
    return false;
  }
  const auto cmd = fmt::format("nohup node {} {} > /dev/null 2>&1 &", MJS_FILE_PATH.string(), port);
  return system(cmd.c_str()) == 0;
}

inline std::string BeMathJax::mathjax_svg(const std::string& tex, bool is_block) {
  static std::string url = fmt::format("http://127.0.0.1:{}/", port_);
  uint32_t retries = 0;
  do {
    auto r = cpr::Get(cpr::Url{url},
    cpr::Parameters{{"mathjax_tex", tex}, {"with_outer", is_block ? "true" : "false"}},
    cpr::ConnectTimeout{std::chrono::milliseconds(500)},
    cpr::Timeout{std::chrono::milliseconds{1000}});
    if (r.status_code == 200) {
      auto j = nlohmann::json::parse(r.text);
      if (j.contains("svg")) {
        if (is_block) {
          return "<p>" + j["svg"].get<std::string>() + "</p>";
        }
        return j["svg"].get<std::string>();
      }
    }
    retries++;
  } while (retries < 3);
  return "";
}

inline bool BeMathJax::run(const MarkdownPtr& md_ptr) {
  if (md_ptr == nullptr) {
    return false;
  }
  if (!inited_) {
    spdlog::error("Init before run!");
    return false;
  }
  for (auto& ele : md_ptr->elements()) {
    if (ele == nullptr) {
      continue;
    }
    auto* latex_block = dynamic_cast<LatexBlock*>(ele.get());
    if (latex_block == nullptr) {
      continue;
    }
    if (latex_block->content().empty()) {
      spdlog::warn("empty latex math");
      continue;
    }
    const auto svg = mathjax_svg(latex_block->content(), true);
    if (svg.empty()) {
      continue;
    }
    auto* svg_ele = new HtmlElement();
    svg_ele->tag_name = "svg";
    svg_ele->html = svg;
    ele.reset(svg_ele);
  }
  for (auto& p : md_ptr->paragraphs()) {
    for (auto& block : p->blocks) {
      if (block->type_ != FragmentType::LATEX) {
        continue;
      }
      auto* inline_latex = dynamic_cast<InlineLatex*>(block.get());
      if (inline_latex->content().empty()) {
        continue;
      }
      const auto svg = mathjax_svg(inline_latex->content());
      if (svg.empty()) {
        continue;
      }
      auto* text_block = new Text(FragmentType::PLAIN, svg);
      block.reset(text_block);
    }
  }
  return true;
}

inline bool BeMathJax::destroy() {
  const auto server_pid = utils::get_cmd_stdout(R"(ps aux | grep "node ./.mathjax_server.mjs" | grep -v "grep" | awk -F' ' '{print $2}')");
  spdlog::debug("BeMathJax server id: {}", server_pid);
  auto status = system(fmt::format("kill -n 9 {}", server_pid).c_str()) == 0;
  if (exists(MJS_FILE_PATH)) {
    status = status && std::filesystem::remove(MJS_FILE_PATH);
  }
  return status;
}

inline bool BeMathJax::is_mathjax_installed() {
  const auto query_result = utils::get_cmd_stdout("npm query '#mathjax' | jq '.[] | select(.name  | type == \"string\") | .name'");
  return query_result == "\"mathjax\"\n";
}

inline bool BeMathJax::install_mathjax() {
  return system("npm install mathjax@4 2>&1") == 0;
}

static PluginRegister<BeMathJax> bemathjax_register_ {"BeMathjax"};

}