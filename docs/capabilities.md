# Implemented capability areas

> Part of the [MasterAI README](../README.md). Implementation does not
> automatically mean operational certification — see
> [Project status](project-status.md) for that distinction.

The current source includes native implementations for:

- Schema-versioned configuration with precedence, atomic replacement, reset,
  migration metadata, and objective-hash verification.
- OS-principal authentication, first-administrator setup, persistent roles,
  hashed sessions, scoped tokens, CSRF/origin/host controls, rate limits, and
  hash-chained audit records.
- Categorized model discovery, strict manifest parsing, exact size and SHA-256
  checks, license/provenance preservation, suitability decisions, quarantine,
  and load-time integrity revalidation.
- Process-isolated `llama.cpp` supervision with readiness checks, loopback IPC,
  tokenization, streamed generation, cancellation, unload, logs, and metrics.
- Authenticated native web pages, projects, chats, model selection,
  attachments, prompt context assembly, streaming, and cancellation. Chat
  bubbles show per-message token counts (prompt tokens on the query,
  generated tokens on the response), and reply budgets are fitted to the
  model's context window up front so oversized attachments produce a clear
  error instead of a runner failure or context-shift repetition loop. Replies
  render Markdown links and bare URLs as real clickable anchors. The model
  picker carries a per-model Effort/Thinking settings panel (opens
  automatically on selection, persisted in the browser and restored whenever
  that model is chosen again): for a reasoning-capable architecture (Qwen,
  gpt-oss) the choice is sent to the model as an explicit reasoning
  directive, and for every other architecture it is instead applied as a
  sampling-preset adjustment (temperature/top_p/reply length), so the
  setting is never a silent no-op. System-error notifications are a
  contained, auto-fading floating bubble with a copy button rather than a
  full-width banner. Each
  user also has durable, owner-scoped chat memory: `save to memory: <detail>`
  stores a detail without invoking the model, common self-disclosures (names,
  preferences, contact details, work, and suggestions) are captured by
  deterministic bounded rules, and relevant saved details are read once when
  a conversation starts, then retained as bounded user reference data on that
  chat for later turns and restarts regardless of the selected model. New
  durable details apply automatically to newly created conversations; an
  active conversation retains its own history and startup snapshot. The
  collapsed Memory sidebar makes every capture visible and removable.
- Agentic tool use (Phase 84): a short confirm/implement/proceed/go-ahead/
  make-the-changes/phase/plan/strategy composer message puts a turn into
  auto-drive mode, where the model can call real read/list/search/write/
  delete-file and admin-allow-listed run-command tools — each shown live in
  the transcript as it happens — and keep working the task across as many
  turns as it takes with no further input, until it reports the task
  complete or Escape/Stop is pressed. Every chat has its own Tool execution
  mode, set from the composer's Model settings panel: Auto (safe actions run
  immediately, risky ones ask first — the default), Confirm every action
  (every call, safe or not, pauses on an Approve/Deny card), or Off (no
  tools at all for that chat). Any destructive action (delete, or a command
  matching a fixed destructive-pattern table) always pauses on an explicit
  Approve/Deny card first, regardless of mode — no execution mode can
  weaken that. Administrators manage the run-command allow-list from
  Settings -> Run-command allow-list (or GET/POST /api/v1/chat-tools/
  allowed-commands, plus POST .../update to edit a record in place). The
  page is pre-loaded with a built-in catalog of common Windows, Linux, and
  cross-platform developer commands, each tagged with the OS(es) it applies
  to and enabled by default -- disable anything not warranted for your
  install, or add/edit/revoke your own entries via the icon-only Edit and
  Revoke buttons on each row. The same six tools are also reachable by a
  connected MCP client (`masterai.project.read_file`/`list_directory`/
  `search`/`write_file`/`delete_file`/`run_command`), gated by the
  `projects.read`/`projects.write` token scopes; a call MCP classifies
  high-risk is refused rather than executed unattended (MCP has no
  Approve/Deny UI of its own yet), so completing that specific action still
  means using the web chat — see [docs/PLAN.md](PLAN.md) Phase 84.
