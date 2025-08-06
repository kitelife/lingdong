#pragma once

#include <gflags/gflags.h>
#include <spdlog/spdlog.h>
#include <uv.h>

#include "config.hpp"
#include "server.hpp"

DEFINE_uint32(port, 8000, "server port to listen");

namespace ling {

static int DEFAULT_BACKLOG = 128;

using LoopPtr = std::shared_ptr<uv_loop_t>;
using NewConnectionCallback = void(uv_stream_t*, int);
using ReadCallBack = void(uv_stream_t *client, ssize_t nread, const uv_buf_t *buf);

typedef struct {
  uv_write_t req;
  uv_buf_t buf;
} write_req_t;

void alloc_buffer(uv_handle_t *handle, size_t suggested_size, uv_buf_t *buf) {
  buf->base = (char*) malloc(suggested_size);
  buf->len = suggested_size;
}

void on_close(uv_handle_t* handle) {
  free(handle);
}

void free_write_req(uv_write_t *req) {
  write_req_t *wr = (write_req_t*) req;
  free(wr->buf.base);
  free(wr);
}

void echo_write(uv_write_t *req, int status) {
  if (status) {
    fprintf(stderr, "Write error %s\n", uv_strerror(status));
  }
  free_write_req(req);
}

//--------------------------------------------------------------------------------

enum class ConnState {
  READY,
  RUNNING,
  FINISHED,
};

class Connection final {
public:
  Connection(const LoopPtr& loop, uv_stream_t* stream): loop_(loop), stream_(stream) {};
  //
  ConnState state() const {
    return state_;
  }
  void reset(const LoopPtr& loop, uv_stream_t* stream) {
    loop_ = loop;
    stream_ = stream;
    state_ = ConnState::READY;
  }
  //
  bool handle();
  ~Connection();

private:
  LoopPtr loop_;
  uv_stream_t* stream_;
  std::shared_ptr<uv_tcp_t> client_;
  //
  std::function<ReadCallBack> on_read_;
  //
  ConnState state_ = ConnState::READY;
};

inline bool Connection::handle() {
  state_ = ConnState::RUNNING;
  on_read_ = [this](uv_stream_t* client, ssize_t nread, const uv_buf_t *buf) {
    if (nread > 0) {
      write_req_t *req = (write_req_t*) malloc(sizeof(write_req_t));
      req->buf = uv_buf_init(buf->base, nread);
      uv_write((uv_write_t*) req, client, &req->buf, 1, echo_write);
      return;
    }
    if (nread < 0) {
      if (nread != UV_EOF)
        fprintf(stderr, "Read error %s\n", uv_err_name(nread));
      uv_close((uv_handle_t*) client, on_close);
    }
    free(buf->base);
  };
  //
  client_ = std::make_shared<uv_tcp_t>();
  uv_tcp_init(loop_.get(), client_.get());
  if (uv_accept(stream_, reinterpret_cast<uv_stream_t*>(client_.get())) == 0) {
    uv_read_start(reinterpret_cast<uv_stream_t*>(client_.get()), alloc_buffer, this->on_read_.target<ReadCallBack>());
  } else {
    state_ = ConnState::FINISHED;
    return false;
  }
  return true;
}

inline Connection::~Connection() {

}

using ConnectionPtr = std::shared_ptr<Connection>;

//-----------------------------------------------------------------------

class Server final {
public:
  Server() = default;
  bool start();

private:
  ConfigPtr conf_;
  LoopPtr loop_;
  std::shared_ptr<uv_tcp_t> tcp_server_;
  std::function<void(uv_stream_t*, int)> on_new_connection_;
  //
  std::vector<ConnectionPtr> connections_;
  std::mutex conn_mutex_;
};

using ServerPtr = std::shared_ptr<Server>;

inline bool Server::start() {
  this->loop_.reset(uv_default_loop());
  uv_tcp_init(this->loop_.get(), this->tcp_server_.get());
  //
  sockaddr_in addr{};
  uv_ip4_addr("0.0.0.0", static_cast<int>(FLAGS_port), &addr);
  //
  uv_tcp_bind(this->tcp_server_.get(), reinterpret_cast<const sockaddr*>(&addr), 0);
  //
  this->on_new_connection_ = [this](uv_stream_t* server, int status) {
    if (status != 0) {
      spdlog::error("New connection failed: {}", status);
      return;
    }
    ConnectionPtr conn_ptr;
    {
      std::lock_guard lck(conn_mutex_);
      for (const auto& connection : this->connections_) {
        if (connection->state() == ConnState::FINISHED) {
          conn_ptr = connection;
        }
      }
      if (conn_ptr == nullptr) {
        conn_ptr = std::make_shared<Connection>(loop_, server);
        this->connections_.push_back(conn_ptr);
      } else {
        conn_ptr->reset(loop_, server);
      }
    }
    if (!conn_ptr->handle()) {
      spdlog::error("Failed to handle connection.");
    }
  };
  //
  int r = uv_listen(reinterpret_cast<uv_stream_t*>(tcp_server_.get()), DEFAULT_BACKLOG,
    this->on_new_connection_.target<NewConnectionCallback>());
  if (r) {
    spdlog::error("listen failed: {}", uv_strerror(r));
    return false;
  }
  return uv_run(this->loop_.get(), UV_RUN_DEFAULT);
}

}