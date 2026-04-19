# Kasane

Kasane is a C++/Rust monorepo template for a CPG/SAST engine.

This repository is organized around architecture layers rather than language alone:

- `engine/` holds reusable core libraries.
- `frontends/` holds code ingestion and extraction frontends.
- `analyses/` holds Datalog rules and higher-level analysis packs.
- `apps/` holds user-facing Rust applications.
- `schemas/` holds versioned contracts for extracted facts and interchange formats.
- `integration/` and `testdata/` hold end-to-end coverage and reusable fixtures.

The included minimum demo is intentionally small:

- `frontends/clang/cpp/src/main.cpp` runs build-aware extraction through `compile_commands.json`.
- `analyses/datalog/rules.dl` runs split Soufflé analyses over canonical v0 facts.
- `apps/cli/src/main.rs` orchestrates the extractor and Soufflé.
- `apps/cli/src/lib.rs` carries the shared execution-plan logic used by both CI and research flows.

The repository also includes a build-aware extraction path driven by
`compile_commands.json` for real CMake projects.

The canonical extraction contract is versioned under `schemas/facts/`.
Start with `schemas/facts/README.md` for the bootstrap-vs-canonical overview and
`schemas/facts/v0.md` for the relation-level v0 contract.


## Devcontainer

The repository includes a ready-to-use `.devcontainer/` setup for VS Code Dev Containers and GitHub Codespaces.

It provisions:

- a C++ development base image,
- LLVM/Clang development packages for the LibTooling frontend,
- a Rust toolchain with `rustfmt`, `clippy`, `rust-src`, and `rust-analyzer`,
- Soufflé built from source,
- a post-create hook that attempts `cmake --preset dev` and links `compile_commands.json`.

Open the repository in a devcontainer and the environment will be prepared automatically.

## Build layout

C++ is built with CMake, Rust with Cargo.

```bash
cmake --preset dev
cmake --build --preset dev
cargo build --workspace
```

`compile_commands.json` is enabled in the CMake presets to support build-aware analysis.

## Demo flow

The demo input is the CMake fixture at `testdata/synthetic`.

```bash
# Build the Clang extractor (requires LLVM/Clang CMake packages)
cmake --preset dev
cmake --build --preset dev --target kasane-clang-extractor

# Run the Rust CLI demo. It configures and builds the synthetic CMake fixture
# before extracting facts and invoking Souffle.
cargo run -p kasane-cli -- demo
```

If the extractor is not at the default CMake path, point the CLI at it:

```bash
KASANE_EXTRACTOR=/path/to/kasane-clang-extractor cargo run -p kasane-cli -- demo
```

If `souffle` is not on your `PATH`, set `KASANE_SOUFFLE` as well.

The demo supports two execution plans on top of the same extracted facts and
`analyses/datalog/rules.dl` entry point:

- `--mode ci` keeps the run bounded for automation with a 15-second wall-clock
  budget, summary traversal depth `2`, and trace outputs disabled by default.
- `--mode research` keeps the same rules and schema but raises the default budget
  to 90 seconds, summary traversal depth to `6`, and enables summary trace
  outputs for debugging.

Example:

```bash
cargo run -p kasane-cli -- demo --mode research
```

## Build-aware extraction

The sample CMake fixture lives at `testdata/cmake/hello`.

```bash
cmake -S testdata/cmake/hello -B build/fixtures/hello
cmake --build build/fixtures/hello

# facts_dir defaults to build/fixtures/hello/kasane/facts
cargo run -p kasane-cli -- extract -p build/fixtures/hello testdata/cmake/hello
```

To override the extractor location, set `KASANE_EXTRACTOR`.

## Analyze existing facts

You can also run the shared Datalog rule set directly on a facts directory:

```bash
cargo run -p kasane-cli -- analyze testdata/regression/wrapper_sink -o build/analysis/wrapper --mode ci
cargo run -p kasane-cli -- analyze testdata/regression/wrapper_sink -o build/analysis/wrapper-research --mode research
```

Both modes use the same extracted fact schema and `rules.dl` entry point. The
CLI injects an analysis-only `execution_plan.facts` control file into a staged
input directory so the analysis can tune traversal depth and trace materialization
without forking the rule set.

CI mode has an explicit upper-bound strategy for expensive steps:

- enforce a wall-clock timeout around the Soufflé process,
- cap interprocedural summary traversal depth,
- skip summary trace materialization unless explicitly re-enabled.

## Notes

- The Clang frontend is skipped automatically if LLVM/Clang CMake packages are not found.
- The repository includes a hand-written `Cargo.lock` template because Cargo is not always available in every bootstrap environment. Regenerate it with `cargo generate-lockfile` on your development machine.
