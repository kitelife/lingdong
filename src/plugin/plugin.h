//
// Created by xiayf on 2025/4/11.
//
#pragma once

namespace ling::plugin {

class Plugin {
public:
  virtual ~Plugin() = default;
  virtual bool run() = 0;
};

} // namespace ling::plugin