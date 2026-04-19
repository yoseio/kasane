#pragma once

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <unordered_map>

namespace kasane::facts {

using Id = std::string;

const Id &null_id();
bool is_null_id(std::string_view id);

class FactEmitter {
public:
  explicit FactEmitter(const std::filesystem::path &directory);

  [[nodiscard]] Id next_translation_unit_id();
  [[nodiscard]] Id next_decl_id();
  [[nodiscard]] Id next_node_id();
  [[nodiscard]] Id next_macro_id();
  [[nodiscard]] Id next_macro_expansion_id();

  [[nodiscard]] Id intern_file(std::string_view path);
  [[nodiscard]] Id intern_type(std::string_view kind, std::string_view printed,
                               std::string_view canonical,
                               std::uint64_t size_bits,
                               std::uint64_t align_bits);

  void emit_translation_unit(Id tu_id, Id root_file_id,
                             std::string_view working_dir,
                             std::string_view compiler,
                             std::string_view target_triple,
                             std::string_view command_hash);
  void emit_compile_arg(Id tu_id, std::uint64_t arg_index, std::string_view arg);
  void emit_tu_file(Id tu_id, Id file_id);
  void emit_include_edge(Id src_file_id, Id dst_file_id, unsigned line,
                         bool is_system);
  void emit_macro(Id macro_id, std::string_view name, bool is_function_like,
                  std::uint64_t arity);
  void emit_macro_expansion(Id expansion_id, Id macro_id);

  void emit_decl(Id decl_id, Id tu_id, std::string_view kind,
                 std::string_view name, std::string_view qualified_name,
                 std::string_view usr, Id type_id, Id parent_decl_id);
  void emit_function_decl(Id decl_id, std::string_view mangled_name,
                          bool is_definition, std::string_view linkage,
                          std::string_view storage, bool is_variadic);
  void emit_param_decl(Id decl_id, Id function_decl_id,
                       std::uint64_t param_index);
  void emit_local_var_decl(Id decl_id, Id function_decl_id,
                           std::string_view storage, bool has_initializer);

  void emit_node(Id node_id, Id tu_id, Id function_decl_id,
                 std::string_view category, std::string_view kind, Id type_id);
  void emit_cfg_edge(Id src_node_id, Id dst_node_id, std::string_view edge_kind);

  void emit_ref(Id node_id, Id decl_id, std::string_view role);
  void emit_use(Id node_id, Id decl_id);
  void emit_def(Id node_id, Id decl_id);
  void emit_assign(Id node_id, Id lhs_node_id, Id rhs_node_id,
                   std::string_view op_kind);
  void emit_call(Id node_id, Id callee_decl_id, std::string_view callee_name,
                 std::string_view dispatch_kind);
  void emit_call_arg(Id call_node_id, std::uint64_t arg_index, Id arg_node_id);
  void emit_return_expr(Id return_node_id, Id expr_node_id);
  void emit_return_value(Id return_node_id, Id expr_node_id);
  void emit_type_of(Id node_id, Id type_id);

  void emit_diag(Id tu_id, Id file_id, unsigned line, unsigned column,
                 std::string_view severity, std::string_view message);

private:
  static std::ofstream open_fact_file(const std::filesystem::path &path);
  static Id make_id(std::string_view prefix, std::uint64_t counter);

  std::uint64_t next_translation_unit_id_ = 1;
  std::uint64_t next_decl_id_ = 1;
  std::uint64_t next_node_id_ = 1;
  std::uint64_t next_macro_id_ = 1;
  std::uint64_t next_macro_expansion_id_ = 1;
  std::uint64_t next_file_id_ = 1;
  std::uint64_t next_type_id_ = 1;

  std::unordered_map<std::string, Id> file_ids_;
  std::unordered_map<std::string, Id> type_ids_;

  std::ofstream file_;
  std::ofstream translation_unit_;
  std::ofstream compile_arg_;
  std::ofstream tu_file_;
  std::ofstream include_edge_;
  std::ofstream macro_;
  std::ofstream macro_expansion_;
  std::ofstream type_;
  std::ofstream decl_;
  std::ofstream function_decl_;
  std::ofstream param_decl_;
  std::ofstream local_var_decl_;
  std::ofstream node_;
  std::ofstream cfg_edge_;
  std::ofstream ref_;
  std::ofstream use_;
  std::ofstream def_;
  std::ofstream assign_;
  std::ofstream call_;
  std::ofstream call_arg_;
  std::ofstream return_expr_;
  std::ofstream return_value_;
  std::ofstream type_of_;
  std::ofstream diag_;
};

} // namespace kasane::facts
