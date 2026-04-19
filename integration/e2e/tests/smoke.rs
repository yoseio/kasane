use std::path::PathBuf;

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
