#include "time.hpp"

#include <absl/time/time.h>
#include <absl/time/clock.h>
#include <filesystem>
#include <fstream>


#include <gtest/gtest.h>

TEST(TimeTest, convert2date) {
  constexpr absl::CivilDay some_day(2025, 4, 18);
  const absl::Time tp = absl::FromCivil(some_day, ling::utils::loc);
  EXPECT_EQ(ling::utils::format2date(tp), "2025-04-18");
}

TEST(TimeTest, test_file_time) {
  auto temp_file = std::filesystem::temp_directory_path() / "time_test.txt";
  std::fstream stream(temp_file, std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    return;
  }
  stream << "Hello world!";
  stream.flush();
  stream.close();
  permissions(temp_file, std::filesystem::perms::owner_all | std::filesystem::perms::group_all,
                std::filesystem::perm_options::add);
  //
  const auto last_write_time = std::filesystem::last_write_time(temp_file);
  EXPECT_EQ(ling::utils::convert(last_write_time), ling::utils::format2date(absl::Now()));
  //
  std::filesystem::remove(temp_file);
}