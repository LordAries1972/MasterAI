# Phase 16 retrieval evaluation set

This authored set compares the production hybrid planner with a literal
full-query text lookup over the same published Phase 15 index generation.
The expected source contains the marker, but the natural-language query does
not occur verbatim in that source.

| Case | Query | Expected source | Required marker |
|---|---|---|---|
| explain-primary | `Explain retrieval_marker_symbol behavior` | `src/one.cpp` | `retrieval_marker_symbol` |
| find-secondary | `Where is second_retrieval_marker declared` | `src/one.cpp` | `second_retrieval_marker` |

`test_phase_sixteen_deadline_bound_retrieval` constructs the indexed fixture,
runs this set through `evaluate_retrieval_quality`, and requires every hybrid
case to hit while exceeding the full-text-only hit count with zero deadline or
context-budget violations. The test fails closed if the baseline catches up,
the planner misses a source/marker, or either operational bound is exceeded.

