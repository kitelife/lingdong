//
// Created by xiayf on 2025/4/11.
//
#pragma once

#include "../parser/parser.h"

namespace ling::plugin {

class Plugin {
public:
  virtual ~Plugin() = default;
  virtual bool init() {
    return true;
  }
  virtual bool run(const ParserPtr& parser_ptr) = 0;
  virtual bool destroy() {
    return true;
  }
};

} // namespace ling::plugin