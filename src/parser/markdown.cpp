//
// Created by xiayf on 2025/3/28.
//

#include "markdown.h"

#include <absl/strings/str_split.h>
#include <fmt/core.h>
#include <spdlog/spdlog.h>

#include <fstream>
#include <inja/inja.hpp>
#include <iostream>
#include <sstream>

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
  body_start_line_idx = pr.next_line_idx;
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
      case '$':  // 可能是独立成行的 latex 公式
        pr = parse_latex();
        break;
      case '[':  // 可能是脚注
        pr = parse_footnote();
        break;
      case '<':  // 可能是 html 元素
        pr = parse_html_element();
        break;
      default:
        pr = parse_default();
    }
    if (pr.status != 0) {
      spdlog::warn("Markdown parse error: {}, next line: {}", pr.status, pr.next_line_idx);
      break;
    }
    last_line_idx = pr.next_line_idx;
  }
  return pr.status == 0;
}

ParseResult Markdown::parse_footnote() {
  const auto& last_line = lines.at(last_line_idx);
  // spdlog::debug("try to parse footnote: {}", last_line);
  if (last_line.size() < 4 || last_line[1] != '^') {
    return parse_default();
  }
  size_t idx = 2;
  do {
    if (last_line[idx] == ']') {
      break;
    }
  } while (++idx < last_line.size());
  if (idx < 3 || idx == last_line.size() || idx + 1 == last_line.size() || last_line[idx + 1] != ':') {
    return parse_default();
  }
  const auto& id = last_line.substr(2, idx - 2);
  ++idx;
  auto paragraph_ptr = std::make_shared<Paragraph>(true);
  auto status = parse_paragraph(last_line.substr(idx + 1), paragraph_ptr);
  if (!status) {
    spdlog::error("Failed to parse '{}'", last_line);
  }
  // spdlog::debug("Has footnote: {}", id);
  footnotes_ptr_->add_footnote(id, std::make_shared<Footnote>(id, paragraph_ptr));
  return ParseResult::make(0, last_line_idx + 1);
}

