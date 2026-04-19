#include "fact_extract_action.hpp"

#include "clang/Tooling/CompilationDatabase.h"
#include "clang/Tooling/Tooling.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Support/raw_ostream.h"

#include "kasane/facts/fact_emitter.hpp"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <iomanip>
#include <sstream>
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
  explicit SingleCommandCompilationDatabase(CompileCommand command)
      : command_(std::move(command)) {}

  std::vector<CompileCommand>
  getCompileCommands(llvm::StringRef /*file_path*/) const override {
    return {command_};
  }

  std::vector<std::string> getAllFiles() const override {
    return {command_.Filename};
  }

private:
  CompileCommand command_;
};

fs::path normalize_path(const fs::path &path) {
  std::error_code err;
  const fs::path canonical = fs::weakly_canonical(path, err);
  if (!err) {
    return canonical.lexically_normal();
  }

  return fs::absolute(path).lexically_normal();
}

fs::path resolve_command_file(const CompileCommand &command) {
  const fs::path file_path(command.Filename);
  if (file_path.is_absolute()) {
    return normalize_path(file_path);
  }

  return normalize_path(fs::path(command.Directory) / file_path);
}

bool is_within_directory(const fs::path &candidate, const fs::path &directory) {
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

bool matches_selection(const fs::path &candidate,
                       const fs::path &selection_root) {
  std::error_code err;
  if (!fs::is_directory(selection_root, err)) {
    return candidate == selection_root;
  }

  return is_within_directory(candidate, selection_root);
}

std::string join_command_line(const CompileCommand &command) {
  std::string joined;
  for (const std::string &arg : command.CommandLine) {
    joined.append(arg);
    joined.push_back('\x1f');
  }
  return joined;
}

bool job_less(const ExtractJob &lhs, const ExtractJob &rhs) {
  if (lhs.resolved_file.generic_string() !=
      rhs.resolved_file.generic_string()) {
    return lhs.resolved_file.generic_string() <
           rhs.resolved_file.generic_string();
  }

  if (lhs.command.Directory != rhs.command.Directory) {
    return lhs.command.Directory < rhs.command.Directory;
  }

  if (lhs.command.Output != rhs.command.Output) {
    return lhs.command.Output < rhs.command.Output;
  }

  return join_command_line(lhs.command) < join_command_line(rhs.command);
}

std::uint64_t fnv1a64(std::string_view value, std::uint64_t hash) {
  constexpr std::uint64_t kPrime = 1099511628211ULL;
  for (const char byte : value) {
    hash ^= static_cast<unsigned char>(byte);
    hash *= kPrime;
  }
  return hash;
}

void mix_string(std::uint64_t &hash, std::string_view value) {
  hash = fnv1a64(value, hash);
  hash = fnv1a64(std::string_view("\0", 1), hash);
}

std::string to_hex(std::uint64_t value) {
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << value;
  return stream.str();
}

std::string normalize_optional_command_path(const std::string &value,
                                            const fs::path &working_dir) {
  if (value.empty()) {
    return {};
  }

  const fs::path candidate(value);
  if (candidate.is_absolute()) {
    return normalize_path(candidate).string();
  }

  return normalize_path(working_dir / candidate).string();
}

std::string extract_target_triple(const CompileCommand &command) {
  for (std::size_t index = 0; index < command.CommandLine.size(); ++index) {
    const std::string &arg = command.CommandLine[index];
    if (arg == "-target" && (index + 1U) < command.CommandLine.size()) {
      return command.CommandLine[index + 1U];
    }

    constexpr std::string_view kPrefix = "--target=";
    if (arg.rfind(kPrefix.data(), 0) == 0) {
      return arg.substr(kPrefix.size());
    }
  }

  return {};
}

kasane::frontends::clangcpp::TranslationUnitContext
build_translation_unit_context(const ExtractJob &job) {
  const fs::path working_dir = normalize_path(job.command.Directory);
  const std::string main_file = job.resolved_file.string();
  const std::string output =
      normalize_optional_command_path(job.command.Output, working_dir);
  const std::string compiler =
      job.command.CommandLine.empty()
          ? std::string()
          : normalize_optional_command_path(job.command.CommandLine.front(),
                                            working_dir);

  std::uint64_t hash = 14695981039346656037ULL;
  mix_string(hash, main_file);
  mix_string(hash, working_dir.string());
  mix_string(hash, output);
  for (const std::string &arg : job.command.CommandLine) {
    mix_string(hash, arg);
  }

  if (hash == 0U) {
    hash = 1U;
  }

  return kasane::frontends::clangcpp::TranslationUnitContext{
      "tu:" + to_hex(hash), main_file, working_dir.string(), compiler,
      extract_target_triple(job.command), "fnv1a64:" + to_hex(hash),
  };
}

struct CommandLineMacroFact {
  std::string kind;
  std::string name;
  std::string value;
};

CommandLineMacroFact parse_command_line_macro(std::string_view option,
                                              std::string_view payload) {
  const std::size_t eq = payload.find('=');
  const std::string name(payload.substr(0, eq));
  const std::string value = eq == std::string_view::npos
                                ? std::string()
                                : std::string(payload.substr(eq + 1));
  return CommandLineMacroFact{
      option == "-D" ? "cmd_define" : "cmd_undef",
      name,
      option == "-D" ? value : std::string(),
  };
}

std::vector<CommandLineMacroFact>
collect_command_line_macros(const CompileCommand &command) {
  std::vector<CommandLineMacroFact> macros;
  for (std::size_t index = 0; index < command.CommandLine.size(); ++index) {
    const std::string &arg = command.CommandLine[index];
    if ((arg == "-D" || arg == "-U") &&
        (index + 1) < command.CommandLine.size()) {
      const CommandLineMacroFact macro =
          parse_command_line_macro(arg, command.CommandLine[index + 1]);
      if (!macro.name.empty()) {
        macros.push_back(macro);
      }
      ++index;
      continue;
    }

    if ((arg.rfind("-D", 0) == 0 || arg.rfind("-U", 0) == 0) &&
        arg.size() > 2U) {
      const CommandLineMacroFact macro =
          parse_command_line_macro(arg.substr(0, 2), arg.substr(2));
      if (!macro.name.empty()) {
        macros.push_back(macro);
      }
    }
  }

  return macros;
}

std::vector<ExtractJob> collect_jobs(const CompilationDatabase &database,
                                     const fs::path &selection_root) {
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
  llvm::errs() << "usage: kasane-clang-extractor -p <build_dir> <source_root> "
                  "<facts_dir>\n";
}

} // namespace

