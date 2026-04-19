use kasane_ffi::version as ffi_version;
use std::env;
use std::error::Error;
use std::ffi::{OsStr, OsString};
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::thread;
use std::time::{Duration, Instant};

#[derive(Debug, Clone, PartialEq, Eq)]
struct ExtractCommand {
    build_dir: PathBuf,
    source_root: PathBuf,
    facts_dir: PathBuf,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct AnalyzeCommand {
    facts_dir: PathBuf,
    out_dir: PathBuf,
    plan: AnalysisPlan,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct DemoCommand {
    plan: AnalysisPlan,
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
enum AnalysisMode {
    Ci,
    Research,
}

#[derive(Debug, Clone, PartialEq, Eq)]
struct AnalysisPlan {
    mode: AnalysisMode,
    time_budget_secs: u64,
    traversal_depth: u32,
    explain_traces: bool,
}

impl AnalysisMode {
    fn parse(value: &str) -> Result<Self, Box<dyn Error>> {
        match value {
            "ci" => Ok(Self::Ci),
            "research" => Ok(Self::Research),
            other => Err(format!("unknown analysis mode: {other}").into()),
        }
    }

    fn as_str(self) -> &'static str {
        match self {
            Self::Ci => "ci",
            Self::Research => "research",
        }
    }

    fn default_plan(self) -> AnalysisPlan {
        match self {
            Self::Ci => AnalysisPlan {
                mode: self,
                time_budget_secs: 15,
                traversal_depth: 2,
                explain_traces: false,
            },
            Self::Research => AnalysisPlan {
                mode: self,
                time_budget_secs: 90,
                traversal_depth: 6,
                explain_traces: true,
            },
        }
    }
}

impl AnalysisPlan {
    fn with_overrides(
        mode: AnalysisMode,
        time_budget_secs: Option<u64>,
        traversal_depth: Option<u32>,
        explain_traces: Option<bool>,
    ) -> Self {
        let defaults = mode.default_plan();

        Self {
            mode,
            time_budget_secs: time_budget_secs.unwrap_or(defaults.time_budget_secs),
            traversal_depth: traversal_depth.unwrap_or(defaults.traversal_depth),
            explain_traces: explain_traces.unwrap_or(defaults.explain_traces),
        }
    }

    fn time_budget(&self) -> Duration {
        Duration::from_secs(self.time_budget_secs)
    }
}

pub fn run_with_args<I, S>(args: I) -> Result<(), Box<dyn Error>>
where
    I: IntoIterator<Item = S>,
    S: Into<String>,
{
    let args: Vec<String> = args.into_iter().map(Into::into).collect();

    match args.get(1).map(String::as_str) {
        None | Some("demo") => {
            if matches!(args.get(2).map(String::as_str), Some("--help" | "-h")) {
                print_demo_usage();
                return Ok(());
            }

            let command = parse_demo_args(&args[2..])?;
            run_demo(&command)
        }
        Some("extract") => {
            if matches!(args.get(2).map(String::as_str), Some("--help" | "-h")) {
                print_extract_usage();
                return Ok(());
            }

            let command = parse_extract_args(&args[2..])?;
            run_extract(&command)
        }
        Some("analyze") => {
            if matches!(args.get(2).map(String::as_str), Some("--help" | "-h")) {
                print_analyze_usage();
                return Ok(());
            }

            let command = parse_analyze_args(&args[2..])?;
            run_analyze(&command)
        }
        Some("version") | Some("--version") | Some("-V") => {
            println!("Kasane CLI 0.1.0 (ffi {})", ffi_version());
            Ok(())
        }
        Some("help") | Some("--help") | Some("-h") => {
            print_usage();
            Ok(())
        }
        Some(other) => Err(format!("unknown command: {other}").into()),
    }
}

pub fn run() -> Result<(), Box<dyn Error>> {
    run_with_args(env::args())
}

fn usage_text() -> &'static str {
    "Usage:
  kasane-cli demo [--mode <ci|research>] [--time-budget-secs <secs>] [--traversal-depth <depth>] [--explain|--no-explain]
  kasane-cli extract -p <build-dir> <source-root> [-o <facts-dir>]
  kasane-cli analyze <facts-dir> [-o <out-dir>] [--mode <ci|research>] [--time-budget-secs <secs>] [--traversal-depth <depth>] [--explain|--no-explain]
  kasane-cli version

Modes:
  ci        15s wall-clock budget, summary depth 2, trace outputs disabled by default
  research  90s wall-clock budget, summary depth 6, trace outputs enabled by default

Environment:
  KASANE_EXTRACTOR          Override the build-aware extractor path.
  KASANE_SOUFFLE            Override the souffle executable."
}

fn demo_usage_text() -> &'static str {
    "Usage:
  kasane-cli demo [--mode <ci|research>] [--time-budget-secs <secs>] [--traversal-depth <depth>] [--explain|--no-explain]

Defaults:
  ci mode keeps the demo bounded for automation.
  research mode keeps the same rules and schema but emits summary trace files."
}

