#include "fact_extract_action.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Analysis/CFG.h"
#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileEntry.h"
#include "clang/Basic/SourceManager.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Lex/MacroInfo.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "llvm/ADT/SmallString.h"

#include "kasane/facts/fact_emitter.hpp"

#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using kasane::facts::FactEmitter;
using kasane::facts::Id;
using kasane::facts::is_null_id;
using kasane::facts::null_id;

std::string normalize_path_like(std::string_view raw_path) {
  if (raw_path.empty()) {
    return {};
  }

  if (raw_path.front() == '<' && raw_path.back() == '>') {
    return std::string(raw_path);
  }

  std::error_code err;
  const fs::path path(raw_path);
  const fs::path canonical = fs::weakly_canonical(path, err);
  if (!err) {
    return canonical.lexically_normal().string();
  }

  return fs::absolute(path).lexically_normal().string();
}

std::string path_from_file_entry(const clang::FileEntry &file) {
  const llvm::StringRef real_path = file.tryGetRealPathName();
  if (!real_path.empty()) {
    return normalize_path_like(real_path.str());
  }

  return normalize_path_like(file.getName().str());
}

std::string
path_from_source_location(const clang::SourceManager &source_manager,
                          clang::SourceLocation location) {
  if (!location.isValid()) {
    return {};
  }

  const clang::PresumedLoc presumed =
      source_manager.getPresumedLoc(source_manager.getExpansionLoc(location));
  if (presumed.isInvalid() || presumed.getFilename() == nullptr) {
    return {};
  }

  return normalize_path_like(presumed.getFilename());
}

unsigned line_from_source_location(const clang::SourceManager &source_manager,
                                   clang::SourceLocation location) {
  if (!location.isValid()) {
    return 0;
  }

  const clang::PresumedLoc presumed =
      source_manager.getPresumedLoc(source_manager.getExpansionLoc(location));
  if (presumed.isInvalid()) {
    return 0;
  }

  return presumed.getLine();
}

unsigned column_from_source_location(const clang::SourceManager &source_manager,
                                     clang::SourceLocation location) {
  if (!location.isValid()) {
    return 0;
  }

  const clang::PresumedLoc presumed =
      source_manager.getPresumedLoc(source_manager.getExpansionLoc(location));
  if (presumed.isInvalid()) {
    return 0;
  }

  return presumed.getColumn();
}

bool is_written_in_main_file(const clang::SourceManager &source_manager,
                             clang::SourceLocation location) {
  if (!location.isValid()) {
    return false;
  }

  return source_manager.isWrittenInMainFile(
      source_manager.getExpansionLoc(location));
}

std::string diagnostic_level_name(clang::DiagnosticsEngine::Level level) {
  switch (level) {
  case clang::DiagnosticsEngine::Ignored:
    return "ignored";
  case clang::DiagnosticsEngine::Note:
    return "note";
  case clang::DiagnosticsEngine::Remark:
    return "remark";
  case clang::DiagnosticsEngine::Warning:
    return "warning";
  case clang::DiagnosticsEngine::Error:
    return "error";
  case clang::DiagnosticsEngine::Fatal:
    return "fatal";
  }

  return "unknown";
}

std::string render_macro_value(const clang::MacroInfo &macro_info,
                               const clang::Preprocessor &preprocessor) {
  std::string value;
  bool first = true;

  for (const clang::Token &token : macro_info.tokens()) {
    if (!first) {
      value.push_back(' ');
    }

    value.append(preprocessor.getSpelling(token));
    first = false;
  }

  return value;
}

std::string storage_class_name(clang::StorageClass storage_class) {
  switch (storage_class) {
  case clang::SC_None:
    return "none";
  case clang::SC_Extern:
    return "extern";
  case clang::SC_Static:
    return "static";
  case clang::SC_PrivateExtern:
    return "private_extern";
  case clang::SC_Auto:
    return "auto";
  case clang::SC_Register:
    return "register";
  }

  return "none";
}

