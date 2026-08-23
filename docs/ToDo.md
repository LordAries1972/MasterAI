# To-Do

Current outstanding work only, derived from docs/PLAN.md status notes
(2026-08-24 pass). Completed phases are not listed here -- see docs/PLAN.md
for the full status record. Phase 20 (`multiple_warm_runners`
implementation), Phase 46 (Model Builder real MLP executor), Phase 51
(Subject Exam LLM-judge grading), Phase 52 (Hyperparameter Optimization
wider search space), Phase 91 (ML web UI clarity pass extended to the
remaining 21 panels, plus an honest capability boundary on every non-real
dropdown), Phase 92 (Machine Learning Dashboard consolidation: a
clickable five-step pipeline overview, and closing the four-interface gap
in `MachineLearningRegistry`'s roster), Phase 93 (Projects: the section 5
governance/target fields), Phase 94 (Dataset Manager: real content-derived
metrics, and Dataset Versioning), Phase 95 (Training Jobs: the section 16
execution-policy fields, with max runtime/retry/checkpoint cadence
genuinely enforced), and Phase 96 (Evaluation Lab: real-measured metrics
beyond accuracy/F1/MSE -- latency, throughput, memory, stability,
robustness, bias/fairness breakdown) closed since the prior (2026-08-23)
pass and removed from the "in progress" list below; see their own
docs/PLAN.md status notes for exactly what changed.

## Real-hardware / manual validation gates (implementation done, needs a human run)

- Phase 1 -- Ubuntu 24.04 / Debian 13 packaging certification: deferred
  (Windows-only per build-scope preference).
- Phase 36 -- physical-hardware certification matrix (multiple storage media,
  multiple machines, GPU-offloaded hardware): every software-controllable
  dimension is wired to a real measurement and two real fingerprint-matched
  runs are recorded on this host via `scripts/run-certification.ps1`
  (see `docs/validation/phase-36-certification-runbook.md`); only running
  that same runbook on further physical hosts/storage media remains.
- Phase 28 -- NUMA/topology: the real thread-pinning primitive is wired into
  every per-connection worker thread, gated on both
  `AppConfig::numa_local_placement_enabled` (off by default, a working
  checkbox on Settings under "Topology") and the Phase 20 `numa_affinity`
  admission -- code-complete but stays inert until an administrator both
  enables the flag and records the Phase 36 benchmark-matrix evidence
  needed to justify it on their host.
- Phase 20 -- advanced throughput: every one of the six registry candidates
  (`continuous_batching`, `speculative_decoding`, `numa_affinity`,
  `storage_prefetch`, `multiple_warm_runners`, `gpu_cpu_kv_placement`) now
  has `implementation_available: true` and a real caller genuinely gated on
  its admission -- `multiple_warm_runners` was the last one
  (`LocalRunnerPool`/Phase 33 was already real, but `server.cpp` used it
  purely from config presence, independent of this registry; that gap is
  closed, 2026-08-23). None is admitted (`enabled: true`) by default -- by
  this registry's own fail-closed design, admission requires an
  administrator to record real per-host measured evidence via
  `POST /api/v1/performance/advanced-optimizations` and call `admit()`;
  this codebase never fabricates that evidence on its own.

## In progress (partially wired, not fully live)

- Phase 46 -- Model Builder: a real, from-scratch, configurable multi-layer-
  perceptron (MLP) trainer now exists (`train_tabular_model()`'s MLP branch,
  `src/ml_engine.cpp`, 2026-08-23) and `resolve_training_architecture()`
  (`src/server.cpp`) genuinely drives it from `ModelBuilderSettings` --
  `layer_configuration`/`hidden_dimensions`, `activation_functions`,
  `dropout`, `optimiser` (sgd/sgd_momentum/adam), `batch_size`,
  `gradient_clipping`, `gradient_accumulation`, `learning_rate_scheduler`
  (constant/step/cosine), `early_stopping`, `initialisation_strategy`,
  `epoch_count`, `random_seed`, and `checkpoint_frequency` are all
  genuinely consumed, not just recorded. `attention_configuration`,
  `vocabulary_tokenizer`, `sequence_length`, and `mixed_precision` stay
  permanently unconsumed by design: they are transformer/sequence-model
  concepts (attention, tokens, mixed-precision tensor cores) that do not
  apply to a tabular-row MLP, and this codebase does not implement a
  from-scratch transformer trainer -- fabricating a mapping for them would
  misrepresent what actually runs. `reproducibility_settings`/
  `distributed_training_settings` remain descriptive free text (no defined
  schema to consume them against).
