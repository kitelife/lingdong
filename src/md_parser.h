
//
// Created by xiayf on 2025/3/28.
//

#pragma once

/*
- https://daringfireball.net/projects/markdown/syntax
- https://www.markdownguide.org/basic-syntax/
 */

#include <string>
#include <vector>

namespace ling {

class ItemList;

class Element {
public:
  std::string to_html() {
    return "";
  }

  virtual ~Element() = default;
};

class Heading: Element {
public:
  int8_t level;
  std::string title;
};

enum class FragmentType {
  UNKNOWN,
  PLAIN_TEXT,
  BOLD,
  ITALIC,
  INLINE_CODE,
  HTML_ELEMENT,
};

class InlineFragment: Element {
public:
  FragmentType type = FragmentType::UNKNOWN;
  std::string text;
  std::shared_ptr<InlineFragment> child;
};

class Paragraph: Element {
public:
  std::vector<InlineFragment> blocks;
};

class BlockQuote: Element {
public:
  std::vector<Paragraph> paragraphs;
};

class Item {
public:
  Paragraph paragraph;
  std::shared_ptr<ItemList> child;
};

class ItemList {
public:
  bool is_ordered = false;
  std::vector<Item> items;
};

class CodeBlock: Element {

};

class HorizontalRule: Element {

};

class Link: Element {
public:
  std::string title;
  std::string uri;
};

class Image: Element {
public:
  std::string title;
  std::string uri;
};

class Table: Element {

};

class MdParser {
public:
  MdParser() = default;
  static bool parse(const std::string& md_content);
  static bool parse_md_file(const std::string& md_file_path);

private:
  static bool parse(std::istream& stream);

private:
  const std::string md_content_;
  const std::string md_file_path_;
};
}  // namespace ling