- How a tool actually runs — and why the model can't just do it: every one
  of the six tools is ordinary MasterAI server code (`execute_chat_tool()`
  in `src/tool_exec.cpp`), not something the loaded model has any direct
  access to. A local model is just text in, text out — it cannot open a
  file, touch the filesystem, or spawn a process by itself. `read_file`/
  `list_directory`/`search`/`write_file`/`delete_file` all resolve their
  path against the project root first and reject anything absolute or
  outside it, and `run_command` will only launch an executable that is
  already on the admin allow-list, run with no shell (so no argument can be
  interpreted as shell syntax) inside the same sandbox outbound MCP servers
  use — a Windows Job Object or Linux rlimit/`PR_SET_NO_NEW_PRIVS` cap, a
  wall-clock timeout, and a combined stdout+stderr byte cap. Because the
  model can't reach any of that directly, there are exactly three ways a
  tool call actually happens, and every one of them goes through the
  server, never the model in isolation:
  1. **The model requests one (Phase 84).** Mid-reply, the model ends its
     text with one `[[TOOL_CALL]]{"tool":"<name>","arguments":{...}}
     [[/TOOL_CALL]]` block — a plain text convention it was told about in
     its own prompt, nothing more. `send_chat_message()` parses that block
     out of the generated text, classifies the call's risk, and only then
     either runs it for real and feeds the model back the real result on
     the next turn, or — for anything destructive, or any chat set to
     Confirm every action — pauses on an Approve/Deny card until a human
     decides. A model that emits a malformed block, or names a tool that
     isn't one of the six, gets nothing executed at all.
  2. **MasterAI recognizes the request itself and skips the model
     entirely (Phase 98).** A bounded table of plain-English phrasings —
     "list the files in ...", "read the file ...", "search the project
     for ...", "run the command ..." — is checked against every message
     the instant it arrives, before the model is ever asked to generate
     anything. A match executes immediately through the exact same
     `execute_chat_tool()` path and the exact same risk/approval gate as
     path 1 above; the model is only brought in afterward, to comment on
     the real result in its own words. `write_file` and `delete_file` are
     deliberately excluded from this table — a write needs real content
     only the model can produce, and a delete is destructive enough that
     reaching for it should stay a considered model decision.
  3. **MasterAI answers a question about its own tooling without
     executing anything (Phase 99).** A separate, broader phrase table —
     "what tools are you authorised to use", "which commands can you
     run", "what's on your allow-list", and many more (see
     `is_capability_disclosure_query()` in `src/server.cpp`) — is checked
     the same way, immediately and before the model runs. A match answers
     straight from the real, live server configuration (the six real
     tools, plus the actual currently-enabled `run_command` allow-list for
     that project) instead of asking the model to describe its own setup
     from memory, which is exactly the failure this closed: a small local
     model asked directly would otherwise guess at a plausible-looking
     list, or simply truncate mid-answer once it ran out of reply budget.
     Nothing is ever executed for this path — it only ever discloses
     configuration, the same way `save to memory: ...` (above) answers
     without invoking the model.

  In short: the model can only ever *ask*, through one narrow, parsed text
  convention (path 1) or by matching a fixed phrase MasterAI already
  recognizes on its own (paths 2 and 3) — every actual filesystem read,
  filesystem write, or process launch happens in MasterAI's own server
  code, gated by the project-root/allow-list checks above and, for
  anything destructive, a human's explicit approval. See
  [docs/PLAN.md](PLAN.md) Phases 84, 98, and 99 for the full implementation
  detail.
- Resumable, journaled, hash-verified model downloads with quarantine on
  integrity failure.
- Quick, standard, and extended benchmark profiles with compatible comparison
  and recommendation records.
- Inbound MCP over newline-delimited `stdio` and Streamable HTTP using protocol
  revision `2025-11-25`.
- Policy-separated outbound MCP over restricted `stdio` and loopback
  Streamable HTTP, with executable pinning, allow-lists, approval, cancellation,
  timeouts, response bounds, secret references, and audit.
- Native VS Code/Agent-Coder and Visual Studio connection profiles, protected
  IDE tokens, diagnostics, and read-only diff preview contracts.
- Backup, restore, secret/log rotation, recovery, hash-bound upgrade and
  rollback, standalone lifecycle scripts, and hardened systemd operations.
- Query-stage measurement, resource attribution, hardware/storage probes,
  authenticated metrics, and repeatable control-plane performance baselines.
- System-wide memory admission, OS reserve protection, bounded work queues,
  pressure actions, profiles, status, and inference leases.