- Phase 51 -- Subject Examination System: `POST .../{id}/run` now grades
  each answer with a real LLM-as-judge (`judge_exam_answer()`,
  `src/server.cpp`, 2026-08-23) -- the same fixed-JSON-prompt pattern
  `scan_content_with_model_classifier()` already proved for content-safety
  scoring, asking the target model itself to judge correctness/confidence/
  rationale. Falls back to the original plain text-overlap heuristic
  (`subject_exam_answer_matches()`) only when the judge call fails or its
  reply is not parseable JSON -- every persisted per-question result now
  records `gradedBy` ("llm_judge" | "heuristic_fallback") so a reviewer can
  see exactly which path produced each score, never presenting a heuristic
  result as model judgement. Section 24's wider metric set (score by topic/
  difficulty, unsupported-claim rate, hallucination rate, source-citation
  quality, reasoning consistency, failure categories; 11 question types
  beyond plain Q/A; per-subject minimum approval score) remains unimplemented
  -- only aggregate pass/fail score exists.
- Phase 52 -- Hyperparameter Optimization: `POST .../{id}/run` now sweeps
  batch size, dropout, and optimiser choice too when the referenced
  training job has a real Phase 46 MLP architecture (`run_hyperparameter_
  search()`, `src/server.cpp`, 2026-08-23) -- a deliberate bounded joint
  sweep riding the existing learning-rate/epoch (i, j) grid rather than a
  full 5-dimensional cartesian product, so the real trial count still never
  exceeds `kMaxHyperparameterTrials` (20). A plain (non-MLP) job's search is
  unaffected, exactly as before. Section 26's remaining knobs (weight decay,
  warmup steps, sequence length, adapter rank, data-sampling strategy) stay
  out of scope: `TabularTrainingOptions` has no such concepts to tune, and
  adapter rank specifically belongs to the separate GGUF LoRA fine-tuning
  path (Phase 73), not this tabular/MLP executor. Strategies beyond grid
  (random, Bayesian, population-based, successive-halving, early-stopping
  search) remain unimplemented.

## Planned (not started)

- Phase 35 -- Query Traces, Runner Configuration, and Model Comparison admin
  pages: no dedicated telemetry route exists yet.
- Phase 65/74/83 -- Safety and Governance: PII, copyright, and
  data-poisoning detectors; retention/export/network policy enforcement.
- Blanket: every ML executor not explicitly named elsewhere as real remains
  `Planned` -- a metadata record or lifecycle transition is not execution
  proof.

## Phase 84 -- agentic tool use in chat

First increment only; not yet validated on a live host. Gaps:

- No admin allow-list management UI (currently metadata-store-write only).
- No MCP inbound tool exposure.
- No real diff shown for `write_file`.
- Persistent shell/web-fetch tool parity is out of scope for this increment.

## Manual/config gates (ML)

- Phase 63/67/75 -- remote/fleet telemetry: real agent process exists but
  must be deployed on each remote node by an administrator.
- Phase 73 -- LoRA fine-tuning: exact llama.cpp finetune/export-lora CLI
  flags are pinned to a historical interface that may drift; tracking
  upstream changes is out of scope, may need admin-supplied extra args.

## Documented scope limits (not gaps -- just noting them)

- Phase 53 -- Model Optimization: the standalone interface now genuinely
  executes `operation: "pruning"` runs (shared with the Phase 72 pipeline's
  Optimize stage via `run_model_optimization()`), but quantization/
  distillation/graph-optimization/operator-fusion/weight-compression/etc.
  (twelve of section 28's thirteen example operations) remain intent-only
  -- no executor exists for them, and none is planned this pass.
- Phase 77 -- inference endpoints: the surface is this codebase's own, not
  OpenAI-compatible; no tool-calling surface on `/v1/completions`. (Phase 86
  separately added a real, dedicated OpenAI-compatible `/v1/chat/completions`
  route on the main server -- see its own docs/PLAN.md status note -- but
  that is a different route from this one.)
- Phase 24 -- `mcp_resource` retrieval strategy: the adapter is real
  (`src/mcp_retrieval.cpp`), but the primary chat retrieval call site
  (`server.cpp`) leaves `RetrievalRequest::requester_scopes` empty since a
  UI/session chat user has no `mcp.tools.invoke`-scoped token today --
  `McpOutboundGateway::invoke()` requires that literal scope, so this
  strategy contributes no evidence on that path until a caller with a real
  token's scopes (e.g. an MCP-inbound or API-token-authenticated request)
  populates the field.
