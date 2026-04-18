#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/Support/raw_ostream.h"

#include "kasane/facts/fact_emitter.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

using namespace clang;
using namespace clang::tooling;

namespace {

std::string read_all(const std::string& path) {
  std::ifstream ifs(path);
  return std::string(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
}

class MiniVisitor : public RecursiveASTVisitor<MiniVisitor> {
public:
  MiniVisitor(ASTContext* ctx, kasane::facts::FactEmitter& out) : ctx_(ctx), out_(out) {}

  bool TraverseFunctionDecl(FunctionDecl* function_decl) {
    if (function_decl == nullptr) {
      return true;
    }

    const bool prev_in_function = in_function_;
    const std::string prev_function_name = current_function_;

    if (function_decl->hasBody() &&
        ctx_->getSourceManager().isWrittenInMainFile(function_decl->getLocation())) {
      in_function_ = true;
      current_function_ = function_decl->getQualifiedNameAsString();

      const FullSourceLoc loc = ctx_->getFullLoc(function_decl->getBeginLoc());
      if (loc.isValid()) {
        out_.emit_function(current_function_, loc.getSpellingLineNumber());
      }

      for (unsigned index = 0; index < function_decl->getNumParams(); ++index) {
        const ParmVarDecl* param_decl = function_decl->getParamDecl(index);
        out_.emit_param(current_function_, param_decl->getNameAsString(), index + 1U);
      }
    }

    RecursiveASTVisitor<MiniVisitor>::TraverseFunctionDecl(function_decl);

    in_function_ = prev_in_function;
    current_function_ = prev_function_name;
    return true;
  }

  bool VisitVarDecl(VarDecl* var_decl) {
    if (!in_function_ || !var_decl->hasInit()) {
      return true;
    }

    const Expr* init = var_decl->getInit()->IgnoreParenImpCasts();
    if (const auto* rhs = dyn_cast<DeclRefExpr>(init)) {
      out_.emit_assign(current_function_, var_decl->getNameAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitBinaryOperator(BinaryOperator* op) {
    if (!in_function_ || !op->isAssignmentOp()) {
      return true;
    }

    const Expr* lhs_expr = op->getLHS()->IgnoreParenImpCasts();
    const Expr* rhs_expr = op->getRHS()->IgnoreParenImpCasts();

    const auto* lhs = dyn_cast<DeclRefExpr>(lhs_expr);
    const auto* rhs = dyn_cast<DeclRefExpr>(rhs_expr);
    if (lhs != nullptr && rhs != nullptr) {
      out_.emit_assign(current_function_, lhs->getNameInfo().getAsString(),
                       rhs->getNameInfo().getAsString());
    }

    return true;
  }

  bool VisitCallExpr(CallExpr* call) {
    if (!in_function_) {
      return true;
    }

    const FunctionDecl* callee = call->getDirectCallee();
    if (callee == nullptr) {
      return true;
    }

    const FullSourceLoc loc = ctx_->getFullLoc(call->getExprLoc());
    if (!loc.isValid()) {
      return true;
    }

    out_.emit_callsite(current_function_, loc.getSpellingLineNumber(),
                       callee->getQualifiedNameAsString());
    return true;
  }

  bool VisitReturnStmt(ReturnStmt* ret) {
    if (!in_function_ || ret->getRetValue() == nullptr) {
      return true;
    }

    const Expr* expr = ret->getRetValue()->IgnoreParenImpCasts();
    if (const auto* decl_ref = dyn_cast<DeclRefExpr>(expr)) {
      out_.emit_return_var(current_function_, decl_ref->getNameInfo().getAsString());
    }

    return true;
  }

private:
  ASTContext* ctx_;
  kasane::facts::FactEmitter& out_;
  bool in_function_ = false;
  std::string current_function_;
};

class MiniConsumer : public ASTConsumer {
public:
  MiniConsumer(ASTContext* ctx, kasane::facts::FactEmitter& out) : visitor_(ctx, out) {}

  void HandleTranslationUnit(ASTContext& ctx) override {
    visitor_.TraverseDecl(ctx.getTranslationUnitDecl());
  }

private:
  MiniVisitor visitor_;
};

class MiniAction : public ASTFrontendAction {
public:
  explicit MiniAction(kasane::facts::FactEmitter& out) : out_(out) {}

  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& ci,
                                                 llvm::StringRef /*unused*/) override {
    return std::make_unique<MiniConsumer>(&ci.getASTContext(), out_);
  }

private:
  kasane::facts::FactEmitter& out_;
};

}  // namespace

int main(int argc, char** argv) {
  if (argc != 3) {
    llvm::errs() << "usage: kasane-mini-extractor <input.cpp> <facts_dir>\n";
    return 1;
  }

  try {
    std::filesystem::create_directories(argv[2]);
    const std::string code = read_all(argv[1]);
    kasane::facts::FactEmitter emitter(argv[2]);

    std::vector<std::string> args = {"-std=c++17"};
    const bool ok = runToolOnCodeWithArgs(std::make_unique<MiniAction>(emitter), code, args, argv[1]);

    return ok ? 0 : 2;
  } catch (const std::exception& ex) {
    llvm::errs() << "kasane-mini-extractor: " << ex.what() << "\n";
    return 3;
  }
}
