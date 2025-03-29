#include <iostream>

#include "absl/strings/str_join.h"
#include "fmt/core.h"
#include "inja/inja.hpp"
#include "md_parser.h"
#include "page_render.h"
#include "publisher.h"
#include "yaml-cpp/yaml.h"
#include "leveldb/db.h"

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
  std::cout << "parse: " << ling::MdParser::parse("#Demo") << std::endl;
  //
  YAML::Node config = YAML::LoadFile("../config.yaml");
  if (config["lastLogin"]) {
    std::cout << "Last logged in: " << config["lastLogin"].as<std::string>() << "\n";
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
  return 0;
}