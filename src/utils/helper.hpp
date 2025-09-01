#pragma once

#include <vector>
#include <filesystem>
#include <fstream>

namespace ling::utils {

using Embedding = std::vector<float>;
using Embeddings = std::vector<Embedding>;

static std::string get_cmd_stdout(std::string cmd) {
  constexpr int max_buffer = 256;
  char buffer[max_buffer];
  std::string data;
  //
  cmd.append(" 2>&1");
  FILE* stream = popen(cmd.c_str(), "r");
  if (stream) {
    while (!feof(stream))
      if (fgets(buffer, max_buffer, stream) != nullptr)
        data.append(buffer);
    pclose(stream);
  }
  return data;
}

static bool is_npm_exist() {
  return system("which npm > /dev/null 2>&1") == 0;
}

static bool is_jq_exist() {
  return system("which jq > /dev/null 2>&1") == 0;
}

static bool write_file(const std::filesystem::path& file_path, const std::string& content) {
  if (exists(file_path)) {
    return false;
  }
  std::ofstream ofs {file_path};
  if (!ofs.is_open()) {
    return false;
  }
  ofs << content;
  ofs.flush();
  ofs.close();
  return true;
}

}