//
// Created by xiayf on 2025/4/14.
//

#include "plantuml.h"

#include <cpr/cpr.h>
#include <fmt/core.h>

#include "../parser/markdown.h"

namespace ling::plugin {

// https://plantuml.com/text-encoding
// https://github.com/markushedvall/plantuml-encoder
std::string PlantUML::encode(const std::string& plantuml_code) {
  return "";
}

std::pair<bool, std::string> PlantUML::code2pic(std::vector<std::string>& lines) {
  std::stringstream ss;
  ss << "@startuml\n";
  for (auto& line : lines) {
    ss << line << "\n";
  }
  ss << "@enduml";
  std::string plantuml_code = ss.str();
  auto encoded = encode(plantuml_code);
  static std::string target_url = "http://127.0.0.1:8000/svg/";
  std::cout << target_url << std::endl;
  cpr::Response r = cpr::Post(cpr::Url{target_url},
                              cpr::Body{plantuml_code},
                              cpr::Header{{"Content-Type", "text/plain"}});
  if (r.status_code != 200) {
    std::cerr << "Failed to call plantuml, status_code: " << r.status_code << ", response: " << r.text << std::endl;
    return std::make_pair(false, "");
  }
  std::cout << r.text << std::endl;
  return std::make_pair(true, r.text);
}

bool PlantUML::run(ParserPtr parser_ptr, const std::string& markup_lang) {
  if (parser_ptr == nullptr) {
    return false;
  }
  if (markup_lang.empty() || markup_lang != "markdown") { // 目前仅支持 markdown
    return false;
  }
  Markdown* md = dynamic_cast<Markdown*>(parser_ptr.get());
  for (auto& ele : md->elements()) {
    if (ele.get() == nullptr) {
      continue;
    }
    auto* codeblock = dynamic_cast<CodeBlock*>(ele.get());
    if (codeblock == nullptr) {
      continue;
    }
    std::cout << "has codeblock, lang_name=" << codeblock->lang_name << std::endl;
    if (codeblock->lang_name != "plantuml" && codeblock->lang_name != "plantuml-svg") {
      continue;
    }
    auto result = code2pic(codeblock->lines);
    if (!result.first) {
      return false;
    }
    std::fstream plantuml_svg_file;
    plantuml_svg_file.open("test.svg", std::ios::binary);
    plantuml_svg_file << result.second;
    plantuml_svg_file.flush();
    plantuml_svg_file.close();
    return true;
  }
  return true;
}
}