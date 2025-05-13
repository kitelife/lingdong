//
// Created by xiayf on 2025/4/14.
//
#pragma once

#include <absl/hash/hash.h>
#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <zlib.h>

#include <filesystem>
#include <iostream>
#include <utility>

#include "../parser/markdown.h"
#include "plugin.h"

namespace ling::plugin {

// https://gist.github.com/gomons/9d446024fbb7ccb6536ab984e29e154a
static std::string zlib_deflate_compress(const std::string input) {
  //
  z_stream zs = {};
  if (deflateInit(&zs, Z_BEST_COMPRESSION) != Z_OK) {
    spdlog::error("deflateInit failed");
    return "";
  }
  zs.avail_in = input.size();
  zs.next_in = (Bytef*)(input.data());
  //
  std::string output;
  int ret;
  do {
    char out_buffer[input.size()];
    zs.avail_out = sizeof(out_buffer);
    zs.next_out = reinterpret_cast<Bytef*>(out_buffer);
    ret = deflate(&zs, Z_FINISH);
    if (output.size() < zs.total_out) {
      output.append(out_buffer, zs.total_out - output.size());
    }
  } while (ret == Z_OK);
  //
  deflateEnd(&zs);
  if (ret != Z_STREAM_END) {
    // an error occurred that was not EOF
    spdlog::error("Exception during zlib compression: {}, {}", ret, zs.msg);
  }
  return output;
}

static std::string zlib_deflate_decompress(const std::string& input) {
  z_stream zs = {};
  if (inflateInit(&zs) != Z_OK) {
    spdlog::error("inflateInit failed");
    return "";
  }
  zs.next_in = (Bytef*)input.data();
  zs.avail_in = input.size();

  std::string output;
  int ret;
  do {
    char out_buffer[1024];
    zs.next_out = reinterpret_cast<Bytef*>(out_buffer);
    zs.avail_out = sizeof(out_buffer);
    ret = inflate(&zs, 0);
    if (output.size() < zs.total_out) {
      output.append(out_buffer, zs.total_out - output.size());
    }
  } while (ret == Z_OK);
  inflateEnd(&zs);
  if (ret != Z_STREAM_END) {
    // an error occurred that was not EOF
    spdlog::error("Exception during zlib decompression: {}, {}", ret, zs.msg);
  }
  return output;
}

class PlantUML final : public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const ParserPtr& parser_ptr) override;

  std::pair<bool, std::string> diagram_desc2pic(std::vector<std::string>& lines);
  static std::string hex_encode(const std::string& diagram_desc);

private:
  ConfigPtr config_;
  std::string plantuml_server_;
  std::string diagram_header_;
};

// https://plantuml.com/text-encoding
inline std::string PlantUML::hex_encode(const std::string& diagram_desc) {
  std::stringstream ss;
  ss << "~h";
  for (const auto c : diagram_desc) {
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(c);
  }
  return ss.str();
}

inline std::pair<bool, std::string> PlantUML::diagram_desc2pic(std::vector<std::string>& lines) {
  std::stringstream ss;
  ss << "@startuml\n";
  ss << diagram_header_ << "\n";
  for (auto& line : lines) {
    ss << line << "\n";
  }
  ss << "@enduml";
  std::string plantuml_diagram = ss.str();
  auto encoded = hex_encode(plantuml_diagram);
  static std::string target_url = fmt::format("http://{0}/plantuml/svg/{1}", plantuml_server_, encoded);
  // std::cout << target_url << std::endl;
  cpr::Response r = cpr::Get(cpr::Url{target_url});
  if (r.status_code != 200) {
    spdlog::error("Failed to call plantuml, status_code: {}, response: {}", r.status_code, r.text);
    return std::make_pair(false, "");
  }
  // std::cout << r.text << std::endl;
  return std::make_pair(true, r.text);
}

inline bool PlantUML::init(ConfigPtr config_ptr) {
  config_ = config_ptr;
  plantuml_server_ = toml::find_or<std::string>(config_->raw_toml_, "plantuml", "server", "www.plantuml.com");
  diagram_header_ = toml::find_or_default<std::string>(config_->raw_toml_, "plantuml", "header");
  inited_ = true;
  return true;
}

inline bool PlantUML::run(const ParserPtr& parser_ptr) {
  if (parser_ptr == nullptr) {
    return false;
  }
  if (!inited_) {
    return false;
  }
  auto* md = dynamic_cast<Markdown*>(parser_ptr.get());
  auto dist_img_dir = std::filesystem::path(config_->dist_dir) / "images";
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
    if (codeblock->lang_name != "plantuml" && codeblock->lang_name != "plantuml-svg") {
      continue;
    }
    auto [fst, snd] = diagram_desc2pic(codeblock->lines);
    if (!fst) {
      return false;
    }
    auto hash_value = absl::Hash<std::string>{}(snd);
    auto svg_file_path = dist_img_dir / fmt::format("{0}.svg", hash_value);
    std::fstream svg_file_stream;
    svg_file_stream.open(svg_file_path, std::ios::out | std::ios::trunc);
    if (!svg_file_stream.is_open()) {
      spdlog::error("Failed to open plantuml svg file");
      continue;
    }
    svg_file_stream << snd;
    svg_file_stream.flush();
    svg_file_stream.close();
    // 替换
    auto* image_ptr = new Image();
    image_ptr->alt_text = svg_file_path.stem();
    image_ptr->uri = "/images/" + svg_file_path.filename().string();
    for (const auto& [fst, snd] : codeblock->attrs) {
      if (fst == "alt") {
        image_ptr->alt_text = snd;
      }
    }
    ele.reset(image_ptr);
  }
  return true;
}

static PluginRegister<PlantUML> plantuml_register_ {"PlantUML"};
}