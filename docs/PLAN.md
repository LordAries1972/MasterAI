# Local Programming AI System — Detailed Implementation Plan

## Document Status

This document is the authoritative phased implementation and validation record for MasterAI.

Current phase status:

- Phase 0: Complete — ADR-0003 pins every release-1 platform, hardware,
  backend, TLS, secret, attachment, voice, MCP, licensing, dataset, and metric
  decision formerly open in Section 27.
- Phase 1: Complete — schema-versioned strict configuration, precedence,
  atomic persistence/backup, first-run/reset wizard, checksummed record
  journal, migration metadata, recovery/checkpoint/backup, lifecycle scripts,
  health, graceful shutdown, and Windows Debug/Release validation are
  implemented. The native Linux x86-64 Release build and tests also pass under
  Ubuntu 26.04 WSL; certification on the pinned Ubuntu 24.04 and Debian 13
  packaging hosts is explicitly deferred and is not blocking this work order.
- Phase 2: Complete — one-time first-admin setup, native OS-principal mapping,
  persistent users/roles/sessions/scoped tokens, login/logout/refresh/me
  routes, cookie rotation/revocation, CSRF/host/origin/rate/body/timeout
  controls, loopback/intranet rejection, OS-backed non-password secrets,
  append-oriented hash-chained audit, and security integration tests exist.
  Release 1 deliberately terminates intranet TLS at the same-host proxy
  boundary fixed by ADR-0003.
- Phase 3: Complete — all manifest fields are strictly parsed, model files are
  size/SHA-256 verified, provenance/license acceptance is preserved, hardware
  suitability controls Ready promotion, CPU features and GPU libraries are
  probed, the actual load command rechecks integrity, and authenticated JSON
  plus native HTML inventories are implemented.
- Phase 4: Implemented, exit validation pending — the `llama.cpp` runner is
  process-isolated and supervised; readiness, loopback IPC tokenization,
  incremental generation, caller/disconnect cancellation, unload, logs, and
  resident-memory/request metrics are implemented. The isolated fake-runner
  integration test passes; the exit criterion still requires a verified real
  programming GGUF on the pinned `llama.cpp` build.
- Phase 5: Implemented, exit validation pending — authenticated native login
  and workspace pages, durable projects/chats, Ready-model selection,
  auto-load, live NDJSON responses, browser cancellation, bounded UTF-8
  attachments, ownership/integrity rechecks, prompt context assembly, and the
  transcription adapter boundary are implemented. Real-model browser and
  optional whisper.cpp operational validation remain.
- Phase 6: Implemented, exit validation pending — immutable Hugging Face and
  GitHub Release URL policy, explicit licenses, persistent resumable jobs,
  pinned curl process isolation, journaled progress, SHA-256 promotion or
  quarantine, model-tree registration integration, CLI/API operations, and
  hardware recommendations are implemented. A deliberately interrupted real
  HTTPS transfer still must certify the exit criterion.
- Phase 7: Implemented, exit validation pending — quick, standard, and extended
  executable suites, exact prompt/settings capture, timing/token/memory and
  quality metrics, durable compatible comparisons, quality-then-speed
  recommendations, CLI/API operations, and browser results are implemented.
  Same-host real-model comparison remains the operational exit check.
- Phase 8: Implemented, exit validation pending — the pinned `2025-11-25`
  JSON-RPC dispatcher, authenticated/project-bound tools and resources,
  newline-delimited `stdio`, Streamable HTTP POST, cancellation notification,
  and native conformance tests are implemented. A live connection from a
  supported IDE or independent MCP inspector remains the operational exit
  check.
- Phase 9: Complete — the separate durable outbound registry pins stdio
  executable digests and transport limits; native stdio process isolation and
  loopback Streamable HTTP clients enforce tool/project/scope/per-call approval,
  cancellation, timeouts, response bounds, OS-secret credential references,
  and hash-chained audit. Release policy deliberately keeps legacy SSE disabled.
- Phase 10: Implemented within the strict C++17 boundary; host packaging and
  live-IDE exit validation pending — per-IDE OS-protected tokens, secret-free
  connection profiles, generic chat/context/cancellation contracts,
  deterministic diagnostics, read-only diff preview, Agent-Coder setup, and
  Visual Studio bridge guidance are implemented. JavaScript/TypeScript VS Code
  packaging and managed Visual Studio VSIX glue are not authorized by the
  project language rule.
- Phase 11: Complete — offline backup/restore, OS-protected secret rotation,
  bounded operational-log rotation, hash-bound upgrade/rollback, crash
  recovery, runtime-root-aware service scripts, and hardened systemd lifecycle
  operations are implemented and covered by clean-host recovery tests.
- Phase 12: Complete for the measurable native control-plane scope — a
  repeatable Release probe identified canonical JSON encoding as the hotspot,
  the encoder was optimized without changing its checksum, and unmeasured
  inference/transport ideas remain deliberately unchanged.
- Phase 13: Complete — end-to-end query traces, real streamed status,
  control-plane/runner resource attribution, native host/storage probes,
  authenticated metric routes, reproducible baseline keys, and bounded
  instrumentation-overhead evidence are implemented and validated.
- Phase 14: Complete — system-wide byte reservations, OS safety reserve,
  deterministic pressure actions, bounded prioritized work, minimal/balanced/
  performance profiles, live status, and inference admission are validated.
- Phase 15: Both exit criteria now have current evidence — cancellable bounded
  discovery, unchanged-file elimination, affected-path incremental updates,
  exact identifier-boundary symbol lookup, immutable checksummed disk
  generations, prior-generation recovery, empty/deleted-path publication,
  typed and coalesced change triggers, bounded background work, authenticated
  project-bound routes, an authenticated `index/notify` ingress, a native
  `ProjectWatcher` file-watcher/branch-switch adapter that calls the same
  service automatically (no external caller required), a representative
  large-project ceiling measurement (`masterai index-probe`), and a recorded
  platform-I/O backend decision are implemented and Windows Debug-validated.
  Deeper language-aware symbol extraction remains a forward enhancement, not
  an exit-criterion blocker (see
  [docs/performance/phase-15-incremental-indexing.md](performance/phase-15-incremental-indexing.md)).
  Windows Release build/test validation for this change set is now recorded
  below (2026-07-31).
- Phase 16: Implemented, exit validation pending — a `RetrievalPlanner`
  chooses the least expensive sufficient strategy across exact identifier
  symbol match, exact literal text match, and a per-token lexical union, all
  read directly from the Phase 15 disk-backed index; independent strategy
  steps run across a small bounded worker pool under a hard wall-clock
  deadline that returns whatever evidence already exists instead of blocking
  once expired; results fuse and deduplicate on the index's own canonical
  chunk identity; a `ContextBudgeter` applies per-source and total chunk/byte
  caps, always keeping the highest-ranked evidence and never truncating a
  kept chunk's text; every candidate (included or omitted) is disclosed with
  its file, offset, index generation, score, and reason, recorded on the
  Phase 13 `QueryTrace` (`retrievalDisclosure`, independent of the terminal
  diagnostic) and exposed through the existing authenticated
  `GET /api/v1/queries/{id}` route; and no project/session state is cached
  inside the planner, so a project's current authorization is re-checked and
  its current index generation is re-read on every call. Current/open-file,
  recent-change, diagnostics/build-log, semantic/embedding,
  dependency-neighbour, conversation-memory, and authorized-MCP-resource
  strategies remain forward work: no embedding adapter, build-log ingestion,
  or MCP-resource-to-retrieval plumbing exists yet, so they are not among the
  strategies `RetrievalPlanner` can choose from today. The exit criterion
  requiring an authored retrieval evaluation set that demonstrates measured
  improvement over full-text-only retrieval has not been produced and remains
  outstanding; the membership/policy invalidation exit criterion is satisfied
  by construction (see above) and by targeted tests.
- Phase 17: Implemented, exit validation pending — a `CacheManager` caches
  Phase 16 retrieval results (the only Phase 17 segment with a real producer
  today) behind versioned keys that embed user/project identity, a policy
  generation, and the current index generation, so file, membership, and
  policy changes invalidate reachability structurally rather than through
  active purging; disk entries are atomic and checksum-quarantined, and
  administrator-only status/trim/clear routes exist alongside
  `GET /api/v1/system/memory`. A formal representative-query latency
  benchmark remains outstanding.
- Phase 18: Implemented, exit validation pending — a `PromptSessionManager`
  (`src/session_cache.cpp`) tracks, per chat, whether the llama.cpp runner's
  own internal KV-cache slot from the previous turn can be resumed for the
  next one. Reuse requires an exact `SessionFingerprint` match (model
  SHA-256, backend executable, chat-template architecture, context length,
  and the chat's project index generation) and a literal byte-prefix match
  between the new fully-templated prompt and the slot's last prompt;
  anything else — a model switch, a project reindex, an edited/resubmitted
  earlier turn — is refused rather than guessed. `RunnerSupervisor::generate()`
  now sends `cache_prompt`/`id_slot` to the runner's `/completion` endpoint
  only when a reuse decision grants them, and `LlamaCppAdapter::build_launch_spec()`
  launches the runner with `--parallel <maxSlots>` so that many independent
  slots exist to address. The session pool is bounded (`session.maxSlots`)
  with least-recently-used eviction per chat, idle entries expire
  (`session.idleRetentionSeconds`), a cancelled or failed turn releases its
  chat's entry instead of ever recording it as reusable, and reuse never
  crosses a chat/user/project boundary because entries are keyed by chat id,
  which is already ownership-checked before `send_chat_message()` ever
  consults the manager. Memory admission
  (`HttpServer::State::send_chat_message`) now reserves KV bytes for the full
  configured slot count, matching how llama.cpp itself preallocates KV cache
  for every `--parallel` slot at load time. The whole feature defaults off
  (`session.enabled=false`) until real-model validation is recorded, matching
  how other real-model exit criteria in this project (Phases 4-7) remain
  outstanding until a pinned backend/model is exercised. New
  `test_phase_eighteen_prompt_session_reuse` covers: no fabricated reuse for
  a chat with no prior turn; reuse granted for an identical-fingerprint,
  prefix-extending turn; reuse refused on fingerprint mismatch; reuse refused
  on a non-prefix (edited-turn) change; least-recently-used eviction once the
  slot pool is full; a released (cancelled-turn) session never being offered
  back; `reset()` clearing every entry; and the launch spec actually exposing
  the configured slot count via `--parallel`. The exit criterion — a
  same-host repeated-turn benchmark on a real pinned model/backend showing
  reduced prompt-evaluation time and TTFT without incorrect output, stale
  policy, memory-cap violation, or cross-boundary reuse — still requires that
  real backend/model and remains outstanding, consistent with how Phases 4-7
  already record that limitation. Chat reply length and context length also
  moved off hardcoded constants in this change: the previous unconfigurable
  512-token reply cap (well below the runner's own 32768-token policy
  ceiling) cut long replies off before the model's own end-of-turn token;
  `inference.chatMaxReplyTokens` (default 8192) and
  `inference.chatContextLength` (default 4096, matching the prior constant)
  are now configurable.
- Phase 19: Implemented, exit validation pending — a `CalibrationService`
  (`src/calibration.cpp`) drives a real `RunnerSupervisor` through a cold
  load and two `generate()` calls (near-zero-token for prompt-evaluation
  timing, a short generation for generation timing) and persists the result
  as a `TuningProfile` in a new `TuningProfileStore`, keyed by host hash
  (`sha256_hex(hardware_info_json(...))`, matching the Phase 13 baseline
  hashing convention), model SHA-256, backend executable digest, and build
  id. Any change to that identity makes the stored profile unreachable, so
  `CalibrationService::resolve()` falls back to `safe_default_profile()`
  (the minimal/balanced/performance starting points from section 31.2)
  instead of ever reusing stale evidence — the same structural-invalidation
  approach Phase 17's `CacheManager` already uses. `LlamaCppAdapter::build_launch_spec`
  gained an optional `LaunchTuning` (GPU layers, mmap/mlock, thread count,
  batch/ubatch tokens) so a calibrated recommendation has concrete launch
  arguments to land in; every field defaults to today's pre-Phase-19
  behavior, so existing callers are unaffected. `probe_system_utilization()`
  (`src/platform.cpp`) adds real system-wide CPU% (Windows `GetSystemTimes`
  deltas / Linux `/proc/stat` deltas) and this-process disk read/write bytes
  (`GetProcessIoCounters` / `/proc/self/io`) as calibration-only evidence,
  never used for memory admission. `SessionFingerprint` (Phase 18) gained a
  `settings_fingerprint` field bound to the active resource profile so a
  profile switch that changes launch tuning can never be mistaken for a
  reusable KV slot. A new `calibrate` CLI command and authenticated
  administrator-only `GET /api/v1/performance/profile/{modelId}/{profile}`,
  `GET /api/v1/performance/recommendations`, and
  `POST /api/v1/performance/calibrate` routes expose it, distinct from the
  pre-existing Phase 13 `/api/v1/performance/baseline` route under the same
  prefix. GPU utilization and thermal-trend probing are not implemented —
  no vendor SDK (NVML/ADL) is an approved dependency per ADR-0003 — and
  remain forward work, as does the exit criterion itself: a real-hardware-
  class benchmark showing the selected profile outperforms safe defaults or
  reduces peak memory without unacceptable quality regression, consistent
  with how Phases 4–7 already leave their own real-model exit criteria
  outstanding until a pinned backend/model is exercised on real hardware.
- Phase 20: Planned and gated, scaffolding only — an
  `AdvancedOptimizationRegistry` (`src/optimization_registry.cpp`) declares
  the six candidate features from section 25 (continuous batching,
  speculative decoding, NUMA affinity, storage prefetch, multiple warm
  runners, GPU/CPU KV placement) and an `AdvancedOptimizationEvidence`
  schema matching section 31.11's required fields, exposed read-only via
  `GET /api/v1/performance/advanced-optimizations`. Every feature defaults
  to `enabled=false`, and no code path — including `record_evidence()` —
  can ever set it to `true`: recording evidence is structurally separate
  from admission, matching the plan's explicit statement that "no feature
  in this phase is pre-approved for implementation merely by appearing in
  the plan." No optimization logic is implemented for any candidate; this
  is a place for a later phase's real, evidence-backed work to attach, not
  an implementation of that work.
- Phase 21: Implemented at a scoped-down level (2026-08-01) — native
  asynchronous storage and prefetch engine. `Win32OverlappedFileReader`
  (IOCP) on Windows and a bounded `PosixPreadPoolReader` fallback on POSIX
  (no Linux `io_uring` adapter), read coalescing, adaptive queue depth keyed
  to a measured `StorageLatencyProfile`, and per-request cancellation are
  implemented and wired into model manifest and index segment reads, with
  automatic fallback to the prior blocking path. Priority A.
- Phase 22: Implemented at a scoped-down level (2026-08-01) — hierarchical
  content and model-data caching. `CacheCategory` expands from Phase 17's six
  values to the plan's full 17-category L0–L5 set; each category's
  `CacheManager::State::Segment` gains probationary/protected/pinned
  segmented eviction (a large one-time scan can only evict other
  probationary entries, never the protected working set) plus a single-hash
  frequency-sketch admission gate that refuses a brand-new candidate rather
  than displacing a demonstrably hotter protected entry, and short-lived
  version-bound negative caching (`put_negative()`/`is_negative()`).
  Immutable, checksummed, atomically-published entries and corruption
  quarantine already existed from Phase 17 and are unchanged. Priority A/B.
- Phase 23: Implemented at a scoped-down level (2026-08-01) — tokenization,
  prompt-template, and prompt-fragment caching. A `CacheCategory::tokenization`
  producer/consumer around `RunnerSupervisor::tokenize()`, compiled
  process-lifetime chat-template plans, `PromptSegment`-based segmented
  prompt assembly, a restricted role/route/key intern table, and an extended
  `PromptSessionManager::try_reuse()` reporting exact byte-level reusable
  prefix length, divergence offset, invalidation reason, and a configurable
  prefix byte ceiling are implemented. Priority A.
- Phase 24: Implemented at a scoped-down level (2026-08-01) — staged
  adaptive retrieval fan-out. Filename/path and recent-change strategies,
  a deterministic request classifier, sticky-sufficiency staged fan-out
  over the existing bounded worker pool with priority tagging, a mutex-
  guarded in-flight-request join table (authorization/project-scoped, never
  cross-boundary), and reference-first (`ChunkReference`) candidate
  materialization gated on `ContextBudgeter` admission are implemented.
  Semantic-embedding, MCP-resource, call-graph, type-reference, git-diff,
  dependency-neighbour, and conversation-memory strategies remain declared
  but disabled pending their adapters, with skip reasons disclosed on
  `QueryTrace`. Priority B.
- Phase 25: Implemented at a scoped-down level (2026-08-01) — weighted-fair
  priority scheduling and backpressure. `RequestScheduler`
  (`src/scheduler.cpp`) implements the plan's eight priority classes
  (cancellation/shutdown through maintenance), per-class weight/queue-depth/
  residence-time/concurrency/memory-allowance policy, deficit-round-robin
  weighted dequeue with cancellation always preempting, and backpressure
  that evicts the lowest-priority still-queued (never running) ticket first
  when a global concurrency ceiling is set. Real continuous batching of live
  backend token-generation steps is out of scope this pass — it requires
  cooperation from the external llama.cpp runner process this control plane
  launches, and no backend flag for it is validated yet (see the class-level
  scope note in masterai.hpp); this phase ships the admission/scheduling
  layer a batching backend can plug into once one exists. Priority C;
  superset of the Phase 20 continuous-batching candidate.
- Phase 26: Implemented at a scoped-down level (2026-08-01) — model loading/
  mapping modes, selective pre-touch, cancellable background warm-up, and a
  warm-model state machine. `ModelLoadMode`/`PreTouchLevel` are selected
  using Phase 21's `StorageLatencyProfile` plus RAM-ceiling evidence; a
  `WarmModelState` machine is layered onto (not a replacement for) the
  existing `RunnerState`/`ModelState` pair via an explicit, regression-tested
  translation table; background warm-up is cancellable and yields under
  memory pressure. Only `none`/`full` pre-touch levels are backend-actionable
  today (`metadata`/`first-use`/`layer-window` are accepted policy that
  currently behaves like `none`). Priority A (mapping and storage-aware
  placement) with Priority B warm-up refinements.
- Phase 27: Implemented at a scoped-down level (2026-08-01) — KV-cache
  accounting, bounded reservation, and deterministic lifecycle. `KvCacheManager`
  (`src/kv_cache.cpp`) tracks per-slot context length, token count, dtype,
  CPU/GPU/split placement, and owning user/chat/project against
  `MemoryBudgetManager`; `reserve()`/`grow()` round up to a configured
  growth step and never optimistically exceed a hard per-slot ceiling
  (refusing the whole request rather than partially granting one); `evict_one()`
  implements the plan's exact deterministic order (failed/cancelled, expired
  idle prefixes, lowest-reuse private slots, large low-value reusable
  prefixes, idle non-pinned sessions, then safe rejection) and never returns
  a pinned or active slot. Backend-validated reduced-precision KV and
  cross-request prefix-tree sharing stay declared-but-disabled
  (`precision_admitted()` is always false for anything but full precision,
  matching `AdvancedOptimizationRegistry`'s "evidence never self-enables"
  discipline from Phase 20) since this codebase has not validated either
  against the external llama.cpp backend it launches. Priority B (accounting/
  placement, delivered) with Priority C precision/prefix-sharing still gated.
- Phase 28: Implemented at a scoped-down level (2026-08-01) — NUMA,
  processor-group, and hybrid-core topology awareness. `probe_hardware_topology()`
  (`src/topology.cpp`) enumerates packages, NUMA nodes, Windows processor
  groups, and performance/efficiency core counts via
  `GetLogicalProcessorInformationEx(RelationAll)`; `recommend_thread_placement()`
  is a pure, tested decision function mapping a `ThreadClass` plus policy to
  a preferred NUMA node and performance/efficiency core preference, always
  falling back to normal OS scheduling on single-node/non-hybrid hosts. A
  real (Win32 `GetNumaNodeProcessorMaskEx`/`SetThreadGroupAffinity`) pinning
  primitive exists and is available but is not called from any real worker
  thread's startup in this pass — the plan requires affinity be applied only
  where per-host measurement shows benefit, and that measurement is Phase
  36's still-Planned benchmark matrix. Priority C; superset of the Phase 20
  NUMA candidate.
- Phase 29: Implemented at a scoped-down level (2026-08-01) — model tiering,
  routing, and cascade inference decision logic. `ModelRouter`
  (`src/model_routing.cpp`) selects the cheapest of five declared tiers
  (deterministic/compact-router/small-fast/medium-general/large-specialist)
  that satisfies disclosed `RoutingSignals` (task category, quality,
  context size, capabilities, available RAM/VRAM, queue depth, benchmark
  evidence), downgrading further to fit available memory and always
  honouring an explicit user pin; `evaluate_cascade()` escalates on the
  plan's exact listed conditions (low confidence, unsupported syntax,
  conflicting retrieval evidence, failed deterministic validation, security
  sensitivity, explicit user request) and safely refuses once already at
  the largest tier instead of escalating past it;
  `resident_set_within_profile()` validates the Minimal/Balanced/Performance
  resident-model ceilings. Wiring tier selection into the live chat pipeline
  so a real request is transparently routed and, on escalation, re-run
  against a second warm runner is out of scope this pass — that needs Phase
  26 warm-state management driving which tiers stay resident, itself only
  scoped-down. Priority B.
