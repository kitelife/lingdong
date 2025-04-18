#pragma once

#include <chrono>
#include <filesystem>

#include <absl/time/time.h>
#include <absl/strings/str_split.h>

namespace ling::utils {

using std::chrono::time_point;
using std::filesystem::file_time_type;

static absl::TimeZone loc = absl::LocalTimeZone();

static std::string format2date(const absl::Time tp) {
  return FormatTime("%Y-%m-%d", tp, loc);
}

static std::string convert(const file_time_type t) {
  const absl::Time at = absl::FromUnixMicros(t.time_since_epoch().count() / 1000);
  return format2date(at);
}

static std::string convert(const time_point<std::chrono::system_clock> tp) {
  const absl::Time at = absl::FromUnixMicros(tp.time_since_epoch().count() / 1000);
  return format2date(at);
}

static std::string date_format_convert(const std::string& date) {
  std::vector<absl::string_view> date_parts;
  if (date.find('/') != std::string::npos) {
    date_parts = absl::StrSplit(date, '/', absl::SkipEmpty());
  } else if (date.find('-') != std::string::npos) {
    date_parts = absl::StrSplit(date, '-', absl::SkipEmpty());
  }
  if (date_parts.size() < 3) {
    return date;
  }
  absl::CivilDay target_day{std::stoi(date_parts[0].data()),
    std::stoi(date_parts[1].data()),std::stoi(date_parts[2].data()) };
  return format2date(absl::FromCivil(target_day, loc));
}

}