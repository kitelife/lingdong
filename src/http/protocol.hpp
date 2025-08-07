#pragma once

#include <spdlog/spdlog.h>
#include <tsl/robin_map.h>
#include <uv.h>

#include <string>

namespace ling::http {

class HttpRequest {
public:
  HttpRequest() = default;
  void to_string(std::string& s);

public:
  std::string_view action;
  std::string_view path;
  std::string_view http_version;
  tsl::robin_map<std::string, std::string> headers;
  std::string_view body;
};

inline void HttpRequest::to_string(std::string& s) {
  s += fmt::format("{} {} HTTP/{}\n", action, path, http_version);
  for (auto [k, v] : headers) {
    s += fmt::format("{}: {}\n", k, v);
  }
  s += "\n";
  if (body.size() > 0) {
    s += body;
    s += "\n\n";
  }
}

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
  ~HttpResponse() {}

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