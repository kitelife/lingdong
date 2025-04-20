#pragma once

#include <iostream>

#include <spdlog/spdlog.h>
#include <uv.h>
#include <gflags/gflags.h>

#include "config.hpp"
#include "server.hpp"

DEFINE_uint32(port, 8000, "server port to listen");

namespace ling {

static int DEFAULT_BACKLOG = 128;

class Server final {
public:
  explicit Server(ConfigPtr conf): conf_(std::move(conf)) {}
  bool start();

private:
  ConfigPtr conf_;
  std::shared_ptr<uv_loop_t> loop_;
  std::shared_ptr<uv_tcp_t> tcp_server_;
  std::function<void(uv_stream_t*, int)> on_new_connection_;
};

using ServerPtr = std::shared_ptr<Server>;

inline bool Server::start() {
  this->loop_.reset(uv_default_loop());
  uv_tcp_init(loop_.get(), tcp_server_.get());
  //
  sockaddr_in addr{};
  uv_ip4_addr("0.0.0.0", static_cast<int>(FLAGS_port), &addr);
  //
  uv_tcp_bind(tcp_server_.get(), reinterpret_cast<const sockaddr*>(&addr), 0);
  //
  this->on_new_connection_ = [this](uv_stream_t* server, int status) {
  };
  //
  int r = uv_listen(reinterpret_cast<uv_stream_t*>(tcp_server_.get()), DEFAULT_BACKLOG,
    this->on_new_connection_.target<void(uv_stream_t*, int)>());
  if (r) {
    spdlog::error("listen failed: {}", uv_strerror(r));
    return false;
  }
  return uv_run(this->loop_.get(), UV_RUN_DEFAULT);
}

}