fn extract_usage_text() -> &'static str {
    "Usage:
  kasane-cli extract -p <build-dir> <source-root> [-o <facts-dir>]

Defaults:
  facts_dir defaults to <build-dir>/kasane/facts"
}

fn analyze_usage_text() -> &'static str {
    "Usage:
  kasane-cli analyze <facts-dir> [-o <out-dir>] [--mode <ci|research>] [--time-budget-secs <secs>] [--traversal-depth <depth>] [--explain|--no-explain]

Defaults:
  out_dir defaults to <facts-dir>/../analysis when possible
  ci mode uses a short wall-clock budget, shallow summary depth, and no trace outputs
  research mode uses a longer budget, deeper summary depth, and emits summary trace outputs"
}

fn print_usage() {
    eprintln!("{}", usage_text());
}

fn print_demo_usage() {
    eprintln!("{}", demo_usage_text());
}

fn print_extract_usage() {
    eprintln!("{}", extract_usage_text());
}

fn print_analyze_usage() {
    eprintln!("{}", analyze_usage_text());
}

fn run_demo(command: &DemoCommand) -> Result<(), Box<dyn Error>> {
    let repo_root = repo_root()?;
    let source_root = repo_root.join("testdata/synthetic");
    let build_dir = repo_root.join("build/demo/project");
    let facts_dir = repo_root.join("build/demo/facts");
    let out_dir = repo_root.join("build/demo/out");
    let rules = default_rules_path();
    let extract = ExtractCommand {
        build_dir,
        source_root,
        facts_dir,
    };

    configure_cmake_project(&extract.source_root, &extract.build_dir)?;
    build_cmake_project(&extract.build_dir)?;
    run_analysis(&extract, &out_dir, &rules, &command.plan)
}

fn run_extract(command: &ExtractCommand) -> Result<(), Box<dyn Error>> {
    run_project_extraction(command)?;
    print_fact_inventory(&command.facts_dir)?;
    Ok(())
}

fn run_analyze(command: &AnalyzeCommand) -> Result<(), Box<dyn Error>> {
    let rules = default_rules_path();
    run_analysis_on_facts(&command.facts_dir, &command.out_dir, &rules, &command.plan)
}

fn run_project_extraction(command: &ExtractCommand) -> Result<(), Box<dyn Error>> {
    if command.facts_dir.exists() {
        fs::remove_dir_all(&command.facts_dir)?;
    }
    fs::create_dir_all(&command.facts_dir)?;

    let extractor = resolve_extractor();
    run_command(
        &extractor,
        &[
            OsString::from("-p"),
            command.build_dir.as_os_str().to_owned(),
            command.source_root.as_os_str().to_owned(),
            command.facts_dir.as_os_str().to_owned(),
        ],
        "extractor",
    )?;

    Ok(())
}

fn run_analysis(
    command: &ExtractCommand,
    out_dir: &Path,
    rules: &Path,
    plan: &AnalysisPlan,
) -> Result<(), Box<dyn Error>> {
    run_project_extraction(command)?;
    print_fact_inventory(&command.facts_dir)?;
    run_analysis_on_facts(&command.facts_dir, out_dir, rules, plan)
}

