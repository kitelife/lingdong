#pragma once

#include "plugin.h"

#include <fstream>
#include <filesystem>

#include <spdlog/spdlog.h>

namespace ling::plugin {

using std::filesystem::path;

const static std::string typst_template = {R"TYPST_MARK(#import "@preview/cmarker:0.1.5"
#set text(font: ({}), lang: "zh", region: "cn")
#cmarker.render(read("{}")))TYPST_MARK"};

static bool is_typst_exist() {
  return system("which typst > /dev/null 2>&1") == 0;
}

static bool typst_compile(const std::string& input_file, const std::string& output_file) {
  return system(fmt::format("typst compile {} {}", input_file, output_file).c_str()) == 0;
}

static void with_public_permission(path p) {
  permissions(p, perms::owner_all | perms::group_all,
                perm_options::add);
};

class TypstCmarkerPdf final: public Plugin {
public:
  bool init(ConfigPtr config_ptr) override;
  bool run(const MarkdownPtr& md_ptr) override;
  bool destroy() override;

private:
  bool make_typst_wrapper_file();
  bool make_tmp_post_file(const MarkdownPtr& md_ptr) const;

private:
  path root_path;
  path output_dir;
  std::string text_fonts;
  path tmp_dir = "tmp_typst_build";
  std::string typst_wrapper_file_name = "md2pdf.typ";
  std::string tmp_md_file_name = "tmp_post.md";
};

inline bool TypstCmarkerPdf::make_typst_wrapper_file() {
  const auto& typst_content = fmt::format(typst_template, text_fonts, tmp_md_file_name);
  spdlog::debug("typst_content: {}", typst_content);
  std::ofstream typst_fstream(tmp_dir / typst_wrapper_file_name);
  if (!typst_fstream.is_open()) {
    spdlog::error("Failed to open file: {}", (tmp_dir/typst_wrapper_file_name).string());
    return false;
  }
  typst_fstream << typst_content;
  typst_fstream.flush();
  typst_fstream.close();
  return true;
}

inline bool TypstCmarkerPdf::make_tmp_post_file(const MarkdownPtr& md_ptr) const {
  const auto& body = md_ptr->body_part();
  std::ofstream tmp_post_fstream(tmp_dir / tmp_md_file_name, std::ios::trunc);
  if (!tmp_post_fstream.is_open()) {
    spdlog::error("Failed to open file: {}", (tmp_dir / tmp_md_file_name).string());
    return false;
  }
  tmp_post_fstream << "# " + md_ptr->metadata().title + "\n\n";
  tmp_post_fstream << body;
  tmp_post_fstream.flush();
  tmp_post_fstream.close();
  with_public_permission(tmp_dir / tmp_md_file_name);
  return true;
}


inline bool TypstCmarkerPdf::init(ConfigPtr config_ptr) {
  if (!is_typst_exist()) {
    spdlog::error("Failed to enable plugin 'TypstPdf', because typst not installed");
    return false;
  }
  output_dir = toml::find_or<std::string>(config_ptr->raw_toml_, "typst_pdf", "output_dir", "./");
  text_fonts = toml::find_or<std::string>(config_ptr->raw_toml_, "typst_pdf", "text_fonts", "");
  if (text_fonts.empty()) {
    spdlog::error("Please specify a text font for typst");
    return false;
  }
  if (!exists(tmp_dir)) {
    create_directory(tmp_dir);
    with_public_permission(tmp_dir);
  }
  make_typst_wrapper_file();
  if (!exists(output_dir)) {
    create_directory(output_dir);
    with_public_permission(output_dir);
  }
  return true;
}

inline bool TypstCmarkerPdf::run(const MarkdownPtr& md_ptr) {
  if (!make_tmp_post_file(md_ptr)) {
    return false;
  }
  const path output_file_path = output_dir / (md_ptr->metadata().id + ".pdf");
  const auto status = typst_compile(tmp_dir / typst_wrapper_file_name, output_file_path.string());
  if (!status) {
    spdlog::error("Failed to generate pdf for post: {}", md_ptr->metadata().id);
  }
  return status;
}

inline bool TypstCmarkerPdf::destroy() {
  if (exists(tmp_dir / tmp_md_file_name)) {
    std::filesystem::remove(tmp_dir / tmp_md_file_name);
  }
  if (exists(tmp_dir / typst_wrapper_file_name)) {
    std::filesystem::remove(tmp_dir / typst_wrapper_file_name);
  }
  if (exists(tmp_dir)) {
    remove_all(tmp_dir);
  }
  return true;
}

static PluginRegister<TypstCmarkerPdf> typst_cmarker_pdf_register_ {"TypstCmarkerPdf"};

}