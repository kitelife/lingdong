#pragma once

#include <filesystem>
#include <memory>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"
#include "hnswlib/hnswlib.h"

#include "utils/helper.hpp"
#include "utils/guard.hpp"
#include "utils/perf.hpp"
#include "utils/executor.hpp"

namespace ling::storage {

using namespace ling::utils;
using namespace hnswlib;

static std::string HN_STORY_FILE = "hn_story.json";
static std::string HN_STORY_META_FILE = "hn_story_meta.json";
static std::string HN_STORY_EMB_FILE = "hn_story_emb.json";
static std::string HN_STORY_EMB_META_FILE = "hn_story_emb_meta.json";
static std::string HN_STORY_HNSW_IDX_FILE = "hn_story_hnsw.bin";

static std::filesystem::path DEFAULT_ROOT_PATH {"."};

// https://github.com/HackerNews/API

class HackerNewItem {
public:
  HackerNewItem() = default;

  uint32_t id; // The item's unique id.
  bool deleted; // true if the item is deleted.
  std::string type; // The type of item. One of "job", "story", "comment", "poll", or "pollopt".
  std::string by; // The username of the item's author.
  uint64_t time; // Creation date of the item, in Unix Time.
  bool dead; // true if the item is dead.
  std::string url; // The URL of the story.
  int score; // The story's score, or the votes for a pollopt.
  std::string title; // The title of the story, poll or job. HTML.

  void from_json(nlohmann::json &j);

  void to_json(nlohmann::json &j);
};

using HackerNewItemPtr = std::shared_ptr<HackerNewItem>;

inline void HackerNewItem::from_json(nlohmann::json &j) {
  id = j["id"].get<uint32_t>();
  if (j.contains("by")) {
    by = j["by"].get<std::string>();
  }
  if (j.contains("time")) {
    time = j["time"].get<uint64_t>();
  }
  if (j.contains("url")) {
    url = j["url"].get<std::string>();
  }
  if (j.contains("type")) {
    type = j["type"].get<std::string>();
  }
  if (j.contains("title")) {
    title = j["title"].get<std::string>();
  }
  if (j.contains("score")) {
    score = j["score"].get<int>();
  }
  if (j.contains("deleted")) {
    deleted = j["deleted"].get<bool>();
  }
  if (j.contains("dead")) {
    dead = j["dead"].get<bool>();
  }
}

inline void HackerNewItem::to_json(nlohmann::json &j) {
  j["id"] = id;
  j["deleted"] = deleted;
  j["type"] = type;
  j["by"] = by;
  j["time"] = time;
  j["dead"] = dead;
  j["url"] = url;
  j["score"] = score;
  j["title"] = title;
}

class HnEmbMeta {
public:
  std::string model_name;
  uint32_t dim{};
  uint32_t cnt{};

