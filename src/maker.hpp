#pragma once

#include <spdlog/spdlog.h>

#include <filesystem>
#include <inja/inja.hpp>
#include <nlohmann/json.hpp>

#include "absl/time/clock.h"
#include "context.hpp"
#include "parser/markdown.h"
#include "plugin/plugins.hpp"
#include "utils/time.hpp"
#include "utils/guard.hpp"

namespace ling {

using namespace std::filesystem;

using inja::Environment;
using inja::Template;

// Post
class Post {
public:
  Post() = default;
  explicit Post(const path& file_path, bool is_page=false);
  bool parse() const;
  virtual ~Post() = default;

  MarkdownPtr parser() {
    return parser_;
  }

  [[nodiscard]] bool is_page() const {
    return is_page_;
  }

  [[nodiscard]] std::string title();
  [[nodiscard]] std::string updated_at();
  [[nodiscard]] std::string id();
  [[nodiscard]] std::string html();
  [[nodiscard]] std::string html_file_name();
  [[nodiscard]] std::string file_path() {
    return file_path_.string();
  }
  file_time_type& file_last_write_time() {
    return last_write_time_;
  }
  std::string file_name() {
    return file_name_;
  }

protected:
  bool is_page_ = false;
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

inline Post::Post(const path& file_path, bool is_page) {
  is_page_ = is_page;
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
            title_ = h1->title_->to_html();
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

class CachedPost final : public Post {
public:
  CachedPost() = default;
  explicit CachedPost(inja::json& j);
  //
  void to_json(inja::json& j);
};

inline CachedPost::CachedPost(inja::json &j) {
  file_path_ = path(j["file_path"].get<std::string>());
  //
  uint64_t ts_nanosec = j["file_last_write_time"].get<uint64_t>();
  last_write_time_ = file_time_type(std::chrono::nanoseconds(ts_nanosec));
  post_updated_ = utils::convert(last_write_time_);
  //
  file_name_ = file_path_.stem();
  id_ = j["post_id"].get<std::string>();
  title_ = j["post_title"].get<std::string>();
  updated_at_ = j["post_updated_at"].get<std::string>();
  html_ = j["post_content_html"].get<std::string>();
  //
  is_page_ = j["is_page"].get<bool>();
}

inline void CachedPost::to_json(inja::json &j) {
  j["file_path"] = file_path();
  j["file_last_write_time"] = std::chrono::duration_cast<std::chrono::nanoseconds>(last_write_time_.time_since_epoch()).count();
  j["post_id"] = id();
  j["post_title"] = title();
  j["post_updated_at"] = updated_at();
  j["post_content_html"] = html();
  j["is_page"] = is_page_;
}

using CachedPostPtr = std::shared_ptr<CachedPost>;

class MakerCache final {
public:
  static MakerCache& singleton() {
    static MakerCache cache_;
    return cache_;
  }
  //
  const static path MAKER_CACHE_FILE_PATH;
  static constexpr uint32_t MAGIC_HEADER = 20130808;
  //
  bool load();
  bool store();
  void cache_it(const PostPtr& post);
  std::pair<bool, PostPtr> match_then_get(std::string& file_path, file_time_type& file_last_write);

private:
  std::unordered_map<std::string, CachedPostPtr> pre_state_;
  std::unordered_map<std::string, CachedPostPtr> state_;
  //
  uint32_t changed_post_cnt = 0;
};

const path MakerCache::MAKER_CACHE_FILE_PATH = path(".make_cache");

inline bool MakerCache::load() {
  if (!exists(MAKER_CACHE_FILE_PATH)) {
    return true;
  }
  std::ifstream ifs {MAKER_CACHE_FILE_PATH, std::ios::binary};
  if (!ifs.is_open()) {
    spdlog::error("when load, failure to open cache file: {}", MAKER_CACHE_FILE_PATH);
    return false;
  }
  //
  utils::DeferGuard ifs_close_guard {[&ifs]() {
    ifs.close();
  }};
  //
  uint32_t magic_header;
  ifs.read(reinterpret_cast<char*>(&magic_header), sizeof(magic_header));
  if (MAGIC_HEADER != magic_header) {
    spdlog::error("illegal cache file, {} != {}", magic_header, MAGIC_HEADER);
    return false;
  }
  uint32_t state_size;
  ifs.read(reinterpret_cast<char*>(&state_size), sizeof(state_size));
  if (state_size == 0) {
    spdlog::warn("has no cached post");
    return false;
  }
  pre_state_.reserve(state_size);
  uint32_t state_idx = 0;
  while (state_idx < state_size) {
    long j_str_size;
    ifs.read(reinterpret_cast<char*>(&j_str_size), sizeof(j_str_size));
    if (j_str_size <= 0) {
      spdlog::error("illegal cache, bson size: {}", j_str_size);
      return false;
    }
    std::string j_str(j_str_size, '\0');
    ifs.read(reinterpret_cast<char*>(j_str.data()), j_str_size);
    //
    inja::json j = inja::json::parse(j_str);
    auto cp_ptr = std::make_shared<CachedPost>(j);
    pre_state_[cp_ptr->file_path()] = cp_ptr;
    //
    state_idx++;
  }
  return true;
}

inline bool MakerCache::store() {
  if (changed_post_cnt == 0 && state_.size() == pre_state_.size()) { // 无变更，不重写缓存状态文件
    return true;
  }
  std::ofstream ofs {MAKER_CACHE_FILE_PATH, std::ios::binary | std::ios::trunc};
  if (!ofs.is_open()) {
    spdlog::error("when store, failure to open cache file: {}", MAKER_CACHE_FILE_PATH);
    return false;
  }
  //
  utils::DeferGuard ofs_close_guard {[&ofs]() {
    ofs.close();
  }};
  //
  ofs.write(reinterpret_cast<const char*>(&MAGIC_HEADER), sizeof(MAGIC_HEADER));
  uint32_t state_size = state_.size();
  ofs.write(reinterpret_cast<const char*>(&state_size), sizeof(state_size));
  for (const auto& [_, pp] : state_) {
    inja::json j {};
    pp->to_json(j);
    std::string j_str = j.dump();
    long total_size = static_cast<long>(j_str.size());
    ofs.write(reinterpret_cast<const char*>(&total_size), sizeof(total_size));
    ofs.write(reinterpret_cast<const char*>(j_str.data()), total_size);
  }
  ofs.flush();
  return true;
}

inline void MakerCache::cache_it(const PostPtr& post) {
  if (state_.count(post->file_path())) {
    spdlog::warn("has in cache: {}", post->file_path());
  }
  state_[post->file_path()] = std::reinterpret_pointer_cast<CachedPost>(post);
  changed_post_cnt++;
}

inline std::pair<bool, PostPtr> MakerCache::match_then_get(std::string& file_path, file_time_type& file_last_write) {
  if (pre_state_.count(file_path) == 0) {
    return std::make_pair(false, nullptr);
  }
  auto post_ptr = pre_state_[file_path];
  if (post_ptr->file_last_write_time() != file_last_write) {
    return std::make_pair(false, post_ptr);
  }
  state_[file_path] = post_ptr;
  return std::make_pair(true, post_ptr);
}


// Maker
class Maker final {
public:
  Maker() = default;
  bool make(bool ignore_cache);

private:
  void init();
  bool load();
  bool parse();
  [[nodiscard]] bool generate() const;
  void make_posts(Environment& env) const;
  void make_index(Environment& env) const;
  void make_rss(Environment& env) const;

private:
  std::vector<path> subdirs_;
  std::vector<PostPtr> posts_;
  std::vector<PostPtr> pages_;
  //
  std::vector<PostPtr> parsed_posts_;
  std::vector<PostPtr> parsed_pages_;
  //
  path dist_path_;
  path post_dir_;
  path page_dir_;
  //
  bool ignore_cache_ = false;
};

using MakerPtr = std::shared_ptr<Maker>;

inline void Maker::init() {
  std::unordered_set<std::string> excluded_entries;
  const auto conf_ = Context::singleton()->with_config();
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
  post_dir_ = "posts";
  page_dir_ = "pages";
  //
  if (!ignore_cache_ && !MakerCache::singleton().load()) {
    spdlog::error("failure to load maker cache");
  }
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
    pages_.emplace_back(std::make_shared<Post>(entry.path(), true));
  }
  return posts_.size() + pages_.size() > 0;
}

inline bool Maker::parse() {
  plugin::Plugins plugins;
  if (!plugins.init(Context::singleton())) {
    return false;
  }
  auto& maker_cache = MakerCache::singleton();
  for (const auto& post : posts_) {
    auto post_file_path = post->file_path();
    if (!ignore_cache_) {
      auto [status, pp] = maker_cache.match_then_get(post_file_path, post->file_last_write_time());
      if (status) {
        spdlog::info("match cache: {}", post->file_path());
        parsed_posts_.emplace_back(pp);
        continue;
      }
    }
    spdlog::debug("try to parse post: {}", post_file_path);
    if (!post->parse()) {
      spdlog::error("failed to parse post: {}", post_file_path);
      return false;
    }
    spdlog::debug("success to parse post: {}", post_file_path);
    plugins.run(post->parser());
    parsed_posts_.emplace_back(post);
    maker_cache.cache_it(post);
  }
  // 按时间从大到小排序，如果时间相同，则比较标题
  std::sort(parsed_posts_.begin(), parsed_posts_.end(), [](const PostPtr& p1, const PostPtr& p2) {
    if (p1->updated_at() == p2->updated_at()) {
      return p1->title() > p2->title();
    }
    return p1->updated_at() > p2->updated_at();
  });
  //
  std::for_each(pages_.begin(), pages_.end(), [&](auto& page) {
    auto page_file_path = page->file_path();
    if (!ignore_cache_) {
      auto [status, pp] = maker_cache.match_then_get(page_file_path, page->file_last_write_time());
      if (status) {
        spdlog::info("match cache: {}", page->file_name());
        parsed_pages_.emplace_back(pp);
        return;
      }
    }
    spdlog::debug("try to parse page: {}", page->file_path());
    if (!page->parse()) {
      spdlog::error("failed to parse page: {}", page->file_path());
      return;
    }
    spdlog::debug("success to parse page: {}", page->file_path());
    plugins.run(page->parser());
    parsed_pages_.emplace_back(page);
    maker_cache.cache_it(page);
  });
  plugins.destroy();
  if (!maker_cache.store()) {
    spdlog::error("failure to store maker cache");
  }
  return true;
}

// https://docs.getpelican.com/en/latest/themes.html
// https://github.com/pantor/inja
inline bool Maker::generate() const {
  if (exists(dist_path_)) {
    for (const auto& entry : directory_iterator(dist_path_)) {
      remove_all(entry);
    }
  }
  create_directories(dist_path_);
  create_directory(dist_path_ / post_dir_);
  create_directory(dist_path_ / page_dir_);
  //
  auto& conf_ptr = Context::singleton()->with_config();
  auto& render_ctx = Context::singleton()->with_render_ctx();
  conf_ptr->assemble(render_ctx);
  auto& theme_ptr = conf_ptr->theme_ptr;
  Environment env{absolute(theme_ptr->template_path_).string()};
  // post & page
  const Template post_template = env.parse_template(theme_ptr->template_post);
  auto render_post = [&](const PostPtr& post) {
    render_ctx["title"] = post->title();
    render_ctx["updated_at"] = post->updated_at();
    render_ctx["post_content"] = post->html();
    //
    const path base_path = dist_path_ / (post->is_page() ? page_dir_ : post_dir_);

    path post_file_path = base_path / post->html_file_name();
    std::fstream post_file_stream{post_file_path, std::ios::out | std::ios::trunc};
    if (!post_file_stream.is_open()) {
      spdlog::error("could not open post file: {}", post_file_path);
      return;
    }
    env.render_to(post_file_stream, post_template, render_ctx);
    post_file_stream.flush();
    post_file_stream.close();
    //
    render_ctx.erase("title");
    render_ctx.erase("updated_at");
    render_ctx.erase("post_content");
  };
  for (const auto& post : parsed_posts_) {
    render_post(post);
  }
  for (const auto& post : parsed_pages_) {
    render_post(post);
  }
  // posts
  make_posts(env);
  // rss
  if (exists(theme_ptr->template_path_ / "rss.xml")) {
    make_rss(env);
  }
  // index
  make_index(env);
  // copy static
  copy(theme_ptr->static_path_, dist_path_ / "static", copy_options::recursive);
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

inline void Maker::make_posts(Environment& env) const {
  auto& context = Context::singleton();
  auto render_ctx = context->with_render_ctx();
  auto& theme = context->with_config()->theme_ptr;
  //
  size_t post_idx = 0;
  for (const auto& post : parsed_posts_) {
    render_ctx["posts"][post_idx] = {
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
  Template posts_template = env.parse_template(theme->template_posts);
  env.render_to(posts_file_stream, posts_template, render_ctx);
  posts_file_stream.flush();
  posts_file_stream.close();
}

inline void Maker::make_index(Environment& env) const {
  auto& context = Context::singleton();
  auto render_ctx = context->with_render_ctx();
  auto& theme = context->with_config()->theme_ptr;
  //
  size_t post_idx = 0;
  for (const auto& post : parsed_posts_) {
    render_ctx["posts"][post_idx] = {
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
  Template index_template = env.parse_template(theme->template_index);
  env.render_to(index_file_stream, index_template, render_ctx);
  index_file_stream.flush();
  index_file_stream.close();
}

inline void Maker::make_rss(Environment& env) const {
  auto& context = Context::singleton();
  auto render_ctx = context->with_render_ctx();
  auto& theme = context->with_config()->theme_ptr;
  //
  render_ctx["pub_date"] = absl::FormatTime(absl::Now());
  //
  size_t post_idx = 0;
  std::string site_url = Context::singleton()->with_config()->site_url;
  if (site_url[site_url.size() - 1] == '/') {
    site_url = site_url.substr(0, site_url.size() - 1);
  }
  std::string post_dir = post_dir_.string();
  for (const auto& post : parsed_posts_) {
    render_ctx["posts"][post_idx] = {
        {"title", post->title()},
        {"desc", "<![CDATA[" + post->html() + "]]>"},
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
  Template rss_template = env.parse_template(theme->template_rss);
  env.render_to(rss_file_stream, rss_template, render_ctx);
  rss_file_stream.flush();
  rss_file_stream.close();
}

inline bool Maker::make(bool ignore_cache) {
  ignore_cache_ = ignore_cache;
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