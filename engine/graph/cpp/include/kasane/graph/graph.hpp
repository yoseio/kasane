#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace kasane::graph {

using NodeId = std::uint32_t;

struct Node {
  NodeId id;
  std::string kind;
  std::string text;
  std::uint32_t line;
};

class Graph {
public:
  NodeId add_node(std::string kind, std::string text, std::uint32_t line);
  [[nodiscard]] std::size_t size() const noexcept;
  [[nodiscard]] const std::vector<Node>& nodes() const noexcept;

private:
  std::vector<Node> nodes_;
};

}  // namespace kasane::graph
