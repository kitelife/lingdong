#pragma once

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <uv.h>
#include <tsl/robin_map.h>

#include "context.hpp"
#include "server.hpp"
#include "utils/strings.hpp"

DEFINE_string(host, "127.0.0.1", "server host to listen");
DEFINE_uint32(port, 8000, "server port to listen");

namespace ling {

static int DEFAULT_BACKLOG = 128;
static size_t REQUEST_SIZE_LIMIT {10 * 1024 * 1024}; // 10MB
static size_t HTTP_FIRST_LINE_LENGTH_LIMIT {5 * 1024}; // 5kB

using LoopPtr = std::shared_ptr<uv_loop_t>;

enum class HttpStatusCode {
  OK = 200,

  NOT_FOUND = 404,
  BAD_REQUEST = 400,

  INTERNAL_ERR = 500,
};

static tsl::robin_map<HttpStatusCode, std::string> CODE2MSG {
  {HttpStatusCode::OK, "Ok"},
  {HttpStatusCode::NOT_FOUND, "Not Found"},
  {HttpStatusCode::BAD_REQUEST, "Bad Request"},
  {HttpStatusCode::INTERNAL_ERR, "Internal Error"}
};

struct ContentType {
  std::string type_name;
  bool is_binary = false;
};

static tsl::robin_map<std::string, ContentType> FILE_SUFFIX_TYPE_M_CONTENT_TYPE {
  {"css", {"text/css; charset=utf-8", false}},
  {"js", {"application/javascript; charset=utf-8", false}},
  {"html", {"text/html; charset=utf-8", false}},
  {"htm", {"text/html; charset=utf-8", false}},
  {"xml", {"application/xml; charset=utf-8", false}},
  {"ico", {"image/x-icon", true}},
};

struct HttpHeader {
  std::string name;
};

namespace http_header {
static HttpHeader ContentType {"Content-Type"};
}

//--------------------------------------------------------------------------------

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static void clean_after_send(uv_write_t *req, int status) {
  if (status) {
    fprintf(stderr, "Write error %s\n", uv_strerror(status));
  }
  // auto buf = reinterpret_cast<write_req_t*>(req)->buf;
  // std::cout << std::string(buf.base, buf.len)  << std::endl;
  //
  write_req_t *wr = reinterpret_cast<write_req_t*>(req);
  free(wr->buf.base);
  free(wr);
}

class HttpResponse {
public:
  HttpResponse() = default;
  ~HttpResponse() {}

  bool send(uv_stream_t *client);
  void with_code(HttpStatusCode status_code) {
    code = status_code;
  }
  void with_header(std::string name, std::string value);
  bool with_body(char* content_data, size_t content_length);

private:
  //
  HttpStatusCode code = HttpStatusCode::OK;
  tsl::robin_map<std::string, std::string> resp_headers;
  //
  bool sealed_ = false;
  char* content_ = nullptr; // 在 clean_after_send 中被 free
  size_t content_length_ = 0;
};

using HttpResponsePtr = std::shared_ptr<HttpResponse>;

inline bool HttpResponse::send(uv_stream_t *client) {
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
    memcpy(resp_buf+copy_idx, s.data(), s.size());
    copy_idx += s.size();
  }
  if (content_length_ > 0) {
    memcpy(resp_buf+copy_idx, content_, content_length_);
    copy_idx += content_length_;
  }
  memcpy(resp_buf+copy_idx, "\r\n\r\n", 4);
  copy_idx += 4;
  auto req = static_cast<write_req_t*>(malloc(sizeof(write_req_t)));
  req->buf = uv_buf_init(resp_buf, buf_size);
  uv_write(reinterpret_cast<uv_write_t*>(req), client, &req->buf, 1, clean_after_send);
  return true;
}

