# Datalog demo

The minimum demo consumes these input relations from `facts/`:

- `func(name, line)`
- `param(func, var, idx)`
- `assign(func, dst, src)`
- `callsite(func, line, callee)`
- `return_var(func, var)`

The current `rules.dl` computes:

- `dangerous_call(func, line, callee)` for direct `strcpy` calls
- `tainted_return(func)` for a tiny intra-procedural propagation model
