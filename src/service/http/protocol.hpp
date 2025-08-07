#pragma once

#include <string>

#include <spdlog/spdlog.h>
#include <tsl/robin_map.h>
#include <uv.h>

#include "../protocol.h"
#include "../../utils/strings.hpp"

namespace ling::http {

static size_t HTTP_FIRST_LINE_LENGTH_LIMIT {5 * 1024}; // 5kB

class HttpRequest {
public:
  HttpRequest() = default;
  void to_string(std::string& s);
  ParseStatus parse(char* buffer, size_t buffer_size);

public:
  size_t first_line_end_idx = 0;
  bool valid = false;
  //
  size_t headers_end_idx = 0;
  //
  std::string action;
  std::string path;
  std::string http_version;
  tsl::robin_map<std::string, std::string> headers;
  std::string_view body;
};

inline ParseStatus HttpRequest::parse(char* buffer, size_t buffer_size) {
  if (first_line_end_idx <= 0) {
    spdlog::error("illegal parse status");
    return ParseStatus::INVALID;
  }
  if (headers_end_idx == 0) { // 解析请求头
    size_t cnt = buffer_size-1-first_line_end_idx;
    std::string_view part_after_first_line {buffer+first_line_end_idx+1, cnt};
    size_t idx = 1;
    size_t new_line_begin_idx = 0;
    while (idx < part_after_first_line.size()) {
      if (part_after_first_line[idx] == '\n') {
        if (part_after_first_line[idx-1] == '\r') {
          if (idx >= 4 && part_after_first_line[idx-1] == '\r' && part_after_first_line[idx-2] == '\n' && part_after_first_line[idx-3] == '\r') { // 请求头结束
            headers_end_idx = idx+(first_line_end_idx+1);
            break;
          }
          std::string_view header_line = part_after_first_line.substr(new_line_begin_idx, idx-new_line_begin_idx-1);
          auto pos = header_line.find(':');
          if (pos == std::string_view::npos) {
            spdlog::warn("illegal header line: {}", header_line);
          } else {
            std::string header_name = std::string(utils::view_strip_empty(header_line.substr(0, pos)));
            std::string header_val = std::string(utils::view_strip_empty(header_line.substr(pos+1, header_line.size()-1-pos)));
            headers[header_name] = header_val;
          }
          new_line_begin_idx = idx+1;
        }
      }
      idx++;
    }
  } // 判断请求体是否结束
  if (headers_end_idx > 0 && buffer_size > headers_end_idx) {
    if (buffer[buffer_size-1] == '\n' && buffer[buffer_size-2] == '\r' && buffer[buffer_size-3] == '\n' && buffer[buffer_size-4] == '\r') {
      body = std::string_view(buffer+headers_end_idx+1, buffer_size-1-headers_end_idx);
      return ParseStatus::COMPLETE;
    }
  }
  return ParseStatus::CONTINUE;
}

inline void HttpRequest::to_string(std::string& s) {
  s += fmt::format("{} {} HTTP/{}\n", action, path, http_version);
  for (auto [k, v] : headers) {
    s += fmt::format("{}: {}\n", k, v);
  }
  s += "\n";
  if (!body.empty()) {
    s += body;
    s += "\n\n";
  }
}

inline ParseStatus probe(const char* buffer, size_t buffer_size, HttpRequest& http_req) {
  size_t idx = 1;
  bool least_one_line = false;
  while (idx < buffer_size) {
    if (*(buffer+idx) == '\n' && *(buffer+idx-1) == '\r') {
      least_one_line = true;
      break;
    }
    idx++;
  }
  // 安全防护：首行长度限制
  if (idx >= HTTP_FIRST_LINE_LENGTH_LIMIT) {
    return ParseStatus::INVALID;
  }
  if (!least_one_line) {
    return ParseStatus::CONTINUE;
  }
  http_req.first_line_end_idx = idx;
  // 示例：GET /path HTTP/1.1
  const std::string_view protocol_line {buffer, idx-1};
  idx = 0;
  size_t last_blank_idx = 0;
  while (idx < protocol_line.size()) {
    if (protocol_line[idx] == ' ' || protocol_line[idx] == '\t') {
      if (http_req.action.empty()) {
        http_req.action = protocol_line.substr(0, idx);
        last_blank_idx = idx;
      } else if (http_req.path.empty()) {
        http_req.path = protocol_line.substr(last_blank_idx+1, idx-1-last_blank_idx);
        last_blank_idx = idx;
        break;
      }
    }
    idx++;
  }
  if (last_blank_idx == 0) {
    return ParseStatus::INVALID;
  }
  idx = last_blank_idx+1;
  if (protocol_line.size()-idx < 5) {
    return ParseStatus::INVALID;
  }
  const std::string_view protocol_version {buffer+idx, protocol_line.size()-idx};
  idx = 0;
  while (idx < protocol_version.size()-1) {
    if (protocol_version[idx] == '/') {
      http_req.http_version = protocol_version.substr(idx+1, protocol_version.size()-idx);
      break;
    }
    idx++;
  }
  if (http_req.http_version.empty()) {
    return ParseStatus::INVALID;
  }
  http_req.valid = true;
  return ParseStatus::CONTINUE;
}

// ---------------------------------------------------------------------------------------------------------------------

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static void clean_after_send(uv_write_t* req, int status) {
  if (status) {
    fprintf(stderr, "Write error %s\n", uv_strerror(status));
  }
  // auto buf = reinterpret_cast<write_req_t*>(req)->buf;
  // std::cout << std::string(buf.base, buf.len)  << std::endl;
  //
  write_req_t* wr = reinterpret_cast<write_req_t*>(req);
  free(wr->buf.base);
  free(wr);
}

struct HttpHeader {
  std::string name;
};

namespace header {
static HttpHeader ContentType{"Content-Type"};
}

enum class HttpStatusCode {
  OK = 200,

