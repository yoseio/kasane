# Kasane architecture notes

## Monorepo policy

- C++ targets use CMake.
- Rust applications use Cargo workspaces.
- The repository root only coordinates these systems; it does not replace them.

## Layering

- `engine/` is reusable infrastructure.
- `frontends/` turns source code into canonical facts.
- `analyses/` derives higher-level meaning from facts.
- `apps/` presents CLI, daemon, and research-oriented entry points.
- `integration/` covers cross-language and cross-process tests.

## Minimum demo

The minimum demo deliberately avoids a full CPG implementation.
Instead it follows the intended long-term flow:

1. Parse C++ with a precise frontend.
2. Emit normalized facts to disk.
3. Evaluate Datalog.
4. Surface results via a Rust application.

That gives you a path to scale without baking an in-memory graph design into every layer.
