#include "fact_extract_action.hpp"

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include "kasane/facts/fact_emitter.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace fs = std::filesystem;
using clang::tooling::ClangTool;
using clang::tooling::CompilationDatabase;
using clang::tooling::CompileCommand;

namespace {

struct ExtractJob {
  CompileCommand command;
  fs::path resolved_file;
};

class SingleCommandCompilationDatabase : public CompilationDatabase {
public:
  explicit SingleCommandCompilationDatabase(CompileCommand command) : command_(std::move(command)) {}

  std::vector<CompileCommand> getCompileCommands(llvm::StringRef /*file_path*/) const override {
    return {command_};
  }

  std::vector<std::string> getAllFiles() const override {
    return {command_.Filename};
  }

private:
  CompileCommand command_;
};

fs::path normalize_path(const fs::path& path) {
  std::error_code err;
  const fs::path canonical = fs::weakly_canonical(path, err);
  if (!err) {
    return canonical.lexically_normal();
  }

  return fs::absolute(path).lexically_normal();
}

fs::path resolve_command_file(const CompileCommand& command) {
  const fs::path file_path(command.Filename);
  if (file_path.is_absolute()) {
    return normalize_path(file_path);
  }

  return normalize_path(fs::path(command.Directory) / file_path);
}

bool is_within_directory(const fs::path& candidate, const fs::path& directory) {
  auto candidate_it = candidate.begin();
  auto directory_it = directory.begin();

  for (; candidate_it != candidate.end() && directory_it != directory.end();
       ++candidate_it, ++directory_it) {
    if (*candidate_it != *directory_it) {
      return false;
    }
  }

  return directory_it == directory.end();
}

bool matches_selection(const fs::path& candidate, const fs::path& selection_root) {
  std::error_code err;
  if (!fs::is_directory(selection_root, err)) {
    return candidate == selection_root;
  }

  return is_within_directory(candidate, selection_root);
}

std::string join_command_line(const CompileCommand& command) {
  std::string joined;
  for (const std::string& arg : command.CommandLine) {
    joined.append(arg);
    joined.push_back('\x1f');
  }
  return joined;
}

bool job_less(const ExtractJob& lhs, const ExtractJob& rhs) {
  if (lhs.resolved_file.generic_string() != rhs.resolved_file.generic_string()) {
    return lhs.resolved_file.generic_string() < rhs.resolved_file.generic_string();
  }

  if (lhs.command.Directory != rhs.command.Directory) {
    return lhs.command.Directory < rhs.command.Directory;
  }

  if (lhs.command.Output != rhs.command.Output) {
    return lhs.command.Output < rhs.command.Output;
  }

  return join_command_line(lhs.command) < join_command_line(rhs.command);
}

std::vector<ExtractJob> collect_jobs(const CompilationDatabase& database,
                                     const fs::path& selection_root) {
  std::vector<ExtractJob> jobs;
  for (CompileCommand command : database.getAllCompileCommands()) {
    const fs::path resolved_file = resolve_command_file(command);
    if (!matches_selection(resolved_file, selection_root)) {
      continue;
    }

    jobs.push_back(ExtractJob{std::move(command), resolved_file});
  }

  std::sort(jobs.begin(), jobs.end(), job_less);
  return jobs;
}

void print_usage() {
  llvm::errs() << "usage: kasane-clang-extractor -p <build_dir> <source_root> <facts_dir>\n";
}

}  // namespace

int main(int argc, char** argv) {
  if (argc == 2 && (std::string_view(argv[1]) == "--help" || std::string_view(argv[1]) == "-h")) {
    print_usage();
    return 0;
  }

  if (argc != 5 || std::string_view(argv[1]) != "-p") {
    print_usage();
    return 1;
  }

  try {
    const fs::path build_dir = normalize_path(argv[2]);
    const fs::path source_root = normalize_path(argv[3]);
    const fs::path facts_dir(argv[4]);

    if (!fs::exists(build_dir)) {
      llvm::errs() << "kasane-clang-extractor: build directory does not exist: " << build_dir.string()
                   << "\n";
      return 2;
    }

    if (!fs::exists(source_root)) {
      llvm::errs() << "kasane-clang-extractor: source root does not exist: " << source_root.string()
                   << "\n";
      return 2;
    }

    std::string error_message;
    std::unique_ptr<CompilationDatabase> database =
        CompilationDatabase::loadFromDirectory(build_dir.string(), error_message);
    if (!database) {
      llvm::errs() << "kasane-clang-extractor: failed to load compile_commands.json from "
                   << build_dir.string() << ": " << error_message << "\n";
      return 2;
    }

    const std::vector<ExtractJob> jobs = collect_jobs(*database, source_root);
    if (jobs.empty()) {
      llvm::errs() << "kasane-clang-extractor: no compile commands matched "
                   << source_root.string() << "\n";
      return 2;
    }

    fs::create_directories(facts_dir);
    kasane::facts::FactEmitter emitter(facts_dir);
    std::unique_ptr<clang::tooling::FrontendActionFactory> action_factory =
        kasane::frontends::clangcpp::create_fact_extract_action_factory(emitter);
    unsigned failed_jobs = 0;

    for (std::size_t index = 0; index < jobs.size(); ++index) {
      const ExtractJob& job = jobs[index];
      llvm::errs() << "[" << (index + 1) << "/" << jobs.size() << "] extracting "
                   << job.resolved_file.string() << "\n";

      SingleCommandCompilationDatabase single_database(job.command);
      std::vector<std::string> sources = {job.command.Filename};
      ClangTool tool(single_database, sources);
      tool.clearArgumentsAdjusters();
      tool.appendArgumentsAdjuster(clang::tooling::getClangStripOutputAdjuster());
      tool.appendArgumentsAdjuster(clang::tooling::getClangSyntaxOnlyAdjuster());

      const int result = tool.run(action_factory.get());
      if (result != 0) {
        ++failed_jobs;
        llvm::errs() << "kasane-clang-extractor: extraction failed for "
                     << job.resolved_file.string() << " with status " << result << "\n";
      }
    }

    if (failed_jobs != 0U) {
      llvm::errs() << "kasane-clang-extractor: " << failed_jobs << " extraction job(s) failed out of "
                   << jobs.size() << "\n";
      return 3;
    }

    return 0;
  } catch (const std::exception& ex) {
    llvm::errs() << "kasane-clang-extractor: " << ex.what() << "\n";
    return 4;
  }
}
