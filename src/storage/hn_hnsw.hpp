#pragma once

#include <filesystem>
#include <memory>

#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"
#include "hnswlib/hnswlib.h"

#include "utils/helper.hpp"
#include "utils/guard.hpp"
#include "utils/perf.hpp"

namespace ling::storage {

using namespace ling::utils;
using namespace hnswlib;

static std::string HN_ITEM_FILE = "hn.json";
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
  std::string text; // The comment, story or poll text. HTML.
  bool dead; // true if the item is dead.
  uint32_t parent; // The comment's parent: either another comment or the relevant story.
  uint32_t poll; // The pollopt's associated poll.
  std::vector<uint32_t> kids; // The ids of the item's comments, in ranked display order.
  std::string url; // The URL of the story.
  int score; // The story's score, or the votes for a pollopt.
  std::string title; // The title of the story, poll or job. HTML.
  std::vector<uint32_t> parts; // A list of related pollopts, in display order.
  uint32_t descendants; // In the case of stories or polls, the total comment count.

  void from_json(nlohmann::json &j);

  void to_json(nlohmann::json &j);
};

using HackerNewItemPtr = std::shared_ptr<HackerNewItem>;

void HackerNewItem::from_json(nlohmann::json &j) {
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
  if (j.contains("score")) {
    score = j["score"].get<int>();
  }
  if (j.contains("deleted")) {
    deleted = j["deleted"].get<bool>();
  }
  if (j.contains("text")) {
    text = j["text"].get<std::string>();
  }
  if (j.contains("dead")) {
    dead = j["dead"].get<bool>();
  }
  if (j.contains("parent")) {
    parent = j["parent"].get<uint32_t>();
  }
  if (j.contains("poll")) {
    poll = j["poll"].get<uint32_t>();
  }
  if (j.contains("kids")) {
    kids = j["kids"].get<std::vector<uint32_t>>();
  }
  if (j.contains("title")) {
    title = j["title"].get<std::string>();
  }
  if (j.contains("parts")) {
    parts = j["parts"].get<std::vector<uint32_t>>();
  }
  if (j.contains("descendants")) {
    descendants = j["descendants"].get<uint32_t>();
  }
}

void HackerNewItem::to_json(nlohmann::json &j) {
  j["id"] = id;
  j["deleted"] = deleted;
  j["type"] = type;
  j["by"] = by;
  j["time"] = time;
  j["text"] = text;
  j["dead"] = dead;
  j["parent"] = parent;
  j["poll"] = poll;
  j["kids"] = kids;
  j["url"] = url;
  j["score"] = score;
  j["title"] = title;
  j["parts"] = parts;
  j["descendants"] = descendants;
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
      cnt = j["dim"].get<uint32_t>();
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
  void data_path(std::string p) {
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
  std::unordered_map<uint32_t, HackerNewItemPtr> fwd_;
  std::shared_ptr<HierarchicalNSW<float>> hnsw_ptr_;
  std::atomic_bool loaded_ {false};
  //
  bool load_hnsw();
};

bool HackNewsHnsw::load_meta() {
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

bool HackNewsHnsw::load_fwd() {
  std::ifstream ifs {data_path_ / HN_STORY_EMB_FILE};
  if (!ifs.is_open()) {
    spdlog::error("failure to open file {}", HN_STORY_EMB_FILE);
    return false;
  }
  ScopedTimer timer {"load_fwd"};
  for (std::string line; std::getline(ifs, line);) {
    nlohmann::json j = nlohmann::json::parse(line);
    auto item_ptr = std::make_shared<HackerNewItem>();
    item_ptr->from_json(j);
    fwd_[item_ptr->id] = item_ptr;
  }
  ifs.close();
  return true;
}

bool HackNewsHnsw::load_hnsw() {
  std::filesystem::path hn_hnsw_idx_path { data_path_/ HN_STORY_HNSW_IDX_FILE};
  if (!std::filesystem::exists(hn_hnsw_idx_path)) {
    spdlog::error("hnsw idx file '{}' not exist", HN_STORY_HNSW_IDX_FILE);
    return false;
  }
  ScopedTimer timer {"load_hnsw"};
  InnerProductSpace metric_space {meta_.dim};
  hnsw_ptr_ = std::make_shared<HierarchicalNSW<float>>(&metric_space, hn_hnsw_idx_path.string());
  return true;
}

bool HackNewsHnsw::load() {
  auto status = load_meta() && load_fwd() && load_hnsw();
  loaded_ = status;
  return status;
}

std::vector<std::pair<HackerNewItemPtr, float>> HackNewsHnsw::search(Embedding query_emb, uint32_t top_k) {
  std::vector<std::pair<HackerNewItemPtr, float>> query_result;
  query_result.reserve(top_k);
  //
  auto result_pq = hnsw_ptr_->searchKnn(query_emb.data(), top_k);
  //
  while (!result_pq.empty()) {
    auto& r = result_pq.top();
    auto id = r.second;
    if (fwd_.count(id) == 0) {
      spdlog::warn("result {} has no fwd idx, score: {}", id, r.first);
    } else {
      auto& hn_item = fwd_[id];
      query_result.emplace_back(hn_item, r.first);
    }
    result_pq.pop();
  }
  return query_result;
}

}