//
// Created by xiayf on 2025/3/30.
//
#pragma once

#include <string>
#include <vector>

namespace ling {

class Parser {
public:
  virtual ~Parser() = default;
  virtual bool parse_str(const std::string& content) = 0;
  virtual bool parse_file(const std::string& file_path) = 0;
  virtual std::string to_html() = 0;
};

using ParserPtr = std::shared_ptr<Parser>;

class PostMetadata final {
public:
  std::string id;
  std::string title;
  std::string publish_date;
  std::vector<std::string> tags;
};
}
