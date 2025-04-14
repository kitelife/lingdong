#include <iostream>

#include <absl/strings/str_join.h>
#include <fmt/core.h>
#include <leveldb/db.h>
#include <yaml-cpp/yaml.h>
#include <toml.hpp>
#include <inja/inja.hpp>

#include "parser/markdown.h"
#include "plugin/plantuml.h"

class Config {
public:
  std::string site_name;
  std::string theme;
  std::string mark_lang;
  std::vector<std::string> plugins;
};

using ConfigPtr = std::shared_ptr<Config>;

ConfigPtr parse_conf(const std::string& conf_file_path = "config.yaml") {
  static std::string default_site_name = "不知名站点";
  static std::string default_theme = "default";
  static std::string default_mark_lang = "markdown";
  YAML::Node conf = YAML::LoadFile(conf_file_path);
  //
  auto conf_ptr = std::make_shared<Config>();
  conf_ptr->site_name = conf["site_name"].IsDefined() ? conf["site_name"].as<std::string>() : default_site_name;
  conf_ptr->theme = conf["theme"].IsDefined() ? conf["theme"].as<std::string>() : default_theme;
  conf_ptr->mark_lang = conf["markup_lang"].IsDefined() ? conf["markup_lang"].as<std::string>() : default_mark_lang;
  if (conf["plugins"]) {
    auto& plugins = conf_ptr->plugins;
    YAML::Node conf_plugins = conf["plugins"];
    for (auto it = conf_plugins.begin(); it != conf_plugins.end(); ++it) {
      plugins.emplace_back(it->as<std::string>());
    }
  }
  return conf_ptr;
}

ConfigPtr parse_conf_v2(const std::string& conf_file_path = "config.toml") {
  const auto conf = toml::parse(conf_file_path);
  auto conf_ptr = std::make_shared<Config>();
  //
  conf_ptr->site_name = toml::find_or_default<std::string>(conf, "site_name");
  conf_ptr->theme = toml::find_or_default<std::string>(conf, "theme");
  conf_ptr->mark_lang = toml::find_or_default<std::string>(conf, "markup_lang");
  conf_ptr->plugins = toml::find_or_default<std::vector<std::string>>(conf, "plugins");
  //
  return conf_ptr;
}

int main() {
  auto lang = "C++";
  std::cout << "Hello and welcome to " << lang << "!\n";

  std::vector<std::string> v = {"foo","bar","baz"};
  std::string s = absl::StrJoin(v, "-");

  fmt::println("fmt hello");

  //
  inja::json data;
  data["name"] = "world";

  std::cout << inja::render("Hello {{ name }}, inja!", data) << std::endl; // Returns std::string "Hello world!"
  inja::render_to(std::cout, "Hello {{ name }}, inja V5!\n", data); // Writes "Hello world!" to stream
  //
  ling::MarkdownPtr md = std::make_shared<ling::Markdown>();
  // std::cout << "parse md str: " << md.parse_str("# 你好   \n## 世界\t") << std::endl;
  // std::cout << "parse to html: " << md.to_html() << std::endl;
  // md.clear();
  std::cout << "parse md file: " << md->parse_file("../demo/demo.md") << std::endl;
  ling::plugin::PlantUML plantuml;
  plantuml.run(md, "markdown");
  std::cout << "parse to html: " << md->to_html() << std::endl;
  //
  YAML::Node config = YAML::LoadFile("../demo/blog/config.yaml");
  if (config["name"]) {
    std::cout << "name: " << config["name"].as<std::string>() << "\n";
  }
  //
  leveldb::DB* db;
  leveldb::Options options;
  options.create_if_missing = true;
  leveldb::Status status = leveldb::DB::Open(options, "./test.db", &db);
  if (!status.ok()) {
    std::cerr << "Failed to open leveldb: " << status.ToString() << "\n";
    return -1;
  }
  leveldb::WriteOptions write_options;
  status = db->Put(write_options, "hello", "world");
  if (!status.ok()) {
    std::cerr << "Failed to write to leveldb: " << status.ToString() << "\n";
    return -1;
  }
  leveldb::ReadOptions read_options;
  std::string value;
  status = db->Get(read_options, "hello", &value);
  if (!status.ok()) {
    std::cerr << "Failed to read from leveldb: " << status.ToString() << "\n";
    return -1;
  }
  std::cout << "key: hello, value: " << value << std::endl;
  // ling::utils::gen_random_byte_stream(std::cout, 10);
  ConfigPtr conf_ptr = parse_conf("../demo/blog/config.yaml");
  std::cout << conf_ptr->site_name << std::endl;
  std::cout << conf_ptr->theme << std::endl;
  std::cout << conf_ptr->mark_lang << std::endl;
  //
  ConfigPtr config_v2_ptr = parse_conf_v2("../demo/blog/config.toml");
  std::cout << config_v2_ptr->site_name << std::endl;
  std::cout << config_v2_ptr->theme << std::endl;
  std::cout << config_v2_ptr->mark_lang << std::endl;
  return 0;
}