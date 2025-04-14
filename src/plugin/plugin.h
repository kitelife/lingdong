//
// Created by xiayf on 2025/4/11.
//
#pragma once

#include "../parser/parser.h"

namespace ling::plugin {

class Plugin {
public:
  virtual ~Plugin() = default;
  virtual bool run(ParserPtr parser_ptr, const std::string& markup_lang) = 0;
};

} // namespace ling::plugin