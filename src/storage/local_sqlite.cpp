//
// Created by xiayf on 2025/8/8.
//

#include "local_sqlite.h"

#include <spdlog/spdlog.h>
#include <absl/strings/str_split.h>

namespace ling::storage {

bool LocalSqlite::open(const std::string& db_file_path, const std::string& init_sql) {
  if (is_open_.load()) {
    spdlog::info("DB {} has open!", db_file_path_);
    return true;
  }
  std::lock_guard guard {db_lock_};
  if (db_file_path.empty()) {
    db_file_path_ = "ling.db";
  } else {
    db_file_path_ = db_file_path;
  }
  db_ptr_ = std::make_shared<SQLite::Database>(db_file_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  if (!init_sql.empty()) {
    auto sql_vec = absl::StrSplit(init_sql, ';', absl::SkipEmpty());
    for (const auto& sql : sql_vec) {
      try {
        db_ptr_->exec(init_sql);
      } catch (SQLite::Exception& e) {
        spdlog::error("failed to exec init sql: {}, exception: {}", init_sql,
          fmt::format("code={}, msg={}", e.getErrorCode(), e.getErrorStr()));
        return false;
      }
    }
  }
  is_open_ = true;
  return true;
}

int LocalSqlite::exec(const std::string& sql) {
  if (!is_open_) {
    spdlog::error("DB not open!");
    return -1;
  }
  return db_ptr_->exec(sql);
}

QueryResultPtr LocalSqlite::query(const std::string& sql) {
  if (!is_open_) {
    spdlog::error("DB not open!");
    return nullptr;
  }
  SQLite::Statement query(*db_ptr_, sql);
  QueryResultPtr qr = std::make_shared<QueryResult>();
  //
  while (query.executeStep()) {
    int col_cnt = query.getColumnCount();
    std::vector<SQLite::Column> row;
    row.reserve(col_cnt);
    int col_idx = 0;
    while (col_idx < col_cnt) {
      row.push_back(query.getColumn(col_idx));
      col_idx++;
    }
    qr->rows.push_back(row);
  }
  return qr;
}


}