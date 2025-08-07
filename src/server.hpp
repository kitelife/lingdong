#pragma once

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <uv.h>
#include <tsl/robin_map.h>

#include "context.hpp"
#include "server.hpp"
#include "service/protocol.h"
#include "service/http/protocol.hpp"
#include "service/http/router.hpp"

DEFINE_string(host, "127.0.0.1", "server host to listen");
DEFINE_uint32(port, 8000, "server port to listen");

namespace ling {

static int DEFAULT_BACKLOG = 128;
static size_t REQUEST_SIZE_LIMIT {10 * 1024 * 1024}; // 10MB

using LoopPtr = std::shared_ptr<uv_loop_t>;

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
  http::HttpRequest& with_http_request() {
    return http_req;
  }
  [[nodiscard]] std::pair<char*, size_t> with_buffer() const {
    return std::make_pair(buffer_, size_);
  }

private:
  char* buffer_ = nullptr;
  size_t size_ = 0;
  //
  Protocol protocol_ = Protocol::UNKNOWN;
  http::HttpRequest http_req;
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
    auto [buffer_ptr, buffer_size] = with_buffer();
    // HTTP 协议探测
    if (http::probe(buffer_ptr, buffer_size, with_http_request()) == ParseStatus::INVALID) {
      protocol_ = Protocol::INVALID;
      return ParseStatus::INVALID;
    }
    if (with_http_request().valid) {
      protocol_ = Protocol::HTTP;
    } else {
      return ParseStatus::CONTINUE;
    }
  }
  if (protocol_ == Protocol::HTTP) { // 正式解析 HTTP 协议
    return with_http_request().parse(buffer_, size_);
  }
  return ParseStatus::CONTINUE;
}

inline void RequestBuffer::handle(uv_stream_t *client, std::function<void(uv_stream_t*)> cb) {
  if (protocol_ == Protocol::HTTP) {
    http::http_route(with_http_request(), [&](const http::HttpResponsePtr& resp_ptr) {
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

// TODO: 确保增删改并发安全
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
      // TODO: 异步并发处理
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