#pragma once

#include <cstdint>
#include <string>

namespace kasane::query_runtime {

struct Finding {
  std::string rule_id;
  std::string message;
  std::string file;
  std::uint32_t line;
};

std::string render_finding(const Finding& finding);

}  // namespace kasane::query_runtime
