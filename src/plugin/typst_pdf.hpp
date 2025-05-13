#pragma once

#include "plugin.h"

namespace ling::plugin {

class TypstPdf final: public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const ParserPtr& parser_ptr) override;
};

inline bool TypstPdf::init(ConfigPtr config_ptr) {
  return true;
}

inline bool TypstPdf::run(const ParserPtr& parser_ptr) {
  return true;
}

static PluginRegister<TypstPdf> typst_pdf_plugin_register_ {"TypstPdf"};
}