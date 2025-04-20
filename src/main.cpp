#include <filesystem>

#include <gflags/gflags.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

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
  spdlog::set_level(spdlog::level::debug);

  const auto origin_wd = std::filesystem::current_path();
  current_path(std::filesystem::absolute(FLAGS_dir));
  spdlog::info("change working dir from {} to {}", origin_wd, std::filesystem::current_path());
  //
  ConfigPtr config = load_conf();
  const auto maker = std::make_shared<ling::Maker>(config);
  if (!maker->make()) {
    spdlog::error("failed to make!");
    return -1;
  }
  spdlog::info("success to make!");
  if (FLAGS_enable_serve) {
    spdlog::info("try to serve this static site");
    const auto server = std::make_shared<ling::Server>(config);
    server->start();
  }
  return 0;
}