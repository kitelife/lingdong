#include "gtest/gtest.h"
#include "absl/strings/str_join.h"

#include "ollama.hpp"

using ling::utils::Ollama;

void call_ollama_generate_embeddings(Ollama& oll, std::vector<std::string>& inputs) {
  auto embs = oll.generate_embeddings(inputs);
  EXPECT_EQ(embs.size(), inputs.size());
  for (size_t idx = 0; idx < embs.size(); idx++) {
    std::vector<std::string> sv;
    std::transform(embs[idx].begin(), embs[idx].end(), std::back_inserter(sv), [](float v) -> std::string {
        return std::to_string(v);
    });
    std::cout << "\"" << inputs[idx] << "\" 的向量长度为：" << embs[idx].size()
              << "，表征为：[" << absl::StrJoin(sv, ",") << "]" << std::endl;
  }
}

TEST(OllamaTest, test_generate_embeddings) {
  if (!Ollama::is_service_running()) {
    spdlog::info("ollama is not running, skip test");
    return;
  }
  //
  std::vector<std::string> inputs;
  inputs.emplace_back("欢迎来上海");
  inputs.emplace_back("我是一个程序员");
  std::vector<std::string> inputs2;
  inputs2.emplace_back("安徽人住在上海");
  inputs2.emplace_back("黄浦江边景色美丽");
  //
  Ollama nomic_embed_text_oll {"nomic-embed-text:latest"};
  if (nomic_embed_text_oll.is_model_serving()) {
    call_ollama_generate_embeddings(nomic_embed_text_oll, inputs);
    call_ollama_generate_embeddings(nomic_embed_text_oll, inputs2);
  }
  Ollama qwen3_4b_oll {"qwen3:4b"};
  if (qwen3_4b_oll.is_model_serving()) {
    call_ollama_generate_embeddings(qwen3_4b_oll, inputs);
    call_ollama_generate_embeddings(qwen3_4b_oll, inputs2);
  }
  Ollama mxbai_embed_large_oll {"mxbai-embed-large:latest"};
  if (mxbai_embed_large_oll.is_model_serving()) {
    call_ollama_generate_embeddings(mxbai_embed_large_oll, inputs);
    call_ollama_generate_embeddings(mxbai_embed_large_oll, inputs2);
  }
}