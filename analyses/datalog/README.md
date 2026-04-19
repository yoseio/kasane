# Datalog analyses

The Datalog layer consumes canonical v0 facts from `schemas/facts/v0.dl`.

The entry point is `rules.dl`, which is intentionally small and delegates to:

- `inputs.dl` for the input relations loaded from `.facts`
- `lib/` for reusable helper relations such as local CFG-based flow
- `packs/` for analysis-specific rules

The current packs include:

- `packs/local_taint.dl` for intraprocedural taint to sinks and returns
- `packs/unsafe_c_api.dl` for banned direct C API calls such as `strcpy`

Regression fixtures for the rule layer live under `testdata/regression/`.
