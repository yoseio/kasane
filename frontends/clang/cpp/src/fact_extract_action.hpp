#pragma once

#include "clang/Tooling/Tooling.h"

#include <memory>

namespace kasane::facts {
class FactEmitter;
}

namespace kasane::frontends::clangcpp {

std::unique_ptr<clang::tooling::FrontendActionFactory> create_fact_extract_action_factory(
    kasane::facts::FactEmitter& emitter);

}  // namespace kasane::frontends::clangcpp