  NOT_FOUND = 404,
  BAD_REQUEST = 400,

  INTERNAL_ERR = 500,
};

static tsl::robin_map<HttpStatusCode, std::string> CODE2MSG{
  {HttpStatusCode::OK, "Ok"},
  {HttpStatusCode::NOT_FOUND, "Not Found"},
  {HttpStatusCode::BAD_REQUEST, "Bad Request"},
  {HttpStatusCode::INTERNAL_ERR, "Internal Error"}};

struct ContentType {
  std::string type_name;
  bool is_binary = false;
};

static tsl::robin_map<std::string, ContentType> FILE_SUFFIX_TYPE_M_CONTENT_TYPE{
  {"css", {"text/css; charset=utf-8", false}},
  {"js", {"application/javascript; charset=utf-8", false}},
  {"html", {"text/html; charset=utf-8", false}},
  {"htm", {"text/html; charset=utf-8", false}},
  {"xml", {"application/xml; charset=utf-8", false}},
  {"ico", {"image/x-icon", true}},
};

class HttpResponse {
public:
  HttpResponse() = default;
  ~HttpResponse() = default;

  bool send(uv_stream_t* client);
  void with_code(HttpStatusCode status_code) {
    code = status_code;
  }
  void with_header(const std::string& name, const std::string& value);
  bool with_body(char* content_data, size_t content_length);

private:
  //
  HttpStatusCode code = HttpStatusCode::OK;
  tsl::robin_map<std::string, std::string> resp_headers;
  //
  bool sealed_ = false;
  char* content_ = nullptr;  // 在 clean_after_send 中被 free
  size_t content_length_ = 0;
};

using HttpResponsePtr = std::shared_ptr<HttpResponse>;

inline bool HttpResponse::send(uv_stream_t* client) {
  size_t buf_size = 0;
  std::vector<std::string> resp_lines;
  resp_lines.emplace_back(fmt::format("HTTP/1.1 {} {}\r\n", static_cast<int>(code), CODE2MSG[code]));
  buf_size += resp_lines.back().size();
  for (auto [k, v] : resp_headers) {
    resp_lines.emplace_back(fmt::format("{}: {}\r\n", k, v));
    buf_size += resp_lines.back().size();
  }
  if (content_length_ > 0) {
    resp_lines.emplace_back(fmt::format("Content-Length: {}\r\n", content_length_));
    buf_size += resp_lines.back().size();
  }
  resp_lines.emplace_back("\r\n");
  buf_size += resp_lines.back().size();
  //
  buf_size += content_length_ + 4;
  //
  size_t copy_idx = 0;
  char* resp_buf = static_cast<char*>(malloc(buf_size));
  for (auto s : resp_lines) {
    memcpy(resp_buf + copy_idx, s.data(), s.size());
    copy_idx += s.size();
  }
  if (content_length_ > 0) {
    memcpy(resp_buf + copy_idx, content_, content_length_);
    copy_idx += content_length_;
  }
  memcpy(resp_buf + copy_idx, "\r\n\r\n", 4);
  copy_idx += 4;
  auto req = static_cast<write_req_t*>(malloc(sizeof(write_req_t)));
  req->buf = uv_buf_init(resp_buf, buf_size);
  uv_write(reinterpret_cast<uv_write_t*>(req), client, &req->buf, 1, clean_after_send);
  return true;
}

inline void HttpResponse::with_header(const std::string& name, const std::string& value) {
  resp_headers[name] = value;
}

inline bool HttpResponse::with_body(char* content_data, size_t content_length) {
  if (sealed_) {
    spdlog::error("response has been sealed");
    return false;
  }
  content_ = content_data;
  content_length_ = content_length;
  sealed_ = true;
  return true;
}

}  // namespace ling::http