/*
元信息部分的格式：

---
id: example-post
title: 示例文章
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
      spdlog::error("Illegal metadata, k: {}, v: {}", k, v);
    }
  } while (true);
  if (line_view != "---") {
    return ParseResult::make(2, 0);
  }
  return ParseResult::make(0, last_line_idx + 1);
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
  std::string heading_title = last_line.substr(title_start, title_end - title_start + 1);
  auto pp = std::make_shared<Paragraph>(true);
  if (parse_paragraph(last_line.substr(title_start, title_end - title_start + 1), pp)) {
    heading_title = pp->to_html();
  }
  const auto heading = std::make_shared<Heading>(level, heading_title);
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
  const std::string codeblock_meta = last_line.substr(3, last_line.size() - 3);
  std::vector<absl::string_view> meta_parts = absl::StrSplit(codeblock_meta, ' ', absl::SkipWhitespace());
  //
  std::string lang_name;
  std::vector<StrPair> attrs;
  do {
    if (meta_parts.empty()) {
      lang_name = "text";
      break;
    }
    lang_name = meta_parts[0];
    if (meta_parts.size() < 2) {
      break;
    }
    for (size_t i = 1; i < meta_parts.size(); i++) {
      std::vector<std::string> attr_parts = absl::StrSplit(meta_parts[i], ':', absl::SkipWhitespace());
      if (attr_parts.size() < 2) {
        spdlog::warn("Illegal attr: {}", meta_parts[i]);
        continue;
      }
      attrs.emplace_back(attr_parts[0], attr_parts[1]);
    }
  } while (false);
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
  codeblock_ptr->attrs = attrs;
  codeblock_ptr->lines = code_lines;
  elements_.push_back(codeblock_ptr);
  return ParseResult::make(0, line_idx + 1);
}

ParseResult Markdown::parse_latex() {
  size_t line_idx = last_line_idx;
  auto line_view = utils::view_strip_empty(lines.at(line_idx));
  size_t view_len = line_view.size();
  if (view_len < 2) {
    return parse_default();
  }
  // 可能单行，也可能多行
  if (line_view[0] == '$' && line_view[1] == '$') {
    const auto latex_block_ptr = std::make_shared<LatexBlock>();
    if (view_len > 4 && line_view[view_len - 1] == '$' && line_view[view_len - 2] == '$') {  // 单行情况
      latex_block_ptr->content = std::string(line_view.substr(2, view_len - 4));
    } else {  // 多行情况
      std::vector<std::string> latex_lines;
      latex_lines.emplace_back(line_view.substr(2, view_len - 2));
      //
      bool reach_end = false;
      while ((++line_idx) < lines.size()) {
        line_view = utils::view_strip_empty(lines.at(line_idx));
        view_len = line_view.size();
        if (view_len >= 2 && line_view[view_len - 1] == '$' && line_view[view_len - 2] == '$') {  // 结束行
          latex_lines.emplace_back(line_view.substr(0, view_len - 2));
          reach_end = true;
          break;
        }
        latex_lines.emplace_back(line_view);
      }
      if (!reach_end) {
        return parse_default();
      }
      latex_block_ptr->content = absl::StrJoin(latex_lines, "\n");
    }
    elements_.push_back(latex_block_ptr);
    return ParseResult::make(0, line_idx + 1);
  }
  return parse_default();
}

ParseResult Markdown::parse_dash_prefix_line() {
  const auto& last_line = lines.at(last_line_idx);
  if (last_line.size() >= 3) {
    bool all_dash = true;
    size_t idx = 0;
    while (idx < last_line.size()) {
      if (last_line[idx] != '-') {
        all_dash = false;
        break;
      }
      idx++;
    }
    if (all_dash) {
      return parse_horizontal_rule();
    }
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
    if (last_line.empty() || last_line.size() <= pos) {
      break;
    }

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
    if (const auto status = parse_paragraph(last_line.substr(pos + 2, last_line.size() - 2 - pos),
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
    if (new_pos_start == blank_prefix_length) {                                          // 可能同层级
      if (!Item::is_ordered_item(absl::string_view(last_line).substr(new_pos_start))) {  // 非有序列表项
        break;
      }
    } else if (new_pos_start > blank_prefix_length) {  // 可能是子层级
      auto is_ordered_item = Item::is_ordered_item(absl::string_view(last_line).substr(new_pos_start));
      if (is_ordered_item || last_line[new_pos_start] == '-') {  // 有序或无序的列表项
        auto child_item_list = std::make_shared<ItemList>();
        child_item_list->is_ordered = is_ordered_item;
        item_list->items.back().child = child_item_list;
        last_line_idx = line_idx;
        auto [status, next_line_idx] = is_ordered_item
                                           ? parse_ordered_itemlist(level + 1, new_pos_start, child_item_list)
                                           : parse_itemlist(level + 1, new_pos_start, child_item_list);  // 子列表
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
  std::string img_width;
  while (idx < last_line.size()) {
    if (last_line[idx] == ']') {
      alt_text = last_line.substr(2, idx - 2);
      break;
    }
    idx++;
  }
  //
  do {
    std::vector<absl::string_view> alt_parts = absl::StrSplit(alt_text, '|');
    if (alt_parts.size() == 2) {
      alt_text = alt_parts[0];
      img_width = alt_parts[1];
    }
  } while (false);
  //
  if (idx >= last_line.size() - 3 || last_line[idx + 1] != '(' || last_line[last_line.size() - 1] != ')') {
    return ParseResult::make(2, last_line_idx);
  }
  // find uri
  std::string uri = last_line.substr(idx + 2, last_line.size() - 1 - (idx + 2));
  //
  const auto image = std::make_shared<Image>();
  image->alt_text = alt_text;
  image->uri = uri;
  if (!img_width.empty()) {
    image->width = img_width;
  }
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
    if (!paragraph->blocks.empty()) {  // 忽略空行
      elements_.push_back(paragraph);
    }
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
  if (clean_line_view[idx] == '\\' && clean_line_view.size() > 2) {
    switch (clean_line_view[idx + 1]) {
      case 'L':
        paragraph_ptr->text_align = "left";
        idx += 2;
        break;
      case 'C':
        paragraph_ptr->text_align = "center";
        idx += 2;
        break;
      case 'R':
        paragraph_ptr->text_align = "right";
        idx += 2;
        break;
      default:
    }
  }
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
  // 脚注
  if (idx < line.size()) {
    if (line[idx] == '^') {
      auto pr = try_parse_footnote_ref(line, idx + 1, paragraph_ptr);
      if (pr.status == 0) {
        return pr;
      }
    }
  }
  // 链接
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
  const absl::string_view link_uri = line.substr(link_url_start + 1, idx - link_url_start - 1);
  paragraph_ptr->blocks.push_back(std::make_shared<InlineLink>(std::string(link_text), std::string(link_uri)));
  LineParseResult pr;
  pr.next_pos = idx + 1;
  return pr;
}

LineParseResult Markdown::try_parse_footnote_ref(const absl::string_view& line,
                                                 size_t start,
                                                 const ParagraphPtr& paragraph_ptr) {
  size_t idx = start;
  LineParseResult lpr;
  do {
    if (line[idx] == ']') {
      break;
    }
  } while (++idx < line.size());
  if (idx == start) {
    lpr.status = 2;
    return lpr;
  }
  const absl::string_view ref_id = line.substr(start, idx - start);
  paragraph_ptr->blocks.push_back(std::make_shared<InlineFootnoteRef>(std::string(ref_id)));
  lpr.next_pos = idx + 1;
  return lpr;
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
      if (sv_idx + 1 < vs && substr_view[sv_idx + 1] == '~') {
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
      if (sv_idx + 1 < vs && substr_view[sv_idx + 1] == '*') {
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

// 单行单个 html 元素
ParseResult Markdown::parse_html_element() {
  const auto& last_line = lines.at(last_line_idx);
  const auto clean_line_view = utils::view_strip_empty(last_line);
  if (clean_line_view.length() <= 4 || clean_line_view[clean_line_view.length() - 1] != '>') {
    return parse_default();
  }
  size_t idx = 1;
  while (!utils::is_space(clean_line_view[idx++]) && idx < clean_line_view.length()) {
  }
  if (idx == clean_line_view.length()) {
    spdlog::warn("Illegal html element: {}", last_line);
    return parse_default();
  }
  const auto& tag_name = clean_line_view.substr(1, idx - 2);
  while (clean_line_view[idx++] != '>' && idx < clean_line_view.length()) {
  }
  if (idx == clean_line_view.length()) {
    spdlog::warn("Illegal html element: {}", last_line);
    return parse_default();
  }
  while (clean_line_view[idx++] != '<' && idx < clean_line_view.length()) {
  }
  if (idx == clean_line_view.length() || clean_line_view[idx] != '/') {
    spdlog::warn("Illegal html element: {}", last_line);
    return parse_default();
  }
  const auto& end_tag_name = clean_line_view.substr(idx + 1, clean_line_view.length() - idx - 2);
  if (tag_name != end_tag_name) {
    spdlog::warn("Illegal html element: {}", last_line);
    return parse_default();
  }
  auto html_ele_ptr = std::make_shared<HtmlElement>();
  html_ele_ptr->tag_name = tag_name;
  html_ele_ptr->html = clean_line_view;
  elements_.emplace_back(html_ele_ptr);
  return ParseResult::make(0, last_line_idx + 1);
}

/*
| Syntax      | Description | Test Text     |
| :---        |    :----:   |          ---: |
| Header      | Title       | Here's this   |
| Paragraph   | Text        | And more      |
 */
