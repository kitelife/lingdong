#pragma once

#include <string>
#include <utility>
#include <vector>
#include <chrono>

#include "cpr/cpr.h"
#include "nlohmann/json.hpp"
#include "spdlog/spdlog.h"

#include "utils/helper.hpp"

namespace ling::utils {

// https://github.com/ollama/ollama

class Ollama {
public:
  explicit Ollama(std::string model_name): model_name_(std::move(model_name)) {}
  Embeddings generate_embeddings(std::vector<std::string>& inputs);
  std::string prompt_generate_with_image(const std::string& prompt, const std::string& image_base64);
  //
  static bool is_service_running();
  static std::vector<std::string> list_local_models();
  bool is_model_serving();

private:
  std::string model_name_;
  static std::string HOST_;
  static std::string ENDPOINT_GENERATE_EMBEDDINGS_;
  static std::string ENDPOINT_PROMPT_GENERATE_;
};

std::string Ollama::HOST_ = "http://localhost:11434";
std::string Ollama::ENDPOINT_GENERATE_EMBEDDINGS_ = "/api/embed";
std::string Ollama::ENDPOINT_PROMPT_GENERATE_ = "/api/generate";

// https://github.com/ollama/ollama/blob/main/docs/api.md#generate-embeddings

inline Embeddings Ollama::generate_embeddings(std::vector<std::string> &inputs) {
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
  return rj["embeddings"].get<Embeddings>();
}

inline std::string Ollama::prompt_generate_with_image(const std::string& prompt, const std::string& image_base64) {
  nlohmann::json rb {};
  rb["model"] = model_name_;
  rb["prompt"] = prompt;
  rb["stream"] = false;
  auto images = nlohmann::json::array();
  images.push_back(image_base64);
  rb["images"] = images;
  auto r = cpr::Post(cpr::Url{HOST_+ENDPOINT_PROMPT_GENERATE_},
    cpr::Body(rb.dump()),
    cpr::Header{{"Content-Type", "application/json"}});
  if (r.status_code != 200) {
    spdlog::error("failure to call model '{}', code: {}, resp: {}", model_name_, r.status_code, r.text);
    return "";
  }
  auto rj = nlohmann::json::parse(r.text);
  if (!rj.contains("response")) {
    return "";
  }
  return rj["response"].get<std::string>();
}

inline bool Ollama::is_service_running() {
  auto r = cpr::Get(cpr::Url{HOST_+"/api/version"}, cpr::Timeout{std::chrono::milliseconds (500)},
                    cpr::ConnectTimeout{std::chrono::milliseconds (200)});
  return r.error.code == cpr::ErrorCode::OK;
}

// https://github.com/ollama/ollama/blob/main/docs/api.md#list-local-models

inline std::vector<std::string> Ollama::list_local_models() {
  auto r = cpr::Get(cpr::Url{HOST_+"/api/tags"},
                    cpr::Timeout{std::chrono::milliseconds(500)},
                    cpr::ConnectTimeout{std::chrono::milliseconds(200)});
  if (r.status_code != 200) {
    spdlog::error("failure to list local models");
    return {};
  }
  nlohmann::json j = nlohmann::json::parse(r.text);
  if (!j.contains("models") || !j["models"].is_array()) {
    spdlog::error("illegal resp: {}", r.text);
    return {};
  }
  std::vector<std::string> models;
  for (auto& m : j["models"]) {
    if (m.contains("model")) {
      models.emplace_back(m["model"].get<std::string>());
    }
  }
  return models;
}

inline bool Ollama::is_model_serving() {
  if (!Ollama::is_service_running()) {
    return false;
  }
  auto models = list_local_models();
  return std::any_of(models.begin(), models.end(), [&](const std::string& model) {
    return model == model_name_;
  });
}

}