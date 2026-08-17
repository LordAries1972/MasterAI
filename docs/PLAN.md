# Local Programming AI System — Detailed Implementation Plan

## Document Status

This document is the authoritative phased implementation and validation record for MasterAI.

Current phase status:

- Phase 0: Complete — ADR-0003 pins every release-1 platform, hardware,
  backend, TLS, secret, attachment, voice, MCP, licensing, dataset, and metric
  decision formerly open in Section 28.
- Phase 1: Complete — schema-versioned strict configuration, precedence,
  atomic persistence/backup, first-run/reset wizard, checksummed record
  journal, migration metadata, recovery/checkpoint/backup, lifecycle scripts,
  health, graceful shutdown, and Windows Debug/Release validation are
  implemented. Revalidation on 2026-08-05 used the current Windows Debug tree:
  the native suite passed, `/health/live` and `/health/ready` both returned
  HTTP 200 on `127.0.0.1:7070`, and `scripts/stop.ps1` completed graceful
  shutdown. The native Linux x86-64 Release build and tests also pass under
  Ubuntu 26.04 WSL; certification on the pinned Ubuntu 24.04 and Debian 13
  packaging hosts remains a release-packaging gate, not a Phase 1 exit
  criterion.
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
- Phase 4: Complete (validated 2026-08-02) — the `llama.cpp` runner is
  process-isolated and supervised; readiness, loopback IPC tokenization,
  incremental generation, caller/disconnect cancellation, unload, logs, and
  resident-memory/request metrics are implemented. The isolated fake-runner
  integration test passes, and the exit criterion is now also backed by a
  real run: `qwen25-coder-3b-q4km` (Qwen2.5-Coder-3B-Instruct Q4_K_M) loaded
  on the pinned `llama.cpp` build (`llama-server.exe`, ADR-0003's
  `b10156`/`91f8c9c`) and answered an authenticated `POST
  /api/v1/chats/{id}/messages` request end to end — 23 prompt tokens, 28
  generated tokens, streamed NDJSON tokens, no model memory resident in the
  main `masterai.exe` process (`runtime/logs/runner-qwen25-coder-3b-q4km.log`
  shows the runner as a separate process with its own CUDA/CPU device log).
- Phase 5: Complete (validated 2026-08-05) —
  authenticated native login and workspace pages, durable projects/chats,
  Ready-model selection, auto-load, live NDJSON responses, browser
  cancellation, bounded UTF-8 attachments, ownership/integrity rechecks,
  prompt context assembly, the transcription adapter boundary, and durable
  owner-scoped user memory are implemented. The memory path recognizes
  `save to memory: <detail>` without invoking inference, applies bounded
  deterministic automatic capture for common self-disclosures, and (since
  Phase 61) recalls the newest details once at conversation creation as an
  explicitly labelled, persisted user-reference snapshot for every model,
  and exposes inspect/add/delete controls through `/api/v1/memories` and the
  collapsed chat sidebar. The end-to-end authenticated chat path is now real-model
  validated at the API level: login (local password account), project
  creation, chat creation against `qwen25-coder-3b-q4km`, and a real streamed
  reply all completed through the live server (see Phase 4). Two real-model
  UI bugs surfaced and were fixed during this validation pass:
  `src/inference.cpp`'s SSE line parser was forwarding every non-empty line
  instead of only `data:` payload lines, which broke on the runner's
  keep-alive comment frames during slow prompt evaluation; and
  `src/web_ui.cpp` picked up a `Query` title on user bubbles and layout
  fixes matching the existing `Response` title styling. A fresh isolated
  Microsoft Edge run then completed local-administrator login, project/chat
  creation, Ready-model selection, a real `qwen25-coder-1.5b-q4km` request,
  streamed assistant rendering, and graceful shutdown. That run exposed and
  fixed a first-message race: foreground generation now waits for the same
  bounded background pre-warm load instead of rejecting `RunnerState::starting`,
  and duplicate warm requests are coalesced. The optional whisper.cpp adapter
  remains optional and is not part of the browser-chat exit criterion.
  On 2026-08-06, three further web UI defects/gaps were fixed: (1)
  `renderInline()`'s Markdown renderer had no link handling at all, so a
  reply containing `[label](url)` or a bare `http(s)://` URL (seen from
  Llama-family replies, but affecting every model equally) rendered as
  literal bracket/paren text instead of a clickable link -- fixed by a new
  `linkify()` pass that converts both forms to real anchors before the
  existing bold/code/math inline passes run; (2) the global
  `#systemErrorBanner` (`showSystemError()`) was a full-viewport-width fixed
  top banner, which on the Machine Learning pages' own longer failure
  messages visually overran the rest of the page -- replaced with a
  width-capped, scrollable, bottom-corner floating bubble that fades out and
  hides itself 10 seconds after the most recent call, with its own Copy
  button alongside the existing dismiss button; (3) the composer's model
  picker gained a settings panel (gear button, opens automatically on every
  model selection/switch) with Effort (low/medium/high) and Thinking
  (off/on) controls, persisted per model id in browser `localStorage` and
  restored whenever that model is chosen again. `POST
  /api/v1/chats/{id}/messages` now accepts optional `effort`/`thinking`
  fields (default `medium`/`off`, validated, backward compatible): for a
  reasoning-capable architecture (`qwen*`, `gpt-oss` --
  `architecture_supports_reasoning_directives()`) they are folded into the
  turn's own text as a real reasoning directive (Qwen's documented
  `/think`/`/no_think` suffix plus a depth hint, or a
  gpt-oss-harmony-style `Reasoning effort: <level>` header -- this project
  does not implement the full Harmony format, so this is an approximation of
  it, not a faithful reproduction); every other architecture instead gets a
  sampling-preset adjustment (temperature/top_p/max_tokens via
  `apply_sampling_preset()`) so the settings still do something honest
  rather than silently no-op-ing. `GET /api/v1/models` now also reports each
  model's `architecture` field so the panel can show which mode applies to
  the currently selected model.
- Phase 6: Complete (validated 2026-08-05) — immutable Hugging Face and
  GitHub Release URL policy, explicit licenses, persistent resumable jobs,
  pinned curl process isolation, journaled progress, SHA-256 promotion or
  quarantine, model-tree registration integration, CLI/API operations, and
  hardware recommendations are implemented. The exit check used the official
  immutable `ggml-org/tiny-llamas` `stories260K.gguf`: a 200,000-byte partial
  resumed to 1,185,376 bytes, matched SHA-256
  `047bf46455a544931cff6fef14d7910154c56afbc23ab1c5e56a72e69912c04b`,
  atomically replaced the `.part`, populated the verified cache, and scanned
  `Ready` from isolated runtime/models roots. This validation also fixed CLI
  configuration precedence so every settings-backed operational command uses
  the same approved `MASTERAI_*` overrides as `serve`.
- Phase 7: Complete (validated 2026-08-02) — quick, standard, and extended
  executable suites, exact prompt/settings capture, timing/token/memory and
  quality metrics, durable compatible comparisons, quality-then-speed
  recommendations, CLI/API operations, and browser results are implemented.
  The same-host comparison exit check now has recorded evidence: `masterai
  benchmark-model` ran the quick suite against two real candidate GGUFs on
  the same host/hardware id — `qwen25-coder-3b-q4km` (2/2 cases passed, 400
  generated tokens) and `llama32-3b-instruct-q4km` (2/2 cases passed, 499
  generated tokens) — both against the same pinned prompt suite hash
  (`a1231a94ff8b786ea9b400f3bcc14bac7dd431b913d88a0da9a6e86d6a63fdee`),
  confirming reproducibility, and both persisted via `BenchmarkStore` in the
  runtime record store for later comparison-UI/CLI retrieval.
- Phase 8: Complete (validated 2026-08-05) — the pinned `2025-11-25`
  JSON-RPC dispatcher, authenticated/project-bound tools and resources,
  newline-delimited `stdio`, Streamable HTTP POST, cancellation notification,
  and native conformance tests are implemented. A live independent inspector
  authenticated with a short-lived, project-bound bearer token, negotiated
  `2025-11-25`, listed four authorized tools, observed only
  `masterai://project/phase5-browser`, and revoked the token afterward.
- Phase 9: Complete — the separate durable outbound registry pins stdio
  executable digests and transport limits; native stdio process isolation and
  loopback Streamable HTTP clients enforce tool/project/scope/per-call approval,
  cancellation, timeouts, response bounds, OS-secret credential references,
  and hash-chained audit. Release policy deliberately keeps legacy SSE disabled.
- Phase 10: Complete (validated 2026-08-05) — per-IDE OS-protected tokens,
  secret-free connection profiles, generic chat/context/cancellation
  contracts, deterministic diagnostics, read-only diff preview, a VS Code
  extension host, and a Visual Studio 2022 VSIX host are implemented. Both
  real hosts launched the native `mcp-stdio` profile, negotiated `2025-11-25`,
  and discovered four project-bound tools. The authorized host glue remains
  isolated under `integrations/`; native product behavior remains C++17.
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
- Phase 16: Complete (validated 2026-08-05) — a `RetrievalPlanner`
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
  strategies `RetrievalPlanner` can choose from today. The authored set in
  `docs/performance/phase-16-retrieval-evaluation.md` passes 2/2 hybrid hits
  versus 0/2 literal full-query hits with zero deadline or context-budget
  violations; membership/policy invalidation is satisfied by construction
  (see above) and by targeted tests.
- Phase 17: Complete (validated 2026-08-05) — a `CacheManager` caches
  Phase 16 retrieval results (the only Phase 17 segment with a real producer
  today) behind versioned keys that embed user/project identity, a policy
  generation, and the current index generation, so file, membership, and
  policy changes invalidate reachability structurally rather than through
  active purging; disk entries are atomic and checksum-quarantined, and
  administrator-only status/trim/clear routes exist alongside
  `GET /api/v1/system/memory`. The production-path benchmark documented in
  `docs/performance/phase-17-cache-benchmark.md` builds a 64-file index and
  proves stable output plus lower cached preparation time over 32 repetitions.
- Phase 18: Complete (validated 2026-08-02) — a `PromptSessionManager`
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
  for every `--parallel` slot at load time — a real run against this
  project's own 4 GiB-VRAM `qwen25-coder-3b-q4km` host with the previously
  planned `session.maxSlots=4` default and a 131072-token
  `inference.chatContextLength` was rejected outright by that admission
  check (`generation_failed: request exceeds the active RAM ceiling or OS
  safety reserve`) before the runner even loaded, confirming the reservation
  is real and enforced, not advisory; the run that produced the exit
  evidence below used `session.maxSlots=1`. New
  `test_phase_eighteen_prompt_session_reuse` covers: no fabricated reuse for
  a chat with no prior turn; reuse granted for an identical-fingerprint,
  prefix-extending turn; reuse refused on fingerprint mismatch; reuse refused
  on a non-prefix (edited-turn) change; least-recently-used eviction once the
  slot pool is full; a released (cancelled-turn) session never being offered
  back; `reset()` clearing every entry; and the launch spec actually exposing
  the configured slot count via `--parallel`. Complete (validated
  2026-08-02): the same-host repeated-turn exit criterion now has real
  evidence — two authenticated turns sent to the same chat against
  `qwen25-coder-3b-q4km` with `session.enabled=true`, `session.maxSlots=1`.
  The API reported the second turn's full templated prompt at 68 tokens, but
  `runtime/logs/runner-qwen25-coder-3b-q4km.log` shows the runner's own
  `prompt eval time` line evaluated only 25 tokens for that turn (`n_tokens =
  97` after, versus `n_tokens = 43` after the first turn) — the prior turn's
  43-token prefix was served from the reused KV-cache slot instead of being
  re-evaluated, matching the byte-prefix-match design exactly. Output was
  correct and coherent on both turns, no stale policy or cross-boundary reuse
  occurred (single chat, single owner), and the memory-cap enforcement above
  demonstrates the reservation path is not bypassed. Chat reply length and
  context length also
  moved off hardcoded constants in this change: the previous unconfigurable
  512-token reply cap (well below the runner's own 32768-token policy
  ceiling) cut long replies off before the model's own end-of-turn token;
  `inference.chatMaxReplyTokens` (default 8192) and
  `inference.chatContextLength` (default 4096, matching the prior constant)
  are now configurable.
- Phase 19: Implemented, comparative exit evidence recorded (2026-08-05) —
  a `CalibrationService`
  (`src/calibration.cpp`) drives a real `RunnerSupervisor` through a cold
  load and two `generate()` calls (near-zero-token for prompt-evaluation
  timing, a short generation for generation timing) and persists the result
  as a `TuningProfile` in a new `TuningProfileStore`, keyed by host hash
  (`sha256_hex(hardware_info_json(...))`, matching the Phase 13 baseline
  hashing convention), model SHA-256, backend executable digest, and build
  id. Any change to that identity makes the stored profile unreachable, so
  `CalibrationService::resolve()` falls back to `safe_default_profile()`
  (the minimal/balanced/performance starting points from section 25.2)
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
  prefix. GPU utilization and thermal-trend probing are not yet wired into
  the calibration pipeline. The vendor-SDK blocker is resolved (2026-08-13,
  see ADR-0001): NVIDIA NVML and AMD ADLX are now an approved, narrow
  exception, both consumed by dynamic-loading the driver-installed vendor
  DLL at runtime (`src/gpu_vendor.cpp`, `probe_gpu_vendor_telemetry()`) —
  AMD additionally through vendored public headers under
  `third_party/ADLX` — so neither vendor binary is linked and a host
  without that vendor's driver fails closed. This telemetry is now wired
  into `CalibrationService` (2026-08-13): `TuningProfile` gained
  `gpu_telemetry_available`/`gpu_vendor`/`average_gpu_utilization_percent`/
  `peak_gpu_temperature_celsius`, sampled at the same 50 ms cadence as the
  existing peak-resident-memory sampler across `calibrate()`'s real
  generation call and averaged/maxed over the sampling window; a host
  without an available vendor SDK simply reports `gpu_telemetry_available:
  false` rather than a fabricated value. A real run against
  `qwen25-coder-3b-q4km` on the GTX 960M measured 39.41% average GPU
  utilization and a 71°C peak temperature during generation, confirmed via
  both the `calibrate` CLI (now prints these fields) and the persisted
  `TuningProfile` record. `TuningProfileStore::restore()` also gained
  tolerant handling of the pre-2026-08-13 26-field schema (defaults the new
  fields rather than throwing) after this exact change hit a real crash on
  startup with genuine pre-existing calibration evidence on disk — a schema
  version bump to a store that eagerly parses every persisted record at
  startup must not take the whole control plane down when an old record's
  build_id would never have matched anyway.
  `masterai calibrate config/settings.json
  qwen25-coder-3b-q4km balanced` was run against the real GTX 960M/
  `qwen25-coder-3b-q4km` host and produced a persisted `TuningProfile`: cold
  load 43.11 s, prompt-evaluation 2.48 s, generation 6.12 s, peak resident
  memory ~2.14 GiB, 2,958 page faults, 9.62% average CPU. This establishes
  the required calibration record for at least one Qwen-class 3B GGUF and
  confirms the pipeline runs end to end on real hardware without CPU
  fallback or an admission failure at the `balanced` profile. The same-host
  comparison in `docs/performance/phase-19-qwen3b-matrix.md` now records
  58.12 prompt tok/s and 12.43 generation tok/s with GPU permitted versus
  CPU-only 29.49 and 6.48 tok/s. Live GPU memory rose by 2,150 MiB at ready
  state, proving actual residency rather than only a requested offload flag.
  Both fixed workloads generated all 64 requested tokens without cancellation
  or allocation failure. Broader semantic-quality scoring remains forward
  work and is not inferred from token completion alone.