fn run_analysis_on_facts(
    facts_dir: &Path,
    out_dir: &Path,
    rules: &Path,
    plan: &AnalysisPlan,
) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(out_dir)?;
    clear_analysis_output_files(out_dir)?;

    let analysis_inputs = out_dir.join("analysis-inputs");
    prepare_analysis_inputs(facts_dir, &analysis_inputs, plan)?;

    println!(
        "analysis plan: mode={} budget={}s depth={} traces={}",
        plan.mode.as_str(),
        plan.time_budget_secs,
        plan.traversal_depth,
        if plan.explain_traces { "on" } else { "off" }
    );

    let souffle = resolve_souffle();
    run_command_with_timeout(
        &souffle,
        &[
            OsString::from("-w"),
            format!("-F{}", analysis_inputs.display()).into(),
            format!("-D{}", out_dir.display()).into(),
            rules.as_os_str().to_owned(),
        ],
        "souffle",
        plan.time_budget(),
    )?;

    print_if_exists(out_dir.join("active_execution_plan.csv"))?;
    print_if_exists(out_dir.join("dangerous_call.csv"))?;
    print_if_exists(out_dir.join("tainted_sink.csv"))?;
    print_if_exists(out_dir.join("tainted_return.csv"))?;

    if plan.explain_traces {
        print_if_exists(out_dir.join("summary_param_to_return_trace.csv"))?;
        print_if_exists(out_dir.join("summary_param_to_sink_trace.csv"))?;
        print_if_exists(out_dir.join("summary_sanitizes_trace.csv"))?;
        print_if_exists(out_dir.join("summary_allocates_trace.csv"))?;
        print_if_exists(out_dir.join("summary_frees_arg_trace.csv"))?;
    }

    Ok(())
}

fn repo_root() -> Result<PathBuf, Box<dyn Error>> {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let candidate = manifest_dir.join("../..");
    Ok(candidate.canonicalize()?)
}

fn default_rules_path() -> PathBuf {
    repo_root()
        .map(|root| root.join("analyses/datalog/rules.dl"))
        .unwrap_or_else(|_| PathBuf::from("analyses/datalog/rules.dl"))
}

fn default_extract_facts_dir(build_dir: &Path) -> PathBuf {
    build_dir.join("kasane").join("facts")
}

fn default_analysis_out_dir(facts_dir: &Path) -> PathBuf {
    facts_dir.parent().unwrap_or(facts_dir).join("analysis")
}

fn executable_name(base: &str) -> String {
    if cfg!(windows) {
        format!("{base}.exe")
    } else {
        base.to_string()
    }
}

fn resolve_built_executable(name: &str) -> Option<PathBuf> {
    if let Ok(root) = repo_root() {
        let candidates = [
            root.join("build/cmake/dev/frontends/clang/cpp")
                .join(executable_name(name)),
            root.join("build/cmake/ci/frontends/clang/cpp")
                .join(executable_name(name)),
        ];

        for candidate in candidates {
            if candidate.exists() {
                return Some(candidate);
            }
        }
    }

    None
}

fn resolve_extractor() -> PathBuf {
    if let Ok(path) = env::var("KASANE_EXTRACTOR") {
        return PathBuf::from(path);
    }

    resolve_built_executable("kasane-clang-extractor")
        .unwrap_or_else(|| PathBuf::from(executable_name("kasane-clang-extractor")))
}

fn resolve_souffle() -> PathBuf {
    env::var("KASANE_SOUFFLE")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(executable_name("souffle")))
}

fn resolve_cmake() -> PathBuf {
    PathBuf::from(executable_name("cmake"))
}

