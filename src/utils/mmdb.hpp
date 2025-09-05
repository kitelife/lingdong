#pragma once

#include <string>
#include <filesystem>
#include <fstream>
#include <map>

#include "maxminddb.h"
#include "cpr/cpr.h"
#include "fmt/format.h"
#include "spdlog/spdlog.h"

#include "helper.hpp"

/*
 * geoip：
 * - https://maxmind.github.io/MaxMind-DB/
 * - https://maxmind.github.io/libmaxminddb/
 * - https://dev.maxmind.com/geoip/geolite2-free-geolocation-data/
 * - https://www.maxmind.com/en/solutions/ip-geolocation-databases-api-services
 * - https://github.com/P3TERX/GeoLite.mmdb
 */

namespace ling::utils {
using namespace std::filesystem;

class MmdbRecord {
public:
  bool is_valid = false;
  std::string continent;
  std::string country;
  std::string city;

  std::string to_string() const {
    std::string s;
    if (!is_valid) {
      return s;
    }
    if (!continent.empty()) {
      s += continent;
    }
    if (!country.empty()) {
      s += (s.empty() ? "" : " / ") + country;
    }
    if (!city.empty()) {
      s += (s.empty() ? "" : " / ") + city;
    }
    return s;
  }

  std::string to_json_string() const {
    if (!is_valid) {
      return "{}";
    }
    return fmt::format(R"({{"continent": "{}", "country": "{}", "city": "{}"}})", continent, country, city);
  }
};

using MmdbRecordPtr = std::shared_ptr<MmdbRecord>;

class MaxMindGeoLite2Db {
public:
  bool open();
  void close() const;
  MmdbRecordPtr query_by_ip(const std::string& ip) const;

private:
  static MmdbRecordPtr fallback2dbip(const std::string& ip);
  std::shared_ptr<MMDB_s> country_mmdb_ = std::make_shared<MMDB_s>();
  std::shared_ptr<MMDB_s> city_mmdb_ = std::make_shared<MMDB_s>();
  //
  static bool check_download();
  static path ROOT_PATH;
  static std::string COUNTRY_MMDB_URL;
  static path COUNTRY_MMDB_FILE;
  static std::string CITY_MMDB_URL;
  static path CITY_MMDB_FILE;

