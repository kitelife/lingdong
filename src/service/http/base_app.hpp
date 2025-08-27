#pragma once

#include "spdlog/spdlog.h"

#include "service/http/protocol.hpp"
#include "service/server.hpp"

namespace ling::http {

class BaseApp {
public:
  BaseApp() = default;
  virtual ~BaseApp() = default;
  void start();

protected:
  virtual bool prepare() = 0;
  virtual std::pair<std::string, int> host_port() = 0;
  virtual std::unique_ptr<Router> router() = 0;
};

inline void BaseApp::start() {
  if (!prepare()) {
    spdlog::error("failure to prepare!");
    return;
  }
  http::router = router();
  auto [host, port] = host_port();
  spdlog::info("run http server on {}:{}", host, port);
  server::start_server(host, port);
}

}