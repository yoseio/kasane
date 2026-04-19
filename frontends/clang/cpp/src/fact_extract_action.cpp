#include "fact_extract_action.hpp"

#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"

#include "kasane/facts/fact_emitter.hpp"

#include <memory>
#include <string>

namespace {

class FactExtractVisitor : public clang::RecursiveASTVisitor<FactExtractVisitor> {
public:
  FactExtractVisitor(clang::ASTContext* ctx, kasane::facts::FactEmitter& out)
      : ctx_(ctx), out_(out) {}

  bool TraverseFunctionDecl(clang::FunctionDecl* function_decl) {
    if (function_decl == nullptr) {
      return true;
    }

    const bool prev_in_function = in_function_;
    const std::string prev_function_name = current_function_;

    if (function_decl->hasBody() &&
        ctx_->getSourceManager().isWrittenInMainFile(function_decl->getLocation())) {
      in_function_ = true;
      current_function_ = function_decl->getQualifiedNameAsString();

      const clang::FullSourceLoc loc = ctx_->getFullLoc(function_decl->getBeginLoc());
      if (loc.isValid()) {
        out_.emit_function(current_function_, loc.getSpellingLineNumber());
      }

      for (unsigned index = 0; index < function_decl->getNumParams(); ++index) {
        const clang::ParmVarDecl* param_decl = function_decl->getParamDecl(index);
        out_.emit_param(current_function_, param_decl->getNameAsString(), index + 1U);
      }
    }

    clang::RecursiveASTVisitor<FactExtractVisitor>::TraverseFunctionDecl(function_decl);

    in_function_ = prev_in_function;
    current_function_ = prev_function_name;
    return true;
  }

  bool VisitVarDecl(clang::VarDecl* var_decl) {
    if (!in_function_ || !var_decl->hasInit()) {
      return true;
    }

    const clang::Expr* init = var_decl->getInit()->IgnoreParenImpCasts();
    if (const auto* rhs = clang::dyn_cast<clang::DeclRefExpr>(init)) {
      out_.emit_assign(current_function_, var_decl->getNameAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitBinaryOperator(clang::BinaryOperator* op) {
    if (!in_function_ || !op->isAssignmentOp()) {
      return true;
    }

    const clang::Expr* lhs_expr = op->getLHS()->IgnoreParenImpCasts();
    const clang::Expr* rhs_expr = op->getRHS()->IgnoreParenImpCasts();

    const auto* lhs = clang::dyn_cast<clang::DeclRefExpr>(lhs_expr);
    const auto* rhs = clang::dyn_cast<clang::DeclRefExpr>(rhs_expr);
    if (lhs != nullptr && rhs != nullptr) {
      out_.emit_assign(current_function_, lhs->getNameInfo().getAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitCallExpr(clang::CallExpr* call) {
    if (!in_function_) {
      return true;
    }

    const clang::FunctionDecl* callee = call->getDirectCallee();
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

  bool VisitReturnStmt(clang::ReturnStmt* ret) {
    if (!in_function_ || ret->getRetValue() == nullptr) {
      return true;
    }

    const clang::Expr* expr = ret->getRetValue()->IgnoreParenImpCasts();
    if (const auto* decl_ref = clang::dyn_cast<clang::DeclRefExpr>(expr)) {
      out_.emit_return_var(current_function_, decl_ref->getNameInfo().getAsString());
    }

    return true;
  }

private:
  clang::ASTContext* ctx_;
  kasane::facts::FactEmitter& out_;
  bool in_function_ = false;
  std::string current_function_;
};

class FactExtractConsumer : public clang::ASTConsumer {
public:
  FactExtractConsumer(clang::ASTContext* ctx, kasane::facts::FactEmitter& out)
      : visitor_(ctx, out) {}

  void HandleTranslationUnit(clang::ASTContext& ctx) override {
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  FactExtractVisitor visitor_;
};

class FactExtractAction : public clang::ASTFrontendAction {
public:
  explicit FactExtractAction(kasane::facts::FactEmitter& out) : out_(out) {}

  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance& ci, llvm::StringRef /*unused*/) override {
    return std::make_unique<FactExtractConsumer>(&ci.getASTContext(), out_);
  }

private:
  kasane::facts::FactEmitter& out_;
};

class FactExtractActionFactory : public clang::tooling::FrontendActionFactory {
public:
  explicit FactExtractActionFactory(kasane::facts::FactEmitter& emitter) : emitter_(emitter) {}

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<FactExtractAction>(emitter_);
  }

private:
  kasane::facts::FactEmitter& emitter_;
};

}  // namespace

namespace kasane::frontends::clangcpp {

std::unique_ptr<clang::tooling::FrontendActionFactory> create_fact_extract_action_factory(
    kasane::facts::FactEmitter& emitter) {
  return std::make_unique<FactExtractActionFactory>(emitter);
}

}  // namespace kasane::frontends::clangcpp