  static std::string DB_IP_API;
};

path MaxMindGeoLite2Db::ROOT_PATH = {"data/geolite2-mmdb"};

std::string MaxMindGeoLite2Db::COUNTRY_MMDB_URL =
    "https://github.com/P3TERX/GeoLite.mmdb/raw/download/GeoLite2-Country.mmdb";
path MaxMindGeoLite2Db::COUNTRY_MMDB_FILE = {"GeoLite2-Country.mmdb"};

std::string MaxMindGeoLite2Db::CITY_MMDB_URL = "https://github.com/P3TERX/GeoLite.mmdb/raw/download/GeoLite2-City.mmdb";
path MaxMindGeoLite2Db::CITY_MMDB_FILE = {"GeoLite2-City.mmdb"};

std::string MaxMindGeoLite2Db::DB_IP_API = "https://db-ip.com/demo/home.php";

inline bool MaxMindGeoLite2Db::open() {
  if (!check_download()) {
    return false;
  }
  auto status = MMDB_open((ROOT_PATH / COUNTRY_MMDB_FILE).c_str(), MMDB_MODE_MMAP, country_mmdb_.get());
  if (MMDB_SUCCESS != status) {
    spdlog::error("failure to open country mmdb file: ", (ROOT_PATH / COUNTRY_MMDB_FILE).string());
    return false;
  }
  status = MMDB_open((ROOT_PATH / CITY_MMDB_FILE).c_str(), MMDB_MODE_MMAP, city_mmdb_.get());
  if (MMDB_SUCCESS != status) {
    spdlog::error("failure to open city mmdb file: ", (ROOT_PATH / COUNTRY_MMDB_FILE).string());
    return false;
  }
  return true;
}

inline void MaxMindGeoLite2Db::close() const {
  if (country_mmdb_ != nullptr && country_mmdb_->filename != nullptr) {
    MMDB_close(country_mmdb_.get());
  }
  if (city_mmdb_ != nullptr && city_mmdb_->filename != nullptr) {
    MMDB_close(city_mmdb_.get());
  }
}

inline MmdbRecordPtr MaxMindGeoLite2Db::query_by_ip(const std::string& ip) const {
  int gai_error;
  int mmdb_error;
  auto mrp = std::make_shared<MmdbRecord>();
  auto res = MMDB_lookup_string(city_mmdb_.get(), ip.c_str(), &gai_error, &mmdb_error);
  if (gai_error == 0 && MMDB_SUCCESS == mmdb_error && res.found_entry) {
    MMDB_entry_data_s city_name_en_entry;
    if (MMDB_SUCCESS == MMDB_get_value(&res.entry, &city_name_en_entry, "city", "names", "en", nullptr) &&
        city_name_en_entry.has_data) {
      auto city_name_en = std::string(city_name_en_entry.utf8_string, city_name_en_entry.data_size);
      mrp->city = city_name_en;
    }
    MMDB_entry_data_s country_name_en_entry;
    if (MMDB_SUCCESS == MMDB_get_value(&res.entry, &country_name_en_entry, "country", "names", "en", nullptr) &&
        country_name_en_entry.has_data) {
      auto country_name_en = std::string(country_name_en_entry.utf8_string, country_name_en_entry.data_size);
      mrp->country = country_name_en;
    }
    MMDB_entry_data_s continent_name_en_entry;
    if (MMDB_SUCCESS == MMDB_get_value(&res.entry, &continent_name_en_entry, "continent", "names", "en", nullptr) &&
        continent_name_en_entry.has_data) {
      auto continent_name_en = std::string(continent_name_en_entry.utf8_string, continent_name_en_entry.data_size);
      mrp->continent = continent_name_en;
    }
    mrp->is_valid = !mrp->city.empty();
  }
  if (mrp->is_valid) {
    return mrp;
  }
  //
  res = MMDB_lookup_string(country_mmdb_.get(), ip.c_str(), &gai_error, &mmdb_error);
  if (gai_error == 0 && MMDB_SUCCESS == mmdb_error && res.found_entry) {
    MMDB_entry_data_s country_name_en_entry;
    if (MMDB_SUCCESS == MMDB_get_value(&res.entry, &country_name_en_entry, "country", "names", "en", nullptr) &&
        country_name_en_entry.has_data) {
      auto country_name_en = std::string(country_name_en_entry.utf8_string, country_name_en_entry.data_size);
      mrp->country = country_name_en;
    }
    MMDB_entry_data_s continent_name_en_entry;
    if (MMDB_SUCCESS == MMDB_get_value(&res.entry, &continent_name_en_entry, "continent", "names", "en", nullptr) &&
        continent_name_en_entry.has_data) {
      auto continent_name_en = std::string(continent_name_en_entry.utf8_string, continent_name_en_entry.data_size);
      mrp->continent = continent_name_en;
    }
    mrp->is_valid = !mrp->country.empty();
  }
  if (mrp->is_valid) {
    return mrp;
  }
  return fallback2dbip(ip);
}

inline bool MaxMindGeoLite2Db::check_download() {
  if (!exists(ROOT_PATH)) {
    if (!create_directories(ROOT_PATH)) {
      spdlog::error("failure to create directory: {}", ROOT_PATH.string());
      return false;
    }
  }
  // https://docs.libcpr.org/advanced-usage.html#download-file
  float last_download_progress = 0.0;
  auto progress_cb = [&last_download_progress](cpr::cpr_pf_arg_t downloadTotal,
                                               cpr::cpr_pf_arg_t downloadNow,
                                               cpr::cpr_pf_arg_t uploadTotal,
                                               cpr::cpr_pf_arg_t uploadNow,
                                               intptr_t userdata) -> bool {
    if (downloadNow > 0 && downloadTotal > 0) {
      float progress_now = 1.0 * downloadNow / downloadTotal * 100;
      if (progress_now - last_download_progress > 0.1 || static_cast<int>(progress_now) == 100) {
        // https://fmt.dev/11.1/syntax/
        spdlog::info("Downloading: {:.2f}%", progress_now);
        last_download_progress = progress_now;
      }
    }
    return true;
  };
  std::map<std::string, std::string> proxies;
  char* https_proxy = std::getenv("HTTPS_PROXY") != nullptr ? std::getenv("HTTPS_PROXY") : std::getenv("https_proxy");
  char* http_proxy = std::getenv("HTTP_PROXY") != nullptr ? std::getenv("HTTP_PROXY") : std::getenv("http_proxy");
  if (https_proxy != nullptr) {
    proxies["https"] = https_proxy;
  }
  if (http_proxy != nullptr) {
    proxies["http"] = http_proxy;
  }
  if (!exists(ROOT_PATH / COUNTRY_MMDB_FILE)) {
    std::ofstream c2mmdb_ofs{ROOT_PATH / COUNTRY_MMDB_FILE, std::ios::trunc | std::ios::binary};
    auto r = cpr::Download(c2mmdb_ofs, cpr::Url{COUNTRY_MMDB_URL},
                           cpr::Verbose{true}, cpr::ProgressCallback{progress_cb}, cpr::Proxies{proxies});
    if (r.status_code != 200) {
      spdlog::error("failure to download {}", COUNTRY_MMDB_URL);
      return false;
    }
  }
  if (!exists(ROOT_PATH / CITY_MMDB_FILE)) {
    std::ofstream c3mmdb_ofs{ROOT_PATH / CITY_MMDB_FILE, std::ios::trunc | std::ios::binary};
    last_download_progress = 0.0;
    auto r = cpr::Download(c3mmdb_ofs, cpr::Url{CITY_MMDB_URL},
                           cpr::Verbose{true}, cpr::ProgressCallback{progress_cb}, cpr::Proxies{proxies});
    if (r.status_code != 200) {
      spdlog::error("failure to download {}", CITY_MMDB_URL);
      return false;
    }
  }
  return true;
}

inline MmdbRecordPtr MaxMindGeoLite2Db::fallback2dbip(const std::string& ip) {
  spdlog::info("fallback to get from db-ip.com: {}", ip);
  auto mrp = std::make_shared<MmdbRecord>();
  auto r = cpr::Get(cpr::Url{DB_IP_API}, cpr::Parameters{{"s", ip}},
                    cpr::Header{{"User-Agent", USER_AGENT}});
  if (r.status_code != 200) {
    spdlog::error("failure to call {}?s={}", DB_IP_API, ip);
    return mrp;
  }
  auto rj = nlohmann::json::parse(r.text);
  if (!rj.contains("status") || rj["status"].get<std::string>() != "ok" || !rj.contains("demoInfo")) {
    spdlog::error("invalid resp: {}", r.text);
    return mrp;
  }
  auto info = rj["demoInfo"].get<nlohmann::json>();
  if (info.contains("continentName")) {
    mrp->continent = info["continentName"].get<std::string>();
  }
  if (info.contains("countryName")) {
    mrp->country = info["countryName"].get<std::string>();
  }
  if (info.contains("city")) {
    mrp->city = info["city"].get<std::string>();
  }
  mrp->is_valid = !mrp->continent.empty() || !mrp->country.empty() || !mrp->city.empty();
  return mrp;
}
}