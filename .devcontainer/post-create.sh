#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

mkdir -p build/facts build/out

printf '[Kasane] tool versions\n'
cmake --version | head -n1 || true
cargo --version || true
rustc --version || true
souffle --version || true
clang --version | head -n1 || true

if cmake --preset dev; then
  if [[ -f build/cmake/dev/compile_commands.json ]]; then
    ln -sf build/cmake/dev/compile_commands.json compile_commands.json
    printf '[Kasane] linked compile_commands.json -> build/cmake/dev/compile_commands.json\n'
  fi
else
  printf '[Kasane] cmake --preset dev failed; please retry manually after the container opens.\n'
fi
