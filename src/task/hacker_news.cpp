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
#include "absl/time/time.h"

#include "storage/hn_hnsw.hpp"
#include "utils/executor.hpp"
#include "utils/guard.hpp"
#include "utils/ollama.hpp"

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
  uint32_t max_item_id;
  if (FLAGS_max_item_id > 0) {
    max_item_id = FLAGS_max_item_id;
  } else {
    max_item_id = HackerNewsApi::max_item_id();
  }
  if (max_item_id == 0) {
    spdlog::error("failure to get max item id");
    return;
  }
  uint32_t last_max_item_id = get_last_max_item_id();
  //
  auto next_version = generate_next_version();
  auto next_version_path = DEFAULT_ROOT_PATH / next_version;
  if (!exists(next_version_path)) {
    create_directory(next_version_path);
  }
  std::filesystem::path next_version_story_file = next_version_path / HN_STORY_FILE;
  std::ofstream ofs {next_version_story_file, std::ios::app};
  if (!ofs.is_open()) {
    spdlog::error("failure to open hn.json");
    return;
  }
  DeferGuard ifs_close_guard{[&ofs]() {
    ofs.close();
  }};
  //
  Executor tp{"fetch-hn-item", 1024, 5 * std::thread::hardware_concurrency()};
  DeferGuard tp_join_guard{[&tp]() {
    tp.join();
  }};
  //
  std::queue<std::pair<uint32_t, HackerNewItemPtr>> ready_item_q;
  std::mutex q_lock;
  //
  auto pf = std::async(std::launch::async, [max_item_id, last_max_item_id, &tp, &q_lock, &ready_item_q]() {
    for (uint32_t id = max_item_id; id > last_max_item_id; id--) {
      tp.async_execute([id, &q_lock, &ready_item_q]() {
        auto item_ptr = HackerNewsApi::item_info(id);
        if (item_ptr == nullptr) {
          spdlog::error("failure to fetch item item for {}", id);
        }
        std::lock_guard q_lock_guard{q_lock};
        ready_item_q.emplace(id, item_ptr);
      });
    }
  });
  uint32_t expected_id_cnt = max_item_id-last_max_item_id;
  uint32_t ready_id_cnt = 0;
  uint32_t valid_story_cnt = 0;
  while (ready_id_cnt < expected_id_cnt) {
    HackerNewItemPtr item_ptr;
    {
      while (ready_item_q.empty()) {
        std::this_thread::sleep_for(milliseconds(10));
      }
      std::lock_guard q_lock_guard{q_lock};
      auto& id2ptr = ready_item_q.front();
      item_ptr = id2ptr.second;
      ready_item_q.pop();
    }
    ready_id_cnt++;
    if (item_ptr == nullptr || item_ptr->dead || item_ptr->deleted ||
      item_ptr->type != "story" || item_ptr->title.empty() || item_ptr->url.empty()) {
      continue;
    }
    nlohmann::json item_info{};
    item_ptr->to_json(item_info);
    ofs << item_info.dump() << std::endl;
    valid_story_cnt++;
  }
  ofs.flush();
  pf.wait();
  //
  nlohmann::json story_meta {};
  story_meta["max_item_id"] = max_item_id;
  story_meta["valid_story_cnt"] = valid_story_cnt;
  story_meta["updated_at"] = std::chrono::duration_cast<milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  auto meta_s = story_meta.dump();
  //
  spdlog::info("finished to crawl hn, meta info: {}", meta_s);
  //
  auto next_version_story_meta_file = next_version_path / HN_STORY_META_FILE;
  std::ofstream story_meta_ofs {next_version_story_meta_file, std::ios::trunc};
  story_meta_ofs << meta_s;
  story_meta_ofs.flush();
  story_meta_ofs.close();
}

static uint32_t MODEL_BATCH_SIZE = 16;

