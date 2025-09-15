#include <iostream>

#include <gtest/gtest.h>
#include "fmt/format.h"

TEST(FormatTest, test_escape_brace) {
  // '{{' 表示对 '{' 转义/表示符号本身，'}}' 同理
  const auto tpl = R"({{"model_name": "{}", "dim": "{}", "cnt": {}}})";
  const auto s = fmt::format(tpl, "mxbai-embed-large:latest", 1024, 10626);
  EXPECT_EQ(s, R"({"model_name": "mxbai-embed-large:latest", "dim": "1024", "cnt": 10626})");
}