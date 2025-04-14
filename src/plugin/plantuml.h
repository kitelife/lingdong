//
// Created by xiayf on 2025/4/11.
//
#pragma once

#include "plugin.h"

#include <iostream>

namespace ling::plugin {

class PlantUML final : public Plugin {
public:
  PlantUML() = default;
  bool run(ParserPtr parser_ptr, const std::string& markup_lang) override;

private:
  std::pair<bool, std::string> code2pic(std::vector<std::string>& lines);
  std::string encode(const std::string& plantuml_code);
};

}