  void from_json(nlohmann::json& j) {
    if (j.contains("model_name")) {
      auto new_model_name = j["model_name"].get<std::string>();
      if (!model_name.empty() && model_name != new_model_name) {
        spdlog::error("HnEmbMeta: model name mismatch");
      }
      model_name = new_model_name;
    }
    if (j.contains("dim")) {
      auto new_dim = j["dim"].get<uint32_t>();
      if (dim > 0 && dim != new_dim) {
        spdlog::error("HnEmbMeta: dim mismatch");
      }
      dim = new_dim;
    }
    if (j.contains("cnt")) {
      cnt += j["cnt"].get<uint32_t>();
    }
  }
};

inline std::vector<std::string> all_versions(const std::filesystem::path& root = DEFAULT_ROOT_PATH) {
  std::vector<std::string> versions;
  for (const auto& entry : std::filesystem::directory_iterator(root)) {
    if (entry.is_directory()) {
      versions.emplace_back(entry.path().stem());
    }
  }
  return versions;
}

inline std::string last_version(const std::filesystem::path& root = DEFAULT_ROOT_PATH) {
  auto versions = all_versions(root);
  return *(std::max_element(versions.begin(), versions.end(), [](const auto& v1, const auto& v2) { return v1 < v2; }));
}

inline uint32_t get_last_max_item_id(const std::filesystem::path& root = DEFAULT_ROOT_PATH) {
  auto version_path = root / last_version();
  auto hn_story_meta_file_path = version_path / HN_STORY_META_FILE;
  if (!exists(hn_story_meta_file_path)) {
    spdlog::warn("hn story meta file not exist: {}", hn_story_meta_file_path.string());
    return 0;
  }
  std::ifstream hn_story_meta_ifs(hn_story_meta_file_path);
  std::string line;
  std::getline(hn_story_meta_ifs, line);
  auto j = nlohmann::json::parse(line);
  if (j.contains("max_item_id")) {
    return j["max_item_id"].get<uint32_t>();
  }
  return 0;
}

inline std::string generate_next_version() {
  const auto now = std::chrono::system_clock::now();
  auto time_now = absl::FromUnixMicros(now.time_since_epoch().count());
  return absl::FormatTime("%Y%m%d%H", time_now, absl::LocalTimeZone());
}

class HackNewsHnsw {
public:
  static HackNewsHnsw& singleton() {
    static HackNewsHnsw hn_hnsw;
    return hn_hnsw;
  }
  //
  HackNewsHnsw() = default;
  void data_path(const std::string& p) {
    data_path_ = p;
  }
  bool load_fwd();
  bool load();
  bool load_meta();
  HnEmbMeta& meta() {
    return meta_;
  }
  //
  std::vector<std::pair<HackerNewItemPtr, float>> search(Embedding query_emb, uint32_t top_k=100);

private:
  std::filesystem::path data_path_;
  //
  HnEmbMeta meta_;
  std::shared_ptr<std::unordered_map<uint32_t, HackerNewItemPtr>> fwd_ptr_ = nullptr;
  std::shared_ptr<HierarchicalNSW<float>> hnsw_ptr_;
  std::atomic_bool loaded_ {false};
  //
  bool load_hnsw();
};

inline bool HackNewsHnsw::load_meta() {
  auto versions = all_versions(data_path_);
  for (const auto& v : versions) {
    std::filesystem::path meta_file_path {data_path_ / v / HN_STORY_EMB_META_FILE};
    if (!std::filesystem::exists(meta_file_path)) {
      spdlog::error("not exist meta file {}", meta_file_path.string());
      continue;
    }
    std::ifstream meta_ifs {meta_file_path};
    if (!meta_ifs.is_open()) {
      spdlog::error("failure to open meta file '{}'", meta_file_path.string());
      continue;
    }
    DeferGuard ifs_close_guard {[&meta_ifs]() {
      meta_ifs.close();
    }};
    ScopedTimer timer {"load_meta"};
    std::string line;
    std::getline(meta_ifs, line);
    auto j = nlohmann::json::parse(line);
    meta_.from_json(j);
  }
  return true;
}

inline bool HackNewsHnsw::load_fwd() {
  auto versions = all_versions(data_path_);
  for (const auto& v : versions) {
    std::ifstream ifs;
    char stream_buffer[1024 * 512];
    ifs.rdbuf()->pubsetbuf(stream_buffer, 1024 * 512);
    //
    auto p = data_path_ / v / HN_STORY_FILE;
    ifs.open(p);
    if (!ifs.is_open()) {
      spdlog::error("failure to open file {}", p.string());
      continue;
    }
    //
    ScopedTimer timer {"load_fwd"};
    Executor tp {"load-fwd"};
    DeferGuard tp_join_guard{[&tp]() {
      tp.join();
    }};
    auto local_fwd_ptr = std::make_shared<std::unordered_map<uint32_t, HackerNewItemPtr>>();
    std::mutex fwd_lock;
    uint32_t line_cnt = 0;
    for (std::string line; std::getline(ifs, line);) {
      line_cnt++;
      tp.async_execute([line, &local_fwd_ptr, &fwd_lock]() {
        nlohmann::json j = nlohmann::json::parse(line);
        auto item_ptr = std::make_shared<HackerNewItem>();
        item_ptr->from_json(j);
        //
        std::lock_guard guard(fwd_lock);
        local_fwd_ptr->insert({item_ptr->id, item_ptr});
      });
    }
    while (local_fwd_ptr->size() < line_cnt) {
      std::this_thread::sleep_for(milliseconds(100));
    }
    fwd_ptr_->insert(local_fwd_ptr->begin(), local_fwd_ptr->end());
    timer.end();
    ifs.close();
  }
  return true;
}

inline bool HackNewsHnsw::load_hnsw() {
  std::filesystem::path hn_hnsw_idx_path { data_path_/ HN_STORY_HNSW_IDX_FILE};
  if (!std::filesystem::exists(hn_hnsw_idx_path)) {
    spdlog::error("hnsw idx file '{}' not exist", HN_STORY_HNSW_IDX_FILE);
    return false;
  }
  ScopedTimer timer {"load_hnsw"};
  InnerProductSpace metric_space {meta_.dim};
  hnsw_ptr_ = std::make_shared<HierarchicalNSW<float>>(&metric_space, hn_hnsw_idx_path.string());
  hnsw_ptr_->checkIntegrity();
  return true;
}

inline bool HackNewsHnsw::load() {
  if (loaded_) {
    spdlog::warn("HackNewsHnsw load already loaded");
    return false;
  }
  auto status = load_meta() && load_fwd() && load_hnsw();
  loaded_ = status;
  return status;
}

inline std::vector<std::pair<HackerNewItemPtr, float>> HackNewsHnsw::search(Embedding query_emb, uint32_t top_k) {
  if (!loaded_) {
    spdlog::error("hnsw index has not loaded");
    return {};
  }
  //
  std::priority_queue<std::pair<float, labeltype>> result_pq;
  try {
    result_pq = hnsw_ptr_->searchKnn(query_emb.data(), top_k);
  } catch (std::runtime_error &e) {
    spdlog::error("hnsw search failed: {}", e.what());
    return {};
  }
  //
  std::vector<std::pair<HackerNewItemPtr, float>> query_result;
  query_result.reserve(top_k);
  while (!result_pq.empty()) {
    auto& r = result_pq.top();
    auto id = r.second;
    if (fwd_ptr_->count(id) == 0) {
      spdlog::warn("result {} has no fwd idx, score: {}", id, r.first);
    } else {
      auto& hn_item = fwd_ptr_->at(id);
      query_result.emplace_back(hn_item, r.first);
    }
    result_pq.pop();
  }
  return query_result;
}

}