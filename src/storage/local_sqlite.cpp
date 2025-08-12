//
// Created by xiayf on 2025/8/8.
//

#include "local_sqlite.h"

#include <spdlog/spdlog.h>
#include <absl/strings/str_split.h>

namespace ling::storage {

bool LocalSqlite::open(const std::string& db_file_path, const std::vector<std::string>& init_sql) {
  std::lock_guard guard {db_lock_};
  if (is_open_) {
    spdlog::info("DB {} has open!", db_file_path_);
    return true;
  }
  if (db_file_path.empty()) {
    db_file_path_ = "ling.db";
  } else {
    db_file_path_ = db_file_path;
  }
  db_ptr_ = std::make_shared<SQLite::Database>(db_file_path_, SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
  if (!init_sql.empty()) {
    for (const auto& sql : init_sql) {
      try {
        db_ptr_->exec(sql);
      } catch (SQLite::Exception& e) {
        spdlog::error("failed to exec init sql: {}, exception: {}", sql,
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

TransPtr LocalSqlite::with_transaction() const {
  return std::make_unique<SQLite::Transaction>(*db_ptr_);
}

QueryResultPtr LocalSqlite::query(const std::string& sql) const {
  if (!is_open_) {
    spdlog::error("DB not open!");
    return nullptr;
  }
  SQLite::Statement query(*db_ptr_, sql);
  QueryResultPtr qr = std::make_shared<QueryResult>();
  //
  while (query.executeStep()) {
    const int col_cnt = query.getColumnCount();
    ColVec row;
    row.reserve(col_cnt);
    int col_idx = 0;
    while (col_idx < col_cnt) {
      std::string col_name = query.getColumnName(col_idx);
      std::any col_val;
      SQLite::Column col = query.getColumn(col_idx);
      switch (col.getType()) {
        case 1: // INTEGER
          col_val = std::make_any<int64_t>(col.getInt64());
          break;
        case 2: // FLOAT
          col_val = std::make_any<double>(col.getDouble());
          break;
        default:
          col_val = std::make_any<std::string>(col.getString());
      }
      row.emplace_back(col_name, col_val);
      col_idx++;
    }
    qr->rows.push_back(row);
  }
  return qr;
}


}