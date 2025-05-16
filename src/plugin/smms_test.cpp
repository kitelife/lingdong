#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "smms.hpp"

TEST(SmmsPluginTest, fetch_api_token) {
  std::ifstream account_file("/Users/xiayf/github/lingdong/src/plugin/.smms_account.json");
  const auto j_account = nlohmann::json::parse(account_file);
  ling::plugin::SmmsOpenAPI smms_api {j_account["username"].get<std::string>(), j_account["password"].get<std::string>()};
  //
  std::ifstream token_file("/Users/xiayf/github/lingdong/src/plugin/.smms_token.json");
  const auto j_token = nlohmann::json::parse(token_file);
  EXPECT_EQ(smms_api.fetch_api_token(), j_token["token"].get<std::string>());
}