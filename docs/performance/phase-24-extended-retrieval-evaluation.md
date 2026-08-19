# Phase 24 extended retrieval strategy evaluation

Companion to `docs/performance/phase-16-retrieval-evaluation.md`, covering the
seven strategies that stayed declared-but-disabled through Phase 16/24's
first pass and now have real adapters: `semantic_embedding`, `mcp_resource`,
`call_graph`, `type_reference`, `git_diff`, `dependency_neighbour`, and
`conversation_memory`.

## Authored cases

| Case | Adapter | Fixture | Expected evidence |
|---|---|---|---|
| call-site-found | `call_graph` | `src/caller.cpp` calls `extended_target_function()`; `src/target.hpp` only declares it | Only `src/caller.cpp` returned -- the declaration site alone must not count as a call site |
| type-position-found | `type_reference` | `new ExtendedTargetType()` in `src/caller.cpp`; `class ExtendedTargetType {` in `src/target.hpp` | Both chunks returned (`new` and `class` are both recognized type-introducing positions) |
| include-edge-resolved | `dependency_neighbour` | `src/caller.cpp` `#include`s `src/target.hpp` | `src/target.hpp` returned as the sole neighbour of `src/caller.cpp` |
| memory-recalled | `conversation_memory` | A `UserMemoryStore` entry recorded for the requester, query text with zero lexical overlap with the project | Planner strategy reports `conversation_memory`; recalled text appears in `context_text` |
| memory-not-configured | `conversation_memory` | Same query, planner built without a `UserMemoryStore*` | `disabled_strategy_reasons` carries a runtime "not configured for this instance" entry naming `conversation_memory` |
| embedding-round-trip | `semantic_embedding` | Same zero-overlap query against a `RunnerSupervisor` backed by the `masterai_fake_llama` fixture | Planner strategy reports `semantic_embedding`; the embedding cache is populated after the call |
| uncommitted-diff-found | `git_diff` | A real `git init` fixture repo with one uncommitted line added | `git_diff_search` returns a chunk for the changed file containing the new line |

`test_phase_twentyfour_extended_retrieval_strategies` (`test/tests.cpp`)
builds this exact fixture and asserts every row above. The `git_diff` case is
best-effort: it runs only when the test host actually has a `git` executable
on `PATH` (checked via `git --version` before committing to the assertion),
matching `git_diff_search`'s own "not a git repo / no git available" silent-
empty contract rather than making the whole native suite depend on git being
installed.

## Fixture-backend limitation (semantic_embedding)

`masterai_fake_llama` (the test fixture's fake `llama-server`) returns the
same fixed, non-unit vector `[3.0, 4.0, 0.0]` for every `/v1/embeddings` call
regardless of input text (see `test/fake_llama_server.cpp`). This makes it
suitable for proving the adapter's real call/cache/rank *mechanism* works
end to end, but it cannot demonstrate genuine semantic discrimination between
relevant and irrelevant chunks -- every chunk ties at cosine similarity 1.0
against this fixture. An administrator validating semantic ranking quality
against a real embedding-capable model should author a separate, larger
corpus and run it through `evaluate_retrieval_quality` the same way Phase 16's
set already is, pointed at a real backend rather than the fixture.

## Latency

Every adapter added this pass reuses either an in-memory scan
(`call_graph`/`type_reference`/`dependency_neighbour`), an already-live
backend call path (`semantic_embedding` via
`RunnerSupervisor::embed()`/`/v1/embeddings`, `mcp_resource` via
`McpOutboundGateway::invoke()`), or a bounded sandboxed subprocess
(`git_diff`). All four expensive/IO-bound strategies (`semantic_embedding`,
`mcp_resource`, `conversation_memory`, `git_diff`) only ever run after the
cheaper strategies from Phase 16/24's first pass were insufficient, under the
same request deadline `RetrievalPlanner` already enforced before this pass --
no new deadline mechanism was needed.
