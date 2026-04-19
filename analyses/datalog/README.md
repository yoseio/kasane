# Datalog analyses

The Datalog layer consumes canonical v0 facts from `schemas/facts/v0.dl`.

The entry point is `rules.dl`, which is intentionally small and delegates to:

- `inputs.dl` for the input relations loaded from `.facts`
- `lib/execution_plan.dl` for shared mode controls such as traversal depth and trace toggles
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

## Execution plans

Kasane now supports two execution plans on top of the same extracted facts and
the same `rules.dl` entry point:

- `ci` is tuned for bounded automation. The CLI defaults it to a 15-second
  wall-clock budget, summary traversal depth `2`, and disabled summary trace
  materialization.
- `research` is tuned for deeper inspection. The CLI defaults it to a 90-second
  wall-clock budget, summary traversal depth `6`, and enabled summary trace
  materialization.

The CLI stages a small analysis-only `execution_plan.facts` relation before
invoking Soufflé:

- `execution_plan(mode, time_budget_secs, traversal_depth, emit_traces)`

That keeps the extracted schema and rule selection shared while still allowing
mode-specific budgets and explainability controls.

The CI upper-bound strategy is intentionally simple and documented:

- enforce the wall-clock budget in the CLI wrapper,
- limit recursive summary expansion through `traversal_depth`,
- avoid materializing debug trace relations unless needed.

Research mode enables additional trace exports that mirror the shared summary
relations with an extra `depth` column:

- `summary_param_to_return_trace`
- `summary_param_to_sink_trace`
- `summary_sanitizes_trace`
- `summary_allocates_trace`
- `summary_frees_arg_trace`

Current constraints:

- summaries are keyed by emitted function name text, matching `call.callee_name`
- recursion is context-insensitive and bounded by `execution_plan.traversal_depth`
- precision still inherits v0's lightweight local-flow model; precise aliasing and
  richer call-result tracking remain future work

Regression fixtures for the rule layer live under `testdata/regression/`.
