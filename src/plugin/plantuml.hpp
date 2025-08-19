//
// Created by xiayf on 2025/4/14.
//
#pragma once

#include <cpr/cpr.h>
#include <spdlog/spdlog.h>
#include <zlib.h>

#include <filesystem>
#include <utility>

#include "parser/markdown.h"
#include "plugin.h"

namespace ling::plugin {

// https://gist.github.com/gomons/9d446024fbb7ccb6536ab984e29e154a
static std::string zlib_deflate_compress(const std::string& input) {
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

static std::string PLANTUML_REMOTE_SERVER = "www.plantuml.com";

class PlantUML final : public Plugin {
public:
  bool init(ContextPtr& context_ptr) override;
  bool run(const MarkdownPtr& md_ptr) override;
  bool destroy() override;

  std::pair<bool, std::string> diagram_desc2pic(std::vector<std::string>& lines);
  static std::string hex_encode(const std::string& diagram_desc);

private:
  bool start_picoweb_server();
  bool stop_picoweb_server() const;

private:
  ConfigPtr config_;
  //
  std::string jar_path_;
  uint32_t picweb_port_ {8000};
  bool picoweb_server_started_ {false};
  //
  std::string plantuml_server_;
  std::string diagram_header_;
  std::filesystem::path target_img_dir_;
};

// https://plantuml.com/text-encoding
inline std::string PlantUML::hex_encode(const std::string& diagram_desc) {
  std::stringstream ss;
  ss << "~h";
  for (const unsigned char c : diagram_desc) {
    // https://stackoverflow.com/questions/37272384/c-convert-utf8-string-to-hexadecimal-and-vice-versa
    ss << std::hex << std::setprecision(2) << std::setw(2) << std::setfill('0') << static_cast<int>(c);
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
  const std::string target_url = fmt::format("http://{}/plantuml/svg/{}", plantuml_server_, encoded);
  cpr::Response r = cpr::Get(cpr::Url{target_url},
    cpr::Timeout{std::chrono::seconds(5)},
    cpr::ConnectTimeout{std::chrono::seconds(2)});
  if (r.status_code != 200) {
    spdlog::error("Failed to call plantuml, status_code: {}, response: {}, err msg: {}",
      r.status_code, r.text, r.error.message);
    return std::make_pair(false, "");
  }
  return std::make_pair(true, r.text);
}

inline bool PlantUML::init(ContextPtr& context_ptr) {
  if (!Plugin::init(context_ptr)) {
    return false;
  }
  config_ = context_ptr->with_config();
  jar_path_ = toml::find_or<std::string>(config_->raw_toml_, "plantuml", "jar_path", "");
  picweb_port_ = toml::find_or<uint32_t>(config_->raw_toml_, "plantuml", "picoweb_port", 8000);
  plantuml_server_ = toml::find_or<std::string>(config_->raw_toml_, "plantuml", "server", PLANTUML_REMOTE_SERVER);
  diagram_header_ = toml::find_or_default<std::string>(config_->raw_toml_, "plantuml", "header");
  if (!jar_path_.empty() && (plantuml_server_.empty() || plantuml_server_ == PLANTUML_REMOTE_SERVER)) {
    if (!start_picoweb_server()) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::seconds(2)); // 等待2s，等服务就绪
    plantuml_server_ = fmt::format("127.0.0.1:{}", picweb_port_);
    picoweb_server_started_ = true;
  }
  //
  target_img_dir_ = std::filesystem::current_path() / "plantuml-images";
  if (std::filesystem::exists(target_img_dir_)) {
    std::filesystem::remove_all(target_img_dir_);
  }
  create_directories(target_img_dir_);
  return true;
}

inline bool PlantUML::start_picoweb_server() {
  const std::string cmd = fmt::format("nohup java -jar {} -picoweb:{} > /dev/null 2>&1 &", jar_path_, picweb_port_);
  return std::system(cmd.c_str()) == 0;
}

inline bool PlantUML::stop_picoweb_server() const {
  std::string cmd = fmt::format("lsof -i:{}", picweb_port_);
  cmd += " | grep \"java\" | awk -F' ' '{print $2}' | xargs kill";
  return std::system(cmd.c_str()) == 0;
}

inline bool PlantUML::run(const MarkdownPtr& md_ptr) {
  if (md_ptr == nullptr) {
    return false;
  }
  if (!inited_) {
    return false;
  }
  //
  for (auto& ele : md_ptr->elements()) {
    if (ele == nullptr) {
      continue;
    }
    auto* codeblock = dynamic_cast<CodeBlock*>(ele.get());
    if (codeblock == nullptr) {
      continue;
    }
    if (codeblock->lang_name != "plantuml" && codeblock->lang_name != "plantuml-svg") {
      continue;
    }
    // WARNING: 此处不要使用 absl::Hash，因为它在不同线程中运行时使用的种子不一样，导致同样的输入，生成的哈希值会不一样。
    auto hash_value = std::hash<std::string>{}(absl::StrJoin(codeblock->lines, "\n"));
    auto svg_file_path = target_img_dir_ / fmt::format("{0}.svg", hash_value);
    if (!exists(svg_file_path)) {
      auto [fst, snd] = diagram_desc2pic(codeblock->lines);
      if (!fst) {
        return false;
      }
      std::fstream svg_file_stream;
      svg_file_stream.open(svg_file_path, std::ios::out | std::ios::trunc);
      if (!svg_file_stream.is_open()) {
        spdlog::error("Failed to open plantuml svg file");
        continue;
      }
      svg_file_stream << snd;
      svg_file_stream.flush();
      svg_file_stream.close();
    }
    // 替换
    auto* image_ptr = new Image();
    image_ptr->width = "";
    image_ptr->alt_text = svg_file_path.stem();
    image_ptr->uri = "../plantuml-images/" + svg_file_path.filename().string();
    for (const auto& [fst, snd] : codeblock->attrs) {
      if (fst == "alt") {
        image_ptr->alt_text = snd;
      }
    }
    ele.reset(image_ptr);
  }
  return true;
}

inline bool PlantUML::destroy() {
  if (!inited_) {
    return true;
  }
  if (picoweb_server_started_) {
    return stop_picoweb_server();
  }
  return true;
}

static PluginRegister<PlantUML> plantuml_register_ {"PlantUML"};
}