- A cancellable, checksummed, disk-generation project index foundation with
  recovery, partial publication, unchanged-file elimination, affected-path
  updates, literal and exact-boundary symbol search, typed/coalesced triggers,
  bounded background work, and authenticated status/rebuild/cancel/notify
  routes, plus a native `ProjectWatcher` file-watcher/branch-switch adapter
  (`indexing.watchProjectFiles`, on by default) that drives the same service
  automatically without any external editor or version-control hook.
- Deadline-bound hybrid retrieval (`RetrievalPlanner`/`ContextBudgeter`) that
  chooses the least expensive sufficient index-backed strategy, runs bounded
  parallel strategy steps, fuses results on canonical chunk identity, and
  discloses every included and omitted candidate on the query trace.
- A security-partitioned, byte-bounded cache (`CacheManager`) in front of
  retrieval results, with segmented LRU eviction, versioned keys that make
  file, index, and policy changes an automatic miss, atomic
  checksum-verified disk entries, and authenticated status/trim/clear
  administration.
- Compatible runner prompt-prefix/KV-session reuse (`PromptSessionManager`)
  that resumes a chat's previous llama.cpp KV-cache slot only on an exact
  fingerprint match and a literal byte-prefix-extending prompt, refusing
  reuse on any model switch, reindex, or edited earlier turn.
- Adaptive hardware/model calibration (`CalibrationService`): a real
  cold-load-plus-generation measurement persisted as a host/model/backend/
  build-keyed `TuningProfile`, with automatic invalidation and a safe-default
  fallback on any identity change, plus GPU-layer/mmap/mlock/thread/batch
  launch tuning and CPU%/disk-byte calibration evidence.
- Real GPU memory (VRAM) detection (`probe_hardware`, DXGI-backed on
  Windows) feeding an evidence-based GPU-layer offload decision
  (`select_gpu_layers`): a model whose weights comfortably fit the detected
  VRAM (minus a reserved headroom for the backend's own context/runtime
  overhead) is recommended for full `--n-gpu-layers` offload; otherwise the
  always-safe CPU-only default is kept rather than guessing a partial layer
  count. Applied automatically to every chat auto-load and manual model
  load, not only after an administrator runs an explicit calibration.