ParseResult Markdown::parse_table() {
  // 标题行
  absl::string_view clear_line_view = utils::view_strip_empty(lines.at(last_line_idx));
  size_t line_len = clear_line_view.length();
  if (clear_line_view[0] != '|' || clear_line_view[line_len - 1] != '|') {
    return parse_default();
  }
  auto table_ptr = std::make_shared<Table>();
  size_t end_idx = 1;
  while (end_idx < line_len) {
    size_t start_idx = end_idx;
    while (clear_line_view[end_idx++] != '|') {
    }
    const auto col_title = utils::view_strip_empty(clear_line_view.substr(start_idx, end_idx - 1 - start_idx));
    table_ptr->col_title_vec.emplace_back(col_title);
  }
  // 列对齐标记行
  clear_line_view = utils::view_strip_empty(lines.at(last_line_idx + 1));
  line_len = clear_line_view.length();
  if (clear_line_view[0] != '|' || clear_line_view[line_len - 1] != '|') {
    return parse_default();
  }
  end_idx = 1;
  while (end_idx < line_len) {
    size_t start_idx = end_idx;
    while (clear_line_view[end_idx++] != '|') {
    }
    absl::string_view col_val = utils::view_strip_empty(clear_line_view.substr(start_idx, end_idx - 1 - start_idx));
    if (col_val.length() < 3 || (col_val[0] != ':' && col_val[col_val.length() - 1] != ':')) {  // 默认左对齐
      table_ptr->col_alignment_vec.push_back(AlignmentType::LEFT);
      continue;
    }
    if (col_val[0] == ':') {
      if (col_val[col_val.length() - 1] == ':') {  // 居中对齐
        table_ptr->col_alignment_vec.push_back(AlignmentType::CENTER);
      } else {
        table_ptr->col_alignment_vec.push_back(AlignmentType::LEFT);
      }
    } else {
      table_ptr->col_alignment_vec.push_back(AlignmentType::RIGHT);
    }
  }
  if (table_ptr->col_title_vec.size() != table_ptr->col_alignment_vec.size()) {
    spdlog::error("Illegal table");
    return ParseResult::make(2, last_line_idx);
  }
  // 表格内容
  last_line_idx += 2;
  do {
    clear_line_view = utils::view_strip_empty(lines.at(last_line_idx));
    line_len = clear_line_view.length();
    if (clear_line_view[0] != '|' || clear_line_view[line_len - 1] != '|') {
      break;
    }
    end_idx = 1;
    table_ptr->col_row_vec.emplace_back();
    while (end_idx < line_len) {
      size_t start_idx = end_idx;
      while (clear_line_view[end_idx++] != '|') {
      }
      const auto col_val = clear_line_view.substr(start_idx, end_idx - 1 - start_idx);
      auto paragraph_ptr = std::make_shared<Paragraph>();
      if (parse_paragraph(std::string(col_val), paragraph_ptr)) {
        table_ptr->col_row_vec.back().emplace_back(paragraph_ptr);
      } else {
        spdlog::warn("Illegal paragraph: {}", col_val);
      }
    }
    if (table_ptr->col_row_vec.back().size() != table_ptr->col_title_vec.size()) {
      spdlog::warn("Illegal table");
      break;
    }
    last_line_idx++;
  } while (last_line_idx < lines.size());
  elements_.push_back(table_ptr);
  return ParseResult::make(0, last_line_idx);
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
  const auto& footnotes_html = footnotes_ptr_->to_html();
  if (!footnotes_html.empty()) {
    lines.push_back(footnotes_html);
  }
  return absl::StrJoin(lines, "\n");
}