- Phase 30: Implemented at a scoped-down level (2026-08-01) — immutable
  shared buffers, request-scoped arenas, and zero-copy streaming.
  `SharedBuffer`/`BufferView`/`MappedBufferView`/`ChunkReference`/
  `TokenSpan`/`PromptSegment`, a debug-poison-checked `RequestArena`, and a
  `FixedSizePool<T>` bridged live into `MemoryBudgetManager` accounting
  (demonstrated via `RetrievalPlanner`'s candidate pool) are implemented; the
  chat-streaming token-send path now writes through a `BufferView` instead of
  concatenating a throwaway JSON string, removing at least one copy from the
  identified runner-buffer-to-socket chain (not every copy in that chain is
  eliminated). Priority A.
- Phase 31: Planned — storage tiering, `ScratchVolumeManager`, and
  storage-aware model/index placement (no RAM-drive placement for durable
  data). Priority A/B.
- Phase 32: Planned — speculative decoding with draft/target compatibility
  checks and automatic fallback. Priority C; deliberately deferred behind
  the Priority A/B work above given its added complexity and hardware
  dependency.
- Phase 33: Planned — distributed local runners and optional mutually
  authenticated intranet worker nodes. Priority C; deliberately deferred for
  the same reason as Phase 32.
- Phase 34: Planned — adaptive performance controller with bounded,
  hysteresis-guarded automatic tuning within administrator ceilings.
  Priority B, gated on Phase 21–31 evidence existing to tune against.
- Phase 35: Planned — performance administration sidebar (overview, memory,
  caches, storage, worker pools, scheduling, advanced optimizations) with no
  single opaque "turbo" switch.
- Phase 36: Planned — full performance benchmark matrix, regression
  thresholds per optimization, and automated build-to-build comparison
  gating releases.
- Phase 37: Implemented at a scoped-down level (2026-08-01) — the Machine
  Learning module foundation described in the "Machine Learning Abilities"
  section below. A new administrator-only `ml.dashboard.view` permission
  (`role_allows`, `src/storage.cpp`) gates a real `MachineLearningRegistry`
  (`src/ml.cpp`) that reports the module as enabled together with real
  (currently zero) dashboard counts and the full 25-interface roadmap from
  section 2, each tagged `available` or `planned` — only Dashboard was
  `available` at this phase; Phase 38 below adds Projects. Reachable at
  `GET /api/v1/ml/dashboard` and the new
  "Machine Learning" sidebar entry (`/app/ml`) in `src/web_ui.cpp`, visible
  only to administrators. This mirrors how Phase 20 begins a large gated
  section: an honest acknowledgement that the subsystem exists, with no
  fabricated data standing in for the training, dataset, or deployment work
  the remaining 24 interfaces still require. `test_machine_learning_foundation_dashboard`
  covers: `ml.dashboard.view` is administrator-only, every dashboard count
  starts at zero, only the Dashboard interface reports `available`, and the
  JSON serializer round-trips that state.
- Phase 38: Implemented at a scoped-down level (2026-08-01) — Machine
  Learning Projects (section 5 below), the organizational container later
  training/dataset/deployment phases will attach to. Scoped down from
  section 5's full field list to identity, intent, subject/task
  classification, and lifecycle status (`MLProject`, `MLProjectStore`,
  `src/ml.cpp`); the deferred fields (owner/contributor assignment,
  security classification, approved data sources, target architecture/
  deployment, success/evaluation/safety criteria, storage/compute
  allocation) belong to the phases that actually consume them. New
  administrator-only `ml.projects.view` / `ml.projects.create` /
  `ml.projects.delete` permissions gate `GET`/`POST /api/v1/ml/projects`
  and `POST /api/v1/ml/projects/{id}/delete`, and a new "Projects" entry
  under the Machine Learning sidebar (`/app/ml/projects`) in
  `src/web_ui.cpp` lists and creates them. `MachineLearningRegistry::
  dashboard()` now takes an `MLProjectStore` and reports a real
  `activeProjects` count (non-archived projects) instead of the Phase 37
  placeholder zero. `test_machine_learning_projects_lifecycle` covers:
  `ml.projects.*` permissions are administrator-only, a new project starts
  at `draft` with its owner recorded, `set_status()` persists a real
  transition and no-ops (not throws) for an unknown id, a project survives
  reload through a second `MLProjectStore` over the same `RecordStore`
  (matching `ChatStore`'s own reload guarantee), the dashboard's
  `activeProjects` count reflects real projects and excludes archived
  ones, and `remove()` actually deletes.
- Phase 39: Implemented at a scoped-down level (2026-08-01) — the Model
  Registry (section 7 below) and Dataset Manager (section 10 below), both
  scoped down to identity, provenance, and lifecycle/approval status
  (`ModelRegistryEntry`/`ModelRegistryStore` and `Dataset`/`DatasetStore`,
  `src/ml.cpp`), not the full field lists (evaluation results, safety
  assessment, hardware/runtime requirements, model hash/signature, dataset
  schema, quality score, versioning) that later training/evaluation/
  ingestion phases will attach to a registry entry or dataset once they
  exist. New administrator-only `ml.models.*` and `ml.datasets.*`
  permissions gate the `GET`/`POST /api/v1/ml/models` and
  `/api/v1/ml/datasets` routes (state/approval transitions and delete
  included), and new "Model Registry"/"Dataset Manager" entries under the
  Machine Learning sidebar list, register, and transition them.
  `ModelRegistryStore::set_state` enforces section 7's rule that a model
  must never reach `production` merely because training finished --
  only an already-`approved` or `staging` entry may make that transition.
  `MachineLearningRegistry::dashboard()` now also takes a `ModelRegistryStore`
  and reports real `modelsTraining`/`modelsAwaitingEvaluation`/
  `deployedModels` counts. `test_machine_learning_model_registry_lifecycle`
  and `test_machine_learning_dataset_manager_lifecycle` cover the same
  permission/lifecycle/reload/remove guarantees as Phase 38's test, plus the
  production-requires-approval rule.
- Phase 40: Implemented at a scoped-down level (2026-08-01) — the Subject
  Knowledge Manager (section 12 below), scoped down to identity, scope,
  ownership, and review status (`SubjectPackage`/`SubjectPackageStore`,
  `src/ml.cpp`), not the full field list (approved terminology,
  definitions, concepts, rules, procedures, examples, counterexamples,
  reference documents, FAQ, required reasoning patterns, prohibited
  conclusions, known limitations, evaluation questions, source citations,
  update schedule) that the later Knowledge Ingestion Pipeline (section 13)
  will attach to a subject package once it exists. New administrator-only
  `ml.subjects.view`/`ml.subjects.create`/`ml.subjects.review`/
  `ml.subjects.delete` permissions gate `GET`/`POST /api/v1/ml/subjects`,
  `POST /api/v1/ml/subjects/{id}/review-status`, and
  `POST /api/v1/ml/subjects/{id}/delete`, and a new "Subject Knowledge
  Manager" entry under the Machine Learning sidebar
  (`/app/ml/subjects`) in `src/web_ui.cpp` lists, creates, and moves them
  through review (`draft` / `in_review` / `approved` / `needs_revision` /
  `retired`). `test_machine_learning_subject_knowledge_manager_lifecycle`
  covers the same permission/lifecycle/reload/remove guarantees as Phase
  38's test. While implementing this phase, an off-by-one in the existing
  Phase 38/39 delete and state/approval routes was also found and fixed:
  `request.target.substr()` was using each route's URL-prefix length plus
  one, silently dropping the first character of the id and making every ML
  project/model/dataset delete, model state change, and dataset approval
  request 404 regardless of a valid id.

Priority note: segmented prompt assembly (Phase 23), tokenization caching
(Phase 23), lazy retrieval-content materialization (Phase 24), immutable
shared buffers (Phase 30), native asynchronous index reads (Phase 21), and
storage-aware model mapping (Phase 26) are the immediate near-term targets —
they improve latency and memory without the hardware-dependent complexity of
speculative decoding (Phase 32) or distributed runners (Phase 33), which
remain explicitly deferred until the Priority A/B phases have measured
evidence.

Status policy:

- `Complete` means every listed deliverable exists and the phase exit criterion
  has current validation evidence.
- `In progress` means at least one real deliverable exists but the exit
  criterion is not yet satisfied.
- `Planned` means no phase implementation has started.

Phase 0 through Phase 3 exit criteria are satisfied for this work order.
Pinned-distribution packaging certification remains an operations follow-up,
as explicitly deferred. Phase 4–7 implementation tests do not substitute for
the real backend/model, interrupted external transfer, and same-host benchmark
exit checks listed above. Phase 8 still requires its external-client
operational check.

Validation evidence recorded on 2026-08-01:

- `inference.startupTimeoutSeconds` (`AppConfig::runner_startup_timeout_seconds`,
  default raised from a previously hardcoded, non-configurable 30 seconds to
  120 seconds) replaces the literal `30U` `RunnerSupervisor::load()` was
  called with at `server.cpp`'s `ensure_model_loaded()`, fixing a real
  `"runner readiness timed out"` failure observed cold-loading a
  DeepSeek-class model. Windows x64 Debug and Release builds completed and
  `masterai_core_tests` passed, including a new save/reload round-trip
  assertion for the field in `test_configuration_and_intranet_policy`.
- Phase 19 implemented: `CalibrationService`/`TuningProfileStore`
  (`src/calibration.cpp`), `probe_system_utilization()`
  (CPU%/disk-byte evidence, `src/platform.cpp`), `LaunchTuning` on
  `LlamaCppAdapter::build_launch_spec`/`RunnerSupervisor::load()`
  (`src/models.cpp`, `src/inference.cpp`), a `settings_fingerprint` field on
  `SessionFingerprint` (`src/session_cache.cpp`), the `performance.autoTune`
  configuration section, a `calibrate` CLI command, and administrator-only
  `GET /api/v1/performance/profile/{modelId}/{profile}`,
  `GET /api/v1/performance/recommendations`, and
  `POST /api/v1/performance/calibrate` routes. New
  `test_phase_nineteen_calibration` covers: a real `fake_llama_executable()`-backed
  calibration run recording its own host/model/backend/build identity;
  `resolve()` returning the persisted profile for a matching identity and
  restoring it across a simulated restart; a backend-hash change correctly
  invalidating the profile and falling back to `safe_default_profile()`;
  and an unknown profile name being rejected rather than silently
  defaulted. Windows x64 Debug and Release builds completed and
  `masterai_core_tests` passed. GPU utilization/thermal-trend probing and
  the real-hardware-class exit benchmark remain outstanding (no approved
  GPU vendor SDK per ADR-0003).
- Phase 20 implemented as scaffolding only:
  `AdvancedOptimizationRegistry`/`AdvancedOptimizationEvidence`
  (`src/optimization_registry.cpp`) declare the six candidate features from
  section 25, every one defaulting to `enabled=false` with no code path able
  to change that, exposed read-only via
  `GET /api/v1/performance/advanced-optimizations`. New
  `test_phase_twenty_advanced_optimizations_disabled` covers: every
  registry entry defaulting to disabled with no evidence; `record_evidence()`
  retaining evidence without ever enabling the feature; and an unknown
  feature name being rejected. No candidate optimization's actual logic is
  implemented. Windows x64 Debug and Release builds completed and
  `masterai_core_tests` passed.

Validation evidence recorded on 2026-07-31:

- Windows x64 Debug build completed under strict C++17 and
  `masterai_core_tests` passed after: flipping the identity defaults so
  locally stored password accounts (`allow_local_password_accounts`) are on
  by default and native OS-verified sign-in (`allow_os_identity_accounts`) is
  an explicit opt-in, gating `POST /api/v1/setup` accordingly and making
  `/health/ready` depend on the OS identity provider only when OS sign-in is
  enabled; adding a persisted model verification cache (`verify-models` CLI
  command and `ModelRegistry::verify`) so `scan()` never hashes files on the
  request path; extending `download-model`/the download-create route to
  write a full manifest (display name, architecture, quantization, license
  SPDX ID, size, RAM estimates) up front so an in-progress download shows as
  `downloading` rather than being invisible; adding pause/cancel/remove
  download controls (`POST .../model-downloads/{id}/pause|cancel|remove`)
  and process-shutdown cancellation of in-flight transfers; deriving and
  persisting chat titles from each chat's first user message with
  backward-compatible restore of pre-existing chat records; and splitting the
  web
  application into per-URL, role-gated sections
  (`/app/chat`, `/app/projects`, `/app/models/inventory`,
  `/app/models/download`, `/app/models/benchmarks`, `/app/admin/create`,
  `/app/admin/users`). Release build/test validation for this change set is
  still outstanding (blocked by a currently running Release-built
  `masterai.exe` holding the executable open) and remains to be recorded
  before this work is considered validated on both build types.
- Windows x64 Debug build completed under strict C++17 and
  `masterai_core_tests` passed (including a new `test_phase_fifteen_project_watcher`
  case) after adding `src/project_watcher.cpp`'s native `ProjectWatcher`: a
  `ReadDirectoryChangesW`/I/O-completion-port watcher on Windows and a
  recursive, 16,384-directory-bounded `inotify` watcher on Linux, both
  debouncing bursts of raw OS events into one `IndexTrigger::watcher` call per
  project, special-casing `.git/HEAD` changes as `IndexTrigger::branch_switch`,
  issuing an immediate baseline `request_rebuild` for a project seen for the
  first time, and falling back to a 30-minute `IndexTrigger::periodic` rescan
  per project as a safety net. It is constructed by `HttpServer::State`
  alongside `ProjectIndexService` (new `indexing.watchProjectFiles` /
  `AppConfig::watch_project_files` config flag, on by default) and destroyed
  before it so its thread stops calling into the index service first. A new
  `masterai index-probe <project-root> [index-root]` CLI command drove a real
  `ProjectIndexer` through a full rebuild and a one-file incremental update
  over both this repository's own `src/` tree and a synthetic 10,000-file/
  ~41&nbsp;MiB tree, reporting elapsed time and this process's own peak
  resident-memory delta; peak resident memory (72.7&nbsp;MiB at 10,000 files)
  stayed far below the configured hard limit (6.29&nbsp;GiB), and this evidence
  is recorded, with the accompanying platform-I/O backend decision, in
  [docs/performance/phase-15-incremental-indexing.md](performance/phase-15-incremental-indexing.md).
- Windows x64 Release build (Ninja/MSVC 19.38, strict `/std:c++17`) completed
  and `masterai_core_tests` passed, closing out the Release validation that
  was outstanding above for both the identity-default/download-manifest/
  chat-title/per-URL-web-section change set and the `ProjectWatcher`/
  `index-probe` change set; both are now Windows Debug- and
  Release-validated. This same Release run, together with an immediately
  preceding Debug build/test run, also validates Phase 16: implementing
  `RetrievalPlanner`/`ContextBudgeter` (`src/retrieval.cpp`), the
  `ProjectIndexService::search_text`/`search_symbol` read path used to reach
  a project's live published index generation, the `retrieval.*`
  configuration section (`deadlineMilliseconds`, `maximumContextBytes`,
  `maximumChunksPerSource`, `maximumTotalChunks`, `enabled`), wiring
  retrieval into `send_chat_message`'s existing `retrieval_planning`/
  `retrieval`/`ranking` stages ahead of prompt assembly, and a new
  `QueryCoordinator::record_retrieval` that attaches a `retrievalDisclosure`
  JSON array to a query trace independently of its terminal diagnostic. The
  new `test_phase_sixteen_deadline_bound_retrieval` case passed in both
  build types, covering: no fabricated evidence before a project has a
  published index; exact-symbol strategy selection with sticky sufficiency
  (a symbol hit skips the more expensive literal-text and lexical steps);
  canonical chunk-id fusion (identical chunk id returned by a symbol lookup
  and a text lookup on the same identifier); live re-read of the current
  index generation with no planner-side caching (an incremental update is
  visible on the very next `retrieve()` call with the same planner
  instance); a near-zero deadline returning bounded partial evidence in
  under two seconds instead of blocking; `ContextBudgeter` preferring the
  highest-ranked evidence and disclosing the rest as omitted under a byte
  cap; and a recorded retrieval disclosure surviving a later successful
  `QueryCoordinator::finish()` call.
- A second Windows Debug and Release build/test pass (same day) validates
  Phase 17: a new `CacheManager` (`src/cache.cpp`), the `cache.*`
  configuration section (`enabled`, `maximumBytesPerCategory`), wiring a
  cache lookup/store around the existing `RetrievalPlanner::retrieve` call
  in `send_chat_message` (new `HttpServer::State::retrieve_with_cache`), a
  `cacheHit` field added to the existing `retrievalDisclosure` JSON, three
  new administrator-only `/api/v1/system/cache`,
  `/api/v1/system/cache/trim`, and `/api/v1/system/cache/clear` routes, and
  `invalidate_policy()` calls from user creation and project creation. The
  new `test_phase_seventeen_security_partitioned_cache` case passed in both
  build types, covering repeated-query hit/miss, entry survival across a
  simulated restart (a second `CacheManager` over the same cache root),
  unreachability after a republished index generation and after
  `invalidate_policy()`, checksum-corrupted-entry quarantine-by-deletion,
  segmented-LRU eviction never exceeding a category's configured byte cap,
  and cross-project/cross-user isolation. Fixing this test surfaced and
  corrected a real use-after-free in `CacheManager::State::evict_locked`
  (it took the entry iterator by reference from callers that passed a
  reference living inside the eviction index's own map storage, then erased
  that same map entry before finishing with the now-dangling reference;
  fixed by taking the iterator by value and reading out every field needed
  before erasing anything) that was reliably reproducible as a
  nondeterministic crash before the fix and is now covered by the
  overwrite-existing-key and corruption paths the new test exercises.
- A third Windows Debug and Release build/test pass (same day) validates
  Phase 18: the new `PromptSessionManager` (`src/session_cache.cpp`),
  `GenerationOptions::cache_prompt`/`slot_id` plumbed into
  `RunnerSupervisor::generate()`'s `/completion` request body, `--parallel`
  added to `LlamaCppAdapter::build_launch_spec()` and threaded through
  `RunnerSupervisor::load()`, the new `session.*` configuration section
  (`enabled`, `maxSlots`, `idleRetentionSeconds`), and `send_chat_message()`
  consulting `PromptSessionManager::try_reuse()`/`record()`/`release()`
  around generation, with KV memory admission scaled by the configured slot
  count. Also in this pass: `inference.chatMaxReplyTokens` (default 8192,
  replacing an unconfigurable 512-token cap that cut long replies off before
  the model's own end-of-turn token) and `inference.chatContextLength`
  (default 4096, the prior hardcoded constant, now configurable). The new
  `test_phase_eighteen_prompt_session_reuse` case passed in both build
  types, covering: no reuse for a chat with no prior turn; reuse granted for
  a matching-fingerprint, prefix-extending turn; reuse refused on a
  fingerprint mismatch and on a non-prefix (edited-turn) change;
  least-recently-used slot-pool eviction; a released (cancelled-turn)
  session never being offered back; `reset()` clearing every entry; and the
  launch spec exposing the configured slot count via `--parallel`. Fixing
  this required adding `session` to the top-level configuration schema
  allow-list (`ConfigurationManager::load`'s `require_only(root, ...)`
  call), which `ConfigurationManager::serialize()` had started emitting a
  `session` section for but the loader did not yet accept -- caught by
  `test_configuration_and_intranet_policy`'s existing save/reload
  round-trip. Also in this pass: `src/web_ui.cpp`'s Markdown renderer now
  preserves a numbered list's own leading numbers (`<ol start="N">` plus a
  per-item `<li value="M">`) instead of letting the browser silently
  renumber every list from 1; fenced code blocks keep their language tag as
  a `language-xxx` class on the emitted `<code>` element; and a failed chat
  turn (network drop, backend error) now renders inside the reply's own
  chat bubble (styled distinctly via a new `.chatMsg-error` class) instead
  of only appearing in the easy-to-miss status line above the composer,
  while a genuinely cancelled turn still leaves its partial reply
  untouched.

Validation evidence recorded on 2026-07-30:

- Windows x64 Debug and Release builds completed under strict C++17 after
  adding the authenticated `POST /api/v1/projects/{id}/index/notify` route,
  and `masterai_core_tests` passed in both build types. The route accepts
  only the closed save/watcher/branch-switch/periodic trigger vocabulary,
  requires the same `projects.write` role and project binding as the
  existing rebuild/cancel routes, rejects an unsupported trigger name,
  forwards named affected paths for save/watcher triggers into the bounded
  `ProjectIndexService::request_update` queue, promotes branch-switch and
  periodic notifications to a full scan, and records an `index.notify` audit
  entry per accepted request. This closes the gap between the previously
  implemented trigger-admission logic and an actual external caller; a live
  native or editor-side file-watcher/branch-switch process that calls this
  route automatically is still outstanding.

Validation evidence recorded on 2026-07-29:

- Windows x64 Debug and Release builds completed under strict C++17 after the
  Phase 15 service integration, and `masterai_core_tests` passed in both build
  types.
- The Phase 15 service test admits one rebuild per project, rejects duplicate
  queued/active work, publishes observable completion, and preserves the
  existing disk-generation validation for restart recovery, corrupt-active
  fallback, partial publication, incremental update, and cancellation.
- Phase 15 affected-path updates now reuse every unaffected chunk, remove
  deleted-file chunks including the final empty generation, reject
  project-escaping paths, and replace stale corrupt generation filenames
  safely. Stable full scans report unchanged-file elimination.
- Exact symbol lookup observes C/C++-style identifier boundaries. The bounded
  service accepts explicit save, watcher, branch-switch, and periodic trigger
  types, coalesces queued affected paths, promotes branch/periodic work to a
  full scan, and reports the applied trigger through the authenticated status
  route.
- `GET /api/v1/projects/{id}/index`,
  `POST /api/v1/projects/{id}/index/rebuild`, and
  `POST /api/v1/projects/{id}/index/cancel` now use the authenticated native
  service. Rebuilds are non-blocking and capacity-bounded; bearer credentials
  require an explicit binding to the selected project.
- Objective verification passed with hash
  `309D6BB25DD35622F1627BF1127DD35457604201975AFD739C1376704101D5EC`.

Earlier validation evidence recorded on 2026-07-28:

- MSVC 19.38 configured every application target with `/std:c++17`.
- The final Phase 8–12 Windows Debug and Release CMake/Ninja builds each
  completed all 33 compile/link steps without errors after the HTTP workload
  and outbound MCP responsibility splits.
- Ubuntu 26.04 WSL completed the native Linux x86-64 Release build and test
  suite with GCC 15.2.0. Only NTFS/WSL clock-skew warnings remained; the
  compiler conversion warnings found on the first pass were fixed and the
  build rerun.
- `masterai security-status` reported the native Windows identity provider
  available, password storage prohibited, and listener policy loopback-only.
- Debug and Release `masterai_core_tests` passed configuration precedence and
  rejection, journal recovery/checkpoint/backup, persistent administrator and
  session rotation, role/scoped-token revocation, Windows DPAPI, identity
  buffer erasure, exact model size/digest promotion, load-time tamper blocking,
  path, Phase 5 chat/project, Phase 6 download-integrity, Phase 7 benchmark,
  and Phase 8–9 MCP policy cases.
- The Phase 8 native MCP conformance cases pass pinned-version negotiation,
  tool/resource discovery, explicit token-to-project bindings, bounded UTF-8
  file reads, literal project search, denial without a project binding, and
  newline-delimited stdio framing. Streamable HTTP uses the same dispatcher and
  bearer-token authority at `/mcp`.
- The Phase 9 native tests restore a durable registry, recheck the pinned
  executable SHA-256, run a real child MCP fixture inside Windows job-object
  process/memory limits and a minimal environment, complete a three-request
  loopback Streamable HTTP session, enforce scope/project/approval denials,
  cancel before execution, and verify approved, denied, successful, and
  cancelled hash-chained audit entries.
- The Phase 10 native tests validate secret-free VS Code/Agent-Coder and Visual
  Studio profiles, project-bound source diagnostics, enforcement of the new
  top-of-unit comment rule, read-only unified-diff counts, traversal denial,
  and DPAPI-protected scoped IDE token installation/removal.
- The Phase 11 native test performs clean-destination backup restoration,
  manifest tamper rejection, secret and log rotation, hash-bound
  upgrade/rollback, and named-residue crash recovery.
- The Phase 12 Release probe retained checksum `92736000`; its final fixed
  workload measured 333.978 ns/op for small JSON strings and 67,127.400 ns/op
  for 16 KiB strings. Debug and Release correctness suites passed afterward.
- The Phase 4 isolated-runner fixture starts a separate process and passes
  readiness, tokenization, incremental generation, completion metrics, and
  unload checks without loading model memory into the test control process.
- Phase 5–7 tests pass durable project/chat/attachment reconstruction,
  attachment ownership and digest enforcement, resumable download-job
  reconstruction, digest quarantine, benchmark persistence, compatibility,
  and recommendation ordering.
- Test cases compile from `test/tests.cpp`; production test code no longer
  resides under `src/`.
- A temporary Release-server smoke test drove the first-run wizard, then
  received HTTP 200 from `/health/live`, expected pre-setup HTTP 503 from
  `/health/ready`, expected HTTP 401 from `/api/v1/users/me`, and process exit
  0 after a graceful stop request.
- Hardware probing reported the active Windows x86-64 host.
- The empty `models/` taxonomy scanned safely without fabricating installed models.
- `scripts/verify-objectives.ps1` and `scripts/verify-objectives.sh` enforce reassessment when the objectives hash changes.
- Agent-Coder 1.2.290 re-analysed the rescoped workspace at 2026-07-28 13:04
  local time: 35 files, CMake-native, no third-party dependency foundation,
  `test/tests.cpp` indexed, obsolete `src/tests.cpp` absent, and the identity
  and workflow sources present. Its indexed objectives hash matches
  `docs/objectives.sha256`.

### Objective alignment audit

The current repository is aligned with these implemented objectives:

- Strict ISO C++17 target configuration and native Windows Debug/Release builds.
- Production/test/document/script/model/rule directory separation.
- Loopback-only listener construction and fail-closed readiness.
- Native OS randomness, SHA-256, OS identity verification, transient password
  erasure, persistent hashed opaque sessions, scoped revocable client tokens,
  OS-backed non-password secrets, and no password storage.
- Canonical project/model path containment and non-symlink policy.
- Categorized model discovery with exact size/digest/provenance/license
  verification, suitability-based Ready promotion, and tamper blocking at load.
- Structured inference launch arguments without shell interpolation.
- Deny-by-default inbound and outbound MCP authorization policy foundations.
- Objective hash reassessment and tests stored under `test/`.

These objective groups are not yet implemented or not yet validated:

- Real pinned-backend/model Phase 4–5 operational certification, interrupted
  real-source Phase 6 resume certification, same-host real-model Phase 7
  comparison, optional whisper.cpp integration, and MCP transports.
- Ubuntu 24.04 and Debian 13 packaging certification (deferred by operator)
  plus later-phase conformance and performance suites.
- Remaining Phase 18–20 real-model exit validation: Phase 18's same-host
  repeated-turn prompt-prefix/KV-session reuse benchmark, Phase 19's
  real-hardware-class calibration benchmark (GPU utilization and
  thermal-trend probing also remain forward work — no approved vendor SDK
  exists), and every Phase 20 candidate optimization, which stays
  unimplemented behind `AdvancedOptimizationRegistry`'s structural
  disabled-by-default gate until its own evidence is produced.
  Phase 15 (representative indexing ceiling evidence, live change-source
  adapters, portable-versus-native I/O benchmark decision), Phase 16
  (deadline-bound hybrid retrieval over the strategies Phase 15's index can
  serve today), and Phase 17 (security-partitioned caching of Phase 16
  retrieval results, versioned-key isolation, atomic checksum-quarantined
  disk entries, and administrative status/trim/clear) are implemented and
  validated as described above; Phase 16's authored retrieval-quality
  evaluation set and Phase 17's representative-query latency benchmark
  remain outstanding, semantic/dependency/conversation-memory/MCP-resource
  retrieval strategies remain deferred forward work pending their own
  supporting infrastructure, and Phase 17's file_content/parsed_chunk/
  embedding/tokenization/prompt cache segments remain declared without a
  real producer until the phases that generate that content exist.

## 1. Executive Design

The system will be a native modular service named provisionally **Local Programming AI Server**. It will coordinate local language models, user access, development projects, chats, files, benchmarks, and MCP integrations from one secured control plane.

The mandatory technology direction is:

- **All application source:** ISO C++17. C++20 or later language features are prohibited.
- **Optional low-level source:** Assembly, isolated behind C++17 interfaces and only when profiling proves a benefit.
- **Web frontend:** Native-server-generated browser assets. No TypeScript or other application source language is permitted.
- **Persistence:** An in-house MasterAI record store with bounded records, explicit schema versions, checksummed journals, atomic replacement, recovery scanning, and replaceable repository interfaces.
- **Inference:** process-isolated adapters, beginning with `llama.cpp`-compatible GGUF models.
- **MCP:** native protocol gateway supporting `stdio` and Streamable HTTP, with optional legacy SSE compatibility.
- **Linux service manager:** `systemd` integration, while retaining standalone scripts.
- **Cryptography:** established audited libraries, never custom cryptographic primitives.

The project must not attempt to write its own transformer inference engine in the first release. The ground-up component is the secure host and orchestration platform. Inference is connected through replaceable native adapters so later optimized engines can be introduced without changing the external API.

## 2. Core Architectural Principles

### 2.1 Control plane and inference plane separation

The control plane manages:

- HTTP and WebSocket/streaming connections.
- Authentication and authorization.
- Users, roles, projects, chats, and attachments.
- Configuration.
- Model catalogue and manifests.
- Download jobs.
- Hardware suitability decisions.
- Benchmark orchestration.
- MCP routing and policy.
- Audit records.
- Process supervision.

The inference plane contains isolated model runner processes. Each runner receives normalized inference requests over a local IPC channel. A runner crash must not crash the control plane.

### 2.2 Deny-by-default policy

Every network listener, API route, MCP server, MCP tool, filesystem path, download source, and administrative operation starts disabled or denied unless explicitly allowed.

### 2.3 Native deployment, not container deployment

Deployment consists of signed binaries, frontend assets, scripts, schemas, migrations, and optional service definitions. Build isolation may use ordinary compiler toolchains, but runtime must not depend on Docker.

### 2.4 Replaceable adapters

Define stable interfaces for:

- Inference backends.
- Model repositories.
- Hardware probes.
- Speech transcription.
- Embedding engines.
- MCP transports.
- Secret stores.
- Authentication extensions.

### 2.5 Security cannot be optional on a listening interface

Localhost mode can reduce exposure, but it does not eliminate browser-origin attacks, malicious local applications, compromised accounts, or unsafe MCP tools. Authentication and authorization remain part of the design.

## 3. Proposed Repository Structure

```text
MasterAI/
├── docs/
│   ├── objectives.md
│   ├── PLAN.md
│   ├── architecture/
│   ├── security/
│   ├── api/
│   ├── mcp/
│   └── operations/
├── src/
│   ├── app/
│   ├── auth/
│   ├── audit/
│   ├── config/
│   ├── crypto/
│   ├── database/
│   ├── downloads/
│   ├── hardware/
│   ├── http/
│   ├── inference/
│   ├── jobs/
│   ├── mcp/
│   ├── models/
│   ├── projects/
│   ├── security/
│   ├── telemetry/
│   └── workspace/
├── test/
│   └── tests.cpp
├── scripts/
│   ├── CMakeLists.txt
│   ├── build.ps1
│   ├── build.sh
│   ├── test.ps1
│   └── test.sh
├── models/
│   ├── general-programming/
│   ├── code-completion/
│   ├── code-review/
│   ├── debugging/
│   ├── documentation/
│   └── embeddings-code-search/
├── runtime/
│   ├── projects/
│   ├── attachments/
│   ├── indexes/
│   ├── downloads/
│   ├── benchmarks/
│   ├── logs/
│   ├── run/
│   └── backups/
└── rules/
```

All production source remains under `src/`, all test cases under `test/`, all
build and test automation under `scripts/`, and all model artifacts under
`models/`. Generated runtime data must not be mixed with source files. Installed
Linux deployments may map `runtime/` to `/var/lib/masterai`, configuration to
`/etc/masterai`, and logs to `/var/log/masterai`.

## 4. Planned Runtime Components

### 4.1 Main server process

Responsibilities:

- Read and validate configuration.
- Initialize logging, database, secret store, and audit service.
- Perform startup security checks.
- Bind approved interfaces.
- Serve static frontend assets.
- Expose API routes.
- Supervise background jobs.
- Route inference requests.
- Supervise MCP connections.
- Publish health and metrics.
- Coordinate graceful shutdown.

The main server should not load a large model directly into its address space.

### 4.2 Inference supervisor

Responsibilities:

- Start runner processes with restricted environment and permissions.
- Select backend executable and arguments from a validated adapter configuration.
- Allocate model-specific ports or IPC endpoints inaccessible from the LAN.
- Detect startup success and readiness.
- Enforce memory and concurrency limits.
- Restart only according to bounded policy.
- Capture runner logs.
- Drain and unload models.

### 4.3 Model runner adapters

Initial adapter order:

1. `llama.cpp` GGUF adapter.
2. Optional local OpenAI-compatible adapter.
3. Optional ONNX Runtime adapter for embeddings or compact classifiers.
4. Later hardware-specific adapters after profiling justifies them.

Each adapter implements:

- Probe backend availability.
- Validate model compatibility.
- Estimate resources.
- Build a safe process launch specification.
- Start and stop.
- Report readiness.
- Submit generation.
- Stream tokens.
- Tokenize and count.
- Cancel generation.
- Collect metrics.

### 4.4 Model registry

The registry scans `models/<category>/<model-id>/manifest.json`. It must never infer trust merely because a file exists.

Registry states:

- Discovered
- Validating
- Valid
- Invalid
- Unverified
- Downloading
- Partial
- Ready
- Loading
- Loaded
- Failed
- Quarantined

### 4.5 Job manager

Long-running tasks become persistent jobs:

- Model download.
- Hash verification.
- Model conversion where permitted.
- Benchmark execution.
- Project indexing.
- Attachment parsing.
- Backup and restore.

Jobs have IDs, state transitions, owner, progress, logs, cancellation state, and restart policy.

### 4.6 MCP gateway

The gateway has two separate modules:

- **Inbound MCP server:** exposes approved project, search, code-analysis, benchmark, and inference capabilities to trusted development clients.
- **Outbound MCP client:** connects the AI orchestration layer to administrator-approved MCP servers and tools.

Never combine inbound identity with unrestricted outbound tool authority. Every outbound tool call is re-authorized against user, role, project, tool, and request context.

## 5. Configuration System

### 5.1 Files

```text
config/settings.json
config/settings.local.json
config/secrets.enc
config/config.schema.json
config/config.version
```

`settings.json` contains non-secret settings. `secrets.enc` contains encrypted application secrets. Password hashes remain in the user database, not in configuration.

### 5.2 Configuration precedence

Lowest to highest:

1. Compiled safe defaults.
2. Default configuration file.
3. Saved administrator configuration.
4. Environment variables explicitly supported by schema.
5. Command-line overrides.

An override is temporary unless `--save-overrides` is explicitly supplied and the user is authorized.

### 5.3 Example configuration domains

```json
{
  "schemaVersion": 1,
  "server": {
    "host": "127.0.0.1",
    "port": 7070,
    "allowIntranet": false,
    "trustedProxies": [],
    "maxRequestBytes": 16777216,
    "shutdownGraceSeconds": 30
  },
  "tls": {
    "mode": "disabled-loopback-only",
    "certificateFile": "",
    "privateKeyFile": ""
  },
  "auth": {
    "enabled": true,
    "allowSingleUserBypass": false,
    "sessionMinutes": 480,
    "requireMfaForAdmins": false
  },
  "workspace": {
    "root": "./runtime",
    "models": "./models",
    "projects": "./runtime/projects"
  },
  "models": {
    "allowedCategories": [
      "general-programming",
      "code-completion",
      "code-review",
      "debugging",
      "documentation",
      "embeddings-code-search"
    ],
    "maxLoaded": 1,
    "memoryReserveMiB": 2048
  },
  "mcp": {
    "stdioEnabled": true,
    "streamableHttpEnabled": true,
    "legacySseEnabled": false
  }
}
```

The final schema must reject unknown fields unless a controlled extension namespace is used.

### 5.4 First-run Linux flow

`start.sh` invokes the configuration controller before launching the binary.

Interactive logic:

```text
No configuration found.
Run initial configuration now? [Y/n]

or

Existing configuration detected: config/settings.json
Configuration version: 1
Last modified: <timestamp>
Choose: [U]se, [R]eview, [X] reset, [C]ancel
```

Reset logic:

1. Ask for reset.
2. Display what will be reset and what will be preserved.
3. Require a second confirmation.
4. Back up settings and encrypted secrets.
5. Run the wizard.
6. Validate.
7. Write to a temporary file.
8. `fsync` where applicable.
9. Atomically rename into place.

Non-interactive services must fail with a clear diagnostic rather than waiting for input.

## 6. Network and Web Server Design

### 6.1 Binding policy

Default:

```text
127.0.0.1:7070
[::1]:7070, if IPv6 loopback is enabled
```

Intranet mode requires:

- An explicit interface or private address.
- TLS enabled.
- Authentication enabled.
- Host allow-list.
- Origin allow-list.
- Firewall guidance.
- A startup warning showing all bound interfaces.

Reject a configuration that combines `0.0.0.0`, disabled authentication, and disabled TLS.

### 6.2 Apache-inspired protections

Adopt mature principles, not Apache source code:

- Privilege separation.
- Drop privileges after binding where privileged ports are ever supported.
- Strict virtual-host or host-header checks.
- Explicit route configuration.
- Bounded request header and body sizes.
- Header read timeout and body read timeout.
- Keep-alive limits.
- Concurrent connection limits.
- Access and error logs.
- Deny directory listing.
- Canonical path resolution.
- Symlink policy.
- MIME type allow-list.
- Static-file root isolation.
- TLS policy separated from application logic.
- Reverse-proxy awareness only when explicitly enabled.

### 6.3 API protocols

Use:

- HTTPS/HTTP for request-response APIs.
- Server-sent streaming or chunked responses for token output.
- WebSocket only for features that genuinely require bidirectional low-latency communication, such as voice streaming.
- MCP Streamable HTTP on a dedicated endpoint.

### 6.4 Core endpoint outline

```text
GET    /health/live
GET    /health/ready
POST   /api/v1/auth/login
POST   /api/v1/auth/logout
POST   /api/v1/auth/refresh
GET    /api/v1/users/me
GET    /api/v1/projects
POST   /api/v1/projects
GET    /api/v1/chats
POST   /api/v1/chats
POST   /api/v1/chats/{id}/messages
POST   /api/v1/chats/{id}/cancel
GET    /api/v1/models
POST   /api/v1/models/{id}/load
POST   /api/v1/models/{id}/unload
POST   /api/v1/model-downloads
GET    /api/v1/jobs/{id}
POST   /api/v1/benchmarks
GET    /api/v1/settings
PATCH  /api/v1/settings
POST   /api/v1/server/restart
POST   /api/v1/server/shutdown
POST   /mcp
GET    /mcp
```

The exact MCP endpoint behaviour must track the selected stable protocol version.

## 7. Authentication and Authorization Plan

### 7.1 First administrator

On a new installation:

1. Server starts in setup-only mode bound to loopback.
2. A one-time setup token is printed to the local terminal or stored in a root-readable file.
3. The user opens the setup page.
4. The user supplies the token and creates the first administrator.
5. The token is invalidated immediately.
6. Normal routes are enabled.

This avoids shipping a default username and password.

### 7.2 Password handling

- Delegate password verification to the operating system; MasterAI has no
  password database.
- Use Windows `LogonUserW` and Linux PAM, failing closed when the provider is
  unavailable.
- Erase mutable password and converted native buffers immediately after
  verification.
- Never log passwords.
- Never place passwords in command-line arguments.
- Never reversibly encrypt passwords.
- Never send passwords to Agent-Coder, inference backends, MCP servers, or model
  context.
- Require TLS before accepting credentials on any non-loopback listener.

### 7.3 Session design

Prefer server-side opaque sessions for the initial release.

Session record fields:

- Random session identifier hash.
- User ID.
- Creation and expiry timestamps.
- Last activity.
- Authentication strength.
- CSRF secret or binding.
- User-agent summary.
- Optional client IP prefix, used carefully.
- Revocation status.

Cookie requirements:

- `HttpOnly`.
- `Secure` in TLS mode.
- `SameSite=Strict` where compatible.
- Narrow path.
- Rotation after login, privilege change, and password change.

### 7.4 Roles

Initial roles:

- Administrator
- Developer
- Viewer
- Service Client

Permissions must be granular, for example:

- `models.view`
- `models.download`
- `models.load`
- `projects.create`
- `projects.read`
- `projects.modify`
- `mcp.tools.invoke`
- `settings.read`
- `settings.modify`
- `server.restart`
- `server.shutdown`
- `audit.read`

### 7.5 Service clients

VS Code, Visual Studio, CLI, and desktop applications should use revocable personal access tokens or a local authorization flow rather than storing the user's password.

Token properties:

- Displayed once.
- Stored hashed.
- Named and scoped.
- Expiring where possible.
- Revocable.
- Audited.
- Restricted to selected projects and capabilities.

## 8. Security Architecture

### 8.1 Threat model categories

Document threats covering:

- Unauthenticated network access.
- Credential stuffing and brute force.
- Session theft.
- CSRF and cross-origin attacks.
- XSS.
- SQL injection.
- Path traversal.
- Malicious attachments.
- Malicious model files.
- Model supply-chain compromise.
- Prompt injection.
- MCP tool poisoning.
- Excessive tool authority.
- Command injection.
- Runner process escape.
- Secrets disclosure.
- Denial of service.
- Resource exhaustion by model loading.
- Download tampering.
- Unsafe administrative shutdown.
- Log injection.

### 8.2 Filesystem controls

- Canonicalize every path before authorization.
- Store project roots by internal ID.
- Reject `..`, alternate separators, device paths, and path aliases after canonicalization.
- Do not follow symlinks by default.
- Keep uploads outside executable and static roots.
- Use per-project directories.
- Apply restrictive permissions.
- Write through temporary files and atomic rename.

### 8.3 Attachment pipeline

1. Stream upload into a bounded temporary file.
2. Calculate hash during transfer.
3. Enforce size and extension policy.
4. Detect actual media type.
5. Reject executables unless an explicit programming-project policy allows them.
6. Quarantine before parsing.
7. Parse in a restricted worker process.
8. Store extracted text separately from the original.
9. Mark extracted content as untrusted context.
10. Audit acceptance or rejection.

### 8.4 Secrets

Use one of:

- Linux kernel keyring or a supported OS secret service.
- An encrypted local secrets file protected by a master key stored outside the workspace.
- Windows Credential Manager on Windows.

Do not place API tokens, private keys, or signing keys in ordinary JSON.

### 8.5 Audit events

Record:

- Authentication success and failure.
- Session creation and revocation.
- User and role changes.
- Configuration changes.
- Model download, verification, load, and unload.
- MCP server registration.
- MCP tool invocation and approval.
- Project access changes.
- Backup and restore.
- Restart and shutdown.

Audit records should be append-oriented, timestamped, request-correlated, sanitized, and protected from ordinary users.

## 9. Model Directory and Manifest

### 9.1 Directory example

```text
models/
└── general-programming/
    └── example-coder-7b-q4/
        ├── manifest.json
        ├── model.gguf
        ├── LICENSE
        ├── README.md
        └── checksums.sha256
```

### 9.2 Manifest example fields

```json
{
  "schemaVersion": 1,
  "id": "example-coder-7b-q4",
  "displayName": "Example Coder 7B Q4",
  "category": "general-programming",
  "source": {
    "provider": "huggingface",
    "repository": "owner/repository",
    "revision": "immutable-commit-id"
  },
  "license": {
    "spdx": "Apache-2.0",
    "acceptedAt": null
  },
  "model": {
    "format": "gguf",
    "architecture": "example",
    "parameters": 7000000000,
    "quantization": "Q4_K_M",
    "contextLength": 32768
  },
  "requirements": {
    "minimumRamMiB": 8192,
    "recommendedRamMiB": 16384,
    "minimumVramMiB": 0,
    "estimatedDiskMiB": 5000
  },
  "files": [
    {
      "path": "model.gguf",
      "size": 0,
      "sha256": "..."
    }
  ],
  "backends": ["llama-cpp"]
}
```

Manifest figures from third parties are advisory. The host should also inspect actual file metadata and compare it with policy.

## 10. Hardware Suitability Engine

### 10.1 Probe phase

At startup and on demand:

- Read CPU topology.
- Detect instruction sets.
- Read physical and available RAM.
- Enumerate GPUs and VRAM.
- Detect inference backend support.
- Measure free disk.
- Record configured reserve.

### 10.2 Memory estimate

Estimate:

```text
model weights
+ key/value cache at selected context length
+ backend working memory
+ graph buffers
+ operating-system reserve
+ concurrent request multiplier
+ safety margin
```

Do not base acceptance only on model file size.

### 10.3 Decision policy

- **Unsupported:** backend or architecture cannot run.
- **Memory Risk:** predicted working set breaches the hard cap or reserve.
- **Slow:** model can load, but predicted throughput falls below configured target.
- **Usable:** expected to operate acceptably with reduced context or concurrency.
- **Recommended:** comfortably fits and meets target performance.

### 10.4 User response

When blocked, show:

- Required versus available RAM/VRAM.
- Relevant context and batch assumptions.
- Why the model is blocked.
- Compatible installed alternatives.
- Approved downloadable alternatives.
- Estimated download size.

The user may adjust context length or backend settings and re-evaluate.

## 11. Model Download System

### 11.1 Supported source adapters

- Hugging Face Hub.
- GitHub Releases.
- Direct HTTPS from administrator-approved domains.
- Future private registry adapter.

Avoid arbitrary shell commands supplied by model metadata.

### 11.2 Download workflow

1. Normalize source identifier.
2. Check provider allow-list.
3. Resolve immutable revision and file list.
4. Retrieve license and metadata.
5. Require license acceptance where needed.
6. Check disk space.
7. Create persistent job record.
8. Download to `.partial` files.
9. Use range requests or the provider's supported resume mechanism.
10. Persist progress checkpoints.
11. Verify expected size.
12. Verify SHA-256 or stronger digest.
13. Scan manifest and filenames.
14. Atomically move into the final model directory.
15. Re-scan registry.
16. Optionally run a smoke benchmark.

### 11.3 Progress reporting

CLI and web UI show:

- Current file.
- Files completed and total.
- Bytes completed and total.
- Percentage.
- Current and average speed.
- Estimated remaining time, explicitly labeled as an estimate.
- Retry count.
- Verification progress.

### 11.4 Resume safety

Persistent state includes source revision, expected file size, digest, partial path, completed ranges, and ETag where useful. If the remote revision changes, do not append to the old partial file; quarantine or discard it according to policy.

## 12. Inference Request Lifecycle

1. Authenticate client.
2. Authorize project and model.
3. Validate request size and generation limits.
4. Resolve selected model and backend.
5. Run suitability and availability checks.
6. Load model if permitted.
7. Construct system policy and project context.
8. Retrieve relevant project context.
9. Mark untrusted attachment or tool content.
10. Tokenize and enforce context limits.
11. Queue with priority and bounded waiting time.
12. Stream generation.
13. Permit cancellation.
14. Record usage and performance metrics.
15. Store message according to retention policy.
16. Release request resources.

## 13. Projects, Chats, and Context

### 13.1 Project model

A project contains:

- Name and description.
- Owner and collaborators.
- Root path or managed uploaded files.
- Allowed file patterns.
- Excluded paths.
- Selected default model.
- System prompt profile.
- MCP permissions.
- Index state.
- Retention settings.

### 13.2 Repository access modes

- **Managed copy:** files uploaded into the workspace.
- **Registered local path:** server reads an administrator-approved path.
- **Client-provided context:** IDE sends selected files or snippets.

Registered paths must be explicitly approved and scoped.

### 13.3 Context indexing

Initial implementation:

- File discovery with ignore rules.
- Language detection.
- Chunking by syntax-aware boundaries where practical.
- Embeddings using a programming-relevant local embedding model.
- MasterAI record-store metadata plus an in-house bounded local vector index.
- Incremental updates by file hash.
- Project-scoped retrieval.

The system must show users which files were used as context.

## 14. Web User Interface Plan

### 14.1 Primary layout

- Left navigation: chats, projects, models, benchmarks, administration.
- Main area: conversation or selected tool.
- Right context panel: model, project, files, token usage, active tools.

### 14.2 Chat features

- New Chat.
- Rename and archive.
- Project assignment.
- Model selection.
- System profile selection.
- Streaming output.
- Stop generation.
- Retry.
- Edit and resubmit.
- Code block copy.
- Diff preview.
- Apply-patch request routed through explicit approval.
- Source/context disclosure.

### 14.3 Attachments

Support programming-relevant text, source, archive, image-for-debugging, log, and document formats according to policy. Archive expansion occurs in a sandboxed worker with expansion-ratio limits.

### 14.4 Voice recording

Voice is an optional prompt-entry feature:

1. Obtain browser permission.
2. Record locally.
3. Upload over authenticated TLS or loopback connection.
4. Transcribe with an approved programming-adjacent speech-to-text adapter.
5. Show transcript for editing before submission by default.
6. Delete raw audio according to retention policy.

### 14.5 Settings restart flow

1. User edits settings.
2. Server validates proposed configuration.
3. UI displays changes requiring restart.
4. Authorized user confirms.
5. Server writes configuration atomically.
6. Service enters draining state.
7. Restart is performed by the supervisor or service manager.
8. UI reconnects and reports result.

The process must not let the web server directly replace its executable or elevate privileges.

## 15. MCP Detailed Plan

### 15.1 Protocol baseline

Implement against a selected stable MCP specification version and expose that version in capability negotiation. Track newer releases behind feature flags until conformance tests pass.

### 15.2 Transport policy

- `stdio`: primary local process transport.
- Streamable HTTP: primary network transport.
- Legacy HTTP+SSE: optional compatibility module only.

Streamable HTTP should use one configured MCP endpoint supporting the required HTTP methods and streaming behaviour.

### 15.3 Inbound tools

Programming-focused examples:

- List authorized projects.
- Read an authorized file.
- Search project code.
- Retrieve diagnostics.
- Start a model inference request.
- Run an approved benchmark.
- Query model inventory.

High-impact operations such as file modification, process execution, builds, or test runs require stronger permissions and approval policy.

### 15.4 Outbound server registry

Each MCP server registration includes:

- ID and display name.
- Transport.
- Executable or URL.
- Immutable executable path where applicable.
- Working directory.
- Allowed environment variables.
- Allowed tools.
- Network policy.
- User/project scopes.
- Approval mode.
- Timeout.
- Output limit.
- Trust state.

### 15.5 `stdio` process safety

- Do not invoke through a shell unless strictly necessary.
- Pass argument arrays directly.
- Use an absolute executable path.
- Sanitize environment.
- Restrict working directory.
- Capture stdout only for protocol messages and stderr for logs.
- Apply process and resource limits.
- Terminate process trees on shutdown.

### 15.6 Prompt injection controls

- Treat tool output and retrieved files as untrusted data.
- Keep system policy separate from retrieved text.
- Do not let retrieved text grant permissions.
- Re-authorize every tool call.
- Present sensitive proposed actions to the user.
- Avoid exposing secrets in tool context.
- Log the authority decision independently from model reasoning.

## 16. VS Code Integration Plan

Develop a thin extension that connects to the server rather than embedding model logic.

Features:

- Sign in or register a scoped token.
- Select server URL.
- Validate TLS certificate.
- Select project and model.
- Chat panel.
- Send selection, file, diagnostics, or workspace context.
- Inline code actions.
- Diff preview before applying changes.
- MCP connection mode where supported.
- Cancellation and status.

Store tokens using VS Code SecretStorage, not plaintext settings.

## 17. Visual Studio Integration Plan

Develop a VSIX extension with equivalent core capabilities:

- Secure server registration.
- Token storage using Windows-protected storage.
- Tool window for chat.
- Solution and project context.
- Error List and build output context.
- Editor selection commands.
- Diff and patch preview.
- MCP or HTTP API client.

The extension must remain a client; it does not own the inference process.

## 18. CLI and Desktop Client Plan

### 18.1 CLI

Commands:

```text
lpai login
lpai chat
lpai projects list
lpai models list
lpai models load <id>
lpai models download ...
lpai benchmark <id>
lpai mcp list
lpai status
lpai shutdown
```

### 18.2 Desktop client

A desktop application is optional after the web API stabilizes. It should reuse the same API, authentication, streaming, and project model. Avoid creating a separate protocol.

## 19. Scripts Plan

Every script must support `--help`, `--version` where appropriate, non-interactive operation, structured exit codes, and safe failure.

### 19.1 `scripts/configure.sh`

Options:

```text
--interactive
--non-interactive
--config <path>
--reset
--review
--set key=value
--validate-only
--help
```

### 19.2 `scripts/build.sh`

Responsibilities:

- Check compiler and build tools.
- Configure CMake preset.
- Build server and tools.
- Build frontend assets.
- Run selected tests.
- Produce a native install tree.

Options:

```text
--debug
--release
--clean
--tests
--no-web
--jobs <n>
--prefix <path>
--help
```

### 19.3 `scripts/start.sh`

Responsibilities:

- Locate installation.
- Acquire lifecycle lock.
- Detect configuration.
- Run interactive configuration flow where appropriate.
- Validate configuration.
- Check port.
- Check database migrations.
- Start process.
- Write PID and instance metadata only after successful launch.
- Wait for readiness when requested.

Options:

```text
--config <path>
--host <address>
--port <port>
--foreground
--daemon
--wait-ready
--save-overrides
--help
```

### 19.4 `scripts/stop.sh`

Responsibilities:

- Read and validate PID metadata.
- Confirm process identity.
- Request authenticated graceful shutdown through local control socket where possible.
- Wait for drain period.
- Send `SIGTERM` if required.
- Use `SIGKILL` only with explicit `--force` after timeout.
- Remove stale lifecycle files safely.

### 19.5 `scripts/download-model.sh`

Options should encapsulate:

```text
--source huggingface|github|https
--repo <owner/repository>
--revision <immutable-revision>
--file <pattern>
--category <approved-category>
--model-id <id>
--token-env <environment-variable-name>
--resume
--verify
--license-accept
--suggest-for-system
--non-interactive
--help
```

Do not accept a secret token directly as a command-line value.

### 19.6 `scripts/benchmark-model.sh`

Options:

```text
--model <id>
--profile quick|standard|extended
--backend <id>
--context <tokens>
--concurrency <n>
--output <path>
--compare <prior-result>
--help
```

### 19.7 `scripts/doctor.sh`

Checks:

- Configuration schema.
- Directory permissions.
- Port conflicts.
- TLS files.
- Database integrity.
- Available inference backends.
- Model manifests.
- Disk capacity.
- GPU access.
- MCP registrations.
- Service manager state.

## 20. Shutdown and Restart Design

Shutdown entry points:

- Administrative web UI.
- `POST /api/v1/server/shutdown`.
- CLI command.
- Shutdown script.
- Service manager.
- OS signal.

All converge on one shutdown coordinator:

1. Authenticate and authorize source where applicable.
2. Mark service not ready.
3. Stop accepting new inference jobs.
4. Notify clients.
5. Cancel or drain active requests according to policy.
6. Persist resumable jobs.
7. Stop MCP child processes.
8. Unload model runners.
9. Flush database and audit logs.
10. Close listeners.
11. Remove runtime metadata.
12. Exit with a meaningful code.

Restart is shutdown plus supervisor-controlled relaunch. A process should not fork an uncontrolled replacement of itself.

## 21. Benchmark Framework

### 21.1 Profiles

**Quick:** load time, one prompt, throughput, memory.

**Standard:** several prompt lengths, generation lengths, and programming tasks.

**Extended:** context scaling, concurrency, repeated runs, thermal behaviour, and quality suite.

### 21.2 Reproducibility

Record:

- Exact model hash.
- Manifest.
- Backend binary hash and version.
- Backend arguments.
- Hardware fingerprint.
- OS and drivers.
- Power mode where detectable.
- Test data version.
- Warm or cold state.
- Number of repetitions.

### 21.3 Quality evaluation

Quality tests must be locally runnable and license-compatible. Keep performance and quality scores separate; a fast model is not necessarily an effective programming model.

## 22. Persistence Design

Initial MasterAI record types:

- `schema_migrations`
- `users`
- `roles`
- `user_roles`
- `permissions`
- `role_permissions`
- `sessions`
- `service_tokens`
- `projects`
- `project_members`
- `conversations`
- `messages`
- `attachments`
- `models`
- `model_files`
- `model_benchmarks`
- `jobs`
- `mcp_servers`
- `mcp_tool_policies`
- `audit_events`
- `settings_history`

Use foreign keys, transactions, prepared statements, migration checksums, and tested backup procedures.

## 23. Logging and Observability

### 23.1 Log streams

- Application log.
- Access log.
- Security log.
- Audit log.
- Inference runner log.
- Download log.
- MCP log.

### 23.2 Structured fields

- Timestamp.
- Severity.
- Component.
- Event code.
- Request ID.
- User ID where permitted.
- Project ID.
- Model ID.
- Duration.
- Result.

Never log passwords, session tokens, API tokens, private prompts under restrictive privacy modes, or full file contents by default.

### 23.3 Metrics

- Active connections.
- Request rate and latency.
- Authentication failures.
- Queue depth.
- Model load state.
- Token throughput.
- Memory and VRAM.
- Download throughput.
- MCP call rate and failures.
- Database latency.

## 24. Testing Strategy

### 24.1 Unit tests

- Configuration parsing and migration.
- Password hashing and verification wrappers.
- Permission evaluation.
- Path canonicalization.
- Manifest validation.
- Suitability calculations.
- Download state transitions.
- API validation.

### 24.2 Integration tests

- First-run setup.
- Login and session rotation.
- Project workflows.
- Model scan/load/generate/unload.
- Download interruption and resume.
- Graceful shutdown.
- MCP inbound and outbound connections.
- IDE client authentication.

### 24.3 Security tests

- Brute-force throttling.
- CSRF.
- XSS.
- SQL injection.
- Path traversal.
- Symlink escape.
- Host-header abuse.
- Origin bypass.
- Malicious archives.
- Command injection in MCP launch settings.
- Tool permission bypass.
- Session fixation.
- Token leakage.
- Dependency scanning.

### 24.4 Performance tests

- HTTP concurrency.
- Streaming backpressure.
- Large prompt ingestion.
- Cancellation.
- Multiple model runner behaviour.
- Indexing throughput.
- Long-running stability.

### 24.5 Failure-injection tests

- Runner crash.
- Database busy state.
- Full disk.
- Corrupt model file.
- Interrupted download.
- Invalid configuration.
- Port conflict.
- Lost network during MCP call.
- Forced shutdown.

## 25. Phased Implementation Roadmap

### Phase 0 — Requirements and decisions

Status: Complete (validated 2026-07-28).

Deliverables:

- Final objectives.
- Threat model.
- Architecture decision records.
- Supported platform matrix.
- Model category taxonomy.
- API and MCP version policy.
- Initial UI wireframes.

Exit criteria:

- No unresolved high-risk architectural ambiguity.

### Phase 1 — Native foundation

Status: Complete for this work order (validated 2026-07-28); pinned Linux
packaging-host certification deferred by operator.

Deliverables:

- Build system.
- Core executable skeleton.
- Structured logging.
- Configuration schema and wizard.
- Lifecycle scripts.
- Health endpoints.
- MasterAI record-store schema migrations.

Exit criteria:

- Native server starts on loopback port 7070, validates configuration, reports health, and shuts down gracefully.

### Phase 2 — Identity and security baseline

Status: Complete (validated 2026-07-28).

Deliverables:

- First-admin setup.
- Native OS identity provider with transient-buffer erasure and fail-closed
  availability validation.
- Sessions.
- Roles and permissions.
- CSRF, origin, host, rate-limit, and request-limit controls.
- Audit events.
- TLS configuration.

Exit criteria:

- Security integration tests pass and intranet binding is rejected without required protections.

### Phase 3 — Model registry and hardware assessment

Status: Complete (validated 2026-07-28).

Deliverables:

- Category directory scanner.
- Model manifest schema.
- Hardware probes.
- Suitability engine.
- Model inventory API and UI.

Exit criteria:

- Installed models are classified accurately and unsafe loads are blocked.

### Phase 4 — First inference adapter

Status: Implemented; real pinned-backend/model exit validation pending
(2026-07-28).

Deliverables:

- Runner supervisor.
- `llama.cpp` adapter.
- Load, unload, tokenize, generate, stream, and cancel operations.
- Resource monitoring.

Exit criteria:

- A supported programming model can answer through the API without placing model memory in the main process.

### Phase 5 — Chat and project web application

Status: Implemented; real-model browser and optional transcription exit
validation pending (2026-07-28).

Deliverables:

- Login UI.
- New Chat and history.
- Projects.
- Model selector.
- Streaming responses.
- Attachments.
- Context display.
- Basic voice recording and transcription adapter interface.

Exit criteria:

- End-to-end authenticated programming chat works from the browser.

### Phase 6 — Secure downloads

Status: Implemented; interrupted real-source resume exit validation pending
(2026-07-28).

Deliverables:

- Hugging Face adapter.
- GitHub Releases adapter.
- Resumable job engine.
- Progress UI and CLI.
- Hash verification.
- License and source policy.
- Hardware-based recommendations.

Exit criteria:

- Interrupted downloads resume correctly and model integrity is verified before registration.

### Phase 7 — Benchmarking

Status: Implemented; same-host real-model comparison exit validation pending
(2026-07-28).

Deliverables:

- Quick, standard, and extended profiles.
- Result persistence.
- Comparison UI.
- Model recommendation inputs.

Exit criteria:

- Benchmarks are reproducible and can compare candidate models on the same host.

### Phase 8 — MCP inbound

Status: Implemented; supported-IDE or independent-inspector connection
validation pending (2026-07-28).

Deliverables:

- `stdio` MCP server mode.
- Streamable HTTP endpoint.
- Authentication and scope mapping.
- Programming-focused tools and resources.
- Conformance tests.

Exit criteria:

- A supported IDE or MCP inspector can connect and use authorized capabilities.

### Phase 9 — MCP outbound

Status: Complete (validated 2026-07-28).

Deliverables:

- MCP server registry.
- `stdio` child-process client.
- Streamable HTTP client.
- Tool allow-list and approvals.
- Sandboxing and audit.
- Legacy SSE compatibility decision: disabled by ADR-0003 and rejected by the
  registry; no compatibility implementation is authorized for release 1.

Exit criteria:

- Tool invocation is isolated, authorized, cancellable, and fully audited.

### Phase 10 — IDE integrations

Status: Implemented within the strict C++17 boundary; host packaging and
live VS Code/Visual Studio connection validation pending (2026-07-28).

Deliverables:

- VS Code integration: native MCP/HTTP bridge, secret-free Agent-Coder launch
  profile, and exact Connected AI Platforms setup guide are implemented.
  JavaScript/TypeScript extension-host packaging requires a future explicit
  exception to the C++17-only project rule.
- Visual Studio integration: the same native bridge, secret-free profile, and
  external-tool/MCP guide are implemented. Managed VSIX host glue requires the
  same future language-rule decision.
- Secure token setup: implemented with hidden console input, scope/user/project
  validation, DPAPI or native Linux protection, and audit.
- Chat, context, diagnostics, diff preview, and cancellation: implemented
  through backend-neutral versioned API/MCP contracts.

Exit criteria:

- Both IDE profiles target the generic local AI server without backend-specific
  logic. The exit check remains live connection validation in both IDE hosts.

### Phase 11 — Operations hardening

Status: Complete — native offline lifecycle operations, clean-host recovery
exercise, authoritative runtime-root scripts, and hardened systemd installation
are implemented and validated on Windows x86-64 Debug (2026-07-29). Linux
systemd runtime certification remains part of the release-host matrix.

Deliverables:

- `systemd` installer: implemented with pre-publication unit validation,
  capability removal, protected host/kernel surfaces, explicit writable roots,
  bounded restart policy, graceful SIGTERM, and data-preserving uninstall.
- Backup and restore: implemented with an allow-listed portable data set,
  deterministic size/SHA-256 manifest, clean staging destinations, tamper
  rejection, atomic publication, and restored runtime-root configuration.
- Secret rotation: implemented through the existing DPAPI/Linux-keyring
  `SecretStore`; plaintext is erased and never printed or audited.
- Log rotation: implemented for bounded operational logs while preserving the
  append-only audit hash chain.
- Upgrade and rollback: implemented as offline, regular-file-only,
  SHA-256-bound atomic replacement with a verified rollback receipt.
- Crash recovery: implemented by clearing only named residue and reusing the
  existing checksummed `RecordStore` recovery/checkpoint path.
- Security hardening guide: implemented in
  `docs/security/phase-11-hardening.md`; command and recovery procedure are in
  `docs/operations/phase-11-operations.md`.

Exit criteria:

- Satisfied by `test_phase_eleven_operations`: a clean settings/runtime
  destination restores database and attachment state, rewrites the configured
  runtime root, and rejects a subsequently tampered backup entry.

### Phase 12 — Performance optimization

Status: Complete for the currently measurable native control-plane scope. A
repeatable Release probe identified canonical JSON encoding as the measured
hotspot; the accepted optimization is documented with five-run before/after
evidence. Unmeasured inference/IDE transport ideas remain deliberately
unchanged (2026-07-29).

Only after measurements:

- Reduce serialization copies: implemented for the shared JSON encoder after a
  five-run Release baseline. Median small-string latency improved 96.48% and
  16 KiB latency improved 96.24%, with identical output checksum.
- Tune buffer pools: no change; the fixed probe did not identify this need.
- Optimize streaming: no change without a representative live stream profile.
- Add prompt-prefix caching: no change without workload hit-rate evidence.
- Tune batching and context reuse: no change without backend concurrency data.
- Evaluate alternate IPC: deferred until transport profiling shows a bottleneck.
- Add hardware-specialized adapters: deferred until supported-host evidence
  justifies an adapter and preserves the replaceable inference boundary.

Exit criteria:

- Satisfied for the accepted JSON optimization by
  `docs/performance/phase-12-json-encoding.md`, identical probe checksums,
  exact escape regression coverage, MCP/IDE integration tests, and the full
  security/correctness suite. No unsupported optimization was admitted.

### Phase 13 — Query measurement and resource baseline

Status: Complete. The bounded native implementation, authenticated interfaces,
real isolated-runner trace, Debug/Release validation, and evidence record are
documented in `docs/performance/phase-13-query-baseline.md`.

Purpose:

- Establish the evidence needed to optimize time to first token (TTFT), prompt
  evaluation, generation throughput, retrieval latency, and peak resident
  memory independently.

Dependencies:

- Phase 7 benchmark records and Phase 12 measurement discipline.
- The Phase 4 isolated-runner metrics boundary.

Deliverables:

- A lightweight `QueryCoordinator` trace identity spanning request admission,
  authentication, normalization, classification, retrieval planning,
  retrieval, ranking, prompt assembly, runner queueing, prompt evaluation,
  generation, persistence, and release.
- Monotonic per-stage timers, cancellation observations, queue-wait time, TTFT,
  prompt tokens/second, generation tokens/second, private/RSS memory, commit,
  page faults, and runner/control-plane attribution.
- Native Windows and Linux probes for physical/logical CPUs, instruction sets,
  NUMA topology where available, physical/available RAM, pagefile/swap, GPU and
  VRAM capabilities, storage class, and backend capabilities.
- Reproducible cold/warm benchmark definitions and a baseline report keyed by
  exact host, model, backend, build, settings, and prompt-suite hashes.
- Real streamed status states: `accepted`, `retrieving`, `queued`,
  `evaluating_prompt`, and `generating`; no simulated progress.

Installation/completion outcome:

- The installed service exposes authenticated request/resource metrics and can
  produce a baseline report without enabling any speculative optimization.

Exit criteria:

- One request can be traced from admission through final token and cleanup with
  stage timing and peak-memory evidence.
- Debug and Release correctness results remain unchanged, and instrumentation
  overhead is measured and bounded.

### Phase 14 — Bounded-memory foundation

Status: Complete. The single native memory authority, strict configuration,
authenticated status, inference lease integration, saturation/recovery tests,
and Debug/Release evidence are recorded in
`docs/performance/phase-14-memory-governance.md`.

Purpose:

- Make safe operation on modest systems an enforced runtime property rather
  than a configuration suggestion.

Dependencies:

- Phase 13 resource probes and measurements.

Deliverables:

- A system-wide `MemoryBudgetManager` covering the control plane, runner
  weights, compute buffers, KV cache, prompt cache, retrieval/index caches,
  file content, attachments, downloads, background jobs, and OS safety reserve.
- Configurable hard byte limits, minimum OS reserve, minimum free-RAM
  percentage, and `Normal`, `Elevated`, `High`, and `Critical` pressure states.
- Bounded queues and reusable bounded buffers; large immutable content is
  shared by views/references rather than duplicated during ranking and prompt
  assembly.
- Pressure actions in a deterministic order: stop prefetch, throttle/pause
  indexing, trim low-value caches, reject non-interactive work, reduce
  context/concurrency, unload idle embedding/generation runners, then reject
  new inference safely.
- Admission estimates that include weights, runtime/graph buffers, KV memory
  per sequence, active sequences, transient request memory, and safety margin.
- Minimal, balanced, and performance profiles. Minimal mode defaults to one
  inference request, one indexing worker, a 2,048–4,096-token context, no
  persistent model unless configured, and paused background work during
  CPU-bound inference.
- Synthetic pressure, cancellation, queue-saturation, and graceful-shutdown
  tests proving recovery without uncontrolled growth.

Installation/completion outcome:

- The installed service can select or accept a hard RAM cap and reports a clear
  corrective action when a request/model cannot fit.

Exit criteria:

- Pressure tests remain within the configured ceiling and preserve enough
  memory to persist state and shut down cleanly.
- No queue, cache, context, attachment buffer, or worker pool is unbounded.

### Phase 15 — Incremental disk-backed project indexing

Status: Complete. Both exit criteria have current evidence, and Windows
Debug and Release build/test validation for the watcher/ceiling-run change
set is recorded below (2026-07-31). The native disk segment/generation
foundation, fixed-capacity background
service, unchanged-file elimination, affected-path updates, exact
identifier-boundary symbol lookup, deleted/empty generation publication,
typed/coalesced trigger admission, authenticated project-bound
status/rebuild/cancel/notify routes, a native `ProjectWatcher` adapter, a
representative large-project ceiling measurement, a recorded I/O backend
decision, and focused Windows Debug validation exist. The `index/notify`
route still gives an external editor, IDE plugin, or version-control process
an authenticated, project-bound way to report an event explicitly; separately,
`ProjectWatcher` now also calls the same `ProjectIndexService` automatically
for every catalog project, so no external caller is required for the common
save/branch-switch case. Deeper language-aware symbol extraction remains a
forward enhancement, not an exit-criterion blocker.

Purpose:

- Make large repositories searchable without retaining whole projects or
  complete indexes in RAM.

Dependencies:

- Phase 14 budgets, bounded queues, cancellation, and pressure signals.

Deliverables:

- A staged pipeline: discovery, path-policy filter, metadata/stat,
  unchanged-file elimination, bounded content read, language detection,
  parsing/chunking, fingerprint/hash, and index publication.
- Separate priority-bounded interactive retrieval, background indexing,
  attachment parsing, download I/O, and maintenance pools; worker counts derive
  from physical cores and background work never consumes all cores.
- Incremental change detection using canonical path, size, modification time,
  filesystem identity where available, fast fingerprint, full digest when
  required, parser version, embedding-model hash, and chunking version.
- Append-friendly disk segments, a mutable delta journal, versioned/checksummed
  headers, read-only mapped segments where suitable, atomic generation
  manifests, and bounded compaction.
- Partial-index publication so exact path/text/symbol retrieval becomes useful
  before a large workspace is fully indexed.
- Save/watcher/branch-switch/manual/periodic triggers with debounce,
  cancellation, backpressure, and affected-edge-only graph updates. An
  authenticated `POST /api/v1/projects/{id}/index/notify` route gives an
  external adapter a stable way to submit save/watcher/branch-switch/periodic
  events, and the native `ProjectWatcher` (`src/project_watcher.cpp`) now also
  calls the same service automatically: `ReadDirectoryChangesW`/an I/O
  completion port on Windows, recursive bounded `inotify` on Linux, both
  debounced per project and special-casing `.git/HEAD` as a branch-switch
  full rescan.
- A portable bounded worker-based file reader remains the correctness
  baseline. Per the recorded 2026-07-31 decision in
  [docs/performance/phase-15-incremental-indexing.md](performance/phase-15-incremental-indexing.md),
  a replaceable Windows overlapped-I/O or Linux `io_uring` implementation is
  not adopted yet: measured rebuild throughput against a representative
  10,000-file synthetic tree left peak resident memory far below the
  configured ceiling, so no bottleneck currently justifies the added
  complexity.

Installation/completion outcome:

- Each approved project gains a versioned local index stored under bounded
  runtime storage, with visible progress, cancellation, and safe rebuild.

Exit criteria:

- A representative large project is indexed and incrementally updated within
  configured peak-RAM and disk ceilings.
- Restart, corrupt-segment quarantine, partial publication, and cancellation
  tests pass without losing the last valid index generation.

### Phase 16 — Deadline-bound hybrid retrieval

Status: Implemented, exit validation pending. `RetrievalPlanner`
(`src/retrieval.cpp`) and `ContextBudgeter` are implemented and wired into
`send_chat_message`'s existing `retrieval_planning`/`retrieval`/`ranking`
stages, replacing nothing (attachment context from Phase 5 is unchanged and
still appended first). The exact-symbol/exact-text/lexical strategies that
Phase 15's disk-backed index can actually serve are implemented, tested, and
Windows Debug/Release validated (`test_phase_sixteen_deadline_bound_retrieval`
in `test/tests.cpp`); the no-retrieval, current/open-files, recent-changes,
diagnostics/build-log, semantic/embedding, dependency-neighbour,
conversation-memory, and authorized-MCP-resource strategies are deliberately
deferred (see Deliverables) since none of their supporting infrastructure
exists yet. The authored-evaluation-set exit criterion is not yet produced.

Purpose:

- Retrieve a small, strong, explainable context set quickly instead of
  injecting an entire project into the model.

Dependencies:

- Phase 15 index formats and Phase 13 stage timing.

Deliverables:

- A `RetrievalPlanner` that chooses the least expensive sufficient strategy:
  no retrieval, current/open files, exact path/symbol/text, recent changes,
  diagnostics/build logs, lexical, semantic, dependency neighbours,
  conversation memory, or authorized MCP resources. **Implemented:** no
  retrieval (query too short or project not yet indexed), exact symbol,
  exact text, and per-token lexical union, each read live from
  `ProjectIndexService::search_text`/`search_symbol` with sufficiency
  sticky across steps so a cheap symbol hit skips broader, more expensive
  strategies entirely. **Deferred (forward work, no supporting
  infrastructure exists):** current/open files, recent changes,
  diagnostics/build logs, semantic/embedding, dependency neighbours,
  conversation memory, and authorized MCP resources.
- Parallel execution only across independent retrieval paths using bounded
  workers and cooperative cancellation. **Implemented:** a small in-process
  worker pool (`DeadlineTaskPool` in `src/retrieval.cpp`) claims independent
  per-strategy/per-token tasks; each worker checks the deadline before
  claiming its next task rather than mid-task, matching this codebase's
  existing cooperative-cancellation shape (`mcp_outbound.cpp`,
  `inference.cpp`).
- Hybrid fusion and reranking with canonical chunk identity, duplicate and
  overlap removal, freshness, source authority, symbol/dependency proximity,
  and query intent. **Implemented:** fusion on the index's own canonical
  chunk id (deduplicating overlap for free), with a bounded corroboration
  bonus when more than one strategy surfaces the same chunk, and strategy
  base-score ordering (symbol > literal text > lexical) as source authority.
  **Deferred:** dependency-proximity and query-intent-specific reranking
  signals beyond strategy/corroboration scoring.
- A `ContextBudgeter` that reserves generation tokens, applies per-source and
  total chunk/token caps, prefers compact high-value evidence, and never
  truncates security policy to admit more project text. **Implemented:**
  per-source and total chunk caps plus a total context-byte cap
  (`retrieval.maximumChunksPerSource`/`maximumTotalChunks`/
  `maximumContextBytes` in configuration), always keeping the highest-ranked
  evidence first and never truncating a kept chunk's text.
- A request deadline budget. On expiry, the coordinator uses the strongest
  authorized evidence already found or proceeds without retrieval when safe;
  it does not block indefinitely. **Implemented:**
  `retrieval.deadlineMilliseconds` bounds every retrieval call; an expired
  deadline stops launching further strategy steps and returns whatever
  bounded-worker results already exist, marking the outcome partial rather
  than blocking.
- Context disclosure recording which files/chunks and index generation were
  used, why they ranked, whether results were partial, and what was omitted.
  **Implemented:** every candidate (included and omitted) is recorded with
  its source strategy, relative path, offset, index generation, score,
  inclusion state, and reason; the JSON array is attached to the Phase 13
  `QueryTrace` via the new `QueryCoordinator::record_retrieval` (kept
  independent of the terminal `diagnostic` field so a later successful
  `finish()` cannot erase it) and surfaced through the existing
  authenticated `GET /api/v1/queries/{id}` route as `retrievalDisclosure`.
- Retrieval quality, latency, memory, stale-index, membership-change, and
  authorization-boundary tests. **Implemented:** strategy selection and
  fusion/dedup correctness, live re-read of the current index generation
  with no planner-side caching (an incremental index update is reflected on
  the very next call), a near-zero deadline returning bounded partial
  evidence instead of blocking, and context-budget capping with full
  disclosure of included versus omitted evidence. Membership/policy
  invalidation is structural (the caller re-authorizes the project on every
  chat message via `ProjectCatalog::find` before calling
  `RetrievalPlanner::retrieve`; the planner itself never caches an
  authorization decision) rather than covered by a dedicated end-to-end HTTP
  test, since Phase 15's own test suite similarly exercises
  `WorkloadHttpController` rather than the private `HttpServer` chat
  transport directly. A formal retrieval-quality evaluation set comparing
  against full-text-only retrieval, and a dedicated latency/memory benchmark
  under Phase 21's benchmark framework, remain outstanding.

Installation/completion outcome:

- Browser, API, IDE, and MCP clients receive prompt status quickly and can
  inspect the actual project sources supplied to the model.

Exit criteria:

- The authored retrieval evaluation set demonstrates improvement over
  full-text-only retrieval while meeting the selected interactive deadline and
  hard memory budget.
- Project membership or policy changes invalidate access immediately.

### Phase 17 — Security-partitioned cache hierarchy

Status: Implemented, exit validation pending. `CacheManager`
(`src/cache.cpp`) is wired into `send_chat_message`'s existing retrieval
step, replacing nothing (a cache miss falls through to exactly the Phase 16
`RetrievalPlanner::retrieve` call that already existed): a repeated chat
message against an unchanged index generation and policy generation is
served from the cache instead of rerunning retrieval, and the disclosure
already surfaced through `/api/v1/queries/{id}` (Phase 16) now carries a
truthful `cacheHit` field. Six segments exist (`file_content`,
`parsed_chunk`, `embedding`, `retrieval_result`, `tokenization`, `prompt`)
with independent byte caps, segmented LRU eviction, and hit/miss/eviction/age
metrics; only `retrieval_result` has a real producer today, since no
parsing/embedding/tokenization pipeline exists yet to populate the other
five -- the same "declared but no producer" shape Phase 16 used for
retrieval strategies its own dependencies could not yet serve. Versioned
`CacheKey`s fold in user/project identity, a process-lifetime policy
generation (bumped on user and project creation), canonical
identity/digest, a component-version tag, and index generation, so a file
change, a membership/permission change, or a stale index generation makes
the previous entry unreachable structurally rather than through active
invalidation -- consistent with how `RetrievalPlanner` itself already
re-reads the index generation on every call instead of caching it. Disk
entries are written atomically (temp file, then platform-native atomic
rename) with a checksum verified on every read; a checksum mismatch is
quarantined by deletion and reported as a miss rather than ever served, and
a scan at construction makes disk entries and `status()` immediately
correct after a restart. Authenticated `GET /api/v1/system/cache`,
`POST /api/v1/system/cache/trim`, and `POST /api/v1/system/cache/clear`
(administrator-only, alongside the existing `GET /api/v1/system/memory`)
give visibility and forced eviction without needing to restart the server.
`test_phase_seventeen_security_partitioned_cache` in `test/tests.cpp`
covers repeated-query hit/miss, restart persistence, stale-generation and
policy-generation invalidation, corrupted-checksum quarantine, segmented-LRU
capacity enforcement, and cross-project/cross-user isolation, and the full
suite is Windows Debug/Release validated (2026-07-31). Per-project/per-user
quotas beyond each category's shared byte cap, cache bytes participating in
live `MemoryBudgetManager` pressure-driven trimming (today `trim()` is only
administrator- or caller-invoked, not triggered automatically by a pressure
transition), and a formal representative-query latency benchmark comparing
cached against uncached preparation time remain outstanding -- the first
exit criterion below is not yet demonstrated with recorded evidence.

Purpose:

- Reduce repeated disk, parsing, embedding, tokenization, retrieval, and prompt
  work without allowing stale or cross-boundary reuse.

Dependencies:

- Phase 14 memory budgets, Phase 15 generation identities, and Phase 16
  canonical retrieval results.

Deliverables:

- One cache manager governing byte-bounded file metadata/content, parsed
  chunk, embedding, retrieval-result, tokenization, and prompt registries.
- Segmented LRU or equivalent measured policy with per-cache hard limits,
  per-project/user quotas, hit/miss/eviction/age metrics, and deterministic
  pressure trimming.
- Versioned keys containing every relevant identity: tenant/user/project and
  policy generation where applicable, canonical file identity and digest,
  parser/chunker/embedding/tokenizer versions, model/backend fingerprints,
  prompt-template version, and retrieval/index generation.
- Event-driven invalidation for file changes, index publication, membership or
  permission changes, model/backend/configuration updates, and detected cache
  corruption.
- Atomic disk entries with size/version/checksum validation and quarantine;
  cached authorization decisions never become authoritative.
- Authenticated administrative status, trim, and clear operations that cannot
  break active requests.
- Repeated-query, restart, corruption, pressure, stale-content, and
  cross-project/cross-user isolation tests.

Installation/completion outcome:

- Administrators can see bounded cache use and trim it safely; ordinary users
  see only a truthful cache-reuse indicator for their request.

Exit criteria:

- Repeated representative queries measurably reduce preparation latency with
  stable output quality and no authorization leakage.
- Every cache has a hard byte limit and a verified invalidation path.

### Phase 18 — Runner prompt-prefix and KV/session reuse

Status: Planned.

Purpose:

- Avoid repeated model prompt evaluation where the exact backend supports safe,
  compatible reuse.

Dependencies:

- Phase 17 cache identity/invalidation and Phase 14 KV/session budgets.
- Real pinned-backend/model validation required by Phases 4, 5, and 7.

Deliverables:

- Exact compatibility fingerprints covering model hash, backend binary/build,
  tokenizer, adapter settings, prompt-template/system-policy version, project
  authorization generation, KV types/placement, and context parameters.
- Stable-prefix detection for immutable system/project content and
  backend-supported prompt checkpoints; reuse is disabled on any uncertainty.
- Per-user/project/model quotas, checkpoint counts, idle retention, memory
  ownership, pressure eviction, and cancellation-safe cleanup.
- Scheduler admission that calculates KV memory per sequence multiplied by
  parallel sequences plus safety margin before accepting a cached session.
- Safe alternatives when a request cannot fit: reduced context/concurrency,
  lower-memory supported KV type, smaller model, adjusted CPU/GPU placement,
  or a new uncached session.
- No sharing of prompt or KV state across unauthorized users/projects and no
  persistence of decrypted secrets or unsafe attachment state.
- Backend-unsupported and fingerprint-mismatch fallback tests proving ordinary
  uncached generation remains correct.

Installation/completion outcome:

- Compatible repeated chat turns can reuse verified runner state; unsupported
  backends continue through the existing isolated inference path.

Exit criteria:

- Same-host repeated-turn benchmarks show reduced prompt-evaluation time and
  TTFT without incorrect output, stale policy, memory-cap violation, or
  cross-boundary reuse.

### Phase 19 — Adaptive hardware and model calibration

Status: Implemented, exit validation pending (see the dated validation
evidence entry above for what shipped and what real-hardware-class
measurement remains outstanding).

Purpose:

- Replace generic tuning guesses with measured settings for an exact host,
  model, backend, and power/storage environment.

Dependencies:

- Phases 13–18 metrics and controls.

Deliverables:

- A short first-use calibration measuring cold load, small/medium prompt
  evaluation, short generation, peak memory/commit, page faults, disk reads,
  CPU/GPU utilization, and thermal trend where available.
- A signed/checksummed tuning-profile record keyed by host, model, backend,
  build, settings schema, and storage identity.
- Recommendations for context, generation/batch workers, batch tokens,
  parallel sequences, GPU layers/offload, KV types/placement, cache limits,
  warmup, mapped/resident load policy, and idle unload.
- Storage-aware model policy for NVMe/SATA SSD/HDD/removable/network/
  compressed/encrypted/RAM-backed locations. Network model files remain denied
  by default; virtual memory is treated as a safety margin, not physical RAM.
- User-selectable `auto`, minimal, balanced, and performance profiles with the
  user's hard RAM cap always taking precedence.
- Recalibration triggers for relevant host, driver, backend, model, storage, or
  power-mode changes and a safe-default fallback for stale/corrupt profiles.
- API/UI controls to run calibration, explain recommendations, compare evidence,
  reduce memory use, or optimize for speed.

Installation/completion outcome:

- The installed service selects defensible defaults for the active machine and
  shows the measurement and trade-off behind each recommendation.

Exit criteria:

- On supported hardware classes, the selected profile either outperforms safe
  defaults or reduces peak memory without unacceptable quality regression and
  never exceeds the configured RAM cap.

### Phase 20 — Optional advanced throughput

Status: Planned and gated; no feature in this phase is pre-approved for
implementation merely by appearing in the plan. An `AdvancedOptimizationRegistry`
scaffold (see the dated validation evidence entry above) declares every
candidate feature disabled-by-default with a ready-made evidence schema; no
candidate's actual optimization logic is implemented.

Purpose:

- Evaluate higher-complexity optimizations after the bounded baseline is
  correct, measurable, and safe.

Dependencies:

- All Phase 13–19 exit criteria and representative real-model workloads.

Candidate deliverables, admitted independently only with evidence:

- Weighted-fair multi-priority inference scheduling and continuous batching for
  compatible requests.
- Optional speculative decoding with exact target/draft compatibility,
  separate memory accounting, quality parity checks, and immediate fallback.
- NUMA-aware placement and affinity for measured multi-node hosts.
- Storage-specific prefetch/read-ahead and alternate asynchronous I/O.
- Multiple warm runners only where their measured latency value justifies their
  full memory cost.
- Backend-specific GPU/CPU KV placement, unified/separate KV, direct I/O, or
  model pre-touch policies behind the replaceable adapter boundary.

Installation/completion outcome:

- Only validated features are exposed as advanced profile capabilities; every
  one can be disabled independently and the safe Phase 19 profile remains
  available.

Exit criteria:

- Each admitted optimization has its own baseline, exact setting change,
  hardware/model/backend hashes, TTFT, prompt/generation throughput, peak
  memory, quality, power/thermal notes, regression decision, and fallback test.
- Interactive work remains responsive under background indexing, download,
  cancellation, disconnect, and queue-saturation tests.

### Phase 21 — Native asynchronous storage and prefetch engine

Status: Implemented at a scoped-down level (2026-08-01). `src/async_storage.cpp`
provides a real `IAsyncFileReader` interface, a Windows IOCP/overlapped
`Win32OverlappedFileReader` backend, a POSIX `PosixPreadPoolReader` bounded
worker-pool backend, a measured `StorageLatencyProfile` probe, an adaptive
queue-depth policy, a pure read coalescer, and per-request cancellation
tokens. Wired into `models.cpp` manifest reads and `indexing.cpp` segment
reads as an opt-in path with the pre-existing blocking path retained as an
automatic fallback (`read_file_bytes()`). Deliberately out of scope for this
pass, and left as follow-on work: a Linux `io_uring` adapter (the POSIX
`pread` worker pool is used unconditionally on Linux instead, matching this
plan's own explicitly-allowed fallback), and memory-mapped file regions.
Windows is the validated build/test target per project convention.

Purpose:

- Remove blocking storage operations from model startup, project indexing,
  retrieval, attachment parsing, cache loading, and model verification.

Dependencies:

- Phase 15 disk-backed index generations; Phase 16 retrieval reads them.

Deliverables:

- A replaceable `IAsyncFileReader` interface with a Windows IOCP/overlapped
  `ReadFile` adapter, a Linux `io_uring` adapter with a bounded `pread`
  worker-pool fallback, memory-mapped file regions, and a
  `StorageCapabilityProbe`/`StorageLatencyProfile` pair that classifies the
  underlying device (HDD/SATA SSD/NVMe/network/removable).
- A read coalescer that merges adjacent independent requests into one
  physical read, and an adaptive queue-depth policy keyed to the measured
  storage class (low/sequential-biased on HDD, bounded parallel segments on
  NVMe, denied-by-default for active model weights on network/removable
  storage).
- Cancellable reads: an abandoned request releases its buffer, never
  publishes a partially verified object, and never blocks shutdown.
- Parallel reads are restricted to genuinely independent data (separate
  index segments, model shards, cache objects, project files); one thread
  per file and unbounded speculative read-ahead are explicitly disallowed.

Exit criteria:

- Retrieval latency improves for mapped/uncached indexes with no regression
  to model cold-load time.
- Queue saturation stays bounded; HDD sequential throughput is not degraded
  by SSD-oriented random fan-out.
- Cancellation and shutdown complete safely under load; peak temporary read
  memory stays within the configured limit.

### Phase 22 — Hierarchical content and model-data caching

Status: Implemented at a scoped-down level (2026-08-01) — see the summary
entry above (`src/cache.cpp`, `src/masterai.hpp`) for the full-category
expansion, segmented eviction, admission gate, and negative caching that
shipped. Priority A/B — file-metadata, file-content, and
model-manifest/verification caches land first (Priority A); embedding,
reranking, and MCP-resource caches follow once their producers exist
(Priority B).

Purpose:

- Replace the single general-purpose cache with layered, independently
  bounded caches matched to each object's real cost and lifetime.

Dependencies:

- Phase 17 `CacheManager` identity/invalidation keys and security
  partitioning; Phase 21 async reads for persistent-cache I/O.

Deliverables:

- An L0–L5 layering model (request-local references through OS file cache
  and reconstructable source storage) and independently bounded categories:
  file metadata, file content, parsed document, source-chunk, symbol,
  retrieval-result, reranking, embedding, tokenization, prompt-template,
  prompt-fragment, model-manifest/verification/metadata, download metadata,
  hardware-probe, tuning-profile, MCP-resource, and static web-asset.
- A TinyLFU-style (or equivalent) bounded admission estimator so one large
  scan cannot evict the hot working set, plus segmented eviction
  (probationary/protected/pinned/streaming).
- Immutable, reference-counted, content-addressed cache objects with
  checksums for persistent entries, atomic publication, and corruption
  quarantine (never silent corruption).
- Cache keys that include user/project identity, authorization-policy and
  membership generation, content digest, index generation, and every
  relevant parser/chunker/tokenizer/embedding/generation-model/backend/
  template/settings fingerprint already required by Phase 17/18.
- Short-lived, version-bound negative caching that can never hide newly
  granted access, new content, completed downloads, or security-policy
  changes.
- Compression restricted to cold persistent entries where measured
  decompression cost is lower than the avoided storage I/O; never applied to
  hot token arrays or active prompt fragments.

Exit criteria:

- Representative repeated requests show lower retrieval latency.
- One-time scans cannot evict the entire hot working set.
- Persistent-cache corruption falls back to safe uncached operation.
- Cross-user and cross-project isolation tests pass; cache memory never
  exceeds category or process-wide limits.

### Phase 23 — Tokenization, template, and prompt-fragment caching

Status: Implemented at a scoped-down level (2026-08-01). `RunnerSupervisor::tokenize()`
(src/inference.cpp) now supports an opt-in `CacheManager`-backed tokenization
cache (`set_tokenization_cache()`), keyed by content SHA-256 plus the loaded
model's own verified digest (doubling as the tokenizer/vocabulary
fingerprint) and a special-token policy string, under the already-declared
`CacheCategory::tokenization`; wired in at server construction
(src/server.cpp). New `src/prompt_assembly.cpp` adds a process-lifetime,
mutex-guarded compiled-template cache (`compiled_chat_template()`) keyed by
architecture name, and segmented prompt assembly
(`assemble_chat_prompt_segments()`/`materialize_prompt()`) over Phase 30's
`PromptSegment`/`BufferView`/`SharedBuffer` types, replacing
`assemble_chat_prompt()`'s repeated `std::string +=` concatenation while
remaining byte-for-byte identical to the old output (see
`test_phase_twentythree_segmented_assembly_byte_identical`). A restricted
`intern_identifier()` table caps interned entries at 128 bytes, throwing
rather than silently caching longer (i.e. plausibly arbitrary
user/file-content) strings. `PromptSessionManager::try_reuse()`
(src/session_cache.cpp) now returns `SessionDecision` with
`reusable_prefix_bytes`, `divergence_offset`, an explicit
`SessionInvalidationReason` enum, and the configured
`prefix_byte_ceiling` (default 4 MiB, constructor-configurable), still using
exact-byte-prefix matching only (no fuzzy matching). Scope trim: reported
prefix size is a byte count, not a token count -- PromptSessionManager
compares raw prompt strings without tokenizer access, and adding one would
reintroduce the per-turn runner round trip Phase 18 exists to avoid; a
caller wanting an actual token count can pass the reused byte range through
the now-cached `tokenize()` itself.

Purpose:

- Remove repeated CPU work performed before inference begins.

Dependencies:

- Phase 18 `PromptSessionManager` prefix/session reuse; Phase 22 cache
  admission and key requirements.

Deliverables:

- A tokenization cache for immutable prompt fragments (system instructions,
  chat wrappers, project policies, static tool descriptions, repeated file
  chunks/conversation prefixes) keyed by content hash plus tokenizer/model
  vocabulary fingerprint and special-token policy.
- Compiled, cached chat-template execution plans (parse/validate once,
  apply variables without reparsing).
- Segmented prompt assembly (`PromptSegment` views into shared buffers)
  instead of repeated large-string concatenation; a contiguous buffer is
  produced only when the backend requires one.
- Interning restricted to high-repetition immutable strings (role names,
  model IDs, route names, repeated JSON keys) — never arbitrary user
  messages or whole source files.
- Extension of Phase 18's prefix reuse with exact reusable token count,
  divergence location, explicit invalidation reason, and a maximum retained
  prefix byte ceiling. No fuzzy prefix matching for KV reuse.

Exit criteria:

- Repeated prompts show lower prompt-preparation time.
- Tokenization cache entries never cross tokenizer or model boundaries.
- Cached prompt output remains byte-for-byte equivalent to uncached
  assembly; edited earlier messages correctly invalidate the affected
  prefix.
- Peak prompt-assembly memory decreases against the Phase 18 baseline.

### Phase 24 — Advanced retrieval fan-out and adaptive query planning

Status: Implemented at a scoped-down level (2026-08-01). `src/retrieval.cpp`
adds a `RetrievalStrategy` enum covering every strategy the spec lists;
`filename_path` (via each chunk's already-tracked `relative_path`, see the
new `ProjectIndexer::search_path`/`ProjectIndexService::search_path`) and
`recent_change` (a post-fusion score boost from each candidate's real
filesystem mtime, no git integration) are real, working adapters alongside
Phase 16's exact_symbol/exact_text/lexical; every strategy without a
same-repo adapter (semantic_embedding, mcp_resource, call_graph,
type_reference, git_diff, dependency_neighbour, conversation_memory) is
declared but disabled, and `retrieve()` stamps a "no adapter" reason for
every one of them onto its `RetrievalOutcome`, pushed onto `QueryTrace` via
new `QueryCoordinator::record_retrieval_strategy_skips()`. A deterministic
keyword/shape classifier (`classify_retrieval_request()`, no ML) maps a
query to completion/symbol_explanation/navigation/documentation/
generic_lexical and is likewise recorded on `QueryTrace`
(`record_classification()`). `RetrievalPlanner::retrieve()` now runs an
explicit staged list (symbol/exact-text first, filename/path second,
lexical last) with sticky sufficiency, reusing `DeadlineTaskPool` with an
interactive/background worker-count parametrization rather than a second
pool implementation. A request-key → `std::shared_future` join table
(guarded by a mutex, keyed on project + requester identity + policy
generation + settings) joins genuinely concurrent identical requests;
`HttpServer` now holds one long-lived `RetrievalPlanner` instead of one per
request so this is reachable in practice. `RetrievalCandidate` adopts
Phase 30's `ChunkReference` (offset+length into a per-call shared arena
buffer built during fusion) instead of a per-candidate full-text copy;
`ContextBudgeter::apply()` calls `.materialize()` exactly once per admitted
candidate. Scope trims, stated honestly: `IndexChunk`'s own on-disk/
in-memory text representation in `indexing.cpp` is unchanged (it is
serialized to disk and consumed well beyond retrieval, so reference-first
adoption is scoped to `RetrievalCandidate`, the type Phase 16 already
introduced for retrieval's own ranking/budgeting path); "open-file" boost
collapses into the recent-change mtime boost since no editor/LSP session
exists in this codebase to know what is actually open; the request
classifier's category set is the reduced one the plan itself allows
("meaningful given existing strategies") rather than the full ten-category
list, since diagnosis/review/architecture/historical-conversation/
model-only/MCP-resource have no corresponding strategy to route to yet.
`test/tests.cpp` adds `test_phase_twentyfour_*` covering classification
shape rules, disabled-strategy skip reasons reaching `QueryTrace`, staged
fan-out skipping later stages once an earlier one is sufficient, duplicate
evidence fusing into one materialized candidate, concurrent identical
requests joining into exactly one in-flight computation
(`RetrievalPlanner::uncached_invocation_count()`, a diagnostic-only counter
mirroring `SharedBuffer::use_count()`'s convention), and mismatched-identity
concurrent requests provably NOT joining. Not attempted: an authored
retrieval evaluation set with measured latency improvement over a Phase 16
baseline (needs a benchmark corpus this session does not have) and the
still-outstanding Phase 16 evaluation-set exit criterion it was meant to
also satisfy.

Purpose:

- Retrieve the strongest useful evidence faster without loading whole
  projects into memory, extending the Phase 16 `RetrievalPlanner`.

Dependencies:

- Phase 16 strategy set and deadline-bound execution; Phase 22 shared cache
  keys for in-flight de-duplication.

Deliverables:

- Additional independently selectable strategies beyond Phase 16's
  exact-identifier/exact-phrase/lexical set: filename/path, open-file,
  recent-change, diagnostic/build-log, dependency-neighbour, call-graph,
  type-reference, git-diff, conversation-memory, semantic-embedding, and
  authorized MCP-resource lookup (each gated on its own adapter existing).
- A request classifier (completion, symbol explanation, diagnosis, review,
  architecture question, navigation, documentation, historical-conversation,
  model-only, MCP-resource) that the planner uses to skip expensive
  strategies when they are not required.
- Staged fan-out groups (exact/cheap first, lexical/diagnostics second,
  semantic/dependency/conversation last) with sticky sufficiency — later
  stages run only when confidence is below threshold, evidence conflicts,
  required source types are missing, or the user requested exhaustive
  analysis.
- A small bounded retrieval worker pool with interactive/background/
  maintenance priority classes, and shared in-flight futures so concurrent
  compatible requests join one retrieval operation instead of duplicating it
  (joining only when authorization, project, policy generation, and
  settings match).
- Reference-first result representation (chunk identity, offset, length,
  score, strategy, generation, digest); full text materializes only for
  candidates admitted by the Phase 16 `ContextBudgeter`.

Exit criteria:

- Median and high-percentile retrieval latency improve over Phase 16 alone.
- Expensive semantic retrieval is skipped when exact matches already
  suffice; no unbounded fan-out occurs.
- Retrieval remains authorized and fully disclosed on the `QueryTrace`.
- Duplicate chunks are materialized once even under concurrent joins.
- An authored retrieval evaluation set demonstrates measured improvement
  over the Phase 16 baseline (this also satisfies the still-outstanding
  Phase 16 evaluation-set exit criterion).

### Phase 25 — Continuous inference batching and request scheduling

Status: Implemented at a scoped-down level (2026-08-01) — see the summary
entry above (`src/scheduler.cpp`, `RequestScheduler`) for the weighted-fair
scheduling and backpressure that shipped, and why continuous batching of
live backend generation steps did not. Priority C — depends on multiple
concurrent compatible requests being common enough to benefit measurably;
superset of the Phase 20 continuous-batching candidate.

Purpose:

- Increase throughput when multiple compatible inference requests are
  active, without harming single-request interactive latency.

Dependencies:

- Phase 14 bounded admission/priority work; Phase 20's disabled-by-default
  `AdvancedOptimizationRegistry` entry for continuous batching.

Deliverables:

- A weighted-fair priority scheduler (cancellation/shutdown, IDE completion,
  interactive chat, interactive analysis, user background jobs, benchmarks,
  indexing/embeddings, maintenance), each class with weight, max queue
  depth/residence time, concurrency allowance, memory allowance, and
  cancellation policy.
- Continuous batching of compatible token-generation steps (same model,
  backend, context configuration, sampling implementation, available KV
  slots/compute memory, no exclusive low-latency requirement), gated by a
  calibrated per-class maximum batch-collection delay.
- Backpressure that rejects low-priority background work first, reserves
  memory before admission, reports queue status to clients, and allows
  cancellation while queued.

Exit criteria:

- Compatible concurrent throughput improves; single-request TTFT does not
  exceed the configured regression limit.
- Queue fairness prevents starvation; memory use remains predictable.
- Cancellation removes queued work immediately.

### Phase 26 — Model loading, mapping, pre-touch, and warm-state management

Status: Implemented at a scoped-down level (2026-08-01). `masterai.hpp`/
`calibration.cpp` add an explicit `ModelLoadMode` (streamed/mapped/resident/
auto) selected by `CalibrationService::resolve()` from a Phase 21
`StorageLatencyProfile` plus available-RAM evidence via `select_load_mode()`,
realized through the existing `--no-mmap`/`--mlock` launch arguments rather
than new backend flags. A `PreTouchLevel` enum (none/metadata/first-use/
layer-window/full) is accepted policy end-to-end, but only none/full are
backend-actionable today (via the same `--mlock`); `pretouch_gap_reason()`
documents that gap explicitly rather than faking finer granularity.
`inference.cpp` adds a `WarmModelState` state machine (Cold/LoadingMetadata/
MappingWeights/InitialisingBackend/Warming/Ready/Busy/Idle/Draining/
Evicting/Unloaded/Failed) layered onto `RunnerSupervisor`'s existing
`RunnerState`/`ModelState` via `WarmModelTracker`, an explicit legal-
transition graph, and a `translate_runner_state()` baseline table so every
pre-existing `RunnerState` consumer is unaffected (see the Phase 26
regression test). `run_cancellable_warmup()` provides cooperative-
cancellation background warm-up that yields on `MemoryBudgetManager`
pressure or a `WarmupCancellationToken`. `ModelUsagePredictor` records
recency/pin/project-preference/waiting-request signals, reported via
`model_usage_signals_json()` following the existing `tuning_profile_json()`
convention. Deliberately out of scope for this pass: `direct` I/O (no
validated backend support), and full CLI/HTTP route wiring for the
use-prediction reporting surface (the recording/reporting API itself is
implemented and tested, but not yet exposed through a dedicated admin route
or CLI command). Windows is the validated build/test target per project
convention.

Purpose:

- Reduce large-GGUF-model startup delay while avoiding destructive paging,
  extending Phase 19 calibration with explicit load-mode control.

Dependencies:

- Phase 3 manifest/integrity verification; Phase 14 hard RAM ceiling;
  Phase 19 `CalibrationService`/`LaunchTuning`; Phase 21 async storage.

Deliverables:

- Explicit load modes (streamed, mapped, resident, direct where validated,
  auto) selected from measured storage and memory evidence; read-only
  memory mapping for compatible GGUF backends with reported resident/
  committed/mapped bytes and rejection of models predicted to cause
  sustained destructive paging.
- Selective pre-touch (none/metadata/first-use/layer-window/full) instead of
  unconditionally touching every model page; full pre-touch remains
  disabled by default on constrained hosts.
- Cancellable, low-priority background warm-up that yields to interactive
  work, low free RAM, thermal pressure, rising storage latency, or
  shutdown.
- A warm-model state machine (Cold/LoadingMetadata/MappingWeights/
  InitialisingBackend/Warming/Ready/Busy/Idle/Draining/Evicting/Unloaded/
  Failed) and bounded, administrator-inspectable use-prediction signals
  (recency, pinning, project preference, waiting-request count) — no opaque
  model may permanently block unloading.

Exit criteria:

- Cold-load and warm-load times are measured separately and both improve or
  hold versus the Phase 19 baseline.
- Selective warm-up improves TTFT where enabled; full pre-touch stays
  disabled on constrained hosts.
- Idle models unload predictably; load cancellation releases all resources.

### Phase 27 — KV-cache compression, placement, and lifecycle management

Status: Implemented at a scoped-down level (2026-08-01) — see the summary
entry above (`src/kv_cache.cpp`, `KvCacheManager`) for the accounting,
bounded reservation, and deterministic eviction that shipped, and why
reduced-precision KV and prefix-tree sharing stay declared-but-disabled.
Priority B for accounting and placement modes (delivered); Priority C
for reduced-precision KV and prefix-tree sharing given their quality-parity
and cross-boundary-isolation risk (still gated).

Purpose:

- Reduce the largest context-dependent memory consumer while preserving
  correctness and isolation.

Dependencies:

- Phase 14 bounded memory; Phase 18 `PromptSessionManager` session/KV reuse;
  Phase 27 requires backend-validated support before any precision change
  is admitted.

Deliverables:

- Per-model/runner/slot/layer KV accounting by context length, token count,
  data type, CPU/GPU placement, and owning user/chat/project.
- Backend-validated reduced-precision KV policies (half/quantized K or V,
  mixed policy) admitted only after quality and stability testing; CPU/GPU/
  split placement modes recorded in the session fingerprint.
- Context-aware reservation with bounded growth steps and a hard maximum,
  used only where the backend supports safe growth (never optimistic
  allocation against a backend that preallocates full slot capacity).
- An optional prefix tree for compatible immutable prefixes (public system
  template, identical administrator-approved policy for same-authorization
  users) that never exposes private conversation KV state across
  unauthorized boundaries; private prefixes stay chat-bound.
- Deterministic eviction order: failed/cancelled slots, expired idle
  prefixes, lowest-reuse private slots, large low-value reusable prefixes,
  idle non-pinned sessions, reduced new-request context, then safe
  rejection.

Exit criteria:

- KV usage is visible per runner and slot.
- Reduced-precision KV passes quality-parity requirements before enablement.
- No reuse crosses identity or policy boundaries; eviction cannot detach an
  active slot.
- Context growth never exceeds the hard memory ceiling.

### Phase 28 — NUMA, processor-group, and topology-aware execution

Status: Implemented at a scoped-down level (2026-08-01) — see the summary
entry above (`src/topology.cpp`, `probe_hardware_topology()`/
`recommend_thread_placement()`) for the real topology probing and pure
placement-recommendation logic that shipped, and why it is not yet applied
to any real worker thread. Priority C; superset of the Phase 20 NUMA
candidate, and only relevant on multi-socket/high-core-count hosts.

Purpose:

- Improve performance on multi-node and processor-group systems without
  regressing single-node hosts.

Dependencies:

- Phase 20 disabled-by-default NUMA-affinity candidate; Phase 36 benchmark
  matrix to prove per-host benefit.

Deliverables:

- Hardware topology probing (packages, NUMA nodes, physical/logical cores,
  efficiency/performance core classes, Windows processor groups, cache
  hierarchy, GPU locality where discoverable).
- Thread-class separation (inference compute, HTTP/streaming, retrieval,
  indexing, storage completion, download, background maintenance) with
  affinity applied only where measurement shows benefit — not pinned by
  default.
- NUMA-local placement of model memory and inference threads on multi-node
  hosts, administrator-disableable, with automatic fallback to normal OS
  scheduling on single-node systems.
- Hybrid-core policy preferring latency-sensitive work on performance cores
  and indexing/downloads/maintenance on efficiency cores, re-evaluated on
  battery power.

Exit criteria:

- NUMA policy improves measured throughput or latency on applicable hosts
  with no regression on single-node hosts.
- Affinity can be disabled without restart where practical; Windows
  processor-group handling uses all authorized cores correctly.

### Phase 29 — Model tiering, routing, and cascade inference

Status: Implemented at a scoped-down level (2026-08-01) — see the summary
entry above (`src/model_routing.cpp`, `ModelRouter`) for the tier-selection,
cascade-escalation, and resident-profile-validation logic that shipped, and
why live chat-pipeline wiring did not. Priority B.

Purpose:

- Avoid using the largest resident model for every request.

Dependencies:

- Phase 3 model registry/manifest; Phase 19 calibration profiles; Phase 26
  warm-state management for controlling how many tiers stay resident.

Deliverables:

- Explicit model tiers (deterministic non-model processing, compact
  router/classifier, small fast model, medium general model, large
  specialist) and disclosed routing signals (task category, language,
  context size, requested quality, latency requirement, capability,
  available RAM/VRAM, queue depth, benchmark evidence, user pin).
- Cascade processing that only escalates to a larger tier on low
  confidence, unsupported syntax, conflicting retrieval evidence, failed
  deterministic validation, security sensitivity, explicit user request, or
  context that cannot fit safely.
- Resident-model profiles (Minimal: one model; Balanced: one generation
  model plus optional compact embedding/router model; Performance: multiple
  warm models only with memory and benchmark evidence) so tiered routing
  cannot leave several full models resident on constrained hosts.

Exit criteria:

- Simple requests complete faster; large-model invocation frequency
  decreases.
- Quality regression stays below the configured threshold; routing
  decisions are disclosed and user-overridable.

### Phase 30 — Memory deduplication and immutable shared-data architecture

Status: Implemented at a scoped-down level (2026-08-01). `src/shared_buffer.cpp`
and `src/masterai.hpp` provide the full foundational type family
(`SharedBuffer`, `BufferView`, `MappedBufferView` with a Win32 file-mapping/
POSIX mmap backend, `ChunkReference`, `TokenSpan`, `PromptSegment`,
`RequestArena` with debug-build generation-counter poisoning, and
`FixedSizePool<T>`), already adopted by Phase 24's `RetrievalCandidate`/
`IndexChunk` (`reference` addresses a per-call shared arena instead of
carrying its own text copy) and Phase 23's segmented prompt assembly. This
pass closes the two gaps that were still open: (1) `FixedSizePool`'s
`bytes_reserved()` is now live-registered against `MemoryBudgetManager`
(`masterai.hpp`'s `MemoryCategory`) via a new `on_reserved_bytes_changed`
growth/shrink hook on `FixedSizePool` and a generic `BudgetTrackedPool<T>`
wrapper, wired to one real consumer: `RetrievalPlanner`'s (`retrieval.cpp`)
fusion-candidate pool, which now pool-allocates `RetrievalCandidate` objects
during `retrieve_uncached()` instead of storing them directly as
`std::map` values, and reserves/releases a call-scoped lease against
`MemoryCategory::retrieval_index_cache` on every real pool growth/shrink
(best-effort accounting, not admission control -- `RetrievalPlanner`'s
`MemoryBudgetManager*` is optional and defaults to `nullptr` so every
pre-Phase-30 call site keeps working unchanged). (2) the streaming
copy chain in `server.cpp`'s `send_chat_message()` token path is reworked:
`json_escape_bytes()` (`http_support.cpp`, sharing its escaping rules with
`json_escape()` via one internal template) escapes directly into a
`std::vector<std::uint8_t>` that moves (no copy) into a `SharedBuffer`, and
`send_chunk_parts()` writes the JSON envelope prefix, the `BufferView` over
that buffer, and the suffix straight to the socket via separate `send_all()`
calls instead of concatenating them (and the outer chunked-encoding framing)
into one throwaway `std::string` first -- removing the three full-payload
copies the pre-Phase-30 code paid on every streamed token, while bounded
partial-output retention (`streamed_text`, persisted on both success and
failure) is untouched. Deliberately out of scope for this pass: a
full request-scoped-arena integration across the whole retrieval/prompt/
JSON-parse path (the plan's `RequestArena` primitive exists and is tested,
but only the one `FixedSizePool` consumer above is wired end-to-end this
session); the upstream runner-line -> JSON-tree -> `generated.text`/
`streamed_text` accumulation copies remain (required for parsing safety and
durable accumulation) -- zero-copy streaming reduces, but does not
eliminate, every copy in the identified chain. Windows is the validated
build/test target per project convention.

Purpose:

- Eliminate unnecessary copies across retrieval, prompt assembly, caching,
  and streaming.

Dependencies:

- Phase 16 chunk-reference results; Phase 23 segmented prompt assembly;
  Phase 22 cache object representation.

Deliverables:

- Immutable shared buffers and views (`SharedBuffer`, `BufferView`,
  `MappedBufferView`, `ChunkReference`, `TokenSpan`, `PromptSegment`) holding
  only an owner reference, offset, length, and optional encoding metadata.
- Copy-on-write restricted to large, mostly immutable, clearly controlled
  data — never for credentials, mutable network buffers, audit records, or
  data crossing a process trust boundary.
- Request-scoped arenas (bounded byte size, defined allocation-failure
  handling, no references surviving the arena, debug poisoning in test
  builds) for retrieval-candidate metadata, ranking scratch, prompt-segment
  descriptors, and JSON parsing scratch.
- Fixed-size pools for frequently allocated small objects (queue nodes,
  request-state records, retrieval-result descriptors, token-stream chunks)
  that shrink or release reserved blocks under memory pressure.
- Zero-copy streaming from the runner IPC buffer through to the socket
  send, avoiding the runner-buffer → temporary-string → response-string →
  JSON-string → network-string copy chain; any retained partial output
  stays bounded and persisted incrementally.

Exit criteria:

- Peak memory during large-context requests decreases.
- Prompt assembly performs fewer full-buffer copies.
- No dangling views or use-after-free defects occur under sanitizer/debug
  testing; pool memory is included in the central memory budget.

### Phase 31 — Storage tiering, virtual drives, and scratch-volume management

Status: Planned. Priority A/B — the `ScratchVolumeManager` and storage-aware
placement recommendations are Priority A; full tier migration tooling is
Priority B.

Purpose:

- Use each storage type for the workload it actually handles best, and keep
  ephemeral scratch data from competing with model/KV memory or filling the
  system drive.

Dependencies:

- Phase 21 storage capability probing; Phase 14 hard RAM ceiling (RAM-backed
  scratch competes directly with model weights, KV cache, and OS file
  cache).

Deliverables:

- Storage tiers (A: fast local NVMe for active models/indexes/hot cache/
  runner scratch; B: local SATA SSD; C: local HDD for archives/backups; D:
  removable/network, import-export only by default; R: RAM-backed, small
  reconstructable temporary artifacts only).
- A `ScratchVolumeManager` with per-job directory and byte quota, a global
  byte quota, preferred-tier selection, atomic publication, shutdown
  cleanup, a crash-recovery journal, orphan cleanup, and a free-space
  reserve.
- A hard prohibition on placing full GGUF models, durable chats, audit
  records, user databases, resumable downloads, backups, security records,
  or the only copy of an index generation on RAM-backed storage.
- Separate reporting of physical/available RAM, committed virtual memory,
  commit limit, pagefile/swap usage, hard page-fault rate, and model
  mapped/resident bytes; a model is rejected or downgraded when projected
  active pages exceed safe physical capacity even if commit capacity
  remains.
- Detection (not assumption) of filesystem compression, encryption,
  deduplication, virtual disks, or network redirection under active model/
  index storage, calibrated by measurement.

Exit criteria:

- Scratch files cannot fill the system drive.
- RAM-drive use is included in physical-memory accounting.
- Model placement recommendations reflect measured storage, not assumption.
- Durable data is never silently redirected to ephemeral storage; storage
  migration preserves integrity and atomicity.

### Phase 32 — Speculative decoding and draft-model acceleration

Status: Planned. Priority C — deliberately deferred behind the Priority A/B
phases above; enabled only per-request after backend/tokenizer/template
compatibility and quality-parity checks pass.

Purpose:

- Increase token-generation throughput using a smaller draft model whose
  proposed tokens the target model verifies.

Dependencies:

- Phase 26 warm-state management for the draft runner; Phase 27 KV
  accounting for the combined memory cost; Phase 20's disabled-by-default
  speculative-decoding candidate.

Deliverables:

- Enablement gated on explicit backend support, exact draft/target
  tokenizer and chat-template compatibility, exact vocabulary mapping,
  combined memory fit, equivalent output quality, immediate fallback, and
  cross-model cancellation.
- Draft-model selection by target family, tokenizer identity, architecture
  compatibility, measured acceptance rate, additional memory cost, and
  relative draft/target speed.
- Dynamic per-request disablement on low acceptance rate, overhead exceeding
  savings, short requests, memory pressure, queued draft runner,
  incompatible sampling settings, or thermal throttling.

Exit criteria:

- Generation throughput improves on representative prompts.
- Output remains identical in deterministic parity tests where required.
- Combined memory is fully accounted; poor acceptance falls back
  automatically; single-model operation remains available at all times.

### Phase 33 — Distributed local runners and multi-device orchestration

Status: Planned. Priority C — deliberately deferred behind the Priority A/B
phases above; local single-runner mode remains the default and is never
required to change.

Purpose:

- Allow one control plane to use multiple local runner processes, or
  optionally approved intranet worker machines, without mixing security
  authority.

Dependencies:

- Phase 2 authorization model (worker authorization stays project-bound);
  Phase 29 device-aware routing signals; Phase 9's existing outbound
  registry pattern for pinned, verified external processes.

Deliverables:

- Local multi-runner configurations (per-GPU runner, CPU+GPU split,
  dedicated embedding/router/benchmark runners) routed by resident model,
  available VRAM/RAM, queue depth, expected TTFT, capability, power/thermal
  state, priority, and authorization.
- Optional intranet worker nodes using mutual TLS, pinned/approved private
  PKI, signed worker registration, model-digest verification, explicit
  per-project authorization, encrypted transport, request-size limits,
  cancellation, audit correlation — no shared user passwords, no direct
  unrestricted filesystem access.
- Compact retrieval-context transfer to runners instead of whole project
  files; worker identity recorded on the query trace.

Exit criteria:

- Runner failure does not crash the control plane; retries occur only when
  semantically safe and never duplicate a persisted response.
- Worker authorization remains project-bound; local single-runner mode
  remains fully functional as the default.

### Phase 34 — Adaptive performance controller

Status: Planned. Priority B, gated on Phase 21–31 producing real measured
evidence to tune against — an empty or synthetic evidence base must not
drive automatic changes.

Purpose:

- Automatically select safe operating parameters from real-time conditions
  and stored calibration evidence, extending Phase 19's calibration service
  from advisory profiles to a live bounded controller.

Dependencies:

- Phase 13 query-trace/resource instrumentation; Phase 14 pressure actions;
  Phase 19 `CalibrationService`/`TuningProfileStore`.

Deliverables:

- Live inputs (RAM/commit/page-fault/CPU/GPU/disk/queue-depth/TTFT/
  throughput/cache-hit/thermal/power/active-user signals) feeding bounded
  adjustments to retrieval/index worker counts, read queue depth, prefetch
  distance, cache quotas, retrieval chunk/context targets, inference
  concurrency and batch size, idle-unload time, warm-up policy, thread
  count, NUMA policy, GPU offload, KV placement, and background-job rate —
  every output constrained to administrator-configured ceilings.
- Stability controls (minimum dwell time, hysteresis, bounded step size,
  cooldown, rolling measurement, confidence requirement, safe rollback,
  max changes per interval) to prevent oscillation.
- Named performance modes (Minimal Memory, Balanced, Lowest Latency,
  Maximum Throughput, Battery Saver, Quiet/Thermal Conservative,
  Administrator Custom, Automatic).

Exit criteria:

- Automatic tuning never exceeds administrator ceilings; oscillation tests
  remain stable.
- Every settings change is auditable; failed recommendations revert to the
  last safe profile; manual mode fully disables automatic changes.

### Phase 35 — Performance administration interfaces

Status: Planned.

Purpose:

- Give administrators full visibility and manual override over every
  optimization from Phase 21–34, extending the existing Phase 19/20
  performance routes into a dedicated sidebar.

Dependencies:

- Phase 13 metrics routes; Phase 19/20 performance API surface; Phase 22–31
  producing the memory/cache/storage/worker-pool data to display.

Deliverables:

- A Performance sidebar (Overview, Live Requests, Models and Runners,
  Memory, Caches, Retrieval, Storage, Worker Pools, Scheduling, Calibration,
  Advanced Optimizations, Benchmarks, Regression History, Recommendations)
  behind existing administrator authorization.
- Per-category memory, cache, storage, and worker-pool detail views with
  explicit actions (trim/clear/disable/rebuild cache; unload idle model;
  reduce context/runner slots; change profile).
- An advanced-optimization view showing, per feature: support status,
  enabled state, required backend capability, memory cost, measured
  benefit/regression, hardware/model/backend fingerprint, last validation
  date, and fallback status — never a single opaque "turbo" toggle.

Exit criteria:

- Every figure shown is backed by the same instrumentation used for
  automated benchmarking, not a separately maintained display-only value.
- Every destructive or resource-reducing action is authorized, audited, and
  reversible or clearly explained.

### Phase 36 — Full performance certification and regression gates

Status: Planned.

Purpose:

- Prevent a performance-oriented change from shipping without evidence that
  it helps its intended metric and does not silently regress another.

Dependencies:

- Phase 12/19 existing baseline/calibration measurement; Phase 21–34 as the
  features under test.

Deliverables:

- A benchmark matrix spanning cold/warm OS cache, cold/warm runner, cache
  hit/miss, KV-prefix reuse/no-reuse, context sizes from 512 tokens to the
  safe host limit, single/multiple compatible/incompatible concurrent
  requests, active indexing/download, cache/memory pressure, cancellation,
  disconnect, queue saturation, HDD/SATA SSD/NVMe, CPU-only/GPU-offloaded,
  and each resident profile.
- Recorded metrics per run: cold/warm load time, TTFT, queue wait,
  retrieval/prompt-assembly/tokenization latency, prompt and generation
  throughput, total request time, peak resident/commit memory, mapped
  bytes, hard page faults, storage bytes read/operation count, cache hit
  rate, CPU/GPU utilization, power/thermal notes, output quality,
  cancellation and shutdown latency.
- Per-optimization regression thresholds (max TTFT regression, max memory
  increase, min throughput benefit, max quality regression, max queue-wait
  increase, max storage amplification, max CPU increase, required fallback
  result) and automated current-vs-previous-accepted-build comparison using
  matched host/model/backend/settings/prompt-suite/index-generation/power
  fingerprints — mismatched environments are never presented as a direct
  comparison.

Exit criteria:

- A release is rejected when any memory ceiling is exceeded, cache
  isolation fails, retrieval authorization fails, quality regression
  exceeds its threshold, cancellation becomes unreliable, background work
  starves interactive requests, model startup regresses without justified
  benefit, page-fault rates indicate destructive paging, an advanced
  optimization's fallback fails, or benchmark identity is incomplete.

## Machine Learning Abilities

Implementation status: Phase 37 (see the phase list above) implements the
Dashboard interface below at a foundation level — real, zero-valued counts
and an honest `available`/`planned` tag on every one of the 25 interfaces
in section 2. Phase 38 implements Projects (section 5) at a scoped-down
level — identity, intent, subject/task classification, and lifecycle
status only. Phase 39 implements the Model Registry (section 7) and
Dataset Manager (section 10), both scoped down the same way — identity,
provenance, and lifecycle/approval status only, not the full field list
(evaluation results, safety assessment, hardware/runtime requirements,
model hash/signature, dataset schema, quality score, versioning) that
later training/evaluation/ingestion phases will attach to a registry
entry or dataset once they exist. The dashboard's models-training,
models-awaiting-evaluation, and deployed-models counts are now real,
drawn from the Model Registry's own state counts. Phase 40 implements the
Subject Knowledge Manager (section 12) at the same scoped-down level —
identity, scope, ownership, and review status only, not the full field
list (approved terminology, definitions, concepts, rules, procedures,
examples, counterexamples, reference documents, FAQ, required reasoning
patterns, prohibited conclusions, known limitations, evaluation questions,
source citations, update schedule) that the later Knowledge Ingestion
Pipeline (section 13) will attach to a subject package once it exists.
Every other capability in this section (Model Builder, training,
fine-tuning, deployment, and everything through section 51) remains
`Planned`: no implementation has started.

This section extends the plan with an administrator-only Machine Learning
administration and model-development module, covering the full lifecycle
from dataset ingestion through training, evaluation, deployment, and
governance. It follows the same authorization, isolation, versioning,
audit, and evidence-based-optimization principles established elsewhere
in this plan and does not relax any existing security or resource
control.


### 1. Purpose

Extend the Master AI system with an administrator-only **Machine Learning** module that allows authorized administrators to:

* Create, import, configure, train, fine-tune, evaluate, version, deploy, and retire machine-learning models.
* Teach models specialized subjects using approved datasets and knowledge sources.
* Build models for classification, prediction, generation, search, recommendation, anomaly detection, computer vision, audio processing, and other supported workloads.
* Manage the complete machine-learning lifecycle from a unified administration interface.
* Use local hardware, local model servers, remote compute nodes, or approved external providers.
* Publish trained models to the Master AI inference system for use by web clients, desktop clients, local applications, MCP clients, development tools, and internal services.
* Maintain strong security, auditability, reproducibility, and administrative control.

This module must be available only when the authenticated user has the required administrator permissions.


### 2. Sidebar Integration

Add a primary sidebar option named:

**Machine Learning**

The Machine Learning option should expand into the following administrator interfaces:

1. Dashboard
2. Projects
3. Model Registry
4. Model Builder
5. Dataset Manager
6. Subject Knowledge Manager
7. Data Labeling
8. Data Preparation
9. Training Jobs
10. Fine-Tuning
11. Evaluation Lab
12. Experiment Tracking
13. Prompt and Instruction Training
14. Embeddings and Vector Stores
15. Retrieval-Augmented Generation
16. Synthetic Data
17. Model Comparison
18. Deployment Manager
19. Inference Endpoints
20. Hardware and Compute
21. Automation Pipelines
22. Safety and Governance
23. Monitoring and Diagnostics
24. Audit Logs
25. Machine Learning Settings

Each interface must respect role-based access control and only display operations the current administrator is authorized to perform.


### 3. Administrator Permissions

Create granular machine-learning permissions rather than relying on one unrestricted administrator role.

Recommended permissions include:

* `ml.dashboard.view`
* `ml.projects.create`
* `ml.projects.edit`
* `ml.projects.delete`
* `ml.datasets.view`
* `ml.datasets.import`
* `ml.datasets.edit`
* `ml.datasets.delete`
* `ml.datasets.approve`
* `ml.datasets.export`
* `ml.labels.manage`
* `ml.models.view`
* `ml.models.import`
* `ml.models.create`
* `ml.models.train`
* `ml.models.finetune`
* `ml.models.evaluate`
* `ml.models.approve`
* `ml.models.deploy`
* `ml.models.rollback`
* `ml.models.delete`
* `ml.compute.manage`
* `ml.endpoints.manage`
* `ml.vectorstores.manage`
* `ml.pipelines.manage`
* `ml.safety.manage`
* `ml.audit.view`
* `ml.settings.manage`

Administrative roles may include:

* Master Administrator
* Machine Learning Administrator
* Data Administrator
* Model Trainer
* Model Evaluator
* Deployment Administrator
* Safety Reviewer
* Read-Only Auditor

Critical operations such as production deployment, dataset deletion, model deletion, safety-policy changes, and external publication should support dual approval.


### 4. Machine Learning Dashboard

The Machine Learning dashboard should provide an overview of the entire model-development environment.

Display:

* Active machine-learning projects
* Models currently training
* Models waiting for evaluation
* Models awaiting administrative approval
* Deployed production models
* Failed training jobs
* GPU, CPU, memory, storage, and network utilisation
* Dataset storage consumption
* Model storage consumption
* Inference request volume
* Inference latency
* Model error rates
* Safety-filter activity
* Recent administrative actions
* Alerts and recommendations

Dashboard cards should provide direct navigation to the corresponding project, model, dataset, training job, or deployment.


### 5. Machine Learning Projects

A project is the main organizational container for a model-development objective.

Each project should include:

* Project name
* Description
* Business or technical objective
* Subject domain
* Model task
* Project owner
* Assigned administrators and contributors
* Security classification
* Approved data sources
* Target model architecture
* Target deployment environment
* Success criteria
* Evaluation requirements
* Safety requirements
* Storage allocation
* Compute allocation
* Project status
* Creation and modification history

Example project types:

* Programming assistant
* C++ code analysis model
* Delphi development assistant
* Game-engine documentation assistant
* Accounting assistant
* Business forecasting model
* Customer-support classifier
* Image-recognition model
* Document summarization model
* Internal search assistant
* Security-event anomaly detector
* Speech transcription model
* Recommendation model

Project statuses should include:

* Draft
* Data Collection
* Data Preparation
* Ready for Training
* Training
* Evaluation
* Awaiting Approval
* Approved
* Deployed
* Paused
* Archived


### 6. Supported Machine Learning Tasks

The system should support multiple machine-learning task categories.

#### 6.1 Language and Text

* Text classification
* Topic classification
* Sentiment analysis
* Named-entity recognition
* Intent recognition
* Question answering
* Summarisation
* Translation
* Grammar correction
* Text generation
* Code generation
* Code completion
* Code explanation
* Code-review assistance
* Information extraction
* Document comparison
* Semantic search
* Text embeddings
* Reranking
* Conversation models
* Tool-selection models

#### 6.2 Computer Vision

* Image classification
* Object detection
* Image segmentation
* Optical character recognition
* Visual question answering
* Image similarity
* Defect detection
* Facial recognition, where legally and ethically permitted
* Scene understanding
* Texture analysis
* Video-frame analysis

#### 6.3 Audio and Speech

* Speech-to-text
* Text-to-speech
* Speaker recognition
* Sound classification
* Music classification
* Audio-event detection
* Noise detection
* Voice-command recognition
* Audio embeddings

#### 6.4 Structured Data

* Regression
* Classification
* Forecasting
* Anomaly detection
* Risk scoring
* Recommendation
* Clustering
* Customer segmentation
* Fraud detection
* Resource prediction
* Capacity planning

#### 6.5 Multimodal Models

* Text and image understanding
* Text and audio understanding
* Document image analysis
* Video and text analysis
* Code, logs, screenshots, and documentation analysis
* Combined enterprise-data assistants


### 7. Model Registry

The Model Registry must be the authoritative catalog for all machine-learning models available to the Master AI system.

Each model entry should contain:

* Internal model identifier
* Model name
* Display name
* Version
* Model family
* Base architecture
* Model task
* Model format
* Parameter count
* Precision
* Quantization format
* Context length
* Input types
* Output types
* Supported languages
* License
* Source
* Ownership
* Security classification
* Training dataset references
* Fine-tuning dataset references
* Evaluation results
* Safety assessment
* Hardware requirements
* Runtime requirements
* Deployment status
* Approval status
* Creation date
* Last modified date
* Model hash
* Model signature
* Storage location
* Change log

Supported model states should include:

* Imported
* Unverified
* Verified
* Training
* Evaluation
* Rejected
* Approved
* Staging
* Production
* Deprecated
* Archived
* Quarantined

Models must never be placed into production merely because training completed successfully. Production use requires evaluation and explicit approval.


### 8. Model Import

Administrators should be able to import models from:

* Local storage
* Internal model repositories
* Network storage
* Approved model hubs
* Existing Master AI installations
* Model-development workstations
* Remote training servers
* Container images
* Supported provider APIs

Before import, the system should verify:

* File integrity
* Model format
* Model architecture
* Model size
* License information
* Malware or unsafe content
* Required runtime
* Required custom code
* External dependencies
* Model hash
* Digital signature, where available
* Compatibility with available hardware

Imported models containing untrusted executable code must be isolated and must not execute automatically.


### 9. Model Builder Interface

The Model Builder should guide administrators through the creation of a model configuration.

The interface should support:

* New model from template
* New model from existing architecture
* New model from imported base model
* New model from a previous model version
* New classical machine-learning model
* New neural-network model
* New language-model adaptation
* New embedding model
* New reranking model
* New vision model
* New audio model

The builder should allow configuration of:

* Model architecture
* Layer configuration
* Hidden dimensions
* Attention configuration
* Vocabulary and tokenizer
* Sequence length
* Activation functions
* Dropout
* Initialisation strategy
* Loss function
* Optimiser
* Learning-rate scheduler
* Batch size
* Epoch count
* Gradient accumulation
* Gradient clipping
* Mixed precision
* Checkpoint frequency
* Validation frequency
* Early stopping
* Random seed
* Reproducibility settings
* Distributed-training settings

The interface must provide basic and advanced configuration modes.


### 10. Dataset Manager

The Dataset Manager must organize all information used for training, fine-tuning, evaluation, retrieval, and testing.

Supported dataset sources should include:

* Uploaded files
* Local folders
* Network folders
* Databases
* Web APIs
* Internal APIs
* Source-code repositories
* Document libraries
* Support tickets
* Knowledge bases
* Chat transcripts
* Application logs
* System metrics
* Images
* Audio
* Video
* Structured data
* Manually entered examples
* Synthetic examples
* Administrator-approved web content

Supported file formats may include:

* TXT
* Markdown
* HTML
* JSON
* JSONL
* CSV
* TSV
* XML
* YAML
* PDF
* DOCX
* XLSX
* Source-code files
* Image formats
* Audio formats
* Video formats
* Database exports

Each dataset should record:

* Dataset identifier
* Name
* Description
* Subject area
* Owner
* Source
* License
* Security classification
* Record count
* File count
* Total size
* Data format
* Schema
* Language
* Creation date
* Modification date
* Approval status
* Data-quality score
* Sensitive-data status
* Duplicate rate
* Train, validation, and test split
* Dataset hash
* Dataset version
* Associated projects
* Associated models


### 11. Dataset Versioning

Every dataset change must create a new version.

Versioning should track:

* Added records
* Removed records
* Modified records
* Schema changes
* Label changes
* Cleaning operations
* Filtering operations
* Deduplication operations
* Source changes
* Administrator responsible
* Reason for change
* Date and time
* Dataset checksum

Training runs must reference immutable dataset versions so that results can be reproduced.


### 12. Subject Knowledge Manager

The Subject Knowledge Manager allows administrators to teach the Master AI system about defined domains without necessarily retraining the entire base model.

A subject package should contain:

* Subject name
* Description
* Scope
* Target audience
* Approved terminology
* Definitions
* Concepts
* Rules
* Procedures
* Examples
* Counterexamples
* Reference documents
* Frequently asked questions
* Required reasoning patterns
* Prohibited conclusions
* Known limitations
* Evaluation questions
* Source citations
* Update schedule
* Subject owner
* Review status

Example subject packages may include:

* C++17 programming
* DirectX 11 rendering
* Vulkan rendering
* OpenGL rendering
* Delphi 12 development
* Blender scripting
* Game-engine architecture
* Accounting systems
* Business-management systems
* Web development
* Network administration
* Internal company procedures
* Product documentation
* Customer support
* Security policies

Subject packages may be used for:

* Fine-tuning
* Instruction tuning
* Retrieval-augmented generation
* Embedding generation
* Evaluation-set creation
* Prompt templates
* Agent specialisation


### 13. Knowledge Ingestion Pipeline

Provide a controlled ingestion pipeline that converts source material into machine-learning-ready content.

Pipeline stages should include:

1. Source acquisition
2. File verification
3. Malware scanning
4. Text extraction
5. Encoding detection
6. Language detection
7. Metadata extraction
8. Document classification
9. Section detection
10. Content cleaning
11. Duplicate detection
12. Sensitive-data detection
13. Personal-information detection
14. Secret and credential detection
15. Content segmentation
16. Chunk creation
17. Label assignment
18. Quality scoring
19. Human review
20. Approval
21. Dataset publication
22. Embedding generation
23. Vector-store indexing

Administrators must be able to inspect and correct the output at each stage.


### 14. Data Labeling Interface

The Data Labeling interface should support manual, assisted, and automated labeling.

Supported labeling modes should include:

* Text category labels
* Intent labels
* Sentiment labels
* Entity spans
* Question and answer pairs
* Instruction and response pairs
* Preferred and rejected responses
* Code correctness labels
* Security-severity labels
* Image classification
* Bounding boxes
* Segmentation masks
* Audio-event labels
* Timestamp labels
* Tabular target values

Features should include:

* Labeling queues
* Assignment to reviewers
* Label guidelines
* Keyboard shortcuts
* Bulk labeling
* Suggested labels
* Confidence values
* Disagreement handling
* Consensus review
* Quality sampling
* Reviewer accuracy metrics
* Annotation history
* Label versioning

Machine-generated labels must be clearly distinguished from human-reviewed labels.


### 15. Data Preparation Interface

Administrators should be able to create reusable data-preparation pipelines.

Operations should include:

* Remove duplicates
* Remove empty records
* Normalize whitespace
* Normalize encoding
* Correct malformed records
* Strip unwanted markup
* Remove boilerplate
* Detect language
* Filter by language
* Filter by quality
* Remove secrets
* Remove credentials
* Redact personal information
* Balance classes
* Sample data
* Shuffle data
* Merge datasets
* Split datasets
* Tokenize text
* Calculate token counts
* Resize images
* Normalize images
* Resample audio
* Generate features
* Handle missing values
* Encode categories
* Scale numeric values
* Detect outliers
* Generate train, validation, and test sets

Each pipeline operation must be logged and reproducible.


### 16. Training Jobs

The Training Jobs interface should allow administrators to create and manage training runs.

A training job should include:

* Job name
* Project
* Model configuration
* Dataset version
* Training type
* Compute target
* Hardware allocation
* Runtime environment
* Container image
* Hyperparameters
* Environment variables
* Secrets references
* Output directory
* Checkpoint policy
* Logging policy
* Notification policy
* Maximum runtime
* Maximum resource usage
* Maximum cost, where applicable
* Failure-recovery strategy

Training-job states should include:

* Draft
* Queued
* Preparing
* Running
* Paused
* Canceling
* Canceled
* Failed
* Completed
* Awaiting Evaluation
* Archived

Administrators should be able to:

* Start
* Pause
* Resume
* Stop
* Clone
* Retry
* Compare
* Archive
* Delete
* Promote resulting checkpoints


### 17. Training Methods

The system should support appropriate training methods based on the selected model type.

Possible methods include:

* Training from scratch
* Supervised learning
* Unsupervised learning
* Semi-supervised learning
* Self-supervised learning
* Transfer learning
* Fine-tuning
* Instruction tuning
* Parameter-efficient fine-tuning
* Adapter training
* Low-rank adaptation
* Quantization-aware training
* Knowledge distillation
* Preference optimization
* Reward-model training
* Reinforcement learning
* Continual learning
* Incremental learning
* Federated learning, where required
* Active learning

Only supported and approved methods should be displayed for each model architecture.


### 18. Fine-Tuning Interface

The Fine-Tuning interface should simplify adaptation of existing models.

Administrators should be able to select:

* Base model
* Base model version
* Fine-tuning dataset
* Subject package
* Training method
* Adapter method
* Target layers
* Learning rate
* Batch size
* Epoch count
* Context length
* Precision
* Checkpoint strategy
* Validation dataset
* Safety dataset
* Output model name
* Output version

Fine-tuning presets should include:

* General instruction tuning
* Subject specialisation
* Code assistant
* Classification
* Question answering
* Conversation style
* Tool-use behavior
* Structured-output generation
* Safety alignment
* Terminology adaptation

The interface must estimate expected hardware requirements and storage usage before the job starts.


### 19. Prompt and Instruction Training

Provide an interface for managing instruction datasets used to teach a model how to respond and behave.

An instruction record may contain:

* System instruction
* User instruction
* Context
* Expected response
* Rejected response
* Tool calls
* Tool results
* Required output format
* Subject classification
* Difficulty
* Safety classification
* Reviewer status

Administrators should be able to:

* Create examples manually
* Import examples
* Generate draft examples
* Review generated examples
* Mark preferred and rejected outputs
* Test instructions against multiple models
* Detect contradictory instructions
* Detect duplicated examples
* Validate structured outputs

Generated training examples must require approval before entering an approved dataset.


### 20. Synthetic Data Generation

The Synthetic Data interface should help expand limited datasets while preventing uncontrolled contamination.

Supported operations should include:

* Generate alternative questions
* Generate paraphrases
* Generate examples
* Generate counterexamples
* Generate difficult cases
* Generate malformed inputs
* Generate edge cases
* Generate balanced-class samples
* Generate code samples
* Generate unit-test cases
* Generate simulated conversations
* Generate image variations
* Generate tabular records

Each generated record should include:

* Generator model
* Generator version
* Prompt
* Generation settings
* Creation date
* Confidence score
* Validation status
* Human-review status
* Original source linkage

Synthetic data must remain distinguishable from human-created and real-world data.


### 21. Embeddings and Vector Stores

The system should include an Embeddings and Vector Stores interface for semantic search and retrieval.

Administrators should be able to:

* Register embedding models
* Create vector stores
* Select distance metric
* Configure vector dimensions
* Import documents
* Generate embeddings
* Rebuild indexes
* Delete documents
* Update documents
* Search vectors
* Filter by metadata
* Configure chunk size
* Configure chunk overlap
* Configure retrieval limits
* Configure relevance thresholds
* Configure reranking
* Test retrieval quality

Each vector store should record:

* Name
* Description
* Embedding model
* Embedding-model version
* Vector dimensions
* Distance metric
* Document count
* Chunk count
* Storage size
* Index type
* Security classification
* Access permissions
* Creation date
* Last rebuild date
* Associated subject packages
* Associated agents
* Associated deployed models


### 22. Retrieval-Augmented Generation

Provide a Retrieval-Augmented Generation configuration interface.

Administrators should be able to define:

* User query preprocessing
* Query rewriting
* Search strategy
* Vector store
* Keyword-search source
* Hybrid-search weighting
* Retrieval count
* Relevance threshold
* Metadata filters
* Reranking model
* Context-size limit
* Citation requirements
* Response template
* Fallback behavior
* Source-priority rules
* Restricted documents
* Cache behavior

The system should support testing:

* Retrieved documents
* Retrieval relevance
* Missing information
* Incorrect citations
* Context conflicts
* Response grounding
* Unsupported claims
* Retrieval latency


### 23. Evaluation Lab

Every model must be evaluated before approval or deployment.

Evaluation categories should include:

* Accuracy
* Precision
* Recall
* F1 score
* Loss
* Perplexity
* Mean absolute error
* Mean squared error
* Ranking quality
* Retrieval accuracy
* Response relevance
* Groundedness
* Hallucination rate
* Code correctness
* Compilation success
* Unit-test success
* Latency
* Throughput
* Memory usage
* GPU usage
* Stability
* Safety compliance
* Bias testing
* Robustness
* Adversarial resistance

The interface should support:

* Standard benchmark sets
* Custom benchmark sets
* Subject-specific tests
* Regression tests
* Safety tests
* Adversarial prompts
* Human evaluation
* Pairwise model comparison
* Blind model comparison
* Automated scoring
* Reviewer notes


### 24. Subject Examination System

To verify whether a model has learned a subject, provide a subject examination system.

Each subject examination may include:

* Multiple-choice questions
* Short-answer questions
* Long-answer questions
* Code-writing tasks
* Code-correction tasks
* Scenario analysis
* Troubleshooting exercises
* Structured-output tasks
* Tool-use tasks
* Retrieval tasks
* Fact-verification tasks

The system should calculate:

* Overall subject score
* Score by topic
* Score by difficulty
* Accuracy by question type
* Unsupported-claim rate
* Hallucination rate
* Source-citation quality
* Reasoning consistency
* Failure categories

A minimum approval score should be configurable for each subject.


### 25. Experiment Tracking

Every training and evaluation run must be recorded as an experiment.

Track:

* Experiment identifier
* Project
* Model
* Dataset
* Source-code version
* Configuration version
* Container version
* Hyperparameters
* Random seed
* Hardware
* Runtime
* Training metrics
* Validation metrics
* Evaluation metrics
* Checkpoints
* Logs
* Artifacts
* Notes
* Tags
* Owner
* Start and completion times
* Failure reason
* Result status

Administrators should be able to compare experiments side by side.

Comparison should highlight:

* Parameter differences
* Dataset differences
* Metric changes
* Runtime changes
* Hardware changes
* Storage changes
* Safety changes
* Regression failures


### 26. Hyperparameter Optimization

Provide automated and manual hyperparameter search.

Supported strategies may include:

* Manual search
* Grid search
* Random search
* Bayesian optimization
* Population-based training
* Successive halving
* Early-stopping optimization

Configurable search spaces should include:

* Learning rate
* Batch size
* Epoch count
* Optimiser
* Weight decay
* Dropout
* Warmup steps
* Scheduler
* Adapter rank
* Sequence length
* Gradient accumulation
* Data-sampling strategy

The system should enforce resource and time limits.


### 27. Model Comparison

The Model Comparison interface should allow administrators to compare:

* Model versions
* Base models
* Fine-tuned models
* Quantized models
* Local and remote models
* Different training experiments
* Different retrieval configurations

Comparison categories should include:

* Accuracy
* Subject knowledge
* Hallucination rate
* Safety
* Latency
* Throughput
* Memory use
* GPU use
* Context capacity
* Output quality
* Structured-output compliance
* Tool-use success
* Deployment cost
* Hardware compatibility

The interface should support blind response comparison to reduce reviewer bias.


### 28. Model Optimization

After training, administrators should be able to optimize models for deployment.

Supported optimization operations may include:

* Quantization
* Pruning
* Distillation
* Graph optimization
* Operator fusion
* Weight compression
* Adapter merging
* Checkpoint merging
* Vocabulary reduction
* Context optimization
* Cache optimization
* Batch optimization
* Runtime conversion

The system should compare the optimized model against the original to detect unacceptable quality loss.


### 29. Model Formats and Runtimes

The architecture should support pluggable model formats and runtimes rather than being restricted to one framework.

Possible supported formats include:

* Native framework checkpoints
* Safetensors
* ONNX
* GGUF
* TensorRT-compatible models
* OpenVINO-compatible models
* Platform-specific optimized formats
* Custom internal model packages

Possible runtimes include:

* CPU inference
* NVIDIA GPU inference
* AMD GPU inference
* Intel accelerator inference
* DirectML inference
* ONNX Runtime
* Local language-model runtimes
* Containerised inference servers
* Remote inference nodes
* Approved provider APIs

Model-runtime adapters should expose a common internal inference interface.


### 30. Hardware and Compute Manager

The Hardware and Compute interface should display available resources.

Track:

* Compute-node name
* Node address
* Operating system
* CPU model
* CPU core count
* System memory
* GPU model
* GPU count
* GPU memory
* Driver version
* Runtime versions
* Available storage
* Current workload
* Node health
* Temperature, where supported
* Power usage, where supported
* Queue length
* Supported model formats
* Supported precision modes

Administrators should be able to:

* Register nodes
* Disable nodes
* Assign nodes to projects
* Reserve resources
* Set resource quotas
* Configure scheduling priority
* Drain nodes for maintenance
* Test node health
* Update runtime components


### 31. Training Scheduler

The scheduler should allocate jobs according to:

* User permissions
* Project priority
* Hardware requirements
* Available GPU memory
* Available system memory
* Estimated runtime
* Resource quotas
* Maximum concurrent jobs
* Maintenance windows
* Administrative priority
* Cost limits

The scheduler should prevent one training job from exhausting all resources unless explicitly authorized.


### 32. Distributed Training

For larger models, the system should support distributed training across multiple devices or nodes.

Required controls include:

* Worker count
* Node count
* GPU assignment
* Communication backend
* Gradient synchronization
* Checkpoint coordination
* Failure recovery
* Worker health
* Network-bandwidth monitoring
* Timeout configuration
* Resume strategy

Distributed training must be optional and hidden when unsupported by the available environment.


### 33. Checkpoint Management

The system should automatically manage training checkpoints.

Checkpoint information should include:

* Training step
* Epoch
* Validation metric
* File size
* Creation time
* Parent model
* Dataset version
* Configuration version
* Checkpoint hash
* Storage location
* Retention status

Administrators should be able to:

* Resume from checkpoint
* Compare checkpoints
* Promote checkpoint
* Download checkpoint
* Archive checkpoint
* Delete checkpoint
* Mark checkpoint as protected

Retention policies should prevent storage from growing without control.


### 34. Deployment Manager

The Deployment Manager should promote approved models into supported environments.

Deployment environments may include:

* Development
* Testing
* Staging
* Production
* Offline workstation
* Local intranet
* MCP service
* Desktop client
* Web application
* Visual Studio integration
* VS Code integration
* Internal API
* Batch-processing service

Deployment strategies should include:

* Direct deployment
* Blue-green deployment
* Canary deployment
* Shadow deployment
* A/B testing
* Rolling update

Every deployment must record:

* Model version
* Runtime
* Target node
* Configuration
* Deployment time
* Administrator
* Approval
* Rollback version
* Health status


### 35. Inference Endpoints

Administrators should be able to create controlled inference endpoints.

Endpoint settings should include:

* Endpoint name
* Model
* Model version
* Runtime
* Host
* Port
* Protocol
* Authentication method
* Encryption settings
* Allowed clients
* Rate limits
* Request-size limits
* Response-size limits
* Timeout
* Batch size
* Maximum concurrent requests
* Logging policy
* Cache policy
* Safety policy
* Tool permissions
* Network-access permissions

Supported interfaces may include:

* Internal REST API
* WebSocket
* Local IPC
* Named pipes
* MCP
* Command-line client
* Desktop application
* Web administration interface
* Visual Studio extension
* VS Code extension


### 36. Agent and Model Integration

The Machine Learning module should allow trained models to be assigned to Master AI agents.

An agent configuration should specify:

* Primary model
* Fallback model
* Embedding model
* Reranking model
* Vision model
* Audio model
* Subject packages
* Vector stores
* Tools
* System instructions
* Context limits
* Safety policy
* Resource limits
* Network permissions
* File permissions
* Execution permissions

Different models may be assigned to specialized agents, including:

* Coding agent
* Planning agent
* Documentation agent
* Research agent
* Data-analysis agent
* Security-analysis agent
* Image-analysis agent
* Audio-analysis agent
* Business-systems agent


### 37. Automated Machine Learning Pipelines

Provide reusable pipelines for the complete machine-learning lifecycle.

A pipeline may include:

1. Import data
2. Validate data
3. Clean data
4. Label data
5. Split data
6. Train model
7. Validate model
8. Evaluate model
9. Run safety tests
10. Optimize model
11. Request approval
12. Deploy to staging
13. Run staging tests
14. Deploy to production
15. Monitor production
16. Trigger rollback when required

Pipeline triggers may include:

* Manual execution
* Scheduled execution
* New dataset version
* New source documents
* Model-performance degradation
* Approval event
* Source-code update
* Configuration update

Administrators must be able to pause, resume, clone, and inspect pipelines.


### 38. Continual Learning

Continual learning should be tightly controlled.

The system must not automatically train production models from uncontrolled user conversations.

A safe continual-learning workflow should be:

1. Collect candidate examples.
2. Remove personal information and secrets.
3. Classify examples.
4. Score quality.
5. Detect harmful or malicious examples.
6. Present examples for administrator review.
7. Add approved examples to a versioned dataset.
8. Run scheduled fine-tuning.
9. Evaluate the new model.
10. Compare against the production model.
11. Require approval.
12. Deploy using a controlled rollout.

This prevents model poisoning, accidental memorization, and uncontrolled behavior changes.


### 39. Feedback Collection

The system may collect feedback from authorized users.

Feedback types should include:

* Correct response
* Incorrect response
* Partially correct response
* Unsafe response
* Outdated response
* Unsupported claim
* Poor citation
* Tool-use failure
* Formatting failure
* Performance problem
* Preferred alternative response

Feedback should enter a review queue and must not become training data automatically.


### 40. Safety and Governance

The Safety and Governance interface should control how models are trained and used.

Required controls include:

* Dataset approval
* Model approval
* Deployment approval
* Restricted data categories
* Prohibited data sources
* Personal-information handling
* Credential detection
* Secret detection
* Copyright and license tracking
* Harmful-content testing
* Bias testing
* Hallucination testing
* Prompt-injection testing
* Data-poisoning detection
* Model provenance
* Model cards
* Dataset cards
* Retention policies
* Deletion policies
* Export restrictions
* Network restrictions

Every production model should have a model card describing:

* Purpose
* Intended use
* Prohibited use
* Training data
* Evaluation results
* Known limitations
* Safety controls
* Hardware requirements
* License
* Owner
* Approval status


### 41. Security Requirements

The Machine Learning module must follow strict security controls.

Required protections include:

* Administrator authentication
* Multi-factor authentication support
* Role-based access control
* Session expiry
* Account-lockout policy
* Password hashing
* Transport encryption
* Encrypted sensitive storage
* Secret vault integration
* Signed model packages
* File-integrity checks
* Dataset checksums
* Model checksums
* Malware scanning
* Path validation
* File-type validation
* Request-size limits
* Rate limiting
* Network-access restrictions
* Sandboxed model conversion
* Sandboxed custom code
* Container isolation
* Least-privilege service accounts
* Audit logging
* Backup encryption

Training workloads must not automatically receive:

* Host administrator privileges
* Unrestricted file-system access
* Unrestricted network access
* Production credentials
* Database-administrator credentials
* Access to unrelated projects


### 42. Model and Dataset Isolation

Projects with different security classifications must be isolated.

Isolation should apply to:

* Storage
* Databases
* Vector stores
* Training jobs
* Runtime environments
* API credentials
* Compute nodes
* Logs
* Backups
* Export permissions

A model trained on confidential data must not be published to unrestricted users or external services without explicit approval.


### 43. Audit Logging

Every important action must be recorded.

Audit events should include:

* Administrator login
* Permission changes
* Dataset import
* Dataset export
* Dataset approval
* Dataset deletion
* Model import
* Model creation
* Training start
* Training stop
* Training failure
* Model evaluation
* Model approval
* Model rejection
* Model deployment
* Model rollback
* Endpoint creation
* Endpoint modification
* Compute-node changes
* Safety-policy changes
* Secret access
* Configuration changes

Each audit record should include:

* Timestamp
* User
* Role
* IP address
* Session
* Action
* Target resource
* Previous value
* New value
* Result
* Failure reason
* Correlation identifier

Audit logs should be tamper-resistant and searchable.


### 44. Monitoring and Diagnostics

The monitoring interface should track both training and inference.

#### Training Monitoring

Display:

* Current epoch
* Current step
* Training loss
* Validation loss
* Learning rate
* Gradient norm
* Throughput
* Estimated completion progress
* CPU usage
* GPU usage
* GPU memory
* System memory
* Disk activity
* Network activity
* Temperature
* Errors
* Warnings

#### Inference Monitoring

Display:

* Requests per second
* Average latency
* Percentile latency
* Queue depth
* Input-token rate
* Output-token rate
* Error rate
* Timeout rate
* Memory usage
* GPU usage
* Cache-hit rate
* Safety-filter rate
* Tool-call success
* Retrieval latency
* Model-loading time


### 45. Drift and Regression Detection

The system should detect when deployed models deteriorate.

Monitor:

* Accuracy drift
* Input-data drift
* Output-distribution drift
* Latency regressions
* Increased hallucinations
* Increased safety violations
* Increased tool-use failures
* Increased user rejection
* Retrieval-quality degradation
* Resource-use increases

When a threshold is exceeded, the system should:

* Raise an alert
* Record diagnostic information
* Compare against the previous model version
* Recommend evaluation
* Optionally disable automatic routing
* Allow rollback to a stable version

Automatic rollback should only occur when explicitly configured.


### 46. Backup and Recovery

Back up:

* Project metadata
* Dataset metadata
* Approved datasets
* Model configurations
* Model files
* Checkpoints
* Evaluation results
* Vector stores
* Pipeline definitions
* Safety policies
* Audit logs

Recovery procedures should support:

* Project restoration
* Dataset-version restoration
* Model-version restoration
* Vector-index rebuilding
* Deployment rollback
* Database recovery
* Compute-node replacement

Backups must be encrypted and periodically tested.


### 47. Storage Management

Provide storage quotas and retention policies for:

* Uploaded datasets
* Processed datasets
* Temporary files
* Checkpoints
* Model versions
* Logs
* Evaluation artifacts
* Vector indexes
* Container images
* Export packages

Administrators should be able to identify:

* Largest models
* Largest datasets
* Unused checkpoints
* Old experiments
* Duplicate files
* Orphaned artifacts
* Expired temporary data

Deletion should support a protected recovery period where appropriate.


### 48. Notifications

Administrators should receive notifications for:

* Training completed
* Training failed
* Evaluation completed
* Model awaiting approval
* Dataset awaiting approval
* Hardware failure
* Storage threshold reached
* Model drift detected
* Safety violation detected
* Deployment completed
* Deployment failed
* Rollback performed
* Endpoint unavailable

Notification channels may include:

* Master AI notification center
* Email
* Desktop notification
* Internal webhook
* Administrative dashboard alerts


### 49. Machine Learning Settings

Global Machine Learning settings should include:

* Default storage paths
* Dataset storage path
* Model storage path
* Checkpoint storage path
* Log storage path
* Temporary-data path
* Default compute node
* Default training runtime
* Default inference runtime
* Maximum concurrent training jobs
* Maximum concurrent inference models
* Default resource quotas
* Default model context limit
* Default dataset split ratios
* Default checkpoint interval
* Default retention policy
* Allowed model formats
* Allowed dataset formats
* Approved external repositories
* Network-access policy
* Proxy settings
* Notification settings
* Audit retention
* Backup schedule
* Safety-policy defaults

Settings should be stored in validated configuration files or the Master AI configuration database.


### 50. Recommended Internal Services

The Machine Learning module should be separated into internal services or clearly defined components.

Recommended components include:

* ML Administration API
* Project Service
* Dataset Service
* Data-Ingestion Service
* Labeling Service
* Training Orchestrator
* Compute Scheduler
* Experiment Service
* Model Registry Service
* Evaluation Service
* Deployment Service
* Inference Gateway
* Embedding Service
* Vector Store Service
* Safety Service
* Audit Service
* Notification Service
* Storage Service
* Secrets Service
* Hardware Monitoring Service

These services may initially run in one application but should use clear interfaces so they can later be separated.


### 51. Suggested API Areas

Recommended administrative API groups include:

* `/api/admin/ml/projects`
* `/api/admin/ml/datasets`
* `/api/admin/ml/dataset-versions`
* `/api/admin/ml/labels`
* `/api/admin/ml/subjects`
* `/api/admin/ml/models`
* `/api/admin/ml/model-versions`
* `/api/admin/ml/training-jobs`
* `/api/admin/ml/experiments`
* `/api/admin/ml/evaluations`
* `/api/admin/ml/checkpoints`
* `/api/admin/ml/vector-stores`
* `/api/admin/ml/rag`
* `/api/admin/ml/deployments`
* `/api/admin/ml/endpoints`
* `/api/admin/ml/compute-nodes`
* `/api/admin/ml/pipelines`
* `/api/admin/ml/safety`
* `/api/admin/ml/audit`
* `/api/admin/ml/settings`

Normal users must not have direct access to administrative routes.


### 52. User Interface Requirements

All Machine Learning interfaces should provide:

* Consistent navigation
* Search
* Filtering
* Sorting
* Pagination
* Status indicators
* Resource-usage indicators
* Validation messages
* Warning confirmations
* Contextual help
* Advanced settings panels
* Change history
* Export options
* Permission-aware controls
* Accessible keyboard navigation
* Responsive layout
* Dark and light interface support

Long-running operations should display progress through job status updates rather than locking the browser request.


### 53. Model-Teaching Workflow

A recommended workflow for teaching a model a specialized subject is:

1. Create a Machine Learning project.
2. Define the subject and expected capabilities.
3. Create a Subject Knowledge Package.
4. Import approved source documents.
5. Run the ingestion and cleaning pipeline.
6. Detect secrets and sensitive information.
7. Divide content into topics.
8. Create training examples.
9. Create evaluation questions.
10. Review and approve the dataset.
11. Select a suitable base model.
12. Select fine-tuning or retrieval-based teaching.
13. Run a small experimental training job.
14. Evaluate subject knowledge.
15. Review errors and weak topics.
16. Improve the dataset.
17. Repeat training and evaluation.
18. Run safety and regression testing.
19. Approve the model.
20. Deploy to staging.
21. Test through the Master AI interface.
22. Deploy to production.
23. Monitor performance.
24. Collect reviewed feedback.
25. Create later model versions as the subject changes.


### 54. Choosing Between Fine-Tuning and Retrieval

The system should help administrators select the correct teaching method.

Use retrieval-augmented generation when:

* Knowledge changes frequently.
* Exact source citations are required.
* Documents must remain separately manageable.
* The model should answer from private internal material.
* The subject is too large to encode into a fine-tuning dataset.
* Administrators need immediate document updates.

Use fine-tuning when:

* The model must learn a response style.
* The model must follow a specialized output format.
* The model must learn repeated task behavior.
* The model must improve domain terminology.
* The model must learn tool-use patterns.
* The desired behavior cannot be achieved reliably through prompts alone.

Use both when:

* The model needs specialized behavior and current factual knowledge.
* The model should understand domain terminology while retrieving exact current information.
* The model needs structured workflows backed by internal documents.


### 55. Initial Implementation Phases

#### Phase 1 — Administration Foundation

Implement:

* Sidebar integration
* Administrator permissions
* Machine Learning dashboard
* Projects
* Model Registry
* Dataset Manager
* Compute-node registration
* Audit logging
* Global settings

#### Phase 2 — Dataset and Knowledge Preparation

Implement:

* Dataset versioning
* Subject Knowledge Manager
* Document ingestion
* Data cleaning
* Data splitting
* Data labeling
* Sensitive-data detection
* Embedding generation
* Vector stores

#### Phase 3 — Training and Fine-Tuning

Implement:

* Model Builder
* Training Jobs
* Checkpoint management
* Fine-tuning
* Experiment tracking
* Training monitoring
* Resource scheduling

#### Phase 4 — Evaluation and Approval

Implement:

* Evaluation Lab
* Subject examinations
* Model comparison
* Safety tests
* Regression tests
* Approval workflow
* Model cards

#### Phase 5 — Deployment and Inference

Implement:

* Deployment Manager
* Inference endpoints
* Runtime adapters
* MCP integration
* Desktop and web integration
* Staging and production environments
* Rollback support

#### Phase 6 — Automation and Advanced Features

Implement:

* Automated pipelines
* Hyperparameter optimization
* Synthetic data
* Continual-learning review workflow
* Drift monitoring
* Canary deployment
* Distributed training
* Advanced multimodal training


### 56. Important Design Rules

The implementation must follow these rules:

1. Do not allow ordinary users to access model-training administration.
2. Do not automatically train from unreviewed conversations.
3. Do not allow imported models to execute untrusted code without isolation.
4. Do not overwrite models, datasets, or configurations without versioning.
5. Do not deploy a model without evaluation and approval.
6. Do not expose sensitive training data through logs or model outputs.
7. Do not provide unrestricted network access to training jobs.
8. Do not place secrets directly into datasets, scripts, logs, or configuration files.
9. Do not rely on model filenames as proof of identity; use hashes and registry metadata.
10. Do not delete production models without rollback protection.
11. Do not permit training jobs to consume unlimited system resources.
12. Do not mix training, validation, and test records incorrectly.
13. Do not use the test dataset during training.
14. Do not permit synthetic data to silently replace verified real data.
15. Do not permit automatic continual learning without human approval.
16. Preserve full audit history for administrative actions.
17. Ensure every training result can be traced to its exact data, configuration, code, and runtime versions.
18. Keep the machine-learning layer modular so that model frameworks and runtimes can be replaced.
19. Support local-first operation so that private models and datasets can remain inside the Master AI intranet.
20. Allow external services only when explicitly configured and approved.


### 57. Expected Outcome

When complete, the Machine Learning module will allow the Master AI system to function as a controlled local AI-development platform rather than only an inference interface.

Administrators will be able to:

* Build specialized AI capabilities.
* Teach models internal and technical subjects.
* Maintain subject-specific assistants.
* Train models using local datasets.
* Adapt imported base models.
* Create retrieval-based knowledge systems.
* Evaluate model quality scientifically.
* Track every experiment.
* Deploy approved models safely.
* Integrate models with agents, MCP, APIs, desktop applications, web applications, Visual Studio, and VS Code.
* Monitor models after deployment.
* Improve models through reviewed and versioned development cycles.
* Maintain ownership and control over private data, training assets, model versions, and deployment infrastructure.

## 26. Release Gates

No release may be called production-ready until it passes:

- Functional gate.
- Security gate.
- Data migration gate.
- Recovery gate.
- MCP conformance gate.
- Performance regression gate.
- Resource-ceiling and low-memory degradation gate.
- Retrieval quality, provenance, and authorization-isolation gate.
- Cache invalidation and cross-boundary isolation gate.
- Licensing and model provenance gate.
- Documentation gate.

## 27. Decisions Gated Before Their Relevant Phase Can Exit

| # | Decision | Audit state |
|---|---|---|
| 1 | Final project and binary name | Resolved: `MasterAI` / `masterai` |
| 2 | C++ server foundation | Resolved: internally authored native C++17 HTTP control plane under rules 14–15 |
| 3 | Supported Linux distributions | Resolved by ADR-0003: Ubuntu 24.04 LTS and Debian 13, x86-64 |
| 4 | Minimum CPU and GPU support | Resolved by ADR-0003: SSE4.2/four threads/16 GiB; GPU optional, 8 GiB for acceleration |
| 5 | Windows release target | Resolved: Windows x86-64 is the primary initial target |
| 6 | Initial inference backend and pinned versions | Resolved by ADR-0003: `llama.cpp` b10156 / 91f8c9c |
| 7 | Programming model taxonomy | Resolved in `docs/architecture/model-taxonomy.md` |
| 8 | Local single-user bypass | Resolved: no bypass; authentication remains fail-closed |
| 9 | Intranet TLS certificate strategy | Resolved by ADR-0003: same-host proxy with organisational/private-PKI or public CA certificate |
| 10 | Identity and non-password secret storage | Resolved: ADR-0002 identity; DPAPI, Linux keyring, and systemd encrypted credentials in ADR-0003 |
| 11 | Release-1 attachment formats | Resolved by ADR-0003: bounded UTF-8 text/code formats; ambiguous binary rejected |
| 12 | Voice transcription backend | Resolved by ADR-0003: optional isolated whisper.cpp v1.7.5 / 51c6961 |
| 13 | MCP stable specification revision | Resolved: `2025-11-25` |
| 14 | Legacy SSE compatibility | Resolved for the initial baseline: disabled; reconsider only for an identified client |
| 15 | Model licensing policy and approved sources | Resolved by ADR-0003 and enforced by strict manifest validation |
| 16 | Benchmark suite licensing and quality metrics | Resolved by ADR-0003: HumanEval MIT, MBPP CC-BY-4.0, authored regression suite, fixed quality/performance metrics |

All release-1 high-risk decisions are closed. Later expansion requires a new
ADR and compatibility/security validation rather than silently reopening this
baseline.

## 28. Non-Goals for Initial Release

- Training foundation models.
- Hosting unrelated generation categories.
- Public internet SaaS operation.
- Multi-node distributed inference.
- Kubernetes deployment.
- Custom cryptographic algorithms.
- An unrestricted shell agent.
- Automatic execution of model-proposed commands without policy and approval.
- Replacing mature optimized inference kernels before profiling proves a need.

## 29. Reference Standards and Upstream Documentation

The implementation team should pin and periodically review these primary references:

- Model Context Protocol transport specification: https://modelcontextprotocol.io/specification/2025-11-25/basic/transports
- Model Context Protocol roadmap: https://modelcontextprotocol.io/development/roadmap
- OWASP Password Storage Cheat Sheet: https://cheatsheetseries.owasp.org/cheatsheets/Password_Storage_Cheat_Sheet.html
- OWASP Authentication Cheat Sheet: https://cheatsheetseries.owasp.org/cheatsheets/Authentication_Cheat_Sheet.html
- OWASP Cryptographic Storage Cheat Sheet: https://cheatsheetseries.owasp.org/cheatsheets/Cryptographic_Storage_Cheat_Sheet.html
- OWASP Top 10: https://owasp.org/www-project-top-ten/
- `llama.cpp` server documentation: https://github.com/ggml-org/llama.cpp/blob/master/tools/server/README.md
- Hugging Face Hub download guide: https://huggingface.co/docs/huggingface_hub/en/guides/download
- Hugging Face CLI guide: https://huggingface.co/docs/huggingface_hub/en/guides/cli

## 30. Final Planning Outcome

The recommended implementation is a native, modular, programming-focused AI host with strict loopback defaults, deliberate intranet enablement, real authentication, process-isolated inference, categorized model storage, resumable verified downloads, hardware-aware recommendations, reproducible benchmarks, a complete development-oriented web interface, and bidirectional MCP support.

The implementation should proceed phase by phase. Security, lifecycle correctness, and observability must be established before advanced agent capabilities or aggressive performance optimization are introduced.

## 31. Performance, Retrieval, Caching, and Low-Memory Architecture

### 31.1 Planning boundary and optimization rule

Phases 13–20 extend the original Phase 12 measurement discipline. They do not
reopen completed security or lifecycle boundaries, authorize a custom
transformer engine, or make `llama.cpp` part of the control-plane foundation.
Backend-specific functionality remains optional, isolated, version-pinned, and
replaceable.

An optimization is accepted only when evidence identifies which independent
outcome improved and what trade-off was introduced:

1. Time to first token.
2. Prompt-evaluation throughput.
3. Generation throughput.
4. Retrieval quality and preparation latency.
5. Peak resident memory and commit/pagefile pressure.
6. Output quality and correctness.

Every queue, cache, buffer pool, context window, index operation, retrieval
result, attachment job, and background worker must have an enforced resource
limit. Virtual address space, a pagefile, swap, or a RAM-backed drive must never
be presented as equivalent to sufficient physical RAM.

### 31.2 Hardware profiles and initial defaults

The service derives a profile but permits an administrator to choose a stricter
one:

| Profile | Intended host | Default behaviour |
|---|---|---|
| Minimal | Older/low-memory CPU-only host | No idle model unless configured; one inference request; 2,048–4,096 context; 8–20 compact chunks; one index worker; background work pauses during inference |
| Balanced | Mainstream desktop/laptop | One warm model; one generation with a bounded queue; 4,096–8,192 context; bounded prefix/retrieval caches; two to four measured indexing workers |
| Performance | Ample RAM/VRAM | Multiple slots or warm runners only when measured; larger context/cache budgets; optional batching and GPU-resident KV |

Recommended first performance-focused defaults are:

| Control | Initial value |
|---|---|
| Binding | Loopback |
| Loaded generation models | Maximum 1 |
| Active/queued inference | 1 / 8 |
| Default context and generation reserve | 4,096 / 1,024 tokens |
| Retrieval chunks | Maximum 16 |
| Background/interactive index workers | 1 / maximum 2 |
| Model load | `auto`, prefer compatible memory mapping |
| Memory lock | Disabled |
| Continuous batching/speculative decoding | Disabled |
| Prompt/retrieval/file-content cache | 256 / 128 / 128 MiB maximum |
| OS reserve | At least 2,048 MiB and 15% free physical RAM |
| Network-hosted models | Denied by default |
| Indexing during inference | Throttled or paused by pressure/profile |

These are safe starting points, not performance claims. Phase 19 calibration
may lower or raise them within the hard administrator limits.

### 31.3 End-to-end query pipeline

```text
Client
  -> HTTP/MCP admission and authentication
  -> authorization and request normalization
  -> prompt classification and context budget
  -> retrieval plan
  -> bounded parallel retrieval
  -> ranking, fusion, and deduplication
  -> segmented prompt assembly and token estimate
  -> runner admission and compatible-prefix check
  -> prompt evaluation
  -> immediately streamed generation
  -> incremental persistence and cache update
  -> deterministic resource release
```

Every stage has a monotonic timer, cancellation point, byte/task limit, and
sanitized diagnostic. The coordinator allocates a total deadline instead of
allowing each stage to consume an independent unbounded timeout. When retrieval
expires, it uses the strongest authorized evidence already available only when
safe to do so.

Large text stays in immutable reference-counted buffers or bounded views.
Request bodies, downloads, conversations, and generated output are streamed or
persisted incrementally. Prompt assembly should use segmented buffers where the
adapter permits it; full logits and duplicate token/text copies are not retained
without an explicit measured requirement.

### 31.4 Runtime components and responsibility boundaries

`QueryCoordinator` owns request deadlines, profile selection, context budgets,
child cancellation, backpressure, streaming state, and per-stage metrics. It
owns only lightweight metadata.

`RetrievalPlanner` selects the lowest-cost sufficient retrieval strategy.
`RetrievalWorkerPool` runs bounded independent tasks with interactive priority.
`ContextBudgeter` reserves generation space and admits only the highest-value
authorized context. `ResultReranker` fuses and deduplicates evidence.

`InferenceScheduler` separates IDE completion, interactive chat, IDE analysis,
user-triggered background work, benchmarks, and maintenance. Weighted fairness
may delay low-priority work but must prevent permanent starvation.

`MemoryBudgetManager` is the sole authority for process-wide budget categories
and pressure actions. Individual caches and workers report usage to it and may
not invent independent unlimited reserves.

`CacheManager` applies byte quotas, identity/version keys, invalidation,
pressure trimming, metrics, and corruption quarantine consistently.

`CalibrationService` records exact reproducible evidence and persists tuning
profiles. It cannot override security policy, model integrity, authorization,
or the administrator's memory ceiling.

### 31.5 Model loading, eviction, virtual memory, and scratch storage

Compatible GGUF adapters should prefer mapped weights in `auto` mode so the OS
can demand-page and reclaim clean file-backed pages. Mapping does not make an
oversized model practical: predicted destructive paging is rejected or
downgraded with an explicit warning.

Adapter-neutral load controls are:

```json
{
  "loadMode": "auto",
  "allowMemoryMap": true,
  "allowMemoryLock": false,
  "allowDirectIo": false,
  "prefetchMode": "adaptive",
  "warmup": "minimal"
}
```

Model states are `Cold`, `Loading`, `Warm`, `Busy`, `Idle`, `Draining`, and
`Unloaded`. Pressure eviction removes low-value retrieval/file caches first,
then idle embedding models, then the least-recently-used unpinned generation
model. Active runners are drained rather than killed except to enforce a hard
safety limit.

Ordinary local SSD storage plus the OS file cache is the default. RAM-backed
drives are allowed only for small ephemeral artifacts when sufficient physical
RAM remains; they are prohibited for durable chats/audit logs, resumable
downloads, or duplicated full models on constrained hosts. Scratch uses unique
per-job directories, a byte ceiling, cleanup records, and atomic publication.
The UI must not promise secure deletion on SSDs; encryption at rest and short
retention are the dependable controls.

### 31.6 Index and retrieval storage design

Index storage consists of compact metadata and string tables, disk-backed
posting lists/vector blocks, small bounded hot tables, immutable mapped
segments, a mutable delta journal, and an atomic generation manifest.
Compaction produces a new verified generation before swapping the manifest.
Corrupt segments are quarantined individually and the last valid generation
remains readable.

Parallel reading happens across independent paths or batches, lexical versus
semantic work, symbol/diagnostic lookups, or attachment versus repository work.
It must not create threads per file, split small files into competing reads, or
parse/hash the same revision repeatedly.

The embedding engine is a smaller replaceable adapter, loaded on demand,
batching background chunks, caching by content hash, operating on CPU when
necessary, and unloading under pressure.

### 31.7 Cache and authorization design

Cache identity is a correctness and security boundary. Where applicable, keys
include user, tenant, project, membership/policy generation, file/chunk digest,
index generation, parser/chunker/embedding/tokenizer versions, model/backend
fingerprints, prompt template, and inference settings.

Performance features must never:

- Share prompt, KV, retrieval, or decrypted state across unauthorized users or
  projects.
- Bypass canonical path authorization, attachment quarantine, model
  verification, audit, cancellation, or request limits.
- Reuse cached results after membership, policy, model, backend, or source
  changes.
- Expose private paths through cache keys, metrics, or timing diagnostics.
- Use unvalidated writable shared memory across trust boundaries.
- Leave sensitive temporary data broadly readable.

Authorization is always evaluated from authoritative current state. Cache
corruption triggers bounded quarantine/rebuild and safe uncached operation.

### 31.8 Configuration surface

The strict schema may add these versioned domains during their owning phase:

```json
{
  "performance": {
    "profile": "auto",
    "interactiveDeadlineMs": 2000,
    "preferLowTimeToFirstToken": true,
    "backgroundWorkDuringInference": "throttle",
    "autoTune": true
  },
  "memory": {
    "policy": "adaptive",
    "hardLimitMiB": 0,
    "minimumOsReserveMiB": 2048,
    "minimumFreePercent": 15,
    "criticalPressurePercent": 92
  },
  "inference": {
    "maxActiveRequests": 1,
    "maxQueuedRequests": 8,
    "idleUnloadSeconds": 600,
    "loadMode": "auto",
    "warmup": "minimal",
    "continuousBatching": "auto",
    "speculativeDecoding": false
  },
  "context": {
    "defaultTokens": 4096,
    "maximumTokens": 8192,
    "generationReserveTokens": 1024,
    "retrievalMaximumChunks": 16,
    "retrievalMaximumTokens": 4096
  },
  "cache": {
    "fileMetadataMiB": 64,
    "fileContentMiB": 128,
    "retrievalMiB": 128,
    "embeddingMiB": 256,
    "tokenizationMiB": 64,
    "promptMiB": 256,
    "allowDiskPromptCache": false
  },
  "indexing": {
    "enabled": true,
    "interactiveWorkers": 2,
    "backgroundWorkers": 1,
    "pauseAtMemoryPressure": "high",
    "publishPartialIndex": true
  },
  "storage": {
    "scratchPath": "./runtime/tmp",
    "scratchLimitMiB": 1024,
    "allowNetworkModels": false,
    "preferMemoryMappedIndexes": true
  }
}
```

`hardLimitMiB: 0` means derive a safe limit, never unlimited. Unknown fields
remain rejected. Backend-specific controls stay within validated adapter
namespaces instead of leaking across the general configuration surface.

### 31.9 API and UI surface

Authenticated routes, introduced only by their owning phase, are:

```text
GET  /api/v1/system/resources
GET  /api/v1/system/memory
GET  /api/v1/performance/profile
POST /api/v1/performance/calibrate
GET  /api/v1/performance/recommendations
GET  /api/v1/cache/status
POST /api/v1/cache/trim
POST /api/v1/cache/clear
GET  /api/v1/projects/{id}/index
POST /api/v1/projects/{id}/index/rebuild
POST /api/v1/projects/{id}/index/cancel
POST /api/v1/models/{id}/suitability
GET  /api/v1/requests/{id}/metrics
```

Administrative mutations require CSRF/host/origin/role/scope enforcement and
audit. Clearing or trimming may detach only unused entries and cannot invalidate
buffers held by active requests.

The UI shows actual process/model/KV/cache memory, context use and remaining
tokens, queue position, TTFT, prompt/generation throughput, retrieval sources,
cache reuse, and low-memory recommendations. Simple actions are `Reduce memory
use`, `Optimize for speed`, and `Run calibration`; backend flags remain in an
expert-only view.

### 31.10 Proposed ISO C++17 source boundaries

```text
src/
├── performance/  performance_profile, calibration_service, latency_budget,
│                 resource_sampler, tuning_profile_store
├── memory/       memory_budget_manager, memory_pressure_monitor, buffer_pool,
│                 mapped_region
├── retrieval/    query_coordinator, retrieval_planner, retrieval_worker_pool,
│                 lexical_retriever, semantic_retriever, symbol_retriever,
│                 graph_expander, result_reranker, context_budgeter
├── cache/        cache_manager, segmented_lru, file_metadata_cache,
│                 embedding_cache, retrieval_cache, tokenization_cache,
│                 prompt_cache_registry
├── indexing/     discovery_pipeline, index_generation, segment_reader,
│                 segment_writer, delta_index, index_compactor
└── io/           async_file_reader, windows_iocp_reader,
                  linux_io_uring_reader, worker_pread_reader, storage_probe
```

These are responsibility boundaries, not permission to create parallel systems.
Before implementation, the owning phase must inspect and extend the existing
`src/performance.*`, inference, storage, workflow, HTTP, and platform paths.
New units follow the top-of-file and function-flow documentation rule, compile
as strict ISO C++17, and keep platform-specific code behind conditional native
adapters.

### 31.11 Expanded benchmark and degradation matrix

Performance validation covers cold/warm OS cache, cold/warm runner, cached and
uncached prefix, context sizes from 512 through 8,192 tokens where the host can
fit them, exact/lexical/semantic/hybrid retrieval, incremental index updates,
mapped-index cold/warm reads, one/two compatible requests, indexing/download
contention, cancellation, disconnect, and queue saturation.

Each accepted optimization records baseline, changed setting, host, model and
backend hashes, TTFT, prompt and generation throughput, peak resident/commit
memory, page faults, quality, and power/thermal notes.

Degradation order is deterministic:

1. Stop speculative prefetch and optional advanced features.
2. Pause or reduce background indexing.
3. Trim file, retrieval, and prompt caches.
4. Reduce retrieval chunks and request context.
5. Reduce parallel sequences.
6. Unload the embedding model.
7. Unload idle generation runners.
8. Reject new work with a safe, actionable diagnostic.

Slow storage reduces random-read fan-out and avoids rescans; thermal decline
reduces background workers and uses measured inference-thread settings. No
degradation step weakens security, integrity, authorization, or audit controls.

### 31.12 Completion acceptance

The performance expansion is complete only when:

- The control plane remains small and never loads model weights.
- Every queue, cache, worker pool, context, and transient buffer has an enforced
  ceiling and cancellation path.
- Low-memory mode operates with one indexing worker and no persistent model.
- Project indexes are incremental, disk-backed, checksummed, and recoverable.
- Retrieval returns compact, disclosed, authorized context within a deadline.
- Repeated requests benefit from safe cache/prefix reuse where compatible.
- A model predicted to cause destructive paging is rejected or downgraded.
- Background work yields to interactive inference.
- Calibration never overrides the hard RAM cap.
- Advanced optimizations have independent switches, evidence, and safe
  fallbacks.
