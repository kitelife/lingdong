/*
 * 抓取 hacker news 上的帖子，经分词 & 向量化，创建倒排索引 & ANN 索引 & FLAT 向量索引，提供检索能力
 * */

#include <vector>
#include <future>
#include <queue>

#include "cpr/cpr.h"
#include "nlohmann/json.hpp"
#include "gflags/gflags.h"
#include "fmt/format.h"

#include "utils/executor.hpp"
#include "utils/guard.hpp"
#include "utils/ollama.hpp"

namespace ling::task {

DEFINE_string(wd, "../../hn", "working dir");
DEFINE_uint32(sub_task, 0, "sub task type");
DEFINE_uint32(max_item_id, 0, "max limit for item id");
DEFINE_string(emb_model_name, "mxbai-embed-large:latest", "embedding model name");
DEFINE_string(like_sentence, "", "find item like this sentence");
DEFINE_uint32(topk, 10, "similarity top k");

static std::string HN_ITEM_FILE = "hn.json";
static std::string HN_STORY_EMB_FILE = "hn_story_emb.json";

using namespace ling::utils;

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

class HackerNewsApi {
public:
  HackerNewsApi() = default;

  static uint32_t max_item_id();
  static HackerNewItemPtr item_info(uint32_t id);

private:
  static std::string ENDPOINT_BASE;
};

std::string HackerNewsApi::ENDPOINT_BASE = "https://hacker-news.firebaseio.com/v0";

uint32_t HackerNewsApi::max_item_id() {
  auto url = ENDPOINT_BASE + "/maxitem.json";
  auto r = cpr::Get(cpr::Url{url}, cpr::Parameters{{"print", "pretty"}},
                    cpr::ConnectTimeout{std::chrono::seconds(5)}, cpr::Timeout{std::chrono::seconds(10)});
  return r.status_code == 200 ? static_cast<uint32_t>(std::stoi(r.text)) : 0;
}

HackerNewItemPtr HackerNewsApi::item_info(uint32_t id) {
  auto url = fmt::format("{}/item/{}.json", ENDPOINT_BASE, id);
  auto r = cpr::Get(cpr::Url{url}, cpr::Parameters{{"print", "pretty"}},
                    cpr::ConnectTimeout{std::chrono::seconds(5)}, cpr::Timeout{std::chrono::seconds(10)});
  if (r.error.code != cpr::ErrorCode::OK || r.status_code != 200) {
    spdlog::error("failure to fetch item {} info, err code: {}, err msg: {}, resp code: {}, text: {}", id,
                  r.status_code, r.text, cpr::error_code_to_string_mapping.at(r.error.code), r.error.message);
    return nullptr;
  }
  spdlog::info("success to fetch item {} info: {}", id, r.text);
  HackerNewItemPtr item_ptr = std::make_shared<HackerNewItem>();
  nlohmann::json j = nlohmann::json::parse(r.text);
  item_ptr->from_json(j);
  return item_ptr;
}

void crawl_hn() {
  HackerNewsApi api;
  //
  uint32_t max_item_id;
  if (FLAGS_max_item_id > 0) {
    max_item_id = FLAGS_max_item_id;
  } else {
    max_item_id = api.max_item_id();
  }
  if (max_item_id == 0) {
    spdlog::error("failure to get max item id");
    return;
  }
  std::ofstream ofs{HN_ITEM_FILE, std::ios::app};
  if (!ofs.is_open()) {
    spdlog::error("failure to open hn.json");
    return;
  }
  DeferGuard ifs_close_guard{[&ofs]() {
      ofs.close();
  }};
  //
  Executor tp{"worker-fetch-hn-item", 1024, 5 * std::thread::hardware_concurrency()};
  DeferGuard tp_join_guard{[&tp]() {
      tp.join();
  }};
  //
  std::queue<std::pair<uint32_t, HackerNewItemPtr>> ready_item_q;
  std::mutex q_lock;
  //
  auto pf = std::async(std::launch::async, [max_item_id, &api, &tp, &q_lock, &ready_item_q]() {
      for (uint32_t id = max_item_id; id > 0; id--) {
        tp.async_execute([id, &api, &q_lock, &ready_item_q]() {
            auto item_ptr = api.item_info(id);
            if (item_ptr == nullptr) {
              spdlog::error("failure to fetch item item for {}", id);
            }
            std::lock_guard q_lock_guard{q_lock};
            ready_item_q.emplace(id, item_ptr);
        });
      }
  });
  uint32_t ready_id_cnt = 0;
  while (ready_id_cnt < max_item_id) {
    HackerNewItemPtr item_ptr;
    {
      while (ready_item_q.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      std::lock_guard q_lock_guard{q_lock};
      auto &id2ptr = ready_item_q.front();
      item_ptr = id2ptr.second;
      ready_item_q.pop();
    }
    ready_id_cnt++;
    if (item_ptr == nullptr) {
      continue;
    }
    nlohmann::json item_info{};
    item_ptr->to_json(item_info);
    ofs << item_info.dump() << std::endl;
  }
  ofs.flush();
  pf.wait();
}

static uint32_t MODEL_BATCH_SIZE = 16;

void hn_story_to_emb() {
  Ollama mxbai_embed_large_oll{FLAGS_emb_model_name};
  if (!mxbai_embed_large_oll.is_model_serving()) {
    spdlog::error("please run ollama service first");
    return;
  }
  if (!std::filesystem::exists(HN_ITEM_FILE)) {
    spdlog::error("{} not exist!", HN_ITEM_FILE);
    return;
  }
  std::ifstream ifs{HN_ITEM_FILE};
  if (!ifs.is_open()) {
    spdlog::error("failure to open {}", HN_ITEM_FILE);
    return;
  }
  DeferGuard ifs_close_guard{[&ifs]() {
      ifs.close();
  }};
  std::ofstream ofs{HN_STORY_EMB_FILE};
  if (!ofs.is_open()) {
    spdlog::error("failure to open {}", HN_STORY_EMB_FILE);
    return;
  }
  DeferGuard ofs_close_guard{[&ofs]() {
      ofs.close();
  }};
  //
  Executor tp{"worker-conv-story2emb", 1024, std::thread::hardware_concurrency()};
  DeferGuard tp_join_guard{[&tp]() {
      tp.join();
  }};
  //
  std::queue<std::pair<HackerNewItemPtr, std::vector<float>>> ready_emb_q;
  std::mutex q_lock;
  //
  ofs << fmt::format(R"({"model_name": "{}"})", FLAGS_emb_model_name) << std::endl;
  auto process_one_batch = [](Executor &tp,
                              std::queue<std::pair<HackerNewItemPtr, std::vector<float>>> &ready_emb_q,
                              std::mutex &q_lock, Ollama &mxbai_embed_large_oll,
                              std::vector<nlohmann::json> one_batch) {
      std::vector<std::pair<nlohmann::json, std::string>> pairs;
      pairs.reserve(MODEL_BATCH_SIZE);
      std::transform(one_batch.begin(), one_batch.end(), std::back_inserter(pairs),
                     [](const nlohmann::json &j) -> std::pair<nlohmann::json, std::string> {
                         auto text = j["text"].get<std::string>();
                         auto title = j["title"].get<std::string>();
                         if (text.empty()) {
                           return {j, title};
                         }
                         return {j, title += ";" + text};
                     });
      tp.async_execute([pairs, &mxbai_embed_large_oll, &ready_emb_q, &q_lock]() {
          std::vector<std::string> model_input;
          model_input.reserve(pairs.size());
          for (auto &[_, s]: pairs) {
            model_input.emplace_back(s);
          }
          Embeddings embs = mxbai_embed_large_oll.generate_embeddings(model_input);
          for (size_t idx = 0; idx < embs.size(); idx++) {
            HackerNewItemPtr item_ptr = std::make_shared<HackerNewItem>();
            item_ptr->from_json(const_cast<nlohmann::json &>(pairs[idx].first));
            std::lock_guard q_lock_guard{q_lock};
            ready_emb_q.emplace(item_ptr, embs[idx]);
          }
      });
  };
  //
  std::atomic_uint32_t item_cnt {0};
  std::atomic_bool producer_finished {false};
  auto pf = std::async(std::launch::async, [&ifs, &tp, &ready_emb_q, &q_lock,
      &mxbai_embed_large_oll, &process_one_batch, &producer_finished, &item_cnt]() {
      std::vector<nlohmann::json> one_batch;
      for (std::string line; std::getline(ifs, line);) {
        auto j = nlohmann::json::parse(line);
        if (j["type"].get<std::string>() != "story" || j["dead"].get<bool>() ||
            j["deleted"].get<bool>()) {
          continue;
        }
        one_batch.emplace_back(j);
        if (one_batch.size() < MODEL_BATCH_SIZE) {
          continue;
        }
        process_one_batch(tp, ready_emb_q, q_lock, mxbai_embed_large_oll, one_batch);
        item_cnt.fetch_add(one_batch.size()); //
        one_batch = {};
      }
      if (!one_batch.empty()) {
        process_one_batch(tp, ready_emb_q, q_lock, mxbai_embed_large_oll, one_batch);
        item_cnt.fetch_add(one_batch.size());
      }
      producer_finished = true;
  });
  auto cf = std::async(std::launch::async, [&producer_finished, &item_cnt, &ready_emb_q, &q_lock, &ofs]() {
    uint32_t consume_cnt = 0;
    while (!producer_finished || consume_cnt < item_cnt) {
      while (ready_emb_q.empty()) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
      auto& [item_ptr, emb] = ready_emb_q.front();
      nlohmann::json j {};
      j["id"] = item_ptr->id;
      j["title"] = item_ptr->title;
      j["text"] = item_ptr->text;
      j["emb"] = emb;
      ofs << j.dump() << std::endl;
      std::lock_guard q_lock_guard {q_lock};
      ready_emb_q.pop();
      consume_cnt++;
    }
  });
  //
  pf.wait();
  cf.wait();
  ofs.flush();
}

void find_similarity_by_brute_force() {
  std::string s = FLAGS_like_sentence;
  uint32_t top_k = std::min(FLAGS_topk, static_cast<uint32_t>(10000));
  if (s.empty()) {
    spdlog::warn("input sentence is empty!");
    return;
  }
  std::ifstream ifs {HN_STORY_EMB_FILE};
  if (!ifs.is_open()) {
    spdlog::error("failure to open {}", HN_STORY_EMB_FILE);
    return;
  }
  DeferGuard ifs_close_guard {[&ifs]() {
    ifs.close();
  }};
  std::string line;
  std::getline(ifs, line);
  nlohmann::json j = nlohmann::json::parse(line);
  if (!j.contains("model_name")) {
    spdlog::error("illegal {}", HN_STORY_EMB_FILE);
    return;
  }
  std::string model_name = j["model_name"].get<std::string>();
  spdlog::info("embedding model: {}", model_name);
  //
  Ollama oll_model {model_name};
  if (!oll_model.is_model_serving()) {
    spdlog::error("please run model '{}' on ollama first", model_name);
    return;
  }
  std::vector<std::string> model_input;
  model_input.emplace_back(s);
  Embeddings embs = oll_model.generate_embeddings(model_input);
  if (embs.size() != 1) {
    spdlog::error("failure to generate embedding for '{}'", model_name);
    return;
  }
  const Embedding& query_emb = embs[0];
  auto cmp = [](nlohmann::json& l, nlohmann::json& r) {
    return l["score"].get<float>() > r["score"].get<float>();
  };
  std::priority_queue<nlohmann::json, std::vector<nlohmann::json>, decltype(cmp)> pq {cmp}; // 最小堆
  uint32_t cnt = 0;
  while (std::getline(ifs, line)) {
    auto jj = nlohmann::json::parse(line);
    Embedding cand_emb = jj["emb"].get<Embedding>();
    if (cand_emb.size() != query_emb.size()) {
      spdlog::error("dim not equal: {}={}", cand_emb.size(), query_emb.size());
      continue;
    }
    float sim = 0.0;
    for (size_t idx=0; idx<cand_emb.size(); idx++) {
      sim += cand_emb[idx] * query_emb[idx];
    }
    jj["score"] = sim;
    pq.push(jj);
    //
    if (pq.size() > top_k) {
      pq.pop();
    }
    cnt++;
  }
  spdlog::info("similarity top {}:", top_k);
  while (!pq.empty()) {
    const auto& pq_j = pq.top();
    spdlog::info("item: {}, title: '{}', text: '{}', score: {}", pq_j["id"].get<uint32_t>(),
        pq_j["title"].get<std::string>(), pq_j["text"].get<std::string>(), pq_j["score"].get<float>());
    pq.pop();
  }
}

void find_similarity_by_hnsw() {

}

} // namespace ling::task

using namespace ling::task;

int main(int argc, char **argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  //
  auto cur_dir = std::filesystem::current_path();
  std::filesystem::path work_dir {FLAGS_wd};
  if (!std::filesystem::exists(work_dir)) {
    std::filesystem::create_directories(work_dir);
  }
  std::filesystem::current_path(work_dir);
  spdlog::info("change {} to {}", cur_dir.string(), work_dir.string());
  //
  switch (ling::task::FLAGS_sub_task) {
    case 0:
      crawl_hn();
      break;
    case 1:
      hn_story_to_emb();
      break;
    case 2:
      find_similarity_by_brute_force();
      break;
    case 3:
      find_similarity_by_hnsw();
      break;
    default:
      break;
  }
  return 0;
}