//
// Created by xiayf on 2025/4/15.
//

#include "strings.hpp"

#include <gtest/gtest.h>

TEST(StringsTest, view_strip_empty) {
  std::string input = "  \t this is a test string ";
  std::string expected = "this is a test string";
  EXPECT_EQ(ling::utils::view_strip_empty(input), expected);
}