void hn_story_to_emb(std::string version) {
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
  auto story_file_path = DEFAULT_ROOT_PATH / version / HN_STORY_FILE;
  if (!exists(story_file_path)) {
    spdlog::error("{} not exist!", story_file_path.string());
    return;
  }
  std::ifstream ifs {story_file_path};
  if (!ifs.is_open()) {
    spdlog::error("failure to open {}", story_file_path.string());
    return;
  }
  DeferGuard ifs_close_guard{[&ifs]() {
    ifs.close();
  }};
  auto story_emb_file_path = DEFAULT_ROOT_PATH / version / HN_STORY_EMB_FILE;
  std::ofstream ofs {story_emb_file_path, std::ios::trunc};
  if (!ofs.is_open()) {
    spdlog::error("failure to open {}", story_emb_file_path.string());
    return;
  }
  DeferGuard ofs_close_guard{[&ofs]() {
    ofs.close();
  }};
  //
  Executor tp{"conv-story2emb", 1024, std::thread::hardware_concurrency()};
  DeferGuard tp_join_guard{[&tp]() {
    tp.join();
  }};
  //
  std::queue<std::pair<uint32_t, std::vector<float>>> ready_emb_q;
  std::mutex q_lock;
  //
  auto process_one_batch = [](Executor& tp,
                              std::queue<std::pair<uint32_t, std::vector<float>>>& ready_emb_q,
                              std::mutex& q_lock,
                              Ollama& mxbai_embed_large_oll,
                              std::vector<nlohmann::json> one_batch) {
    std::vector<std::pair<uint32_t, std::string>> pairs;
    pairs.reserve(MODEL_BATCH_SIZE);
    std::transform(one_batch.begin(), one_batch.end(), std::back_inserter(pairs),
                   [](const nlohmann::json& j) -> std::pair<nlohmann::json, std::string> {
                     return {j["id"].get<uint32_t>(), j["title"].get<std::string>()};
                   });
    tp.async_execute([pairs, &mxbai_embed_large_oll, &ready_emb_q, &q_lock]() {
      std::vector<std::string> model_input;
      model_input.reserve(pairs.size());
      for (auto& [_, s] : pairs) {
        model_input.emplace_back(s);
      }
      Embeddings embs = mxbai_embed_large_oll.generate_embeddings(model_input);
      for (size_t idx = 0; idx < embs.size(); idx++) {
        std::lock_guard q_lock_guard{q_lock};
        ready_emb_q.emplace(pairs[idx].first, embs[idx]);
      }
    });
  };
  //
  std::atomic_uint32_t item_cnt{0};
  std::atomic_bool producer_finished{false};
  auto pf = std::async(std::launch::async, [&ifs, &tp, &ready_emb_q, &q_lock, &mxbai_embed_large_oll,
                         &process_one_batch, &producer_finished, &item_cnt]() {
                         std::vector<nlohmann::json> one_batch;
                         for (std::string line; std::getline(ifs, line);) {
                           auto j = nlohmann::json::parse(line);
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
        std::this_thread::sleep_for(milliseconds(10));
      }
      auto& [story_id, emb] = ready_emb_q.front();
      nlohmann::json j{};
      j["id"] = story_id;
      j["emb"] = emb;
      ofs << j.dump() << std::endl;
      std::lock_guard q_lock_guard{q_lock};
      ready_emb_q.pop();
      consume_cnt++;
      if (consume_cnt % 10000 == 0) {
        spdlog::info("generate emb cnt: {}", consume_cnt);
      }
    }
  });
  //
  pf.wait();
  cf.wait();
  ofs.flush();
  // store meta info
  do {
    auto meta_info = fmt::format(R"({{"model_name": "{}", "dim": {}, "cnt": {}}})",
                                 FLAGS_emb_model_name, dim, item_cnt.load());
    auto story_emb_meta_file_path = DEFAULT_ROOT_PATH / version / HN_STORY_EMB_META_FILE;
    std::ofstream meta_ofs {story_emb_meta_file_path};
    if (!meta_ofs.is_open()) {
      spdlog::error("failure to open meta file '{}', meta info: '{}'", story_emb_meta_file_path.string(), meta_info);
      break;
    }
    meta_ofs << meta_info << std::endl;
    meta_ofs.flush();
    meta_ofs.close();
  } while (false);
}

void versioned_hn_story_to_emb() {
  const auto versions = all_versions();
  std::vector<std::string> version_cands;
  for (const auto& v : versions) {
    auto story_emb_meta_file_path = DEFAULT_ROOT_PATH / v / HN_STORY_EMB_META_FILE;
    if (!exists(story_emb_meta_file_path)) {
      version_cands.emplace_back(v);
    }
  }
  if (version_cands.empty()) {
    return;
  }
  for (const auto& v : version_cands) {
    hn_story_to_emb(v);
  }
}

void find_similarity_by_brute_force() {
  std::string s = FLAGS_like_sentence;
  uint32_t top_k = std::min(FLAGS_topk, static_cast<uint32_t>(10000));
  if (s.empty()) {
    spdlog::warn("input sentence is empty!");
    return;
  }
  //
  Ollama oll_model{FLAGS_emb_model_name};
  if (!oll_model.is_model_serving()) {
    spdlog::error("please run model '{}' on ollama first", FLAGS_emb_model_name);
    return;
  }
  std::vector<std::string> model_input;
  model_input.emplace_back(s);
  Embeddings embs = oll_model.generate_embeddings(model_input);
  if (embs.size() != 1) {
    spdlog::error("failure to generate embedding for '{}' with model '{}'", s, FLAGS_emb_model_name);
    return;
  }
  const Embedding& query_emb = embs[0];
  auto cmp = [](nlohmann::json& l, nlohmann::json& r) {
    return l["score"].get<float>() > r["score"].get<float>();
  };
  std::priority_queue<nlohmann::json, std::vector<nlohmann::json>, decltype(cmp)> pq{cmp}; // 最小堆
  uint32_t cnt = 0;
  //
  auto versions = all_versions();
  std::vector<std::string> version_cands;
  for (const auto& v : versions) {
    if (std::filesystem::exists(DEFAULT_ROOT_PATH / v / HN_STORY_EMB_FILE)) {
      version_cands.emplace_back(v);
    }
  }
  for (const auto& v : version_cands) {
    auto p = DEFAULT_ROOT_PATH / v / HN_STORY_EMB_FILE;
    std::ifstream ifs{p};
    if (!ifs.is_open()) {
      spdlog::error("failure to open {}", p.string());
      return;
    }
    DeferGuard ifs_close_guard{[&ifs]() {
      ifs.close();
    }};
    std::string line;
    while (std::getline(ifs, line)) {
      auto jj = nlohmann::json::parse(line);
      auto cand_emb = jj["emb"].get<Embedding>();
      if (cand_emb.size() != query_emb.size()) {
        spdlog::error("dim not equal: {}={}", cand_emb.size(), query_emb.size());
        continue;
      }
      float sim = 0.0;
      for (size_t idx = 0; idx < cand_emb.size(); idx++) {
        sim += cand_emb[idx] * query_emb[idx];
      }
      jj["ann_score"] = sim;
      pq.push(jj);
      //
      if (pq.size() > top_k) {
        pq.pop();
      }
      cnt++;
    }
  }
  spdlog::info("similarity top {}:", top_k);
  while (!pq.empty()) {
    const auto& pq_j = pq.top();
    spdlog::info("item: {}, score: {}", pq_j["id"].get<uint32_t>(), pq_j["ann_score"].get<float>());
    pq.pop();
  }
}

