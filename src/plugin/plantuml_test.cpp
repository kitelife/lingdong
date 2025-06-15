//
// Created by xiayf on 2025/4/15.
//
#include "plantuml.hpp"

#include <absl/hash/hash.h>
#include <absl/strings/escaping.h>
#include <gtest/gtest.h>

#include <string>

using namespace std::string_literals; // enables s-suffix for std::string literals
using namespace ::ling::plugin;

TEST(PlantUMLPluginTest, encode) {
  const std::string diagram_desc = R"("@startuml
Alice -> Bob: Authentication Response
Bob --> Alice: Authentication Response
@enduml")";
  const std::string diagram_desc_encoded = "~h22407374617274756d6c0a416c696365202d3e20426f623a2041757468656e7469636174696f6e20526573706f6e73650a426f62202d2d3e20416c6963653a2041757468656e7469636174696f6e20526573706f6e73650a40656e64756d6c22";
  // Expect equality.
  EXPECT_EQ(PlantUML::hex_encode(diagram_desc), diagram_desc_encoded);
}

TEST(PlantUMLPluginTest, zh_encode) {
  const std::string diagram_desc = R"(@startuml
Alice->Bob : 中文
@enduml)";
  const std::string encoded = "~h407374617274756d6c0a416c6963652d3e426f62203a20e4b8ade696870a40656e64756d6c";
  EXPECT_EQ(PlantUML::hex_encode(diagram_desc), encoded);
}

TEST(PlantUMLPluginTest, zlib_deflate) {
  static std::string input = "hello";
  std::string compress_output = zlib_deflate_compress(input);
  std::string decompress_output = zlib_deflate_decompress(compress_output);
  std::cout << "decompress_output: " << decompress_output << std::endl;
  EXPECT_EQ(input, decompress_output);
  EXPECT_EQ(absl::WebSafeBase64Escape(compress_output), "eNrLSM3JyQcABiwCFQ");
}

TEST(PlantUMLPluginTest, absl_hash) {
  std::string input1 = "~h407374617274756d6c0a416c6963652d3e426f62203a20e4b8ade696870a40656e64756d6c";
  std::string input2 = "~h407374617274756d6c0a416c6963652d3e426f62203a20e4b8ade696870a40656e64756d6c";
  auto hash1 = absl::Hash<std::string>{}(input1);
  auto hash2 = absl::Hash<std::string>{}(input2);
  std::cout << hash1 << std::endl;
  ASSERT_EQ(hash2, hash1);
  //
  static void* p = &p;
  std::cout << p << std::endl;
  std::cout << &p << std::endl;
}

TEST(PlantUMLPluginTest, std_hash) {
  std::string input1 = "~h407374617274756d6c0a416c6963652d3e426f62203a20e4b8ade696870a40656e64756d6c";
  std::string input2 = "~h407374617274756d6c0a416c6963652d3e426f62203a20e4b8ade696870a40656e64756d6c";
  auto hash1 = std::hash<std::string>{}(input1);
  auto hash2 = std::hash<std::string>{}(input2);
  std::cout << hash1 << std::endl;
  ASSERT_EQ(hash2, hash1);
}