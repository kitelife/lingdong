#pragma once

namespace ling {

enum class Protocol {
  UNKNOWN,
  INVALID,
  HTTP,
};

enum class ParseStatus {
  INVALID,
  CONTINUE,
  COMPLETE,
};

}