#include "fact_extract_action.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
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
#include <utility>

namespace {

namespace fs = std::filesystem;

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

class PersistingDiagnosticConsumer : public clang::DiagnosticConsumer {
public:
  PersistingDiagnosticConsumer(kasane::facts::FactEmitter &out,
                               std::string tu_id)
      : out_(out), tu_id_(std::move(tu_id)) {}

  void HandleDiagnostic(clang::DiagnosticsEngine::Level level,
                        const clang::Diagnostic &info) override {
    clang::DiagnosticConsumer::HandleDiagnostic(level, info);

    llvm::SmallString<256> message_buffer;
    info.FormatDiagnostic(message_buffer);

    out_.emit_diag(tu_id_, diagnostic_level_name(level), {}, 0, 0,
                   message_buffer.str().str());
  }

private:
  kasane::facts::FactEmitter &out_;
  std::string tu_id_;
};

class BuildContextPPCallbacks : public clang::PPCallbacks {
public:
  BuildContextPPCallbacks(
      clang::Preprocessor &preprocessor, kasane::facts::FactEmitter &out,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : preprocessor_(preprocessor),
        source_manager_(preprocessor.getSourceManager()), out_(out),
        context_(std::move(context)) {
    seen_files_.insert(context_.main_file);
  }

  void FileChanged(clang::SourceLocation loc, FileChangeReason reason,
                   clang::SrcMgr::CharacteristicKind file_type,
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

    out_.emit_file(context_.tu_id, file_path,
                   classify_file_kind(loc, file_type));
  }

  void InclusionDirective(
      clang::SourceLocation hash_loc, const clang::Token & /*include_tok*/,
      llvm::StringRef file_name, bool is_angled,
      clang::CharSourceRange /*filename_range*/, const clang::FileEntry *file,
      llvm::StringRef /*search_path*/, llvm::StringRef /*relative_path*/,
      const clang::Module * /*imported*/,
      clang::SrcMgr::CharacteristicKind /*file_type*/) override {
    const std::string including_file =
        path_from_source_location(source_manager_, hash_loc);
    const std::string included_path =
        file != nullptr ? path_from_file_entry(*file) : std::string();

    out_.emit_include(context_.tu_id, including_file,
                      line_from_source_location(source_manager_, hash_loc),
                      included_path, file_name.str(), is_angled);
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

    out_.emit_macro(context_.tu_id, "define", identifier->getName().str(),
                    render_macro_value(*macro_info, preprocessor_),
                    path_from_source_location(source_manager_, definition_loc),
                    line_from_source_location(source_manager_, definition_loc));
  }

  void MacroUndefined(const clang::Token &macro_name_tok,
                      const clang::MacroDefinition & /*definition*/,
                      const clang::MacroDirective * /*undef*/) override {
    const clang::IdentifierInfo *identifier =
        macro_name_tok.getIdentifierInfo();
    if (identifier == nullptr ||
        !should_record_macro_location(macro_name_tok.getLocation())) {
      return;
    }

    out_.emit_macro(context_.tu_id, "undef", identifier->getName().str(), {},
                    path_from_source_location(source_manager_,
                                              macro_name_tok.getLocation()),
                    line_from_source_location(source_manager_,
                                              macro_name_tok.getLocation()));
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

  std::string
  classify_file_kind(clang::SourceLocation location,
                     clang::SrcMgr::CharacteristicKind file_type) const {
    if (source_manager_.isWrittenInBuiltinFile(location)) {
      return "builtin";
    }

    if (source_manager_.isWrittenInCommandLineFile(location)) {
      return "command-line";
    }

    switch (file_type) {
    case clang::SrcMgr::C_User:
      return "user";
    case clang::SrcMgr::C_User_ModuleMap:
      return "user-module-map";
    case clang::SrcMgr::C_System:
      return "system";
    case clang::SrcMgr::C_ExternCSystem:
      return "extern-c-system";
    case clang::SrcMgr::C_System_ModuleMap:
      return "system-module-map";
    }

    if (location.isValid()) {
      return "user";
    }

    return "unknown";
  }

  clang::Preprocessor &preprocessor_;
  clang::SourceManager &source_manager_;
  kasane::facts::FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
  std::set<std::string> seen_files_;
};

class FactExtractVisitor
    : public clang::RecursiveASTVisitor<FactExtractVisitor> {
public:
  FactExtractVisitor(clang::ASTContext *ctx, kasane::facts::FactEmitter &out)
      : ctx_(ctx), out_(out) {}

  bool TraverseFunctionDecl(clang::FunctionDecl *function_decl) {
    if (function_decl == nullptr) {
      return true;
    }

    const bool prev_in_function = in_function_;
    const std::string prev_function_name = current_function_;

    if (function_decl->hasBody() &&
        ctx_->getSourceManager().isWrittenInMainFile(
            function_decl->getLocation())) {
      in_function_ = true;
      current_function_ = function_decl->getQualifiedNameAsString();

      const clang::FullSourceLoc loc =
          ctx_->getFullLoc(function_decl->getBeginLoc());
      if (loc.isValid()) {
        out_.emit_function(current_function_, loc.getSpellingLineNumber());
      }

      for (unsigned index = 0; index < function_decl->getNumParams(); ++index) {
        const clang::ParmVarDecl *param_decl =
            function_decl->getParamDecl(index);
        out_.emit_param(current_function_, param_decl->getNameAsString(),
                        index + 1U);
      }
    }

    clang::RecursiveASTVisitor<FactExtractVisitor>::TraverseFunctionDecl(
        function_decl);

    in_function_ = prev_in_function;
    current_function_ = prev_function_name;
    return true;
  }

  bool VisitVarDecl(clang::VarDecl *var_decl) {
    if (!in_function_ || !var_decl->hasInit()) {
      return true;
    }

    const clang::Expr *init = var_decl->getInit()->IgnoreParenImpCasts();
    if (const auto *rhs = clang::dyn_cast<clang::DeclRefExpr>(init)) {
      out_.emit_assign(current_function_, var_decl->getNameAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitBinaryOperator(clang::BinaryOperator *op) {
    if (!in_function_ || !op->isAssignmentOp()) {
      return true;
    }

    const clang::Expr *lhs_expr = op->getLHS()->IgnoreParenImpCasts();
    const clang::Expr *rhs_expr = op->getRHS()->IgnoreParenImpCasts();

    const auto *lhs = clang::dyn_cast<clang::DeclRefExpr>(lhs_expr);
    const auto *rhs = clang::dyn_cast<clang::DeclRefExpr>(rhs_expr);
    if (lhs != nullptr && rhs != nullptr) {
      out_.emit_assign(current_function_, lhs->getNameInfo().getAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitCallExpr(clang::CallExpr *call) {
    if (!in_function_) {
      return true;
    }

    const clang::FunctionDecl *callee = call->getDirectCallee();
    if (callee == nullptr) {
      return true;
    }

    const clang::FullSourceLoc loc = ctx_->getFullLoc(call->getExprLoc());
    if (!loc.isValid()) {
      return true;
    }

    out_.emit_callsite(current_function_, loc.getSpellingLineNumber(),
                       callee->getQualifiedNameAsString());
    return true;
  }

  bool VisitReturnStmt(clang::ReturnStmt *ret) {
    if (!in_function_ || ret->getRetValue() == nullptr) {
      return true;
    }

    const clang::Expr *expr = ret->getRetValue()->IgnoreParenImpCasts();
    if (const auto *decl_ref = clang::dyn_cast<clang::DeclRefExpr>(expr)) {
      out_.emit_return_var(current_function_,
                           decl_ref->getNameInfo().getAsString());
    }

    return true;
  }

private:
  clang::ASTContext *ctx_;
  kasane::facts::FactEmitter &out_;
  bool in_function_ = false;
  std::string current_function_;
};

class FactExtractConsumer : public clang::ASTConsumer {
public:
  FactExtractConsumer(clang::ASTContext *ctx, kasane::facts::FactEmitter &out)
      : visitor_(ctx, out) {}

  void HandleTranslationUnit(clang::ASTContext &ctx) override {
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  FactExtractVisitor visitor_;
};

class FactExtractAction : public clang::ASTFrontendAction {
public:
  FactExtractAction(kasane::facts::FactEmitter &out,
                    kasane::frontends::clangcpp::TranslationUnitContext context)
      : out_(out), context_(std::move(context)) {}

  bool BeginSourceFileAction(clang::CompilerInstance &ci) override {
    if (!clang::ASTFrontendAction::BeginSourceFileAction(ci)) {
      return false;
    }

    ci.getDiagnostics().setClient(
        new PersistingDiagnosticConsumer(out_, context_.tu_id), true);
    ci.getPreprocessor().addPPCallbacks(
        std::make_unique<BuildContextPPCallbacks>(ci.getPreprocessor(), out_,
                                                  context_));
    return true;
  }

  std::unique_ptr<clang::ASTConsumer>
  CreateASTConsumer(clang::CompilerInstance &ci,
                    llvm::StringRef /*unused*/) override {
    return std::make_unique<FactExtractConsumer>(&ci.getASTContext(), out_);
  }

  void EndSourceFileAction() override {
    clang::ASTFrontendAction::EndSourceFileAction();
  }

private:
  kasane::facts::FactEmitter &out_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
};

class FactExtractActionFactory : public clang::tooling::FrontendActionFactory {
public:
  FactExtractActionFactory(
      kasane::facts::FactEmitter &emitter,
      kasane::frontends::clangcpp::TranslationUnitContext context)
      : emitter_(emitter), context_(std::move(context)) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<FactExtractAction>(emitter_, context_);
  }

private:
  kasane::facts::FactEmitter &emitter_;
  kasane::frontends::clangcpp::TranslationUnitContext context_;
};

} // namespace

namespace kasane::frontends::clangcpp {

std::unique_ptr<clang::tooling::FrontendActionFactory>
create_fact_extract_action_factory(kasane::facts::FactEmitter &emitter,
                                   TranslationUnitContext context) {
  return std::make_unique<FactExtractActionFactory>(emitter,
                                                    std::move(context));
}

} // namespace kasane::frontends::clangcpp