- Phase 20: Complete as an optional evidence/admission layer (validated
  2026-08-05) — `AdvancedOptimizationRegistry` now strictly validates and
  durably restores the full per-candidate evidence contract, separates
  evidence recording from explicit admission, rejects unavailable or
  regressing implementations and unverified fallbacks, independently
  disables candidates, preserves the safe Phase 19 profile, and exposes the
  audited administrator-only `GET`/`POST
  `/api/v1/performance/advanced-optimizations` contract. The current native
  suite passed, and a live isolated server returned all six candidates,
  reported the safe profile available, persisted a disabled decision across
  restart, and shut down gracefully. No optional candidate is currently
  admitted: candidate implementations and measurements remain independently
  gated, exactly as this optional phase requires, rather than being
  misrepresented as enabled throughput.
- Phase 21: Implementation complete (2026-08-05) — native
  asynchronous storage and prefetch engine. `Win32OverlappedFileReader`
  (IOCP) on Windows and a bounded `PosixPreadPoolReader` fallback on POSIX
  (no Linux `io_uring` adapter), read coalescing, adaptive queue depth keyed
  to a measured `StorageLatencyProfile`, and per-request cancellation are
  implemented and wired into model manifest and index segment reads, with
  bounded parallel batch reads, kernel cancellation, and automatic fallback
  to the prior blocking path. Priority A.
- Phase 22: Implementation complete (2026-08-05) — hierarchical
  content and model-data caching. `CacheCategory` expands from Phase 17's six
  values to the plan's full 17-category L0–L5 set; each category's
  `CacheManager::State::Segment` gains streaming/probationary/protected/pinned
  segmented eviction (a large one-time scan can only evict other
  probationary entries, never the protected working set) plus a single-hash
  frequency-sketch admission gate and immutable reference-counted resident L1
  that refuse a brand-new candidate rather
  than displacing a demonstrably hotter protected entry, and short-lived
  version-bound negative caching (`put_negative()`/`is_negative()`).
  Immutable, checksummed, atomically-published entries and corruption
  quarantine already existed from Phase 17 and are unchanged. Priority A/B.
- Phase 23: Implementation complete (2026-08-05) — tokenization,
  prompt-template, and prompt-fragment caching. A `CacheCategory::tokenization`
  producer/consumer around `RunnerSupervisor::tokenize()`, compiled
  process-lifetime chat-template plans, `PromptSegment`-based segmented
  prompt assembly, a restricted role/route/key intern table, and an extended
  `PromptSessionManager::try_reuse()` reporting exact reusable token and byte
  counts, divergence offset, invalidation reason, and a configurable
  prefix byte ceiling are implemented. Priority A.
- Phase 24: Implementation complete (2026-08-05) — staged
  adaptive retrieval fan-out. Filename/path and recent-change strategies,
  a deterministic request classifier, sticky-sufficiency staged fan-out
  over the existing bounded worker pool with priority tagging, a mutex-
  guarded in-flight-request join table (authorization/project-scoped, never
  cross-boundary), and reference-first (`ChunkReference`) candidate
  materialization gated on `ContextBudgeter` admission are implemented.
  Semantic-embedding, MCP-resource, call-graph, type-reference, git-diff,
  dependency-neighbour, and conversation-memory strategies remain declared
  but disabled pending their adapters, with skip reasons disclosed on
  `QueryTrace`; the authored Release evaluation measured 213 us total/145 us
  high case versus 736 us/401 us for the Phase 16 bounded-fan-out baseline.
  Priority B.
- Phase 25: Implementation complete; activation remains calibrated and
  default-off (2026-08-05) — weighted-fair
  priority scheduling and backpressure. `RequestScheduler`
  (`src/scheduler.cpp`) implements the plan's eight priority classes
  (cancellation/shutdown through maintenance), per-class weight/queue-depth/
  residence-time/concurrency/memory-allowance policy, deficit-round-robin
  weighted dequeue with cancellation always preempting, and backpressure
  that evicts the lowest-priority still-queued (never running) ticket first
  when a global concurrency ceiling is set. The live chat path now reserves,
  queues, reports status, waits cancellably, and completes through that
  scheduler. Compatible llama.cpp runners receive `--cont-batching` only
  after the Phase 20 registry admits the optimization; the supervisor then
  permits the calibrated parallel slot count. Priority C;
  superset of the Phase 20 continuous-batching candidate.
- Phase 26: Implementation complete (2026-08-05) — model loading/
  mapping modes, selective pre-touch, cancellable background warm-up, and a
  warm-model state machine. `ModelLoadMode`/`PreTouchLevel` are selected
  using Phase 21's `StorageLatencyProfile` plus RAM-ceiling evidence; a
  `WarmModelState` machine is layered onto (not a replacement for) the
  existing `RunnerState`/`ModelState` pair via an explicit, regression-tested
  translation table; background warm-up is cancellable and yields under
  memory pressure. Metadata, first-use, and distributed layer-window
  pre-touch use bounded read-only mappings; full pre-touch remains the
  backend `--mlock` path. Live use/wait/pin signals are exposed through
  administrator routes. Priority A (mapping and storage-aware
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
- Phase 30A: Implemented (2026-08-02) — explicit CPU-only/GPU-disabled
  low-memory operation, as an integration/hardening pass over the existing
  Phase 14/19/21/26/27/30 controls rather than a second memory manager or
  inference pipeline. `hardware.acceleratorPolicy` (`auto`/`cpu_only`/
  `gpu_allowed`) is validated end to end with fail-closed launch (zero GPU
  layers, CPU KV placement, GPU-hiding runner environment variables;
  `CalibrationService` rejects rather than silently zeroes a stale
  GPU-offload profile under `cpu_only`). A `MemoryCategory::runner_weights`
  admission check (`ensure_model_loaded()`'s `admit_runner_weights()`) rejects
  a model with a concrete `assess_model()`-backed reason before the runner
  process is ever started, and releases its lease on every unload path.
  `MemoryPolicy::maximum_active_inference` is now a real admission gate on
  `send_chat_message()` instead of a validated-but-unread field. The new
  `MemorySweeper` background thread (mirroring `ProjectWatcher`'s pimpl/
  worker-thread shape) periodically applies `RunnerSupervisor::
  apply_idle_timeout()` (unconditionally under a profile with
  `keep_idle_model=false`, otherwise once pressure reaches `elevated`) and
  `CacheManager::trim()` once pressure reaches `high`. Administration
  visibility was added to `GET /api/v1/runner/status` (requested-vs-actual
  GPU layers, load-time accelerator policy, unload countdown) and
  `GET /api/v1/system/memory` (pagefile/swap headroom, process commit bytes,
  hard-fault count, configured idle-unload seconds), and an
  administrator-only `GET`/`POST /api/v1/admin/config` round-trips the live
  `settings.json` through the existing `ConfigurationManager` (surfaced in
  the web UI's Settings → System configuration panel). Real-model benchmark
  evidence (`auto` vs `cpu_only` cold/warm load, peak resident memory, GPU
  utilization/temperature) is now recorded (2026-08-13, see
  `docs/performance/phase-19-qwen3b-matrix.md`'s second table): both states
  ran `qwen25-coder-3b-q4km`'s `minimal` calibration workload on the same
  GTX 960M/i7-6700HQ host to completion and persisted cleanly (the earlier
  2026-08-05 run's `cpu_only` persistence gap, caused by an orphaned
  `llama-server.exe` child process left bound to the runner port by a prior
  failed attempt and independently discovered/fixed during this pass, no
  longer applies) — `auto` completed prompt evaluation in 1.78 s and
  generation in 6.26 s at 2.29 GiB peak resident memory with 39.9% average/
  72°C peak GPU utilization; `cpu_only` completed prompt evaluation in
  7.54 s (4.2x slower) and generation in 15.47 s (2.5x slower) at 3.60 GiB
  peak resident memory (1.57x more, consistent with no GPU offload) and
  measurably cooler/idler GPU telemetry (23.5%/60°C, residual monitoring
  overhead rather than active generation work). `cpu_only` never attempted
  GPU allocation and completed cleanly, closing this phase's last
  outstanding deliverable. Priority A.
- Phase 31: Implemented (2026-08-13) — the five storage tiers, a real
  `ScratchVolumeManager` (per-job directory/quota, global quota, atomic
  publish, crash-recovery journal, orphan cleanup, free-space reserve), the
  hard RAM-tier prohibition enforced in code via
  `durable_data_class_allows_ram_tier()`, separate (non-conflated)
  physical/commit/pagefile/page-fault/model-resident memory accounting via
  `probe_memory_accounting()`, and best-effort Windows filesystem-integrity
  detection (compression/encryption/dedup/virtual-disk/network-redirection)
  via `probe_filesystem_integrity_flags()`. `recommend_storage_placement()`
  derives a model-placement recommendation from measured evidence, not
  assumption. **Tier migration tooling is now also implemented
  (2026-08-13)**: `migrate_durable_file()` (`src/scratch_storage.cpp`)
  relocates an already-published durable file to a new tier's directory,
  verified by a real SHA-256 digest match between source and staged copy
  before the atomic rename, and refuses (throws) a destination that
  measures as Tier R (RAM-backed) storage for anything but
  `reconstructable_scratch` -- the same hard prohibition every other Phase
  31 entry point enforces. Exposed administrator-only via `POST
  /api/v1/system/storage/migrate`. **The central migration manifest is now
  also implemented (2026-08-13, this pass)**, closing the last Priority B
  gap: `DurableFileManifest` (`src/masterai.hpp`/`src/scratch_storage.cpp`)
  is a durable, `RecordStore`-journaled log of every `migrate_durable_file()`
  move (source path, destination path, SHA-256, data class, timestamp),
  chained so a file migrated more than once still resolves from any of its
  former paths. `migrate_durable_file()` now records every move into it,
  installed process-wide via `install_global_durable_file_manifest()` so
  `resolve_durable_path()` transparently finds the current location for any
  caller still holding a pre-migration path -- wired into the one concrete
  consumer the plan names: `LlamaCppAdapter::build_launch_spec()`
  (`src/models.cpp`) resolves a Model Registry entry's model file through it
  before launch (falling back to the unmigrated path's existing directory-
  containment check when nothing was ever migrated, and to the re-verified
  SHA-256 digest -- independent of location -- as the real security
  property once it has been), and `RunnerSupervisor::load()`'s pre-touch
  path (`src/inference.cpp`) does the same. Administrator-visible via `GET
  /api/v1/system/storage/manifest`. The migrate endpoint's response field
  changed from `callerMustUpdateReferencingRecord: true` to
  `recordedInManifest: true` to match. Priority A/B, both now closed.
- Phase 32: Implemented, real dual-model launch path added, evidence-pending
  (2026-08-13) — `check_draft_target_compatibility()`, `SpeculativeDecodingStats`
  (rolling acceptance-rate tracking), and `decide_speculative_decoding_for_request()`
  (`src/speculative_decoding.cpp`) remain the real, independently testable
  decision logic covering the plan's full deliverable list (exact
  compatibility checks, measured-acceptance-rate gating, memory-fit gating,
  short-request skip, queued-draft-runner skip, sampling-compatibility
  gating, thermal gating), unchanged in behavior. This pass closes the
  specific gap the phase's original entry named: `LaunchTuning` gained
  `speculative_draft_model_file`/`speculative_draft_gpu_layers`, and
  `LlamaCppAdapter::build_launch_spec` (`src/models.cpp`) emits llama.cpp
  server's documented `--model-draft`/`--gpu-layers-draft`/`--draft-max`/
  `--draft-min` flags when a caller sets them -- the real dual-model
  (draft+target concurrently resident, one llama-server process) launch
  path that did not exist before. `select_speculative_draft_candidate()`
  picks the smallest `check_draft_target_compatibility()`-accepted model
  from the verified/ready registry. `SpeculativeDecodingPairEvidenceStore`
  (new, `src/speculative_decoding.cpp`) is a durable, administrator-
  submitted measured acceptance rate per (target, draft) pair -- separate
  from `AdvancedOptimizationRegistry`'s one-time global admission evidence
  (Phase 20), since `decide_speculative_decoding_for_request()` needs a
  real number for the *specific* pair, and this codebase never fabricates
  a starting assumption for an unproven pair. `server.cpp`'s
  `ensure_model_loaded()` wires all of this together at model-load time,
  gated exactly like `continuous_batching` on
  `AdvancedOptimizationRegistry::is_enabled("speculative_decoding")`
  (`implementation_available` flipped to `true` for this feature in
  `optimization_registry.cpp`, since a real implementation now exists;
  admission still requires an administrator to separately record evidence
  and admit) -- `admit_runner_weights()` extended to reserve the
  draft model's weights too, so the combined-memory-fit check is a real
  `MemoryBudgetManager` reservation, not a second parallel heuristic. New
  administrator-only `GET`/`POST /api/v1/performance/speculative-pairs`
  routes record/list the per-pair evidence. **The sampling-compatibility
  gate is corrected (2026-08-13, this pass)**: the field previously named
  `sampling_is_greedy_or_deterministic` and hard-failed every live chat
  request (since this codebase's lowest sampling preset is temperature
  0.15, never exactly greedy) has been replaced with
  `sampling_supported_by_speculative_verification`. llama.cpp's own
  speculative-decoding verification is the general rejection-sampling
  algorithm (Leviathan et al.), mathematically valid for any temperature/
  top-p/top-k/repeat-penalty sampling -- it does not require greedy
  decoding, and this control plane's `GenerationOptions` (`masterai.hpp`)
  exposes no grammar/logit-bias/json-schema field that could make a
  request actually incompatible with it. `ensure_model_loaded()` now sets
  this field `true`, so speculative decoding activates for live chat
  traffic under this codebase's real sampling presets once an
  administrator has admitted the feature and recorded a qualifying
  measured acceptance rate for the resolved draft/target pair -- the
  "current default chat sampling presets ... mean the gate will not
  currently let it activate for live chat traffic" limitation this note
  previously carried is closed. Priority C. Exit criterion status: still
  explicitly unvalidated on real hardware, not claimed -- "generation
  throughput improves on representative prompts" has a real, now-reachable
  execution path but no measured run against it yet; that measurement
  requires an administrator to actually run a calibration/benchmark pass
  on real hardware, which this control plane never does on its own.
- Phase 33: Implemented (2026-08-13) — both halves. The local-only
  multi-runner orchestration half (2026-08-13, earlier pass):
  `LocalRunnerConfig`/`LocalRunnerPool` (`src/runner_pool.cpp`) generalize
  the existing single-runner `RunnerSupervisor` supervision pattern to an
  opt-in pool of N concurrent local runner processes (per-GPU, CPU+GPU
  split, or dedicated embedding/router/benchmark runners), routed by
  resident-model match, runner health/state, capability, Phase 2
  project-bound authorization, and priority; a failing runner is isolated
  (marked unhealthy, never taken down with the control plane) and a retry
  is only ever attempted when `retry_is_semantically_safe()` confirms
  nothing already reached the caller or was persisted; the chat-generation
  and dedicated-embedding request paths in `server.cpp` are wired through
  it, and the runner that actually served each query is recorded on the
  Phase 13 query trace (`QueryTrace::runner_id`) and exposed
  administrator-only at `GET /api/v1/runner/pool`. Single-runner mode is
  unchanged and remains the default: an empty (the default)
  `localRunnerPool` setting means every pre-Phase-33 behavior is untouched.
  The intranet-worker half (2026-08-13, this pass): `PrivateCertificateAuthority`/
  `IntranetWorkerConfig`/`WorkerModeConfig`/`IntranetWorkerPool`/
  `WorkerListener` (`src/intranet_worker.cpp`) add mutual-TLS, pinned-PKI
  routing to administrator-approved remote worker machines, using OpenSSL
  as this codebase's first (and only) vendored TLS/crypto dependency --
  every other network path here is either loopback-only or explicitly
  fail-closed without it (see `mcp_outbound.cpp`'s `http_exchange()`
  comment); `MASTERAI_HAS_OPENSSL` gates the whole feature closed at
  runtime, not build-time, when OpenSSL development files are unavailable
  (`scripts/CMakeLists.txt`), the same optional-dependency fail-closed
  precedent already established for PAM. A worker's certificate is
  verified against a pinned private CA (never the system trust store) AND
  against a pinned leaf-certificate digest (defense in depth, reusing
  Phase 9's outbound-executable-digest-pinning convention); the worker
  equally verifies this control plane's own client certificate (true
  mutual TLS). Signed worker registration is administrator-driven, not
  self-service: `POST /api/v1/system/pki/initialize` generates the private
  CA once (refuses to overwrite an existing one), `POST
  /api/v1/system/pki/workers` signs one worker certificate, and an
  administrator copies the resulting certificate/key to the physical
  worker machine out of band. Model-digest verification is real:
  `IntranetWorkerPool::refresh_status()`/`refresh_all()` read back each
  worker's self-reported loaded-model sha256 over the already-authenticated
  channel and only ever make that worker selectable once the digest
  matches a model this control plane's own registry also knows under the
  same id (`GET`/`POST /api/v1/worker/pool[/refresh]`, administrator-only).
  A worker's model is fixed by its own local configuration -- there is no
  "load this model on that worker" RPC, closing off a remote
  resource-exhaustion class an admin-driven "control every worker's model
  remotely" design would otherwise open. `WorkerListener` is this
  codebase's one deliberate, narrowly-scoped exception to
  `HttpServer`'s loopback-only constraint (`AppConfig::worker_mode`,
  disabled by default): it speaks only the small mutually authenticated
  worker protocol (`GET /worker/status`, `POST /worker/generate`,
  `POST /worker/embed`), never the administrator HTTP/API surface, and
  refuses any connection whose client certificate is not on its approved
  digest allowlist. **Automatic remote-worker failover is now wired into
  the live chat-generation dispatch path (2026-08-13, this pass)**, closing
  the gap this note used to describe as the phase's one remaining scope
  cut: `server.cpp`'s chat handler gained a `try_remote_worker_failover()`
  helper, tried whenever the default runner (`inference->generate`) or a
  selected local pool runner (`LocalRunnerPool::generate`) throws, applying
  the identical `retry_is_semantically_safe()` rule the existing local-pool
  retry already used -- only when nothing from the failed attempt has
  reached the client yet, since persistence only happens after `generate()`
  returns successfully. It selects a healthy worker via
  `IntranetWorkerPool::select_worker()` (already-verified model-digest
  match, project authorization, capability, priority) and dispatches
  through `IntranetWorkerPool::generate()`; on the local-pool path, remote
  failover is tried before the existing default-local-runner fallback, not
  instead of it, so a request still degrades to the same safe local retry
  when no remote worker is available or the remote attempt itself fails.
  The runner identity recorded on the query trace becomes
  `"intranet-worker:<id>"` when a remote worker actually served the
  request, keeping `QueryTrace::runner_id` honest about who generated the
  reply. Honest scope note carried forward: this failover applies to the
  chat-generation dispatch path specifically (the concrete case the
  original gap named); the separate RAG one-shot generation path
  (`execute_rag_generation()`) and inference-endpoint dispatch still use
  only the default local runner.
- Phase 34: Implemented at a scoped-down level (2026-08-13) — `AdaptiveController`
  (`src/adaptive_controller.cpp`) extends Phase 19 `CalibrationService`'s
  advisory profiles into a live, bounded controller. Every stability
  control the plan requires is real and independently testable in
  `evaluate()`: minimum dwell time, cooldown, and max-changes-per-interval
  (epoch-second bookkeeping), bounded step size (`PerformanceCeilings::max_step_percent`),
  rolling measurement (a 5-sample pressure window), a confidence
  requirement derived from that window (an adjustment only applies at
  confidence >= 0.6), and safe rollback (`AdaptiveController::rollback()`
  reverts to the `MemoryPolicy` active immediately before the last applied
  change). All eight named modes (Minimal Memory, Balanced, Lowest
  Latency, Maximum Throughput, Battery Saver, Quiet/Thermal Conservative,
  Administrator Custom, Automatic) are implemented; `automatic` picks the
  best-fit mode each cycle from live `MemoryStatus`/`SchedulingClassStatus`
  signals, `administrator_custom` applies configured ceilings with
  automatic tuning fully disabled (the plan's exact requirement). Honest
  scope note: `evaluate()` computes and discloses a proposed value for
  every knob the plan lists, but only ever *applies* the subset wired to a
  genuinely live-mutable target in this codebase -- the new
  `MemoryBudgetManager::set_policy()` (inference concurrency, queued-
  inference ceiling, index worker count, default context tokens). Every
  other named knob (read queue depth, prefetch distance, cache quotas,
  batch size, idle-unload time, warm-up policy, thread count, NUMA policy,
  GPU offload, KV placement, background-job rate) has no live setter
  anywhere in this codebase yet -- still computed and surfaced as a
  recommendation (`proposedNotYetApplied` on the JSON report, visible on
  the Phase 35 administration page), matching the same "computed,
  disclosed, not yet wired to an automatic call site" pattern Phase 26/28
  already established (e.g. Phase 28's topology-aware thread-pinning
  primitive). Administrator-only routes: `GET /api/v1/performance/adaptive`,
  `POST .../mode`, `POST .../ceilings`, `POST .../rollback`.
- Phase 35: Implemented at a scoped-down level (2026-08-13; extended
  2026-08-13, this pass) — one consolidated "Performance" administration
  page (`/app/performance`, `src/web_ui.cpp`) rather than the plan's full
  thirteen-route enumeration, condensed to Overview/Adaptive Controller,
  Local Runner Pool, Intranet Worker Pool, and now (this pass) real Memory,
  Caches, Storage (including the Phase 31 tier-migration manifest),
  Scheduling, Advanced Optimizations, and Calibration sections. Every
  figure it renders comes directly from the real routes those phases
  already expose (`GET /api/v1/performance/adaptive`, `GET
  /api/v1/runner/pool`, `GET /api/v1/worker/pool`, `GET
  /api/v1/system/memory`, `GET /api/v1/system/cache`, `GET
  /api/v1/system/storage`, `GET /api/v1/system/scratch`, `GET
  /api/v1/system/storage/manifest`, `GET /api/v1/system/scheduler`, `GET
  /api/v1/performance/advanced-optimizations`, `GET
  /api/v1/performance/recommendations`) -- nothing on this page is a
  separately maintained display-only value, and there is no single opaque
  "turbo" switch (mode selection is one of eight named, disclosed modes,
  and every applied/recommended adjustment is listed individually with its
  reason and confidence). Cache trim/clear and scratch-cleanup are wired as
  real buttons against their existing administrator-only POST routes.
  Deferred to a later pass: Live Requests and Models and Runners as pages
  distinct from the Overview's own summary, Worker Pools as a page distinct
  from this consolidated section, Benchmarks, Regression History, the
  Query Traces page, the Runner Configuration page, and the Model
  Comparison page -- none of those has a dedicated telemetry route to
  render yet (Retrieval likewise: `RetrievalPlanner` exposes no status/
  stats accessor in this codebase today), so this stays honest about what
  is and is not real rather than fabricating a page with nothing behind
  it.
  Extended 2026-08-17: a second, narrower "Memory Status" widget added to
  the Model Inventory page (`/app/models-inventory`, `src/web_ui.cpp`),
  administrator-only, so an administrator does not have to leave the
  models list to see whether a model is RAM-blocked and do something about
  it. Auto-refreshes every 5 seconds from the same real routes the
  Performance page already reads (`GET /api/v1/system/resources`, `GET
  /api/v1/system/memory`, `GET /api/v1/system/cache`, `GET
  /api/v1/system/scratch`) — system RAM, system virtual memory (pagefile),
  and MasterAI's own cache/scratch usage. A new "Clean Memory" action (`GET`/
  `POST /api/v1/system/memory/clean`, `MemoryCleanupTracker` in
  `src/memory.cpp`) lets an administrator choose which real reclaim steps to
  run — trim bounded caches (`CacheManager::trim()`), remove orphaned
  scratch/temp files (`ScratchVolumeManager::recover_orphans()`), release
  MasterAI's own process memory back to the OS (`release_process_working_set()`,
  `EmptyWorkingSet` on Windows in `src/platform.cpp`; a documented no-op
  elsewhere), and optionally unload the currently loaded model
  (`RunnerSupervisor::unload()`) — and watch genuine step-by-step percent
  progress while it runs, polled from a single-flight tracker rather than a
  fabricated animation. On completion the widget re-fetches both itself and
  `GET /api/v1/models` so a model that was previously RAM-blocked is shown
  as ready immediately, without a page reload — `model_inventory()`
  already re-probes hardware on every call, so no caching needed
  invalidating. Honest scope note: MasterAI cannot force another process or
  the OS itself to release memory, only shrink its own footprint; the
  widget's copy and this note both say so rather than implying a
  system-wide sweep.
  Extended again 2026-08-17 (same pass): added the true system-wide Windows
  memory-manager options a dedicated memory-cleaner tool would offer,
  alongside the process-scoped ones above — trim other applications'
  working sets (with a configurable minimum-size threshold and a
  protect-the-foreground-application option), flush the modified page
  list, purge the standby list, purge only low-priority standby pages,
  empty system/service working sets, and clear the system file cache. Each
  is a real primitive in `src/platform.cpp`
  (`trim_other_process_working_sets()`, `flush_modified_page_list()`,
  `purge_standby_list()`, `purge_low_priority_standby_list()`,
  `empty_system_and_service_working_sets()`, `clear_system_file_cache()`),
  the last five wrapping the same undocumented-but-stable
  `NtSetSystemInformation(SystemMemoryListInformation, ...)` primitive
  Sysinternals RAMMap uses, or (for the file cache) the documented
  `SetSystemFileCacheSize()` shrink-then-restore technique. Every one of
  these genuinely requires the MasterAI process itself to be running
  elevated (Administrator) — `acquire_privilege_if_available()` enables the
  underlying Windows privilege (`SeProfileSingleProcessPrivilege`,
  `SeIncreaseQuotaPrivilege`, or `SeDebugPrivilege`) only if the process
  token actually holds it, and every function fails closed (returns
  false/0) rather than crashing or silently pretending to have worked when
  it doesn't. `MemoryCleanupResult::privilege_denied_steps` names exactly
  which requested steps were skipped and why, surfaced verbatim in both the
  API response and the widget's completion summary — trimming other
  processes (which only needs ordinary same-user process access, not
  elevation) was confirmed working end-to-end in an unelevated test run;
  the five true kernel-level operations were confirmed to fail closed with
  the correct named reason in that same run, since verifying their success
  path requires MasterAI itself to be launched elevated.
- Phase 36: Implemented at a scoped-down level (2026-08-17) —
  `PerformanceCertificationRunner`/`PerformanceCertificationStore`
  (`src/regression_gate.cpp`) run the existing Phase 7 `BenchmarkRunner`
  quality suite plus five real, independently callable regression check
  groups against this codebase's own live decision logic (runner
  attribution against `RunnerMetrics`, low-memory admission against
  `projected_resident_exceeds_safe_physical_capacity()`, prompt-cache
  reuse/refusal against a fresh `PromptSessionManager`, calibration
  identity-exact restore against `TuningProfileStore`, and smallest-
  capable-tier/Minimal-profile-residency routing against `ModelRouter`) —
  none fabricated. Recorded metrics per run: TTFT, total elapsed, prompt/
  generated tokens, peak resident memory, a post-run CPU sample
  (`probe_system_utilization()`), and the quality suite's pass rate. A run
  is compared only against the previous *accepted* run sharing its exact
  fingerprint (model/backend/hardware/settings/prompt-suite/cache-state/
  profile — `PerformanceCertificationRecord::fingerprint()`), so mismatched
  environments are never presented as a direct comparison, matching the
  plan's exit criterion. Also recorded per run: real queue wait (one
  `SchedulingClass::benchmark` ticket actually admitted through the same
  `RequestScheduler` production traffic contends on, timed end-to-end
  through `wait_until_ready()`, then immediately released) and real storage
  bytes read (the delta of this process's own cumulative disk-read counter
  from `probe_system_utilization()`, the same counter `CalibrationService`
  already uses, sampled before and after the run). `compare_against_baseline()`
  applies administrator-configurable `RegressionThresholds` — every
  threshold the plan names (max TTFT regression, max memory increase, min
  throughput, max quality regression, max CPU increase, max queue-wait
  increase, max storage amplification) — and a run is accepted only when
  every regression check group and every metric comparison passes; a
  rejected run is still persisted (silently dropping failing evidence would
  defeat the point of a regression gate). Administrator/developer routes:
  `GET`/`POST /api/v1/performance/certification`, `GET`/`POST
  /api/v1/performance/certification/thresholds`, rendered on the new
  "Benchmarks & Regression" Performance sidebar page
  (`/app/performance/benchmarks`, `src/web_ui.cpp`) — the "Benchmarks" and
  "Regression History" pages the Phase 35 status note named as deferred.
  Honest scope note (the same "declare the gap, don't guess" precedent
  Phase 30A/32 established, kept to exactly the part that is genuinely
  outside this session's control): the plan's full matrix additionally
  spans physical dimensions a single host cannot manufacture on demand —
  multiple storage media (HDD/SATA SSD/NVMe), multiple physical machines,
  and GPU-offloaded hardware that may not be present on the host running
  this build. Every dimension controllable in software ships real (cache
  cold/warm via `CacheManager::trim()`, sequential concurrency depth,
  prompt/context size via `BenchmarkProfile`, real queue wait and storage
  reads as above, and whatever accelerator mode the current launch actually
  used, recorded from `RunnerMetrics` rather than assumed); an
  administrator runs the matrix across whichever further axes their real
  hardware supports.
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
- Phase 41: Implemented at a scoped-down level (2026-08-04) — Data
  Labeling (section 14 below) and Data Preparation (section 15 below),
  both scoped down to identity, the dataset each task/job targets, and a
  lifecycle status (`LabelTask`/`LabelTaskStore` and
  `DataPreparationJob`/`DataPreparationJobStore`, `src/ml.cpp`), not the
  full feature lists (label guidelines, keyboard shortcuts, bulk labeling,
  suggested labels, confidence values, disagreement handling, consensus
  review, quality sampling, reviewer accuracy metrics, annotation history,
  label versioning; pipeline-step composition, execution, logging, and
  reproducibility record) that require actual label records or an
  executing pipeline to mean anything. Each labeling task's mode and each
  preparation job's operation are stored as free text rather than a closed
  enum, since sections 14 and 15 list 14 and 28 options respectively and
  growing. New administrator-only `ml.labels.view`/`ml.labels.manage`
  permissions gate `GET`/`POST /api/v1/ml/label-tasks`,
  `POST /api/v1/ml/label-tasks/{id}/status`, and
  `POST /api/v1/ml/label-tasks/{id}/delete`; new
  `ml.dataprep.view`/`ml.dataprep.manage` permissions gate the mirrored
  `/api/v1/ml/prep-jobs` routes. New "Data Labeling"
  (`/app/ml/label-tasks`) and "Data Preparation" (`/app/ml/prep-jobs`)
  entries under the Machine Learning sidebar in `src/web_ui.cpp` list,
  create, and move each through status. `test_machine_learning_data_labeling_lifecycle`
  and `test_machine_learning_data_preparation_lifecycle` cover the same
  permission/lifecycle/reload/remove guarantees as Phase 40's test, plus
  `create()` rejecting an empty target dataset id.
- Phase 42: Implemented at a scoped-down level (2026-08-04) — Training
  Jobs (section 16 below), scoped down to identity, the project/model/
  dataset each job targets, a free-text training method, and the section's
  full eleven-state lifecycle status (`TrainingJob`/`TrainingJobStore`,
  `src/ml.cpp`), not the compute/hyperparameter/scheduling field list
  (hardware allocation, container image, hyperparameters, environment
  variables, secrets references, checkpoint/logging/notification policy,
  resource/cost ceilings, failure-recovery strategy) that only means
  something once an actual training executor exists to consume it. New
  administrator-only `ml.training.view`/`ml.training.manage` permissions
  gate `GET`/`POST /api/v1/ml/training-jobs`,
  `POST /api/v1/ml/training-jobs/{id}/status`, and
  `POST /api/v1/ml/training-jobs/{id}/delete`. New "Training Jobs"
  (`/app/ml/training-jobs`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves jobs through status. The
  dashboard's `failedTrainingJobs` count is now real, drawn from Training
  Jobs in the `failed` state, replacing the always-zero placeholder.
  `test_machine_learning_training_jobs_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 41's tests, plus
  the dashboard's failed-count aggregate.