fn run_command(program: &Path, args: &[OsString], label: &str) -> Result<(), Box<dyn Error>> {
    let status = Command::new(program).args(args).status().map_err(|err| {
        if err.kind() == io::ErrorKind::NotFound {
            format!(
                "{label} not found: {}. Set the corresponding environment variable if needed.",
                program.display()
            )
        } else {
            format!("{label} failed to start: {err}")
        }
    })?;

    if !status.success() {
        return Err(format!("{label} exited with status {status}").into());
    }

    Ok(())
}

fn run_command_with_timeout(
    program: &Path,
    args: &[OsString],
    label: &str,
    budget: Duration,
) -> Result<(), Box<dyn Error>> {
    let mut child = Command::new(program).args(args).spawn().map_err(|err| {
        if err.kind() == io::ErrorKind::NotFound {
            format!(
                "{label} not found: {}. Set the corresponding environment variable if needed.",
                program.display()
            )
        } else {
            format!("{label} failed to start: {err}")
        }
    })?;

    let deadline = Instant::now() + budget;

    loop {
        if let Some(status) = child.try_wait()? {
            if status.success() {
                return Ok(());
            }

            return Err(format!("{label} exited with status {status}").into());
        }

        if Instant::now() >= deadline {
            child.kill()?;
            let _ = child.wait();
            return Err(format!(
                "{label} exceeded the {}s wall-clock budget",
                budget.as_secs()
            )
            .into());
        }

        thread::sleep(Duration::from_millis(50));
    }
}

fn configure_cmake_project(source_dir: &Path, build_dir: &Path) -> Result<(), Box<dyn Error>> {
    run_command(
        &resolve_cmake(),
        &[
            OsString::from("-S"),
            source_dir.as_os_str().to_owned(),
            OsString::from("-B"),
            build_dir.as_os_str().to_owned(),
        ],
        "cmake",
    )
}

fn build_cmake_project(build_dir: &Path) -> Result<(), Box<dyn Error>> {
    run_command(
        &resolve_cmake(),
        &[OsString::from("--build"), build_dir.as_os_str().to_owned()],
        "cmake",
    )
}

fn parse_demo_args(args: &[String]) -> Result<DemoCommand, Box<dyn Error>> {
    let mut mode = AnalysisMode::Ci;
    let mut time_budget_secs: Option<u64> = None;
    let mut traversal_depth: Option<u32> = None;
    let mut explain_traces: Option<bool> = None;

    let mut index = 0;
    while index < args.len() {
        match args[index].as_str() {
            "--mode" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("demo requires a value after --mode")?;
                mode = AnalysisMode::parse(value)?;
            }
            "--time-budget-secs" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("demo requires a value after --time-budget-secs")?;
                time_budget_secs = Some(parse_positive_u64(value, "--time-budget-secs")?);
            }
            "--traversal-depth" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("demo requires a value after --traversal-depth")?;
                traversal_depth = Some(parse_u32(value, "--traversal-depth")?);
            }
            "--explain" => {
                explain_traces = Some(true);
            }
            "--no-explain" => {
                explain_traces = Some(false);
            }
            other => {
                return Err(format!("unknown demo option: {other}").into());
            }
        }

        index += 1;
    }

    Ok(DemoCommand {
        plan: AnalysisPlan::with_overrides(mode, time_budget_secs, traversal_depth, explain_traces),
    })
}

fn parse_extract_args(args: &[String]) -> Result<ExtractCommand, Box<dyn Error>> {
    let mut build_dir: Option<PathBuf> = None;
    let mut source_root: Option<PathBuf> = None;
    let mut facts_dir: Option<PathBuf> = None;

    let mut index = 0;
    while index < args.len() {
        match args[index].as_str() {
            "-p" | "--build-dir" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("extract requires a path after -p/--build-dir")?;
                build_dir = Some(PathBuf::from(value));
            }
            "-o" | "--facts-dir" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("extract requires a path after -o/--facts-dir")?;
                facts_dir = Some(PathBuf::from(value));
            }
            other if other.starts_with('-') => {
                return Err(format!("unknown extract option: {other}").into());
            }
            value => {
                if source_root.is_some() {
                    return Err("extract accepts exactly one <source-root>".into());
                }
                source_root = Some(PathBuf::from(value));
            }
        }

        index += 1;
    }

    let build_dir = build_dir.ok_or("extract requires -p <build-dir>")?;
    let source_root = source_root.ok_or("extract requires <source-root>")?;
    let facts_dir = facts_dir.unwrap_or_else(|| default_extract_facts_dir(&build_dir));

    Ok(ExtractCommand {
        build_dir,
        source_root,
        facts_dir,
    })
}

