#pragma once

#include <string>
#include <utility>

#include <spdlog/spdlog.h>
#include <tsl/robin_map.h>
#include <uv.h>
#include <absl/strings/str_split.h>
#include "nlohmann/json.hpp"

#include "service/protocol.h"
#include "utils/strings.hpp"

namespace ling::http {

static size_t HTTP_FIRST_LINE_LENGTH_LIMIT {5 * 1024}; // 5kB
static std::string HTTP_VERSION_CODE_1_1 = "1.1";

enum class HTTP_METHOD {
  UNKNOWN = 0,
  GET = 1,
  POST = 2,
  PUT = 4,
  HEAD = 8,
  DELETE = 16,
};

namespace header {

static std::string ContentType {"Content-Type"};
static std::string ContentLength {"Content-Length"};
static std::string UserAgent {"User-Agent"};

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
    nlohmann::json j;
    j["path"] = path;
    j["params"] = nlohmann::json({});
    for (auto& [k, v] : params) {
      j["params"][k] = v;
    }
    return j.dump(2);
  }
};

// https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Methods/POST
struct UrlEncodedFormBody {
  tsl::robin_map<std::string, std::string> params;
  //
  void parse(std::string_view s) {
    const std::vector<std::string> parts = absl::StrSplit(s, '&');
    for (const auto& sp : parts) {
      auto pos = sp.find('=');
      if (pos == std::string::npos) {
        params[sp] = "";
      } else {
        params[sp.substr(0, pos)] = sp.substr(pos+1, sp.size()-1-pos);
      }
    }
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
  if (headers.contains(header::ContentLength)) {
    long length = std::strtol(headers[header::ContentLength].c_str(), nullptr, 10);
    if (length > 0) {
      if (length == buffer_size-1-headers_end_idx) {
        body = std::string_view(buffer+headers_end_idx+1, length);
        return ParseStatus::COMPLETE;
      }
      if (length > buffer_size-1-headers_end_idx) {
        return ParseStatus::CONTINUE;
      }
      if (length < buffer_size-1-headers_end_idx) {
        spdlog::warn("illegal content length: {}, too small!", length);
        return ParseStatus::INVALID;
      }
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

  BAD_REQUEST = 400,
  UNAUTHORIZED = 401,
  FORBIDDEN = 403,
  // https://developer.mozilla.org/en-US/docs/Web/HTTP/Reference/Status/429
  NOT_FOUND = 404,
  METHOD_NOT_ALLOWED = 405,
  CONTENT_TOO_LARGE = 413,
  URI_TOO_LONG = 414,
  TOO_MANY_REQUESTS = 429,

  INTERNAL_ERR = 500,
};

static tsl::robin_map<HttpStatusCode, std::string> CODE2MSG{
  {HttpStatusCode::OK, "Ok"},

  {HttpStatusCode::BAD_REQUEST, "Bad Request"},
  {HttpStatusCode::UNAUTHORIZED, "Unauthorized"},
  {HttpStatusCode::FORBIDDEN, "Forbidden"},
  {HttpStatusCode::NOT_FOUND, "Not Found"},
  {HttpStatusCode::METHOD_NOT_ALLOWED, "Method Not Allowed"},
  {HttpStatusCode::CONTENT_TOO_LARGE, "Content Too Large"},
  {HttpStatusCode::URI_TOO_LONG, "URI Too Long"},
  {HttpStatusCode::TOO_MANY_REQUESTS, "Too Many Requests"},

  {HttpStatusCode::INTERNAL_ERR, "Internal Error"}};


// https://developer.mozilla.org/en-US/docs/Web/HTTP/Guides/MIME_types/Common_types
namespace content_type {

static std::string CSS = "text/css";
static std::string JS = "application/javascript";
static std::string HTML = "text/html";
static std::string XML = "application/xml";
static std::string ICO = "image/x-icon";
static std::string PLAIN = "text/plain";
static std::string JSON = "application/json";
static std::string X_WWW_FORM_URL_ENCODED = "application/x-www-form-urlencoded";
static std::string SVG = "image/svg+xml";
static std::string PNG = "image/png";
static std::string JPEG = "image/jpeg";
static std::string GIF = "image/gif";
static std::string TTF = "font/ttf";
static std::string WOFF = "font/woff";
static std::string WOFF2 = "font/woff2";

}

struct ContentType {
  std::string type_name;
  bool is_binary = false;
};

static tsl::robin_map<std::string, ContentType> FILE_SUFFIX_TYPE_M_CONTENT_TYPE{
  {"css", {content_type::CSS, false}},
  {"js", {content_type::JS, false}},
  {"html", {content_type::HTML, false}},
  {"htm", {content_type::HTML, false}},
  {"xml", {content_type::XML, false}},
  {"json", {content_type::JSON, false}},
  {"svg", {content_type::SVG, false}},
  {"txt", {content_type::PLAIN, false}},

  {"png", {content_type::PNG, true}},
  {"jpeg", {content_type::JPEG, true}},
  {"jpg", {content_type::JPEG, true}},
  {"ico", {content_type::ICO, true}},
  {"gif", {content_type::GIF, true}},
  {"ttf", {content_type::TTF, true}},
  {"woff", {content_type::WOFF, true}},
  {"woff2", {content_type::WOFF2, true}},
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
  resp_lines.emplace_back(fmt::format("HTTP/{} {} {}\r\n", HTTP_VERSION_CODE_1_1, static_cast<int>(code), CODE2MSG[code]));
  buf_size += resp_lines.back().size();
  for (auto [k, v] : resp_headers) {
    resp_lines.emplace_back(fmt::format("{}: {}\r\n", k, v));
    buf_size += resp_lines.back().size();
  }
  //
  resp_lines.emplace_back(fmt::format("{}: {}\r\n", header::ContentLength, body_.size()));
  buf_size += resp_lines.back().size();
  buf_size += body_.size() + 4;
  //
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