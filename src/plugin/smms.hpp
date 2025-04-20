#pragma once
/*
 * 自动将本地图片上传到 https://smms.app/，并替换页面中的图片链接
 * SM.MS API： https://doc.sm.ms/
 */

#include "plugin.h"

#include "../config.hpp"

namespace ling::plugin {

class Smms final : public Plugin {
public:
  explicit Smms(const ConfigPtr& config) : config_(config) {}
  bool run(const ParserPtr& parser_ptr) override;

private:
  ConfigPtr config_;
};

inline bool Smms::run(const ParserPtr& parser_ptr) {
  return true;
}
}