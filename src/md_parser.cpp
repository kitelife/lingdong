//
// Created by xiayf on 2025/3/28.
//

#include "md_parser.h"

#include <fstream>
#include <sstream>

namespace ling {

bool MdParser::parse(const std::string& md_content) {
  std::istringstream s(md_content);
  return parse(s);
};

bool MdParser::parse_md_file(const std::string& md_file_path) {
  std::ifstream s(md_file_path);
  if (!s.is_open()) {
    return false;
  }
  return parse(s);
};

bool MdParser::parse(std::istream& stream) {

  return true;
};


}