void build_hnsw_index() {
  using namespace hnswlib;
  auto& hn_hnsw = HackNewsHnsw::singleton();
  hn_hnsw.data_path(DEFAULT_ROOT_PATH.string());
  hn_hnsw.load_meta();
  auto& meta = hn_hnsw.meta();
  if (meta.model_name.empty() || meta.dim == 0 || meta.cnt == 0) {
    return;
  }
  //
  std::filesystem::path emb_file_path = DEFAULT_ROOT_PATH / HN_STORY_EMB_FILE;
  if (!std::filesystem::exists(emb_file_path)) {
    spdlog::error("not exist emb file: {}", emb_file_path.string());
    return;
  }
  //
  std::ifstream emb_ifs;
  char stream_buffer[1024 * 512];
  emb_ifs.rdbuf()->pubsetbuf(stream_buffer, 1024 * 512);
  emb_ifs.open(emb_file_path);
  if (!emb_ifs.is_open()) {
    spdlog::error("failure to open file {}", emb_file_path.string());
    return;
  }
  DeferGuard emb_ifs_close_guard{[&emb_ifs]() {
    emb_ifs.close();
  }};
  //
  InnerProductSpace metric_space{meta.dim};
  int ef_construction = 100;
  std::shared_ptr<HierarchicalNSW<float>> hnsw_ptr = std::make_shared<HierarchicalNSW<float>>(&metric_space,
    meta.cnt, meta.dim, ef_construction);
  //
  uint32_t line_cnt = 0;
  std::atomic_uint32_t point_cnt = 0;
  //
  Executor tp{"build-hnsw-index"};
  for (std::string line; std::getline(emb_ifs, line);) {
    line_cnt++;
    tp.async_execute([line, &hnsw_ptr, &point_cnt]() {
      auto jj = nlohmann::json::parse(line);
      hnsw_ptr->addPoint(jj["emb"].get<Embedding>().data(), jj["id"].get<uint32_t>());
      ++point_cnt;
      if (point_cnt % 10000 == 0) {
        spdlog::info("{} points processed", point_cnt.load());
      }
    });
  }
  while (point_cnt.load() < line_cnt) {
    std::this_thread::sleep_for(milliseconds(50));
  }
  spdlog::info("hnsw points cnt: {}", point_cnt.load());
  hnsw_ptr->saveIndex((DEFAULT_ROOT_PATH / HN_STORY_HNSW_IDX_FILE).string());
  tp.join();
}

void find_similarity_by_hnsw() {
  using namespace hnswlib;
  //
  auto& hn_hnsw = HackNewsHnsw::singleton();
  hn_hnsw.data_path(DEFAULT_ROOT_PATH.string());
  hn_hnsw.load(); // !!!
  //
  auto& meta = hn_hnsw.meta();
  //
  Ollama oll_model{meta.model_name};
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
  for (auto& [item_ptr, sim] : qr) {
    spdlog::info("ANN item: id={}, title='{}', url='{}', score={}, sim={}",
      item_ptr->id, item_ptr->title, item_ptr->score, sim);
  }
}
} // namespace ling::task

using namespace ling::task;

int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  //
  auto cur_dir = std::filesystem::current_path();
  std::filesystem::path work_dir{FLAGS_wd};
  if (!std::filesystem::exists(work_dir)) {
    std::filesystem::create_directories(work_dir);
  }
  std::filesystem::current_path(work_dir);
  spdlog::info("change {} to {}", cur_dir.string(), work_dir.string());
  //
  switch (FLAGS_sub_task) {
    case 0:
      crawl_hn();
      break;
    case 1:
      versioned_hn_story_to_emb();
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