int main(int argc, char **argv) {
  if (argc == 2 && (std::string_view(argv[1]) == "--help" ||
                    std::string_view(argv[1]) == "-h")) {
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
      llvm::errs() << "kasane-clang-extractor: build directory does not exist: "
                   << build_dir.string() << "\n";
      return 2;
    }

    if (!fs::exists(source_root)) {
      llvm::errs() << "kasane-clang-extractor: source root does not exist: "
                   << source_root.string() << "\n";
      return 2;
    }

    std::string error_message;
    std::unique_ptr<CompilationDatabase> database =
        CompilationDatabase::loadFromDirectory(build_dir.string(),
                                               error_message);
    if (!database) {
      llvm::errs() << "kasane-clang-extractor: failed to load "
                      "compile_commands.json from "
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
    unsigned failed_jobs = 0;

    for (std::size_t index = 0; index < jobs.size(); ++index) {
      const ExtractJob &job = jobs[index];
      const kasane::frontends::clangcpp::TranslationUnitContext context =
          build_translation_unit_context(job);
      llvm::errs() << "[" << (index + 1) << "/" << jobs.size()
                   << "] extracting " << job.resolved_file.string() << "\n";

      const kasane::facts::Id root_file_id = emitter.intern_file(context.main_file);
      emitter.emit_translation_unit(context.tu_id, root_file_id,
                                    context.working_directory,
                                    context.compiler, context.target_triple,
                                    context.command_hash);
      emitter.emit_tu_file(context.tu_id, root_file_id);
      for (std::size_t arg_index = 0; arg_index < job.command.CommandLine.size();
           ++arg_index) {
        emitter.emit_compile_arg(context.tu_id, arg_index,
                                 job.command.CommandLine[arg_index]);
      }
      for (const CommandLineMacroFact &macro :
           collect_command_line_macros(job.command)) {
        const kasane::facts::Id macro_id = emitter.next_macro_id();
        emitter.emit_macro(macro_id, macro.name, false,
                           macro.value.empty() ? 0U : 1U);
      }

      std::unique_ptr<clang::tooling::FrontendActionFactory> action_factory =
          kasane::frontends::clangcpp::create_fact_extract_action_factory(
              emitter, context);

      SingleCommandCompilationDatabase single_database(job.command);
      std::vector<std::string> sources = {job.command.Filename};
      ClangTool tool(single_database, sources);
      tool.clearArgumentsAdjusters();
      tool.appendArgumentsAdjuster(
          clang::tooling::getClangStripOutputAdjuster());
      tool.appendArgumentsAdjuster(
          clang::tooling::getClangSyntaxOnlyAdjuster());

      const int result = tool.run(action_factory.get());
      if (result != 0) {
        ++failed_jobs;
        emitter.emit_diag(context.tu_id, root_file_id, 0, 0, "error",
                          "extraction failed with status " +
                              std::to_string(result));
        llvm::errs() << "kasane-clang-extractor: extraction failed for "
                     << job.resolved_file.string() << " with status " << result
                     << "\n";
      }
    }

    if (failed_jobs != 0U) {
      llvm::errs() << "kasane-clang-extractor: " << failed_jobs
                   << " extraction job(s) failed out of " << jobs.size()
                   << "\n";
      return 3;
    }

    return 0;
  } catch (const std::exception &ex) {
    llvm::errs() << "kasane-clang-extractor: " << ex.what() << "\n";
    return 4;
  }
}
