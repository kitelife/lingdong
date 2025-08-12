#pragma once

#include <SQLiteCpp/SQLiteCpp.h>

#include <any>
#include <string>
#include <utility>

namespace ling::storage {

using ColNameValue = std::pair<std::string, std::any>;
using ColVec = std::vector<ColNameValue>;
using RowVec = std::vector<ColVec>;

class QueryResult {
public:
  RowVec rows;
};

using QueryResultPtr = std::shared_ptr<QueryResult>;
using TransPtr = std::unique_ptr<SQLite::Transaction>;

class LocalSqlite {
public:
  static LocalSqlite& singleton() {
    static LocalSqlite obj_ {};
    return obj_;
  }

  LocalSqlite() = default;

  bool open(const std::string& db_file_path, const std::vector<std::string>& init_sql);
  int exec(const std::string& sql);
  TransPtr with_transaction() const;
  QueryResultPtr query(const std::string& sql) const;

private:
  std::string db_file_path_;
  std::shared_ptr<SQLite::Database> db_ptr_ = nullptr;
  std::atomic_bool is_open_ {false};
  std::mutex db_lock_ {};
};

}
