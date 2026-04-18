#include "kasane/graph/graph.hpp"

#include <utility>

namespace kasane::graph {

NodeId Graph::add_node(std::string kind, std::string text, std::uint32_t line) {
  const auto next_id = static_cast<NodeId>(nodes_.size() + 1U);
  nodes_.push_back(Node{next_id, std::move(kind), std::move(text), line});
  return next_id;
}

std::size_t Graph::size() const noexcept {
  return nodes_.size();
}

const std::vector<Node>& Graph::nodes() const noexcept {
  return nodes_;
}

}  // namespace kasane::graph
