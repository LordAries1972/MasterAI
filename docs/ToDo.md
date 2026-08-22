# To-Do

Current outstanding work only, derived from docs/PLAN.md status notes
(2026-08-22 pass). Completed phases are not listed here — see docs/PLAN.md
for the full status record. Phase 24 (retrieval strategy mislabeling) and
Phase 29/34 (model tiering live routing; adaptive controller full
completion) closed since the prior pass and removed from this list.

## Real-hardware / manual validation gates (implementation done, needs a human run)

- Phase 1 — Ubuntu 24.04 / Debian 13 packaging certification: deferred
  (Windows-only per build-scope preference).
- Phase 36 — physical-hardware certification matrix (multiple storage media,
  multiple machines, GPU-offloaded hardware): every software-controllable
  dimension is wired to a real measurement and two real fingerprint-matched
  runs are recorded on this host via `scripts/run-certification.ps1`
  (see `docs/validation/phase-36-certification-runbook.md`); only running
  that same runbook on further physical hosts/storage media remains.
- Phase 28 — NUMA/topology: the real thread-pinning primitive is wired into
  every per-connection worker thread, gated on both
  `AppConfig::numa_local_placement_enabled` (off by default, now a working
  checkbox on Settings under "Topology" -- 2026-08-22 fixed a real bug where
  toggling it saved to disk but silently never took effect until a restart)
  and the Phase 20 `numa_affinity` admission -- so this is code-complete but
  stays inert until an administrator both enables the flag and records the
  Phase 36 benchmark-matrix evidence needed to justify it on their host.

## In progress (partially wired, not fully live)

- Phase 20 — advanced throughput: `numa_affinity`'s `implementation_
  available` is now `true` (2026-08-19, see Phase 28) since a real caller
  exists; `continuous_batching`/`speculative_decoding`/`storage_prefetch`/
  `gpu_cpu_kv_placement` also have real implementations. None are admitted
  by default (gate intentionally not tripped) -- an administrator must
  record real per-host evidence and call `admit()`. `multiple_warm_runners`
  remains the one candidate with no implementation at all.
- Phase 46 — Model Builder: submission now hands off to a real
  `TrainingJob` run against the existing tabular trainer
  (`run_model_builder_config()`, `src/server.cpp`, 2026-08-19) instead of
  being a status flip with nothing behind it. This is a **permanent** scope
  limit, not a temporarily-deferred gap: this codebase has no from-scratch,
  architecture-configurable neural network trainer, so `ModelBuilderSettings`'
  architecture/hidden-dimension/attention/optimiser/scheduler fields are
  recorded for a human to read but are never consumed by the job that
  actually runs. Closing that gap for real would mean building a genuine
  configurable deep-learning training engine, which is out of scope for
  this codebase's tabular-model-only ML execution layer.
- Phase 51 — Subject Examination System: `POST .../{id}/run` now asks the
  target model each configured question for real via
  `execute_rag_generation()` and scores answers with a plain, inspectable
  text-overlap heuristic (`run_subject_exam()`, `src/server.cpp`,
  2026-08-19). This is a real, honest but simple grader, not "AI grading":
  this codebase has no LLM-judge scoring path wired for exam answers, so a
  substring/whole-token-overlap check is what is actually implemented, not
  a claim of semantic understanding.
- Phase 52 — Hyperparameter Optimization: `POST .../{id}/run` now runs a
  real, bounded grid search over learning rate and epoch count
  (`run_hyperparameter_search()`, `src/server.cpp`, 2026-08-19), genuinely
  retraining and re-evaluating the referenced training job's dataset per
  trial, hard-capped at 20 trials. Section 26's wider search space
  (batch size, optimiser, dropout, adapter rank, and more) stays
  permanently out of scope: `train_tabular_model()` does not read any of
  those as tunable parameters, so there is nothing real to search over for
  them without first building a trainer that accepts them.

## Planned (not started)

- Phase 35 — Query Traces, Runner Configuration, and Model Comparison admin
  pages: no dedicated telemetry route exists yet.
- Phase 65/74/83 — Safety and Governance: PII, copyright, and
  data-poisoning detectors; retention/export/network policy enforcement.
- Blanket: every ML executor not explicitly named elsewhere as real remains
  `Planned` — a metadata record or lifecycle transition is not execution
  proof.

## Phase 84 — agentic tool use in chat

First increment only; not yet validated on a live host. Gaps:

- No admin allow-list management UI (currently metadata-store-write only).
- No MCP inbound tool exposure.
- No real diff shown for `write_file`.
- Persistent shell/web-fetch tool parity is out of scope for this increment.

## Manual/config gates (ML)

- Phase 63/67/75 — remote/fleet telemetry: real agent process exists but
  must be deployed on each remote node by an administrator.
- Phase 73 — LoRA fine-tuning: exact llama.cpp finetune/export-lora CLI
  flags are pinned to a historical interface that may drift; tracking
  upstream changes is out of scope, may need admin-supplied extra args.

## Documented scope limits (not gaps — just noting them)

- Phase 53 — Model Optimization: the standalone interface now genuinely
  executes `operation: "pruning"` runs (2026-08-19, shared with the Phase
  72 pipeline's Optimize stage via `run_model_optimization()`), but
  quantization/distillation/graph-optimization/operator-fusion/weight-
  compression/etc. (twelve of the section 28's thirteen example operations)
  remain intent-only -- no executor exists for them, and none is planned
  this pass.
- Phase 77 — inference endpoints: the surface is this codebase's own, not
  OpenAI-compatible; no tool-calling surface on `/v1/completions`.
- Phase 24 — `mcp_resource` retrieval strategy: the adapter is real
  (`src/mcp_retrieval.cpp`), but the primary chat retrieval call site
  (`server.cpp`) leaves `RetrievalRequest::requester_scopes` empty since a
  UI/session chat user has no `mcp.tools.invoke`-scoped token today —
  `McpOutboundGateway::invoke()` requires that literal scope, so this
  strategy contributes no evidence on that path until a caller with a real
  token's scopes (e.g. an MCP-inbound or API-token-authenticated request)
  populates the field.
