#pragma once

#include <future>

#include <spdlog/spdlog.h>
#include <uv.h>

#include "service/protocol.h"

#include "service/http/protocol.hpp"
#include "service/http/router.hpp"

#include "utils/guard.hpp"
#include "utils/executor.hpp"

namespace ling::server {

static int DEFAULT_BACKLOG = 128;
static size_t REQUEST_SIZE_LIMIT {10 * 1024 * 1024}; // 10MB

using LoopPtr = std::shared_ptr<uv_loop_t>;
static LoopPtr loop_;

enum class RequestBufferStage {
  INIT,
  PARSING,
  HANDLING,
  COMPLETE,
};

class RequestBuffer;
using RequestBufferPtr = std::shared_ptr<RequestBuffer>;

class RequestBufferManager {
public:
  static RequestBufferManager& instance() {
    static RequestBufferManager rbm;
    return rbm;
  }

  RequestBufferManager();
  ~RequestBufferManager() {
    exit_flag_ = true;
    cleanup_future_.wait_for(std::chrono::seconds(5));
  }

  RequestBufferPtr with_req_buffer(const std::string& k) {
    if (request_buffers_.count(k) > 0) {
      return request_buffers_.at(k);
    }
    std::lock_guard lock(mutex_);
    if (request_buffers_.count(k) > 0) {
      return request_buffers_.at(k);
    }
    request_buffers_[k] = std::make_shared<RequestBuffer>();
    return request_buffers_.at(k);
  }

  bool free_req_buffer(const std::string& k) {
    if (request_buffers_.count(k) > 0) {
      std::lock_guard lock(mutex_);
      request_buffers_.erase(k);
      return true;
    }
    return false;
  }

private:
  std::mutex mutex_;
  std::unordered_map<std::string, RequestBufferPtr> request_buffers_;
  std::future<void> cleanup_future_;
  std::atomic_bool exit_flag_ {false};
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
  void handle(uv_stream_t *client);
  std::chrono::time_point<std::chrono::system_clock>& last_fill_time() {
    return last_fill_time_;
  }

private:
  ParseStatus try_parse();
  http::HttpRequest& with_http_request() {
    return http_req_;
  }
  [[nodiscard]] std::pair<char*, size_t> with_buffer() const {
    return std::make_pair(buffer_, size_);
  }

private:
  char* buffer_ = nullptr;
  size_t size_ = 0;
  RequestBufferStage stage_ = RequestBufferStage::INIT;
  std::chrono::time_point<std::chrono::system_clock> last_fill_time_ = std::chrono::system_clock::now();
  Protocol protocol_ = Protocol::UNKNOWN;
  http::HttpRequest http_req_;
};

inline ParseStatus RequestBuffer::accept(const uv_buf_t* buf, ssize_t nread) {
  // 安全防护
  if (size_ + nread >= REQUEST_SIZE_LIMIT) {
    spdlog::warn("request size exceed limit!");
    return ParseStatus::INVALID;
  }
  // TODO: 待优化，参考 brpc IOBuf
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
  last_fill_time_ = std::chrono::system_clock::now();
  //
  return try_parse();
}

inline ParseStatus RequestBuffer::try_parse() {
  stage_ = RequestBufferStage::PARSING;
  //
  if (size_ <= 0 || buffer_ == nullptr) {
    return ParseStatus::INVALID;
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
      return ParseStatus::INVALID;
    }
  }
  if (protocol_ == Protocol::HTTP) { // 正式解析 HTTP 协议
    return with_http_request().parse(buffer_, size_);
  }
  return ParseStatus::INVALID;
}

static bool parse_peer_info(uv_stream_t* client, std::pair<std::string, int>& peer) {
  int peer_addr_len = sizeof(sockaddr);
  auto* peer_addr = static_cast<sockaddr*>(malloc(peer_addr_len));
  utils::MemoryGuard memory_guard {peer_addr};
  auto err_code = uv_tcp_getpeername(reinterpret_cast<uv_tcp_t*>(client), peer_addr, &peer_addr_len);
  if (err_code != 0) {
    return false;
  }
  char host[NI_MAXHOST], service[NI_MAXSERV];
  if (getnameinfo(peer_addr, peer_addr_len, host, NI_MAXHOST, service, NI_MAXSERV, NI_NUMERICSERV) == 0) {
    peer.first = std::string(host);
    peer.second = std::strtol(service, nullptr, 10);
    return true;
  }
  return false;
}

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

static void clean_after_send(uv_write_t* req, int status) {
  if (status) {
    fprintf(stderr, "Write error %s\n", uv_strerror(status));
  }
  write_req_t* wr = reinterpret_cast<write_req_t*>(req);
  free(wr->buf.base);
  free(wr);
}

