#pragma once

#include <absl/strings/ascii.h>
#include <spdlog/spdlog.h>

#include <filesystem>
#include <inja/inja.hpp>
#include <nlohmann/json.hpp>
#include <utility>

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

/*
 * cache 文件格式：
 * |4字节魔术数标识|4字节主题名字节数|主题名|4字节主题文件数|8字节主题最近变更时间|4字节 md 文件数量|逐个 md 文件的元信息...|
 *
 * 单个 md 文件的元信息格式：
 * |4字节文件名字节数|文件名|8个字节文件最近变更时间|4字节文件大小字节数|
 */

class FileFingerprint {
public:
  FileFingerprint() = default;
  explicit FileFingerprint(std::string file_name): name(std::move(file_name)) {}
  FileFingerprint(std::string file_name, uint64_t file_last_updated_time, uint32_t file_size_or_cnt):
    name(std::move(file_name)), last_updated_time(file_last_updated_time), size_bytes_or_file_cnt(file_size_or_cnt) {}

  std::string name;
  uint64_t last_updated_time {0};
  uint32_t size_bytes_or_file_cnt {0};
};

// 4 bytes，32 bits
static int CACHE_MAGIC_HEADER = 0x20130808;

class BuildCache final {
public:
  static BuildCache& singleton() {
    static BuildCache cache_;
    return cache_;
  }
  bool load();
  bool has_cache() const {
    return has_cache_;
  }
  bool has_md_deleted();
  bool save();
  bool check_theme_change(const path& theme_path);
  void cache_theme(const path& theme_path);
  bool check_md_change(const path& md_path);
  void cache_md(const path& md_path);

private:
  static FileFingerprint theme_fingerprint(const path& theme_path);
  static FileFingerprint md_fingerprint(const path& md_path);

  std::string cache_file_name_ {".build_cache"};
  bool has_cache_ = false;
  FileFingerprint pre_theme_fp_ {"default"};
  std::unordered_map<std::string, FileFingerprint> pre_md_fp_m_;
  //
  FileFingerprint theme_fp_ {"default"};
  std::unordered_map<std::string, FileFingerprint> md_fp_m_;
};

inline bool BuildCache::load() {
  if (!exists(cache_file_name_)) {
    spdlog::info("cache file {} not exist", cache_file_name_);
    return true;
  }
  std::ifstream ifs {cache_file_name_, std::ios::binary};
  if (!ifs.is_open()) {
    spdlog::error("failed to open {} file", cache_file_name_);
    return false;
  }
  utils::DeferGuard close_guard {[&ifs]() {
    ifs.close();
  }};
  int cache_magic_header;
  ifs >> cache_magic_header;
  if (cache_magic_header != CACHE_MAGIC_HEADER) {
    spdlog::error("illegal cache file, magic header is {}", cache_magic_header);
    return false;
  }
  //
  uint64_t now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
  //
  uint32_t theme_name_bytes;
  ifs >> theme_name_bytes;
  if (theme_name_bytes == 0) {
    spdlog::error("illegal cache file, theme_name_bytes: {}", theme_name_bytes);
    return false;
  }
  std::string theme_name;
  ifs.read(theme_name.data(), theme_name_bytes);
  uint32_t theme_file_cnt;
  ifs >> theme_file_cnt;
  if (theme_file_cnt == 0) {
    spdlog::error("illegal cache file, theme file cnt: {}", theme_file_cnt);
    return false;
  }
  uint64_t theme_last_updated;
  ifs >> theme_last_updated;
  if (theme_last_updated == 0 || theme_last_updated >= now) {
    spdlog::error("illegal cache file, theme_last_updated: {}", theme_last_updated);
    return false;
  }
  pre_theme_fp_.name = theme_name;
  pre_theme_fp_.size_bytes_or_file_cnt = theme_file_cnt;
  pre_theme_fp_.last_updated_time = theme_last_updated;
  //
  uint32_t md_file_num;
  ifs >> md_file_num;
  if (md_file_num > 0) {
    pre_md_fp_m_.reserve(md_file_num);
    while (md_file_num > 0) {
      uint32_t name_bytes_cnt;
      ifs >> name_bytes_cnt;
      if (name_bytes_cnt <= 0) {
        spdlog::error("illegal cache file, name_bytes_cnt: {}", name_bytes_cnt);
        return false;
      }
      std::string md_file_name;
      ifs.read(md_file_name.data(), name_bytes_cnt);
      uint64_t md_last_updated;
      uint32_t md_size_bytes;
      ifs >> md_last_updated >> md_size_bytes;
      if (md_last_updated == 0 || md_last_updated >= now || md_size_bytes == 0) {
        spdlog::error("illegal cache file, md_file_name: {}, md_last_updated: {}, md_hash_val: {}", md_file_num, md_last_updated, md_size_bytes);
        return false;
      }
      pre_md_fp_m_[md_file_name] = {md_file_name, md_last_updated, md_size_bytes};
      md_file_num--;
    }
  }
  has_cache_ = true;
  return true;
}