inline void HttpResponse::with_header(std::string name, std::string value) {
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

enum class Protocol {
  UNKNOWN,
  INVALID,
  HTTP,
};

enum class ParseStatus {
  INVALID,
  CONTINUE,
  COMPLETE,
};

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

class RequestBuffer {
public:
  RequestBuffer() = default;
  ~RequestBuffer() {
    if (buffer_ != nullptr) {
      free(buffer_);
      buffer_ = nullptr;
    }
  }

  ParseStatus accept(const uv_buf_t* buf, ssize_t nread);
  void handle(uv_stream_t *client, std::function<void(uv_stream_t*)> cb);

private:
  ParseStatus try_parse();
  HttpRequest& with_http_request() {
    return http_req;
  }

private:
  char* buffer_ = nullptr;
  size_t size_ = 0;
  //
  Protocol protocol_ = Protocol::UNKNOWN;
  // http 协议相关字段
  size_t first_line_end_idx = 0;
  size_t headers_end_idx = 0;
  HttpRequest http_req;
};

inline ParseStatus RequestBuffer::accept(const uv_buf_t* buf, ssize_t nread) {
  // 安全防护
  if (size_ + nread >= REQUEST_SIZE_LIMIT) {
    spdlog::warn("request size exceed limit!");
    return ParseStatus::INVALID;
  }
  char* tmp_buffer = nullptr;
  size_t tmp_size;
  char* copy_base;
  if (buffer_ == nullptr) {
    tmp_size = nread;
    tmp_buffer = static_cast<char*>(malloc(tmp_size));
    copy_base = tmp_buffer;
  } else {
    tmp_size = size_ + nread;
    tmp_buffer = static_cast<char*>(malloc(tmp_size));
    memcpy(tmp_buffer, buffer_, size_);
    copy_base = tmp_buffer + size_;
  }
  //
  memcpy(copy_base, buf->base, nread);
  //
  free(buffer_);
  buffer_ = tmp_buffer;
  size_ = tmp_size;
  //
  return try_parse();
}

inline ParseStatus RequestBuffer::try_parse() {
  if (size_ <= 0 || buffer_ == nullptr) {
    return ParseStatus::CONTINUE;
  }
  if (protocol_ == Protocol::UNKNOWN) {
    size_t idx = 1;
    bool least_one_line = false;
    while (idx < size_) {
      if (*(buffer_+idx) == '\n' && *(buffer_+idx-1) == '\r') {
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
    first_line_end_idx = idx;
    // 示例：GET /path HTTP/1.1
    const std::string_view protocol_line {buffer_, idx-1};
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
      protocol_ = Protocol::INVALID;
      return ParseStatus::INVALID;
    }
    idx = last_blank_idx+1;
    if (protocol_line.size()-idx < 5) {
      protocol_ = Protocol::INVALID;
      return ParseStatus::INVALID;
    }
    const std::string_view protocol_version {buffer_+idx, protocol_line.size()-idx};
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
    //
    protocol_ = Protocol::HTTP;
  }
  if (protocol_ == Protocol::HTTP) {
    if (first_line_end_idx <= 0) {
      spdlog::error("illegal parse status");
      return ParseStatus::INVALID;
    }
    if (headers_end_idx == 0) { // 解析请求头
      size_t cnt = size_-1-first_line_end_idx;
      std::string_view part_after_first_line {buffer_+first_line_end_idx+1, cnt};
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
              http_req.headers[header_name] = header_val;
            }
            new_line_begin_idx = idx+1;
          }
        }
        idx++;
      }
    } // 判断请求体是否结束
    if (headers_end_idx > 0 && size_ > headers_end_idx) {
      if (buffer_[size_-1] == '\n' && buffer_[size_-2] == '\r' && buffer_[size_-3] == '\n' && buffer_[size_-4] == '\r') {
        http_req.body = std::string_view(buffer_+headers_end_idx+1, size_-1-headers_end_idx);
        return ParseStatus::COMPLETE;
      }
    }
  }
  return ParseStatus::CONTINUE;
}

static std::string find_suffix_type(const std::string& file_path) {
  std::string suffix_type = "";
  if (file_path.size() == 0) {
    return suffix_type;
  }
  size_t idx = file_path.size()-1;
  while (idx > 0) {
    if (file_path[idx] == '.') {
      suffix_type = file_path.substr(idx+1);
      break;
    }
    idx--;
  }
  return suffix_type;
}

static void static_file_handler(const HttpRequest& req, HttpResponsePtr resp, std::function<void(HttpResponsePtr)> cb) {
  std::string path = std::string(req.path);
  if (path[path.size()-1] == '/') {
    path += "index.html";
  }
  std::filesystem::path file_path {"." + path};
  if (!exists(file_path)) {
    resp->with_code(HttpStatusCode::NOT_FOUND);
    cb(resp);
    return;
  }
  std::ifstream file_stream;
  file_stream.open(file_path);
  if (file_stream.fail()) {
    resp->with_code(HttpStatusCode::INTERNAL_ERR);
    cb(resp);
    return;
  }
  std::string resp_content((std::istreambuf_iterator<char>(file_stream)), std::istreambuf_iterator<char>());
  resp->with_body(resp_content.data(), resp_content.size());
  std::string suffix_type = find_suffix_type(path);
  if (!suffix_type.empty() && FILE_SUFFIX_TYPE_M_CONTENT_TYPE.contains(suffix_type)) {
    resp->with_header(http_header::ContentType.name, FILE_SUFFIX_TYPE_M_CONTENT_TYPE[suffix_type].type_name);
  }
  //
  cb(resp);
}

static tsl::robin_map<std::string, void(*)(const HttpRequest&, HttpResponsePtr, std::function<void(HttpResponsePtr)>)> routes {
  {"", static_file_handler}
};

