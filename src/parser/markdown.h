
//
// Created by xiayf on 2025/3/28.
//

#pragma once

/*
- https://daringfireball.net/projects/markdown/syntax
- https://www.markdownguide.org/basic-syntax
 */

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <absl/strings/str_join.h>
#include <inja/inja.hpp>

namespace ling {

using StrPair = std::pair<std::string, std::string>;

class PostMetadata final {
public:
  std::string id;
  std::string title;
  std::string publish_date;
  std::vector<std::string> tags;
};

class ParseResult {
public:
  ParseResult() = default;
  int8_t status = 0; // 0 - ok, 1 - not, 2 - illegal
  size_t next_line_idx = -1;

  static ParseResult make(int8_t status, size_t next_line_idx) {
    ParseResult pr;
    pr.status = status;
    pr.next_line_idx = next_line_idx;
    return pr;
  }
};

class LineParseResult {
public:
  int8_t status = 0;  // 0 - ok, 1 - not, 2 - illegal
  size_t next_pos = -1;
};

class ItemList;

class Element {
public:
  virtual std::string to_html() = 0;
  virtual void clear() {}
  virtual ~Element() = default;
};

class HtmlElement final : public Element {
public:
  std::string tag_name;
  std::string html;
public:
  HtmlElement() = default;
  std::string to_html() override {
    return html;
  }
};

class Heading final : public Element {
public:
  size_t level_;
  std::string title_;

public:
  Heading(const size_t level, std::string title) : level_(level), title_(std::move(title)) {}
  std::string to_html() override;
};

enum class FragmentType {
  UNKNOWN,

  PLAIN,

  INLINE_CODE,
  LATEX,
  LINK,
  FOOT_NOTE_REF,
};

class InlineFragment : public Element {
public:
  FragmentType type_ = FragmentType::UNKNOWN;

  explicit InlineFragment(const FragmentType type) : type_(type) {}
};

class InlineFootnoteRef final : public InlineFragment {
public:
  explicit InlineFootnoteRef(std::string id): InlineFragment(FragmentType::FOOT_NOTE_REF), id_(std::move(id)) {}
  std::string to_html() override;

private:
  std::string id_;
};

class InlineCode final : public InlineFragment {
public:
  explicit InlineCode(std::string code = "") : InlineFragment(FragmentType::INLINE_CODE), code_(std::move(code)) {
    code_ = inja::htmlescape(code_);
  }
  std::string to_html() override;

  void set_code(std::string code) {
    code_ = std::move(code);
  }

private:
  std::string code_;
};

class InlineLatex final : public InlineFragment {
public:
  explicit InlineLatex(std::string math_text) : InlineFragment(FragmentType::LATEX), math_text_(std::move(math_text)) {}
  std::string to_html() override;

private:
  std::string math_text_;
};

class InlineLink final : public InlineFragment {
public:
  explicit InlineLink(std::string text, std::string uri)
      : InlineFragment(FragmentType::LINK), text_(std::move(text)), uri_(std::move(uri)) {}
  std::string to_html() override;

private:
  std::string text_;
  std::string uri_;
};

class Text final : public InlineFragment {
public:
  explicit Text(FragmentType t, std::string text) : InlineFragment(t), text_(std::move(text)) {}
  std::string to_html() override;

private:
  std::string text_;
};

class Paragraph final : public Element {
public:
  explicit Paragraph(const bool unwrap_html = false): unwrap_html_(unwrap_html) {}
  [[nodiscard]] std::string to_text() const;
  std::string to_html() override;

  // left / center / right / justify
  std::string text_align = "justify";
  bool unwrap_html_;
  std::vector<std::shared_ptr<InlineFragment>> blocks;
};

class BlockQuote final : public Element {
public:
  std::vector<std::shared_ptr<Element>> elements_;

  std::string to_html() override;
  void clear() override;
};

class Item final : public Element {
public:
  std::shared_ptr<Paragraph> paragraph_ptr = std::make_shared<Paragraph>(true);
  std::shared_ptr<ItemList> child;

  std::string to_html() override;

  static bool is_ordered_item(absl::string_view s);
};

class ItemList : public Element {
public:
  bool is_ordered = false;
  int8_t level = 0;
  std::vector<Item> items;

