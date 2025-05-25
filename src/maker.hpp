#pragma once

#include <absl/strings/ascii.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

#include "absl/time/clock.h"
#include "config.hpp"
#include "parser/markdown.h"
#include "plugin/plugins.hpp"
#include "utils/time.hpp"

namespace ling {

using namespace std::filesystem;

using inja::Environment;
using inja::Template;

// Post
class Post final {
public:
  explicit Post(const path& file_path);
  bool parse() const;
  ~Post() = default;
  MarkdownPtr parser() {
    return parser_;
  }
  [[nodiscard]] std::string title();
  [[nodiscard]] std::string updated_at();
  [[nodiscard]] std::string id();
  [[nodiscard]] std::string html();
  [[nodiscard]] std::string html_file_name();
  [[nodiscard]] std::string file_path() {
    return file_path_;
  }
  std::string file_name() {
    return file_name_;
  }

private:
  path file_path_;
  MarkdownPtr parser_;
  //
  file_time_type last_write_time_;
  std::string post_updated_;
  std::string file_name_;
  //
  std::string id_;
  std::string title_;
  std::string updated_at_;
  std::string html_;
};

using PostPtr = std::shared_ptr<Post>;

inline Post::Post(const path& file_path) {
  file_path_ = file_path;
  last_write_time_ = last_write_time(file_path_);
  post_updated_ = utils::convert(last_write_time_);
  file_name_ = file_path_.stem().string();
  parser_ = std::make_shared<Markdown>();
}

inline bool Post::parse() const {
  if (parser_ == nullptr) {
    return false;
  }
  return parser_->parse_file(file_path_);
}

inline std::string Post::title() {
  if (!title_.empty()) {
    return title_;
  }
  if (parser_ != nullptr) {
    auto title = parser_->metadata().title;
    if (title.empty()) {
      for (const auto& ele : parser_->elements()) {
        auto* h1 = dynamic_cast<Heading*>(ele.get());
        if (h1 != nullptr) {
          if (h1->level_ == 1) {
            title_ = h1->title_;
            break;
          }
        }
      }
    } else {
      title_ = title;
    }
  }
  return title_;
}

inline std::string Post::updated_at() {
  if (!updated_at_.empty()) {
    return updated_at_;
  }
  if (parser_ != nullptr) {
    auto publish_date = parser_->metadata().publish_date;
    if (!publish_date.empty()) {
      updated_at_ = publish_date;
    }
  }
  if (updated_at_.empty()) {
    updated_at_ = post_updated_;
  }
  return updated_at_;
}

inline std::string Post::id() {
  if (!id_.empty()) {
    return id_;
  }
  if (parser_ != nullptr) {
    id_ = parser_->metadata().id;
  }
  if (id_.empty()) {
    id_ = file_name_;
  }
  return id_;
}

inline std::string Post::html() {
  if (!html_.empty()) {
    return html_;
  }
  if (parser_ != nullptr) {
    html_ = parser_->to_html();
  }
  return html_;
}

inline std::string Post::html_file_name() {
  return fmt::format("{}.html", id());
}

class Theme final {
public:
  explicit Theme(path theme_path) : base_path_(std::move(theme_path)) {
    static_path_ = base_path_ / path("static");
    template_path_ = base_path_ / path("templates");
  }

public:
  path static_path_;
  path template_path_;
  //
  path template_index{"index.html"};
  path template_post{"post.html"};
  path template_posts{"posts.html"};
  path template_rss{"rss.xml"};

private:
  path base_path_;
};

// Maker
class Maker final {
public:
  explicit Maker(ConfigPtr conf) : conf_(std::move(conf)) {}
  bool make();

private:
  void init();
  bool load();
  bool parse();
  bool generate() const;
  void prefill_payload(inja::json& payload) const;
  void make_posts(Environment& env, Theme& theme) const;
  void make_index(Environment& env, Theme& theme) const;
  void make_rss(Environment& env, Theme& theme) const;

private:
  ConfigPtr conf_;
  //
  std::vector<path> subdirs_;
  std::vector<PostPtr> posts_;
  std::vector<PostPtr> pages_;
  //
  path dist_path_;
  path post_dir_;
  path page_dir_;
};

using MakerPtr = std::shared_ptr<Maker>;

inline void Maker::init() {
  std::unordered_set<std::string> excluded_entries;
  std::for_each(conf_->exclude_source_entries.begin(), conf_->exclude_source_entries.end(),
                [&excluded_entries](auto& entry) { excluded_entries.emplace(entry); });
  const auto& dir_iter = directory_iterator{current_path()};
  std::for_each(begin(dir_iter), end(dir_iter), [this, &excluded_entries](const directory_entry& entry) {
    if (excluded_entries.count(entry.path().filename()) > 0) {
      return;
    }
    subdirs_.emplace_back(entry.path());
  });
  //
  dist_path_ = conf_->dist_dir;
  if (exists(dist_path_)) {
    for (const auto& entry : directory_iterator(dist_path_)) {
      remove_all(entry);
    }
  }
  create_directories(dist_path_);
  //
  post_dir_ = "posts";
  create_directory(dist_path_ / post_dir_);
  page_dir_ = "pages";
  create_directory(dist_path_ / page_dir_);
}

inline bool Maker::load() {
  static path posts_path{"posts"};
  static path pages_path{"pages"};
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
  // 按文件名从小到大排序，确定处理序列
  std::sort(posts_.begin(), posts_.end(), [](const PostPtr& lhs, const PostPtr& rhs) {
    return lhs->file_name() < rhs->file_name();
  });
  for (const auto& entry : directory_iterator(pages_path)) {
    if (!is_markdown_file(entry)) {
      continue;
    }
    pages_.emplace_back(std::make_shared<Post>(entry.path()));
  }
  return posts_.size() + pages_.size() > 0;
}

inline bool Maker::parse() {
  plugin::Plugins plugins;
  if (!plugins.init(conf_)) {
    return false;
  }
  for (const auto& post : posts_) {
    spdlog::debug("try to parse post: {}", post->file_path());
    if (!post->parse()) {
      spdlog::error("failed to parse post: {}", post->file_path());
      return false;
    }
    spdlog::debug("success to parse post: {}", post->file_path());
    plugins.run(post->parser());
  }
  plugins.destroy();
  // 按时间从大到小排序，如果时间相同，则比较标题
  std::sort(posts_.begin(), posts_.end(), [](const PostPtr& p1, const PostPtr& p2) {
    if (p1->updated_at() == p2->updated_at()) {
      return p1->title() > p2->title();
    }
    return p1->updated_at() > p2->updated_at();
  });
  //
  std::for_each(pages_.begin(), pages_.end(), [](auto& page) {
    spdlog::debug("try to pase page: {}", page->file_path());
    if (!page->parse()) {
      spdlog::error("failed to parse page: {}", page->file_path());
      return;
    }
    spdlog::debug("success to parse page: {}", page->file_path());
  });
  return true;
}

inline void Maker::prefill_payload(nlohmann::json& payload) const {
  payload["SITE_TITLE"] = conf_->site_title;
  payload["SITE_URL"] = conf_->site_url;
  payload["SITE_DESC"] = conf_->site_desc;
  if (!conf_->site_ico.empty()) {
    payload["SITE_ICO"] = conf_->site_ico;
  }
  //
  size_t idx = 0;
  for (const auto& [fst, snd] : conf_->navigation) {
    payload["navigation"][idx] = {{"url", snd}, {"name", fst}};
    idx++;
  }
}

// https://docs.getpelican.com/en/latest/themes.html
// https://github.com/pantor/inja
inline bool Maker::generate() const {
  Theme theme{conf_->theme};
  Environment env{absolute(theme.template_path_).string()};
  // post & page
  inja::json post_payload;
  prefill_payload(post_payload);
  const Template post_template = env.parse_template(theme.template_post);
  auto render_post = [&](const PostPtr& post, bool is_page) {
    post_payload["title"] = post->title();
    post_payload["updated_at"] = post->updated_at();
    post_payload["post_content"] = post->parser()->to_html();
    //
    const path base_path = dist_path_ / (is_page ? page_dir_ : post_dir_);

    path post_file_path = base_path / post->html_file_name();
    std::fstream post_file_stream{post_file_path, std::ios::out | std::ios::trunc};
    if (!post_file_stream.is_open()) {
      spdlog::error("could not open post file: {}", post_file_path);
      return;
    }
    env.render_to(post_file_stream, post_template, post_payload);
    post_file_stream.flush();
    post_file_stream.close();
  };
  for (const auto& post : posts_) {
    render_post(post, false);
  }
  for (const auto& post : pages_) {
    render_post(post, true);
  }
  // posts
  make_posts(env, theme);
  // rss
  if (exists(theme.template_path_ / "rss.xml")) {
    make_rss(env, theme);
  }
  // index
  make_index(env, theme);
  // copy static
  copy(theme.static_path_, dist_path_ / "static", copy_options::recursive);
  // copy other directories
  std::for_each(subdirs_.begin(), subdirs_.end(), [&](const path& subdir) {
    if (!exists(subdir)) {
      return;
    }
    std::string dir_name = subdir.stem().string();
    spdlog::debug("subdir: {}, dir_name: {}", subdir, dir_name);
    if (dir_name == "posts" || dir_name == "pages" || dir_name[0] == '.') {
      return;
    }
    copy(subdir, dist_path_ / dir_name, copy_options::recursive);
  });
  return true;
}

inline void Maker::make_posts(Environment& env, Theme& theme) const {
  inja::json posts_json;
  prefill_payload(posts_json);
  //
  size_t post_idx = 0;
  for (const auto& post : posts_) {
    posts_json["posts"][post_idx] = {
        {"title", post->title()},
        {"updated_at", post->updated_at()},
        {"url", fmt::format("/{0}/{1}", post_dir_.string(), post->html_file_name())},
    };
    post_idx++;
  }
  std::fstream posts_file_stream{dist_path_ / "posts.html", std::ios::out | std::ios::trunc};
  if (!posts_file_stream.is_open()) {
    spdlog::error("could not open posts file: {}", dist_path_ / "posts.html");
    return;
  }
  Template posts_template = env.parse_template(theme.template_posts);
  env.render_to(posts_file_stream, posts_template, posts_json);
  posts_file_stream.flush();
  posts_file_stream.close();
}

inline void Maker::make_index(Environment& env, Theme& theme) const {
  inja::json index_json;
  prefill_payload(index_json);
  //
  size_t post_idx = 0;
  for (const auto& post : posts_) {
    index_json["posts"][post_idx] = {
        {"title", post->title()},
        {"updated_at", post->updated_at()},
        {"url", fmt::format("/{0}/{1}", post_dir_.string(), post->html_file_name())},
    };
    post_idx++;
  }
  std::fstream index_file_stream{dist_path_ / "index.html", std::ios::out | std::ios::trunc};
  if (!index_file_stream.is_open()) {
    spdlog::error("could not open index file: {}", dist_path_ / "index.html");
    return;
  }
  Template index_template = env.parse_template(theme.template_index);
  env.render_to(index_file_stream, index_template, index_json);
  index_file_stream.flush();
  index_file_stream.close();
}

inline void Maker::make_rss(Environment& env, Theme& theme) const {
  inja::json rss_json;
  prefill_payload(rss_json);
  //
  rss_json["pub_date"] = absl::FormatTime(absl::Now());
  //
  size_t post_idx = 0;
  std::string site_url = conf_->site_url;
  if (site_url[site_url.size() - 1] == '/') {
    site_url = site_url.substr(0, site_url.size() - 1);
  }
  std::string post_dir = post_dir_.string();
  for (const auto& post : posts_) {
    rss_json["posts"][post_idx] = {
        {"title", post->title()},
        {"desc", inja::htmlescape(post->html())},
        {"pub_date", post->updated_at()},
        {"link", fmt::format("{0}/{1}/{2}", site_url, post_dir, post->html_file_name())},
    };
    post_idx++;
  }
  //
  std::fstream rss_file_stream{dist_path_ / "rss.xml", std::ios::out | std::ios::trunc};
  if (!rss_file_stream.is_open()) {
    spdlog::error("could not open rss file: {}", dist_path_ / "rss.xml");
    return;
  }
  Template rss_template = env.parse_template(theme.template_rss);
  env.render_to(rss_file_stream, rss_template, rss_json);
  rss_file_stream.flush();
  rss_file_stream.close();
}

inline bool Maker::make() {
  init();
  if (!load()) {
    return false;
  }
  spdlog::info("successfully loaded posts: {}", posts_.size() + pages_.size());
  parse();
  spdlog::debug("success to parse!");
  return generate();
}

}  // namespace ling