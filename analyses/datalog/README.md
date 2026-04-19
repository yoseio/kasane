# Datalog analyses

The Datalog layer consumes canonical v0 facts from `schemas/facts/v0.dl`.

The entry point is `rules.dl`, which is intentionally small and delegates to:

- `inputs.dl` for the input relations loaded from `.facts`
- `lib/` for reusable helper relations such as local CFG-based flow
- `packs/` for analysis-specific rules

The current packs include:

- `packs/summary_exports.dl` for exporting derived summary relations
- `packs/local_taint.dl` for taint to sinks and returns via the shared summary layer
- `packs/unsafe_c_api.dl` for banned direct C API calls such as `strcpy`

The summary layer lives in `lib/summaries.dl` and is intentionally separate from
reporting packs. It derives lightweight, context-insensitive function summaries
from canonical v0 facts and shared API models:

- `summary_param_to_return(function_name, param_index)`
- `summary_param_to_sink(function_name, param_index, sink_name, sink_arg_index)`
- `summary_sanitizes(function_name, param_index)`
- `summary_allocates(function_name)`
- `summary_frees_arg(function_name, param_index)`

Rule packs should consume the summary and call-site helper relations from
`lib/summaries.dl` rather than rebuilding frontend-specific joins over `call_arg`,
`ref`, or `return_expr`.

Current constraints:

- summaries are keyed by emitted function name text, matching `call.callee_name`
- recursion is context-insensitive and bounded only by the finite fixed point
- precision still inherits v0's lightweight local-flow model; precise aliasing and
  richer call-result tracking remain future work

Regression fixtures for the rule layer live under `testdata/regression/`.