- Phase 43: Implemented at a scoped-down level (2026-08-04) — Evaluation
  Lab (section 23 below), scoped down to identity, the model/dataset each
  run targets, a free-text evaluation category, and a lifecycle status
  (`EvaluationRun`/`EvaluationRunStore`, `src/ml.cpp`), not the section's
  full surface (standard/custom benchmark sets, human evaluation, pairwise/
  blind model comparison, automated scoring, reviewer notes, and the
  numeric score itself) that only means something once an actual
  evaluation harness exists to produce one. category is free text rather
  than a closed enum, matching training_type's precedent above, since
  section 23 lists 24 evaluation categories and a real run can report more
  than one metric. New administrator-only `ml.evaluation.view`/
  `ml.evaluation.manage` permissions gate `GET`/`POST /api/v1/ml/evaluation-runs`,
  `POST /api/v1/ml/evaluation-runs/{id}/status`, and
  `POST /api/v1/ml/evaluation-runs/{id}/delete`. New "Evaluation Lab"
  (`/app/ml/evaluation-runs`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves runs through status.
  `test_machine_learning_evaluation_lab_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 42's test.
- Phase 44: Implemented at a scoped-down level (2026-08-04) — Experiment
  Tracking (section 25 below), scoped down to identity, the project/model/
  dataset an experiment relates to (dataset optional, mirroring Training
  Jobs' own optional model id), and a lifecycle status
  (`Experiment`/`ExperimentStore`, `src/ml.cpp`), not the section's full
  surface (source-code/configuration/container version, hyperparameters,
  random seed, hardware/runtime, metrics, checkpoints/logs/artifacts, tags,
  side-by-side comparison) that only means something once a real training/
  evaluation executor exists to attach it. New administrator-only
  `ml.experiments.view`/`ml.experiments.manage` permissions gate `GET`/
  `POST /api/v1/ml/experiments`, `POST /api/v1/ml/experiments/{id}/status`,
  and `POST /api/v1/ml/experiments/{id}/delete`. New "Experiment Tracking"
  (`/app/ml/experiments`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves experiments through status.
  `test_machine_learning_experiment_tracking_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 43's test.
- Phase 45: Implemented at a scoped-down level (2026-08-04) — the
  Fine-Tuning Interface (section 18 below), scoped down to identity, the
  project/model/dataset a job relates to (project id optional, model id
  and dataset id both mandatory since fine-tuning always adapts an
  existing base model with an existing dataset), a free-text method, and
  the same eleven-state lifecycle status Training Jobs uses
  (`FineTuningJob`/`FineTuningJobStore`, `src/ml.cpp`), not the section's
  full surface (base model version, adapter method, target layers,
  learning rate, batch size, epoch count, checkpoint strategy, validation/
  safety dataset, output model name/version) that only means something
  once a real fine-tuning executor exists to consume it. New
  administrator-only `ml.finetuning.view`/`ml.finetuning.manage`
  permissions gate `GET`/`POST /api/v1/ml/fine-tuning-jobs`,
  `POST /api/v1/ml/fine-tuning-jobs/{id}/status`, and
  `POST /api/v1/ml/fine-tuning-jobs/{id}/delete`. New "Fine-Tuning"
  (`/app/ml/fine-tuning-jobs`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves jobs through status.
  `test_machine_learning_fine_tuning_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 44's test.
- Phase 46: Fully implemented (2026-08-05; scoped-down version 2026-08-04)
  — the Model Builder Interface (section 9 below) at full surface:
  identity, the project/base model it relates to (both optional, since a
  from-template or from-scratch build has neither a tracked project nor an
  existing model to start from), a free-text source type (section 9 lists
  eleven starting points such as new model from template, imported base
  model, or embedding model, not a closed enum), its own five-state
  design-time lifecycle status — draft, configuring, ready, submitted,
  archived — rather than the eleven-state job lifecycle Training Jobs and
  Fine-Tuning use, since a builder configuration never queues, runs, or
  pauses, plus the complete section 9 build-settings list
  (`ModelBuilderSettings` in `src/masterai.hpp`: architecture, layer
  configuration, hidden dimensions, attention configuration, vocabulary
  and tokenizer, sequence length, activation functions, dropout,
  initialisation strategy, loss function, optimiser, learning-rate
  scheduler, batch size, epoch count, gradient accumulation, gradient
  clipping, mixed precision, checkpoint frequency, validation frequency,
  early stopping, random seed, reproducibility settings, and
  distributed-training settings) carried by `ModelBuilderConfig`/
  `ModelBuilderConfigStore` (`src/ml.cpp`), which validates the closed
  basic/advanced configuration-mode set and the bounded dropout/gradient-
  clipping ranges and restores legacy seven-field scoped-down records with
  default settings so existing databases need no migration. Administrator-
  only `ml.modelbuilder.view`/`ml.modelbuilder.manage` permissions gate
  `GET`/`POST /api/v1/ml/model-builder-configs`,
  `POST /api/v1/ml/model-builder-configs/{id}/status`,
  `POST /api/v1/ml/model-builder-configs/{id}/configure` (partial-update:
  absent fields keep their stored values), and
  `POST /api/v1/ml/model-builder-configs/{id}/delete`. The "Model Builder"
  (`/app/ml/model-builder-configs`) entry under the Machine Learning
  sidebar in `src/web_ui.cpp` lists, creates, and moves configurations
  through status, and its "Configure build settings" form provides the
  basic and advanced configuration modes section 9 requires — basic shows
  the six everyday fields (mode, architecture, loss function, optimiser,
  batch size, epoch count, sequence length), advanced additionally reveals
  the full surface — pre-filled from a row's Configure button.
  `test_machine_learning_model_builder_lifecycle` covers the permission/
  lifecycle/reload/remove guarantees plus configure() validation,
  full-settings persistence across reload, and legacy-record restore. The
  dashboard roster's `model-builder` interface entry (`src/ml.cpp`) now
  reports `available`, and `test_machine_learning_foundation_dashboard`'s
  exclusion list adds it accordingly. The build-settings record describes
  the intended build; the model-construction executor that consumes it is
  a separate future phase.
- Phase 47: Implemented at a scoped-down level (2026-08-04) — Prompt and
  Instruction Training (section 19 below), scoped down to identity, the
  dataset each example targets, a free-text subject classification, and a
  five-state reviewer-approval lifecycle status — draft, in_review,
  approved, rejected, archived — matching section 19's requirement that
  "generated training examples must require approval before entering an
  approved dataset" rather than reusing Training Jobs'/Fine-Tuning's
  eleven-state job lifecycle, since an instruction example never queues,
  runs, or pauses (`InstructionExample`/`InstructionExampleStore`,
  `src/ml.cpp`), not the section's full record (system instruction, user
  instruction, context, expected response, rejected response, tool calls,
  tool results, required output format, difficulty, safety classification)
  that only means something once an actual example record exists to hold
  it. `create()` requires a target dataset id, mirroring Data Labeling's/
  Data Preparation's own required-dataset pattern. New administrator-only
  `ml.instructions.view`/`ml.instructions.manage` permissions gate `GET`/
  `POST /api/v1/ml/instruction-examples`,
  `POST /api/v1/ml/instruction-examples/{id}/status`, and
  `POST /api/v1/ml/instruction-examples/{id}/delete`. New "Prompt and
  Instruction Training" (`/app/ml/instruction-examples`) entry under the
  Machine Learning sidebar in `src/web_ui.cpp` lists, creates, and moves
  examples through status.
  `test_machine_learning_instruction_training_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 46's test. The
  dashboard roster's `prompt-instruction-training` interface entry
  (`src/ml.cpp`) intentionally still reports `planned`, matching Phases
  43-46's own roster entries — `test_machine_learning_foundation_dashboard`'s
  fixed exclusion list stays frozen at Phase 42's interface set, so this
  phase changes nothing there.
- Phase 48: Implemented at a scoped-down level (2026-08-05) — Synthetic Data
  Generation (section 20 below), scoped down to identity, the dataset each
  record targets, a free-text generation technique (section 20 lists
  thirteen operations such as generating paraphrases, counterexamples, or
  code samples, not a closed enum), and the same five-state reviewer-
  approval lifecycle status Phase 47 used — draft, in_review, approved,
  rejected, archived — matching section 20's requirement that generated
  records carry a "human-review status" and "remain distinguishable from
  human-created and real-world data" until reviewed, rather than reusing
  Training Jobs'/Fine-Tuning's eleven-state job lifecycle, since a synthetic
  record never queues, runs, or pauses (`SyntheticRecord`/
  `SyntheticRecordStore`, `src/ml.cpp`), not the section's full record
  (generator model, generator version, prompt, generation settings,
  confidence score, original source linkage) that only means something once
  a real generation executor exists to produce it. `create()` requires a
  target dataset id, mirroring Phase 47's own required-dataset pattern. New
  administrator-only `ml.syntheticdata.view`/`ml.syntheticdata.manage`
  permissions gate `GET`/`POST /api/v1/ml/synthetic-records`,
  `POST /api/v1/ml/synthetic-records/{id}/status`, and
  `POST /api/v1/ml/synthetic-records/{id}/delete`. New "Synthetic Data
  Generation" (`/app/ml/synthetic-records`) entry under the Machine Learning
  sidebar in `src/web_ui.cpp` lists, creates, and moves records through
  status. `test_machine_learning_synthetic_data_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 47's test. The
  dashboard roster's `synthetic-data` interface entry (`src/ml.cpp`)
  intentionally still reports `planned`, matching Phases 43-47's own roster
  entries — `test_machine_learning_foundation_dashboard`'s fixed exclusion
  list stays frozen at Phase 42's interface set, so this phase changes
  nothing there.
- Phase 49: Implemented at a scoped-down level (2026-08-05) — Embeddings and
  Vector Stores (section 21 below), scoped down to identity, a free-text
  embedding model name and a free-text distance metric (section 21 lists
  "Register embedding models" and "Select distance metric" as operations
  without naming closed sets for either), and a three-state pending/
  approved/rejected approval lifecycle (`VectorStore`/`VectorStoreStore`,
  `src/ml.cpp`) — not the section's full field list (embedding-model
  version, vector dimensions, document count, chunk count, storage size,
  index type, security classification, access permissions, last rebuild
  date, associated subject packages/agents/deployed models) that only means
  something once a real document-import/chunking/indexing pipeline exists.
  Unlike Phases 47-48's target-scoped content records, a vector store is a
  standalone registered resource like Dataset Manager's own entries, so it
  carries no required parent id and reuses `DatasetApprovalStatus`'s
  three-state pending/approved/rejected workflow shape rather than the
  five-state reviewer workflow content records use, since a vector store is
  infrastructure to be approved for use, not content to be drafted and
  reviewed. New administrator-only `ml.vectorstores.view`/
  `ml.vectorstores.manage` permissions (the `manage` name matches section
  3's own recommended permission list) gate `GET`/
  `POST /api/v1/ml/vector-stores`, `POST /api/v1/ml/vector-stores/{id}/status`,
  and `POST /api/v1/ml/vector-stores/{id}/delete`. New "Embeddings and
  Vector Stores" (`/app/ml/vector-stores`) entry under the Machine Learning
  sidebar in `src/web_ui.cpp` lists, creates, and moves stores through
  status. `test_machine_learning_vector_store_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 48's test, plus
  `create()` rejecting an empty name. The dashboard roster's
  `embeddings-vector-stores` interface entry (`src/ml.cpp`) intentionally
  still reports `planned`, matching Phases 43-48's own roster entries —
  `test_machine_learning_foundation_dashboard`'s fixed exclusion list stays
  frozen at Phase 42's interface set, so this phase changes nothing there.
  Phase 59 later changes this entry to `available` when the real index lands.
- Phase 50: Implemented at a scoped-down level (2026-08-05) — Retrieval-
  Augmented Generation (section 22 below), scoped down to identity, a
  free-text search strategy (section 22 lists "Search strategy" as a
  configurable operation without naming a closed set, matching how
  `VectorStore`'s distance metric stays free text), an optional
  `vector_store_id` referencing a `VectorStoreStore` entry (section 22
  lists "Vector store" as a configurable operation, and a `VectorStoreStore`
  entry is the one real resource this phase can link against; optional
  because a keyword-only retrieval strategy needs no vector store), and a
  three-state pending/approved/rejected approval lifecycle (`RagConfig`/
  `RagConfigStore`, `src/ml.cpp`) — not the section's full configuration
  surface (query preprocessing, query rewriting, hybrid-search weighting,
  retrieval count, relevance threshold, metadata filters, reranking model,
  context-size limit, citation requirements, response template, fallback
  behavior, source-priority rules, restricted documents, cache behavior)
  or its retrieval-testing surface (retrieved documents, retrieval
  relevance, missing information, incorrect citations, context conflicts,
  response grounding, unsupported claims, retrieval latency) that only mean
  something once a real retrieval executor exists. Like Phase 49's vector
  store, a RAG configuration is a standalone registered resource, not a
  target-scoped content record, so it reuses the same three-state
  pending/approved/rejected workflow rather than the five-state reviewer
  workflow content records use. New administrator-only
  `ml.ragconfigs.view`/`ml.ragconfigs.manage` permissions gate `GET`/
  `POST /api/v1/ml/rag-configs`, `POST /api/v1/ml/rag-configs/{id}/status`,
  and `POST /api/v1/ml/rag-configs/{id}/delete`. New "Retrieval-Augmented
  Generation" (`/app/ml/rag-configs`) entry under the Machine Learning
  sidebar in `src/web_ui.cpp` lists, creates, and moves configurations
  through status. `test_machine_learning_rag_config_lifecycle` covers the
  same permission/lifecycle/reload/remove guarantees as Phase 49's test.
  The dashboard roster's `retrieval-augmented-generation` interface entry
  (`src/ml.cpp`) intentionally still reports `planned`, matching Phases
  43-49's own roster entries — `test_machine_learning_foundation_dashboard`'s
  fixed exclusion list stays frozen at Phase 42's interface set, so this
  phase changes nothing there. Phase 60 later changes this entry to
  `available` when retrieval/context execution lands.
- Phase 51: Implemented at a scoped-down level (2026-08-05) — the Subject
  Examination System (section 24 below), scoped down to identity, a
  mandatory `subject_id` referencing a `SubjectPackageStore` entry (an
  exam only means something against a registered subject package,
  mirroring Phase 47's required-dataset pattern), a free-text question
  format (section 24 lists nine question types as examples, not a closed
  enum), and the same five-state draft/in_review/approved/rejected/
  archived reviewer-approval lifecycle content records use (`SubjectExam`/
  `SubjectExamStore`, `src/ml.cpp`) — not the section's question-bank/
  score-suite surface (per-topic and per-difficulty breakdowns,
  hallucination rate, citation quality, minimum approval score) that only
  means something once a real examination executor exists. An exam is
  authored content a reviewer approves before it may examine anything,
  exactly like an `InstructionExample`, so it uses the five-state reviewer
  workflow rather than the three-state resource-approval workflow. New
  administrator-only `ml.subjectexams.view`/`ml.subjectexams.manage`
  permissions gate `GET`/`POST /api/v1/ml/subject-exams`,
  `POST /api/v1/ml/subject-exams/{id}/status`, and
  `POST /api/v1/ml/subject-exams/{id}/delete`. New "Subject Examination"
  (`/app/ml/subject-exams`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves exams through status.
  `test_machine_learning_subject_exam_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 50's test, plus
  the required-subject-id rule. The dashboard roster's
  `subject-examination` interface entry (`src/ml.cpp`) intentionally still
  reports `planned`, matching Phases 43-50's own roster entries —
  `test_machine_learning_foundation_dashboard`'s fixed exclusion list
  stays frozen at Phase 42's interface set, so this phase changes nothing
  there.
- Phase 52: Implemented at a scoped-down level (2026-08-05) —
  Hyperparameter Optimization (section 26 below), scoped down to identity,
  a mandatory `training_job_id` referencing a `TrainingJobStore` entry (a
  search tunes an existing training job's configuration, so the job
  reference is required the way Phase 45's base model is), a free-text
  strategy (section 26 lists grid/random/Bayesian/population-based/
  successive-halving as examples, not a closed enum), and the same
  eleven-state job lifecycle Training Jobs and Fine-Tuning use
  (`HyperparameterSearch`/`HyperparameterSearchStore`, `src/ml.cpp`) —
  not the search-space/trial-history/best-result surface (learning rate,
  batch size, epochs, optimiser, dropout, adapter rank, early stopping,
  resource/time limits) that only means something once a real search
  executor exists. New administrator-only `ml.hyperparams.view`/
  `ml.hyperparams.manage` permissions gate `GET`/
  `POST /api/v1/ml/hyperparameter-searches`,
  `POST /api/v1/ml/hyperparameter-searches/{id}/status`, and
  `POST /api/v1/ml/hyperparameter-searches/{id}/delete`. New
  "Hyperparameter Optimization" (`/app/ml/hyperparameter-searches`) entry
  under the Machine Learning sidebar in `src/web_ui.cpp` lists, creates,
  and moves searches through status.
  `test_machine_learning_hyperparameter_search_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 51's test, plus
  the required-training-job-id rule and an eleven-state lifecycle walk.
  The dashboard roster's `hyperparameter-optimization` interface entry
  intentionally still reports `planned`, exactly as Phases 43-51 left
  their own roster entries.
- Phase 53: Implemented at a scoped-down level (2026-08-05) — Model
  Optimization (section 28 below), scoped down to identity, a mandatory
  `model_id` referencing a `ModelRegistryStore` entry (an optimization run
  only means something against a registered model), a free-text operation
  (section 28 lists thirteen operations — quantization, pruning,
  distillation, graph optimization, and more — as examples, not a closed
  enum), and the same eleven-state job lifecycle Training Jobs use
  (`ModelOptimizationRun`/`ModelOptimizationStore`, `src/ml.cpp`) — not
  the before/after quality-loss comparison against the original model
  that only means something once a real optimizer executor exists. New
  administrator-only `ml.modelopts.view`/`ml.modelopts.manage` permissions
  gate `GET`/`POST /api/v1/ml/model-optimizations`,
  `POST /api/v1/ml/model-optimizations/{id}/status`, and
  `POST /api/v1/ml/model-optimizations/{id}/delete`. New "Model
  Optimization" (`/app/ml/model-optimizations`) entry under the Machine
  Learning sidebar in `src/web_ui.cpp` lists, creates, and moves runs
  through status. `test_machine_learning_model_optimization_lifecycle`
  covers the same permission/lifecycle/reload/remove guarantees as Phase
  52's test, plus the required-model-id rule. The dashboard roster's
  `model-optimization` interface entry intentionally still reports
  `planned`, exactly as Phases 43-52 left their own roster entries.
- Phase 54: Implemented at a scoped-down level (2026-08-05) — Checkpoint
  Management (section 33 below), scoped down to identity, a mandatory
  `training_job_id` referencing a `TrainingJobStore` entry (a checkpoint
  is a child record of the training job that produced it), a free-text
  capture reason (section 33 describes automatic epoch/step capture
  alongside manual capture without naming a closed set), and a bespoke
  three-state retention lifecycle — active (subject to normal retention),
  pinned (section 33's "protect" operation: exempt from retention
  deletion), archived (`TrainingCheckpoint`/`TrainingCheckpointStore`,
  `src/ml.cpp`) — not the step/epoch/validation-metric/size/hash/
  parent-model record or the resume/compare/promote/download operations
  that only mean something once a real training executor captures
  checkpoints. This is a retention lifecycle, not an approval workflow, so
  it deliberately reuses neither the three-state pending/approved/rejected
  shape nor the five-state reviewer shape: nobody "approves" a checkpoint;
  they keep it, protect it, or archive it. New administrator-only
  `ml.checkpoints.view`/`ml.checkpoints.manage` permissions gate `GET`/
  `POST /api/v1/ml/checkpoints`, `POST /api/v1/ml/checkpoints/{id}/status`,
  and `POST /api/v1/ml/checkpoints/{id}/delete`. New "Checkpoint
  Management" (`/app/ml/checkpoints`) entry under the Machine Learning
  sidebar in `src/web_ui.cpp` lists, creates, and moves checkpoint records
  through status. `test_machine_learning_checkpoint_lifecycle` covers the
  same permission/lifecycle/reload/remove guarantees as Phase 53's test,
  plus the bespoke retention lifecycle. The dashboard roster's
  `checkpoint-management` interface entry intentionally still reports
  `planned`, exactly as Phases 43-53 left their own roster entries.
- Phase 55: Implemented at a scoped-down level (2026-08-05) — the
  Deployment Manager (section 34 below), scoped down to identity, a
  mandatory `model_id` referencing a `ModelRegistryStore` entry (a
  deployment promotes a registered model and nothing else), free-text
  environment and strategy fields (section 34 lists dev/test/staging/
  production/offline/intranet targets and direct/blue-green/canary/shadow/
  A-B/rolling strategies as examples, not closed enums), and the same
  three-state pending/approved/rejected approval lifecycle Phases 49-50
  use (`Deployment`/`DeploymentStore`, `src/ml.cpp`) — section 34
  explicitly names approval as part of the deployment record, and a
  deployment is a standalone registered resource awaiting authorization,
  not reviewer-workflow content — not the model-version/runtime/
  target-node/rollback-version/health-status record that only means
  something once a real deployment executor exists. New administrator-only
  `ml.deployments.view`/`ml.deployments.manage` permissions gate `GET`/
  `POST /api/v1/ml/deployments`, `POST /api/v1/ml/deployments/{id}/status`,
  and `POST /api/v1/ml/deployments/{id}/delete`. New "Deployment Manager"
  (`/app/ml/deployments`) entry under the Machine Learning sidebar in
  `src/web_ui.cpp` lists, creates, and moves deployments through status.
  `test_machine_learning_deployment_lifecycle` covers the same
  permission/lifecycle/reload/remove guarantees as Phase 54's test, plus
  the required-model-id rule. The dashboard roster's `deployment-manager`
  interface entry intentionally still reports `planned`, exactly as
  Phases 43-54 left their own roster entries —
  `test_machine_learning_foundation_dashboard`'s fixed exclusion list
  stays frozen at Phase 42's interface set, so these phases change
  nothing there.
- Phase 56: Implemented (2026-08-05) — the Machine Learning module's first
  REAL execution layer (`src/ml_engine.cpp`): nothing in this phase records
  intent, everything computes. (1) Real dataset content: `POST/GET
  /api/v1/ml/datasets/{id}/content` (`DatasetContentStore`) uploads CSV
  against a registered dataset, fully parsed and validated server-side
  (`parse_tabular_csv` — header row, quoted fields, numeric feature
  enforcement, 8 MiB cap, ≤64 classes) with a returned profile (rows,
  feature columns, task, classes). (2) Real training: `POST
  /api/v1/ml/training-jobs/{id}/run` trains the job's dataset by full-batch
  gradient descent on standardized features — linear regression for numeric
  targets, logistic/softmax classification for categorical targets — moving
  the job through queued/preparing/running/awaiting_evaluation for real,
  recording a genuine per-epoch loss curve, capturing `TrainingCheckpoint`
  records with actually measured losses, persisting the learned weights as
  a reloadable artifact (`TrainedModelStore`, keyed by model registry id,
  full-precision round-trip), and registering/advancing the model registry
  entry to the `evaluation` state. Held-out metrics come from a
  deterministic seeded split whose standardization statistics use the
  training rows only. (3) Real evaluation: `POST
  /api/v1/ml/evaluation-runs/{id}/run` scores a trained artifact against
  any schema-matching dataset — accuracy, macro precision/recall/F1, and a
  confusion matrix for classification; MSE, MAE, and R² for regression —
  storing the result (`EvaluationResultStore`) behind `GET
  /api/v1/ml/evaluation-runs/{id}/result`. (4) Live prediction: `GET
  /api/v1/ml/models/{id}/artifact` reports what was learned and `POST
  /api/v1/ml/models/{id}/predict` serves real predictions (winning class
  plus per-class probabilities, or the predicted value) from the persisted
  weights, surviving server restarts. Web UI: Dataset Manager gains a
  validated CSV upload form, Training Jobs a "Train now" action with a
  genuine result panel, Evaluation Lab "Evaluate now"/"View result"
  actions, and Model Registry a prediction form; all ML row actions were
  also converted to compact, consistent Bootstrap-Icons icon buttons
  (embedded SVG paths, no CDN) with hover hints.
  `test_machine_learning_real_training_and_prediction` proves the engine
  actually learns: >90% held-out accuracy on a separable classification
  set, R² > 0.99 recovering a known line, identical predictions from a
  reloaded artifact, and parser rejection of malformed CSV. Honest
  boundary: this executor covers tabular classification/regression;
  LLM fine-tuning, knowledge ingestion, embeddings-based vector search,
  and the remaining executor surfaces are still `Planned`.
- Phase 57: Implemented (2026-08-05) — Model Comparison (section 27) as
  the module's second REAL executor, built directly on Phase 56's
  evaluation machinery rather than the scoped-down roster pattern. A
  `ModelComparison` record (`ModelComparisonStore`, `ml_model_comparisons`)
  names a mandatory baseline model, a mandatory (and necessarily
  different) candidate model, and a mandatory shared benchmark dataset,
  moving through Evaluation Lab's five run states via `GET/POST
  /api/v1/ml/model-comparisons` and the `/status`/`/delete` routes, gated
  by new administrator-only `ml.comparisons.view`/`ml.comparisons.manage`
  permissions. `POST /api/v1/ml/model-comparisons/{id}/run` executes for
  real: both trained artifacts are loaded from `TrainedModelStore`, scored
  against the benchmark dataset's actual uploaded CSV via
  `evaluate_tabular_model`, and `tabular_model_comparison_json`
  (`src/ml_engine.cpp`) reports both full metric sets plus a measured
  verdict — primary metric macro F1 for classification and MSE for
  regression, the candidate-minus-baseline delta, and the winner (an
  exact tie is reported as `tie`, never a picked side); a cross-task
  comparison is rejected. Results persist in `ComparisonResultStore`
  (`ml_comparison_results`), recalled by `GET .../result` and deleted with
  their comparison. Missing artifacts or dataset content return 409 with
  an actionable detail, and a failed run lands the record in `failed`.
  Web UI: a new administrator-only "Model Comparison" page
  (`/app/ml/model-comparisons`) with a fully labeled create form
  (baseline/candidate/benchmark IDs), the standard status/actions table
  with "Compare now"/"View result" icon actions, and a verdict panel;
  additionally, the workspace content pane's fixed 1200px width cap was
  removed so every panel now spans the full browser width consistently
  at any window size (chat keeps its own reading-width cap). Honest
  boundary against section 27's wishlist: hallucination rate, safety,
  latency/throughput/GPU cost, and blind response comparison only apply
  to generative models this tabular engine does not train — everything
  this phase reports is computed from real evaluations.
  `test_machine_learning_model_comparison_lifecycle_and_execution` covers
  the administrator-only permissions, the record-shape rules (missing
  name/ids rejected, self-comparison rejected), lifecycle/reload/remove,
  and the executor's verdicts: a correctly trained model must beat a
  label-flipped one, swapping baseline/candidate must swap the winner,
  identical metrics must tie, cross-task comparisons must throw, and
  stored results must round-trip and die on remove. The dashboard
  roster's `model-comparison` interface entry now reports `available`
  because the executor exists. Windows x64 Debug and Release builds completed and
  `masterai_core_tests` passed; as part of this validation the Phase 30
  arena poison assertions in `test_phase_thirty_request_arena_allocation_
  and_poison` were wrapped in `#ifndef NDEBUG`, since masterai.hpp has
  always documented ArenaHandle's generation tracking as compiling out
  under NDEBUG and the Release suite could therefore never pass that
  debug-only assertion.
- Phase 58: Implemented (2026-08-06) — Knowledge Ingestion (section 13)
  now accepts real administrator-selected `.txt`, Markdown, CSV, JSON, and
  JSONL sources through Subject Knowledge Manager. `POST
  /api/v1/ml/knowledge-documents` validates the referenced subject package
  and vector store, rejects binary/unsupported content and files above
  2 MiB, hashes the exact accepted UTF-8 text content with SHA-256, creates
  bounded overlapping chunks, and persists source provenance plus chunk records in
  `ml_knowledge_documents`/`ml_knowledge_chunks`. List/delete APIs and the
  UI expose measured byte/chunk counts and remove a document's chunks with
  it. `ml.knowledge.view/manage` remain administrator-only and mutations
  are audited.
- Phase 59: Implemented (2026-08-06) — the Embeddings and Vector Stores
  surface now has a real populated local index. Each Phase 58 chunk receives
  a persisted 128-dimensional L2-normalized vector from MasterAI's
  independently authored deterministic feature-hashing vectorizer
  (`src/ml_knowledge.cpp`); `GET /api/v1/ml/vector-stores/{id}/index`
  reports the actual method, dimensions, document count, chunk count, and
  indexed text bytes. This is genuine vector generation and cosine search,
  but it is not a learned transformer embedding model and must not be
  described as equivalent semantic quality.
- Phase 60: Implemented (2026-08-06) — approved RAG configurations with an
  approved populated vector store can execute through `POST
  /api/v1/ml/rag-configs/{id}/query`. The executor supports the implemented
  `hybrid`, `vector` (including legacy `vector_only`), and `keyword`
  (including legacy `keyword_only`) strategies,
  bounds queries and `topK`, ranks persisted source chunks, and returns
  vector score, keyword score, combined score, stable source citation,
  exact evidence text, and a citation-ready context package. The RAG UI
  provides an evidence panel and named record selectors. Phase 60 is a real
  retrieval/context executor; it deliberately does not generate an LLM
  answer or claim that a later answer is grounded. Dataset ingestion now
  uses a native browser CSV file dialog rather than pasted text, knowledge
  ingestion uses a bounded multi-format file dialog, and ML foreign-key
  forms use populated record selectors instead of copied opaque IDs.
- Phase 61: Implemented (2026-08-06) — learned neural embedding adapters and
  once-per-conversation memory recall. A Vector Store may now select either
  `authored_hashing_vectorizer_v1` or a verified, ready GGUF from the
  `embeddings-code-search` model category. Learned vectors execute through
  the existing process-isolated `RunnerSupervisor` and llama.cpp's loopback
  `POST /v1/embeddings` contract; no third-party control-plane foundation was
  added. The server bounds input and vector dimensions, rejects empty,
  non-finite, zero-magnitude, malformed, or multi-vector responses, L2-
  normalizes before persistence, records the exact embedding model and
  dimensions with every chunk, migrates Phase 59's 128-dimensional records,
  and refuses retrieval when query and stored-vector model provenance differ.
  Keyword-only retrieval does not load an embedding model. The Vector Store
  page lists only verified ready embedding-category models alongside the
  authored fallback, and the index profile reports the real method and
  dimensions. Separately, durable user-memory records are read once when a
  chat is created, saved as a bounded owner-scoped snapshot in the chat
  header, and reused on later turns and after restart. Legacy chats perform
  one lazy snapshot on their first post-upgrade turn. Details learned during
  an active conversation remain available through its ordinary message
  history and become durable context for newly created chats; the durable
  memory collection is no longer queried on every turn. Native Windows x64
  Debug and Release tests prove the llama.cpp response contract with a
  deterministic isolated fake,
  normalization, persistence/reload, model-mismatch and invalid-vector
  rejection, and memory-snapshot stability. No learned embedding GGUF is
  installed in this checkout, so semantic-quality/latency evidence remains a
  per-model deployment gate rather than a fabricated Phase 61 result.
- Phase 62: Implemented (2026-08-11) — Inference Endpoints (section 2 item
  19, section 35). A scoped-down registry: name, model reference, runtime,
  host, port, protocol, authentication method, and rate limit, moved through
  a draft/active/disabled lifecycle via `GET/POST /api/v1/ml/inference-
  endpoints`, `.../status`, `.../delete`. Deliberately does not open a real
  network listener, enforce the recorded rate limit, or apply a safety/tool
  policy — this records administrator intent, matching the Model Registry/
  Projects pattern, not a live inference-serving path.
- Phase 63: Implemented (2026-08-11) — Hardware and Compute (section 2 item
  20, section 30). A static compute-node registry: name, address, operating
  system, CPU/GPU description, and system memory, with an available/
  reserved/draining/disabled administrative status via `GET/POST
  /api/v1/ml/compute-nodes`, `.../status`, `.../delete`. Deliberately does
  not poll live telemetry (temperature, power draw, queue length, current
  workload) — that requires an agent process on the node this phase does
  not build.
- Phase 64: Implemented (2026-08-11) — Automated Machine Learning Pipelines
  (section 2 item 21, section 37). A pipeline definition names an ordered,
  free-text list of stages drawn from section 37's sixteen-stage lifecycle
  taxonomy; `POST .../run` records a run outcome (queued/running/completed/
  failed/canceled) rather than orchestrating the other stores' real
  training/evaluation/deployment jobs. `GET/POST /api/v1/ml/automation-
  pipelines`, `.../status`, `.../run`, `.../runs`, `.../delete`.
- ML forms clarity pass (2026-08-17): a "?" hint badge (`.mlHint`/
  `.mlHintBubble` CSS, `field_hint()` in `src/web_ui.cpp`) sits after a
  label's own text and reveals a detailed explanation on hover/focus,
  toggleable off from Machine Learning Settings (`#cfgHintsEnabled`, a
  `localStorage`-only client preference — no server config field, so it
  never touches `admin_config_put`'s strict schema). Named to avoid
  colliding with the pre-existing `<p class="fieldHint">` note already on
  the Knowledge ingestion form (`ml-subjects`). Every remaining opaque
  "paste an ID" text field across the Machine Learning forms is now a
  `<select>` populated from the actual entity list (`fillMlSelect()`), and
  every remaining comma-separated multi-id field is now a checkbox
  `.multiSelect` list (`fillMultiSelect()` for a fetched list, or a literal
  checkbox list for a fixed vocabulary): Instruction Example content-edit/
  test target, Test-against-models model list, Duplicates/contradictions
  dataset, Inference Endpoint policy's endpoint and safety-policy targets,
  Experiment Compare's baseline (its own required `<select>`, so "baseline
  first" is structural rather than a typing convention) and candidate list,
  and Automation Pipeline's stage list (a fixed 16-entry checkbox list in
  canonical lifecycle order, since stages execute in list order and
  free-typed stage names risked a silent-no-match typo against the exact
  strings `run_automation_pipeline` matches). Dataset Manager's content-
  upload format (see the dataset multi-format entry above) also gained a
  hint naming its four accepted extensions. Deliberately did not touch
  every already-clear field (most free-text fields already carry a
  `placeholder="e.g. ..."` example) or any non-ML form (Workspace Projects'
  project-id slug, the Model Inventory download form's model-id) — both are
  administrator-chosen identifiers, not a reference into an existing list,
  so a dropdown would be wrong there.
- Phase 65: Implemented (2026-08-11) — Safety and Governance (section 2 item
  22, section 40). A governance policy (name, scope, restricted data
  categories) and a per-model card (purpose, intended/prohibited use,
  training data reference, evaluation results, known limitations, license)
  each move through their own independent pending/approved/rejected
  approval workflow, matching Dataset/Deployment approval. `GET/POST
  /api/v1/ml/safety-policies` and `/api/v1/ml/model-cards`, each with
  `.../status`, `.../delete`. Deliberately does not run harmful-content/
  bias/hallucination/prompt-injection testing or credential/secret
  detection — those require content-scanning executors this phase does not
  build; the policy only records administrator intent and an approval
  decision.
- Phase 66: Implemented (2026-08-11) — Audit Logs (section 2 item 24,
  section 43). Every `ml.*` administrator action already appended to the
  hash-chained `AuditLog` (every phase back through Phase 37); this phase
  adds the read path — `AuditLog::recent()` parses the chained log file and
  `GET /api/v1/ml/audit-logs` returns the most recent 200 `ml.*`-prefixed
  entries, newest first, in a dedicated read-only administrator page. No new
  write path. Also: Machine Learning Settings (section 2 item 25, section
  49) — an ML-scoped view of the existing System Configuration form
  (`GET/POST /api/v1/admin/config`) surfacing only genuinely-enforced ML
  fields: the Dataset Manager's tabular CSV upload cap (newly configurable;
  previously a hardcoded 8 MiB constant in `ml_engine.cpp`) and the existing
  Subject Knowledge Manager document-upload cap and Parquet helper path.
  Layout/consistency cleanup landed alongside these phases: `#content`/
  `.panel` items gained `min-width:0` so a wide ML table can no longer force
  the page wider than the viewport (the actual cause of the reported
  screen-boundary overflow), every `table()` result is now wrapped in an
  `overflow-x:auto` container, the sidebar gained two narrow-viewport
  breakpoints, and every ML row-action button (`iconifyMlButtons()`,
  renamed `styleMlActionButtons()`) now keeps its visible text at a compact
  size instead of collapsing to an icon-only affordance. Separately,
  knowledge ingestion (`ml_knowledge.cpp`) now recognizes the Scraper
  Project's (`f:\projects\delphi12\Scraper`) three fixed JSON/JSONL export
  record shapes (instruction/input/output, conversation messages, raw
  document text) and rewrites them to clean text before chunking instead of
  slicing raw JSON syntax into 1200-byte windows; unrecognized JSON falls
  back to the previous raw-byte chunking unchanged.
- Phase 67: Implemented (2026-08-11) — Hardware and Compute live telemetry
  (section 2 item 20, section 30), closing the first of the six interfaces
  that Phases 62-65 deliberately left as "planned" identity/lifecycle
  registries. A compute node may now be flagged `isLocal` at creation --
  meaning it is the same host this MasterAI process is already running on
  -- and `GET /api/v1/ml/compute-nodes/{id}/telemetry` returns a genuinely
  fresh `probe_hardware()` snapshot (physical/logical CPU count, total/
  available RAM, GPU backends and VRAM, free disk) for that node on every
  request; a node not flagged local 400s rather than fabricating numbers,
  since polling an arbitrary remote node still requires an agent process
  this phase does not build. The Hardware and Compute dashboard tile moves
  from "planned" to "available" on that basis: real for the local host,
  not for a fleet. The web UI gained a "local node" checkbox on the create
  form, a Local column, and a "View live telemetry" action rendered only
  for local nodes. Separately, the Machine Learning sidebar was reordered
  to match the chronological order of actually building a model (Projects
  through data/knowledge preparation, training/fine-tuning, evaluation,
  optimization/comparison, safety/deployment/serving/ops, Automation
  Pipelines, then Model Builder last), and Audit Logs, Machine Learning
  Settings, and Model Registry were moved out of that pipeline tree into
  their own "Machine Learning Logs and Settings" sidebar group, since they
  are operational/inspection destinations rather than build steps.
- Phase 68: Implemented (2026-08-11) — Monitoring and Diagnostics (section
  2 item 23, section 44), closing the last of the six interfaces Phases
  62-65 deliberately left as "planned". `GET /api/v1/ml/monitoring` is a
  read-only aggregation over data other real phases already measured: a
  live `probe_hardware()` snapshot (Phase 67's same CPU/RAM/GPU/disk
  probe), real training-job status counts from `TrainingJobStore`, every
  completed evaluation run's genuinely measured metrics from Phase 56/57's
  `EvaluationResultStore`, and real prompt/generation tokens-per-second
  computed from actual `BenchmarkStore` runs. It deliberately does not
  report section 44's per-step training curves (current epoch/step,
  gradient norm, learning rate) — the tabular trainer has no iterative
  loop to sample one from — or any live per-request inference telemetry
  (requests/sec, latency percentiles, queue depth, cache-hit rate, safety-
  filter rate, tool-call success, retrieval latency, model-loading time,
  temperature, network activity) — no request-path instrumentation for any
  of that exists in this codebase yet, so surfacing it would mean
  fabricating numbers. The Monitoring and Diagnostics dashboard tile moves
  from "planned" to "available" on that same real-aggregation basis. New
  permission: `ml.monitoring.view` (administrator-only, view-only — there
  is nothing to mutate on a read-only aggregation page).

  Separately, fixed a real ingestion bug this phase's work surfaced: every
  `parse_json()` call site shared one hardcoded 1 MiB input ceiling, which
  rejected any knowledge document (JSON body includes the whole document
  as a string field, base64-inflated for Parquet) larger than 1 MiB even
  though `knowledge_maximum_document_bytes` already permitted up to 25 MiB.
  `parse_json()` gained an explicit, defaulted `max_bytes` parameter (every
  other call site keeps the 1 MiB default unchanged) instead of a second
  hardcoded constant; the knowledge-document ingestion endpoint and its
  internal Scraper-record JSON/JSONL flattening now both parse against
  `configuration.max_request_bytes` / the document's own configured size
  ceiling respectively, so a legitimately large, already-permitted
  knowledge document no longer fails one layer below the check that was
  supposed to allow it.

- Phase 69: Implemented (2026-08-11) — Automation Pipelines (section 2 item
  21, section 37), closing the last "records intent" ML interface Phase 64
  originally scoped down. A pipeline now names a target dataset and an
  optional starting model alongside its stage list; `POST .../run` executes
  every recognized stage for real, in order: a "Train model" stage creates
  a real `TrainingJob` against the pipeline's dataset and runs it through
  Phase 56's actual `train_tabular_model` (the same code path the Training
  Jobs page uses, factored into a shared `execute_training_job()` helper),
  handing its resulting model id to the next stage; an "Evaluate model"
  stage creates a real `EvaluationRun` and scores it via Phase 56's
  `evaluate_tabular_model` (`execute_evaluation_run()`, shared with the
  Evaluation Lab page) against the model the pipeline just trained, or the
  pipeline's configured starting model if no Train model stage ran first.
  Every other one of section 37's fourteen named stages (import/validate/
  clean/label/split data, validate model, safety tests, optimize, request
  approval, deploy staging/production, staging tests, monitor, rollback) is
  recorded honestly as `"skipped"` with a "no automated executor exists for
  this stage yet" detail — this codebase has no data-labeling, safety-
  scanning, deployment-serving, or monitoring executor for a pipeline to
  call, so fabricating success for those would be dishonest. Each run
  stores a per-stage `{"stage","status","detail"}` array (`stageResults` on
  `GET/POST .../run` and `.../runs`) instead of the prior single canned
  outcome note. The web UI's pipeline form gained dataset/model selects and
  a run-detail panel rendering that per-stage table.
- Phase 70: Implemented (2026-08-11) — Fine-Tuning (section 2 item 10,
  section 18) gains a real executor, closing the last "records intent"
  training-family interface Phase 45 originally scoped down.
  `train_tabular_model` (`src/ml_engine.cpp`) gained an optional
  `warm_start` parameter: when a fine-tuning run supplies the job's base
  model (`ml_trained_models->find(job.model_id)`, required — a job whose
  base model has no trained weights yet 409s rather than silently training
  from scratch), gradient descent initializes from that model's
  already-learned weights instead of zero, so the run genuinely continues
  training the existing model rather than coincidentally reusing the same
  optimizer code path. `warm_start`'s feature schema, task, and (for
  classification) class label set must match the fine-tuning dataset
  exactly, or the run fails with a clear schema-mismatch error before any
  training happens. `POST /api/v1/ml/fine-tuning-jobs/{id}/run`
  (`execute_fine_tuning_job` in `server.cpp`, mirroring Phase 56/69's
  `execute_training_job`) moves the job through
  queued/preparing/running for real, registers the adapted weights as a
  new Model Registry entry (the base model is left untouched, matching
  Model Comparison's baseline/candidate pattern), records genuinely
  measured checkpoints, and leaves the job `awaiting_evaluation` so
  Evaluation Lab can score the adapted model the same way it scores a
  freshly trained one. The web UI's Fine-Tuning page gained a "Fine-tune
  now" action and a last-run result panel, mirroring Training Jobs' "Train
  now". The dashboard's Fine-Tuning tile moves from "planned" to
  "available" on that real-execution basis. Deliberately out of scope: an
  actual LLM adapter/LoRA fine-tuning path (this executor only fine-tunes
  Phase 56's tabular linear/logistic/softmax models, the only model family
  this codebase actually trains) and any of section 18's still-deferred
  hyperparameter/checkpoint-strategy/output-model-versioning fields.
- Phase 71: Implemented (2026-08-11) — Automation Pipelines (section 2 item
  21, section 37) gains a real executor for seven more of the twelve
  stages Phase 69 left as "skipped": "Validate data" (`parse_tabular_csv`
  against the dataset's uploaded content, the same structural check Phase
  56's trainer already relies on), "Validate model" (the current model
  actually has trained weights in `TrainedModelStore`), "Safety tests" (an
  approved `ModelCard` exists for the current model, via
  `SafetyGovernanceStore::list_model_cards()`), "Request approval"/"Deploy
  staging"/"Deploy production" (a real `Deployment` record is created and
  approved for that environment — matching Deployment Manager's own
  documented scope of an approval workflow, not live traffic serving), and
  "Rollback" (that run's most recent deployment has its approval revoked,
  i.e. moved to `rejected`). "Monitor" also becomes real, reusing Phase
  68's monitoring aggregation (factored out of the `GET
  /api/v1/ml/monitoring` route into `build_ml_monitoring_json()` so both
  the route and a pipeline stage call the identical, genuinely measured
  snapshot). "Import data", "Clean data", "Label data", "Split data",
  "Optimize", and "Staging tests" remain honestly recorded as skipped —
  this codebase still has no data-labeling, data-cleaning, model-
  optimization, or staging-test executor to call. Separately, a pipeline
  run no longer blocks the HTTP request until every stage finishes: `POST
  /api/v1/ml/automation-pipelines/{id}/run` now calls
  `AutomationPipelineStore::begin_run()` (persists a `running` row with
  the pipeline's real stage count), returns immediately with `202
  Accepted`, and executes every stage on a detached background thread
  (`run_automation_pipeline()` in `server.cpp`) that calls
  `append_stage_result()` before and after each stage — once to mark it
  "Running: `<stage>`", once with its finished outcome — and `finish_run()`
  once every stage has run. `AutomationPipelineRun` gained
  `totalStageCount`/`completedStageCount`/`currentStage` fields so `GET
  .../runs` (already polled elsewhere in the ML admin UI) reports live
  progress; the web UI's pipeline "Run" button renders a `<progress>` bar
  and the currently-running stage's name, polling once a second via a new
  `pollMlPipelineRun()` helper (mirroring the existing model-download
  progress poll) until the run reaches a terminal status. The now-unused
  one-shot `AutomationPipelineStore::record_run()` was removed rather than
  left dead. `test_machine_learning_automation_pipeline_progress_lifecycle`
  covers `begin_run`/`append_stage_result`/`finish_run`/`find_run`
  (including their unknown-id no-op/reject behavior), progress surviving a
  store reload, and `automation_pipeline_run_json`'s new fields; the stage-
  executor dispatch itself lives on the `Server` class and is exercised
  through the web UI/HTTP surface rather than a store-level unit test, the
  same boundary Phase 69's original two stages left untested.
- Phase 72: Implemented (2026-08-12) — Automation Pipelines (section 2 item
  21, section 37) closes the six stages Phase 71 left honestly `"skipped"`.
  `ml_engine.cpp` gains three new real, tested helpers: `clean_tabular_csv`
  (splits raw CSV text into lines, reports genuine before/after row counts,
  and removes blank rows and exact-duplicate data rows — the header is
  never touched), `split_tabular_csv` (parses the CSV for real via
  `parse_tabular_csv`, then applies the identical `test_fraction * row_count`
  formula `train_tabular_model` already uses internally, so the reported
  train/holdout counts are exactly what a subsequent Train model stage will
  use, not a second, possibly-inconsistent number), and
  `prune_tabular_model` (zeroes weights below a magnitude threshold on a
  `TrainedTabularModel`'s real learned weight matrix in place and reports
  how many weights ended up pruned — real work, since these tabular models
  are literally weight vectors). Six new stage lambdas in
  `run_automation_pipeline()` (`server.cpp`) call them: "Import data" checks
  `ml_dataset_content` actually has stored content for the pipeline's
  dataset (distinct from "Validate data", which parses structure); "Clean
  data" runs `clean_tabular_csv` and persists the cleaned CSV back over the
  dataset's content via `ml_dataset_content->put()`, so later Train/
  Evaluate/Staging-tests stages in the same pipeline see the cleaned rows;
  "Split data" runs `split_tabular_csv` and reports the real counts;
  "Optimize" creates a real `ModelOptimizationRun` (operation `"pruning"`,
  via the existing `ml_model_optimizations` store from Phase 53) against
  the pipeline's current model, runs `prune_tabular_model` against that
  model's actual persisted weights, writes the pruned weights back via
  `ml_trained_models->put()`, and moves the run to `completed`/`failed`;
  "Staging tests" reuses `execute_evaluation_run()` — the same function
  "Evaluate model" and the Evaluation Lab page already call — against the
  pipeline's dataset. "Label data" first looks for a real `LabelTaskStore`
  (Phase 41) entry targeting the pipeline's dataset in the `completed`
  state and reports success referencing that task's id when one exists.
  **(2026-08-13, this pass)** when none does, it no longer reports
  `"skipped"`: `auto_label_tabular_dataset()` (`src/ml_engine.cpp`) is a
  real, deterministic heuristic labeler. When every row already carries a
  non-empty target value, those are validated as real ground truth and
  never overwritten (method `"existing_labels_validated"`); when one or
  more rows are missing a target, the labeler finds the first fully-numeric
  non-target column, computes that column's real 33rd/66th percentile
  thresholds across the dataset, and fills each missing target with
  `"low"`/`"medium"`/`"high"` based on where that row's value falls (method
  `"quantile_binning"`) — the source column and thresholds are reported so
  a reviewer can see exactly why a row got the label it did. The stage
  persists the filled-in CSV back over the dataset's content and records a
  completed `LabelTaskStore` entry (assignee `"automated-heuristic"`).
  Honest about what this is: a real, inspectable, reproducible heuristic,
  not a semantic understanding of the data and not a substitute for a
  human-reviewed labeling task where correctness genuinely matters. Every
  stage's outcome is a genuinely computed result; none fabricates
  pass/fail.
- Phase 73: Implemented (2026-08-12) — real LLM LoRA fine-tuning (section 2
  item 10, section 18), gaining an execution path alongside Phase 70's
  tabular warm-start path rather than replacing it: a `FineTuningJob` whose
  `method` starts with the free-text `"llm:"` prefix (e.g.
  `"llm:code assistant"` — matching `method`'s own existing "free text, not
  a closed enum" convention) now routes `POST .../fine-tuning-jobs/{id}/run`
  to a real LoRA adapter train-and-merge pipeline instead of the tabular
  trainer. This codebase does not implement transformer backpropagation
  itself: `src/ml_finetune.cpp` supervises administrator-vendored `llama.cpp`
  tooling as external processes, the same isolation approach `inference.cpp`
  already uses for the inference runner and `downloads.cpp` already uses for
  `curl`. Two new optional `AppConfig` fields,
  `llama_finetune_executable`/`llama_export_lora_executable` (settable via
  `config/settings.json`'s `inference` section or
  `MASTERAI_LLAMA_FINETUNE`/`MASTERAI_LLAMA_EXPORT_LORA`), follow
  `llama_server_executable`'s existing manual-placement convention — this
  codebase has no in-app downloader for any of these three tools, only for
  GGUF models (`downloads.cpp`); README.md documents where to get them from
  the same `ggml-org/llama.cpp` releases page. A real llama.cpp finetune run
  can take far longer than the HTTP request timeout, so the run executes on
  a detached background thread (`run_llm_fine_tuning_job` in `server.cpp`,
  mirroring Phase 71's automation-pipeline background-thread pattern): the
  job moves through `queued`/`preparing`/`running` for real, `POST .../run`
  returns `202 Accepted` immediately, and a new `FineTuningRunResultStore`
  (the async counterpart to `EvaluationResultStore`) records the real
  outcome for `GET .../fine-tuning-jobs/{id}/llm-result` to report —
  including each tool's own log tail on failure, never a fabricated
  success. `write_llm_finetune_training_text` converts the job's dataset
  CSV into the plain instruction/response text block format llama.cpp's
  finetune tooling consumes, honestly requiring a recognizable
  instruction/prompt column and a response/output/completion column rather
  than guessing. The base model comes from the job's Model Registry entry's
  `source` field, validated to be a real, existing GGUF file on disk before
  anything runs (a 409 with a clear detail otherwise) — Model Registry
  entries are administrator-entered metadata (Phase 39), not verified
  against the separate inference `ModelManifest` catalog, so this is the
  one link between them this phase adds. On success the merged GGUF is
  registered as a new Model Registry entry (`format` `"gguf-lora-merged"`,
  state `evaluation`), leaving the base model untouched, matching Phase
  70's own base-model-preserving pattern. Honestly out of scope: the exact
  CLI flags `run_llama_lora_finetune` passes match llama.cpp's historical
  `finetune`/`export-lora` example tools, but that interface has changed
  across `llama.cpp` releases and this codebase cannot verify which flags
  an administrator's specific vendored build expects — each job's optional
  `extraFinetuneArguments`/`extraExportLoraArguments` request fields exist
  specifically so an administrator can adapt to their build rather than a
  wrong guess silently producing nothing useful.
- Phase 74: Implemented (2026-08-12) — real heuristic content scanning for
  Safety and Governance (section 2 item 22, section 40), closing the
  content-scanning gap Phase 65's class comment named. `src/ml_safety_scan.cpp`
  adds `scan_content_for_risks()`: hand-rolled pattern matching (this
  codebase does not use `<regex>` anywhere, favoring explicit character
  scanning the same way `parse_tabular_csv`'s CSV splitter does), not an ML
  classifier. It detects secret-shaped tokens (AWS `AKIA...` keys,
  OpenAI-style `sk-...` keys, GitHub `ghp_...` tokens each checked by
  prefix and length, PEM key/certificate blocks, and a generic 32+
  character mixed-letter-and-digit token fallback for unlabeled secrets),
  prompt-injection phrasing (a documented sixteen-phrase case-insensitive
  list — "ignore previous instructions", "disregard the system prompt",
  "you are now", etc.), and, when a `SafetyPolicy` is supplied, a verbatim
  case-insensitive search for each of its comma-separated
  `restricted_data_categories` terms. New `POST
  /api/v1/ml/safety-policies/{id}/scan` (`ml.safety.view` scope — a
  read-only diagnostic, not a mutation) scans arbitrary request text
  against one policy. The Automation Pipeline's already-real "Safety
  tests" stage (Phase 71) now also runs this scanner over the pipeline's
  dataset content after its existing approved-ModelCard check, failing the
  stage and naming every finding if the scan is not clean, rather than
  only checking model-card approval. Every finding is a real, reproducible
  pattern match against the actual text scanned — never a fabricated risk
  score. **(2026-08-13, this pass)** the remaining gap the class comment on
  `SafetyPolicy`/`ModelCard` used to name — bias, hallucination, and
  subtler harmful content having no real detector — is closed by
  `scan_content_with_model_classifier()` (`src/ml_safety_scan.cpp`): a real
  LLM-as-judge classifier that sends the scanned text plus a fixed, JSON-
  only prompt to the same locally loaded language model this server already
  runs inference through (via a caller-supplied `generate` callback wired
  to `execute_rag_generation()` in `server.cpp`, so it respects the exact
  same memory/scheduler admission every other generation call goes
  through), and parses the model's own bias/`hallucination_risk`/
  `harmful_content` confidence-and-rationale JSON reply. Never fabricates a
  verdict on failure: an unparseable reply or a generation error reports
  `available: false` with a diagnostic, not a silent "clean" result. Wired
  in two places: `POST /api/v1/ml/safety-policies/{id}/scan` runs it
  opt-in when the request includes a `modelId` field (alongside the
  always-on heuristic scan); the Automation Pipeline's "Safety tests" stage
  runs it as a best-effort, purely informational addendum after its
  heuristic scan and model-card checks pass (an unavailable/misconfigured
  classifier never fails a stage the heuristic scan already passed). Honest
  about what this is: a real LLM-as-judge classifier, not a purpose-trained
  bias/hallucination model — the judge model's own blind spots and biases
  are still present, stated in the function's own comment rather than
  claimed away.
- Phase 75: Implemented (2026-08-12) — remote/fleet Hardware and Compute
  telemetry (section 2 item 20, section 30), closing the "requires an
  agent process on the node this phase does not build" gap Phase 63/67's
  class comments named for every node except the local host. New
  `src/telemetry_agent.cpp` implements both ends of a small, deliberately
  single-endpoint protocol over plain HTTP: `run_telemetry_agent` blocks
  serving one authenticated `GET /telemetry` route (a real
  `probe_hardware()` snapshot) on a listener that — unlike `HttpServer`,
  which hard-enforces loopback-only binding — can bind whatever host an
  operator points it at, since it has to be reachable from the main server
  on a different machine; `fetch_remote_telemetry` is the client side the
  `.../telemetry` route now calls for a non-local node. Started via a new
  `masterai telemetry-agent <host> <port> <shared-secret>` CLI subcommand
  (main.cpp) run on the remote node itself — never by the main server
  process. `ComputeNode` gains `agent_url` and `agent_shared_secret_hash`
  (settable at creation via the existing `POST /api/v1/ml/compute-nodes`
  route's new `agentUrl`/`agentSharedSecret` fields); only the secret's
  `sha256_hex` hash is ever persisted on the `ComputeNode` record itself
  (matching the setup-token hash convention), since that record is
  returned to every `GET .../compute-nodes` caller. The plaintext secret
  the server actually needs to *present* to the agent on each poll lives
  only in `HttpServer::State`'s existing encrypted-at-rest `secrets`
  `SecretStore` (key `"telemetry-agent:<node id>"`) — the same protection
  `main.cpp` already gives the IDE MCP token — and is erased when its node
  is deleted. Authentication is a Bearer shared secret compared with
  `constant_time_equal` (never a raw string compare). A node with no
  `agent_url` configured still 400s exactly as before; an unreachable
  agent or a rejected secret is a real `502`, never fabricated numbers.
  The Hardware and Compute dashboard tile's "real for the local host, not
  for a fleet" caveat from Phase 67 is now resolved for any node an
  administrator points at a running agent.
- Phase 76: Implemented (2026-08-12) — real RAG answer generation (section
  2 item 9, section 22), closing the "the response never fabricates a
  generated-model answer" boundary Phase 60's route comment named (that
  boundary meant "we will not fake one", not "we will never generate
  one" — this phase adds the real path). `POST /api/v1/ml/rag-configs/
  {id}/query` gains an optional `"generate":true` request field (with a
  now-required `modelId` alongside it); when set, a new
  `execute_rag_generation()` helper feeds the same retrieved context the
  response already returns into `assemble_chat_prompt` and
  `inference->generate` — the identical prompt-assembly and inference call
  the chat handler uses, not a second, divergent implementation — and
  returns a real generated answer plus real prompt/generated token counts
  in a new `"answer"` response field. Critically, `execute_rag_generation`
  also replicates the chat handler's full memory-lease and scheduler-
  ticket admission contract (`memory->reserve`/`request_scheduler->admit`/
  `wait_until_ready`, paired with `release`/`complete`/`cancel` on every
  exit path, mirrored exactly rather than approximated) so a RAG-generated
  answer cannot bypass the same concurrency/memory limits chat generation
  is careful to enforce — it is a new, independent call site, not a
  refactor of the existing chat path, so this adds no regression risk to
  chat. Omitted deliberately, since they are chat-specific concerns a
  one-shot answer has no need of: chat history, prompt-cache session-slot
  reuse, and query-lifecycle (`/api/v1/queries/{id}`) tracking. The
  default response (no `"generate"` field, or `false`) is byte-for-byte
  unchanged from Phase 60 — context only, no generated answer — so no
  existing caller's behavior changes.
- Phase 77: Implemented (2026-08-12) — a real network listener for
  Inference Endpoints (section 2 item 19, section 35), closing the "does
  not open a real network listener, enforce the rate limit, or apply the
  safety/tool policy" gap Phase 62's class comment named in full. When an
  endpoint's status becomes `active`, a new background thread
  (`run_inference_endpoint` in `server.cpp`) binds its own listener on
  `endpoint.host`/`endpoint.port` — its own small accept loop, the same
  hand-rolled Winsock/BSD pattern `HttpServer::run` already uses for the
  main listener, since that socket layer was never a singleton object —
  and serves exactly one route, `POST /v1/completions` with a
  `{"prompt":"..."}` body, until the status changes away from `active` or
  the endpoint is deleted (the thread's `shared_ptr<atomic_bool>` stop
  flag is flipped and joined either way; `HttpServer::State` gained a
  destructor that does the same for any still-running listener at
  shutdown, so none can outlive the process or leak a bound port). Every
  request is real, not simulated: authenticated (a Bearer token check
  against `InferenceEndpoint::auth_token_hash` when
  `authentication_method != "none"` — one enforced mechanism regardless of
  the free-text label, an explicitly documented simplification, not
  per-scheme fidelity), rate-limited (a genuine in-memory per-minute
  counter local to the listener thread, capped at
  `rate_limit_per_minute`, resetting each new minute — in-memory/
  best-effort, not a distributed limiter, and not persisted across a
  restart, both stated honestly), and scanned by Phase 74's real
  `scan_content_for_risks` on both the incoming prompt and the generated
  answer (a flagged prompt 400s before generation runs; a flagged answer
  is still returned alongside its scan result, not silently withheld,
  since Phase 74's scanner is heuristic and a false positive should not
  make an endpoint fail closed). Generation itself reuses Phase 76's exact
  `execute_rag_generation` path — the identical memory-lease/scheduler-
  ticket admission contract, not a third divergent implementation — so an
  endpoint request cannot bypass the same concurrency/memory limits chat
  and RAG generation are careful to enforce. `InferenceEndpoint` gained
  `auth_token_hash` (settable via the existing creation route's new
  `authToken` field; only the hash persists on the record itself, matching
  Phase 75's `ComputeNode`/`agent_shared_secret_hash` convention, with the
  plaintext living in the same `secrets` `SecretStore`, key
  `"inference-endpoint:<id>"`). Honestly out of scope: `/v1/completions`
  is this codebase's own minimal surface, not an OpenAI-compatible API.
  **(2026-08-13, this pass)** per-endpoint safety-policy configuration
  beyond the fixed content scan is now implemented: `InferenceEndpoint`
  gains `content_scan_enabled` (skip the heuristic scan entirely),
  `block_on_scan_finding` (default true, preserving the prior always-block
  prompt behavior; false makes a flagged prompt advisory-only),
  `block_answer_on_scan_finding` (default false, preserving the prior
  never-block answer behavior, kept as a *separate* flag from the prompt
  one specifically so this pass could not silently change the pre-existing
  default), `safety_policy_id` (attaches a real `SafetyPolicy`'s
  `restricted_data_categories` terms to the scan, the same policy the
  `.../safety-policies/{id}/scan` route already scans against), and
  `model_classifier_enabled`/`model_classifier_confidence_floor` (opts this
  endpoint into Phase 74's new LLM-judge classifier pass). New `POST
  /api/v1/ml/inference-endpoints/{id}/policy` (`ml.endpoints.manage` scope)
  updates them; `run_inference_endpoint` re-reads the endpoint's live
  policy from the store fresh on every request (not once at listener-thread
  start), so a policy change takes effect on the very next request without
  restarting the listener. Persisted record format is additive/backward-
  compatible: a pre-policy record still restores with the exact defaults
  above. Still honestly out of scope: no tool-calling surface exists on
  `/v1/completions` at all (pure text generation), so there is no
  "allowed tools" enforcement point to add here yet.
- Phase 78: Implemented (2026-08-12) — real per-request inference
  telemetry for Monitoring and Diagnostics (section 2 item 23, section 44),
  closing the per-request half of the gap Phase 68's route comment named
  ("no request-path instrumentation exists"). New
  `InferenceMetricsStore`/`src/inference_metrics.cpp`: `begin_request()`
  marks a request queued (real queue-depth +1), `end_request(latency)`
  marks it finished (queue-depth -1, latency recorded into a rolling
  15-minute sample window pruned on every access so it cannot grow
  unbounded on a long-running server); `snapshot()` reports the current
  queue depth, requests in the last real minute, and p50/p95/p99 latency
  (nearest-rank over the last-minute samples, empty window honestly
  reports 0 rather than dividing by zero). Wired into every real
  generation call site this codebase has: the chat handler (`begin_request`
  at the same point `scheduler_ticket` is assigned -- real admission --
  `end_request` on both the success path, using `generated
  .elapsed_microseconds` as the authoritative latency the API response
  itself already reports, and the failure/cancellation path, timed from
  the same admission moment) and Phase 76's `execute_rag_generation`
  (already shared by the RAG route and Phase 77's inference-endpoint
  listener, so one instrumentation site covers all three). `GET
  /api/v1/ml/monitoring` (`build_ml_monitoring_json()`) gained a new
  `"inferenceRequests"` field carrying this real snapshot.
  **(2026-08-13, this pass)** both remaining gaps are now closed. Live
  per-step training curves: `train_tabular_model()` gained an optional
  `on_epoch(epoch, loss)` callback, called synchronously after every real
  gradient-descent step; `execute_training_job()` wires it to a new
  `TrainingProgressTracker` (`begin()`/`update()`/`end()`,
  `src/ml_engine.cpp`), an in-process, deliberately non-persisted live-state
  map. Because `HttpServer` already serves each connection on its own
  thread, a concurrent `GET /api/v1/ml/training-jobs/{id}/live-progress` (or
  the new `"liveTrainingProgress"` array in `build_ml_monitoring_json()`,
  which reports every currently in-flight job at once) genuinely observes
  epoch/loss values *while* the run is still executing on its own thread —
  not a restructured async job contract, since `POST .../training-jobs/
  {id}/run` still returns only once training finishes, exactly as before.
  A `ProgressGuard` RAII wrapper calls `end()` on every exit path (success
  or failure) so a finished/failed job never reports stale "still running"
  state. Cache-hit rate: `InferenceMetricsStore` gained
  `record_cache_decision(bool reuse)` and `cache_hits`/`cache_misses`/
  `cache_hit_rate` fields on `Snapshot`, called with
  `PromptSessionManager::try_reuse()`'s real `reuse` outcome at its one call
  site (the chat handler) whenever `session_reuse_enabled` is on — the
  actual KV-slot reuse decision every generation on that path already
  makes, not a separately invented counter. Honest scope note kept in the
  class comment: this counts session-level (whole-prompt-prefix) reuse
  decisions, not a finer-grained per-token cache-hit counter, since
  llama.cpp's own runner does not expose one to this adapter.
- Phase 79: Implemented (2026-08-17) — real checkpoint weight capture and a
  resume-training executor for Checkpoint Management (section 33, Phase
  54), closing the "step/epoch/hash/resume record a real training executor
  will attach" gap Phase 54's own class comment named and that no phase
  through 78 ever revisited. `train_tabular_model`'s `on_epoch` callback
  (`src/masterai.hpp`/`src/ml_engine.cpp`) is widened with a third
  argument, the in-flight `TrainedTabularModel` itself — its weights are
  already updated and its feature/target/class schema already fixed for
  the whole run at that point, so it is exactly the state a real
  checkpoint snapshot needs. `execute_training_job` and
  `execute_fine_tuning_job` (`src/server.cpp`) now compute the checkpoint
  epoch stride up front (capped at ten checkpoints, same cap the previous
  post-hoc logic used) and, on `on_epoch`, create a `TrainingCheckpoint`
  carrying its real `epoch` and `has_snapshot=true` (both new fields on
  the struct) and persist the genuine weights through a new
  `CheckpointModelStore` (`src/masterai.hpp`/`src/ml_engine.cpp`), keyed
  by checkpoint id with the same flat pack/unpack shape `TrainedModelStore`
  already uses — replacing the previous behavior, which only wrote the
  epoch's loss into a free-text `capture_reason` string after training had
  already finished, with nothing to resume from. A manually-created
  checkpoint (the existing `POST /api/v1/ml/checkpoints` free-form path)
  still defaults to `epoch=0`/`has_snapshot=false`, since there is no
  weight state to attach to an administrator's own note. The new
  `execute_checkpoint_resume` executor continues gradient descent from a
  captured snapshot via `train_tabular_model`'s existing `warm_start`
  parameter — the exact mechanism Phase 70's fine-tuning executor already
  uses to adapt a base model — re-parsing the owning training job's
  dataset CSV and registering the result as a new Model Registry entry in
  the `evaluation` state (the checkpoint and the model it was captured
  from are left untouched, mirroring Phase 70's own base-model-untouched
  pattern). `POST /api/v1/ml/checkpoints/{id}/resume` (gated on
  `ml.training.manage`, since it performs a real training operation)
  exposes it, mirroring `POST .../training-jobs/{id}/run`'s request/
  response shape and returning `409 ml_checkpoint_has_no_snapshot` for a
  manually-created checkpoint with nothing to resume from. The web UI's
  Checkpoint Management page gained "Epoch" and "Snapshot" columns and a
  "Resume training" action next to each snapshot-bearing checkpoint,
  showing the same real method/loss/metrics summary the training-jobs
  "Train now" action shows. New `test_machine_learning_checkpoint_real_
  capture_and_resume` (`test/tests.cpp`) drives the real path end to end
  without any route plumbing: captures a snapshot mid-training via the
  same `on_epoch` mechanism the executors use, reloads it through
  `CheckpointModelStore`, resumes training from it, and asserts the
  resumed run's final loss is strictly lower than an equivalent cold run
  over the same epoch count from zero-initialized weights — the concrete,
  measured evidence that resuming genuinely continues a previous run
  rather than silently restarting it.
- Phase 80: Fully implemented (2026-08-17) — Experiment Tracking (section
  25 below) becomes a real executor phase like Phase 56/57/70 before it,
  closing the "version/hyperparameter/metric/artifact/comparison fields
  deferred to the phase that actually executes and records a run" gap
  Phase 44's own class comment named. `Experiment` (`src/masterai.hpp`)
  gained every section-25 definition-time field (`hyperparameters_json`,
  `random_seed`, `source_code_version`, `configuration_version`,
  `container_version`, `tags`, `notes`, `started_at_epoch_seconds`,
  `completed_at_epoch_seconds`, `failure_reason`), plus a fix to
  `ExperimentStore::restore()`, which previously never restored
  `created_at`/`updated_at_epoch_seconds` from the persisted record at all
  — both now round-trip correctly. `POST /api/v1/ml/experiments/{id}/run`
  (`ml.experiments.manage`) is the real executor
  (`execute_experiment_run`, `src/server.cpp`): trains the experiment's
  dataset content via `train_tabular_model` exactly like
  `execute_training_job` (same stride-capped-at-ten checkpoint capture
  through `TrainingCheckpointStore`/`CheckpointModelStore`, same trained-
  model registration into `ModelRegistryState::evaluation`), then runs an
  *independent* `evaluate_tabular_model` pass against the full dataset for
  "evaluation metrics" distinct from the held-out "validation metrics"
  `train_tabular_model` already computed — three genuinely different
  numbers for section 25's three separate metric fields, plus real probed
  hardware (`probe_hardware`) and measured wall-clock runtime, bundled by
  the new `experiment_result_json()` (`src/ml_engine.cpp`) and stored in a
  new `ExperimentResultStore` (the same opaque-JSON-blob pattern
  `EvaluationResultStore`/`ComparisonResultStore` already use), recalled
  via `GET .../result` and cascade-removed on `.../delete`.
  `ExperimentStore` gained `mark_started()`/`mark_completed()` (real
  start/completion timestamps and failure reason, distinct from
  `set_status()`'s generic `updated_at` touch) and `update_metadata()` for
  post-creation edits to notes/tags/hyperparameters/version strings. New
  `POST /api/v1/ml/experiments/compare` (`ml.experiments.view`) builds a
  genuine side-by-side diff of two or more already-run experiments
  (`experiments_comparison_json()`, `src/ml_engine.cpp`): parameter/
  dataset/seed/version differences, the same primary-metric choice
  `tabular_model_comparison_json` already uses (macro F1 for
  classification, MSE for regression), a metric delta, a regression flag,
  runtime delta, and a hardware-differs flag, relative to the first id as
  baseline — safety differences are reported as `null` with an explicit
  "no safety-scoring executor exists yet" note rather than fabricated,
  the same honesty convention `ModelComparison`'s class comment already
  sets. The web UI's Experiment Tracking page gained the new definition-
  time fields on its create form, a "Run now" action showing the real
  training/validation/evaluation metrics (mirroring Training Jobs' "Train
  now" panel), and a "Compare experiments" control. Extended
  `test_machine_learning_experiment_tracking_lifecycle`
  (`test/tests.cpp`) covers every new field's create/reload round trip
  (including the created/updated-at reload fix),
  `mark_started`/`mark_completed`/`update_metadata`, a real
  `experiment_result_json` built from an actually-trained model's genuine
  metrics, `ExperimentResultStore` round-tripping, and
  `experiments_comparison_json` correctly flagging a regression when a
  weaker (label-flipped-data) model is compared against a stronger one,
  flipping the verdict when baseline/candidate swap, and rejecting fewer
  than two ids or an unknown id.
- Phase 81: Fully implemented (2026-08-17) — Prompt and Instruction
  Training (section 19 below) gains the real content record and admin
  operations Phase 47's class comment deferred: "generate draft examples,"
  "test instructions against multiple models," "detect contradictory
  instructions," "detect duplicated examples," and "validate structured
  outputs." `InstructionExampleStore` itself is unchanged (still the
  identity/dataset/subject/lifecycle record); a new
  `InstructionExampleContentStore` (`src/masterai.hpp`/`src/ml.cpp`) holds
  the full section-19 record body (system instruction, user instruction,
  context, expected response, rejected response, tool calls, tool
  results, required output format, difficulty, safety classification)
  keyed by example id, the same identity/content split
  `DatasetStore`/`DatasetContentStore` already use. `POST
  /api/v1/ml/instruction-examples/{id}/content` (`ml.instructions.manage`)
  upserts it and `GET .../content` (`ml.instructions.view`) reads it back.
  `POST .../generate` composes a prompt from the given system/user
  instruction and context and calls `Server::execute_rag_generation`
  (Phase 76's RAG-answer helper, reused as-is -- it already handles model
  loading, chat templating, and scheduler admission) to produce a real
  draft example with genuine generated text as `expected_response`,
  starting `draft` like every manually-created example. `POST
  .../{id}/test` loads an example's stored content and loops
  `execute_rag_generation` once per administrator-selected model id,
  returning each model's real response and elapsed time -- a fan-out
  probe, not new persisted content. `POST .../import` bulk-creates
  examples (with optional content) from one request body, reporting each
  entry's own success/failure rather than an all-or-nothing result. `POST
  .../{id}/validate` runs the new `validate_structured_output()`
  (`src/masterai.hpp`/`src/ml.cpp`): when `required_output_format` names
  JSON, it real-parses `expected_response` via the existing `parse_json`
  and reports the parser's own error on failure; any other format has no
  checkable grammar in this codebase and passes through, noted honestly
  rather than fabricating a verdict. `POST .../duplicates` and `POST
  .../contradictions` (body-based, matching
  `/api/v1/ml/experiments/compare`'s own precedent -- this codebase has no
  query-string parsing utility anywhere else) run the new
  `detect_duplicate_instruction_examples()`/
  `detect_contradictory_instruction_examples()`: a documented heuristic
  (Jaccard token overlap on normalized instruction text, threshold 0.85)
  flags near-duplicate pairs, and pairs that are near-duplicate on
  instruction text but disagree on `required_output_format`, or where one
  example's `expected_response` equals another's `rejected_response`, are
  flagged as contradictions -- explicitly not a claim of semantic
  understanding, the same honesty convention the rest of this module
  uses. Section 19's "generated training examples must require approval
  before entering an approved dataset" is now concretely enforced: the
  `.../status` handler refuses a transition to `approved`
  (`400 ml_instruction_example_not_reviewable`) unless a content record
  already exists. `.../delete` cascades to remove the content record.
  The web UI's Instruction Training page gained a content edit form (all
  ten fields, pre-filled by a new "Configure" action per row), a
  "Generate draft" form, a "Test against models" form, "Check
  duplicates"/"Check contradictions" controls, and a "Validate" action
  per row. Extended `test_machine_learning_instruction_training_lifecycle`
  (`test/tests.cpp`) covers `InstructionExampleContentStore` round-
  tripping and reload, `validate_structured_output` on valid/malformed
  JSON and a pass-through format, `instruction_examples_are_near_duplicate`
  and `detect_duplicate_instruction_examples` correctly flagging a near-
  identical pair and skipping an unrelated one, and
  `detect_contradictory_instruction_examples` flagging a disagreeing-
  output-format pair and a direct expected/rejected-response collision
  while leaving unrelated examples unflagged. `.../generate` and
  `.../test`, like every other `execute_rag_generation`-dependent path in
  this codebase, need a loaded model and are exercised the same way those
  are (manual/integration testing), not a new inference stub.
- Phase 82: Fully implemented (2026-08-17) — Deployment Manager, Inference
  Endpoints, and Synthetic Data (sections 34, 35, 20) all move from
  `"planned"` to `"available"` in `MachineLearningRegistry`'s roster
  (`src/ml.cpp`).
  **Synthetic Data** (section 20) gains the real generation executor Phase
  48's class comment deferred: a new `SyntheticRecordContentStore`
  (`src/masterai.hpp`/`src/ml.cpp`), the same identity/content split Phase
  81 gave Prompt and Instruction Training, holds generator model, generator
  version (resolved from `ModelRegistryStore` when available), the composed
  prompt, generation settings (the technique string), the real generated
  text, a documented heuristic confidence score (1.0 if generation
  completed uncancelled with non-empty text, 0.0 otherwise -- this codebase
  has no per-token logprob to compute a real probability from), and
  optional source-record linkage. `POST /api/v1/ml/synthetic-records/
  generate` (`ml.syntheticdata.manage`) composes a technique-specific
  prompt from a fixed lookup table covering all thirteen of section 20's
  named operations (falling back to a generic template for any other
  free-text technique, keeping the field as open-ended as Phase 48 left
  it), calls `Server::execute_rag_generation`, creates the `SyntheticRecord`
  (still starting `draft`), and stores the result. `GET .../{id}/content`
  reads it back; `.../delete` cascades to remove it. The web UI's Synthetic
  Data page gained a "Generate synthetic record" form (dataset, model,
  technique dropdown, source text).
  **Deployment Manager** (section 34) gains its own real deploy/health/
  rollback action instead of relying solely on Automation Pipelines' stage
  executor. `Deployment` gained `health_status`, `deployed_at_epoch_
  seconds`, and `previous_deployment_id` fields. `DeploymentStore::deploy()`
  requires the caller (server.cpp) to have already confirmed an approved
  `ModelCard` exists for the deployment's model -- the exact same gate
  `AutomationPipeline`'s "Request approval"/"Deploy" stages already
  enforce (`run_safety_tests_stage`), so approving through this module's
  own API is held to the identical bar as approving through a pipeline
  run -- and computes a real (not fabricated) health signal from whether
  `TrainedModelStore` holds trained weights for the model (`"trained_
  weights_present"` or the honest `"unverified"`, since an LLM-backed
  model reached only through `execute_rag_generation` has no tabular
  artifact to check). Deploying supersedes (rejects) any other deployment
  already `approved` for the same environment and records its id as
  `previous_deployment_id`; `DeploymentStore::rollback()` reverses that:
  rejects the current deployment and re-approves the one it superseded.
  `POST /api/v1/ml/deployments/{id}/deploy` and `.../rollback`
  (`ml.deployments.manage`) expose these. The web UI's Deployment Manager
  page gained "Deploy now"/"Rollback" row actions and Health/Deployed-at
  columns.
  **Inference Endpoints** (section 35) needed no new execution: Phase 77
  had already given it a real socket listener (`run_inference_endpoint`),
  real Bearer auth, a real per-minute rate limiter, and live per-request
  safety-policy enforcement -- the roster comment simply hadn't been
  updated to say so and was still claiming "no live network listener...
  yet." The web UI's create form gained the `authToken` field the API
  already accepted but the form never exposed, and a new policy-editing
  form exposes the previously API-only `POST .../{id}/policy` route
  (content scan enabled, block-on-finding for prompt/answer, safety
  policy id, model classifier enabled/confidence floor).
  Extended `test_machine_learning_synthetic_data_lifecycle` and
  `test_machine_learning_deployment_lifecycle` (`test/tests.cpp`) cover
  `SyntheticRecordContentStore` round-tripping and reload, and
  `DeploymentStore::deploy()`/`rollback()`'s refusal-without-an-approved-
  card, real health-status computation, supersede-on-redeploy, and
  rollback-restores-the-previous-deployment behavior, at the store level
  the same way Phase 55's own test does. No new HTTP-layer test was added
  for `run_inference_endpoint` (`POST /v1/completions` auth/rate-limit
  enforcement): this test suite has no existing precedent for a test that
  opens a real listening socket, and inventing one was out of scope for
  this pass -- that surface remains covered by manual/integration testing
  only, the same honestly-stated gap Phase 81 left for its own
  `execute_rag_generation`-dependent routes.