fn parse_analyze_args(args: &[String]) -> Result<AnalyzeCommand, Box<dyn Error>> {
    let mut facts_dir: Option<PathBuf> = None;
    let mut out_dir: Option<PathBuf> = None;
    let mut mode = AnalysisMode::Ci;
    let mut time_budget_secs: Option<u64> = None;
    let mut traversal_depth: Option<u32> = None;
    let mut explain_traces: Option<bool> = None;

    let mut index = 0;
    while index < args.len() {
        match args[index].as_str() {
            "-o" | "--out-dir" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("analyze requires a path after -o/--out-dir")?;
                out_dir = Some(PathBuf::from(value));
            }
            "--mode" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("analyze requires a value after --mode")?;
                mode = AnalysisMode::parse(value)?;
            }
            "--time-budget-secs" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("analyze requires a value after --time-budget-secs")?;
                time_budget_secs = Some(parse_positive_u64(value, "--time-budget-secs")?);
            }
            "--traversal-depth" => {
                index += 1;
                let value = args
                    .get(index)
                    .ok_or("analyze requires a value after --traversal-depth")?;
                traversal_depth = Some(parse_u32(value, "--traversal-depth")?);
            }
            "--explain" => {
                explain_traces = Some(true);
            }
            "--no-explain" => {
                explain_traces = Some(false);
            }
            other if other.starts_with('-') => {
                return Err(format!("unknown analyze option: {other}").into());
            }
            value => {
                if facts_dir.is_some() {
                    return Err("analyze accepts exactly one <facts-dir>".into());
                }
                facts_dir = Some(PathBuf::from(value));
            }
        }

        index += 1;
    }

    let facts_dir = facts_dir.ok_or("analyze requires <facts-dir>")?;
    let out_dir = out_dir.unwrap_or_else(|| default_analysis_out_dir(&facts_dir));

    Ok(AnalyzeCommand {
        facts_dir,
        out_dir,
        plan: AnalysisPlan::with_overrides(mode, time_budget_secs, traversal_depth, explain_traces),
    })
}

fn parse_positive_u64(value: &str, flag: &str) -> Result<u64, Box<dyn Error>> {
    let parsed = value
        .parse::<u64>()
        .map_err(|_| format!("{flag} expects an integer, got {value}"))?;

    if parsed == 0 {
        return Err(format!("{flag} must be greater than zero").into());
    }

    Ok(parsed)
}

fn parse_u32(value: &str, flag: &str) -> Result<u32, Box<dyn Error>> {
    value
        .parse::<u32>()
        .map_err(|_| format!("{flag} expects a non-negative integer, got {value}").into())
}

fn clear_analysis_output_files(out_dir: &Path) -> Result<(), Box<dyn Error>> {
    for entry in fs::read_dir(out_dir)? {
        let entry = entry?;
        let file_type = entry.file_type()?;
        let path = entry.path();

        if file_type.is_dir() && entry.file_name() == OsStr::new("analysis-inputs") {
            fs::remove_dir_all(path)?;
        } else if file_type.is_file()
            && path.extension().and_then(|ext| ext.to_str()) == Some("csv")
        {
            fs::remove_file(path)?;
        }
    }

    Ok(())
}

