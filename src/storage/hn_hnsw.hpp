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
      model_name = j["model_name"].get<std::string>();
    }
    if (j.contains("dim")) {
      dim = j["dim"].get<uint32_t>();
    }
    if (j.contains("cnt")) {
      cnt = j["cnt"].get<uint32_t>();
    }
  }
};

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
  std::filesystem::path meta_file_path {data_path_ / HN_STORY_EMB_META_FILE};
  if (!std::filesystem::exists(meta_file_path)) {
    spdlog::error("not exist meta file {}", meta_file_path.string());
    return false;
  }
  std::ifstream meta_ifs {meta_file_path};
  if (!meta_ifs.is_open()) {
    spdlog::error("failure to open meta file '{}'", HN_STORY_EMB_META_FILE);
    return false;
  }
  DeferGuard ifs_close_guard {[&meta_ifs]() {
      meta_ifs.close();
  }};
  ScopedTimer timer {"load_meta"};
  std::string line;
  std::getline(meta_ifs, line);
  auto j = nlohmann::json::parse(line);
  meta_.from_json(j);
  return true;
}

inline bool HackNewsHnsw::load_fwd() {
  std::ifstream ifs;
  //
  char stream_buffer[1024 * 512];
  ifs.rdbuf()->pubsetbuf(stream_buffer, 1024 * 512);
  //
  ifs.open(data_path_ / HN_STORY_FILE);
  if (!ifs.is_open()) {
    spdlog::error("failure to open file {}", HN_STORY_FILE);
    return false;
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
  fwd_ptr_ = local_fwd_ptr;
  timer.end();
  ifs.close();
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