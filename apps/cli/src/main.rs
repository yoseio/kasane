use kasane_ffi::version as ffi_version;
use std::env;
use std::error::Error;
use std::ffi::OsString;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

#[derive(Debug, Clone, PartialEq, Eq)]
struct ExtractCommand {
    build_dir: PathBuf,
    source_root: PathBuf,
    facts_dir: PathBuf,
}

fn main() {
    if let Err(err) = run() {
        eprintln!("kasane-cli: {err}");
        std::process::exit(1);
    }
}

fn run() -> Result<(), Box<dyn Error>> {
    let args: Vec<String> = env::args().collect();

    match args.get(1).map(String::as_str) {
        None | Some("demo") => run_demo(),
        Some("extract") => {
            if matches!(args.get(2).map(String::as_str), Some("--help" | "-h")) {
                print_extract_usage();
                return Ok(());
            }

            let command = parse_extract_args(&args[2..])?;
            run_extract(&command)
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

fn print_usage() {
    eprintln!(
        "Usage:\n  kasane-cli demo\n  kasane-cli extract -p <build-dir> <source-root> [-o <facts_dir>]\n  kasane-cli version\n\nEnvironment:\n  KASANE_EXTRACTOR          Override the build-aware extractor path.\n  KASANE_SOUFFLE            Override the souffle executable."
    );
}

fn print_extract_usage() {
    eprintln!(
        "Usage:\n  kasane-cli extract -p <build-dir> <source-root> [-o <facts_dir>]\n\nDefaults:\n  facts_dir defaults to <build-dir>/kasane/facts"
    );
}

fn run_demo() -> Result<(), Box<dyn Error>> {
    let repo_root = repo_root()?;
    let source_root = repo_root.join("testdata/synthetic");
    let build_dir = repo_root.join("build/demo/project");
    let facts_dir = repo_root.join("build/demo/facts");
    let out_dir = repo_root.join("build/demo/out");
    let rules = default_rules_path();
    let command = ExtractCommand {
        build_dir,
        source_root,
        facts_dir,
    };

    configure_cmake_project(&command.source_root, &command.build_dir)?;
    build_cmake_project(&command.build_dir)?;
    run_analysis(&command, &out_dir, &rules)
}

fn run_extract(command: &ExtractCommand) -> Result<(), Box<dyn Error>> {
    run_project_extraction(command)?;
    println!("facts written to {}", command.facts_dir.display());
    Ok(())
}

fn run_project_extraction(command: &ExtractCommand) -> Result<(), Box<dyn Error>> {
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
) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(out_dir)?;
    let souffle = resolve_souffle();

    run_project_extraction(command)?;

    run_command(
        &souffle,
        &[
            format!("-F{}", command.facts_dir.display()).into(),
            format!("-D{}", out_dir.display()).into(),
            rules.as_os_str().to_owned(),
        ],
        "souffle",
    )?;

    print_if_exists(out_dir.join("dangerous_call.csv"))?;
    print_if_exists(out_dir.join("tainted_return.csv"))?;

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

fn run_command(
    program: &Path,
    args: &[std::ffi::OsString],
    label: &str,
) -> Result<(), Box<dyn Error>> {
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

#[cfg(test)]
mod tests {
    use super::*;

    fn args(values: &[&str]) -> Vec<String> {
        values.iter().map(|value| value.to_string()).collect()
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

fn print_if_exists(path: PathBuf) -> Result<(), Box<dyn Error>> {
    if path.exists() {
        println!("== {} ==", path.display());
        println!("{}", fs::read_to_string(path)?);
    }
    Ok(())
}
