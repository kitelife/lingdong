#pragma once

#include <csignal>
#include <iostream>
#include <thread>

#include <spdlog/spdlog.h>

#include "config.hpp"
#include "server.hpp"

namespace ling {
inline volatile std::sig_atomic_t g_signal_status;
inline void signal_handler(const int signal) {
  g_signal_status = signal;
}

class Server final {
public:
  explicit Server(ConfigPtr conf): conf_(std::move(conf)) {}
  bool start();

private:
  ConfigPtr conf_;
};

using ServerPtr = std::shared_ptr<Server>;

inline bool Server::start() {
  std::signal(SIGINT, signal_handler);
  std::signal(SIGTERM, signal_handler);
  //
  while (g_signal_status == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    spdlog::info("server running!");
  }
  return true;
}

}