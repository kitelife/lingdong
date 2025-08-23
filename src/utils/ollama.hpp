#pragma once

#include <string>
#include <utility>
#include <vector>
#include <chrono>

#include "cpr/cpr.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

namespace ling::utils {

using Embeddings = std::vector<std::vector<float>>;

// https://github.com/ollama/ollama

class Ollama {
public:
  explicit Ollama(std::string model_name): model_name_(std::move(model_name)) {}
  Embeddings generate_embeddings(std::vector<std::string>& inputs);
  //
  static bool is_service_running();

private:
  std::string model_name_;
  static std::string HOST_;
  static std::string ENDPOINT_GENERATE_EMBEDDINGS_;
};

std::string Ollama::HOST_ = "http://localhost:11434";
std::string Ollama::ENDPOINT_GENERATE_EMBEDDINGS_ = "/api/embed";

// https://github.com/ollama/ollama/blob/main/docs/api.md#generate-embeddings

Embeddings Ollama::generate_embeddings(std::vector<std::string> &inputs) {
  nlohmann::json j {};
  j["model"] = model_name_;
  j["input"] = inputs;
  auto start_tp = std::chrono::steady_clock::now();
  auto r = cpr::Post(cpr::Url{HOST_+ENDPOINT_GENERATE_EMBEDDINGS_},
                     cpr::Body(j.dump()),
                     cpr::Header{{"Content-Type", "application/json"}});
  auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-start_tp).count();
  spdlog::info("call {} to generate embeddings, elapsed {} ms, input size: {}", model_name_, elapsed_ms, inputs.size());
  if (r.status_code != 200) {
    spdlog::error("failure to generate embeddings with model {}, status code: {}, resp: {}", model_name_, r.status_code,r.text);
    return {};
  }
  auto rj = nlohmann::json::parse(r.text);
  if (!rj.contains("embeddings")) {
    spdlog::error("illegal resp: {}", r.text);
    return {};
  }
  return rj["embeddings"].get<std::vector<std::vector<float>>>();
}

bool Ollama::is_service_running() {
  auto r = cpr::Get(cpr::Url{HOST_+"/api/version"}, cpr::Timeout{std::chrono::milliseconds (500)},
                    cpr::ConnectTimeout{std::chrono::milliseconds (200)});
  return r.error.code == cpr::ErrorCode::OK;
}

}