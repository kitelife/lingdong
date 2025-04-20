#pragma once

/*
 * 为了前端响应速度，将超出一定大小的图片文件，进行压缩替换，同时保持图片尺寸大小不变。
 */

#include "plugin.h"

#include <filesystem>

#include <spdlog/spdlog.h>
#include <toml.hpp>

#include "../parser/markdown.h"
#include "../config.hpp"

namespace ling::plugin {

static bool is_local_uri(const absl::string_view uri) {
  if ((uri.size() > 7 && uri.substr(0, 7) == "http://") || (uri.size() > 8 && uri.substr(0, 8) == "https://")) {
    return false;
  }
  return true;
}

class SmallImage final : Plugin {
public:
  explicit SmallImage(ConfigPtr  config): config_(std::move(config)) {
    threshold_ = toml::find_or<size_t>(config_->raw_toml_, "small_image", "threshold", 1024*1024);
  };
  bool run(const ParserPtr& parser_ptr) override;

private:
  ConfigPtr config_;
  size_t threshold_;
};

inline bool SmallImage::run(const ParserPtr& parser_ptr) {
  if (parser_ptr == nullptr) {
    return false;
  }
  auto* md = dynamic_cast<Markdown*>(parser_ptr.get());
  if (md == nullptr) {
    return false;
  }
  for (const auto& ele : md->elements()) {
    auto* img = dynamic_cast<Image*>(ele.get());
    if (img == nullptr) {
      continue;
    }
    if (!is_local_uri(img->uri)) {
      continue;
    }
    std::filesystem::path p {img->uri};
    std::error_code ec;
    // size 的单位为 byte
    const std::uintmax_t size = std::filesystem::file_size(p, ec);
    if (ec) {
      spdlog::warn("failure to get file size, path: {}, err msg: {}", p, ec.message());
      continue;
    }
    // 大于 1MB，则需要压缩
    if (size < threshold_) {
      continue;
    }
    spdlog::debug("try to compress image file: {}, size:{}", p, size);
  }
  return true;
}

}