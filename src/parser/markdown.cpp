//
// Created by xiayf on 2025/3/28.
//

#include "markdown.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include <fmt/core.h>
#include <absl/strings/str_split.h>

#include "../utils/strings.hpp"
#include "../utils/time.hpp"

namespace ling {

bool Markdown::parse_str(const std::string& md_content) {
  this->stream_ptr_ = std::make_shared<std::istringstream>(md_content);
  return parse();
};

bool Markdown::parse_file(const std::string& md_file_path) {
  const auto fstream_ptr = std::make_shared<std::ifstream>(md_file_path);
  if (!fstream_ptr->is_open()) {
    return false;
  }
  this->stream_ptr_ = fstream_ptr;
  return parse();
};

bool Markdown::parse() {
  std::string line;
  while (std::getline(*stream_ptr_, line, '\n')) {
    lines.emplace_back(line);
  }
  ParseResult pr = parse_metadata();
  if (pr.status == 2) {
    return false;
  }
  last_line_idx = pr.next_line_idx;
  //
  while (last_line_idx < lines.size()) {
    switch (lines.at(last_line_idx)[0]) {
      case '#':  // Heading
        pr = parse_heading();
        break;
      case '>':  // Block Quote
        pr = parse_blockquote();
        break;
      case '`':  // Code Block
        pr = parse_codeblock();
        break;
      case '-':  // Horizontal Rule or Item List
        pr = parse_dash_prefix_line();
        break;
      case '|':  // Table
        pr = parse_table();
        break;
      case '!':  // 可能是图片
        pr = parse_image();
        break;
      case '$': // 可能是独立成行的 latex 公式
        pr = parse_latex();
        break;
      default:
        pr = parse_default();
    }
    if (pr.status != 0) {
      break;
    }
    last_line_idx = pr.next_line_idx;
  }
  return pr.status == 0;
}

/*
元信息部分的格式：

---
id: example-post
author: xiayf
date: 2024-04-15
tags: 示例, markdown, C++
---

*/
ParseResult Markdown::parse_metadata() {
  absl::string_view line_view;
  while (true) {
    line_view = utils::view_strip_empty(lines.at(last_line_idx));
    if (!line_view.empty()) {
      break;
    }
    last_line_idx++;
  }
  if (line_view != "---") {
    return ParseResult::make(1, last_line_idx);
  }
  do {
    last_line_idx++;
    line_view = utils::view_strip_empty(lines.at(last_line_idx));
    if (line_view == "---") {
      break;
    }
    size_t colon_pos = line_view.find(':');
    if (colon_pos == std::string::npos) {
      continue;
    }
    const auto k = utils::view_strip_empty(line_view.substr(0, colon_pos));
    const auto v = utils::view_strip_empty(line_view.substr(colon_pos + 1));
    if (k == "id") {
      metadata_.id = v;
    } else if (k == "title") {
      metadata_.title = v;
    } else if (k == "date") {
      metadata_.publish_date = utils::date_format_convert(v.data());
    } else if (k == "tags") {
      auto tags = absl::StrSplit(v, ',', absl::SkipWhitespace());
      for (const auto& tag : tags) {
        metadata_.tags.emplace_back(utils::view_strip_empty(tag));
      }
    } else {
      std::cerr << "Illegal metadata, k=" << k << ", v=" << v << std::endl;
    }
  } while (true);
  if (line_view != "---") {
    return ParseResult::make(2, 0);
  }
  return ParseResult::make(0, last_line_idx+1);
}

ParseResult Markdown::parse_default() {
  const auto& last_line = lines.at(last_line_idx);
  if (Item::is_ordered_item(last_line)) {
    return parse_ordered_itemlist();
  }
  return parse_paragraph();
}

ParseResult Markdown::parse_heading() {
  const auto& last_line = lines.at(last_line_idx);
  size_t level = 1;
  for (size_t idx = level; idx < last_line.size(); idx++) {
    if (last_line[idx] != '#') {
      break;
    }
    level++;
  }
  size_t title_start = level;
  while (title_start < last_line.size() && (last_line[title_start] == ' ' || last_line[title_start] == '\t')) {
    title_start++;
  }
  size_t title_end = last_line.size() - 1;
  while (title_end > title_start && (last_line[title_end] == ' ' || last_line[title_end] == '\t')) {
    title_end--;
  }
  const auto heading = std::make_shared<Heading>(level, last_line.substr(title_start, title_end - title_start + 1));
  elements_.push_back(heading);
  return ParseResult::make(0, last_line_idx + 1);
}

ParseResult Markdown::parse_blockquote() {
  size_t line_idx = last_line_idx;
  auto& last_line = lines.at(line_idx);
  //
  std::vector<std::string> block_quote_lines;
  block_quote_lines.push_back(last_line.substr(1, last_line.size() - 1));
  line_idx++;
  while (line_idx < lines.size()) {
    last_line = lines.at(line_idx);
    if (last_line[0] != '>') {
      break;
    }
    block_quote_lines.push_back(last_line.substr(1, last_line.size() - 1));
    line_idx++;
  }
  const auto block_quote = std::make_shared<BlockQuote>();
  elements_.push_back(block_quote);
  for (const auto& line : block_quote_lines) {
    auto p_ptr = std::make_shared<Paragraph>();
    block_quote->elements_.push_back(p_ptr);
    parse_paragraph(line, p_ptr);
  }
  return ParseResult::make(0, line_idx);
}

ParseResult Markdown::parse_codeblock() {
  size_t line_idx = last_line_idx;
  auto& last_line = lines.at(line_idx);
  if (last_line.size() < 3 || last_line[1] != '`' || last_line[2] != '`') {
    return parse_paragraph();
  }
  //
  std::string lang_name = last_line.substr(3, last_line.size() - 3);
  lang_name = absl::StripSuffix(lang_name, " ");
  //
  std::vector<std::string> code_lines;
  bool valid_code_block = false;
  line_idx++;
  while (line_idx < lines.size()) {
    last_line = lines.at(line_idx);
    if (last_line.size() == 3 && last_line[0] == '`' && last_line[1] == '`' && last_line[2] == '`') {
      valid_code_block = true;
      break;
    }
    code_lines.push_back(last_line);
    line_idx++;
  }
  if (!valid_code_block) {
    return ParseResult::make(2, line_idx);
  }
  const auto codeblock_ptr = std::make_shared<CodeBlock>();
  codeblock_ptr->lang_name = lang_name;
  codeblock_ptr->lines = code_lines;
  elements_.push_back(codeblock_ptr);
  return ParseResult::make(0, line_idx + 1);
}

ParseResult Markdown::parse_latex() {
  size_t line_idx = last_line_idx;
  const auto line_view = utils::view_strip_empty(lines.at(line_idx));
  if (line_view.empty()) {
    return parse_default();
  }
  size_t view_len = line_view.size();
  if (line_view[0] == '$' && line_view[1] == '$' && line_view[view_len-1] == '$' && line_view[view_len-2] == '$') {
    const auto latex_block_ptr = std::make_shared<LatexBlock>();
    latex_block_ptr->content = std::string(line_view.substr(2, view_len-4));
    elements_.push_back(latex_block_ptr);
    return ParseResult::make(0, line_idx + 1);
  }
  return parse_default();
}

ParseResult Markdown::parse_dash_prefix_line() {
  const auto& last_line = lines.at(last_line_idx);
  if (last_line.size() == 3 && last_line[0] == '-' && last_line[1] == '-' && last_line[2] == '-') {
    return parse_horizontal_rule();
  }
  return parse_itemlist();
}

ParseResult Markdown::parse_horizontal_rule() {
  elements_.push_back(std::make_shared<HorizontalRule>());
  return ParseResult::make(0, last_line_idx + 1);
}

ParseResult Markdown::parse_itemlist(int8_t level, size_t blank_prefix_length, const ItemListPtr& item_list) {
  size_t pos = blank_prefix_length;
  size_t line_idx = last_line_idx;

  bool ret_status = true;
  do {
    auto& last_line = lines.at(line_idx);
    item_list->is_ordered = false;
    item_list->items.emplace_back();
    if (const auto status = parse_paragraph(last_line.substr(pos + 1, last_line.size() - 1 - pos),
                                            item_list->items.back().paragraph_ptr);
        !status) {
      ret_status = status;
      break;
    }
    // 当前行处理 ok，则看下一行
    line_idx++;
    if (line_idx >= lines.size()) {
      break;
    }
    last_line = lines.at(line_idx);
    if (last_line.empty()) {
      break;
    }
    //
    size_t new_pos_start = 0;
    while (last_line[new_pos_start] == ' ' || last_line[new_pos_start] == '\t') {
      new_pos_start++;
    }
    //
    if (new_pos_start == blank_prefix_length) {  // 可能同层级
      if (last_line[new_pos_start] != '-') {     // 非列表项
        break;
      }
    } else if (new_pos_start > blank_prefix_length) {
      if (last_line[new_pos_start] == '-') {
        auto child_item_list = std::make_shared<ItemList>();
        item_list->items.back().child = child_item_list;
        last_line_idx = line_idx;
        auto [status, next_line_idx] = parse_itemlist(level + 1, new_pos_start, child_item_list);  // 子列表
        line_idx = next_line_idx;
      } else {  // 非列表
        break;
      }
    } else {
      break;
    }
  } while (true);
  return ParseResult::make(ret_status ? 0 : 2, line_idx);
}

ParseResult Markdown::parse_itemlist() {
  const auto item_list = std::make_shared<ItemList>();
  item_list->is_ordered = false;
  item_list->level = 1;
  const auto pr = parse_itemlist(item_list->level, 0, item_list);
  elements_.push_back(item_list);
  return pr;
}

ParseResult Markdown::parse_ordered_itemlist(int8_t level, size_t blank_prefix_length, const ItemListPtr& item_list) {
  size_t pos = blank_prefix_length;
  size_t line_idx = last_line_idx;
  bool ret_status = true;
  do {
    auto& last_line = lines.at(line_idx);
    item_list->is_ordered = true;
    item_list->items.emplace_back();
    if (const auto status = parse_paragraph(last_line.substr(pos + 2, last_line.size() - 2 - pos), item_list->items.back().paragraph_ptr); !status) {
      ret_status = status;
      break;
    }
    // 当前行处理 ok，则看下一行
    line_idx++;
    if (line_idx >= lines.size()) {
      break;
    }
    last_line = lines.at(line_idx);
    if (last_line.empty()) {
      break;
    }
    //
    size_t new_pos_start = 0;
    while (last_line[new_pos_start] == ' ' || last_line[new_pos_start] == '\t') {
      new_pos_start++;
    }
    //
    if (new_pos_start == blank_prefix_length) {  // 可能同层级
      if (!Item::is_ordered_item(absl::string_view(last_line).substr(new_pos_start))) { // 非有序列表项
        break;
      }
    } else if (new_pos_start > blank_prefix_length) { // 可能是子层级
      auto is_ordered_item = Item::is_ordered_item(absl::string_view(last_line).substr(new_pos_start));
      if (is_ordered_item || last_line[new_pos_start] == '-') { // 有序或无序的列表项
        auto child_item_list = std::make_shared<ItemList>();
        child_item_list->is_ordered = is_ordered_item;
        item_list->items.back().child = child_item_list;
        last_line_idx = line_idx;
        auto [status, next_line_idx] = is_ordered_item ? parse_ordered_itemlist(level + 1, new_pos_start, child_item_list) : parse_itemlist(level + 1, new_pos_start, child_item_list);  // 子列表
        line_idx = next_line_idx;
      } else {  // 非列表
        break;
      }
    } else {
      break;
    }
  } while (true);
  return ParseResult::make(ret_status ? 0 : 2, line_idx);
}

ParseResult Markdown::parse_ordered_itemlist() {
  const auto item_list = std::make_shared<ItemList>();
  item_list->is_ordered = true;
  item_list->level = 1;
  const auto pr = parse_ordered_itemlist(item_list->level, 0, item_list);
  elements_.push_back(item_list);
  return pr;
}

ParseResult Markdown::parse_image() {
  auto& last_line = lines.at(last_line_idx);
  if (last_line[0] != '!' || last_line[1] != '[') {
    return parse_default();
  }
  size_t idx = 2;
  // find alt text
  std::string alt_text;
  while (idx < last_line.size()) {
    if (last_line[idx] == ']') {
      alt_text = last_line.substr(2, idx - 2);
      break;
    }
    idx++;
  }
  if (idx >= last_line.size() - 3 || last_line[idx + 1] != '(' || last_line[last_line.size() - 1] != ')') {
    return ParseResult::make(2, last_line_idx);
  }
  // find uri
  std::string uri = last_line.substr(idx + 2, last_line.size() - 1 - (idx + 2));
  //
  const auto image = std::make_shared<Image>();
  image->alt_text = alt_text;
  image->uri = uri;
  elements_.push_back(image);
  //
  return ParseResult::make(0, last_line_idx + 1);
}

/*
 * - 文本
 * - 链接 []()
 * - 行内代码 ``
 * - latex $$
 */
ParseResult Markdown::parse_paragraph() {
  const auto paragraph = std::make_shared<Paragraph>();
  if (parse_paragraph(lines.at(last_line_idx), paragraph)) {
    elements_.push_back(paragraph);
  }
  return ParseResult::make(0, last_line_idx + 1);
}

bool Markdown::parse_paragraph(const std::string& line, const ParagraphPtr& paragraph_ptr) {
  const auto clean_line_view = utils::view_strip_empty(line);
  if (clean_line_view.empty()) {
    return true;
  }
  LineParseResult pr;
  size_t idx = 0;
  while (idx < clean_line_view.size()) {
    auto c = clean_line_view[idx];
    switch (c) {
      case '`':
        pr = try_parse_code(clean_line_view, idx, paragraph_ptr);
        break;
      case '$':
        pr = try_parse_inline_latex(clean_line_view, idx, paragraph_ptr);
        break;
      case '[':
        pr = try_parse_link(clean_line_view, idx, paragraph_ptr);
        break;
      default:
        pr = try_parse_text(clean_line_view, idx, paragraph_ptr);
    }
    if (pr.status == 2) {
      return false;
    }
    if (pr.status == 0) {
      idx = pr.next_pos;
    }
  }
  return true;
}

LineParseResult Markdown::try_parse_code(const absl::string_view& line,
                                         size_t start,
                                         const ParagraphPtr& paragraph_ptr) {
  size_t idx = start + 1;
  while (idx < line.size()) {
    if (line[idx] == '`') {
      break;
    }
    idx++;
  }
  if (idx == line.size() - 1 && line[idx] != '`') {  // 当成普通文本
    return try_parse_text(line, start, paragraph_ptr);
  }
  paragraph_ptr->blocks.push_back(std::make_shared<InlineCode>(std::string(line.substr(start + 1, idx - start - 1))));
  LineParseResult pr;
  pr.next_pos = idx + 1;
  return pr;
}

LineParseResult Markdown::try_parse_inline_latex(const absl::string_view& line,
                                          size_t start,
                                          const ParagraphPtr& paragraph_ptr) {
  size_t idx = start + 1;
  while (idx < line.size()) {
    if (line[idx] == '$') {
      break;
    }
    idx++;
  }
  if (idx == line.size() - 1 && line[idx] != '$') {
    return try_parse_text(line, start, paragraph_ptr);
  }
  paragraph_ptr->blocks.push_back(std::make_shared<InlineLatex>(std::string(line.substr(start + 1, idx - start - 1))));
  LineParseResult pr;
  pr.next_pos = idx + 1;
  return pr;
}

LineParseResult Markdown::try_parse_link(const absl::string_view& line,
                                         size_t start,
                                         const ParagraphPtr& paragraph_ptr) {
  size_t idx = start + 1;
  while (idx < line.size()) {
    if (line[idx] == ']') {
      break;
    }
    idx++;
  }
  if (idx >= line.size() - 3 || line[idx + 1] != '(') {
    return try_parse_text(line, start, paragraph_ptr);
  }
  absl::string_view link_text = line.substr(start + 1, idx - start - 1);
  idx++;
  const size_t link_url_start = idx;
  while (idx < line.size()) {
    if (line[idx] == ')') {
      break;
    }
    idx++;
  }
  if (idx == line.size() - 1 && line[idx] != ')') {
    return try_parse_text(line, start, paragraph_ptr);
  }
  const absl::string_view link_uri = line.substr(link_url_start+1, idx - link_url_start-1);
  paragraph_ptr->blocks.push_back(std::make_shared<InlineLink>(std::string(link_text), std::string(link_uri)));
  LineParseResult pr;
  pr.next_pos = idx + 1;
  return pr;
}

LineParseResult Markdown::try_parse_text(const absl::string_view& line,
                                         size_t start,
                                         const ParagraphPtr& paragraph_ptr) {
  size_t idx = start + 1;
  while (idx < line.size()) {
    if (line[idx] == '`' || line[idx] == '$' || line[idx] == '[') {
      break;
    }
    idx++;
  }
  absl::string_view substr_view = line.substr(start, idx - start);
  const auto vs = substr_view.size();
  // 粗体 & 斜体 & 删除线 使用标签替换实现，简单粗暴
  std::stringstream ss;
  size_t sv_idx = 0;
  /*
   * 0 - 当前处于普通文本状态
   * 1 - 当前处于粗体文本状态
   * 2 - 当前处于斜体文本状态
   * 3 - 当前处于删除线文本状态
   */
  std::stack<int8_t> mark_states;
  while (sv_idx < vs) {
    if (substr_view[sv_idx] != '*' && substr_view[sv_idx] != '~') {
      ss << substr_view[sv_idx];
    } else if (substr_view[sv_idx] == '~') {
      if (sv_idx+1 < vs && substr_view[sv_idx+1] == '~') {
        if (mark_states.empty() || mark_states.top() != 3) {
          mark_states.push(3);
          ss << "<strike>";
        } else {
          mark_states.pop();
          ss << "</strike>";
        }
        sv_idx++;
      } else {
        ss << substr_view[sv_idx];
      }
    } else {
      if (sv_idx+1 < vs && substr_view[sv_idx+1] == '*') {
        if (mark_states.empty() || mark_states.top() != 1) {
          mark_states.push(1);
          ss << "<strong>";
        } else {
          mark_states.pop();
          ss << "</strong>";
        }
        sv_idx++;
      } else {
        if (mark_states.empty() || mark_states.top() != 2) {
          mark_states.push(2);
          ss << "<em>";
        } else {
          mark_states.pop();
          ss << "</em>";
        }
      }
    }
    sv_idx++;
  }
  LineParseResult pr;
  if (!mark_states.empty()) {
    pr.status = 2;
  }
  paragraph_ptr->blocks.push_back(std::make_shared<Text>(FragmentType::PLAIN, ss.str()));
  pr.next_pos = idx;
  return pr;
}

ParseResult Markdown::parse_table() {
  // TODO:
  return parse_default();
}

void Markdown::clear() {
  if (stream_ptr_ != nullptr) {
    stream_ptr_ = nullptr;
  }
  if (!elements_.empty()) {
    for (const auto& ele : elements_) {
      if (ele != nullptr) {
        ele->clear();
      }
    }
    elements_.clear();
  }
}

std::string Markdown::to_html() {
  std::vector<std::string> lines;
  lines.reserve(elements_.size());
  for (const auto& ele : elements_) {
    lines.push_back(ele->to_html());
  }
  return absl::StrJoin(lines, "\n");
}

std::string Heading::to_html() {
  return fmt::format("<h{0}>{1}</h{0}>", level_, title_);
}

std::string Paragraph::to_text() const {
  std::vector<std::string> fragments;
  fragments.reserve(blocks.size());
  for (auto& block : blocks) {
    fragments.push_back(block->to_html());
  }
  return absl::StrJoin(fragments, "");
}

std::string Paragraph::to_html() {
  return fmt::format("<p>{}</p>", to_text());
}

std::string BlockQuote::to_html() {
  std::vector<std::string> ps;
  ps.reserve(elements_.size());
  for (const auto& ele : elements_) {
    ps.push_back(ele->to_html());
  }
  return fmt::format("<blockquote>{}</blockquote>", absl::StrJoin(ps, "\n"));
}

void BlockQuote::clear() {
  if (!elements_.empty()) {
    elements_.clear();
  }
}

std::string Item::to_html() {
  if (child == nullptr || child->items.empty()) {
    if (paragraph_ptr == nullptr) {
      return "";
    }
    return fmt::format("<li>{}</li>", paragraph_ptr->to_html());
  }
  std::vector<std::string> lines;
  lines.push_back(paragraph_ptr == nullptr ? "" : paragraph_ptr->to_text());
  if (child != nullptr) {
    lines.push_back(child->to_html());
  }
  return fmt::format("<li>{}</li>", absl::StrJoin(lines, "\n"));
}

bool Item::is_ordered_item(absl::string_view s) {
  return s.size() > 2 && s[0] >= '1' && s[0] <= '9' && s[1] == '.';
}

std::string ItemList::to_html() {
  std::vector<std::string> lines;
  lines.reserve(items.size());
  for (auto& item : items) {
    lines.push_back(item.to_html());
  }
  auto item_list = absl::StrJoin(lines, "\n");
  if (is_ordered) {
    return fmt::format("<ol>{}</ol>", item_list);
  }
  return fmt::format("<ul>{0}</ul>", item_list);
}

std::string CodeBlock::to_html() {
  std::string class_name = fmt::format("language-{}", absl::AsciiStrToLower(lang_name));
  return fmt::format(R"(<pre class="{0} {1}"><code>{2}</code></pre>)",
    class_name, "line-numbers", absl::StrJoin(lines, "\n"));
}

std::string LatexBlock::to_html() {
  return fmt::format(R"(<p style="text-align: center">$${0}$$</p>)", content);
}

std::string InlineCode::to_html() {
  return fmt::format("<code>{}</code>", code_);
}

std::string InlineLink::to_html() {
  return fmt::format("<a href='{0}'>{1}</a>", uri_, text_);
}

std::string InlineLatex::to_html() {
  return fmt::format("${}$", math_text_);
}

std::string Text::to_html() {
  return text_;
}

std::string Image::to_html() {
  return fmt::format("<img src='{0}' title='{1}' alt='{1}' sizes='100%'>", uri, alt_text);
}

std::string HorizontalRule::to_html() {
  return "<hr>";
}

}  // namespace ling