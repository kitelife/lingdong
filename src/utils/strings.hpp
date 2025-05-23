#pragma once

#include <absl/strings/string_view.h>
#include <filesystem>
#include <fstream>

namespace ling::utils {

inline absl::string_view view_strip_empty(absl::string_view str) {
  const size_t len = str.size();
  size_t valid_start_idx = 0;
  while (valid_start_idx < len && (str[valid_start_idx] == ' ' || str[valid_start_idx] == '\t')) {
    valid_start_idx++;
  }
  if (valid_start_idx == len) {
    return "";
  }
  size_t valid_end_idx = len - 1;
  while (valid_end_idx > valid_start_idx && (str[valid_end_idx] == ' ' || str[valid_end_idx] == '\t')) {
    valid_end_idx--;
  }

  return str.substr(valid_start_idx, valid_end_idx - valid_start_idx + 1);
}

inline std::string read_file_all(const std::filesystem::path& p) {
  auto size = std::filesystem::file_size(p);
  std::string content(size, '\0');
  std::ifstream fi(p);
  fi.read(&content[0], size);
  return content;
}

}