inline bool BuildCache::save() {
  std::ofstream ofs {cache_file_name_, std::ios::binary | std::ios::trunc};
  if (!ofs.is_open()) {
    spdlog::error("failed to open {}", cache_file_name_);
    return false;
  }
  utils::DeferGuard close_guard {[&ofs]() {
    ofs.close();
  }};
  ofs << CACHE_MAGIC_HEADER;
  ofs << theme_fp_.name.size() << theme_fp_.name.data() << theme_fp_.size_bytes_or_file_cnt << theme_fp_.last_updated_time;
  ofs << md_fp_m_.size();
  for (const auto& [name, fp] : md_fp_m_) {
    ofs << static_cast<uint32_t>(name.size()) << name.data() << fp.last_updated_time << fp.size_bytes_or_file_cnt;
  }
  ofs.flush();
  return true;
}

inline bool BuildCache::has_md_deleted() {
  if (pre_md_fp_m_.size() > md_fp_m_.size()) {
    return true;
  }
  for (const auto& [md_file_name, _] : pre_md_fp_m_) {
    if (md_fp_m_.count(md_file_name) == 0) {
      return true;
    }
  }
  return false;
}

inline FileFingerprint BuildCache::theme_fingerprint(const path& theme_path) {
  const auto theme_name = theme_path.filename();
  // 遍历找出所有文件路径，检测文件数量，以及最近变更文件的变更时间
  std::vector<path> path_vec;
  std::queue<path> q;
  q.push(theme_path);
  while (!q.empty()) {
    const auto p = q.front();
    q.pop();
    for (const auto& entry : directory_iterator(p)) {
      if (entry.is_directory()) {
        q.push(p / entry);
        continue;
      }
      if (entry.path().filename().string()[0] == '.') { // 忽略隐藏文件
        continue;
      }
      path_vec.emplace_back(p / entry);
    }
  }
  std::sort(path_vec.begin(), path_vec.end(), [](const path& lhs, const path& rhs) -> bool {
    return last_write_time(lhs) > last_write_time(rhs);
  });
  uint64_t last_updated_time =
      std::chrono::duration_cast<std::chrono::seconds>(last_write_time(path_vec[0]).time_since_epoch()).count();
  return {theme_name, last_updated_time, static_cast<uint32_t>(path_vec.size())};
}

inline bool BuildCache::check_theme_change(const path& theme_path) {
  if (!has_cache_) {
    return true;
  }
  if (!is_directory(theme_path)) {
    spdlog::error("{} not a directory", theme_path.string());
    return true;
  }
  const auto& theme_fp = theme_fingerprint(theme_path);
  const auto theme_name = theme_fp.name;
  if (theme_name != pre_theme_fp_.name) {
    return true;
  }
  if (theme_fp.size_bytes_or_file_cnt != pre_theme_fp_.size_bytes_or_file_cnt) {
    return true;
  }
  const bool is_changed = pre_theme_fp_.last_updated_time != theme_fp.last_updated_time;
  if (!is_changed) {
    theme_fp_.name = pre_theme_fp_.name;
    theme_fp_.last_updated_time = pre_theme_fp_.last_updated_time;
    theme_fp_.size_bytes_or_file_cnt = pre_theme_fp_.size_bytes_or_file_cnt;
  }
  return is_changed;
}

inline void BuildCache::cache_theme(const path& theme_path) {
  theme_fp_ = theme_fingerprint(theme_path);
}

inline FileFingerprint BuildCache::md_fingerprint(const path& md_path) {
  const auto md_file_name = md_path.filename();
  uint32_t md_size = file_size(md_path);
  uint64_t md_last_updated =
      std::chrono::duration_cast<std::chrono::seconds>(last_write_time(md_path).time_since_epoch()).count();
  return {md_file_name, md_last_updated, md_size};
}

inline bool BuildCache::check_md_change(const path& md_path) {
  if (!has_cache_) {
    return true;
  }
  const auto& md_fp = md_fingerprint(md_path);
  if (!pre_md_fp_m_.count(md_fp.name)) {
    return true;
  }
  const auto pre_md_fp = pre_md_fp_m_[md_fp.name];
  const bool is_changed = (md_fp.size_bytes_or_file_cnt != pre_md_fp.size_bytes_or_file_cnt || md_fp.last_updated_time != pre_md_fp.last_updated_time);
  if (!is_changed) {
    md_fp_m_[md_fp.name] = md_fp;
  }
  return is_changed;
}