std::string function_linkage_name(const clang::FunctionDecl &function_decl) {
  return function_decl.isExternallyVisible() ? "external" : "internal";
}

std::string type_kind_name(clang::QualType type) {
  const clang::Type *type_ptr = type.getTypePtrOrNull();
  if (type_ptr == nullptr) {
    return "unknown";
  }

  if (type_ptr->isBuiltinType()) {
    return "builtin";
  }
  if (type_ptr->isPointerType()) {
    return "pointer";
  }
  if (type_ptr->isReferenceType()) {
    return "reference";
  }
  if (type_ptr->isArrayType()) {
    return "array";
  }
  if (type_ptr->isFunctionType()) {
    return "function_proto";
  }
  if (type_ptr->isRecordType()) {
    return "record";
  }
  if (type_ptr->isEnumeralType()) {
    return "enum";
  }
  if (type_ptr->getTypeClass() == clang::Type::Typedef) {
    return "typedef";
  }

  return type_ptr->getTypeClassName();
}

class PersistingDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  PersistingDiagnosticConsumer(clang::SourceManager &source_manager,
                               FactEmitter &out,
                               kasane::frontends::clangcpp::TranslationUnitContext
                                   context)
      : source_manager_(source_manager), out_(out), context_(std::move(context)) {
  }

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    clang::DiagnosticConsumer::HandleDiagnostic(level, info);

    llvm::SmallString<256> message_buffer;
    info.FormatDiagnostic(message_buffer);

    const std::string path =
        path_from_source_location(source_manager_, info.getLocation());
    const Id file_id = path.empty() ? null_id() : out_.intern_file(path);
    out_.emit_diag(context_.tu_id, file_id,
                   line_from_source_location(source_manager_, info.getLocation()),
                   column_from_source_location(source_manager_,
                                               info.getLocation()),
                   diagnostic_level_name(level), message_buffer.str().str());
  }

private:
  clang::SourceManager &source_manager_;
  FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
};

