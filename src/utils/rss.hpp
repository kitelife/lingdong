#pragma once

#include <string>

#include <tinyxml2.h>

namespace ling::utils {

using namespace tinyxml2;

static auto ATOM_ROOT_ELE_NAME = "feed";
static auto RSS_ROOT_ELE_NAME = "rss";

namespace rss_parse_result {

static int32_t ERR_CODE_102 = 102;
static std::string ERR_MSG_102 = "failure to parse rss";
static int32_t ERR_CODE_103 = 103;
static std::string ERR_MSG_103 = "illegal rss/feed xml";

}

class RssEntry {
public:
  RssEntry() = default;
  std::string title;
  std::string link;
  std::string updated_time;
  std::string content;
};

class RSS {
public:
  void parse(const std::string& s);
  void to_json(nlohmann::json& j);

  int32_t parse_result_code() const {
    return parse_result_code_;
  }
  std::string parse_result_msg() const {
    return parse_result_msg_;
  }

private:
  std::string title_;
  std::string link_;
  std::string updated_time_;
  std::vector<RssEntry> entries_;

  int32_t parse_result_code_ = 0;
  std::string parse_result_msg_ = "success";

private:
  void parse_rss(XMLDocument& doc);
  void parse_atom(XMLDocument& doc);
};

inline void RSS::parse(const std::string& s) {
  XMLDocument doc;
  if (doc.Parse(s.c_str(), s.size()) != tinyxml2::XML_SUCCESS) {
    parse_result_code_ = rss_parse_result::ERR_CODE_102;
    parse_result_msg_ = rss_parse_result::ERR_MSG_102;
    return;
  }
  auto root_ele_name = doc.RootElement()->Name();
  // https://wiki.digitalclassicist.org/RSS_or_Atom
  if (*root_ele_name == *ATOM_ROOT_ELE_NAME) {
    parse_atom(doc);
  } else if (*root_ele_name == *RSS_ROOT_ELE_NAME) {
    parse_rss(doc);
  } else {
    parse_result_code_ = rss_parse_result::ERR_CODE_103;
    parse_result_msg_ = rss_parse_result::ERR_MSG_103;
  }
}

// https://datatracker.ietf.org/doc/html/rfc4287
inline void RSS::parse_atom(XMLDocument& doc) {
  auto* root_ele = doc.RootElement();
  auto* title_ele = root_ele->FirstChildElement("title");
  if (title_ele != nullptr) {
    auto* t = title_ele->GetText();
    title_ = t == nullptr ? "" : t;
  }
  auto* link_ele = root_ele->FirstChildElement("link");
  if (link_ele != nullptr) {
    auto* t = link_ele->GetText();
    if (t == nullptr) {
      t = link_ele->FindAttribute("href")->Value();
    }
    link_ = t == nullptr ? "" : t;
  }
  auto* updated_ele = root_ele->FirstChildElement("updated");
  if (updated_ele != nullptr) {
    auto* t = updated_ele->GetText();
    updated_time_ = t == nullptr ? "" : t;
  }
  //
  auto entry_ele = root_ele->FirstChildElement("entry");
  while (entry_ele != nullptr) {
    RssEntry entry;
    auto* entry_title_ele = entry_ele->FirstChildElement("title");
    if (entry_title_ele != nullptr) {
      auto* t = entry_title_ele->GetText();
      entry.title = t == nullptr ? "" : t;
    }
    auto* entry_link_ele = entry_ele->FirstChildElement("link");
    if (entry_link_ele != nullptr) {
      auto* t = entry_link_ele->GetText();
      if (t == nullptr) {
        t = entry_link_ele->FindAttribute("href")->Value();
      }
      entry.link = t == nullptr ? "" : t;
    }
    auto* entry_updated_ele = entry_ele->FirstChildElement("updated");
    if (entry_updated_ele != nullptr) {
      auto* t = entry_updated_ele->GetText();
      entry.updated_time = t == nullptr ? "" : t;
    }
    auto* entry_summary_ele = entry_ele->FirstChildElement("summary");
    if (entry_summary_ele == nullptr) {
      entry_summary_ele = entry_ele->FirstChildElement("content");
    }
    if (entry_summary_ele != nullptr) {
      auto* t = entry_summary_ele->GetText();
      entry.content = t == nullptr ? "" : t;
    }
    entries_.push_back(entry);
    //
    entry_ele = entry_ele->NextSiblingElement("entry");
  }
}

// https://en.wikipedia.org/wiki/RSS
inline void RSS::parse_rss(XMLDocument& doc) {
  auto* root_ele = doc.RootElement();
  XMLElement* channel_ele = root_ele->FirstChildElement("channel");
  if (channel_ele == nullptr) {
    parse_result_code_ = rss_parse_result::ERR_CODE_103;
    parse_result_msg_ = rss_parse_result::ERR_MSG_103;
    return;
  }
  auto* title_ele = channel_ele->FirstChildElement("title");
  if (title_ele != nullptr) {
    auto* t = title_ele->GetText();
    title_ = t == nullptr ? "" : t;
  }
  auto* link_ele = channel_ele->FirstChildElement("link");
  if (link_ele != nullptr) {
    auto* t = link_ele->GetText();
    link_ = t == nullptr ? "" : t;
  }
  auto* pub_date_ele = channel_ele->FirstChildElement("pubDate");
  if (pub_date_ele == nullptr) {
    pub_date_ele = channel_ele->FirstChildElement("lastBuildDate");
  }
  if (pub_date_ele != nullptr) {
    auto* t = pub_date_ele->GetText();
    updated_time_ = t == nullptr ? "" : t;
  }
  //
  auto* item_ele = channel_ele->FirstChildElement("item");
  while (item_ele != nullptr) {
    RssEntry entry;
    auto* item_title_ele = item_ele->FirstChildElement("title");
    if (item_title_ele != nullptr) {
      auto* t = item_title_ele->GetText();
      entry.title = t == nullptr ? "" : t;
    }
    auto* item_desc_ele = item_ele->FirstChildElement("description");
    if (item_desc_ele != nullptr) {
      auto* t = item_desc_ele->GetText();
      entry.content = t == nullptr ? "" : t;
    }
    auto* item_link_ele = item_ele->FirstChildElement("link");
    if (item_link_ele == nullptr) {
      item_link_ele = item_ele->FirstChildElement("guid");
    }
    if (item_link_ele != nullptr) {
      auto* t = item_link_ele->GetText();
      entry.link = t == nullptr ? "" : t;
    }
    auto* item_pub_date_ele = item_ele->FirstChildElement("pubDate");
    if (item_pub_date_ele != nullptr) {
      auto* t = item_pub_date_ele->GetText();
      entry.updated_time = t == nullptr ? "" : t;
    }
    entries_.push_back(entry);
    //
    item_ele = item_ele->NextSiblingElement("item");
  }
}

inline void RSS::to_json(nlohmann::json& j) {
  j["title"] = title_;
  j["link"] = link_;
  j["updated_time"] = updated_time_;
  nlohmann::json entries = nlohmann::json::array();
  for (const auto& entry : entries_) {
    entries.push_back({
      {"title", entry.title},
      {"link", entry.link},
      {"updated_time", entry.updated_time},
      {"content", entry.content},
    });
  }
  j["entries"] = entries;
}


}