std::string Markdown::body_part() {
  const std::vector<std::string> body_lines{lines.begin() + body_start_line_idx, lines.end()};
  return absl::StrJoin(body_lines, "\n");
}

std::string Footnotes::to_html() {
  if (footnotes_.empty()) {
    return "";
  }
  std::vector<std::string> lines;
  lines.reserve(footnotes_.size());
  for (const auto& [id, footnote] : footnotes_) {
    lines.push_back(footnote->to_html());
  }
  const std::string footnotes_part =
      fmt::format(R"(<div class="footnotes" role="doc-endnotes"><ol>{}</ol></div>)", absl::StrJoin(lines, "\n"));
  return HorizontalRule().to_html() + "\n" + footnotes_part;
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
  if (unwrap_html_) {
    return to_text();
  }
  return fmt::format(R"(<p class="text-align-{0}">{1}</p>)", text_align, to_text());
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
  const auto& ln = absl::AsciiStrToLower(lang_name);
  // 有点 trick，不优雅
  // 应该放在 inja 模板渲染时解决？
  // 不用检测是什么语言的代码，无脑转义？
  for (auto& line : lines) {
    line = inja::htmlescape(line);
  }
  std::string class_name = fmt::format("language-{}", ln);
  return fmt::format(R"(<pre class="{0}"><code>{1}</code></pre>)", class_name, absl::StrJoin(lines, "\n"));
}

std::string LatexBlock::to_html() {
  return fmt::format(R"(<p style="text-align: center">$${0}$$</p>)", content);
}

std::string Footnote::to_html() {
  return fmt::format(
      R"(<li id="fn:{0}"><p>{1}<a href="#fnref:{0}" class="reversefootnote" role="doc-backlink">↩</a></p></li>)", id_,
      p_ptr_->to_html());
}

std::string InlineFootnoteRef::to_html() {
  return fmt::format(
      R"(<sup id="fnref:{0}"><a href="#fn:{0}" class="footnote" rel="footnote" role="doc-noteref">[{0}]</a></sup>)",
      id_);
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
  if (width.empty()) {
    return fmt::format("<img src='{0}' title='{1}' alt='{1}'/>", uri, alt_text);
  }
  return fmt::format("<img src='{0}' title='{1}' alt='{1}' width='{2}'/>", uri, alt_text, width);
}

std::string HorizontalRule::to_html() {
  return "<hr>";
}

std::string Table::to_html() {
  static std::string table_tpl = R"(<table class="table table-bordered">
<thead>
<tr>{0}</tr>
</thead>
<tbody>{1}</tbody>
</table>)";
  //
  std::string thead;
  size_t col_cnt = col_title_vec.size();
  std::vector<std::string> col_align;
  col_align.reserve(col_cnt);
  for (size_t col_idx = 0; col_idx < col_cnt; col_idx++) {
    col_align.emplace_back(to_string(col_alignment_vec[col_idx]));
    thead += fmt::format("<th style=\"text-align: {0}\">{1}</th>\n", col_align[col_idx], col_title_vec[col_idx]);
  }
  //
  std::string tbody;
  for (const auto& r : col_row_vec) {
    std::string tr = "<tr>\n";
    for (size_t col_idx = 0; col_idx < col_cnt; col_idx++) {
      const auto& col_ptr = r[col_idx];
      col_ptr->text_align = col_align[col_idx];
      tr += fmt::format("<td style=\"text-align: {0}\">{1}</td>\n", col_align[col_idx], col_ptr->to_html());
    }
    tr += "</tr>\n";
    tbody += tr;
  }
  return fmt::format(table_tpl, thead, tbody);
}
}  // namespace ling