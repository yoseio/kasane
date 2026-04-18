use std::path::PathBuf;

#[test]
fn demo_fixture_exists() {
    let manifest_dir = PathBuf::from(env!("CARGO_MANIFEST_DIR"));
    let sample = manifest_dir.join("../../testdata/synthetic/sample.cpp");
    let rules = manifest_dir.join("../../analyses/datalog/rules.dl");

    assert!(sample.exists(), "sample fixture should exist");
    assert!(rules.exists(), "rules file should exist");
}