- Phase 83: Fully implemented (2026-08-17) — Safety and Governance (section
  2 item 22, section 40) moves from `"planned"` to `"available"` in
  `MachineLearningRegistry`'s roster (`src/ml.cpp`). No new executor was
  needed: Phase 65 already gave it real policy/model-card approval CRUD,
  Phase 74 already gave it real heuristic content scanning
  (`scan_content_for_risks`) and a real LLM-as-judge classifier
  (`scan_content_with_model_classifier`) for bias/hallucination/harmful
  content, and Phase 82 already made `DeploymentStore::deploy()` genuinely
  refuse to deploy without an approved `ModelCard` and made
  `run_inference_endpoint` genuinely enforce a policy's content-scan/
  block-on-finding settings on every real request — the roster entry
  simply hadn't been updated to say so, the same stale-tag gap Phase 82
  found and fixed for Inference Endpoints. Added
  `test_machine_learning_safety_governance_lifecycle` (`test/tests.cpp`),
  the store-level test this module was missing: `SafetyGovernanceStore`
  policy/model-card create/find/list/status/remove and reload-survival, and
  `scan_content_for_risks()`'s four real behaviors (clean text stays clean,
  an AWS-shaped secret token is caught unconditionally, prompt-injection
  phrasing is caught unconditionally, and a restricted-data-category term
  is only matched when a `SafetyPolicy` is supplied). Honest remaining gap,
  stated directly in the roster comment: this governs content this
  codebase's own inference paths generate, not dataset ingestion sources,
  PII/copyright/data-poisoning detectors, or retention/export/network
  policy enforcement from section 40's full list — none of those have an
  executor in this codebase. Also fixed an unrelated web UI layout bug on
  the same page: the Inference Endpoints policy form's four checkbox
  labels were missing the `checkboxLabel` CSS class every other checkbox
  label in this codebase uses, so the checkbox and its text wrapped onto
  separate lines instead of sitting on one line (`src/web_ui.cpp`).
