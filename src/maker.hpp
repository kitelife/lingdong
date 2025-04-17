#pragma once

#include <absl/strings/ascii.h>

#include <filesystem>
#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

#include "config.hpp"
#include "parser/markdown.h"
#include "plugin/plantuml.hpp"
#include "plugin/mermaid.hpp"

namespace ling {

using std::filesystem::path;
using std::filesystem::directory_entry;
using std::filesystem::directory_iterator;
using std::filesystem::file_time_type;

// Post
class Post final {
public:
  explicit Post(const path& file_path);
  bool parse();
  ~Post() = default;
  ParserPtr parser() {
    return parser_;
  }

private:
  path file_path_;
  ParserPtr parser_;
  //
  file_time_type last_write_;
  std::string file_name_;
};

using PostPtr = std::shared_ptr<Post>;

inline Post::Post(const path& file_path) {
  file_path_ = file_path;
  last_write_ = last_write_time(file_path_);
  file_name_ = file_path_.filename().string();
  parser_ = std::make_shared<Markdown>();
}

inline bool Post::parse() {
  if (parser_ == nullptr) {
    return false;
  }
  return parser_->parse_file(file_path_);
}

// https://pelicanthemes.com/
class Theme final {
public:
  explicit Theme(path theme_path): base_path_(std::move(theme_path)) {
    static_path_ = base_path_ / path("static");
    template_path_ = base_path_ / path("templates");
  }

public:
  path static_path_;
  path template_path_;
  //
  path template_index {"/index.html"};
  path template_archives {"/archives.html"};

private:
  path base_path_;
};

// Maker
class Maker final {
public:
  explicit Maker(ConfigPtr conf): conf_(std::move(conf)) {}
  bool make();

private:
  void init() const;
  bool load();
  bool parse();
  bool generate();

private:
  ConfigPtr conf_;
  std::vector<PostPtr> posts_;
  std::vector<PostPtr> pages_;
};

using MakerPtr = std::shared_ptr<Maker>;

inline void Maker::init() const {
  static path dist_path {conf_->dist_dir};
  if (exists(dist_path)) {
    remove_all(dist_path);
  }
  create_directory(dist_path);
}

inline bool Maker::load() {
  static path posts_path {"posts"};
  static path pages_path {"pages"};
  //
  const auto is_markdown_file = [](const directory_entry& entry) -> bool {
    if (!entry.is_regular_file()) {
      return false;
    }
    auto file_ext = absl::AsciiStrToLower(entry.path().extension().string());
    if (file_ext != ".md" && file_ext != ".markdown") {
      return false;
    }
    return true;
  };

  for (const auto& entry : directory_iterator(posts_path)) {
    if (!is_markdown_file(entry)) {
      continue;
    }
    posts_.emplace_back(std::make_shared<Post>(entry.path()));
  }
  for (const auto& entry : directory_iterator(pages_path)) {
    if (!is_markdown_file(entry)) {
      continue;
    }
    pages_.emplace_back(std::make_shared<Post>(entry.path()));
  }
  return posts_.size() + pages_.size() > 0;
}

inline bool Maker::parse() {
  plugin::PlantUML plugin_plantuml {conf_};
  plugin::Mermaid plugin_mermaid {conf_};
  for (const auto& post : posts_) {
    if (!post->parse()) {
      return false;
    }
    plugin_plantuml.run(post->parser());
    plugin_mermaid.run(post->parser());
  }
  return std::all_of(pages_.begin(), pages_.end(), [](auto& page) {
    return page->parse();
  });
}

// https://docs.getpelican.com/en/latest/themes.html
// https://github.com/pantor/inja
inline bool Maker::generate() {
  //
  Theme theme {conf_->theme};
  inja::Environment env {absolute(theme.template_path_).string()};
  // page
  // posts
  // index
  nlohmann::json index_data;
  index_data["SITENAME"] = conf_->site_name;
  // env.render_file(theme.template_index, index_data);
  // archives
  nlohmann::json archives_data;
  //env.render_file(theme.template_archives, archives_data);
  // rss
  // copy static
  return true;
}

inline bool Maker::make() {
  init();
  if (!load()) {
    return false;
  }
  std::cout << "success to load post: " << posts_.size() + pages_.size() << std::endl;
  if (!parse()) {
    return false;
  }
  std::cout << "success to parse!" << std::endl;
  return generate();
}

}