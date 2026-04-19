# Fact schemas

Kasane has two fact layers in the repository today.

## 1. Bootstrap demo schema

The current minimum demo extractor emits a tiny bootstrap schema:

- `func.facts` storing `func(name, line)`
- `param.facts` storing `param(func, var, idx)`
- `assign.facts` storing `assign(func, dst, src)`
- `callsite.facts` storing `callsite(func, line, callee)`
- `return_var.facts` storing `return_var(func, var)`

These files are UTF-8 TSV relations named `<relation>.facts`.
Their current column contracts are:

- `func(name, line)` — function qualified name and 1-based definition line
- `param(func, var, idx)` — enclosing function, parameter name, and 1-based parameter position
- `assign(func, dst, src)` — best-effort local variable-to-variable assignment or initializer inside `func`
- `callsite(func, line, callee)` — enclosing function, 1-based callsite line, and direct callee qualified name
- `return_var(func, var)` — function and directly returned variable name

This exists only to keep the repository easy to bootstrap.
It is intentionally too small to be the long-term canonical format.

## 2. Canonical schema v0

The target extraction contract is documented in:

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

New extractors should target schema v0.
The bootstrap demo can be migrated gradually and does not need to change all at once.
