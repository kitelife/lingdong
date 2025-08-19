#include <gtest/gtest.h>
#include "markdown.h"

TEST(MarkdownTest, parse_table) {
  const auto md_ptr = std::make_shared<ling::Markdown>();
  std::string table_raw_str = R"(| Syntax      | Description | Test Text     |
| :---        |    :----:   |          ---: |
| Header      | Title       | Here's this   |
| Paragraph   | Text        | And more      |)";
  bool status = md_ptr->parse_str(table_raw_str);
  ASSERT_EQ(status, true);
  std::cout << "element cnt: " << md_ptr->elements().size() << std::endl;
  auto* table_ptr = reinterpret_cast<ling::Table*>(md_ptr->elements()[0].get());
  std::cout << "| ";
  for (const auto t : table_ptr->col_title_vec) {
    std::cout << t << " |";
  }
  std::cout << std::endl;
  std::cout << "| ";
  for (const auto align : table_ptr->col_alignment_vec) {
    std::string align_text = "left";
    switch (align) {
      case ling::AlignmentType::CENTER:
        align_text = "center";
        break;
      case ling::AlignmentType::RIGHT:
        align_text = "right";
        break;
      default:
        break;
    }
    std::cout << align_text << " |";
  }
  std::cout << std::endl;
  for (const auto r : table_ptr->col_row_vec) {
    std::cout << "| ";
    for (const auto c : r) {
      std::cout << c->to_html() << " |";
    }
    std::cout << std::endl;
  }
  std::cout << table_ptr->to_html() << std::endl;
}

TEST(MarkdownTest, parse_html_element) {
  std::string html_element_str =
      R"(<iframe width="100%" height="400px" src="https://gcc.godbolt.org/e#z:OYLghAFBqd5QCxAYwPYBMCmBRdBLAF1QCcAaPECAMzwBtMA7AQwFtMQByARg9KtQYEAysib0QXACx8BBAKoBnTAAUAHpwAMvAFYTStJg1AB9U8lJL6yAngGVG6AMKpaAVxYMQAJlIOAMngMmABy7gBGmMQg0gAOqAqEtgzObh7epHEJNgIBQaEsEVHSlpjWSUIETMQEKe6ePiVlAhVVBLkh4ZHRFpXVtWkNvW2BHQVdkgCUFqiuxMjsHACkXgDMgchuWADUiyuOLEwECAB0CLvYixoAgoEEW/yo1LSoh/cTOwDsAEKXV1v/W2ImAIswYWzwCmYDGoE12P2uiw%2BABEOFNaJwAKy8TwcLSkVCcRxbBQzOaYHarHikAiaVFTADW0Q%2Bxw%2BAE4AByrdkY1kadkfLwffScSS8FgSDQaUg4vEEji8BQgKU03Go0hwWAwRAoVAsGJ0SLkShoPUGqLIYBcLg%2BGi0AiRRUQMK00hhQJVACenCpJrYggA8gxaF7VaQsAcjOJQ/ggWUAG6YRWhzCqUque3e3i3TDo0O0PBhYie5xYF0EYh4cXcNVUAzABQANTwmAA7v6YoxMzJBCIxOwpN35Eo1C7dFx9IYTGZ9AXFZApqgYtkGEmALRUKhMBQEVcHI5bVf%2BlYKnOlZf2BhOFx1CQ%2BfwjfKFEArDLxRICfqea2vrJJdqProX0aZcWj6a80m/YDyiGf9OiiIChk/W8elaWCxngqYSVmeY9HLTAFh4NFMWxF05VUdkADZVwoyQtmAZBkC2K1ji8LYIEcUgtlwQgSApFZxy2ZxTXoYg%2BK4CZeBVLQJimBBMCYLAoggBkQAxKVc1FUhxTU6VSM4BUlWpWkpg1bUTX1ESjQgcyzRQDZJy4FZJT4Oh7WIR1nVDN1mGIEMfV1P0CEDYMXXDScozxGMzzwBMkzxFM0wzatyEEHMXXzQtiwwBY8XLStMymWsmHrJtW3bTtkv4HtRHEAcqqHFR1FDXQfAMIwQFMYxzAyudlPxJckjXDctx3PcEAPI8Tysc8IAcJDx3vPI4L0TJ32ScCvx/Na0KfccoOaRCNr0faGFA4YlvQ47DtSTbt1Qh9lvE6ZsP7akgQItVcyxXTQzIyjqNo%2ByjCYlZjg0MG2I4rj8CIUTln4zihIsyI%2BK8CSjNVGTSDkhSuj6jSxVUqUZV4OUDOVYyVIxLwWI0LgPgoqQNFZa0PhfXNjx%2B2V9Ix6SiI4LwSN%2BnmpLpUgE3cpJoiAA%3D"></iframe>)";
  const auto md_ptr = std::make_shared<ling::Markdown>();
  bool status = md_ptr->parse_str(html_element_str);
  ASSERT_EQ(status, true);
  std::cout << "element cnt: " << md_ptr->elements().size() << std::endl;
  auto* html_element_ptr = reinterpret_cast<ling::HtmlElement*>(md_ptr->elements()[0].get());
  std::cout << html_element_ptr->to_html() << std::endl;
}