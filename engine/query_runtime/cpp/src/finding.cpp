#include "kasane/query_runtime/finding.hpp"

namespace kasane::query_runtime {

std::string render_finding(const Finding& finding) {
  return finding.rule_id + ": " + finding.file + ":" + std::to_string(finding.line) +
         ": " + finding.message;
}

}  // namespace kasane::query_runtime
