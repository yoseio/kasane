#include "kasane/facts/fact_emitter.hpp"

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kasane::facts {

namespace {

constexpr std::string_view kNullId = "-";

std::string escape_field(std::string_view value) {
  std::string escaped;
  escaped.reserve(value.size());

  for (const char ch : value) {
    switch (ch) {
    case '\\':
      escaped.append("\\\\");
      break;
    case '\t':
      escaped.append("\\t");
      break;
    case '\n':
      escaped.append("\\n");
      break;
    default:
      escaped.push_back(ch);
      break;
    }
  }

  return escaped;
}

void append_field(std::ofstream &stream, std::string_view value) {
  stream << escape_field(value);
}

void append_field(std::ofstream &stream, const std::string &value) {
  append_field(stream, std::string_view(value));
}

void append_field(std::ofstream &stream, const char *value) {
  append_field(stream, std::string_view(value != nullptr ? value : ""));
}

void append_field(std::ofstream &stream, std::uint64_t value) {
  stream << value;
}

void append_field(std::ofstream &stream, unsigned value) { stream << value; }

void append_field(std::ofstream &stream, bool value) { stream << (value ? 1 : 0); }

template <typename First, typename... Rest>
void write_row(std::ofstream &stream, const First &first, const Rest &...rest) {
  append_field(stream, first);
  ((stream << '\t', append_field(stream, rest)), ...);
  stream << '\n';
}

std::string type_key(std::string_view kind, std::string_view printed,
                     std::string_view canonical, std::uint64_t size_bits,
                     std::uint64_t align_bits) {
  std::string key;
  key.reserve(kind.size() + printed.size() + canonical.size() + 32U);
  key.append(kind);
  key.push_back('\x1f');
  key.append(printed);
  key.push_back('\x1f');
  key.append(canonical);
  key.push_back('\x1f');
  key.append(std::to_string(size_bits));
  key.push_back('\x1f');
  key.append(std::to_string(align_bits));
  return key;
}

} // namespace

const Id &null_id() {
  static const Id kId(kNullId);
  return kId;
}

bool is_null_id(std::string_view id) { return id == kNullId; }

std::ofstream FactEmitter::open_fact_file(const std::filesystem::path &path) {
  std::ofstream stream(path.string(), std::ios::out | std::ios::trunc);
  if (!stream.is_open()) {
    throw std::runtime_error("failed to open fact file: " + path.string());
  }
  return stream;
}

Id FactEmitter::make_id(std::string_view prefix, std::uint64_t counter) {
  std::string id(prefix);
  id.push_back(':');
  id.append(std::to_string(counter));
  return id;
}

FactEmitter::FactEmitter(const std::filesystem::path &directory) {
  std::filesystem::create_directories(directory);

  file_ = open_fact_file(directory / "file.facts");
  translation_unit_ = open_fact_file(directory / "translation_unit.facts");
  compile_arg_ = open_fact_file(directory / "compile_arg.facts");
  tu_file_ = open_fact_file(directory / "tu_file.facts");
  include_edge_ = open_fact_file(directory / "include_edge.facts");
  macro_ = open_fact_file(directory / "macro.facts");
  macro_expansion_ = open_fact_file(directory / "macro_expansion.facts");
  type_ = open_fact_file(directory / "type.facts");
  decl_ = open_fact_file(directory / "decl.facts");
  function_decl_ = open_fact_file(directory / "function_decl.facts");
  param_decl_ = open_fact_file(directory / "param_decl.facts");
  local_var_decl_ = open_fact_file(directory / "local_var_decl.facts");
  node_ = open_fact_file(directory / "node.facts");
  cfg_edge_ = open_fact_file(directory / "cfg_edge.facts");
  ref_ = open_fact_file(directory / "ref.facts");
  use_ = open_fact_file(directory / "use.facts");
  def_ = open_fact_file(directory / "def.facts");
  assign_ = open_fact_file(directory / "assign.facts");
  call_ = open_fact_file(directory / "call.facts");
  call_arg_ = open_fact_file(directory / "call_arg.facts");
  return_expr_ = open_fact_file(directory / "return_expr.facts");
  return_value_ = open_fact_file(directory / "return_value.facts");
  type_of_ = open_fact_file(directory / "type_of.facts");
  diag_ = open_fact_file(directory / "diag.facts");
}

Id FactEmitter::next_translation_unit_id() {
  return make_id("tu", next_translation_unit_id_++);
}

Id FactEmitter::next_decl_id() { return make_id("decl", next_decl_id_++); }

Id FactEmitter::next_node_id() { return make_id("node", next_node_id_++); }

Id FactEmitter::next_macro_id() { return make_id("macro", next_macro_id_++); }

Id FactEmitter::next_macro_expansion_id() {
  return make_id("macro_expansion", next_macro_expansion_id_++);
}

Id FactEmitter::intern_file(std::string_view path) {
  const std::string normalized(path);
  const auto it = file_ids_.find(normalized);
  if (it != file_ids_.end()) {
    return it->second;
  }

  const Id file_id = make_id("file", next_file_id_++);
  file_ids_.emplace(normalized, file_id);
  write_row(file_, file_id, normalized, "");
  return file_id;
}

Id FactEmitter::intern_type(std::string_view kind, std::string_view printed,
                            std::string_view canonical,
                            std::uint64_t size_bits,
                            std::uint64_t align_bits) {
  const std::string key =
      type_key(kind, printed, canonical, size_bits, align_bits);
  const auto it = type_ids_.find(key);
  if (it != type_ids_.end()) {
    return it->second;
  }

  const Id type_id = make_id("type", next_type_id_++);
  type_ids_.emplace(key, type_id);
  write_row(type_, type_id, kind, printed, canonical, size_bits, align_bits);
  return type_id;
}

void FactEmitter::emit_translation_unit(Id tu_id, Id root_file_id,
                                        std::string_view working_dir,
                                        std::string_view compiler,
                                        std::string_view target_triple,
                                        std::string_view command_hash) {
  write_row(translation_unit_, tu_id, root_file_id, working_dir, compiler,
            target_triple, command_hash);
}

void FactEmitter::emit_compile_arg(Id tu_id, std::uint64_t arg_index,
                                   std::string_view arg) {
  write_row(compile_arg_, tu_id, arg_index, arg);
}

void FactEmitter::emit_tu_file(Id tu_id, Id file_id) {
  write_row(tu_file_, tu_id, file_id);
}

void FactEmitter::emit_include_edge(Id src_file_id, Id dst_file_id,
                                    unsigned line, bool is_system) {
  write_row(include_edge_, src_file_id, dst_file_id, line, is_system);
}

void FactEmitter::emit_macro(Id macro_id, std::string_view name,
                             bool is_function_like, std::uint64_t arity) {
  write_row(macro_, macro_id, name, is_function_like, arity);
}

void FactEmitter::emit_macro_expansion(Id expansion_id, Id macro_id) {
  write_row(macro_expansion_, expansion_id, macro_id);
}

void FactEmitter::emit_decl(Id decl_id, Id tu_id, std::string_view kind,
                            std::string_view name,
                            std::string_view qualified_name,
                            std::string_view usr, Id type_id,
                            Id parent_decl_id) {
  write_row(decl_, decl_id, tu_id, kind, name, qualified_name, usr, type_id,
            parent_decl_id);
}

void FactEmitter::emit_function_decl(Id decl_id, std::string_view mangled_name,
                                     bool is_definition,
                                     std::string_view linkage,
                                     std::string_view storage,
                                     bool is_variadic) {
  write_row(function_decl_, decl_id, mangled_name, is_definition, linkage,
            storage, is_variadic);
}

void FactEmitter::emit_param_decl(Id decl_id, Id function_decl_id,
                                  std::uint64_t param_index) {
  write_row(param_decl_, decl_id, function_decl_id, param_index);
}

void FactEmitter::emit_local_var_decl(Id decl_id, Id function_decl_id,
                                      std::string_view storage,
                                      bool has_initializer) {
  write_row(local_var_decl_, decl_id, function_decl_id, storage,
            has_initializer);
}

void FactEmitter::emit_node(Id node_id, Id tu_id, Id function_decl_id,
                            std::string_view category, std::string_view kind,
                            Id type_id) {
  write_row(node_, node_id, tu_id, function_decl_id, category, kind, type_id);
}

void FactEmitter::emit_cfg_edge(Id src_node_id, Id dst_node_id,
                                std::string_view edge_kind) {
  write_row(cfg_edge_, src_node_id, dst_node_id, edge_kind);
}

void FactEmitter::emit_ref(Id node_id, Id decl_id, std::string_view role) {
  write_row(ref_, node_id, decl_id, role);
}

void FactEmitter::emit_use(Id node_id, Id decl_id) {
  write_row(use_, node_id, decl_id);
}

void FactEmitter::emit_def(Id node_id, Id decl_id) {
  write_row(def_, node_id, decl_id);
}

void FactEmitter::emit_assign(Id node_id, Id lhs_node_id, Id rhs_node_id,
                              std::string_view op_kind) {
  write_row(assign_, node_id, lhs_node_id, rhs_node_id, op_kind);
}

void FactEmitter::emit_call(Id node_id, Id callee_decl_id,
                            std::string_view callee_name,
                            std::string_view dispatch_kind) {
  write_row(call_, node_id, callee_decl_id, callee_name, dispatch_kind);
}

void FactEmitter::emit_call_arg(Id call_node_id, std::uint64_t arg_index,
                                Id arg_node_id) {
  write_row(call_arg_, call_node_id, arg_index, arg_node_id);
}

void FactEmitter::emit_return_expr(Id return_node_id, Id expr_node_id) {
  write_row(return_expr_, return_node_id, expr_node_id);
}

void FactEmitter::emit_return_value(Id return_node_id, Id expr_node_id) {
  write_row(return_value_, return_node_id, expr_node_id);
}

void FactEmitter::emit_type_of(Id node_id, Id type_id) {
  if (is_null_id(type_id)) {
    return;
  }
  write_row(type_of_, node_id, type_id);
}

void FactEmitter::emit_diag(Id tu_id, Id file_id, unsigned line,
                            unsigned column, std::string_view severity,
                            std::string_view message) {
  write_row(diag_, tu_id, file_id, line, column, severity, message);
}

} // namespace kasane::facts
