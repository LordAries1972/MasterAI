# To-Do

Things left to do:-

I now have a clear picture. Here's what's still not fully completed in MasterAI, based on docs/PLAN.md (including the uncommitted Phase 72–78 additions currently in your working tree):

## Whole phases not started (Priority A/B/C performance track)

- Phase 31 — storage tiering / ScratchVolumeManager: DONE, including
  Priority B tier-migration tooling (migrate_durable_file(), POST
  /api/v1/system/storage/migrate) — 2026-08-13.
- Phase 32 — speculative decoding: DONE, real dual-model launch path added
  (2026-08-13); still explicitly unvalidated on real hardware.
- Phase 33 — distributed local runners / mTLS: DONE (2026-08-13), both the
  local-only runner pool and the intranet mTLS worker protocol.
- Phase 34 — adaptive performance controller: DONE at a scoped-down level
  (2026-08-13) — AdaptiveController (src/adaptive_controller.cpp), all
  eight named modes, live-mutable knobs applied through
  MemoryBudgetManager::set_policy(); every other named knob computed and
  disclosed but not yet wired to a live setter.
- Phase 35 — performance administration sidebar: DONE at a scoped-down
  level (2026-08-13, extended 2026-08-17) — one consolidated "Performance"
  page (/app/performance) plus the new "Benchmarks & Regression" page
  (Phase 36, below); Query Traces, Runner Configuration, and Model
  Comparison remain deferred (no dedicated telemetry route exists yet).
- Phase 36 — full performance benchmark matrix / regression suite: DONE at
  a scoped-down level (2026-08-17) — PerformanceCertificationRunner/
  PerformanceCertificationStore (src/regression_gate.cpp) run the quality
  benchmark suite plus five real regression check groups (runner
  attribution, low memory, prompt cache, calibration, model routing)
  against this codebase's own live decision logic, plus real queue-wait
  (an actual RequestScheduler admission, timed end-to-end) and real
  storage-bytes-read (delta of the process's own disk-read counter)
  measurement, with threshold-gated comparison against the previous
  accepted run sharing the exact same fingerprint. Every software-
  controllable dimension and threshold the plan names is wired to a real
  measurement; only the plan's physical-hardware matrix (multiple storage
  media, multiple machines, GPU-offloaded hardware) remains an
  administrator-run, per-real-host exercise — no software running on one
  host can manufacture a second physical drive or GPU. See the Phase 36
  status note in docs/PLAN.md.

## Honest, named gaps inside otherwise-implemented recent phases

- Phase 72 (automation pipelines): DONE — "Label data" now runs a real
  heuristic auto-labeler (auto_label_tabular_dataset(), quantile-binning or
  existing-label validation) when no LabelTaskStore entry exists —
  2026-08-13.
- Phase 73 (LoRA fine-tuning): exact llama.cpp finetune/export-lora CLI flags are pinned to a historical interface that may drift; out of scope to track upstream changes.
- Phase 74 (safety scanning): DONE — scan_content_with_model_classifier()
  adds a real LLM-as-judge classifier pass (bias/hallucination_risk/
  harmful_content) alongside the existing heuristic scan — 2026-08-13.
- Phase 77 (inference endpoints): DONE — per-endpoint policy
  (contentScanEnabled/blockOnScanFinding/blockAnswerOnScanFinding/
  safetyPolicyId/modelClassifierEnabled) via POST .../inference-endpoints/
  {id}/policy, re-read live on every request — 2026-08-13. The surface
  remains this codebase's own, not OpenAI-compatible, and there is still no
  tool-calling surface on /v1/completions to attach an "allowed tools"
  policy to.
- Phase 78 (telemetry): DONE — live per-step training curves
  (TrainingProgressTracker, GET .../training-jobs/{id}/live-progress) and
  cache-hit-rate telemetry (InferenceMetricsStore::record_cache_decision(),
  wired to PromptSessionManager::try_reuse()) are both now real — 2026-08-13.

## Recurring cross-cutting items still outstanding everywhere

- Real-hardware/real-model exit validation for Phases 4–7, 19 (GPU utilization/thermal probing — no approved vendor SDK yet), and the Phase 30A auto vs cpu_only benchmark matrix.
- Phase 15/16/17: authored retrieval-quality evaluation set and representative-query latency benchmark; semantic/dependency/conversation-memory/MCP-resource retrieval strategies remain deferred.
- Ubuntu 24.04 / Debian 13 packaging certification (explicitly deferred by you — Windows-only per your build-scope preference).
