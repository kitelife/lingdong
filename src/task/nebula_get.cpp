#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "spdlog/spdlog.h"
#include "gflags/gflags.h"
#include "nlohmann/json.hpp"
#include "cpr/cpr.h"
#include "cryptopp/sha.h"     // SHA256 类
#include "cryptopp/hex.h"     // 十六进制编码器
#include "cryptopp/filters.h" // StringSink

#include "utils/helper.hpp"

DEFINE_string(app_id, "panama", "app id");
DEFINE_string(token, "", "token");
DEFINE_string(url, "", "url");
DEFINE_string(cookie_file, ".cookie", "cookie file path");

using namespace CryptoPP;

std::string sha256(const std::string& message) {
  // 创建存放结果的数组
  // SHA256::DIGESTSIZE 是一个常量，值为 32 (字节)
  byte digest[SHA256::DIGESTSIZE];
  // 创建 SHA256 哈希对象并计算
  SHA256 hash;
  hash.CalculateDigest(digest, (const byte*) message.c_str(), message.length());
  // 将二进制摘要转换为十六进制字符串以便显示
  HexEncoder encoder {nullptr, false};
  std::string output;
  // 建立一个过滤器管道：digest -> HexEncoder -> StringSink(output)
  encoder.Attach(new StringSink(output));
  encoder.Put(digest, sizeof(digest));
  encoder.MessageEnd(); // 处理所有数据
  return output;
}

std::string cookie() {
  static std::string cookie_file_name = FLAGS_cookie_file;
  if (!std::filesystem::exists(cookie_file_name)) {
    return "";
  }
  std::ifstream cookie_if(cookie_file_name);
  if (!cookie_if.is_open()) {
    spdlog::error("failure to open cookie file");
    return "";
  }
  if (std::string cookie; std::getline(cookie_if, cookie)) {
    return cookie;
  }
  return "";
}

nlohmann::json call() {
  std::string timestamp = std::to_string(std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()).count());
  std::string message = FLAGS_app_id + ":" + FLAGS_token + ":" + timestamp;
  std::string sign = sha256(message);
  auto headers = cpr::Header{
      {"Content-Type", ling::utils::CONTENT_TYPE_JSON},
      {"User-Agent", ling::utils::USER_AGENT},
      {"App-Id", FLAGS_app_id},
      {"Timestamp", timestamp},
      {"Signature", sign},
  };
  auto ck = cookie();
  if (!ck.empty()) {
    headers["Cookie"] = ck;
  }
  //
  auto r = cpr::Get(cpr::Url{FLAGS_url}, headers);
  if (r.status_code != 200 || (r.header.count("Content-Type") && r.header["Content-Type"] != ling::utils::CONTENT_TYPE_JSON)) {
    spdlog::error("failure to call {}, code: {}, resp: {}", r.url.c_str(), r.status_code, r.text.c_str());
    return {};
  }
  return nlohmann::json::parse(r.text);
}

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  if (FLAGS_url.empty()) {
    spdlog::error("please specify an URL");
    return -1;
  }
  auto rj = call();
  if (!rj.empty()) {
    std::cout << rj.dump() << std::endl;
  }
  return 0;
}