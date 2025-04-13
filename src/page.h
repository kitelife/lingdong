//
// Created by xiayf on 2025/4/13.
//

#pragma once

#include <string>
#include <vector>

#include "parser/parser.h"

namespace ling {

class PostMetadata {
public:
  std::string author;
  std::string publish_date;
  std::string post_id;
  std::vector<std::string> tags;
};

class Post {
public:
  Post(std::string& file_path, std::string& markup_lang);
  bool parse();
  virtual ~Post() = default;

private:
  bool parse_metadata();

private:
  std::string file_path_;
  std::string markup_lang_;
  ParserPtr parser_;
  PostMetadata metadata_;
};

}