# Datalog demo

The repository currently includes a very small bootstrap analysis over the demo facts in `facts/`:

- `func(name, line)`
- `param(func, var, idx)`
- `assign(func, dst, src)`
- `callsite(func, line, callee)`
- `return_var(func, var)`

The current `rules.dl` computes:

- `dangerous_call(func, line, callee)` for direct `strcpy` calls
- `tainted_return(func)` for a tiny intraprocedural propagation model

This bootstrap demo is intentionally smaller than the planned canonical schema.
The target extraction contract for future analyses is:

- `schemas/facts/v0.md`
- `schemas/facts/v0.dl`

As Kasane grows, the Datalog layer should import the canonical schema and derive richer relations from it, rather than expanding the bootstrap schema indefinitely.
