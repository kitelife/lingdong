#include <gtest/gtest.h>

#include "tokenizer.hpp"

TEST(TokenizerTest, test_cut_for_search) {
  auto tokens = ling::utils::tokenize("hello 欢迎来上海");
  EXPECT_EQ(tokens.size(), 5);
  for (const auto& token : tokens) {
    std::cout << "-> " << token << std::endl;
  }
  EXPECT_EQ(std::any_of(tokens.begin(), tokens.end(), [](const auto& token) { return token == "欢迎"; }), true);
  EXPECT_EQ(std::any_of(tokens.begin(), tokens.end(), [](const auto& token) { return token == "来上"; }), false);
}