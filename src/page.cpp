//
// Created by xiayf on 2025/4/13.
//

#include "page.h"

#include "parser/markdown.h"
#include "utils/strings.hpp"

namespace ling {

Post::Post(std::string& file_path, std::string& markup_lang) {
  file_path_ = file_path;
  markup_lang_ = markup_lang;
  if (markup_lang_ == "markdown") {
    parser_ = std::make_shared<Markdown>();
  }
}
/*
元信息部分的格式：

---
id: example-post
author: xiayf
date: 2024-04-15
tags: 示例, markdown, C++
---

*/
bool Post::parse_metadata() {
  return true;
}

bool Post::parse() {
  if (parser_ == nullptr) {
    return false;
  }
  return parser_->parse_file(file_path_);
}

}