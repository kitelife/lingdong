#pragma once

#include <absl/hash/hash.h>
#include <absl/strings/str_join.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>

#include "../parser/markdown.h"
#include "absl/strings/strip.h"
#include "plugin.h"

namespace ling::plugin {

using std::filesystem::path;

static std::string get_cmd_stdout(std::string cmd) {
  constexpr int max_buffer = 256;
  char buffer[max_buffer];
  std::string data;
  //
  cmd.append(" 2>&1");
  FILE* stream = popen(cmd.c_str(), "r");
  if (stream) {
    while (!feof(stream))
      if (fgets(buffer, max_buffer, stream) != nullptr)
        data.append(buffer);
    pclose(stream);
  }
  return data;
}

class Mermaid final: public Plugin {
public:
  explicit Mermaid(ConfigPtr  config): config_(std::move(config)) {};
  bool run(const ParserPtr& parser_ptr) override;

private:
  static bool is_npm_exist();
  static bool is_jq_exist();
  static bool is_mermaid_cli_installed();
  static bool install_mermaid_cli();
  static bool mmd2svg(path& mmd, path& svg);

private:
  ConfigPtr config_;
};

// https://github.com/mermaid-js/mermaid-cli
inline bool Mermaid::run(const ParserPtr& parser_ptr) {
  if (parser_ptr == nullptr) {
    return false;
  }
  if (!is_npm_exist() || !is_jq_exist()) {
    spdlog::error("npm or jq not exists!");
    return false;
  }
  if (!is_mermaid_cli_installed()) {
    spdlog::warn("mermaid cli not installed!");
    if (!install_mermaid_cli()) {
      spdlog::error("failed to install mermaid cli!");
      return false;
    }
  } else {
    spdlog::debug("mermaid cli has installed!");
  }
  auto* md = dynamic_cast<Markdown*>(parser_ptr.get());
  auto temp_dir = std::filesystem::current_path() / "temp";
  auto dist_img_dir = std::filesystem::path(config_->dist_dir) / "images";
  if (!exists(temp_dir)) {
    create_directory(temp_dir);
  }
  if (!exists(dist_img_dir)) {
    create_directories(dist_img_dir);
  }
  for (auto& ele : md->elements()) {
    if (ele == nullptr) {
      continue;
    }
    auto* codeblock = dynamic_cast<CodeBlock*>(ele.get());
    if (codeblock == nullptr) {
      continue;
    }
    // std::cout << "has codeblock, lang_name=" << codeblock->lang_name << std::endl;
    if (codeblock->lang_name != "mermaid") {
      continue;
    }
    auto mmd_diagram = absl::StrJoin(codeblock->lines, "\n");
    auto hash_value = absl::Hash<std::string>{}(mmd_diagram);
    auto mmd_file_path = temp_dir / fmt::format("{0}.mmd", hash_value);
    std::fstream mmd_file_stream(mmd_file_path, std::ios::out | std::ios::trunc);
    if (!mmd_file_stream.is_open()) {
      spdlog::error("Failed to open file {}", mmd_file_path);
      continue;
    }
    mmd_file_stream << mmd_diagram;
    mmd_file_stream.flush();
    mmd_file_stream.close();
    permissions(mmd_file_path, std::filesystem::perms::owner_all | std::filesystem::perms::group_all,
                std::filesystem::perm_options::add);
    //
    auto svg_file_name = fmt::format("{}.svg", hash_value);
    auto svg_path = dist_img_dir/ svg_file_name;
    if (!mmd2svg(mmd_file_path, svg_path)) {
      spdlog::error("Failed to export mmd to svg!");
      continue;
    }
    // 替换
    auto* image_ptr = new Image();
    image_ptr->alt_text = mmd_file_path.stem();
    image_ptr->uri = "/images/" + svg_file_name;
    //
    for (const auto& [fst, snd] : codeblock->attrs) {
      if (fst == "width") {
        image_ptr->width = snd;
      } else if (fst == "alt") {
        image_ptr->alt_text = snd;
      }
    }
    ele.reset(image_ptr);
  }
  remove_all(temp_dir);
  return true;
}

inline bool Mermaid::mmd2svg(path& mmd, path& svg) {
  const auto cmd = fmt::format("npm exec -- @mermaid-js/mermaid-cli -i {0} -o {1} 2>&1", mmd.string(), svg.string());
  // std::cout << "mmd2svg cmd: " << cmd << std::endl;
  return system(cmd.c_str()) == 0;
}

inline bool Mermaid::is_npm_exist() {
  return system("which npm > /dev/null 2>&1") == 0;
}

inline bool Mermaid::is_jq_exist() {
  return system("which jq > /dev/null 2>&1") == 0;
}

inline bool Mermaid::is_mermaid_cli_installed() {
  const auto query_result = get_cmd_stdout("npm query '#@mermaid-js/mermaid-cli' | jq '.[] | select(.name  | type == \"string\") | .name'");
  return query_result == "\"@mermaid-js/mermaid-cli\"\n";
}

inline bool Mermaid::install_mermaid_cli() {
  return system("npm install @mermaid-js/mermaid-cli 2>&1") == 0;
}

}