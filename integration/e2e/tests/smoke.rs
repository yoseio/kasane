use kasane_cli::run_with_args;
use std::path::{Path, PathBuf};
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

fn run_cli_fixture(fixture_dir: &Path, mode: &str) -> PathBuf {
    let fixture_name = fixture_dir
        .file_name()
        .and_then(|name| name.to_str())
        .unwrap_or("fixture");
    let out_dir = unique_temp_dir(&format!("{fixture_name}-{mode}"));

    run_with_args([
        "kasane-cli".to_string(),
        "analyze".to_string(),
        fixture_dir.display().to_string(),
        "-o".to_string(),
        out_dir.display().to_string(),
        "--mode".to_string(),
        mode.to_string(),
    ])
    .expect("CLI analysis should succeed");

    out_dir
}

#[test]
fn regression_fixture_captures_argument_local_sink_flow_in_both_modes() {
    let fixture = repo_root().join("testdata/regression/arg_local_sink");
    for mode in ["ci", "research"] {
        let out_dir = run_cli_fixture(&fixture, mode);

        let active_execution_plan =
            std::fs::read_to_string(out_dir.join("active_execution_plan.csv"))
                .expect("active_execution_plan output");
        assert!(
            active_execution_plan.contains(mode),
            "active execution plan should record the selected mode"
        );

        let tainted_sink =
            std::fs::read_to_string(out_dir.join("tainted_sink.csv")).expect("tainted_sink output");
        assert!(
            tainted_sink.contains("copy_user\tstrcpy\t1\ttmp"),
            "tainted sink should report src -> tmp -> strcpy flow"
        );

        let dangerous_call = std::fs::read_to_string(out_dir.join("dangerous_call.csv"))
            .expect("dangerous_call output");
        assert!(
            dangerous_call.contains("copy_user\tstrcpy"),
            "unsafe C API analysis should flag strcpy"
        );

        let summary_param_to_sink =
            std::fs::read_to_string(out_dir.join("summary_param_to_sink.csv"))
                .expect("summary_param_to_sink output");
        assert!(
            summary_param_to_sink.contains("copy_user\t1\tstrcpy\t1"),
            "summary should capture src -> strcpy via the direct sink model"
        );

        let trace = std::fs::read_to_string(out_dir.join("summary_param_to_sink_trace.csv"))
            .expect("summary_param_to_sink_trace output");
        if mode == "ci" {
            assert!(
                trace.trim().is_empty(),
                "CI mode should keep trace outputs empty by default"
            );
        } else {
            assert!(
                trace.contains("copy_user\t1\tstrcpy\t1\t0"),
                "research mode should emit trace rows for summary derivations"
            );
        }
    }
}

#[test]
fn regression_fixture_captures_local_return_flow_in_both_modes() {
    let fixture = repo_root().join("testdata/regression/local_return");
    for mode in ["ci", "research"] {
        let out_dir = run_cli_fixture(&fixture, mode);

        let tainted_return = std::fs::read_to_string(out_dir.join("tainted_return.csv"))
            .expect("tainted_return output");
        assert!(
            tainted_return.contains("id\ty"),
            "tainted return should report x -> y -> return flow"
        );

        let summary_param_to_return =
            std::fs::read_to_string(out_dir.join("summary_param_to_return.csv"))
                .expect("summary_param_to_return output");
        assert!(
            summary_param_to_return.contains("id\t0"),
            "summary should capture x -> return for the local helper"
        );

        let trace = std::fs::read_to_string(out_dir.join("summary_param_to_return_trace.csv"))
            .expect("summary_param_to_return_trace output");
        if mode == "ci" {
            assert!(
                trace.trim().is_empty(),
                "CI mode should avoid return trace materialization by default"
            );
        } else {
            assert!(
                trace.contains("id\t0\t0"),
                "research mode should expose return summary depth information"
            );
        }
    }
}

#[test]
fn regression_fixture_captures_wrapper_summary_flow_in_both_modes() {
    let fixture = repo_root().join("testdata/regression/wrapper_sink");
    for mode in ["ci", "research"] {
        let out_dir = run_cli_fixture(&fixture, mode);

        let active_execution_plan =
            std::fs::read_to_string(out_dir.join("active_execution_plan.csv"))
                .expect("active_execution_plan output");
        if mode == "ci" {
            assert!(
                active_execution_plan.contains("ci\t15\t2\t0"),
                "CI mode should carry the bounded defaults into the analysis plan"
            );
        } else {
            assert!(
                active_execution_plan.contains("research\t90\t6\t1"),
                "research mode should carry the deeper defaults into the analysis plan"
            );
        }

        let summary_param_to_sink =
            std::fs::read_to_string(out_dir.join("summary_param_to_sink.csv"))
                .expect("summary_param_to_sink output");
        assert!(
            summary_param_to_sink.contains("copy_wrapper\t1\tstrcpy\t1"),
            "wrapper summary should capture src -> strcpy"
        );
        assert!(
            summary_param_to_sink.contains("copy_user\t1\tstrcpy\t1"),
            "caller summary should inherit the helper's sink summary"
        );

        let tainted_sink =
            std::fs::read_to_string(out_dir.join("tainted_sink.csv")).expect("tainted_sink output");
        assert!(
            tainted_sink.contains("copy_user\tcopy_wrapper\t1\tsrc"),
            "tainted sink should propagate through the wrapper call"
        );

        let trace = std::fs::read_to_string(out_dir.join("summary_param_to_sink_trace.csv"))
            .expect("summary_param_to_sink_trace output");
        if mode == "ci" {
            assert!(
                trace.trim().is_empty(),
                "CI mode should keep the expensive wrapper trace empty"
            );
        } else {
            assert!(
                trace.contains("copy_wrapper\t1\tstrcpy\t1\t0"),
                "research mode should include direct wrapper summary trace rows"
            );
            assert!(
                trace.contains("copy_user\t1\tstrcpy\t1\t1"),
                "research mode should include inherited wrapper depth information"
            );
        }
    }
}