- User administration: the Users panel gains a per-row Disable/Enable
  action. `UserStore::set_enabled()` (`src/storage.cpp`) flips the
  already-persisted but previously unwired `UserRecord::enabled` field;
  `POST /api/v1/users/{id}/status` (`users.manage`) exposes it, and every
  authenticated request already refused a disabled account (`!user->enabled`
  in the request handler, present since the field was added) so this is
  disable, not delete — a user's id, audit history, and any records owned
  by that id are left intact, and re-enabling restores authentication
  immediately. Refuses to let an administrator disable their own account (a
  400, checked before touching the store) so a single click can never lock
  every administrator out.
- Dataset Manager content-upload format completion (docs/PLAN.md "Machine
  Learning Abilities" section 10): `POST /api/v1/ml/datasets/{id}/content`
  originally accepted CSV text only; a new `format` request field (default
  `"csv"`, so every existing caller is unaffected) also accepts `"json"` (a
  top-level JSON array of flat objects), `"jsonl"` (one flat JSON object per
  line), and `"parquet"` (base64-encoded bytes, the same convention the
  Knowledge ingestion Parquet upload already uses). `json_records_to_csv`/
  `json_array_to_csv`/`jsonl_to_csv` (`src/ml_engine.cpp`) convert all three
  to the exact CSV text `parse_tabular_csv` already validates -- the header
  is every key seen across records (alphabetical, since `JsonValue::Object`
  is a `std::map`), a record missing a key renders that cell empty, and a
  nested array/object field is rejected with a clear reason. Parquet reuses
  the existing `parquet_bytes_to_json` DuckDB helper (its output is already
  one JSON object per line) rather than adding a second Parquet path.
  `DatasetContentStore` and every downstream trainer/evaluator/comparator
  stay CSV-only and unchanged -- this is purely an upload-time conversion.
  The web UI's dataset content form picks the format from the uploaded
  file's own extension (`.csv`/`.json`/`.jsonl`/`.parquet`) so nothing else
  on the form changes between formats, and raised the client-side size
  guard from 8 MiB to 32 MiB (`configuration.tabular_dataset_maximum_csv_
  bytes` still enforces the real, administrator-configurable ceiling
  server-side). Covered by new assertions in
  `test_machine_learning_real_training_and_prediction` (`test/tests.cpp`):
  a JSON array and the equivalent JSONL text produce byte-identical CSV, a
  missing field renders as an empty cell, and a nested field is rejected.

Priority note: **Phase 30A CPU-only/GPU-disabled low-memory operation is
implemented (2026-08-02)**, closing the integration/validation gap that
turned the previously separate Phase 14/19/21/26/27/30 controls into a
coherent CPU-only operating mode: no GPU allocation attempts under
`cpu_only`, a pre-load `runner_weights` admission check, one-slot admission
via `MemoryPolicy::maximum_active_inference`, an unattended idle-release/
pressure-trim sweep, and administration visibility/configuration for all of
it. **The matched real-model benchmark matrix (`auto` vs `cpu_only` on a
pinned local GGUF) is now recorded (2026-08-13)** — see the Phase 30A status
entry and `docs/performance/phase-19-qwen3b-matrix.md` — closing Phase 30A's
last outstanding deliverable. Phase 19's GPU utilization/thermal-trend
telemetry is also now wired into `CalibrationService` with real measured
evidence (2026-08-13, see the Phase 19 status entry), closing that phase's
last outstanding deliverable too. **Phase 31 storage tiering is implemented
(2026-08-13)**, closing the Priority A/B `ScratchVolumeManager`/storage-aware
placement gap; **its Priority B tier-migration workflow, including the
central migration manifest, is also now fully implemented (2026-08-13, this
pass)** — see its own status note for `migrate_durable_file()`,
`DurableFileManifest`, and `resolve_durable_path()`; both Priority B gaps
(the file operation itself and the "no central manifest" follow-up) are now
closed. **Phase 33 is now fully implemented (2026-08-13)** -- both the
earlier local multi-runner orchestration half and, that same pass, the
intranet/mTLS worker protocol half (private PKI, mutual TLS, model-digest
verification, `WorkerListener`/`IntranetWorkerPool`); **automatic
remote-worker failover on the live chat-generation dispatch path is also now
wired in (2026-08-13, this pass)**, closing the one honest scope note that
remained -- see its own status note for `try_remote_worker_failover()`.
**Phase 34 is now implemented at a scoped-down level (2026-08-13)** -- a
real, bounded, hysteresis-guarded `AdaptiveController` with every stability
control the plan requires, applying live to the one genuinely mutable target
(`MemoryBudgetManager::set_policy()`) and disclosing every other knob as a
recommendation; see its own status note. **Phase 35 is now implemented at a
scoped-down level (2026-08-13; extended 2026-08-13, this pass)** -- one
consolidated, fully real Performance administration page, now covering
Memory, Caches, Storage (including the Phase 31 migration manifest),
Scheduling, Advanced Optimizations, and Calibration in addition to the
original Overview/Adaptive Controller and runner-pool sections; see its own
status note for exactly which named pages still have no telemetry route to
render and therefore remain deferred. **Speculative decoding (Phase 32) now
has a real dual-model launch path (2026-08-13)** -- `LlamaCppAdapter::
build_launch_spec` emits llama.cpp's documented `--model-draft` flags,
`select_speculative_draft_candidate()` picks a compatible draft from the
verified registry, a new `SpeculativeDecodingPairEvidenceStore` holds
administrator-submitted per-pair measured acceptance rates, and
`server.cpp`'s `ensure_model_loaded()` wires all of it together behind the
same `AdvancedOptimizationRegistry` evidence/admission gate
`continuous_batching` already uses -- closing the "no dual-model launch path
yet" gap the original pass named. **The sampling-compatibility gate is now
corrected (2026-08-13, this pass)**: `sampling_supported_by_speculative_
verification` (renamed from the overly strict `sampling_is_greedy_or_
deterministic`) correctly reflects that llama.cpp's rejection-sampling
verification algorithm supports any temperature/top-p/top-k/repeat-penalty
sampling, not only greedy decoding, so speculative decoding now activates
for live chat traffic once admitted and evidenced, closing that gap. The
decision logic itself (compatibility checking, acceptance-rate tracking,
per-request enable/disable) is otherwise unchanged. Still explicitly
unvalidated: no real-hardware run has yet exercised the new launch path --
that measurement requires an administrator to actually run one, which this
control plane never does on its own. **Phase 36 is now implemented at a
scoped-down level (2026-08-17)** -- a real quality-plus-five-regression-
check-group certification pass with threshold-gated build-to-build
comparison; see its own status note for exactly which physical-hardware
matrix dimensions still require an administrator to run this control plane
on real target hosts.

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
  the real-hardware-class exit benchmark remain outstanding. The GPU vendor
  SDK blocker itself is resolved (2026-08-13): NVML/ADLX are now approved
  per ADR-0001, with a dynamic-loading probe available at
  `probe_gpu_vendor_telemetry()` (`src/gpu_vendor.cpp`); wiring that
  telemetry into `CalibrationService` and the benchmark matrix is
  unstarted.
- Phase 20 originally landed as a read-only scaffold. It was superseded on
  2026-08-05 by the durable admission implementation described in the current
  status and detailed Phase 20 entry: strict evidence validation, restart-safe
  persistence, implementation availability, explicit admit/disable actions,
  audit, safe-profile retention, API mutation, and expanded native coverage.
  This historical entry no longer describes the current interface.

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
  real-source Phase 6 resume certification, and same-host real-model Phase 7
  comparison are each implemented and real-hardware validated (see their own
  status entries, dated 2026-08-02/2026-08-05); optional whisper.cpp
  integration and MCP transports remain outside those exit criteria and are
  not yet validated.
- Ubuntu 24.04 and Debian 13 packaging certification (deferred by operator)
  plus later-phase conformance and performance suites.
- Phase 19's real-hardware-class calibration benchmark is implemented and
  validated, including GPU utilization and thermal-trend probing (wired into
  `CalibrationService` 2026-08-13, see its own status entry — the vendor SDK
  approved per ADR-0001/`src/gpu_vendor.cpp` is now consumed, not just
  available). Phase 20's optional candidates remain unadmitted behind the durable
  evidence/admission gate until each candidate's own evidence is produced;
  an unadmitted candidate is not an incomplete or implied optimization.
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
GET    /api/v1/memories
POST   /api/v1/memories
POST   /api/v1/memories/{id}/delete
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
- Per-user remembered details: explicit `save to memory:` command, bounded
  deterministic self-disclosure capture, model-independent later-turn recall,
  and visible add/forget management.

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

## 25. Performance, Retrieval, Caching, and Low-Memory Architecture

### 25.1 Planning boundary and optimization rule

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

### 25.2 Hardware profiles and initial defaults

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

### 25.3 End-to-end query pipeline

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

### 25.4 Runtime components and responsibility boundaries

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

### 25.5 Model loading, eviction, virtual memory, and scratch storage

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

### 25.6 Index and retrieval storage design

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

### 25.7 Cache and authorization design

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

### 25.8 Configuration surface

The strict schema may add these versioned domains during their owning phase:

```json
{
  "performance": {
    "profile": "auto",
    "interactiveDeadlineMs": 2000,
    "preferLowTimeToFirstToken": true,
    "backgroundWorkDuringInference": "throttle",
    "autoTune": true,
    "preferMemoryEfficiency": true
  },
  "memory": {
    "policy": "adaptive",
    "hardLimitMiB": 0,
    "minimumOsReserveMiB": 2048,
    "minimumFreePercent": 15,
    "criticalPressurePercent": 92,
    "cpuOnlyHardLimitMiB": 0,
    "maximumTransientRequestMiB": 256,
    "maximumMappedModelMiB": 0,
    "trimWorkingSetAfterUnload": true,
    "releaseIdlePoolsSeconds": 120
  },
  "hardware": {
    "acceleratorPolicy": "auto",
    "allowGpu": true,
    "requireCpuFallback": true,
    "failIfGpuRequestedWhileDisabled": true
  },
  "inference": {
    "maxActiveRequests": 1,
    "maxQueuedRequests": 8,
    "idleUnloadSeconds": 600,
    "loadMode": "auto",
    "warmup": "minimal",
    "continuousBatching": "auto",
    "speculativeDecoding": false,
    "cpuOnlyMaxActiveRequests": 1,
    "cpuOnlyContextTokens": 2048,
    "cpuOnlyMaxReplyTokens": 2048,
    "cpuOnlyBatchTokens": 128,
    "cpuOnlyMicroBatchTokens": 64,
    "cpuOnlyThreads": 0,
    "cpuOnlyUseMmap": true,
    "cpuOnlyUseMlock": false,
    "cpuOnlyKvPlacement": "cpu",
    "cpuOnlyGpuLayers": 0
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

`hardLimitMiB: 0`, `cpuOnlyHardLimitMiB: 0`, and
`maximumMappedModelMiB: 0` mean derive safe host-specific limits, never
unlimited. `hardware.acceleratorPolicy` accepts only `auto`, `cpu_only`, or
`gpu_allowed`; `cpu_only` forces zero GPU layers, CPU KV placement, no GPU
backend initialization, and rejection of any adapter setting that would attempt
a GPU allocation. `cpuOnlyThreads: 0` means use a calibrated safe value rather
than every logical processor. Unknown fields remain rejected. Backend-specific
controls stay within validated adapter namespaces instead of leaking across the
general configuration surface.

### 25.9 API and UI surface

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

### 25.10 Proposed ISO C++17 source boundaries

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

### 25.11 Expanded benchmark and degradation matrix

Performance validation covers cold/warm OS cache, cold/warm runner, cached and
uncached prefix, context sizes from 512 through 8,192 tokens where the host can
fit them, exact/lexical/semantic/hybrid retrieval, incremental index updates,
mapped-index cold/warm reads, one/two compatible requests, indexing/download
contention, cancellation, disconnect, and queue saturation.

Each accepted optimization records baseline, changed setting, host, model and
backend hashes, TTFT, prompt and generation throughput, peak resident/commit
memory, page faults, quality, and power/thermal notes.

Degradation order is deterministic:

1. Stop speculative prefetch, warm-up, and optional advanced features.
2. In CPU-only mode, prevent new background CPU work and collapse inference to
   one active sequence before any additional allocation.
3. Pause or reduce background indexing, downloads, compaction, and ML jobs.
4. Trim file, retrieval, tokenization, prompt, embedding, and inactive pool
   caches, releasing empty allocator blocks where supported.
5. Reduce retrieval chunks, reply allowance, and request context; rebuild the
   request estimate before admission.
6. Reduce parallel sequences to one and refuse creation of extra KV slots.
7. Unload the embedding model and nonessential helper/router models.
8. Unload idle generation runners, close mapped views, and request working-set
   trimming only after owned buffers and mappings are released.
9. Reject new work with a safe, actionable diagnostic rather than permitting
   destructive paging.

Slow storage reduces random-read fan-out and avoids rescans; thermal decline
reduces background workers and uses measured inference-thread settings. No
degradation step weakens security, integrity, authorization, or audit controls.

### 25.12 Completion acceptance

The performance expansion is complete only when:

- The control plane remains small and never loads model weights.
- Every queue, cache, worker pool, context, and transient buffer has an enforced
  ceiling and cancellation path.
- Low-memory mode operates with one indexing worker and no persistent model.
- CPU-only mode performs no GPU discovery-to-allocation transition, launches
  the backend with zero GPU layers and CPU KV placement, and reports zero
  MasterAI-attributed GPU allocation attempts.
- CPU-only admission accounts for mapped/resident model pages, CPU compute
  buffers, KV cache, tokenizer state, prompt/retrieval materialization,
  allocator/pool reserve, pagefile/swap commitment, and the OS safety reserve.
- Project indexes are incremental, disk-backed, checksummed, and recoverable.
- Retrieval returns compact, disclosed, authorized context within a deadline.
- Repeated requests benefit from safe cache/prefix reuse where compatible.
- A model predicted to cause destructive paging is rejected or downgraded.
- Background work yields to interactive inference.
- Calibration never overrides the hard RAM cap.
- Advanced optimizations have independent switches, evidence, and safe
  fallbacks.

### 25.13 Limited-hardware investigation: stage mapping and diagnostic order

This subsection closes the loop between the Limited-Hardware Inference
Performance Investigation and Optimization work order and the phase roadmap
in section 26: no new parallel performance subsystem is authorized, and every
item below already has an owning phase.

| Work-order stage | Owning phase(s) |
| --- | --- |
| Stage 1 — attribute latency, direct-runner comparison, Qwen 3B record | Phase 13, Phase 19 |
| Stage 2 — safe low-memory defaults, explicit CPU-only/GPU-disabled mode, one slot, OS reserve, destructive-paging detection | Phase 14, Phase 19, Phase 27, Phase 30A |
| Stage 3 — calibrated runner tuning matrix (threads/batch/ubatch/GPU layers/context/KV) | Phase 19 |
| Stage 4 — prompt-prefix reuse, tokenization/template caching, staged retrieval, compaction | Phase 18, Phase 23, Phase 24 |
| Stage 5 — model routing and tiering wired into the live request path | Phase 29 |
| Stage 6 — storage classification, async reads, cold/warm load benchmarking | Phase 21, Phase 26, Phase 31 |
| Stage 7 — bounded adaptive tuning and the Performance administration sidebar | Phase 34, Phase 35 |
| Stage 8 — full benchmark matrix, regression thresholds, release gating | Phase 36 |

Every stage-1 latency question is answered in the fixed order defined in
Phase 13: queue wait, then retrieval/prompt assembly, then cold-load time,
then prompt-evaluation rate, then generation throughput, then — only after
those are ruled out — the direct-runner comparison decides whether the
remaining cost sits in `llama-server.exe`/the model/the hardware or in
MasterAI's own control plane. No stage may be skipped to reach a conclusion
faster, and no component is blamed without the attribution evidence Phase 13
requires.

## 26. Phased Implementation Roadmap

### Implementation priority override — memory-first CPU-only operation

**Phase 30A is implemented (2026-08-02)**; only its real-model benchmark
matrix remains outstanding (see the priority note above). **Phase 31 storage
tiering, including its Priority B tier-migration workflow, is fully
implemented (2026-08-13)**. Phases 32–35 must not become the primary
implementation target until Phase 30A's benchmark evidence is addressed.
Existing completed phases are extended rather than replaced, and no
completed validation status is retroactively claimed for requirements that
still need real-model/hardware evidence.

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

Status: Complete (revalidated 2026-08-05). The current Windows Debug native
suite passed; the server validated configuration, served HTTP 200 from both
health endpoints on loopback port 7070, and shut down gracefully through the
documented lifecycle script. Pinned Linux packaging-host certification remains
a release gate outside this phase's exit criterion.

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

Status: Complete (revalidated 2026-08-05). The current native suite passed,
including configuration, identity, session/token, secret-store, request-policy,
audit, and intranet-rejection coverage; `security-status` also confirmed the
native identity and non-password secret providers are available and the
listener policy remains loopback-only.

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

Status: Complete (revalidated 2026-08-05). The current native suite passed its
manifest, model-integrity, hardware-suitability, inventory, and unsafe-load
checks; the existing verified-model cache and load-time integrity boundary
remain authoritative for operational models.

Deliverables:

- Category directory scanner.
- Model manifest schema.
- Hardware probes.
- Suitability engine.
- Model inventory API and UI.

Exit criteria:

- Installed models are classified accurately and unsafe loads are blocked.

### Phase 4 — First inference adapter

Status: Complete (current native suite revalidated 2026-08-05; real-model exit
validated 2026-08-02) — `qwen25-coder-3b-q4km` answered an authenticated API
chat request through the pinned `llama.cpp` runner, isolated in its own
supervised process. See the Phase 4 entry in Document Status above for
details.

Deliverables:

- Runner supervisor.
- `llama.cpp` adapter.
- Load, unload, tokenize, generate, stream, and cancel operations.
- Resource monitoring.

Exit criteria:

- A supported programming model can answer through the API without placing model memory in the main process.

### Phase 5 — Chat and project web application

Status: Complete (validated 2026-08-05). An isolated actual-browser session
completed authentication, project/chat creation, Ready-model selection, and a
real streamed model response with no rendered error card. Foreground messages
now serialize with the bounded pre-warm load, closing the `starting`-state race
found during this validation. Durable owner-scoped user memory is also wired
into the chat path independently of inference: a whole-message `save to
memory:` command is confirmed directly, deterministic self-disclosures are
captured under fixed limits, saved data is recalled once into a persisted chat
snapshot and reused on later turns without system-instruction priority, and
users can inspect/add/delete entries through the Memory sidebar and
`/api/v1/memories`. The optional transcription adapter
boundary is implemented; a whisper.cpp installation remains an optional
integration. As of 2026-08-06 the model selector also carries a persisted
per-model Effort/Thinking settings panel (see Document Status above for the
full description), chat replies render Markdown links and bare URLs as real
anchors, and the global system-error indicator is a contained, auto-fading
floating bubble with a copy action instead of a full-width top banner.

Deliverables:

- Login UI.
- New Chat and history.
- Projects.
- Model selector, with a per-model Effort/Thinking settings panel
  (persisted client-side; applied server-side as a reasoning directive for
  reasoning-capable architectures or a sampling preset otherwise).
- Streaming responses, with Markdown links and bare URLs rendered as real
  anchors.
- Attachments.
- Context display.
- Inspectable, owner-scoped remembered user details.
- Basic voice recording and transcription adapter interface.
- Contained, auto-fading system-error notification with a copy action.

Exit criteria:

- End-to-end authenticated programming chat works from the browser.

### Phase 6 — Secure downloads

Status: Complete (validated 2026-08-05). A deliberately interrupted immutable
Hugging Face GGUF resumed from 200,000 of 1,185,376 bytes, matched its published
SHA-256, promoted without leaving a `.part`, entered the verified cache, and
scanned `Ready` from isolated roots. Settings-backed CLI commands now share the
service's approved environment-override precedence, preventing validation or
administration from silently targeting different roots.

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

Status: Complete (validated 2026-08-02) — same-host real-model comparison
exit validation recorded: `qwen25-coder-3b-q4km` and `llama32-3b-instruct-q4km`
both ran the quick benchmark suite on the same host and hardware id. See the
Phase 7 entry in Document Status above for details.

Deliverables:

- Quick, standard, and extended profiles.
- Result persistence.
- Comparison UI.
- Model recommendation inputs.

Exit criteria:

- Benchmarks are reproducible and can compare candidate models on the same host.

### Phase 8 — MCP inbound

Status: Complete (validated 2026-08-05). A live independent Streamable HTTP
client authenticated with a short-lived project-bound token, negotiated the
pinned protocol, listed four tools, listed only its bound project resource,
and then revoked its token.

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

Status: Complete; live VS Code and Visual Studio 2022 host validation passed
on Windows x86-64 Debug (2026-08-05).

Deliverables:

- VS Code integration: the native MCP/HTTP bridge, secret-free Agent-Coder
  profile, and the narrowly authorized extension under `integrations/vscode`
  are implemented. Its real extension host negotiated `2025-11-25` and listed
  four tools while keeping its bearer token in VS Code `SecretStorage`.
- Visual Studio integration: the same native bridge and the Visual Studio 2022
  VSIX under `integrations/visual-studio` are implemented. The package uses
  current-user DPAPI, loaded through the supported background package API, and
  produced the same protocol and four-tool live evidence.
- Secure token setup: implemented with hidden console input, scope/user/project
  validation, DPAPI or native Linux protection, and audit.
- Chat, context, diagnostics, diff preview, and cancellation: implemented
  through backend-neutral versioned API/MCP contracts.

Exit criteria:

- Both IDE profiles target the generic local AI server without backend-specific
  logic, and both live host checks pass.

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

The 2026-08-05 maintenance pass additionally removes repeated Windows CNG
provider opens from authenticated SHA-256/password paths, replaces the
power-of-two frequency-sketch modulo with a mask, and gives both JSON escape
paths a complete 256-byte decision table with bulk clean-run copies. These
changes preserve strict bounds and output semantics and are covered by the
native correctness suite; they are not credited as new Phase 12 throughput
evidence until a comparable Release before/after probe is recorded.

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

Extended evidence required (limited-hardware inference performance work
order, folded into this phase rather than a parallel subsystem):

- Every `QueryTrace` also records model SHA-256/architecture/quantization/
  GGUF size, backend executable SHA-256 and `llama.cpp` build id, GPU
  backend/device, requested versus actually-offloaded GPU layers, CPU thread
  count, batch/micro-batch size, context length, parallel-slot count,
  prompt-cache/retrieval-cache/tokenization-cache hit state, runner warm/cold
  state, OS file-cache warm/cold state, available RAM/VRAM before load and
  before generation, process private/resident bytes, commit size, and
  page-fault counts, alongside the existing stage timers — closing the field
  list this phase's evidence must carry so a slow request can be attributed
  to a single stage rather than reported as one undifferentiated latency.
- A diagnostic mode that captures the exact fully templated prompt and
  active generation settings MasterAI sent for a query, replays the same
  prompt directly against the same pinned `llama-server.exe` (same model
  hash, context, GPU layers, threads, batch/micro-batch, parallel slots, KV
  settings, prompt-cache setting, sampling settings, and seed), and reports
  the stage-by-stage delta between the direct-runner run and the
  MasterAI-mediated run, so control-plane overhead can be distinguished from
  inference-plane cost with evidence instead of assumption.
- A terminal diagnostic conclusion (`Primary bottleneck: …`,
  `MasterAI overhead: N% of pre-generation latency`) attached to the trace,
  produced by walking the fixed decision tree: queue wait, then
  retrieval/prompt assembly, then cold-load time, then prompt-evaluation
  rate (token count, prefix reuse, batch/ubatch, threads, actual GPU
  offload, page faults, context/parallel KV allocation), then generation
  tokens/second (quantization/backend/offload, thermal/power state, stream
  buffering), before ever naming `llama-server.exe`, the model, or MasterAI
  itself as the cause.

Installation/completion outcome:

- The installed service exposes authenticated request/resource metrics and can
  produce a baseline report without enabling any speculative optimization.
- An administrator can ask "why was this response slow?" and receive the
  stage-attributed answer above instead of a single elapsed-time number.

Exit criteria:

- One request can be traced from admission through final token and cleanup with
  stage timing and peak-memory evidence.
- Debug and Release correctness results remain unchanged, and instrumentation
  overhead is measured and bounded.
- A same-host direct-runner-versus-MasterAI comparison and a real Qwen-class
  3B GGUF benchmark record (see Phase 19's benchmark-matrix deliverable) are
  on file, matching this phase's own real-model exit-validation pattern.

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

Status: Complete (validated 2026-08-05). `RetrievalPlanner`
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
exists yet. The authored hybrid-vs-full-text evaluation now passes 2/2 versus
0/2 within the configured deadline and context budget.

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
  transport directly. `evaluate_retrieval_quality` and the authored set in
  `docs/performance/phase-16-retrieval-evaluation.md` provide the formal
  full-text-only comparison and enforce both operational bounds.

Installation/completion outcome:

- Browser, API, IDE, and MCP clients receive prompt status quickly and can
  inspect the actual project sources supplied to the model.

Exit criteria:

- The authored retrieval evaluation set demonstrates improvement over
  full-text-only retrieval while meeting the selected interactive deadline and
  hard memory budget.
- Project membership or policy changes invalidate access immediately.

### Phase 17 — Security-partitioned cache hierarchy

Status: Complete (validated 2026-08-05). `CacheManager`
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
transition) remain forward hardening. The production-path benchmark now
builds a representative 64-file index, runs 32 cached and uncached
preparations, requires stable output, and fails unless cached preparation is
measurably faster; see `docs/performance/phase-17-cache-benchmark.md`.

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

Status: Complete (validated 2026-08-02) — see the Phase 18 entry in Document
Status above for the real-model repeated-turn evidence.

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
- A bounded calibration matrix, not a single guessed configuration: CPU
  threads (physical-cores-minus-one, physical cores, logical cores, and
  calibrated thermal-limited alternatives), batch size (128/256/512/1024
  where admitted), micro-batch size (backend-supported values no larger than
  the chosen batch), GPU-layer offload (CPU-only baseline through the
  largest safely admitted partial offload up to full offload, each reserving
  VRAM for KV cache, compute buffers, backend overhead, and a configurable
  safety margin), and context length (2,048 initially, 4,096 after
  admission, larger only with explicit calibration evidence) — the largest
  or most parallel value is never assumed fastest; the matrix records which
  candidates were rejected and why (VRAM exceeded, CPU fallback triggered,
  paging observed, allocation failure).
- A per-model benchmark report recording the best accepted profile (GPU
  layers, CPU threads, batch, micro-batch, context, parallel slots, KV
  placement, prompt-cache setting, load mode) alongside its measured cold
  load time, cold/warm prompt tokens/second, generation tokens/second, TTFT,
  peak runner RSS, peak commit, hard page faults, approximate VRAM use, and
  quality result, plus every rejected profile and its rejection reason —
  produced for any model under investigation (a Qwen-class 3B GGUF is the
  first required case per this phase's exit criterion) using the Phase 13
  query-trace fields and direct-runner comparison.

Installation/completion outcome:

- The installed service selects defensible defaults for the active machine and
  shows the measurement and trade-off behind each recommendation.

Exit criteria:

- On supported hardware classes, the selected profile either outperforms safe
  defaults or reduces peak memory without unacceptable quality regression and
  never exceeds the configured RAM cap.
- A same-host benchmark matrix and best-accepted-profile report exist for at
  least one Qwen-class 3B GGUF model, confirming actual GPU layers offloaded
  (not merely requested), ruling out CPU fallback on unsupported operators,
  and confirming prompt-evaluation throughput is within the accepted range
  for the host's GPU class before any launch-configuration change is
  credited with a fix.

### Phase 20 — Optional advanced throughput

Status: Complete as an optional evidence/admission layer (validated
2026-08-05). The registry and administrator API persist strict evidence,
separate recording from admission, require a wired implementation plus a
non-regressing result and verified fallback, permit independent disable, audit
mutations, and always retain the safe Phase 19 profile. No candidate is
currently admitted; unsupported or unmeasured candidates remain visibly
disabled rather than becoming implicit performance claims.

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

Validation evidence (2026-08-05): the strict C++17 Windows Debug build and
the complete native suite passed (49.30 seconds). Coverage proves default-off,
strict evidence rejection, record-without-admit, unavailable-implementation
rejection, regression rejection, explicit admission of an implemented
candidate, independent disable, and restart persistence; the existing bounded
download/index/cancellation and weighted-fair queue-saturation tests remained
green. A live isolated server returned six candidates with
`safeProfileAvailable=true`, accepted an authenticated/CSRF-protected disable,
restored that disabled state after restart, and shut down gracefully. Since no
candidate is admitted, the per-admitted-feature measurement clause currently
has no unsafe or unevidenced exception.

### Phase 21 — Native asynchronous storage and prefetch engine

Status: Implementation complete (2026-08-05). `src/async_storage.cpp`
provides a real `IAsyncFileReader` interface, a Windows IOCP/overlapped
`Win32OverlappedFileReader` backend, a POSIX `PosixPreadPoolReader` bounded
worker-pool backend, a measured `StorageLatencyProfile` probe, an adaptive
queue-depth policy, a pure read coalescer, and per-request cancellation
tokens. `read_file_ranges()` issues coalesced waves bounded by both queue
depth and temporary bytes, preserves caller order, and reports actual
physical reads. Windows cancellation reaches `CancelIoEx`. Wired into
`models.cpp` manifest reads and `indexing.cpp` segment
reads as an opt-in path with the pre-existing blocking path retained as an
automatic fallback (`read_file_bytes()`). Deliberately out of scope for this
pass: a Linux `io_uring` adapter (the POSIX
`pread` worker pool is used unconditionally on Linux instead, matching this
plan's explicitly-allowed fallback). `MappedBufferView`, shared with Phase
26/30 rather than duplicated here, supplies bounded memory-mapped regions.
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

Status: Implementation complete (2026-08-05) — see the summary
entry above (`src/cache.cpp`, `src/masterai.hpp`) for the full-category
expansion, streaming/probationary/protected/pinned eviction, admission gate,
immutable checksum-validated resident L1, and negative caching that shipped.
Priority A/B — file-metadata, file-content, and
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

Status: Implementation complete (2026-08-05). `RunnerSupervisor::tokenize()`
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
`reusable_prefix_bytes`, exact `reusable_prefix_tokens` when the recorded
session carries its backend token count, `divergence_offset`, an explicit
`SessionInvalidationReason` enum, and the configured
`prefix_byte_ceiling` (default 4 MiB, constructor-configurable), still using
exact-byte-prefix matching only (no fuzzy matching). Exact token reuse never
guesses from bytes: the server records the backend prompt-token count with
the session, and the decision marks whether that count is exact.

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

Status: Implementation complete (2026-08-05). `src/retrieval.cpp`
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
concurrent requests provably NOT joining. The authored corpus includes 200
decoy files and two expected-evidence cases. On the final Windows-x64 Release
run it measured Phase 24 at 213 us total/145 us high case versus the faithful
Phase 16 bounded-worker fan-out at 736 us/401 us, with both expected markers
recovered, no deadline violation, and no context-budget violation. This also
closes the Phase 16 authored-evaluation requirement.

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

Status: Implementation complete; activation remains calibrated and
default-off (2026-08-05). The live chat route uses `RequestScheduler` for
memory reservation, weighted-fair queueing, cancellable wait, status,
dispatch, and completion. `RunnerSupervisor` permits compatible concurrent
generations up to the calibrated slot count when `--cont-batching` is
explicitly admitted through Phase 20; otherwise it retains the one-generation
baseline. Priority C — depends on multiple
concurrent compatible requests being common enough to benefit measurably;
superset of the Phase 20 continuous-batching candidate.

Purpose:

- Increase throughput when multiple compatible inference requests are
  active, without harming single-request interactive latency.

Dependencies:

- Phase 14 bounded admission/priority work; Phase 20's durable, default-off
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

Status: Implementation complete (2026-08-05). `masterai.hpp`/
`calibration.cpp` add an explicit `ModelLoadMode` (streamed/mapped/resident/
auto) selected by `CalibrationService::resolve()` from a Phase 21
`StorageLatencyProfile` plus available-RAM evidence via `select_load_mode()`,
realized through the existing `--no-mmap`/`--mlock` launch arguments rather
than new backend flags. A `PreTouchLevel` enum (none/metadata/first-use/
layer-window/full) is actionable end-to-end: metadata and first-use touch a
bounded prefix, layer-window touches bounded distributed mapped windows, and
full remains the default-off backend `--mlock` path.
`inference.cpp` adds a `WarmModelState` state machine (Cold/LoadingMetadata/
MappingWeights/InitialisingBackend/Warming/Ready/Busy/Idle/Draining/
Evicting/Unloaded/Failed) layered onto `RunnerSupervisor`'s existing
`RunnerState`/`ModelState` via `WarmModelTracker`, an explicit legal-
transition graph, and a `translate_runner_state()` baseline table so every
pre-existing `RunnerState` consumer is unaffected (see the Phase 26
regression test). `run_cancellable_warmup()` provides cooperative-
cancellation background warm-up that yields on `MemoryBudgetManager`
pressure, a caller-supplied thermal/storage/interactive pressure signal, or
a `WarmupCancellationToken`. `ModelUsagePredictor` records
recency/pin/project-preference/waiting-request signals, reported via
`model_usage_signals_json()` following the existing `tuning_profile_json()`
convention. The live server records waiting and successful-use signals and
exposes administrator-only `GET /api/v1/models/usage-signals` plus audited,
CSRF-protected pin updates through `POST /api/v1/models/usage-signals`.
`direct` remains rejected where the external backend has no validated direct
I/O contract. Windows is the validated build/test target per project
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

### Phase 30A — CPU-only and GPU-disabled low-memory operation

Status: Implemented (2026-08-02), except the real-model benchmark matrix
(deliverable/implementation-order item 6's benchmark half) which remains
outstanding pending a pinned local GGUF and dedicated hardware run — the same
open-until-measured status every other real-model exit criterion in this
project (Phases 4-7) carries. This phase is an integration and hardening
phase over the existing Phase 14, 19, 21–27, and 30 controls; it does not
create a second memory manager or a second inference pipeline. Implementation
notes:

- Deliverable 1 (config schema/precedence/fail-closed launch):
  `AppConfig::accelerator_policy`, `CalibrationService`, and
  `LlamaCppAdapter::build_launch_spec()` — see `test_phase_thirty_a_cpu_only_
  accelerator_policy` in `test/tests.cpp`.
- Deliverable 2 (CPU-only admission estimator): `HttpServer::State::
  admit_runner_weights()` (`src/server.cpp`) reserves
  `MemoryCategory::runner_weights` against `MemoryBudgetManager` before
  `RunnerSupervisor::load()` is ever called, consulting `assess_model()` for
  a concrete rejection reason first — see
  `test_phase_thirty_a_runner_weights_admission`.
- Deliverable "one active request" (implementation-order item 3):
  `MemoryPolicy::maximum_active_inference` is enforced by
  `HttpServer::State::try_admit_inference_slot()` on every
  `send_chat_message()` call. Reduced context/KV/cache defaults and worker
  threading were already delivered by Phase 14/19's `MemoryPolicy`/
  `CalibrationService`; this phase's job was making the concurrency ceiling
  they define actually load-bearing, which it now is.
- Deliverable 4 (deterministic unload and pressure recovery): the new
  `MemorySweeper` class (`src/masterai.hpp`/`src/memory.cpp`) periodically
  applies `RunnerSupervisor::apply_idle_timeout()` and
  `CacheManager::trim()` — see `test_phase_thirty_a_memory_sweeper_idle_
  unload`. KV-slot eviction and request-arena reset remain at their existing
  Phase 27/30 per-request boundaries rather than duplicated into the sweep.
- Deliverable 5 (administration visibility): `GET /api/v1/runner/status`
  (requested-vs-actual GPU layers, load-time accelerator policy, unload
  countdown) and `GET /api/v1/system/memory` (pagefile/swap headroom,
  process commit bytes, hard-fault count, configured idle-unload seconds).
  An administrator-only `GET`/`POST /api/v1/admin/config`
  (`admin_config_get()`/`admin_config_put()` in `src/server.cpp`) round-trips
  the live `settings.json` through `ConfigurationManager`, applying the
  subset of fields every request path already re-reads live (accelerator
  policy, context/reply-token limits, retrieval/cache/session-reuse toggles,
  rate/body limits, allow-lists, sign-in toggles) immediately and reporting
  `restartRequired` for the rest; surfaced in the web UI's
  Settings → System configuration panel (administrator-only, `src/web_ui.cpp`).

Purpose:

- Allow an administrator to disable GPU use completely while keeping local
  inference, indexing, retrieval, chat, MCP, and administration usable within
  a strict RAM/commit/pagefile budget.
- Reduce steady-state and peak memory when GPU offload is unavailable,
  unreliable, undesirable, or explicitly disabled in configuration.

Dependencies:

- Phase 14 central memory authority and pressure states.
- Phase 19 launch tuning and host/model/backend fingerprints.
- Phase 21/26 storage-aware reads and mapped model loading.
- Phase 23/24/27/30 prompt, retrieval, KV, shared-buffer, arena, and pool
  accounting.

Deliverables:

- A strict `hardware.acceleratorPolicy` with `auto`, `cpu_only`, and
  `gpu_allowed`. In `cpu_only`, backend launch arguments force zero GPU layers,
  CPU KV placement, and no GPU-specific attention/kernel option; conflicting
  saved, CLI, API, environment, or calibrated settings are rejected instead of
  silently overriding the administrator's choice.
- A GPU-disabled startup path that may enumerate hardware for diagnostics but
  must not load CUDA, Vulkan, HIP, SYCL, Metal, DirectML, OpenCL, or vendor
  management libraries, create GPU contexts, reserve VRAM, or start a runner
  that reports an accelerator allocation. The selected backend executable and
  its reported devices are recorded in the query/run trace.
- A CPU-only admission estimator that separately accounts for mapped model
  bytes, expected resident model pages, backend graph/compute buffers,
  tokenizer state, KV bytes per slot, prompt/retrieval buffers, cache/pool
  reserves, runner process overhead, concurrent control-plane work, commit
  charge, pagefile/swap headroom, and the existing OS safety reserve.
- CPU-only safe defaults: one active request, one KV slot, 2,048-token default
  context, bounded reply allowance, small batch/micro-batch, calibrated worker
  threads rather than all logical processors, `mmap` preferred when validated,
  `mlock` disabled, minimal warm-up, no persistent idle runner by default, and
  background indexing/ML/download work paused or throttled during inference.
- Memory reduction actions applied before load and between requests: compact
  prompt/retrieval materialization, lazy attachment reads, no duplicate model
  verification buffers, bounded tokenizer/prompt caches, request-arena reset,
  empty pool-block release, expired session/KV eviction, mapped-view closure on
  unload, and process working-set trimming only after owned allocations are
  released.
- A CPU-only model suitability result that can recommend a smaller
  quantization/model tier, lower context, fewer reply tokens, or no-load
  operation. A model is rejected before runner start when projected active
  pages or commit would cross the hard ceiling or OS reserve.
- Configuration/UI/API support exposing the effective accelerator policy,
  requested versus actual GPU layers, mapped/resident/committed bytes, runner
  and control-plane memory, KV/cache/pool/arena use, pagefile/swap headroom,
  hard-fault rate, unload countdown, and the exact degradation/rejection reason.
- Tests covering configuration precedence, conflicting GPU settings, zero-GPU
  launch arguments, no GPU backend initialization, CPU-only model admission,
  one-slot enforcement, cache/pool trimming, unload/reload, cancellation,
  repeated requests, memory-pressure recovery, and clean shutdown.
- A matched benchmark matrix for at least one compact GGUF across `auto` and
  `cpu_only` on the same host: cold/warm load, TTFT, prompt/generation rate,
  peak resident and commit memory, mapped bytes, page faults, pagefile/swap,
  output quality, cancellation latency, unload release, and idle steady state.

Implementation order inside Phase 30A:

1. Configuration schema, precedence, validation, and fail-closed backend launch.
2. CPU-only memory estimator and pre-load/pre-request admission.
3. One-slot/context/batch/thread/cache defaults and background-work throttling.
4. Deterministic unload, mapping closure, pool/arena/cache release, and pressure
   recovery.
5. Administration visibility and actionable diagnostics.
6. Debug/Release tests and real-model benchmark evidence.

Exit criteria:

- With `acceleratorPolicy=cpu_only`, every validated runner launch uses zero GPU
  layers and CPU KV placement, and instrumentation records no MasterAI-caused
  GPU allocation attempt.
- Peak resident memory and commit remain within the configured CPU-only ceiling
  and OS reserve during cold load, generation, cancellation, unload, repeated
  requests, and simultaneous permitted background activity.
- After idle unload and cache/pool expiry, memory returns to a documented
  bounded steady-state range; no request-sized allocation remains retained
  without an identified cache or pool owner and byte ceiling.
- A model that would cause destructive paging is downgraded or rejected before
  runner start, with a concrete recommended model/context/profile change.
- CPU-only output passes the same correctness/quality checks as the matched
  baseline within the configured tolerance, and shutdown remains reliable
  under High/Critical pressure.

### Phase 31 — Storage tiering, virtual drives, and scratch-volume management

Status: Implemented (2026-08-13; manifest closure 2026-08-13, this pass).
Priority A/B — the `ScratchVolumeManager` and storage-aware placement
recommendations (Priority A) are real, and tier *migration* tooling
(relocating already-placed durable data between tiers after the fact, e.g.
an administrator-initiated "move this model from Tier C to Tier A") is also
real: `migrate_durable_file()` (`src/scratch_storage.cpp`) plus `POST
/api/v1/system/storage/migrate`, verified by a SHA-256 digest match before
the atomic rename and enforcing the same hard RAM-tier prohibition every
other entry point in this file does. **The central migration manifest is
now also real (this pass)**: `DurableFileManifest` records every migration
in a durable, `RecordStore`-journaled log and `resolve_durable_path()`
transparently follows it, wired into `LlamaCppAdapter::build_launch_spec()`
and `RunnerSupervisor::load()`'s pre-touch path so a Model Registry entry's
model file is found at its current location even after being migrated.
Administrator-visible via `GET /api/v1/system/storage/manifest`.

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
  reconstructable temporary artifacts only) — `StorageTier` plus
  `classify_storage_tier()`, which derives the tier from the same measured
  `StorageLatencyProfile` evidence Phase 21 already gathers (device type +
  timed random-read latency), never from assumption.
- A `ScratchVolumeManager` (`src/scratch_storage.cpp`) with per-job directory
  and byte quota (`begin_job()`/`reserve()`), a global byte quota, preferred-
  tier selection (constructor argument, set from a real
  `classify_storage_tier()` measurement of the scratch volume at server
  startup), atomic publication (`publish()`: stage-then-atomic-rename, same
  pattern as `RecordStore::checkpoint()`), shutdown cleanup (destructor calls
  `shutdown_cleanup()`), a crash-recovery journal and orphan cleanup
  (`recover_orphans()`, run once at server startup and re-runnable on demand
  via `POST /api/v1/system/scratch/cleanup`), and a free-space reserve
  (`free_space_available()`, checked before every admission).
- A hard prohibition on placing full GGUF models, durable chats, audit
  records, user databases, resumable downloads, backups, security records,
  or the only copy of an index generation on RAM-backed storage — enforced
  in code (not just documented) via `DurableDataClass` and
  `durable_data_class_allows_ram_tier()`, which `begin_job()`/`publish()`
  both consult and reject (throw) against rather than silently downgrade.
- Separate reporting of physical/available RAM, committed virtual memory,
  commit limit, pagefile/swap usage, hard page-fault rate, and model
  mapped/resident bytes (`MemoryAccountingSnapshot`/
  `probe_memory_accounting()`, `src/platform.cpp`); a model is rejected or
  downgraded when projected active pages exceed safe physical capacity even
  if commit capacity remains
  (`projected_resident_exceeds_safe_physical_capacity()`). Scope note: the
  reported page-fault rate is the total (soft+hard) rate from Windows'
  `GetProcessMemoryInfo` — a true hard-fault-only isolation would need
  PDH/ETW counters this project does not yet depend on; the reported rate is
  used the same directional way a hard-fault-only rate would be.
- Detection (not assumption) of filesystem compression, encryption,
  deduplication, virtual disks, or network redirection under active model/
  index storage, calibrated by measurement
  (`FilesystemIntegrityFlags`/`probe_filesystem_integrity_flags()`,
  `src/platform.cpp`, via `GetVolumeInformationW`/`FSCTL_GET_COMPRESSION`/
  `FILE_ATTRIBUTE_ENCRYPTED`/`GetDriveTypeW`/`IOCTL_STORAGE_QUERY_PROPERTY`
  bus-type/`IO_REPARSE_TAG_DEDUP`). Best-effort per this project's
  Windows-first build scope: `detection_available` is false on non-Windows
  and whenever the underlying query fails, and per-file deduplication
  detection only applies when the probed path is itself a regular file, not
  a directory (no dependency on the admin-only Data Deduplication WMI
  surface a true directory/volume-level check would need).
- `recommend_storage_placement()` combines the tier classification and
  filesystem-integrity evidence above into one placement recommendation
  (`GET /api/v1/system/storage`, administrator-only), so a recommendation is
  always derived from a real measurement of the actual path in question.

Exit criteria:

- Scratch files cannot fill the system drive — enforced structurally by
  `ScratchVolumeManager`'s global quota and free-space reserve, checked
  before every `begin_job()`/`reserve()` admission. Real-hardware validation
  that this holds under sustained load is a separate, later exercise, on the
  same footing as this project's other real-model/real-hardware exit
  criteria (Phases 4-7, 30A) that remain open pending a dedicated hardware
  run.
- RAM-drive use is included in physical-memory accounting —
  `classify_storage_tier()` reports `StorageTier::ram_backed` from the same
  `probe_storage_class()`/`GetDriveTypeW` signal `probe_hardware()` already
  feeds into `HardwareInfo::available_ram_mib`, so a RAM-disk-backed scratch
  root is never invisible to physical-memory accounting.
- Model placement recommendations reflect measured storage, not assumption —
  `recommend_storage_placement()` takes only measured
  `StorageLatencyProfile`/`FilesystemIntegrityFlags` evidence as input and
  performs no hardcoded per-drive-letter or per-vendor assumption.
- Durable data is never silently redirected to ephemeral storage; storage
  migration preserves integrity and atomicity — enforced by
  `durable_data_class_allows_ram_tier()` (rejects, never downgrades),
  `ScratchVolumeManager::publish()`'s stage-then-atomic-rename for the
  scratch-to-durable path, and (2026-08-13, this pass)
  `migrate_durable_file()`'s SHA-256-verified stage-then-atomic-rename for
  the already-durable tier-to-tier path, exposed via `POST
  /api/v1/system/storage/migrate`. **(2026-08-13, this pass)** the same
  call now also records the move in `DurableFileManifest`, so a Model
  Registry entry or other record that referenced the pre-migration path is
  never left silently stale: `resolve_durable_path()` transparently follows
  the manifest to the file's current location.

### Phase 32 — Speculative decoding and draft-model acceleration

Status: **Implemented, evidence-pending (2026-08-13; sampling-gate correction
2026-08-13, this pass)** — see the Phase 32 entry in the status summary
above for the full breakdown. Priority C — the decision logic (compatibility
checks, acceptance-rate tracking, per-request enable/disable) is real and
tested, wired to a real dual-model (draft+target concurrently resident)
launch path (`LlamaCppAdapter::build_launch_spec`'s `--model-draft` flags),
and the sampling-compatibility gate now correctly reflects that llama.cpp's
speculative verification supports this codebase's real (non-greedy) chat
sampling presets, so the feature activates for live chat traffic once an
administrator admits it and records a qualifying measured acceptance rate.
Still explicitly unvalidated on real hardware -- no measured throughput run
has exercised the launch path yet.

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

Status: **Fully implemented (2026-08-13; failover wiring 2026-08-13, this
pass)** — both halves. The local multi-runner orchestration half (earlier
pass), the intranet/mTLS worker protocol half (private PKI/mutual TLS/
model-digest verification), and now automatic remote-worker failover on the
live chat-generation dispatch path are all implemented; see the Phase 33
entry in the status summary above for the full breakdown. Priority C
overall — this phase stayed deliberately deferred behind the Priority A/B
work above until the local-only slice was explicitly authorized and pulled
forward, then the intranet-worker slice was separately authorized and
completed this pass; local single-runner mode remains the default and was
not required to change, and did not change.

Purpose:

- Allow one control plane to use multiple local runner processes, or
  optionally approved intranet worker machines, without mixing security
  authority.

Dependencies:

- Phase 2 authorization model (worker authorization stays project-bound) —
  reused via the same open-unless-restricted `authorized_project_ids`
  pattern already used by `IdeIntegrationService`/`McpIdentity`, not
  reinvented.
- Phase 29 device-aware routing signals (`model_routing.cpp`) — this phase's
  `RunnerSelectionSignals` deliberately mirrors that struct's "declare and
  disclose every signal, even ones a rule does not yet act on" discipline
  for the separate decision of *which runner process*, not *which model
  tier*.
- Phase 4's existing single-runner supervision pattern (`RunnerSupervisor`,
  `src/inference.cpp`) — `LocalRunnerPool` generalizes it to N concurrent
  processes rather than inventing a parallel mechanism.
- Phase 9's existing outbound registry pattern for pinned, verified external
  processes remains the template the *not-yet-implemented* intranet-worker
  half of this phase will follow when that pass happens; nothing in the
  local-only slice needs it, since every runner it manages is a local child
  process this control plane spawns itself.

Deliverables:

- **Done:** local multi-runner configurations (per-GPU runner, CPU+GPU
  split, dedicated embedding/router/benchmark runners) — opt-in via the new
  `localRunnerPool` settings array (`LocalRunnerConfig`, `src/config.cpp`),
  empty by default. Routed by `LocalRunnerPool::select_runner()`
  (`src/runner_pool.cpp`) on resident-model match, runner health/state
  (a queue-depth proxy — see the honest scope note below), capability, and
  priority, with project-bound authorization and capability applied as hard
  filters before scoring. Available-VRAM/RAM and expected-TTFT signals are
  declared on `RunnerSelectionSignals` for disclosure but are not yet live
  scoring inputs in this pass (no live-per-runner VRAM/TTFT probe exists
  yet, distinct from the control-plane-wide Phase 30 hardware probe); a
  per-runner thermal/power signal is likewise declared-but-undriven for the
  same reason this project already documents equivalent gaps elsewhere
  (e.g. Phase 78's cache-hit-rate note) rather than fabricating a value.
- **Deferred (Planned):** optional intranet worker nodes using mutual TLS,
  pinned/approved private PKI, signed worker registration, model-digest
  verification, explicit per-project authorization, encrypted transport,
  request-size limits, cancellation, audit correlation — no shared user
  passwords, no direct unrestricted filesystem access. Not implemented in
  this pass; every runner `LocalRunnerPool` manages is local-only.
- **Done:** compact retrieval-context transfer to runners — the multi-runner
  chat/RAG-generation path reuses the exact same assembled-prompt string
  (`assemble_inference_prompt`/`assemble_chat_prompt`, already a bounded,
  ranked, budgeted context assembly per Phase 16/24) every single-runner
  call already sent; `LocalRunnerPool::generate()` takes that same string,
  never a whole project file, so no separate "compact transfer" mechanism
  needed inventing.
- **Done:** worker identity recorded on the query trace —
  `QueryTrace::runner_id` (`"inference"` for the always-present default
  supervisor, or a `LocalRunnerConfig::id` for a pool runner), set via the
  new `QueryCoordinator::record_runner()` (`src/query.cpp`), following the
  same "independent of the terminal diagnostic" convention as
  `record_classification()`/`record_retrieval()`.
- Administration visibility: `GET /api/v1/runner/pool` (administrator-only,
  same shape as `GET /api/v1/system/storage`) lists every configured
  runner's live state, capabilities, authorized projects, and health.

Exit criteria:

- Runner failure does not crash the control plane; retries occur only when
  semantically safe and never duplicate a persisted response. Enforced by
  `LocalRunnerPool::generate()` wrapping every call in try/catch (a failing
  runner is marked unhealthy and excluded from future routing, never taken
  down with the control plane) and by `retry_is_semantically_safe()`
  (`src/runner_pool.cpp`), which the chat-generation call site consults via
  `RunnerGenerationFailure::any_bytes_emitted` — tracked directly at the
  `on_chunk` callback, not inferred after the fact — before ever retrying
  on the default runner.
- Worker authorization remains project-bound — `LocalRunnerConfig::
  authorized_project_ids` is a hard filter in `select_runner()`, never a
  soft preference, reusing the Phase 2 project-bound pattern.
- Local single-runner mode remains fully functional as the default — an
  empty (default) `localRunnerPool` setting means `runner_pool` is never
  constructed and every request path falls back to the pre-existing
  `inference`/`ensure_model_loaded()` behavior unchanged.

### Phase 34 — Adaptive performance controller

Status: **Implemented at a scoped-down level (2026-08-13)** — see the
Phase 34 entry in the status summary above for the full breakdown. Priority
B; every stability control is real and tested, applied live to the one
genuinely mutable target (`MemoryBudgetManager::set_policy()`) with every
other named knob computed and disclosed as a recommendation rather than
applied.

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

Status: **Implemented at a scoped-down level (2026-08-13; extended
2026-08-13, this pass)** — see the Phase 35 entry in the status summary
above for exactly which named pages this condenses to (one consolidated,
fully real page now covering Overview/Adaptive Controller, Local Runner
Pool, Intranet Worker Pool, Memory, Caches, Storage/migration manifest,
Scheduling, Advanced Optimizations, and Calibration) versus which remain
deferred for lack of a dedicated telemetry route.

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
- An Overview page showing current profile, loaded model, runner state,
  total/available RAM, control-plane/runner/KV/cache memory, mapped model
  bytes, approximate VRAM, queue depth, current query stage, TTFT, prompt
  and generation tokens/second, page-fault warnings, and the primary
  detected bottleneck from the Phase 13 decision-tree diagnostic.
- A Query Traces page exposing every Phase 13 stage timing for a selected
  request plus its terminal conclusion (`Primary bottleneck`,
  `Secondary bottleneck`, `Recommended action`, `Expected trade-off`,
  `Evidence confidence`) and, where run, its direct-runner comparison.
- A Calibration page exposing quick/standard/extended calibration runs (see
  Phase 19), a current-vs-safe-defaults comparison, apply/restore controls,
  and sanitized evidence export.
- A Runner Configuration page (expert-only) listing every backend launch
  setting — GPU layers, CPU threads, batch, micro-batch, context, parallel
  slots, mmap, mlock, KV type/placement, attention/kernel options, model-load
  mode — each with current value, recommended value, supporting evidence,
  memory impact, speed impact, compatibility, and whether a restart is
  required.
- A Model Comparison page benchmarking multiple models on the same host and
  prompt suite (quality, load time, TTFT, prompt/generation throughput, peak
  RAM/VRAM, recommended profile, suitability for the current machine).

Exit criteria:

- Every figure shown is backed by the same instrumentation used for
  automated benchmarking, not a separately maintained display-only value.
- Every destructive or resource-reducing action is authorized, audited, and
  reversible or clearly explained.
- An administrator can select any traced query and receive the same
  stage-attributed "why was this slow" answer defined in Phase 13's exit
  criteria, sourced from the same underlying trace data shown elsewhere in
  this sidebar.

### Phase 36 — Full performance certification and regression gates

Status: Implemented at a scoped-down level (2026-08-17). See the Phase 36
status summary entry above for the full breakdown of what ships
(`PerformanceCertificationRunner`/`PerformanceCertificationStore` in
`src/regression_gate.cpp`, the five regression check groups, threshold-
gated fingerprint-matched build comparison, the `/api/v1/performance/
certification*` routes, and the "Benchmarks & Regression" Performance
page, real queue-wait and storage-bytes-read measurement) versus what
remains an administrator-run, per-real-host exercise (the plan's full
cold/warm-runner/HDD-SATA-SSD-NVMe/GPU-offloaded physical matrix below —
dimensions no single host can manufacture on demand).

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
  disconnect, queue saturation, HDD/SATA SSD/NVMe, CPU-only/GPU-disabled/
  GPU-offloaded, and each resident profile.
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
- Explicit regression test groups, each independently gating a release:
  runner-attribution tests (direct-runner-vs-MasterAI parity, control-plane
  overhead bound, requested-vs-actual GPU settings recorded, CPU-only launches
  proving zero accelerator allocation, cold/warm runs never mixed); low-memory
  tests (rejection before destructive paging, OS
  reserve never consumed by admission, per-slot KV reservation, cache
  trimming never invalidating an active request's buffers, recovery from
  Critical pressure); prompt-cache tests (prefix reuse on exact match,
  refusal on edited history/model change/index-generation change/settings
  change, no cross-chat/user/project reuse, cancelled turns never recorded
  reusable, measured TTFT improvement on accepted repeated-turn cases);
  calibration tests (exact-identity-only profile restore, invalidation on
  backend/model/storage/power-profile change, safe fallback on a corrupt
  profile, hard RAM cap always overriding a recommendation); model-routing
  tests (smallest capable model chosen for simple tasks, safe pins honoured,
  unsafe pins actionably refused, no multiple large resident models under
  Minimal profile).

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
Phase 41 implements Data Labeling (section 14) and Data Preparation
(section 15) at the same scoped-down level — identity, target dataset,
and lifecycle status only, with each task's label mode and each job's
operation stored as free text rather than the full feature lists (label
guidelines, bulk labeling, consensus review, reviewer accuracy metrics,
...; pipeline-step composition, execution, logging, reproducibility)
those sections describe. Phase 42 implements Training Jobs (section 16)
at the same scoped-down level — identity, target project/model/dataset,
training method, and the full eleven-state lifecycle status from section
16, not the compute/hyperparameter/scheduling field list (hardware
allocation, container image, hyperparameters, environment variables,
secrets, checkpoint/logging/notification policy, resource/cost ceilings,
failure-recovery strategy) that a real training executor will attach to
a job once it exists. The dashboard's failed-training-jobs count is now
real, drawn from Training Jobs in the `failed` state. Phase 43 implements
the Evaluation Lab (section 23) at the same scoped-down level — identity,
the model/dataset a run targets, a free-text evaluation category, and a
lifecycle status, not the benchmark-set/human-evaluation/comparison/
numeric-score field list that a real evaluation harness will attach to a
run once it exists. Phase 44 implements Experiment Tracking (section 25)
at the same scoped-down level — identity, the project/model/dataset an
experiment relates to (dataset optional, mirroring Training Jobs' optional
model id), and a lifecycle status, not the source-code/configuration/
container-version, hyperparameter, random-seed, hardware/runtime,
metric, checkpoint/log/artifact, tag, or side-by-side comparison field
list that a real training/evaluation executor will attach to an experiment
once it exists. Phase 45 implements the Fine-Tuning Interface (section 18)
at the same scoped-down level -- identity, the project/model/dataset a job
relates to (project id optional, model id and dataset id both mandatory
since fine-tuning always adapts an existing base model with an existing
dataset), a free-text method (section 18 lists ten presets such as subject
specialisation, code assistant, and safety alignment, not a closed enum),
and the same eleven-state lifecycle status Training Jobs uses, not the
base-model-version/adapter-method/hyperparameter/checkpoint/output-model
field list that a real fine-tuning executor will attach to a job once it
exists. Phase 46 implements the Model Builder Interface (section 9) at
full surface -- identity, the project/base model it relates to (both
optional, since a from-template or from-scratch build has neither a
tracked project nor an existing model to start from), a free-text source
type (section 9 lists eleven starting points such as new model from
template, imported base model, or embedding model, not a closed enum),
its own five-state design-time lifecycle status (draft, configuring,
ready, submitted, archived) rather than the eleven-state job lifecycle
Training Jobs and Fine-Tuning use, since a builder configuration never
queues, runs, or pauses -- plus the complete section 9 build-settings
list (architecture, layer configuration, hidden dimensions, attention
configuration, vocabulary and tokenizer, sequence length, activation
functions, dropout, initialisation strategy, loss function, optimiser,
learning-rate scheduler, batch size, epoch count, gradient accumulation,
gradient clipping, mixed precision, checkpoint frequency, validation
frequency, early stopping, random seed, reproducibility settings, and
distributed-training settings) edited through the basic and advanced
configuration modes section 9 requires. The settings describe the
intended build; the model-construction executor that consumes them is a
separate future phase. Phase 47 implements Prompt and
Instruction Training (section 19) at the same scoped-down level — identity,
the dataset an example targets, a free-text subject classification, and a
five-state reviewer-approval lifecycle status (draft, in_review, approved,
rejected, archived), not the section's full record (system instruction,
user instruction, context, expected response, rejected response, tool
calls, tool results, required output format, difficulty, safety
classification) that only means something once an actual example record
exists to hold it. Phase 48 implements Synthetic Data Generation (section
20) at the same scoped-down level — identity, the dataset a record
targets, a free-text generation technique, and the same five-state
reviewer-approval lifecycle status Phase 47 uses, not the section's full
record (generator model, generator version, prompt, generation settings,
confidence score, original source linkage) that only means something once
a real generation executor exists. Phase 49 implements Embeddings and
Vector Stores (section 21) at the same scoped-down level — identity, a
free-text embedding model name, a free-text distance metric, and a
three-state pending/approved/rejected approval lifecycle, not the
section's full field list (embedding-model version, vector dimensions,
document count, chunk count, storage size, index type, security
classification, access permissions, last rebuild date, associated subject
packages/agents/deployed models) that only means something once a real
document-import/chunking/indexing pipeline exists; unlike Phases 47-48's
target-scoped content records, a vector store is a standalone registered
resource like Dataset Manager's own entries, so it reuses
DatasetApprovalStatus's three-state workflow rather than the five-state
reviewer workflow content records use. Phase 50 implements Retrieval-
Augmented Generation (section 22) at the same scoped-down level — identity,
a free-text search strategy, an optional vector_store_id referencing a
VectorStoreStore entry, and the same three-state pending/approved/rejected
approval lifecycle Phase 49 uses, not the section's full configuration
surface (query preprocessing, query rewriting, hybrid-search weighting,
retrieval count, relevance threshold, metadata filters, reranking model,
context-size limit, citation requirements, response template, fallback
behavior, source-priority rules, restricted documents, cache behavior) or
its retrieval-testing surface that only mean something once a real
retrieval executor exists; like Phase 49's vector store, a RAG
configuration is a standalone registered resource, not a target-scoped
content record. Phase 51 implements the Subject Examination System
(section 24) at the same scoped-down level — identity, a mandatory
subject package reference, a free-text question format, and the
five-state reviewer-approval lifecycle content records use, not the
question-bank/score-suite surface that only means something once a real
examination executor exists. Phase 52 implements Hyperparameter
Optimization (section 26) at the same scoped-down level — identity, a
mandatory training job reference, a free-text search strategy, and the
same eleven-state job lifecycle Training Jobs use, not the
search-space/trial-history surface a real search executor will attach.
Phase 53 implements Model Optimization (section 28) at the same
scoped-down level — identity, a mandatory model reference, a free-text
operation, and the eleven-state job lifecycle, not the quality-loss
comparison a real optimizer executor will attach. Phase 54 implements
Checkpoint Management (section 33) at the same scoped-down level —
identity, a mandatory training job reference, a free-text capture
reason, and a bespoke three-state active/pinned/archived retention
lifecycle (pinned being section 33's "protect" operation), not the
step/epoch/hash/resume record a real training executor will attach.
Phase 55 implements the Deployment Manager (section 34) at the same
scoped-down level — identity, a mandatory model reference, free-text
environment and strategy fields, and the same three-state
pending/approved/rejected approval lifecycle Phases 49-50 use, since
section 34 explicitly names approval as part of the deployment record,
not the runtime/target-node/rollback/health-status record a real
deployment executor will attach. Phase 56 breaks the scoped-down pattern:
it is the module's first real execution layer (src/ml_engine.cpp). Dataset
Manager entries now hold real uploaded CSV content, Training Jobs actually
train tabular models (gradient-descent linear regression and logistic/
softmax classification) with genuine loss curves, held-out metrics, and
measured-loss checkpoints, Evaluation Lab actually scores trained models
(accuracy/precision/recall/F1/confusion matrix or MSE/MAE/R²) and stores
the results, and the Model Registry serves live predictions from persisted
learned weights — see the Phase 56 entry in the phase list above for the
full surface. Phase 57 continues the real-executor pattern with Model
Comparison (section 27): a comparison record names a baseline model, a
candidate model, and a shared benchmark dataset, and running it scores
both trained artifacts against that dataset's real uploaded content and
stores a measured verdict — both metric sets, the primary-metric delta
(macro F1 for classification, MSE for regression), and the winner — see
the Phase 57 entry above for the full surface and its honest boundary.
Phases 58-60 add bounded knowledge-file ingestion, persisted authored
hashing-vector indexes, and approved RAG retrieval/context execution. Phase 61
adds a real process-isolated llama.cpp learned-embedding adapter with durable
model/dimension provenance and changes durable user-memory recall from every
turn to one persisted snapshot per conversation. LLM fine-tuning (Phase 73),
RAG answer generation (Phase 76), and Inference Endpoints (Phase 62/77) are
real executors too, and Phase 82 gives Deployment Manager and Synthetic
Data their own real executors as well, flipping all three of that pass's
named interfaces from `planned` to `available` — see their own phase
entries above; every other executor not named above remains `Planned`: a
metadata record or lifecycle
transition is not execution proof. Checkpoint Management (section 33) was
the last-scoped-down interface never revisited with a real executor until
Phase 79, which now captures genuine mid-training weight snapshots and
supports resuming gradient descent from one, closing that gap. Experiment
Tracking (section 25) got the same treatment in Phase 80: `POST
.../experiments/{id}/run` genuinely trains the experiment's dataset content,
computes real training/validation/evaluation metrics and captures real
checkpoints, and `POST .../experiments/compare` builds a genuine side-by-side
diff of two or more already-run experiments — see the Phase 80 entry above
for the full surface and its honest boundary (no safety-scoring executor
exists yet, reported as such rather than fabricated). Prompt and Instruction
Training (section 19) got the same treatment in Phase 81 — see its own entry
above for what "generate," "test against models," "detect duplicates/
contradictions," and "validate structured output" now genuinely do. Safety
and Governance (section 40) is the last of these stale-roster-tag fixes:
Phase 83 flips it from `planned` to `available`, since its real content-
scanning executors (Phase 74) and real approval-gated deployment/inference
enforcement (Phase 82) already met the bar every other `available` entry
here does.

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

## 27. Release Gates

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

## 28. Decisions Gated Before Their Relevant Phase Can Exit

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

## 29. Non-Goals for Initial Release

- Training foundation models.
- Hosting unrelated generation categories.
- Public internet SaaS operation.
- Multi-node distributed inference.
- Kubernetes deployment.
- Custom cryptographic algorithms.
- An unrestricted shell agent.
- Automatic execution of model-proposed commands without policy and approval.
- Replacing mature optimized inference kernels before profiling proves a need.

## 30. Reference Standards and Upstream Documentation

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

## 31. Final Planning Outcome

The recommended implementation is a native, modular, programming-focused AI host with strict loopback defaults, deliberate intranet enablement, real authentication, process-isolated inference, categorized model storage, resumable verified downloads, hardware-aware recommendations, reproducible benchmarks, a complete development-oriented web interface, and bidirectional MCP support.

The implementation should proceed phase by phase. Security, lifecycle correctness, and observability must be established before advanced agent capabilities or aggressive performance optimization are introduced.

