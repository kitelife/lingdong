#pragma once

#include <string>
#include <utility>

#include <SQLiteCpp/SQLiteCpp.h>

namespace ling::storage {

class QueryResult {
public:
  std::vector<std::vector<SQLite::Column>> rows;
};

using QueryResultPtr = std::shared_ptr<QueryResult>;

class LocalSqlite {
public:
  static LocalSqlite& singleton() {
    static LocalSqlite obj_ {};
    return obj_;
  }

  LocalSqlite() = default;

  bool open(const std::string& db_file_path, const std::string& init_sql = "");
  int exec(const std::string& sql);
  QueryResultPtr query(const std::string& sql);

private:
  std::string db_file_path_;
  std::shared_ptr<SQLite::Database> db_ptr_ = nullptr;
  std::atomic_bool is_open_ {false};
  std::mutex db_lock_ {};
};

}
