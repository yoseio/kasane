use std::path::{Path, PathBuf};
use std::process::Command;
use std::time::{SystemTime, UNIX_EPOCH};

#[test]
fn demo_fixture_exists() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let sample = manifest_dir.join("../../testdata/synthetic/sample.cpp");
    let demo_cmake_fixture = manifest_dir.join("../../testdata/synthetic/CMakeLists.txt");
    let rules = manifest_dir.join("../../analyses/datalog/rules.dl");
    let cmake_fixture = manifest_dir.join("../../testdata/cmake/hello/CMakeLists.txt");

    assert!(sample.exists(), "sample fixture should exist");
    assert!(
        demo_cmake_fixture.exists(),
        "demo cmake fixture should exist"
    );
    assert!(rules.exists(), "rules file should exist");
    assert!(cmake_fixture.exists(), "cmake fixture should exist");
}

#[test]
fn canonical_fact_schema_docs_are_present_and_referenced() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let repo_root = manifest_dir.join("../..");
    let schema_readme = repo_root.join("schemas/facts/README.md");
    let schema_v0 = repo_root.join("schemas/facts/v0.md");
    let schema_v0_dl = repo_root.join("schemas/facts/v0.dl");
    let readme = repo_root.join("README.md");
    let architecture = repo_root.join("docs/architecture.md");

    assert!(schema_readme.exists(), "fact schema README should exist");
    assert!(schema_v0.exists(), "canonical fact schema v0 should exist");
    assert!(
        schema_v0_dl.exists(),
        "canonical fact schema Datalog declarations should exist"
    );

    let readme_text = std::fs::read_to_string(&readme).expect("root README should be readable");
    assert!(
        readme_text.contains("schemas/facts/v0.md"),
        "root README should reference the canonical fact schema"
    );

    let architecture_text =
        std::fs::read_to_string(&architecture).expect("architecture notes should be readable");
    assert!(
        architecture_text.contains("schemas/facts/v0.md"),
        "architecture notes should reference the canonical fact schema"
    );
}

fn repo_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("../..")
}

fn unique_temp_dir(label: &str) -> PathBuf {
    let nanos = SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .expect("system clock should be after epoch")
        .as_nanos();
    std::env::temp_dir().join(format!("kasane-{label}-{nanos}"))
}

fn run_souffle_fixture(fixture_dir: &Path) -> PathBuf {
    let repo_root = repo_root();
    let rules = repo_root.join("analyses/datalog/rules.dl");
    let out_dir = unique_temp_dir(
        fixture_dir
            .file_name()
            .and_then(|name| name.to_str())
            .unwrap_or("fixture"),
    );

    std::fs::create_dir_all(&out_dir).expect("output dir should be creatable");
    let status = Command::new("souffle")
        .arg("-w")
        .arg(format!("-F{}", fixture_dir.display()))
        .arg(format!("-D{}", out_dir.display()))
        .arg(&rules)
        .status()
        .expect("souffle should start");

    assert!(
        status.success(),
        "souffle should succeed for {}",
        fixture_dir.display()
    );
    out_dir
}

#[test]
fn regression_fixture_captures_argument_local_sink_flow() {
    let fixture = repo_root().join("testdata/regression/arg_local_sink");
    let out_dir = run_souffle_fixture(&fixture);

    let tainted_sink =
        std::fs::read_to_string(out_dir.join("tainted_sink.csv")).expect("tainted_sink output");
    assert!(
        tainted_sink.contains("copy_user\tstrcpy\t1\ttmp"),
        "tainted sink should report src -> tmp -> strcpy flow"
    );

    let dangerous_call =
        std::fs::read_to_string(out_dir.join("dangerous_call.csv")).expect("dangerous_call output");
    assert!(
        dangerous_call.contains("copy_user\tstrcpy"),
        "unsafe C API analysis should flag strcpy"
    );
}

#[test]
fn regression_fixture_captures_local_return_flow() {
    let fixture = repo_root().join("testdata/regression/local_return");
    let out_dir = run_souffle_fixture(&fixture);

    let tainted_return =
        std::fs::read_to_string(out_dir.join("tainted_return.csv")).expect("tainted_return output");
    assert!(
        tainted_return.contains("id\ty"),
        "tainted return should report x -> y -> return flow"
    );
}
