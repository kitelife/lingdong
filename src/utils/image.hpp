#pragma once

#include <filesystem>
#include <fstream>

#include <absl/strings/escaping.h>
#include <spdlog/spdlog.h>

namespace ling::utils {

inline void image2base64(const std::string& src, std::string& target) {
  absl::Base64Escape(src, &target);
}

inline void image2base64(const std::filesystem::path& image_path, std::string& target) {
  std::ifstream ifs(image_path, std::ios::binary);
  if (!ifs.is_open()) {
    spdlog::error("Could not open file {}", image_path.string());
    return;
  }
  uint32_t image_size = std::filesystem::file_size(image_path);
  std::string src(image_size, '\0');
  ifs.read(src.data(), image_size);
  //
  image2base64(src, target);
}

}