class BuildContextPPCallbacks : public clang::PPCallbacks {
public:
  BuildContextPPCallbacks(
      clang::Preprocessor &preprocessor, FactEmitter &out,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : preprocessor_(preprocessor),
        source_manager_(preprocessor.getSourceManager()), out_(out),
        context_(std::move(context)) {
    seen_files_.insert(context_.main_file);
  }

  void FileChanged(clang::SourceLocation loc, FileChangeReason reason,
                   clang::SrcMgr::CharacteristicKind /*file_type*/,
                   clang::FileID /*prev_fid*/) override {
    if (reason != clang::PPCallbacks::EnterFile &&
        reason != clang::PPCallbacks::RenameFile) {
      return;
    }

    const std::string file_path =
        path_from_source_location(source_manager_, loc);
    if (file_path.empty() || !seen_files_.insert(file_path).second) {
      return;
    }

    const Id file_id = out_.intern_file(file_path);
    out_.emit_tu_file(context_.tu_id, file_id);
  }

  void InclusionDirective(
      clang::SourceLocation hash_loc, const clang::Token & /*include_tok*/,
      llvm::StringRef /*file_name*/, bool is_angled,
      clang::CharSourceRange /*filename_range*/, const clang::FileEntry *file,
      llvm::StringRef /*search_path*/, llvm::StringRef /*relative_path*/,
      const clang::Module * /*imported*/,
      clang::SrcMgr::CharacteristicKind /*file_type*/) override {
    const std::string including_file =
        path_from_source_location(source_manager_, hash_loc);
    const std::string included_path =
        file != nullptr ? path_from_file_entry(*file) : std::string();
    if (including_file.empty() || included_path.empty()) {
      return;
    }

    out_.emit_include_edge(out_.intern_file(including_file),
                           out_.intern_file(included_path),
                           line_from_source_location(source_manager_, hash_loc),
                           is_angled);
  }

  void MacroDefined(const clang::Token &macro_name_tok,
                    const clang::MacroDirective *directive) override {
    if (directive == nullptr) {
      return;
    }

    const clang::IdentifierInfo *identifier =
        macro_name_tok.getIdentifierInfo();
    const clang::MacroInfo *macro_info = directive->getMacroInfo();
    if (identifier == nullptr || macro_info == nullptr) {
      return;
    }

    const clang::SourceLocation definition_loc = macro_info->getDefinitionLoc();
    if (!should_record_macro_location(definition_loc)) {
      return;
    }

    const Id macro_id = out_.next_macro_id();
    out_.emit_macro(macro_id, identifier->getName().str(),
                    macro_info->isFunctionLike(), macro_info->getNumParams());
    (void)render_macro_value(*macro_info, preprocessor_);
  }

private:
  bool should_record_macro_location(clang::SourceLocation location) const {
    if (!location.isValid()) {
      return false;
    }

    const clang::SourceLocation spelling_loc =
        source_manager_.getSpellingLoc(location);
    if (source_manager_.isWrittenInBuiltinFile(spelling_loc) ||
        source_manager_.isWrittenInCommandLineFile(spelling_loc) ||
        source_manager_.isInSystemHeader(spelling_loc)) {
      return false;
    }

    return true;
  }

  clang::Preprocessor &preprocessor_;
  clang::SourceManager &source_manager_;
  FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
  std::set<std::string> seen_files_;
};

class FunctionExtractor;

class ReferencedDeclCollector
    : public clang::RecursiveASTVisitor<ReferencedDeclCollector> {
public:
  explicit ReferencedDeclCollector(FunctionExtractor &owner) : owner_(owner) {}

  bool VisitDeclRefExpr(clang::DeclRefExpr *expr);
  std::vector<Id> take() && { return {decl_ids_.begin(), decl_ids_.end()}; }

private:
  FunctionExtractor &owner_;
  std::set<Id> decl_ids_;
};

class FunctionExtractor
    : public clang::RecursiveASTVisitor<FunctionExtractor> {
public:
  FunctionExtractor(
      clang::ASTContext &ctx, FactEmitter &out,
      const kasane::frontends::clangcpp::TranslationUnitContext &context,
      std::unordered_map<const clang::Decl *, Id> &decl_ids,
      clang::FunctionDecl &function_decl, Id function_decl_id)
      : ctx_(ctx), source_manager_(ctx.getSourceManager()), out_(out),
        context_(context), decl_ids_(decl_ids), function_decl_(function_decl),
        function_decl_id_(function_decl_id) {}

  void extract() {
    emit_param_decls();

    entry_node_id_ =
        emit_synthetic_node("FunctionEntry", /*type_id=*/null_id());
    exit_node_id_ = emit_synthetic_node("FunctionExit", /*type_id=*/null_id());

    if (clang::Stmt *body = function_decl_.getBody()) {
      TraverseStmt(body);
      emit_cfg(body);
    } else {
      emit_cfg_edge(entry_node_id_, exit_node_id_);
    }
  }

  Id decl_id_for(const clang::Decl *decl) const {
    if (decl == nullptr) {
      return null_id();
    }

    const clang::Decl *canonical = decl->getCanonicalDecl();
    const auto it = decl_ids_.find(canonical);
    return it == decl_ids_.end() ? null_id() : it->second;
  }

  bool VisitVarDecl(clang::VarDecl *var_decl) {
    if (var_decl == nullptr || clang::isa<clang::ParmVarDecl>(var_decl) ||
        !var_decl->isLocalVarDecl() ||
        !is_written_in_main_file(source_manager_, var_decl->getLocation())) {
      return true;
    }

    const Id decl_id = ensure_local_var_decl(*var_decl);
    if (!is_null_id(decl_id) &&
        emitted_local_var_decl_ids_.insert(decl_id).second) {
      out_.emit_local_var_decl(decl_id, function_decl_id_,
                               storage_class_name(var_decl->getStorageClass()),
                               var_decl->hasInit());
    }
    return true;
  }

  bool VisitDeclStmt(clang::DeclStmt *decl_stmt) {
    if (decl_stmt == nullptr ||
        !is_written_in_main_file(source_manager_, decl_stmt->getBeginLoc())) {
      return true;
    }

    const Id stmt_node_id = ensure_node(decl_stmt);
    for (clang::Decl *decl : decl_stmt->decls()) {
      auto *var_decl = clang::dyn_cast<clang::VarDecl>(decl);
      if (var_decl == nullptr || clang::isa<clang::ParmVarDecl>(var_decl) ||
          !var_decl->isLocalVarDecl()) {
        continue;
      }

      const Id decl_id = ensure_local_var_decl(*var_decl);
      if (is_null_id(decl_id) || !var_decl->hasInit()) {
        continue;
      }

      out_.emit_def(stmt_node_id, decl_id);
      emit_use_facts_for_expr(stmt_node_id, var_decl->getInit());
    }

    return true;
  }

  bool VisitBinaryOperator(clang::BinaryOperator *op) {
    if (op == nullptr || !op->isAssignmentOp() ||
        !is_written_in_main_file(source_manager_, op->getOperatorLoc())) {
      return true;
    }

    const Id stmt_node_id = ensure_node(op);
    if (const Id lhs_decl_id = direct_decl_ref_id(op->getLHS());
        !is_null_id(lhs_decl_id)) {
      out_.emit_def(stmt_node_id, lhs_decl_id);
      emit_direct_ref(op->getLHS(), "write");
    }

    emit_use_facts_for_expr(stmt_node_id, op->getRHS());
    return true;
  }

  bool VisitCallExpr(clang::CallExpr *call) {
    if (call == nullptr ||
        !is_written_in_main_file(source_manager_, call->getExprLoc())) {
      return true;
    }

    const Id call_node_id = ensure_node(call);
    const clang::FunctionDecl *callee = call->getDirectCallee();
    out_.emit_call(call_node_id, decl_id_for(callee),
                   callee != nullptr ? callee->getQualifiedNameAsString() : "",
                   callee != nullptr ? "direct" : "indirect");

    for (unsigned index = 0; index < call->getNumArgs(); ++index) {
      const clang::Expr *arg = call->getArg(index);
      if (arg == nullptr) {
        continue;
      }

      const clang::Expr *normalized = arg->IgnoreParenImpCasts();
      const Id arg_node_id = ensure_node(normalized);
      out_.emit_call_arg(call_node_id, index, arg_node_id);
      emit_direct_ref(normalized, "read");
      emit_use_facts_for_expr(call_node_id, normalized);
    }

    return true;
  }

  bool VisitReturnStmt(clang::ReturnStmt *ret) {
    if (ret == nullptr ||
        !is_written_in_main_file(source_manager_, ret->getReturnLoc())) {
      return true;
    }

    const Id return_node_id = ensure_node(ret);
    const clang::Expr *value = ret->getRetValue();
    if (value == nullptr) {
      out_.emit_return_expr(return_node_id, null_id());
      out_.emit_return_value(return_node_id, null_id());
      return true;
    }

    const clang::Expr *normalized = value->IgnoreParenImpCasts();
    const Id expr_node_id = ensure_node(normalized);
    out_.emit_return_expr(return_node_id, expr_node_id);
    out_.emit_return_value(return_node_id, expr_node_id);
    emit_direct_ref(normalized, "read");
    emit_use_facts_for_expr(return_node_id, normalized);
    return true;
  }

private:
  void emit_param_decls() {
    for (unsigned index = 0; index < function_decl_.getNumParams(); ++index) {
      clang::ParmVarDecl *param_decl = function_decl_.getParamDecl(index);
      if (param_decl == nullptr) {
        continue;
      }

      const clang::Decl *canonical = param_decl->getCanonicalDecl();
      if (decl_ids_.contains(canonical)) {
        continue;
      }

      const Id decl_id = out_.next_decl_id();
      decl_ids_.emplace(canonical, decl_id);

      const Id type_id = intern_type(param_decl->getType());
      out_.emit_decl(decl_id, context_.tu_id, "param",
                     param_decl->getNameAsString(),
                     param_decl->getQualifiedNameAsString(), {}, type_id,
                     function_decl_id_);
      out_.emit_param_decl(decl_id, function_decl_id_, index);
    }
  }

  Id ensure_local_var_decl(const clang::VarDecl &var_decl) {
    const clang::Decl *canonical = var_decl.getCanonicalDecl();
    if (const auto it = decl_ids_.find(canonical); it != decl_ids_.end()) {
      return it->second;
    }

    const Id decl_id = out_.next_decl_id();
    decl_ids_.emplace(canonical, decl_id);

    const Id type_id = intern_type(var_decl.getType());
    out_.emit_decl(decl_id, context_.tu_id, "local_var",
                   var_decl.getNameAsString(),
                   var_decl.getQualifiedNameAsString(), {}, type_id,
                   function_decl_id_);
    return decl_id;
  }

  Id intern_type(clang::QualType type) {
    if (type.isNull()) {
      return null_id();
    }

    std::uint64_t size_bits = 0U;
    std::uint64_t align_bits = 0U;
    if (!type->isDependentType() && !type->isIncompleteType()) {
      const auto info = ctx_.getTypeInfo(type);
      size_bits = info.Width;
      align_bits = info.Align;
    }

    return out_.intern_type(type_kind_name(type), type.getAsString(),
                            type.getCanonicalType().getAsString(), size_bits,
                            align_bits);
  }

  Id emit_synthetic_node(std::string_view kind, Id type_id) {
    const Id node_id = out_.next_node_id();
    out_.emit_node(node_id, context_.tu_id, function_decl_id_, "synthetic",
                   kind, type_id);
    out_.emit_type_of(node_id, type_id);
    return node_id;
  }

  Id ensure_node(const clang::Stmt *stmt) {
    if (stmt == nullptr) {
      return null_id();
    }

    const auto it = node_ids_.find(stmt);
    if (it != node_ids_.end()) {
      return it->second;
    }

    Id type_id = null_id();
    std::string category = "stmt";
    if (const auto *expr = clang::dyn_cast<clang::Expr>(stmt)) {
      category = "expr";
      type_id = intern_type(expr->getType());
    }

    const Id node_id = out_.next_node_id();
    node_ids_.emplace(stmt, node_id);
    out_.emit_node(node_id, context_.tu_id, function_decl_id_, category,
                   stmt->getStmtClassName(), type_id);
    out_.emit_type_of(node_id, type_id);
    return node_id;
  }

  void emit_cfg(clang::Stmt *body) {
    clang::CFG::BuildOptions build_options;
    std::unique_ptr<clang::CFG> cfg =
        clang::CFG::buildCFG(&function_decl_, body, &ctx_, build_options);
    if (!cfg) {
      emit_cfg_edge(entry_node_id_, exit_node_id_);
      return;
    }

    for (clang::CFGBlock *block : *cfg) {
      (void)anchor_for_block(cfg.get(), block);
    }

    for (clang::CFGBlock *block : *cfg) {
      Id previous = anchor_for_block(cfg.get(), block);

      for (const clang::CFGElement &element : *block) {
        if (auto stmt = element.getAs<clang::CFGStmt>()) {
          const clang::Stmt *ast_stmt = stmt->getStmt();
          if (ast_stmt == nullptr ||
              !is_written_in_main_file(source_manager_, ast_stmt->getBeginLoc())) {
            continue;
          }

          const Id stmt_node_id = ensure_node(ast_stmt);
          emit_cfg_edge(previous, stmt_node_id);
          previous = stmt_node_id;
        }
      }

      if (const clang::Stmt *terminator = block->getTerminatorStmt();
          terminator != nullptr &&
          is_written_in_main_file(source_manager_, terminator->getBeginLoc())) {
        const Id terminator_node_id = ensure_node(terminator);
        emit_cfg_edge(previous, terminator_node_id);
        previous = terminator_node_id;
      }

      for (clang::CFGBlock::const_succ_iterator succ_it = block->succ_begin();
           succ_it != block->succ_end(); ++succ_it) {
        const clang::CFGBlock *successor = succ_it->getReachableBlock();
        if (successor == nullptr) {
          continue;
        }

        emit_cfg_edge(previous, anchor_for_block(cfg.get(), successor));
      }
    }
  }

  Id anchor_for_block(const clang::CFG *cfg, const clang::CFGBlock *block) {
    if (block == nullptr) {
      return null_id();
    }

    if (block == &cfg->getEntry()) {
      return entry_node_id_;
    }
    if (block == &cfg->getExit()) {
      return exit_node_id_;
    }

    const auto it = block_anchor_ids_.find(block);
    if (it != block_anchor_ids_.end()) {
      return it->second;
    }

    const Id block_node_id = emit_synthetic_node("CFGBlock", null_id());
    block_anchor_ids_.emplace(block, block_node_id);
    return block_node_id;
  }

  void emit_cfg_edge(Id src_node_id, Id dst_node_id) {
    if (is_null_id(src_node_id) || is_null_id(dst_node_id)) {
      return;
    }

    if (!emitted_cfg_edges_.emplace(src_node_id, dst_node_id).second) {
      return;
    }

    out_.emit_cfg_edge(src_node_id, dst_node_id, "normal");
  }

  Id direct_decl_ref_id(const clang::Expr *expr) const {
    if (expr == nullptr) {
      return null_id();
    }

    const clang::Expr *normalized = expr->IgnoreParenImpCasts();
    const auto *decl_ref = clang::dyn_cast<clang::DeclRefExpr>(normalized);
    return decl_ref == nullptr ? null_id() : decl_id_for(decl_ref->getDecl());
  }

  void emit_direct_ref(const clang::Expr *expr, std::string_view role) {
    if (expr == nullptr) {
      return;
    }

    const clang::Expr *normalized = expr->IgnoreParenImpCasts();
    const auto *decl_ref = clang::dyn_cast<clang::DeclRefExpr>(normalized);
    if (decl_ref == nullptr) {
      return;
    }

    const Id decl_id = decl_id_for(decl_ref->getDecl());
    if (is_null_id(decl_id)) {
      return;
    }

    out_.emit_ref(ensure_node(normalized), decl_id, role);
  }

  void emit_use_facts_for_expr(Id node_id, const clang::Expr *expr) {
    if (expr == nullptr || is_null_id(node_id)) {
      return;
    }

    ReferencedDeclCollector collector(*this);
    collector.TraverseStmt(const_cast<clang::Expr *>(expr));
    const std::vector<Id> referenced_decl_ids = std::move(collector).take();
    for (const Id &decl_id : referenced_decl_ids) {
      out_.emit_use(node_id, decl_id);
    }
  }

  clang::ASTContext &ctx_;
  clang::SourceManager &source_manager_;
  FactEmitter &out_;
  const kasane::frontends::clangcpp::TranslationUnitContext &context_;
  std::unordered_map<const clang::Decl *, Id> &decl_ids_;
  clang::FunctionDecl &function_decl_;
  Id function_decl_id_;
  Id entry_node_id_ = null_id();
  Id exit_node_id_ = null_id();
  std::unordered_map<const clang::Stmt *, Id> node_ids_;
  std::unordered_map<const clang::CFGBlock *, Id> block_anchor_ids_;
  std::set<std::pair<Id, Id>> emitted_cfg_edges_;
  std::set<Id> emitted_local_var_decl_ids_;
};

bool ReferencedDeclCollector::VisitDeclRefExpr(clang::DeclRefExpr *expr) {
  if (expr == nullptr) {
    return true;
  }

  const Id decl_id = owner_.decl_id_for(expr->getDecl());
  if (!is_null_id(decl_id)) {
    decl_ids_.insert(decl_id);
  }
  return true;
}

class FactExtractVisitor
    : public clang::RecursiveASTVisitor<FactExtractVisitor> {
public:
  FactExtractVisitor(
      clang::ASTContext &ctx, FactEmitter &out,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : ctx_(ctx), source_manager_(ctx.getSourceManager()), out_(out),
        context_(std::move(context)) {}

  bool VisitFunctionDecl(clang::FunctionDecl *function_decl) {
    if (function_decl == nullptr ||
        !is_written_in_main_file(source_manager_, function_decl->getLocation())) {
      return true;
    }

    const clang::Decl *canonical = function_decl->getCanonicalDecl();
    Id function_decl_id = null_id();
    if (const auto it = decl_ids_.find(canonical); it != decl_ids_.end()) {
      function_decl_id = it->second;
    } else {
      function_decl_id = out_.next_decl_id();
      decl_ids_.emplace(canonical, function_decl_id);

      const Id type_id = intern_type(function_decl->getType());
      out_.emit_decl(function_decl_id, context_.tu_id, "function",
                     function_decl->getNameAsString(),
                     function_decl->getQualifiedNameAsString(), {}, type_id,
                     null_id());
      out_.emit_function_decl(function_decl_id,
                              function_decl->getQualifiedNameAsString(),
                              function_decl->hasBody(),
                              function_linkage_name(*function_decl),
                              storage_class_name(
                                  function_decl->getStorageClass()),
                              function_decl->isVariadic());
    }

    if (function_decl->hasBody() && function_decl->isThisDeclarationADefinition()) {
      FunctionExtractor extractor(ctx_, out_, context_, decl_ids_,
                                  *function_decl, function_decl_id);
      extractor.extract();
    }

    return true;
  }

private:
  Id intern_type(clang::QualType type) {
    if (type.isNull()) {
      return null_id();
    }

    std::uint64_t size_bits = 0U;
    std::uint64_t align_bits = 0U;
    if (!type->isDependentType() && !type->isIncompleteType()) {
      const auto info = ctx_.getTypeInfo(type);
      size_bits = info.Width;
      align_bits = info.Align;
    }

    return out_.intern_type(type_kind_name(type), type.getAsString(),
                            type.getCanonicalType().getAsString(), size_bits,
                            align_bits);
  }

  clang::ASTContext &ctx_;
  clang::SourceManager &source_manager_;
  FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
  std::unordered_map<const clang::Decl *, Id> decl_ids_;
};

class FactExtractConsumer : public clang::ASTConsumer {
public:
  FactExtractConsumer(
      clang::ASTContext &ctx, FactEmitter &out,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : visitor_(ctx, out, std::move(context)) {}

  void HandleTranslationUnit(clang::ASTContext &ctx) override {
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  FactExtractVisitor visitor_;
};

class FactExtractAction : public clang::ASTFrontendAction {
public:
  FactExtractAction(FactEmitter &out,
                    kasane::frontends::clangcpp::TranslationUnitContext context)
      : out_(out), context_(std::move(context)) {}

  bool BeginSourceFileAction(clang::CompilerInstance &ci) override {
    if (!clang::ASTFrontendAction::BeginSourceFileAction(ci)) {
      return false;
    }

    ci.getDiagnostics().setClient(new PersistingDiagnosticConsumer(
                                      ci.getSourceManager(), out_, context_),
                                  true);
    ci.getPreprocessor().addPPCallbacks(
        std::make_unique<BuildContextPPCallbacks>(ci.getPreprocessor(), out_,
                                                  context_));
    return true;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci,
                    llvm::StringRef /*unused*/) override {
    return std::make_unique<FactExtractConsumer>(ci.getASTContext(), out_,
                                                 context_);
  }

private:
  FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
};

class FactExtractActionFactory : public clang::tooling::FrontendActionFactory {
public:
  FactExtractActionFactory(
      FactEmitter &emitter,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : emitter_(emitter), context_(std::move(context)) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<FactExtractAction>(emitter_, context_);
  }

private:
  FactEmitter &emitter_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
};

} // namespace

namespace kasane::frontends::clangcpp {

std::unique_ptr<clang::tooling::FrontendActionFactory>
create_fact_extract_action_factory(FactEmitter &emitter,
                                   TranslationUnitContext context) {
  return std::make_unique<FactExtractActionFactory>(emitter,
                                                    std::move(context));
}

} // namespace kasane::frontends::clangcpp
