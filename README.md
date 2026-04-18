# Kasane

Kasane is a C++/Rust monorepo template for a CPG/SAST engine.

This repository is organized around architecture layers rather than language alone:

- `engine/` holds reusable core libraries.
- `frontends/` holds code ingestion and extraction frontends.
- `analyses/` holds Datalog rules and higher-level analysis packs.
- `apps/` holds user-facing Rust applications.
- `integration/` and `testdata/` hold end-to-end coverage and reusable fixtures.

The included minimum demo is intentionally small:

- `frontends/clang/cpp/src/main.cpp` extracts a few facts from a single C++ file.
- `analyses/datalog/rules.dl` runs a tiny Soufflé analysis over those facts.
- `apps/cli/src/main.rs` orchestrates the extractor and Soufflé.


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

The demo input is `testdata/synthetic/sample.cpp`.

```bash
# Build the Clang extractor (requires LLVM/Clang CMake packages)
cmake --preset dev
cmake --build --preset dev --target kasane-mini-extractor

# Run the Rust CLI demo
cargo run -p kasane-cli -- demo
```

If the extractor is not at the default CMake path, point the CLI at it:

```bash
KASANE_EXTRACTOR=/path/to/kasane-mini-extractor cargo run -p kasane-cli -- demo
```

If `souffle` is not on your `PATH`, set `KASANE_SOUFFLE` as well.

## Notes

- The Clang frontend is skipped automatically if LLVM/Clang CMake packages are not found.
- The repository includes a hand-written `Cargo.lock` template because Cargo is not always available in every bootstrap environment. Regenerate it with `cargo generate-lockfile` on your development machine.
