#pragma once

#include "clang/Tooling/Tooling.h"

#include <memory>
#include <string>

namespace kasane::facts {
class FactEmitter;
}

namespace kasane::frontends::clangcpp {

struct TranslationUnitContext {
  std::string tu_id;
  std::string main_file;
  std::string working_directory;
  std::string output;
  std::string command_line;
};

std::unique_ptr<clang::tooling::FrontendActionFactory>
create_fact_extract_action_factory(kasane::facts::FactEmitter &emitter,
                                   TranslationUnitContext context);

} // namespace kasane::frontends::clangcpp
