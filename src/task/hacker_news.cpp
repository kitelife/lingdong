/*
 * 抓取 hacker news 上的帖子，经分词 & 向量化，创建倒排索引 & ANN 索引 & FLAT 向量索引，提供检索能力
 * */

#include <vector>
#include <future>
#include <queue>

#include "cpr/cpr.h"
#include "nlohmann/json.hpp"
#include "gflags/gflags.h"

#include "utils/executor.hpp"
#include "utils/guard.hpp"

namespace ling::task {

DEFINE_uint32(max_item_id, 0, "max limit for item id");

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

  void from_json(nlohmann::json& j);
  void to_json(nlohmann::json& j);
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
  uint32_t max_item_id();
  HackerNewItemPtr item_info(uint32_t id);

private:
  static std::string ENDPOINT_BASE;
};

std::string HackerNewsApi::ENDPOINT_BASE = "https://hacker-news.firebaseio.com/v0";

uint32_t HackerNewsApi::max_item_id() {
  auto url = ENDPOINT_BASE + "/maxitem.json";
  auto r = cpr::Get(cpr::Url{url},cpr::Parameters{{"print", "pretty"}},
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
  std::ofstream ofs {"./hn.json", std::ios::app};
  if (!ofs.is_open()) {
    spdlog::error("failure to open hn.json");
    return;
  }
  DeferGuard ifs_close_guard {[&ofs]() {
    ofs.close();
  }};
  //
  Executor tp {"worker-fetch-hn-item", 1024, 5 * std::thread::hardware_concurrency()};
  DeferGuard tp_join_guard {[&tp]() {
    tp.join();
  }};
  //
  std::queue<std::pair<uint32_t, HackerNewItemPtr>> ready_item_q;
  std::mutex q_lock;
  //
  auto pf = std::async(std::launch::async, [max_item_id, &api, &tp, &q_lock, &ready_item_q]() {
      for (uint32_t id=max_item_id; id > 0; id--) {
        tp.async_execute([id, &api, &q_lock, &ready_item_q]() {
            auto item_ptr = api.item_info(id);
            if (item_ptr == nullptr) {
              spdlog::error("failure to fetch item item for {}", id);
            }
            std::lock_guard q_lock_guard {q_lock};
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
      std::lock_guard q_lock_guard {q_lock};
      auto& id2ptr = ready_item_q.front();
      item_ptr = id2ptr.second;
      ready_item_q.pop();
    }
    ready_id_cnt++;
    if (item_ptr == nullptr) {
      continue;
    }
    nlohmann::json item_info {};
    item_ptr->to_json(item_info);
    ofs << item_info.dump() << std::endl;
  }
  ofs.flush();
  pf.wait();
}

} // namespace ling::task


int main(int argc, char** argv) {
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  spdlog::set_level(spdlog::level::debug);
  //
  ling::task::crawl_hn();
  return 0;
}