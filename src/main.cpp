#include <iostream>
#include <filesystem>

#include <gflags/gflags.h>

#include "config.hpp"
#include "maker.hpp"
#include "server.hpp"

DEFINE_string(dir, "../demo/blog", "working directory");
DEFINE_bool(enable_serve, false, "enable to serve the static site");

ConfigPtr load_conf(const std::string& conf_file_path = "config.toml") {
  auto conf_ptr = std::make_shared<Config>();
  conf_ptr->raw_toml_ = toml::parse(conf_file_path);
  conf_ptr->parse();
  return conf_ptr;
}

int main() {
  const auto origin_wd = std::filesystem::current_path();
  current_path(std::filesystem::absolute(FLAGS_dir));
  std::cout << "Change working dir from " << origin_wd << " to " << std::filesystem::current_path() << std::endl;
  //
  ConfigPtr config = load_conf();
  const auto maker = std::make_shared<ling::Maker>(config);
  if (!maker->make()) {
    std::cerr << "Failed to make!" << std::endl;
    return -1;
  }
  std::cout << "success to make!" << std::endl;
  if (FLAGS_enable_serve) {
    std::cout << "try to serve this static site" << std::endl;
    const auto server = std::make_shared<ling::Server>(config);
    server->start();
  }
  return 0;
}