struct ClientResp {
  write_req_t* write_req;
  uv_stream_t* client;

  ClientResp(write_req_t* w, uv_stream_t* c) : write_req(w), client(c) {}
};

static void write_resp(uv_work_t* work) {
  auto client_resp = static_cast<ClientResp*>(work->data);
  // client 和 buf 由 uv_write 回收内存？
  // clean_after_send 负责回收 work-> data 和 work 的内存
  uv_write(reinterpret_cast<uv_write_t*>(client_resp->write_req), client_resp->client, &client_resp->write_req->buf,
    1, clean_after_send);
}

static void after_write_resp(uv_work_t* w, int status) {
  delete static_cast<ClientResp*>(w->data);
  delete w;
}

inline void RequestBuffer::handle(uv_stream_t *client) {
  stage_ = RequestBufferStage::HANDLING;
  if (protocol_ == Protocol::HTTP) {
    auto& http_req = with_http_request();
    if (!parse_peer_info(client, http_req.from)) {
      spdlog::warn("failed to parse peer info");
    }
    http_req.handle([client](char* resp, size_t resp_size) {
      auto req = static_cast<write_req_t*>(malloc(sizeof(write_req_t)));
      req->buf = uv_buf_init(resp, resp_size);
      //
      auto* work = new uv_work_t();
      work->data = new ClientResp(req, client);
      // 排队由 loop_ 来统一处理
      uv_queue_work(loop_.get(), work, write_resp, after_write_resp);
    });
    stage_ = RequestBufferStage::COMPLETE;
    RequestBufferManager::instance().free_req_buffer(http_req.from.first+":"+std::to_string(http_req.from.second)); //
    return;
  }
  spdlog::error("Illegal protocol");
}

inline RequestBufferManager::RequestBufferManager() {
  cleanup_future_ = std::async(std::launch::async, [&]() {
      while (!exit_flag_) {
        if (request_buffers_.empty()) {
          std::this_thread::sleep_for(std::chrono::seconds(30));
        }
        std::vector<std::string> keys_to_cleanup;
        auto now = std::chrono::system_clock::now();
        for (const auto& [k, v] : request_buffers_) {
          if ((now - v->last_fill_time()) > std::chrono::seconds(300)) {
            keys_to_cleanup.emplace_back(k);
          }
        }
        if (keys_to_cleanup.empty()) {
          std::this_thread::sleep_for(std::chrono::seconds(30));
          continue;
        }
        std::lock_guard lock{mutex_};
        for (const auto& k : keys_to_cleanup) {
          request_buffers_.erase(k);
        }
      }
    });
}

// ---------------------------------------------------------------------------------------------------------------------

namespace connection {

static void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = static_cast<char*>(malloc(suggested_size));
  buf->len = suggested_size;
}

static void on_close(uv_handle_t* handle) {
  free(handle);
}

static void on_read(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf) {
  if (nread > 0) {
    // accept 需要将 buf 的内容拷贝一份
    // 返回 true，则表示请求体已接收完整
    std::pair<std::string, int> peer_info;
    if (parse_peer_info(client, peer_info)) {
      auto peer = peer_info.first + ":" + std::to_string(peer_info.second);
      auto req_buffer = RequestBufferManager::instance().with_req_buffer(peer);
      auto status = req_buffer->accept(buf, nread);
      if (status == ParseStatus::COMPLETE) { // 请求完整了
        utils::default_executor().async_execute([client, req_buffer]() {
          req_buffer->handle(client);
        });
      } else if (status == ParseStatus::INVALID) { // 不合法的请求
        spdlog::error("Illegal request");
      } else {} // 还没接收完成的请求
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

static void cleanup_before_exit() {
  utils::default_executor().join();
}

static bool start_server(const std::string& host, int port) {
  loop_.reset(uv_default_loop());
  tcp_server_ = std::make_shared<uv_tcp_t>();
  uv_tcp_init(loop_.get(), tcp_server_.get());
  //
  sockaddr_in addr{};
  uv_ip4_addr(host.c_str(), port, &addr);
  //
  uv_tcp_bind(tcp_server_.get(), reinterpret_cast<const sockaddr*>(&addr), 0);
  //
  int r = uv_listen(reinterpret_cast<uv_stream_t*>(tcp_server_.get()), DEFAULT_BACKLOG, on_new_connection);
  if (r) {
    spdlog::error("listen failed: {}", uv_strerror(r));
    return false;
  }
  bool ret_status = uv_run(loop_.get(), UV_RUN_DEFAULT);
  cleanup_before_exit();
  return ret_status;
}

}