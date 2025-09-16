#include <filesystem>
#include <fstream>

#include "gflags/gflags.h"
#include "spdlog/spdlog.h"
#include "cpr/cpr.h"
#include "absl/strings/str_split.h"
#include "absl/strings/str_join.h"
#include "fmt/format.h"

#include "utils/helper.hpp"
#include "utils/strings.hpp"

namespace ling::m3u8 {

DEFINE_string(m3u8_url, "", "m3u8 url");
DEFINE_uint32(concurrent, 8, "concurrent");
DEFINE_string(output, "", "output file path");
DEFINE_bool(retain_tmp, false, "retain tmp dir");

using std::filesystem::path;

static std::string TMP_DIR = ".tmp_m3u8_to_mp4";

class M3u8Segment {
public:
  std::string meta;
  std::string ts;
};

class M3u8 {
public:
  std::vector<std::string> headers;
  std::vector<M3u8Segment> segments;
  std::string end = "#EXT-X-ENDLIST";
};

using M3u8Ptr = std::unique_ptr<M3u8>;

class TaskMeta {
public:
  uint32_t id = 0;
  std::string output_file_name;
  bool status = false;
  //
  std::vector<std::string> headers;
  std::vector<M3u8Segment> segments;
  std::string end;
};

M3u8Ptr fetch_m3u8(const std::string& m3u8_url) {
  auto r = cpr::Get(cpr::Url{m3u8_url}, cpr::Header{{"User-Agent", utils::USER_AGENT}});
  if (r.status_code != 200) {
    spdlog::error("failure to get {}", m3u8_url);
    return {};
  }
  int32_t last_slash_idx = utils::find_last_index(m3u8_url, '/');
  if (last_slash_idx < 0) {
    spdlog::error("invalid m3u8_url: {}", m3u8_url);
    return {};
  }
  auto ts_url_prefix = m3u8_url.substr(0, last_slash_idx + 1);
  std::vector<std::string> ts_vec;
  auto lines = absl::StrSplit(r.text, '\n');
  bool header_end = false;
  auto m3u8_ptr = std::make_unique<M3u8>();
  for (auto& line : lines) {
    if (line.empty()) {
      continue;
    }
    if (!header_end) {
      if (line.find("#EXTINF:") == 0) {
        header_end = true;
      } else {
        m3u8_ptr->headers.emplace_back(line);
        continue;
      }
    }
    if (line.find("#EXTINF:") == 0) {
      m3u8_ptr->segments.emplace_back();
      m3u8_ptr->segments.back().meta = line;
    } else if (line.find('#') != 0) {
      m3u8_ptr->segments.back().ts = line;
    } else {
      spdlog::warn("unknown line: {}", line);
    }
  }
  //
  for (auto& segment : m3u8_ptr->segments) {
    if (segment.ts.find("http://") != 0 && segment.ts.find("https://") != 0) {
      segment.ts = ts_url_prefix + segment.ts;
    }
  }
  return m3u8_ptr;
}

std::vector<TaskMeta> gen_task_seq(const M3u8Ptr& m3u8_ptr, uint32_t concurrent) {
  const auto& segments = m3u8_ptr->segments;
  concurrent = std::min(concurrent, static_cast<uint32_t>(segments.size()));
  uint32_t step = static_cast<uint32_t>(segments.size()) / concurrent;
  std::vector<TaskMeta> res;
  uint32_t i = 0;
  while (i < segments.size()) {
    res.emplace_back();
    for (uint32_t j = 0; j < step && i < segments.size(); ++j, ++i) {
      res.back().segments.emplace_back(segments[i]);
    }
    res.back().headers = m3u8_ptr->headers;
    res.back().end = m3u8_ptr->end;
    res.back().id = res.size();
    res.back().output_file_name = fmt::format("{}.mp4", res.size());
  }
  return res;
}

void download(std::vector<TaskMeta>& tasks, path& tmp_output_dir) {
  std::vector<std::future<void>> futures;
  futures.reserve(tasks.size());
  for (uint32_t idx = 0; idx < tasks.size(); idx++) {
    futures.emplace_back(std::async(std::launch::async, [idx, &tasks, &tmp_output_dir]() {
      auto& task = tasks[idx];
      const auto part_m3u8_path = tmp_output_dir / fmt::format("part-{}.m3u8", task.id);
      const auto part_mp4_path = tmp_output_dir / task.output_file_name;
      std::ofstream part_m3u8_file{part_m3u8_path};
      part_m3u8_file << absl::StrJoin(task.headers, "\n") << "\n";
      for (const auto& ts : task.segments) {
        part_m3u8_file << ts.meta << "\n" << ts.ts << "\n";
      }
      part_m3u8_file << task.end;
      part_m3u8_file.flush();
      part_m3u8_file.close();
      //
      const auto cmd = fmt::format("ffmpeg -protocol_whitelist file,http,https,tcp,tls,crypto -i '{}' -c copy {}",
                                   part_m3u8_path.c_str(), part_mp4_path.c_str());
      spdlog::info("run cmd: {}", cmd);
      if (system(cmd.c_str()) == 0) {
        tasks[idx].status = true;
      }
    }));
  }
  for (auto& fu : futures) {
    fu.wait();
  }
}

void combine(const std::vector<TaskMeta>& tasks, const path& tmp_output_dir, const std::string& output_file_name) {
  const auto part_mp4_list_fp = tmp_output_dir / "part-mp4-list.txt";
  std::ofstream part_mp4_list_f{part_mp4_list_fp};
  for (const auto& task : tasks) {
    if (task.status) {
      part_mp4_list_f << "file " << task.output_file_name << std::endl;
    }
  }
  part_mp4_list_f.flush();
  part_mp4_list_f.close();
  //
  const auto cmd = fmt::format("ffmpeg -f concat -i {} -c copy {}", part_mp4_list_fp.c_str(), output_file_name);
  spdlog::info("run cmd: {}", cmd);
  if (system(cmd.c_str()) == 0) {
    spdlog::info("success to combine mp4 file: {}", output_file_name);
  } else {
    spdlog::error("failed to combine mp4 file");
  }
}
}

int main(int argc, char* argv[]) {
  using namespace ling::m3u8;
  //
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  //
  auto tmp_output_dir = path(TMP_DIR) / std::to_string(std::hash<std::string>{}(FLAGS_m3u8_url));
  if (!std::filesystem::exists(tmp_output_dir)) {
    if (!std::filesystem::create_directories(tmp_output_dir)) {
      spdlog::error("failure to create tmp directory: {}", tmp_output_dir.c_str());
      return -1;
    }
  }
  //
  auto target_m3u8 = fetch_m3u8(FLAGS_m3u8_url);
  auto task_seq = gen_task_seq(target_m3u8, FLAGS_concurrent);
  //
  download(task_seq, tmp_output_dir);
  uint32_t failed_task_cnt = 0;
  for (const auto& tm : task_seq) {
    failed_task_cnt += (tm.status ? 1 : 0);
  }
  if (failed_task_cnt > 0) {
    spdlog::info("total task cnt: {}, failure task cnt: {}", task_seq.size(), failed_task_cnt);
  }
  //
  combine(task_seq, tmp_output_dir, FLAGS_output);
  //
  if (!FLAGS_retain_tmp) {
    std::filesystem::remove_all(tmp_output_dir);
  }
  return 0;
}