- Phase 30A: CPU-only/GPU-disabled low-memory operation. A strict
  `hardware.acceleratorPolicy` config (`auto` / `cpu_only` / `gpu_allowed`,
  default `auto`). Under `cpu_only`, every runner launch is forced to zero
  GPU layers -- `CalibrationService` never recommends GPU offload, a stale
  persisted profile that does is rejected rather than silently zeroed, and
  `LlamaCppAdapter::build_launch_spec()` refuses to launch at all if a
  nonzero GPU-layer request reaches it. The runner process is also launched
  with GPU-visibility environment overrides (`CUDA_VISIBLE_DEVICES=-1` and
  equivalents) so a backend library cannot enumerate or initialize a GPU
  even if it ignores the launch flags. Before that runner process is ever
  started, `admit_runner_weights()` reserves a `MemoryCategory::
  runner_weights` budget lease sized from the model's real weight bytes plus
  compute-buffer/KV heuristics, rejecting an oversized model with a concrete
  reason instead of letting it map/load first and fail later; a released
  lease on every unload path keeps the next model's admission check
  accurate. `MemoryPolicy::maximum_active_inference` (one request under the
  minimal/cpu_only profile) is enforced on every chat request instead of
  being a validated-but-unread field. A background `MemorySweeper` thread
  unloads an idle runner once past its calibrated idle-unload seconds
  (unconditionally under a profile that does not keep a warm idle model by
  default, otherwise once real memory pressure appears) and trims bounded
  caches under sustained pressure. `GET /api/v1/system/resources` reports
  the effective `acceleratorPolicy`; `GET /api/v1/runner/status` adds
  requested-vs-actual GPU layers and an unload countdown; `GET /api/v1/
  system/memory` adds pagefile/swap headroom (`totalVirtualMemoryMiB`/
  `availableVirtualMemoryMiB`, already pagefile-inclusive via Windows
  `GlobalMemoryStatusEx`), process commit bytes, and hard-fault count.
  Reconfiguring the OS pagefile itself is out of scope -- MasterAI reports
  virtual-memory/pagefile state, it never changes it. An administrator-only
  `GET`/`POST /api/v1/admin/config` reads and writes the live `settings.json`
  (surfaced in the web UI's Settings -> System configuration panel), applying
  every field a live request path already re-reads immediately and reporting
  which changed fields need a restart. The matched `auto`-vs-`cpu_only`
  real-model benchmark matrix is recorded against a pinned local GGUF
  (`docs/performance/phase-19-qwen3b-matrix.md`), closing this phase.
- A durable, administrator-controlled Phase 20 registry for optional
  advanced-throughput candidates (continuous batching, speculative decoding,
  NUMA affinity, storage prefetch, multiple warm runners, GPU/CPU KV
  placement). Evidence records are strictly validated and restart-safe;
  recording evidence never enables a feature; admission additionally requires
  a wired implementation, no recorded regression, and a verified fallback;
  every admitted feature remains independently disableable. The safe Phase 19
  profile is always retained. No optional candidate is currently admitted.
- Phase 31: storage tiering and scratch-volume management. Five measured
  storage tiers (fast local NVMe, local SATA SSD, local HDD, removable/
  network, RAM-backed) classified from Phase 21's real device-type/latency
  evidence, never assumed. `ScratchVolumeManager` bounds ephemeral per-job
  scratch storage with per-job and global byte quotas, refuses admission
  once free disk space would fall below a configured reserve, publishes
  finished output atomically (stage-then-rename, so a durable destination is
  never partially observable), cleans up on shutdown, and recovers orphaned
  scratch left by a previous crashed run via a durable journal at the next
  startup (also re-runnable on demand via `POST /api/v1/system/scratch/
  cleanup`). A hard, code-enforced prohibition (`durable_data_class_allows_
  ram_tier()`) refuses -- never silently downgrades -- any attempt to place a
  GGUF model, durable chat, audit/security record, user database, resumable
  download, backup, or sole-copy index generation on RAM-backed storage.
  `GET /api/v1/system/storage` (administrator-only) reports measured
  filesystem-integrity flags (compression/encryption/dedup/virtual-disk/
  network-redirection, best-effort on Windows) and a resulting model-
  placement recommendation, plus separate physical/committed/commit-limit/
  pagefile/page-fault-rate/model-resident memory accounting so none of those
  figures are conflated into one number. `GET /api/v1/system/scratch`
  reports live quota/usage/active-job status. A dedicated multi-tier
  migration workflow (relocating already-placed durable data between tiers)
  is also implemented: `migrate_durable_file()` verifies a SHA-256 digest
  match between source and staged copy before an atomic rename, enforces
  the same RAM-tier prohibition, and is exposed administrator-only via
  `POST /api/v1/system/storage/migrate`. Every migration is recorded in a
  durable, journaled `DurableFileManifest`, and `resolve_durable_path()`
  transparently follows it -- wired into the model-launch path so a Model
  Registry entry's model file is still found after being migrated, and
  visible administrator-only at `GET /api/v1/system/storage/manifest`.
- Phase 33: distributed runners, both halves. Local multi-runner
  orchestration: `LocalRunnerPool` generalizes the existing single-runner
  `RunnerSupervisor` supervision pattern to N concurrent local runner
  processes (per-GPU runner, CPU+GPU split, or dedicated embedding/router/
  benchmark runners), configured via the `localRunnerPool` settings array,
  which stays empty (unchanged single-runner mode) unless an administrator
  opts in. Requests are routed by resident-model match, runner health/
  state, capability, and priority, with Phase 2 project-bound authorization
  enforced as a hard filter; a failing runner is isolated (marked
  unhealthy, excluded from routing) rather than taking down the control
  plane, and a retry is only ever attempted when nothing from the failed
  attempt has already reached the caller or been persisted. The runner
  that actually served each query is recorded on the Phase 13 query trace
  and visible administrator-only at `GET /api/v1/runner/pool`. Intranet
  worker protocol: `IntranetWorkerPool`/`WorkerListener`
  (`src/intranet_worker.cpp`) add mutual-TLS routing to administrator-
  approved remote worker machines, using OpenSSL as this codebase's first
  vendored TLS/crypto dependency (optional at build time via
  `find_package(OpenSSL)`; fails closed at runtime with a clear error when
  unavailable, never falls back to plaintext). A worker's certificate is
  verified against a pinned private CA plus a pinned leaf digest; the
  worker equally verifies this control plane's own client certificate.
  Worker certificates are issued from an in-process private CA
  (`POST /api/v1/system/pki/initialize`, `POST /api/v1/system/pki/workers`)
  and copied to the physical worker machine out of band -- there is no
  self-service worker registration. Each worker's self-reported loaded-
  model digest is verified against this control plane's own model registry
  before that worker is ever selectable (`GET`/`POST /api/v1/worker/pool[/refresh]`).
  `WorkerListener` (`AppConfig::workerMode`, disabled by default) is this
  codebase's one deliberate exception to the administrator HTTP server's
  loopback-only constraint, speaking only the narrow authenticated worker
  protocol, never the administrator surface. Wired into the live
  chat-generation dispatch path: a chat request automatically fails over
  onto a healthy, verified remote worker (before falling back further to
  the default local runner) whenever the local runner it tried fails and
  nothing from that attempt has already reached the caller.
- Phase 34: adaptive performance controller. `AdaptiveController`
  (`src/adaptive_controller.cpp`) extends Phase 19 calibration into a live,
  bounded controller with real minimum-dwell-time, cooldown, bounded-step-
  size, rolling-measurement, confidence-requirement, and safe-rollback
  stability controls, and all eight named modes (Minimal Memory, Balanced,
  Lowest Latency, Maximum Throughput, Battery Saver, Quiet/Thermal
  Conservative, Administrator Custom, Automatic). Every named knob now has a
  real, genuinely-consumed target: inference concurrency, queued-inference
  ceiling, index worker count (also "background-job rate" -- background
  indexing is what that name means here), default context tokens, and idle-
  unload time apply instantly via `MemoryBudgetManager::set_policy()`; the
  cache byte quota applies instantly via `CacheManager::set_policy()`;
  thread count, GPU offload layers, prompt-processing batch size, and NUMA
  local placement are launch-time arguments to a separate llama.cpp process
  with no live-reload primitive, so they apply at the next natural model
  load rather than forcing a disruptive unload/reload of an in-flight
  model. Read queue depth and KV placement beyond the existing KV-precision
  admission remain disclosed-only (no live setter exists for either);
  prefetch distance and warm-up policy are a permanent, documented scope
  limit -- no subsystem exists in this codebase to apply a computed value
  to. The Settings page's Performance section exposes a ceilings editor for
  every `PerformanceCeilings` field alongside the mode selector and the
  applied/recommended report. Administrator-only routes:
  `GET /api/v1/performance/adaptive`, `POST .../mode`, `POST .../ceilings`,
  `POST .../rollback`.
- Phase 35: a "Performance" administration page (`/app/performance`)
  consolidating live visibility into the local runner pool, the intranet
  worker pool, the adaptive controller (mode selection, applied/
  recommended adjustments with reasons and confidence, rollback), memory,
  caches, storage tiers and the tier-migration manifest, the request
  scheduler, advanced optimizations, and calibration profiles, backed
  entirely by the real routes above -- condensed from the plan's full
  named-page enumeration into one working page rather than many
  placeholders. Extended with a "Memory Status" widget on the Model
  Inventory page (`/app/models-inventory`, administrator-only):
  auto-refreshes every 5 seconds (system RAM, system virtual memory, and
  MasterAI's own cache/scratch usage) and offers a "Clean Memory" action
  (`GET`/`POST /api/v1/system/memory/clean`) with selectable real reclaim
  steps -- trim bounded caches, remove orphaned scratch/temp files, release
  MasterAI's own process memory back to the OS, and optionally unload the
  loaded model -- shown as genuine step-by-step progress, not a fabricated
  animation. Refreshes the models table immediately afterward so a
  previously RAM-blocked model shows as ready without a page reload. Also
  offers true system-wide Windows memory options -- trim other
  applications' working sets (size threshold, foreground-protect), flush
  the modified page list, purge the (optionally low-priority-only) standby
  list, empty system/service working sets, and clear the system file
  cache. These require MasterAI itself to be running elevated
  (Administrator); each fails closed and names itself in the result when it
  can't acquire the privilege, rather than silently doing nothing.
- Phase 36: full performance benchmark matrix and regression gate.
  `PerformanceCertificationRunner`/`PerformanceCertificationStore`
  (`src/regression_gate.cpp`) run the existing quality benchmark suite plus
  five real regression check groups against this codebase's own live
  decision logic -- runner attribution, low-memory admission, prompt-cache
  reuse/refusal, calibration identity-exact restore, and smallest-capable-
  tier/Minimal-profile model routing -- plus real per-run metrics (TTFT,
  peak resident memory, generation throughput, a real queue-wait
  measurement from an actual `RequestScheduler` admission, real storage
  bytes read from the process's own disk-read counter, and a CPU sample).
  A run is only ever compared against the previous *accepted* run sharing
  its exact fingerprint (model/backend/hardware/settings/prompt-suite/
  cache-state/profile), so mismatched environments are never presented as
  a direct comparison; administrator-configurable thresholds gate
  acceptance, and a rejected run is still persisted. Administrator/
  developer routes: `GET`/`POST /api/v1/performance/certification`,
  `GET`/`POST /api/v1/performance/certification/thresholds`, rendered on
  the new "Benchmarks & Regression" page (`/app/performance/benchmarks`).
  Real evidence now spans two model/profile combinations on this host
  (`llama32-1b-instruct-q4km` `quick`, 2026-08-18; `granite31-2b-instruct-
  q4km` `standard`, 2026-08-26), each accepted with a clean matching-
  fingerprint comparison -- see
  [docs/validation/phase-36-certification-runbook.md](validation/phase-36-certification-runbook.md).
  What remains outside this control plane's reach: the plan's full
  physical matrix (multiple storage media, multiple machines, GPU-
  offloaded hardware) requires an administrator to actually run this page
  on each real target host -- no software on one machine can manufacture a
  second physical drive or GPU.
- Phase 32: speculative decoding. `check_draft_target_
  compatibility()`, `SpeculativeDecodingStats`, and
  `decide_speculative_decoding_for_request()`
  (`src/speculative_decoding.cpp`) implement exact draft/target
  compatibility checking, rolling acceptance-rate tracking, and the bounded
  per-request enable/disable rule the plan describes, wired to a real
  dual-model (draft+target concurrently resident) launch path
  (`LlamaCppAdapter::build_launch_spec`'s `--model-draft` flags) and gated
  behind the same evidence/admission registry `continuous_batching` uses.
  The sampling-compatibility gate correctly reflects that llama.cpp's
  rejection-sampling verification supports this codebase's real (non-greedy)
  chat sampling presets, so the feature activates for live chat traffic once
  admitted and evidenced. Real hardware evidence is now recorded via the
  `masterai speculative-benchmark` CLI command
  (`docs/performance/phase-32-speculative-matrix.md`): on this low-VRAM
  test host the dual-model path measured 22.5% *slower* than the
  single-model baseline (`qwen25-coder` pair), corroborated 2026-08-26 by a
  second, unrelated pair (`llama32-3b`/`llama32-1b`) measuring 40.5%
  *slower* -- an honest negative result, twice over -- the launch path
  itself works, but this host lacks the compute/VRAM headroom for it to
  pay off.
- A native asynchronous storage and prefetch engine (`IAsyncFileReader`):
  IOCP-backed overlapped reads on Windows and a bounded worker-pool `pread`
  fallback on POSIX, adjacent-request read coalescing, an adaptive
  queue-depth policy keyed to a measured `StorageLatencyProfile`, and
  cancellable requests, wired into model-manifest and index-segment reads
  with automatic fallback to the prior blocking path.
- Tokenization, chat-template, and segmented prompt-fragment caching: a
  content/tokenizer-fingerprinted tokenization cache, compiled
  process-lifetime chat-template execution plans, `PromptSegment`-based
  assembly over shared immutable buffers, a restricted identifier intern
  table, and an extended `PromptSessionManager` reuse decision reporting
  exact reusable-prefix length, divergence offset, invalidation reason, and
  a configurable prefix byte ceiling.
- Low-risk control-plane hot-path reductions: Windows CNG SHA-256/HMAC
  algorithm-provider handles are reused for the process lifetime instead of
  reopened for each authenticated request, frequency-sketch slots use a
  power-of-two mask instead of integer modulo, and both JSON encoders use a
  complete 256-entry escape table plus bulk clean-run copies. These are
  implementation optimizations, not new throughput claims; correctness and
  measured performance gates remain authoritative.
- Staged, classified retrieval fan-out on top of `RetrievalPlanner`:
  filename/path and recent-change strategies, a deterministic request
  classifier, sticky-sufficiency staged execution, an authorization-scoped
  in-flight request join table, and reference-first (`ChunkReference`)
  candidate materialization gated on `ContextBudgeter` admission. Every
  strategy now has a real adapter: heuristic call-graph/type-reference/
  dependency-neighbour scans (C++/Python/JavaScript/TypeScript), a
  sandboxed `git diff` adapter, real cosine-similarity semantic-embedding
  search over the already-live `/v1/embeddings` backend call, conversation
  memory recall, and outbound-MCP resource reads -- the four expensive/
  IO-bound ones are gated both by a config toggle and by whether the
  requesting `RetrievalPlanner` instance was actually constructed with the
  optional dependency each needs, with a runtime "not configured" reason
  disclosed on the query trace when it wasn't.
- Explicit model load-mode/pre-touch selection and a warm-model state
  machine: `ModelLoadMode`/`PreTouchLevel` chosen from measured storage and
  RAM evidence, and a `WarmModelState` machine layered onto the existing
  runner-state tracking via a regression-tested translation table, with
  cancellable background warm-up that yields under memory pressure.
- An immutable shared-buffer and request-scoped memory architecture:
  `SharedBuffer`/`BufferView`/`MappedBufferView`/`ChunkReference`/
  `TokenSpan`/`PromptSegment`, a debug-poison-checked `RequestArena`, and a
  `FixedSizePool<T>` bridged into `MemoryBudgetManager` accounting, plus a
  zero-copy write path for streamed chat tokens.
- A "PageFile" storage setting (`storage.pageFileRoot`, restart required):
  redirects MasterAI's own disk-backed, per-category, key-indexed cache
  (`CacheManager` -- tokenization, prompt/retrieval, model manifests, and
  more) to an administrator-chosen directory instead of the default
  `runtime_root/cache`, so that disk activity can be pointed at a faster
  drive or away from the drive backing the real Windows pagefile, with no
  admin privilege required and no data outside MasterAI's own use. Left
  empty, behavior is unchanged. Configurable in the web UI's Settings ->
  System configuration panel; resolved via `resolve_page_file_root()`.
- A "System Report" (web UI: Report -> System Report, administrator-only,
  `GET /api/v1/system/report`): one consolidated read of hardware (RAM, GPU
  memory, CPU/NUMA), the real Windows pagefile's commit headroom,
  `MemoryBudgetManager` pressure, this process's resident/commit memory, the
  configured PageFile location's drive capacity/free space and MasterAI's
  own used/capacity bytes there, and which optional features (retrieval,
  cache, session reuse, automatic calibration, project-file watching,
  local-password/OS sign-in) are currently enabled.
- A stall watchdog on live generation requests (`inference.stallTimeoutSeconds`,
  default 120s, hot-reloadable): `RunnerSupervisor::generate()`'s wait on the
  runner's `/completion` stream now has a deadline measured from the last
  byte actually received, so a runner that stalls mid-request (most likely
  on a cold model's first prompt) surfaces as a timeout error instead of
  hanging the request indefinitely; a still-streaming generation is never
  cut off since the deadline resets on every byte received.
- Weighted-fair request scheduling and bounded backpressure across eight
  priority classes, deterministic KV-cache reservation and eviction,
  hardware-topology discovery, and evidence-driven model-routing/cascade
  decision logic. Continuous backend batching, live worker affinity, and
  transparent multi-model cascade execution remain gated follow-up work.
- An administrator-only Machine Learning control-plane foundation with a
  dashboard and durable lifecycle records for ML projects, model registry
  entries, datasets, subject packages, labeling and preparation work,
  training and fine-tuning jobs, evaluations, experiments, model-builder
  configurations, instruction and synthetic-data records, vector stores,
  RAG configurations, subject exams, hyperparameter searches,
  model-optimization runs, training checkpoints, and deployments — plus a
  real execution engine (Phases 56-61) that ingests validated CSV dataset
  content, trains tabular models by gradient descent with genuine loss
  curves and held-out metrics, captures measured-loss checkpoints, scores
  trained models with real evaluation metrics, compares two trained models
  on a shared benchmark with a measured winner, serves live predictions
  from persisted weight artifacts, ingests and hashes approved text files,
  persists either authored hashing vectors or validated learned vectors from
  a verified local embedding GGUF, and executes ranked, cited retrieval/
  context assembly. Generative RAG answers (Phase 76), exam administration
  with real LLM-as-judge grading (Phase 51), hyperparameter search execution
  (Phase 52), and deployment promotion (Phase 82) all run for real too --
  see the [Project status](project-status.md) table for each one's own
  honest boundary. Every ML executor not explicitly named across Phases
  56-82 remains `Planned`. (Phase 73, real LLM LoRA fine-tuning, was
  implemented and later removed -- see its row in the status table.)