inline void BuildCache::cache_md(const path& md_path) {
  const auto& md_fp = md_fingerprint(md_path);
  md_fp_m_[md_fp.name] = md_fp;
}

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

// Maker
class Maker final {
public:
  Maker() = default;
  bool make(bool force=false);

private:
  void init();
  bool load();
  bool parse();
  bool generate();
  void make_posts(Environment& env) const;
  void make_index(Environment& env) const;
  void make_rss(Environment& env) const;

private:
  bool ignore_cache_ = false;
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
  auto& build_cache = BuildCache::singleton();
  build_cache.load();
  if (!build_cache.has_cache() || build_cache.check_theme_change(conf_->theme_ptr->base_path_)) {
    ignore_cache_ = true;
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
  auto& build_cache = BuildCache::singleton();
  for (const auto& entry : directory_iterator(posts_path)) {
    if (!is_markdown_file(entry)) {
      continue;
    }
    if (!ignore_cache_ && !build_cache.check_md_change(entry.path())) {
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
    if (!ignore_cache_ && !build_cache.check_md_change(entry.path())) {
      continue;
    }
    pages_.emplace_back(std::make_shared<Post>(entry.path()));
  }
  return posts_.size() + pages_.size() > 0;
}

inline bool Maker::parse() {
  plugin::Plugins plugins;
  if (!plugins.init(Context::singleton())) {
    return false;
  }
  auto& build_cache = BuildCache::singleton();
  for (const auto& post : posts_) {
    spdlog::debug("try to parse post: {}", post->file_path());
    if (!post->parse()) {
      spdlog::error("failed to parse post: {}", post->file_path());
      return false;
    }
    spdlog::debug("success to parse post: {}", post->file_path());
    plugins.run(post->parser());
    //
    build_cache.cache_md(post->file_path());
  }
  // 按时间从大到小排序，如果时间相同，则比较标题
  std::sort(posts_.begin(), posts_.end(), [](const PostPtr& p1, const PostPtr& p2) {
    if (p1->updated_at() == p2->updated_at()) {
      return p1->title() > p2->title();
    }
    return p1->updated_at() > p2->updated_at();
  });
  //
  std::for_each(pages_.begin(), pages_.end(), [&](auto& page) {
    spdlog::debug("try to pase page: {}", page->file_path());
    if (!page->parse()) {
      spdlog::error("failed to parse page: {}", page->file_path());
      return;
    }
    spdlog::debug("success to parse page: {}", page->file_path());
    plugins.run(page->parser());
    //
    build_cache.cache_md(page->file_path());
  });
  plugins.destroy();
  return true;
}

// https://docs.getpelican.com/en/latest/themes.html
// https://github.com/pantor/inja
inline bool Maker::generate() {
  auto& conf_ptr = Context::singleton()->with_config();
  auto& build_cache = BuildCache::singleton();
  if (ignore_cache_ || build_cache.has_md_deleted()) {
    dist_path_ = conf_ptr->dist_dir;
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
  //
  build_cache.cache_theme(conf_ptr->theme_ptr->base_path_);
  //
  auto& render_ctx = Context::singleton()->with_render_ctx();
  conf_ptr->assemble(render_ctx);
  auto& theme_ptr = conf_ptr->theme_ptr;
  Environment env{absolute(theme_ptr->template_path_).string()};
  // post & page
  const Template post_template = env.parse_template(theme_ptr->template_post);
  auto render_post = [&](const PostPtr& post, bool is_page) {
    render_ctx["title"] = post->title();
    render_ctx["updated_at"] = post->updated_at();
    render_ctx["post_content"] = post->parser()->to_html();
    //
    const path base_path = dist_path_ / (is_page ? page_dir_ : post_dir_);

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
  for (const auto& post : posts_) {
    render_post(post, false);
  }
  for (const auto& post : pages_) {
    render_post(post, true);
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
  for (const auto& post : posts_) {
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
  for (const auto& post : posts_) {
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
  for (const auto& post : posts_) {
    render_ctx["posts"][post_idx] = {
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
  Template rss_template = env.parse_template(theme->template_rss);
  env.render_to(rss_file_stream, rss_template, render_ctx);
  rss_file_stream.flush();
  rss_file_stream.close();
}

inline bool Maker::make(bool force) {
  ignore_cache_ = force;
  init();
  if (!load()) {
    return false;
  }
  spdlog::info("successfully loaded posts: {}", posts_.size() + pages_.size());
  parse();
  spdlog::debug("success to parse!");
  bool status = generate();
  if (status) {
    status = BuildCache::singleton().save();
  }
  return status;
}

}  // namespace ling