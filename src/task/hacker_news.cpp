/*
 * 抓取 hacker news 上的帖子，经分词 & 向量化，创建倒排索引 & ANN 索引 & FLAT 向量索引，提供检索能力
 * */

#include <vector>
#include <future>
#include <queue>
#include <unordered_map>

#include "cpr/cpr.h"
#include "nlohmann/json.hpp"
#include "gflags/gflags.h"
#include "fmt/format.h"
#include "hnswlib/hnswlib.h"

#include "storage/hn_hnsw.hpp"
#include "utils/executor.hpp"
#include "utils/guard.hpp"
#include "utils/ollama.hpp"
#include "utils/perf.hpp"

namespace ling::task {

DEFINE_string(wd, "../../data/hn", "working dir");
DEFINE_uint32(sub_task, 0, "sub task type");
DEFINE_uint32(max_item_id, 0, "max limit for item id");
DEFINE_string(emb_model_name, "mxbai-embed-large:latest", "embedding model name");
DEFINE_string(like_sentence, "", "find item like this sentence");
DEFINE_uint32(topk, 10, "similarity top k");

using namespace ling::utils;
using namespace ling::storage;

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
  // test, get dim
  std::vector<std::string> test_input;
  test_input.emplace_back("简单测试一下，拿向量维度信息");
  Embeddings embs = mxbai_embed_large_oll.generate_embeddings(test_input);
  if (embs.size() != test_input.size()) {
    spdlog::error("failure to test model '{}'", FLAGS_emb_model_name);
    return;
  }
  uint32_t dim = embs[0].size();
  //
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
      j["url"] = item_ptr->url;
      j["score"] = item_ptr->score;
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
  // store meta info
  do {
    auto meta_info = fmt::format(R"({"model_name": "{}", "dim": "{}", "cnt": {}})",
                                 FLAGS_emb_model_name, dim, item_cnt.load());
    std::ofstream meta_ofs {HN_STORY_EMB_META_FILE};
    if (!meta_ofs.is_open()) {
      spdlog::error("failure to open meta file '{}', meta info: '{}'", HN_STORY_EMB_META_FILE, meta_info);
      break;
    }
    meta_ofs << meta_info << std::endl;
    meta_ofs.flush();
    meta_ofs.close();
  } while (false);
}

nlohmann::json fetch_emb_meta_info() {
  std::filesystem::path meta_file_path {HN_STORY_EMB_META_FILE};
  if (!std::filesystem::exists(meta_file_path)) {
    spdlog::error("not exist meta file {}", meta_file_path.string());
    return {};
  }
  std::ifstream meta_ifs {meta_file_path};
  if (!meta_ifs.is_open()) {
    spdlog::error("failure to open meta file '{}'", HN_STORY_EMB_META_FILE);
    return {};
  }
  DeferGuard ifs_close_guard {[&meta_ifs]() {
    meta_ifs.close();
  }};
  std::string line;
  std::getline(meta_ifs, line);
  return nlohmann::json::parse(line);
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
  auto j = fetch_emb_meta_info();
  std::string model_name = j["model_name"].get<std::string>();
  spdlog::info("embedding model: {}, dim: {}, cnt: {}",
               model_name, j["dim"].get<uint32_t>(), j["cnt"].get<uint32_t>());
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
    spdlog::error("failure to generate embedding for '{}' with model '{}'", s, model_name);
    return;
  }
  const Embedding& query_emb = embs[0];
  auto cmp = [](nlohmann::json& l, nlohmann::json& r) {
    return l["score"].get<float>() > r["score"].get<float>();
  };
  std::priority_queue<nlohmann::json, std::vector<nlohmann::json>, decltype(cmp)> pq {cmp}; // 最小堆
  uint32_t cnt = 0;
  std::string line;
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

void build_hnsw_index() {
  using namespace hnswlib;
  auto& hn_hnsw = ling::storage::HackNewsHnsw::singleton();
  std::filesystem::path root_path {"./"};
  hn_hnsw.data_path(root_path.string());
  hn_hnsw.load_meta();
  auto& meta = hn_hnsw.meta();
  if (meta.model_name.empty() || meta.dim == 0 || meta.cnt == 0) {
    return;
  }
  //
  std::filesystem::path emb_file_path {root_path / HN_STORY_EMB_FILE};
  if (!std::filesystem::exists(emb_file_path)) {
    spdlog::error("not exist emb file: {}", HN_STORY_EMB_FILE);
    return;
  }
  std::ifstream emb_ifs {emb_file_path};
  if (!emb_ifs.is_open()) {
    spdlog::error("failure to open emb file {}", HN_STORY_EMB_FILE);
  }
  DeferGuard emb_ifs_close_guard {[&emb_ifs]() { emb_ifs.close(); }};
  //
  InnerProductSpace metric_space {meta.dim};
  int ef_construction = 200;
  std::shared_ptr<HierarchicalNSW<float>> hnsw_ptr = std::make_shared<HierarchicalNSW<float>>(&metric_space,
      meta.cnt, meta.dim, ef_construction);
  std::string line;
  uint32_t point_cnt = 0;
  while (std::getline(emb_ifs, line)) {
    auto jj = nlohmann::json::parse(line);
    hnsw_ptr->addPoint(jj["emb"].get<Embedding>().data(), jj["id"].get<uint32_t>());
    point_cnt++;
    if (point_cnt % 10000 == 0) {
      spdlog::info("{} points processed", point_cnt);
    }
  }
  hnsw_ptr->saveIndex((root_path / HN_STORY_HNSW_IDX_FILE).string());
}

void find_similarity_by_hnsw() {
  using namespace hnswlib;
  //
  auto& hn_hnsw = ling::storage::HackNewsHnsw::singleton();
  std::filesystem::path root_path {"./"};
  hn_hnsw.data_path(root_path.string());
  hn_hnsw.load(); // !!!
  //
  auto& meta = hn_hnsw.meta();
  //
  Ollama oll_model {meta.model_name};
  if (!oll_model.is_model_serving()) {
    spdlog::error("please run model '{}' on ollama first", meta.model_name);
    return;
  }
  std::vector<std::string> model_input;
  model_input.emplace_back(FLAGS_like_sentence);
  Embeddings embs = oll_model.generate_embeddings(model_input);
  if (embs.size() != 1) {
    spdlog::error("failure to generate embedding for '{}' with model '{}'", FLAGS_like_sentence, meta.model_name);
    return;
  }
  Embedding& query_emb = embs[0];
  if (query_emb.size() != meta.dim) {
    spdlog::error("dim not equal: {} != {}", query_emb.size(), meta.dim);
  }
  auto qr = hn_hnsw.search(query_emb);
  for (auto& [item_ptr, score] : qr) {
    spdlog::info("ANN item: id={}, title='{}', score={}", item_ptr->id, item_ptr->title, score);
  }
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
      build_hnsw_index();
      break;
    case 4:
      find_similarity_by_hnsw();
      break;
    default:
      break;
  }
  return 0;
}