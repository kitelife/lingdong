#pragma once

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <uv.h>
#include <tsl/robin_map.h>

#include "context.hpp"
#include "server.hpp"
#include "utils/strings.hpp"
#include "http/protocol.hpp"
#include "http/router.hpp"

DEFINE_string(host, "127.0.0.1", "server host to listen");
DEFINE_uint32(port, 8000, "server port to listen");

namespace ling {

using namespace http;

static int DEFAULT_BACKLOG = 128;
static size_t REQUEST_SIZE_LIMIT {10 * 1024 * 1024}; // 10MB
static size_t HTTP_FIRST_LINE_LENGTH_LIMIT {5 * 1024}; // 5kB

using LoopPtr = std::shared_ptr<uv_loop_t>;

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

inline void RequestBuffer::handle(uv_stream_t *client, std::function<void(uv_stream_t*)> cb) {
  if (protocol_ == Protocol::HTTP) {
    http_route(with_http_request(), [&](const HttpResponsePtr& resp_ptr) {
      resp_ptr->send(client);
      // 请求处理完成后，需要清理 client_req_buffer_m_
      cb(client);
    });
    return;
  }
  spdlog::error("Illegal protocol");
}

using RequestBufferPtr = std::shared_ptr<RequestBuffer>;

// ---------------------------------------------------------------------------------------------------------------------

namespace connection {

static tsl::robin_map<uv_stream_t*, RequestBufferPtr> client_req_buffer_m_;
static std::mutex robin_mutex_;

static void clean_client_req_buffer_m(uv_stream_t* client) {
  std::lock_guard guard {robin_mutex_};
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