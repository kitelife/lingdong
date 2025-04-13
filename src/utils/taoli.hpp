//
// Created by xiayf on 2025/4/8.
//
// 桃代李僵
#pragma once

#include <fstream>
#include <random>

namespace ling::utils {

inline bool gen_random_byte_stream(std::ostream& os, size_t length) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<int32_t> dist(0, std::numeric_limits<int32_t>::max());
  bool ret_status = true;
  length = length / (sizeof(int32_t)/sizeof(int8_t));
  while (length--) {
    if (!os.good()) {
      ret_status = false;
      break;
    }
    const int32_t v = dist(gen);
    os << static_cast<int8_t>(v >> 24) << static_cast<int8_t>((v >> 16) & 0x00ff);
    os << static_cast<int8_t>((v >> 8) & 0x0000ff) << static_cast<int8_t>(v);
  }
  return ret_status;
}
}

/*
int main() {
  std::ofstream os;
  os.open("random_file.bin", std::ios::binary | std::ios::out | std::ios::trunc);
  if (!os.good()) {
    return -1;
  }
  const auto status = ling::utils::gen_random_byte_stream(os, 1024*1024*100);
  os.flush();
  os.close();
  return status ? 0 : 1;
}
*/