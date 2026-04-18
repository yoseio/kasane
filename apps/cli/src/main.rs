use kasane_ffi::version as ffi_version;
use std::env;
use std::error::Error;
use std::fs;
use std::io;
use std::path::{Path, PathBuf};
use std::process::Command;

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
        Some("run") => {
            if args.len() < 5 {
                print_usage();
                return Err("run requires: <source.cpp> <facts_dir> <out_dir> [rules.dl]".into());
            }

            let source = PathBuf::from(&args[2]);
            let facts_dir = PathBuf::from(&args[3]);
            let out_dir = PathBuf::from(&args[4]);
            let rules = args
                .get(5)
                .map(PathBuf::from)
                .unwrap_or_else(default_rules_path);

            run_analysis(&source, &facts_dir, &out_dir, &rules)
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
        "Usage:\n  kasane-cli demo\n  kasane-cli run <source.cpp> <facts_dir> <out_dir> [rules.dl]\n  kasane-cli version\n\nEnvironment:\n  KASANE_EXTRACTOR  Override the extractor path.\n  KASANE_SOUFFLE    Override the souffle executable."
    );
}

fn run_demo() -> Result<(), Box<dyn Error>> {
    let repo_root = repo_root()?;
    let source = repo_root.join("testdata/synthetic/sample.cpp");
    let facts_dir = repo_root.join("build/demo/facts");
    let out_dir = repo_root.join("build/demo/out");
    let rules = default_rules_path();

    run_analysis(&source, &facts_dir, &out_dir, &rules)
}

fn run_analysis(
    source: &Path,
    facts_dir: &Path,
    out_dir: &Path,
    rules: &Path,
) -> Result<(), Box<dyn Error>> {
    fs::create_dir_all(facts_dir)?;
    fs::create_dir_all(out_dir)?;

    let extractor = resolve_extractor();
    let souffle = resolve_souffle();

    run_command(
        &extractor,
        &[source.as_os_str().to_owned(), facts_dir.as_os_str().to_owned()],
        "extractor",
    )?;

    run_command(
        &souffle,
        &[
            format!("-F{}", facts_dir.display()).into(),
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

fn executable_name(base: &str) -> String {
    if cfg!(windows) {
        format!("{base}.exe")
    } else {
        base.to_string()
    }
}

fn resolve_extractor() -> PathBuf {
    if let Ok(path) = env::var("KASANE_EXTRACTOR") {
        return PathBuf::from(path);
    }

    if let Ok(root) = repo_root() {
        let candidates = [
            root.join("build/cmake/dev/frontends/clang/cpp")
                .join(executable_name("kasane-mini-extractor")),
            root.join("build/cmake/ci/frontends/clang/cpp")
                .join(executable_name("kasane-mini-extractor")),
        ];

        for candidate in candidates {
            if candidate.exists() {
                return candidate;
            }
        }
    }

    PathBuf::from(executable_name("kasane-mini-extractor"))
}

fn resolve_souffle() -> PathBuf {
    env::var("KASANE_SOUFFLE")
        .map(PathBuf::from)
        .unwrap_or_else(|_| PathBuf::from(executable_name("souffle")))
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

fn print_if_exists(path: PathBuf) -> Result<(), Box<dyn Error>> {
    if path.exists() {
        println!("== {} ==", path.display());
        println!("{}", fs::read_to_string(path)?);
    }
    Ok(())
}
