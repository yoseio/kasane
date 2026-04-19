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
