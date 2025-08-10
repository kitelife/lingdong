#pragma once

#include <string>
#include <utility>

#include <spdlog/spdlog.h>
#include <tsl/robin_map.h>
#include <uv.h>

#include "service/protocol.h"
#include "utils/strings.hpp"

namespace ling::http {

static size_t HTTP_FIRST_LINE_LENGTH_LIMIT {5 * 1024}; // 5kB

struct HttpHeader {
  std::string name;
};

namespace header {
static HttpHeader ContentType {"Content-Type"};
static HttpHeader ContentLength {"Content-Length"};
static HttpHeader UserAgent {"User-Agent"};
}

struct UrlQuery {
  std::string path;
  tsl::robin_map<std::string, std::string> params;

  static std::pair<std::string, std::string> parse_one(const std::string& param) {
    std::string pk = param;
    std::string pv;
    auto equal_pos = param.find('=');
    if (equal_pos == std::string::npos || equal_pos == param.size()-1) {
      return std::make_pair(pk, pv);
    }
    pk = utils::view_strip_empty(param.substr(0, equal_pos));
    pv = param.substr(equal_pos+1, param.size()-1-equal_pos);
    return std::make_pair(pk, pv);
  }

  bool parse(std::string& q) {
    if (q.empty()) {
      return false;
    }
    const auto pos = q.find('?');
    if (pos == std::string::npos) {
      path = q.substr(0, q.size());
      return true;
    }
    path = q.substr(0, pos);
    // example: /?hello=world
    std::string params_part {utils::view_strip_empty(q.substr(pos+1))};
    size_t idx = 0;
    size_t sub_begin = 0;
    while (idx < params_part.size()) {
      if (params_part[idx] == '&') {
        std::string param {utils::view_strip_empty(params_part.substr(sub_begin, idx-sub_begin))};
        auto [pk, pv] = parse_one(param);
        params[pk] = pv;
        sub_begin = idx+1;
      }
      idx++;
    }
    // spdlog::debug(params_part.substr(sub_begin));
    auto [pk, pv] = parse_one(std::string(utils::view_strip_empty(params_part.substr(sub_begin))));
    params[pk] = pv;
    return true;
  }

  std::string json_str() {
    inja::json j;
    j["path"] = path;
    j["params"] = inja::json({});
    for (auto [k, v] : params) {
      j["params"][k] = v;
    }
    return j.dump(2);
  }
};

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
  std::pair<std::string, int> from;
  //
  std::string action;
  std::string raw_q;
  UrlQuery q;
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
            std::string header_name {utils::view_strip_empty(header_line.substr(0, pos))};
            std::string header_val {utils::view_strip_empty(header_line.substr(pos+1, header_line.size()-1-pos))};
            headers[header_name] = header_val;
          }
          new_line_begin_idx = idx+1;
        }
      }
      idx++;
    }
  }
  /*
  if (spdlog::get_level() == spdlog::level::debug) {
    spdlog::debug("query: {}", q.json_str());
  }
  */
  // 判断请求体是否结束
  if (headers.contains(header::ContentLength.name)) {
    long length = std::strtol(headers[header::ContentLength.name].c_str(), nullptr, 10);
    if (length > 0 && length == buffer_size-1-headers_end_idx) {
      body = body = std::string_view(buffer+headers_end_idx+1, length);
      return ParseStatus::COMPLETE;
    }
  } else if (headers_end_idx > 0 && buffer_size > headers_end_idx) {
    if (buffer[buffer_size-1] == '\n' && buffer[buffer_size-2] == '\r' && buffer[buffer_size-3] == '\n' && \
      buffer[buffer_size-4] == '\r') {
      body = std::string_view(buffer+headers_end_idx+1, buffer_size-1-headers_end_idx);
      return ParseStatus::COMPLETE;
    }
  }
  return ParseStatus::INVALID;
}

inline void HttpRequest::to_string(std::string& s) {
  s += fmt::format("{} {} HTTP/{}\n", action, raw_q, http_version);
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
    return ParseStatus::INVALID;
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
      } else if (http_req.raw_q.empty()) {
        http_req.raw_q = protocol_line.substr(last_blank_idx+1, idx-1-last_blank_idx);
        http_req.q.parse(http_req.raw_q);
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
  bool match_http = false;
  while (idx < protocol_version.size()-1) {
    if (protocol_version[idx] == '/') {
      if (protocol_version.substr(0, idx) != "HTTP") {
        break;
      }
      match_http = true;
      http_req.http_version = protocol_version.substr(idx+1, protocol_version.size()-idx);
      break;
    }
    idx++;
  }
  if (!match_http || http_req.http_version.empty()) {
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
  bool with_body(std::string body);

private:
  //
  HttpStatusCode code = HttpStatusCode::OK;
  tsl::robin_map<std::string, std::string> resp_headers {};
  //
  bool sealed_ = false;
  // char* content_ = nullptr;  // 在 clean_after_send 中被 free
  // size_t content_length_ = 0;
  std::string body_;
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
  if (!body_.empty()) {
    resp_lines.emplace_back(fmt::format("{}: {}\r\n", header::ContentLength.name, body_.size()));
    buf_size += resp_lines.back().size();
    //
    buf_size += body_.size() + 4;
  }
  resp_lines.emplace_back("\r\n");
  buf_size += resp_lines.back().size();
  //
  size_t copy_idx = 0;
  char* resp_buf = static_cast<char*>(malloc(buf_size));
  memset(resp_buf, 0, buf_size);
  for (auto s : resp_lines) {
    memcpy(resp_buf + copy_idx, s.data(), s.size());
    copy_idx += s.size();
  }
  if (!body_.empty()) {
    memcpy(resp_buf + copy_idx, body_.data(), body_.size());
    copy_idx += body_.size();
    memcpy(resp_buf + copy_idx, "\r\n\r\n", 4);
  }
  copy_idx += 4;
  auto req = static_cast<write_req_t*>(malloc(sizeof(write_req_t)));
  req->buf = uv_buf_init(resp_buf, buf_size);
  // spdlog::debug("Resp: {}", std::string(resp_buf, buf_size));
  uv_write(reinterpret_cast<uv_write_t*>(req), client, &req->buf, 1, clean_after_send);
  return true;
}

inline void HttpResponse::with_header(const std::string& name, const std::string& value) {
  resp_headers[name] = value;
}

inline bool HttpResponse::with_body(std::string body) {
  if (sealed_) {
    spdlog::error("response has been sealed");
    return false;
  }
  body_ = std::move(body);
  sealed_ = true;
  return true;
}

}  // namespace ling::http