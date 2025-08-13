#include <filesystem>

#include <gflags/gflags.h>
#include <fmt/std.h>
#include <spdlog/spdlog.h>

#include "context.hpp"
#include "maker.hpp"
#include "server.hpp"

DEFINE_string(dir, "../demo/blog", "working directory");
DEFINE_bool(skip_make, true, "skip make or not");
DEFINE_bool(enable_serve, true, "enable to serve the static site");

DEFINE_string(test_post, "", "for test, to parse single post");

using namespace ling;

ConfigPtr load_conf(const std::string& conf_file_path = "config.toml") {
  auto conf_ptr = Context::singleton()->with_config();
  conf_ptr->raw_toml_ = toml::parse(conf_file_path);
  conf_ptr->parse();
  return conf_ptr;
}

bool test_post(const std::string& post_file) {
  auto post_ptr = std::make_shared<ling::Post>(std::filesystem::path(post_file));
  if (!post_ptr->parse()) {
    return false;
  }
  spdlog::info("title: {}", post_ptr->title());
  spdlog::info("id: {}", post_ptr->id());
  spdlog::info("updated_at: {}", post_ptr->updated_at());
  spdlog::info("html:\n{}", post_ptr->html());
  return true;
}

int main(int argc, char** argv) {
  google::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  spdlog::flush_on(spdlog::level::warn);
  spdlog::flush_every(std::chrono::seconds(30));
  // 单篇测试使用
  if (!FLAGS_test_post.empty()) {
    if (!test_post(FLAGS_test_post)) {
      spdlog::error("Failed to parse {}", FLAGS_test_post);
      return -1;
    }
    return 0;
  }
  //
  const auto origin_wd = current_path();
  current_path(absolute(FLAGS_dir));
  spdlog::info("change working dir from {} to {}", origin_wd, current_path());
  // 加载解析配置
  load_conf();
  // make
  if (!FLAGS_skip_make) {
    const auto maker = std::make_shared<Maker>();
    if (!maker->make()) {
      spdlog::error("failed to make!");
      return -1;
    }
    spdlog::info("success to make!");
  } else {
    spdlog::info("Skip make!");
  }
  // serve
  if (FLAGS_enable_serve) {
    spdlog::info("try to serve this static site");
    server::start();
  }
  return 0;
}