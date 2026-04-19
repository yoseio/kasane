# Fact schemas

Kasane has two fact layers in the repository today.

## Canonical schema v0

The extraction contract is documented in:

- `schemas/facts/v0.md`
- `schemas/facts/v0.dl`

Kasane versions canonical extraction schemas by directory / file name rather
than by embedding a schema column into every relation. In practice that means
consumers bind to a schema family such as `v0` and then read the relation files
declared by that version.

Schema v0 is relation-first and build-aware.
It is designed to support:

- build-aware extraction from real projects,
- local AST / CFG reconstruction,
- local data-flow and taint rules,
- summary-based interprocedural analysis,
- SARIF output,
- a derived CPG view when needed.

The current repository uses v0 facts for both extraction and Soufflé analysis.
