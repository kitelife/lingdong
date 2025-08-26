#include <iostream>
#include <filesystem>

#include "gflags/gflags.h"

#include "utils/image.hpp"

DEFINE_string(image_path, "../../src/tests/data/ollama.png", "image path");

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  std::string target_base64;
  ling::utils::image2base64(std::filesystem::path(FLAGS_image_path), target_base64);
  std::cout << target_base64 << std::endl;
  return 0;
}