  std::string to_html() override;
};

class CodeBlock final : public Element {
public:
  std::string lang_name;
  /*
   * 属性键值对
   * 属性之间以空格分隔，键值以冒号分隔
   */
  std::vector<StrPair> attrs;
  std::vector<std::string> lines;

public:
  std::string to_html() override;
};

class LatexBlock final : public Element {
public:
  std::string content;

public:
  std::string to_html() override;
};

class HorizontalRule final : public Element {
public:
  std::string to_html() override;
};

class Link final : public Element {
public:
  std::string title;
  std::string uri;

  std::string to_html() override;
};

class Image final : public Element {  // 图片单独成行
public:
  std::string alt_text;
  std::string uri;
  //
  std::string width = "100%";

  std::string to_html() override;
};

using ParagraphPtr = std::shared_ptr<Paragraph>;
using ItemListPtr = std::shared_ptr<ItemList>;

class Footnote final : public Element {
public:
  std::string id_;
  ParagraphPtr p_ptr_;

public:
  Footnote(std::string id, ParagraphPtr p_ptr) : id_(std::move(id)), p_ptr_(std::move(p_ptr)) {}
  std::string to_html() override;
};

using FootnotePtr = std::shared_ptr<Footnote>;

class Footnotes final : public Element {
public:
  std::map<std::string, FootnotePtr> footnotes_;
  std::string to_html() override;
  //
  void add_footnote(const std::string& id, const FootnotePtr& footnote_ptr) {
    footnotes_[id] = footnote_ptr;
  };
};

// https://markdownguide.offshoot.io/extended-syntax/#tables
enum class AlignmentType {
  LEFT, RIGHT, CENTER
};

static std::string to_string(const AlignmentType alignment) {
  switch (alignment) {
      case AlignmentType::RIGHT: return "right";
      case AlignmentType::CENTER: return "center";
      default: return "left";
  }
}

class Table final : public Element {
public:
  std::vector<std::string> col_title_vec;
  std::vector<AlignmentType> col_alignment_vec;
  std::vector<std::vector<ParagraphPtr>> col_row_vec;

public:
  std::string to_html() override;
};

class Markdown final {
public:
  Markdown() = default;
  bool parse_str(const std::string& md_content);
  bool parse_file(const std::string& md_file_path);
  std::string body_part();
  std::string to_html();
  void clear();

  std::vector<std::shared_ptr<Element>>& elements() {
    return elements_;
  }

  PostMetadata& metadata() {
    return metadata_;
  }

private:
  bool parse();
  ParseResult parse_metadata();
  ParseResult parse_heading();
  ParseResult parse_blockquote();
  ParseResult parse_codeblock();
  ParseResult parse_latex();
  //
  ParseResult parse_paragraph();
  static bool parse_paragraph(const std::string& line, const ParagraphPtr& paragraph_ptr);

  ParseResult parse_html_element();
  ParseResult parse_table();
  //
  ParseResult parse_dash_prefix_line();
  ParseResult parse_itemlist();
  ParseResult parse_itemlist(int8_t level, size_t blank_prefix_length, const ItemListPtr& child);
  ParseResult parse_ordered_itemlist();
  ParseResult parse_ordered_itemlist(int8_t level, size_t blank_prefix_length, const ItemListPtr& child);

  ParseResult parse_horizontal_rule();
  ParseResult parse_image();
  //
  ParseResult parse_footnote();
  //
  ParseResult parse_default();

  static LineParseResult try_parse_code(const absl::string_view& line, size_t start,
    const ParagraphPtr& paragraph_ptr);
  static LineParseResult try_parse_inline_latex(const absl::string_view& line, size_t start,
    const ParagraphPtr& paragraph_ptr);
  static LineParseResult try_parse_link(const absl::string_view& line, size_t start,
    const ParagraphPtr& paragraph_ptr);
  static LineParseResult try_parse_footnote_ref(const absl::string_view& line, size_t start,
    const ParagraphPtr& paragraph_ptr);
  static LineParseResult try_parse_text(const absl::string_view& line, size_t start,
    const ParagraphPtr& paragraph_ptr);

private:
  std::shared_ptr<std::istream> stream_ptr_;
  //
  std::vector<std::string> lines;
  size_t body_start_line_idx = 0;
  size_t last_line_idx = 0;
  //
  PostMetadata metadata_;
  std::vector<std::shared_ptr<Element>> elements_;
  std::shared_ptr<Footnotes> footnotes_ptr_ = std::make_shared<Footnotes>();
};

using MarkdownPtr = std::shared_ptr<Markdown>;

}  // namespace ling