static void route(HttpRequest& req, std::function<void(HttpResponsePtr)> cb) {
  const HttpResponsePtr resp_ptr = std::make_shared<HttpResponse>();
  const auto route_path = std::string(req.path);
  if (!routes.contains(route_path)) {
    static_file_handler(req, resp_ptr, cb);
    return;
  }
  routes[route_path](req, resp_ptr, cb);
}

inline void RequestBuffer::handle(uv_stream_t *client, std::function<void(uv_stream_t*)> cb) {
  //
  std::string req_str;
  http_req.to_string(req_str);
  spdlog::debug("HTTP Request:\n{}", req_str);
  //
  route(with_http_request(), [&](const HttpResponsePtr& resp_ptr) {
    resp_ptr->send(client);
    // 请求处理完成后，需要清理 client_req_buffer_m_
    cb(client);
  });
}

using RequestBufferPtr = std::shared_ptr<RequestBuffer>;


// ---------------------------------------------------------------------------------------------------------------------

namespace connection {

static tsl::robin_map<uv_stream_t*, RequestBufferPtr> client_req_buffer_m_;
static std::mutex robin_mutex_;

static void clean_client_req_buffer_m(uv_stream_t* client) {
  std::lock_guard<std::mutex> {robin_mutex_};
  client_req_buffer_m_.erase(client);
}

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = static_cast<char*>(malloc(suggested_size));
  buf->len = suggested_size;
}

static void on_close(uv_handle_t* handle) {
  free(handle);
}

static void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    std::string content = std::string(buf->base, nread);
    if (!client_req_buffer_m_.contains(client)) {
      client_req_buffer_m_[client] = std::make_shared<RequestBuffer>();
    }
    // accept 需要将 buf 的内容拷贝一份
    // 返回 true，则表示请求体已接收完整
    auto& req_buffer = client_req_buffer_m_[client];
    auto status = req_buffer->accept(buf, nread);
    if (status == ParseStatus::COMPLETE) { // 请求完整了
      req_buffer->handle(client, clean_client_req_buffer_m);
    } else if (status == ParseStatus::INVALID) { // 不合法的请求
      clean_client_req_buffer_m(client);
    }
  } else if (nread < 0) {
    if (nread != UV_EOF) {
      fprintf(stderr, "Read error %s\n", uv_err_name(nread));
    }
    uv_close(reinterpret_cast<uv_handle_t*>(client), on_close);
  }
  //
  free(buf->base);
}

static bool handle(const LoopPtr& loop_, uv_stream_t* server) {
  uv_tcp_t *client = static_cast<uv_tcp_t*>(malloc(sizeof(uv_tcp_t)));
  uv_tcp_init(loop_.get(), client);
  if (uv_accept(server, reinterpret_cast<uv_stream_t*>(client)) == 0) {
    // on_read 要负责释放 client 的内存
    uv_read_start(reinterpret_cast<uv_stream_t*>(client), alloc_buffer, on_read);
    return true;
  }
  // on_close 要负责释放 client 的内存
  uv_close(reinterpret_cast<uv_handle_t*>(client), on_close);
  return false;
}

}

//----------------------------------------------------------------------------------------------------------------------

namespace server {

static LoopPtr loop_;
static std::shared_ptr<uv_tcp_t> tcp_server_;

static void on_new_connection(uv_stream_t* server, int status) {
  if (status != 0) {
    spdlog::error("New connection failed: {}", status);
    return;
  }
  if (!connection::handle(loop_, server)) {
    spdlog::error("Failed to handle connection.");
  }
}

static bool start() {
  auto conf_ptr = Context::singleton()->with_config();
  auto dist_dir = conf_ptr->dist_dir;
  //
  const auto origin_wd = current_path();
  current_path(absolute(dist_dir));
  spdlog::info("change working dir from {} to {}", origin_wd, current_path());
  //
  loop_.reset(uv_default_loop());
  tcp_server_ = std::make_shared<uv_tcp_t>();
  uv_tcp_init(loop_.get(), tcp_server_.get());
  //
  sockaddr_in addr{};
  uv_ip4_addr(FLAGS_host.c_str(), static_cast<int>(FLAGS_port), &addr);
  //
  uv_tcp_bind(tcp_server_.get(), reinterpret_cast<const sockaddr*>(&addr), 0);
  //
  int r = uv_listen(reinterpret_cast<uv_stream_t*>(tcp_server_.get()), DEFAULT_BACKLOG, on_new_connection);
  if (r) {
    spdlog::error("listen failed: {}", uv_strerror(r));
    return false;
  }
  return uv_run(loop_.get(), UV_RUN_DEFAULT);
}

}
}