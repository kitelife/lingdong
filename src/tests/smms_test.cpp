#include <gtest/gtest.h>

#include <fstream>
#include <nlohmann/json.hpp>

#include "plugin/smms.hpp"

using SmmsOpenAPI = ling::plugin::SmmsOpenAPI;

static SmmsOpenAPI make_smms_api() {
  std::ifstream account_file("../src/plugin/.smms_account.json");
  const auto j_account = nlohmann::json::parse(account_file);
  return SmmsOpenAPI{j_account["username"].get<std::string>(), j_account["password"].get<std::string>()};
}

TEST(SmmsPluginTest, fetch_api_token) {
  auto smms_api = make_smms_api();
  //
  std::ifstream token_file("../src/plugin/.smms_token.json");
  const auto j_token = nlohmann::json::parse(token_file);
  EXPECT_EQ(smms_api.fetch_api_token(), j_token["token"].get<std::string>());
};

TEST(SmmsPluginTest, fetch_upload_history) {
  auto smms_api = make_smms_api();
  const auto histories = smms_api.fetch_upload_history();
  for (const auto& h : histories) {
    std::cout << h << std::endl;
  }
  EXPECT_GT(histories.size(), 0);
}

TEST(SmmsPluginTest, upload) {
  auto smms_api = make_smms_api();
  //
  const std::string image_path = "../demo/blog/assets/kmeans_clustering.png";
  const auto [success, history] = smms_api.upload(image_path);
  EXPECT_TRUE(success);
  std::cout << history << std::endl;
  if (success && !history.hash.empty()) {
    EXPECT_TRUE(smms_api.del(history.hash));
  }
}