fn prepare_analysis_inputs(
    facts_dir: &Path,
    staged_dir: &Path,
    plan: &AnalysisPlan,
) -> Result<(), Box<dyn Error>> {
    if staged_dir.exists() {
        fs::remove_dir_all(staged_dir)?;
    }
    fs::create_dir_all(staged_dir)?;

    for entry in fs::read_dir(facts_dir)? {
        let entry = entry?;
        let path = entry.path();
        if path.extension().and_then(|ext| ext.to_str()) != Some("facts") {
            continue;
        }

        fs::copy(&path, staged_dir.join(entry.file_name()))?;
    }

    fs::write(
        staged_dir.join("execution_plan.facts"),
        format!(
            "{}\t{}\t{}\t{}\n",
            plan.mode.as_str(),
            plan.time_budget_secs,
            plan.traversal_depth,
            bool_to_number(plan.explain_traces)
        ),
    )?;

    Ok(())
}

fn bool_to_number(value: bool) -> u8 {
    if value {
        1
    } else {
        0
    }
}

fn print_if_exists(path: PathBuf) -> Result<(), Box<dyn Error>> {
    if path.exists() {
        println!("== {} ==", path.display());
        println!("{}", fs::read_to_string(path)?);
    }
    Ok(())
}

fn print_fact_inventory(facts_dir: &Path) -> Result<(), Box<dyn Error>> {
    println!("facts written to {}", facts_dir.display());

    let mut fact_files: Vec<String> = fs::read_dir(facts_dir)?
        .filter_map(|entry| entry.ok())
        .filter_map(|entry| {
            let path = entry.path();
            (path.extension().and_then(|ext| ext.to_str()) == Some("facts"))
                .then(|| entry.file_name().to_string_lossy().into_owned())
        })
        .collect();

    fact_files.sort();

    if !fact_files.is_empty() {
        println!("emitted fact files:");
        for fact_file in fact_files {
            println!("  {}", fact_file);
        }
    }

    Ok(())
}

#[cfg(test)]
mod tests {
    use super::*;

    fn args(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| value.to_string()).collect()
    }

    #[test]
    fn usage_mentions_both_modes() {
        let usage = usage_text();

        assert!(usage.contains("--mode <ci|research>"));
        assert!(usage.contains("ci        15s wall-clock budget"));
        assert!(usage.contains("research  90s wall-clock budget"));
    }

    #[test]
    fn parse_demo_args_defaults_to_ci_mode() {
        let command = parse_demo_args(&args(&[])).expect("demo args should parse");

        assert_eq!(
            command.plan,
            AnalysisPlan {
                mode: AnalysisMode::Ci,
                time_budget_secs: 15,
                traversal_depth: 2,
                explain_traces: false,
            }
        );
    }

    #[test]
    fn parse_analyze_args_applies_research_defaults() {
        let command = parse_analyze_args(&args(&["tmp/facts", "--mode", "research"]))
            .expect("analyze args should parse");

        assert_eq!(
            command,
            AnalyzeCommand {
                facts_dir: PathBuf::from("tmp/facts"),
                out_dir: PathBuf::from("tmp/analysis"),
                plan: AnalysisPlan {
                    mode: AnalysisMode::Research,
                    time_budget_secs: 90,
                    traversal_depth: 6,
                    explain_traces: true,
                },
            }
        );
    }

    #[test]
    fn parse_extract_args_uses_default_facts_dir() {
        let command = parse_extract_args(&args(&["-p", "build/dev", "testdata/cmake/hello"]))
            .expect("extract args should parse");

        assert_eq!(
            command,
            ExtractCommand {
                build_dir: PathBuf::from("build/dev"),
                source_root: PathBuf::from("testdata/cmake/hello"),
                facts_dir: PathBuf::from("build/dev/kasane/facts"),
            }
        );
    }

    #[test]
    fn parse_extract_args_accepts_explicit_facts_dir() {
        let command = parse_extract_args(&args(&[
            "-p",
            "build/dev",
            "-o",
            "tmp/facts",
            "testdata/cmake/hello",
        ]))
        .expect("extract args should parse");

        assert_eq!(command.facts_dir, PathBuf::from("tmp/facts"));
    }

    #[test]
    fn parse_extract_args_requires_build_dir() {
        let error = parse_extract_args(&args(&["testdata/cmake/hello"])).unwrap_err();

        assert_eq!(error.to_string(), "extract requires -p <build-dir>");
    }
}
