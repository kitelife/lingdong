#pragma once
/*
 * 自动将本地图片上传到 https://smms.app/，并替换页面中的图片链接
 * SM.MS API： https://doc.sm.ms/
 */

#include "plugin.h"

namespace ling::plugin {

class Smms final : public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const ParserPtr& parser_ptr) override;

private:
  ConfigPtr config_;
};

inline bool Smms::init(ConfigPtr config_ptr) {
  inited_ = true;
  return true;
}


inline bool Smms::run(const ParserPtr& parser_ptr) {
  return true;
}

static PluginRegister<Smms> smms_register_ {"Smms"};

}