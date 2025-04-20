//
// Created by xiayf on 2025/4/20.
//

#include <SQLiteCpp/SQLiteCpp.h>

#include <iostream>
#include <cstdio>

int main() {
  std::cout << "SQlite3 version " << SQLite::VERSION << " (" << SQLite::getLibVersion() << ")" << std::endl;
  std::cout << "SQliteC++ version " << SQLITECPP_VERSION << std::endl;
  //
  try {
    // Open a database file in create/write mode
    SQLite::Database db("test.db3", SQLite::OPEN_READWRITE | SQLite::OPEN_CREATE);
    std::cout << "SQLite database file '" << db.getFilename().c_str() << "' opened successfully\n";
    // Create a new table with an explicit "id" column aliasing the underlying rowid
    db.exec("DROP TABLE IF EXISTS test");
    db.exec("CREATE TABLE test (id INTEGER PRIMARY KEY, value TEXT)");
    // first row
    int nb = db.exec("INSERT INTO test VALUES (NULL, \"test\")");
    std::cout << "INSERT INTO test VALUES (NULL, \"test\")\", returned " << nb << std::endl;

    // second row
    nb = db.exec("INSERT INTO test VALUES (NULL, \"second\")");
    std::cout << "INSERT INTO test VALUES (NULL, \"second\")\", returned " << nb << std::endl;

    // update the second row
    nb = db.exec("UPDATE test SET value=\"second-updated\" WHERE id='2'");
    std::cout << "UPDATE test SET value=\"second-updated\" WHERE id='2', returned " << nb << std::endl;

    // Check the results : expect two row of result
    SQLite::Statement query(db, "SELECT * FROM test");
    std::cout << "SELECT * FROM test :\n";
    while (query.executeStep()) {
      std::cout << "row (" << query.getColumn(0) << ", \"" << query.getColumn(1) << "\")\n";
    }

    db.exec("DROP TABLE test");
  } catch (std::exception& e) {
    std::cout << "SQLite exception: " << e.what() << std::endl;
    return EXIT_FAILURE;  // unexpected error : exit the example program
  }
  remove("test.db3");

  return 0;
}
