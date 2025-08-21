#pragma once

#include <vector>
#include <string>

#include "cppjieba/Jieba.hpp"

namespace ling::utils {

static cppjieba::Jieba jieba;

static std::vector<std::string> tokenize(const std::string& s) {
  std::vector<std::string> tokens;
  jieba.CutForSearch(s, tokens, true);
  return tokens;
}

}