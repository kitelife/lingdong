/*
 * 统计访问用户相关数据
 * */

#include <tsl/robin_set.h>

#include "spdlog/spdlog.h"
#include "gflags/gflags.h"
#include "tsl/robin_map.h"
#include "tsl/robin_set.h"
#include "absl/strings/str_split.h"
#include "nlohmann/json.hpp"

#include "utils/guard.hpp"
#include "utils/mmdb.hpp"
#include "storage/local_sqlite.h"

DEFINE_string(q, "139.226.50.188", "what ip to query");
DEFINE_bool(vs, false, "to run viewer stat");
DEFINE_string(db, "", "sqlite db file path");

using ling::utils::MaxMindGeoLite2Db;
using ling::storage::LocalSqlite;

void query_ip_geo(const MaxMindGeoLite2Db& mmdb) {
  auto r = mmdb.query_by_ip(FLAGS_q);
  spdlog::info("ip: {}, geo: {}", FLAGS_q, r->to_string());
  spdlog::info("ip: {}, geo: {}", FLAGS_q, r->to_json_string());
}

bool sqlite_table_exists(const LocalSqlite& db, const std::string& table_name) {
  auto qr = db.query(fmt::format("SELECT COUNT(*) FROM sqlite_schema WHERE type='table' AND name='{}'", table_name));
  if (qr->rows.empty()) {
    return false;
  }
  auto [k, v] = qr->rows[0][0];
  return std::any_cast<int64_t>(v) > 0;
}

tsl::robin_map<std::string, uint32_t> stat_viewer_browser(const LocalSqlite& db) {
  auto q = "SELECT user_agent FROM access_log";
  const auto qr = db.query(q);
  tsl::robin_map<std::string, uint32_t> stats = {
    {"chrome", 0},
    {"firefox", 0},
    {"safari", 0},
    {"edge", 0},
    {"curl", 0},
    {"unknown", 0},
  };
  for (const auto& row : qr->rows) {
    auto& [_, v] = row[0];
    auto sv = std::any_cast<std::string>(v);
    // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Headers/User-Agent
    if (sv.find("Safari") == std::string::npos) {
      if (sv.find("Chrome")) {
        stats["chrome"] += 1;
      } else if (sv.find("Edg") == std::string::npos) {
        stats["edge"] += 1;
      } else {
        stats["safari"] += 1;
      }
    } else if (sv.find("Firefox") == std::string::npos) {
      stats["firefox"] += 1;
    } else if (sv.find("curl") != std::string::npos) {
      stats["curl"] += 1;
    } else {
      stats["unknown"] += 1;
    }
  }
  return stats;
}

tsl::robin_map<std::string, uint32_t> stat_viewer_geo(const LocalSqlite& db, const MaxMindGeoLite2Db& mmdb) {
  auto q = "SELECT peer FROM access_log";
  const auto qr = db.query(q);
  tsl::robin_map<std::string, uint32_t> stats = {
    {"uv", 0},
  };
  stats["pv"] = qr->rows.size();
  tsl::robin_set<std::string> viewers;
  for (const auto& row : qr->rows) {
    auto& [_, v] = row[0];
    auto sv = std::any_cast<std::string>(v);
    if (sv.empty()) {
      continue;
    }
    //
    std::vector<std::string> parts = absl::StrSplit(sv, ':');
    auto& ip = parts[0];
    if (ip.size() < 7 || ip.size() > 15 || (ip[0] < '1' || ip[0] > '9') || (ip[ip.size() - 1] < '0' || ip[ip.size() - 1] > '9')) {
      spdlog::warn("illegal ip: {}", ip);
      continue;
    }
    if (!viewers.contains(ip)) {
      stats["uv"] += 1;
      viewers.insert(ip);
    }
    //
    ling::utils::MmdbRecordPtr mrp = mmdb.query_by_ip(ip);
    if (!mrp->is_valid) {
      continue;
    }
    if (!mrp->continent.empty()) {
      auto k = "c1_" + mrp->continent;
      stats[k] = stats[k] + 1;
    }
    if (!mrp->country.empty()) {
      auto k = "c2_" + mrp->country;
      stats[k] = stats[k] + 1;
    }
    if (!mrp->city.empty()) {
      auto k = "c3_" + mrp->city;
      stats[k] = stats[k] + 1;
    }
  }
  return stats;
}

int viewer_stat(const MaxMindGeoLite2Db& mmdb) {
  LocalSqlite db;
  if (!db.open(FLAGS_db, {})) {
    return -1;
  }
  if (!sqlite_table_exists(db, "access_log")) {
    return -1;
  }
  auto viewer_browser_stats = stat_viewer_browser(db);
  nlohmann::json vbj;
  for (const auto& [k, v] : viewer_browser_stats) {
    vbj[k] = v;
  }
  spdlog::info("browser stats: {}", vbj.dump(2));
  //
  auto viewer_geo_stats = stat_viewer_geo(db, mmdb);
  nlohmann::json vgj;
  for (const auto& [k, v] : viewer_geo_stats) {
    vgj[k] = v;
  }
  spdlog::info("geo stats: {}", vgj.dump(2));
  return 0;
}

int main() {
  MaxMindGeoLite2Db mmdb;
  if (!mmdb.open()) {
    spdlog::error("failure to open mmdb");
    return -1;
  }
  ling::utils::DeferGuard close_guard {[&mmdb]() {
    mmdb.close();
  }};
  //
  if (!FLAGS_q.empty()) {
    query_ip_geo(mmdb);
    return 0;
  }
  if (!FLAGS_vs) {
    return 0;
  }
  return viewer_stat(mmdb);
}