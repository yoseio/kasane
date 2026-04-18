.PHONY: dev ci cargo-build test lint demo clean

dev:
	cmake --preset dev
	cmake --build --preset dev

ci:
	cmake --preset ci
	cmake --build --preset ci

cargo-build:
	cargo build --workspace

test:
	ctest --preset dev
	cargo test --workspace

lint:
	cargo fmt --all --check
	cargo clippy --workspace --all-targets -- -D warnings

demo:
	cmake --preset dev
	cmake --build --preset dev --target kasane-mini-extractor
	cargo run -p kasane-cli -- demo

clean:
	rm -rf build
