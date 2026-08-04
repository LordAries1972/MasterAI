# MasterAI

**A secure, native, local programming AI control plane built from the ground up
in ISO C++17.**

[![Language: C++17](https://img.shields.io/badge/language-ISO%20C%2B%2B17-00599C.svg)](docs/architecture/ADR-0001-cpp17-native-architecture.md)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux-4C8BF5.svg)](docs/architecture/platform-matrix.md)
[![License: MIT](https://img.shields.io/badge/license-MIT-green.svg)](LICENSE)
[![Status: Active development](https://img.shields.io/badge/status-active%20development-orange.svg)](docs/PLAN.md)

MasterAI coordinates local language models, authenticated users, programming
projects, chats, source context, verified downloads, benchmarks, IDE clients,
and Model Context Protocol (MCP) integrations from one security-focused native
service.

The project is designed for developers who want local ownership of models and
project data without making an inference engine, a container platform, or a
third-party application framework the foundation of the system.

> [!IMPORTANT]
> MasterAI is under active development and is **not yet production-ready**.
> Several major capabilities are implemented and test-covered, but real-model,
> external-client, distribution-packaging, and large-project exit checks remain
> outstanding. See [Project status](#project-status) and the authoritative
> [implementation plan](docs/PLAN.md).

## Why MasterAI?

MasterAI is intended to provide a dependable local AI programming environment
with explicit security and resource boundaries:

- **Local ownership** — models, projects, chats, indexes, benchmarks, and
  operational state remain on systems you control.
- **Native C++17 control plane** — no Docker runtime and no inherited web or
  application framework.
- **Programming-first workflows** — chat, code completion, review, debugging,
  documentation, project search, IDE integration, and reproducible model
  comparison.
- **Process-isolated inference** — model runners execute outside the main
  control-plane process, so model memory and runner failures remain isolated.
- **Deny-by-default security** — listeners, routes, files, model sources, MCP
  tools, and administrative operations require explicit authority.
- **Verified model lifecycle** — manifests bind model identity, provenance,
  license, size, digest, backend, and hardware suitability.
- **Bounded resource use** — queues, memory reservations, contexts, background
  work, downloads, and indexes are designed around enforced ceilings.
- **Replaceable adapters** — inference, hardware, storage, embedding, speech,
  secret, and MCP integrations do not dictate the internal architecture.
- **Evidence-driven optimization** — performance work must be measured without
  bypassing correctness, security, cancellation, or auditing.

## Project aims

MasterAI aims to become a native, modular programming AI host that can:

1. Run securely on developer workstations and approved local servers.
2. Discover and validate categorized local programming models.
3. Assess whether a model can run safely on the current hardware.
4. Supervise local inference backends without loading model weights into the
   control process.
5. Provide authenticated browser, API, CLI, IDE, and MCP workflows.
6. Preserve durable projects, chats, attachments, indexes, jobs, and audit
   records with recovery support.
7. Download model artifacts only from approved immutable sources, resume
   interrupted transfers, and verify them before promotion.
8. Compare models with reproducible performance and programming-quality
   evidence.
9. Operate safely on lower-memory systems through deterministic admission,
   pressure handling, and degradation.
10. Add retrieval, caching, prompt reuse, and advanced throughput only after
    their security, memory, and quality boundaries are proven.

## Architecture

MasterAI separates the security-sensitive control plane from replaceable model
runner processes:

```mermaid
flowchart LR
    Clients["Browser / CLI / IDE / MCP clients"]
    Boundary["Authentication, authorization, limits, and audit"]
    Control["MasterAI native C++17 control plane"]
    Data["Record store, projects, chats, indexes, jobs, and logs"]
    Supervisor["Inference supervisor"]
    Runner["Isolated llama.cpp-compatible runner"]
    Models["Verified GGUF models"]
    External["Approved download and outbound MCP endpoints"]

    Clients --> Boundary
    Boundary --> Control
    Control <--> Data
    Control --> Supervisor
    Supervisor <--> Runner
    Runner --> Models
    Control <--> External
```

The control plane owns configuration, authentication, authorization, users,
projects, chats, model metadata, downloads, benchmarks, MCP policy, process
supervision, health, metrics, and graceful lifecycle operations. It does not
map large model weights into its own address space.

Initial inference uses an optional, version-pinned `llama.cpp` server behind a
validated process adapter. `llama.cpp` is not the architectural foundation and
can be replaced without changing MasterAI's external contract.

### Core design principles

- Strict ISO C++17 for application source; optional Assembly only when profiling
  proves a benefit and a C++17 boundary is retained.
- Native Windows and Linux operation without a container runtime.
- Loopback-only network binding by default.
- Authentication and authorization even in local-only mode.
- Canonical, authorized filesystem paths with symlink traversal denied by
  default.
- OS-backed cryptography and secret protection; no custom cryptographic
  primitives.
- Strict configuration schemas, bounded inputs, atomic persistence, and
  recoverable state.
- Process isolation, cancellation, timeouts, backpressure, and graceful
  shutdown.
- Explicit model provenance, licensing, size, and SHA-256 verification.
- Current authorization checks remain authoritative; caches never become an
  access-control source.

Further design detail is available in
[ADR-0001](docs/architecture/ADR-0001-cpp17-native-architecture.md), the
[release 1 baseline](docs/architecture/ADR-0003-release-1-product-baseline.md),
and the [security threat model](docs/security/threat-model.md).

## Implemented capability areas

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
  attachments, prompt context assembly, streaming, and cancellation.
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
  which changed fields need a restart. The one remaining deliverable is the
  matched `auto`-vs-`cpu_only` real-model benchmark matrix, which needs a
  pinned local GGUF and dedicated hardware run.
- A framework-only registry for optional advanced-throughput candidates
  (continuous batching, speculative decoding, NUMA affinity, storage
  prefetch, multiple warm runners, GPU/CPU KV placement), every one
  disabled by default with no code path able to enable it until its own
  evidence is produced.
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
- Staged, classified retrieval fan-out on top of `RetrievalPlanner`:
  filename/path and recent-change strategies, a deterministic request
  classifier, sticky-sufficiency staged execution, an authorization-scoped
  in-flight request join table, and reference-first (`ChunkReference`)
  candidate materialization gated on `ContextBudgeter` admission, with
  not-yet-adapted strategies (semantic embedding, MCP-resource, call-graph,
  and others) declared but disabled and disclosed on the query trace.
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
  model-optimization runs, training checkpoints, and deployments. These are
  real authorized administrative records; they do not yet execute training,
  generate embeddings, populate vector indexes, run an end-to-end RAG
  pipeline, administer exams, run searches or optimizations, capture
  checkpoints, or promote deployments.

Implementation does not automatically mean operational certification. The next
section records the distinction.

## Project status

Status below reflects the evidence recorded in
[docs/PLAN.md](docs/PLAN.md) on **5 August 2026**.

| Phase | Area | Status |
|---:|---|---|
| 0 | Requirements and release decisions | Complete |
| 1 | Native foundation and lifecycle | Complete |
| 2 | Identity and security baseline | Complete |
| 3 | Model registry and hardware assessment | Complete |
| 4 | First isolated inference adapter | Complete; real pinned backend/GGUF path validated |
| 5 | Chat and project web application | Implemented; browser visual and optional voice checks pending |
| 6 | Secure resumable downloads | Implemented; interrupted real HTTPS transfer check pending |
| 7 | Reproducible benchmarking | Complete; same-host real-model comparison validated |
| 8 | Inbound MCP | Implemented; live supported IDE/inspector connection pending |
| 9 | Outbound MCP | Complete |
| 10 | IDE integrations | Native boundary implemented; host packaging and live-IDE checks pending |
| 11 | Operations hardening | Complete |
| 12 | Measured control-plane optimization | Complete for measured native scope |
| 13 | Query measurement and resource baseline | Complete |
| 14 | Bounded-memory foundation | Complete |
| 15 | Incremental disk-backed indexing | Complete; deeper symbol extraction remains a forward enhancement |
| 16 | Deadline-bound hybrid retrieval | Implemented; authored retrieval-quality evaluation set pending |
| 17 | Security-partitioned cache hierarchy | Implemented; representative-query latency benchmark pending |
| 18 | Prompt-prefix and KV/session reuse | Complete; real repeated-turn prefix reuse validated |
| 19 | Hardware/model calibration | Implemented; comparative tuning evidence still pending |
| 20 | Optional advanced throughput | Scaffolding only; every candidate remains disabled and unimplemented |
| 21 | Native asynchronous storage and prefetch engine | Implemented at a scoped-down level; no Linux `io_uring` adapter |
| 22 | Hierarchical content and model-data caching | Implemented at a scoped-down level |
| 23 | Tokenization, template, and prompt-fragment caching | Implemented at a scoped-down level |
| 24 | Advanced retrieval fan-out and adaptive query planning | Implemented at a scoped-down level; some strategies stubbed pending adapters |
| 25 | Continuous inference batching and request scheduling | Scheduling/backpressure implemented at a scoped-down level; backend batching pending |
| 26 | Model loading, mapping, pre-touch, and warm-state management | Implemented at a scoped-down level; two pre-touch levels not yet backend-actionable |
| 27 | KV-cache compression, placement, and lifecycle management | Accounting/placement/lifecycle implemented at a scoped-down level; compression and prefix sharing gated |
| 28 | NUMA, processor-group, and topology-aware execution | Discovery and recommendation implemented at a scoped-down level; live affinity pending evidence |
| 29 | Model tiering, routing, and cascade inference | Decision logic implemented at a scoped-down level; live chat routing/cascade execution pending |
| 30 | Memory deduplication and immutable shared-data architecture | Implemented at a scoped-down level |
| 30A | CPU-only and GPU-disabled low-memory operation | Implemented; matched real-model benchmark matrix pending |
| 31 | Storage tiering, virtual drives, and scratch-volume management | Planned |
| 32 | Speculative decoding and draft-model acceleration | Planned |
| 33 | Distributed local runners and multi-device orchestration | Planned |
| 34 | Adaptive performance controller | Planned |
| 35 | Performance administration interfaces | Planned |
| 36 | Full performance certification and regression gates | Planned |
| 37 | Machine Learning module foundation | Implemented at a scoped-down level |
| 38 | Machine Learning projects | Implemented at a scoped-down level |
| 39 | ML model registry and dataset manager | Implemented at a scoped-down level |
| 40 | Subject Knowledge Manager | Implemented at a scoped-down level |
| 41 | Data labeling and preparation | Implemented at a scoped-down level |
| 42 | Training Jobs | Implemented at a scoped-down level; no training executor |
| 43 | Evaluation Lab | Implemented at a scoped-down level; no scoring harness |
| 44 | Experiment Tracking | Implemented at a scoped-down level |
| 45 | Fine-Tuning Interface | Implemented at a scoped-down level; no fine-tuning executor |
| 46 | Model Builder | Implemented at a scoped-down level; no construction executor |
| 47 | Prompt and Instruction Training | Implemented at a scoped-down metadata level |
| 48 | Synthetic Data Generation | Implemented at a scoped-down metadata level; no generator |
| 49 | Embeddings and Vector Stores | Implemented at a scoped-down registry level; no embedding/index pipeline |
| 50 | Retrieval-Augmented Generation | Implemented at a scoped-down configuration level; no RAG executor |
| 51 | Subject Examination System | Implemented at a scoped-down record level; no exam administration |
| 52 | Hyperparameter Optimization | Implemented at a scoped-down record level; no search executor |
| 53 | Model Optimization | Implemented at a scoped-down record level; no optimizer executor |
| 54 | Checkpoint Management | Implemented at a scoped-down retention-record level; no checkpoint capture |
| 55 | Deployment Manager | Implemented at a scoped-down approval-record level; no deployment executor |

Current validation includes Windows x64 Debug and Release builds and tests under
strict C++17, plus a Linux x86-64 Release build and test run under Ubuntu 26.04
WSL. Release packaging certification on the pinned Ubuntu 24.04 and Debian 13
hosts is still outstanding.

Phase 15 has a bounded native indexing service, affected-path updates,
typed/coalesced trigger admission, disk-generation recovery tests, a native
`ProjectWatcher` file-watcher/branch-switch adapter that calls the indexing
service automatically (no external editor or VCS hook required), a
representative large-project ceiling measurement (`masterai index-probe`),
and a recorded platform-I/O backend decision. Both of its exit criteria now
have current evidence on Windows Debug and Release; deeper language-aware
symbol extraction remains a forward enhancement, not an exit-criterion
blocker.

Phase 16 adds a `RetrievalPlanner` that reads a project's Phase 15 index
directly (exact symbol, exact text, and per-token lexical strategies),
runs bounded parallel strategy steps under a hard request deadline, fuses
and deduplicates results on canonical chunk identity, and discloses exactly
which evidence was used through `/api/v1/queries/{id}`. Phase 17 adds a
byte-bounded, security-partitioned `CacheManager` in front of Phase 16
retrieval results, keyed so that a file change, an index republish, or a
membership/policy change makes a stale entry unreachable automatically,
with authenticated `GET/POST /api/v1/system/cache*` administrative routes.
Both are Windows Debug- and Release-validated; each still has one
evidence-gathering exit criterion outstanding (see the status table above).

Phases 21–30 add a first, real (but deliberately
scoped-down) layer of performance/architecture work on top of Phases 15–19:
an asynchronous storage/prefetch engine, hierarchical and prompt caches,
staged retrieval fan-out, weighted-fair scheduling, model warm-state
management, KV-cache governance, hardware-topology decisions, model-routing
logic, and an immutable shared-buffer/arena architecture with a zero-copy
chat-streaming write path. The mechanisms are real and tested, but each
phase retains the explicit scope limits recorded in
[docs/PLAN.md](docs/PLAN.md), such as Linux `io_uring`, live continuous
backend batching, measured worker affinity, KV compression/prefix sharing,
and transparent live cascade execution. Phase 31 and Phases 32–36 remain
planned.

Phases 37–50 establish the current Machine Learning administration layer.
Administrators can manage durable, permission-gated records through native
web pages and `/api/v1/ml/*` routes, with lifecycle, review, approval,
reload, and deletion behavior covered by native tests. This is deliberately
not represented as an operating training platform: no training or
fine-tuning executor, embedding generator, populated vector index, complete
evaluation harness, or executable RAG pipeline exists yet. For the concepts,
current workflows, exact capability boundary, and a sequential teaching
guide, read [How to Use Machine Learning and Models with MasterAI](docs/HowToUse-MachineLearning.md).

No release may be called production-ready until the functional, security,
migration, recovery, MCP conformance, performance, resource-ceiling, retrieval,
cache-isolation, licensing, provenance, and documentation gates all pass.

## Supported targets and baseline hardware

The release 1 target matrix is:

- Windows 11 x86-64
- Windows Server 2022 x86-64
- Ubuntu Server 24.04 LTS x86-64
- Debian 13 x86-64

Other distributions, ARM64, WSL, and containers are experimental build or
validation environments rather than supported release targets.

Release 1 baseline hardware:

- x86-64 CPU with SSE4.2 and at least four logical processors
- 16 GiB physical RAM, retaining at least 2 GiB for the operating system
- 20 GiB free storage plus model and backup requirements
- Optional GPU; CPU inference remains the compatibility baseline
- For GPU acceleration: a supported CUDA, Vulkan, or HIP backend, a compatible
  driver, and at least 8 GiB dedicated VRAM. Dedicated VRAM size is
  auto-detected on Windows (DXGI); the largest real (non-software) adapter
  found decides whether a given model's weights are recommended for full
  GPU offload.

Individual model manifests may require more capable hardware. MasterAI reports
`Unsupported`, `Memory Risk`, `Slow`, `Usable`, or `Recommended` and blocks
unsafe loads according to policy.

## Building from source

### Prerequisites

Common requirements:

- Git
- CMake
- A C++17 compiler
- Sufficient disk space for generated builds and any local models

Windows development currently uses:

- Visual Studio 2022 with the MSVC x64 C++ toolchain
- Ninja, as supplied by the configured Visual Studio installation
- PowerShell 7 or Windows PowerShell

Linux development uses:

- GCC or Clang with C++17 support
- CMake
- A supported build tool generated by CMake
- POSIX `sh`

The project owns its CMake entry point under `scripts/CMakeLists.txt`.

### Windows x64

Run from the repository root:

```powershell
.\scripts\build.ps1 -Platform Windows-x64 -BuildType Release
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Release
```

For a development build:

```powershell
.\scripts\build.ps1 -Platform Windows-x64 -BuildType Debug
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Debug
```

The build helper searches supported Visual Studio installation roots and
currently expects the Visual Studio-integrated Ninja path configured in
`scripts/build.ps1`. See the
[Visual Studio guidance](docs/ide/visual-studio.md) if local tool discovery
needs adjustment.

### Linux x86-64

```sh
sh ./scripts/build.sh Release Linux-x86_64
sh ./scripts/test.sh Release Linux-x86_64
```

`Linux-arm64` is accepted by the build helper as an experimental validation
target, not a release 1 supported platform.

### Cleaning generated builds

Preview before deleting:

```powershell
.\scripts\clean.ps1 -WhatIf
```

Clean on Windows:

```powershell
.\scripts\clean.ps1
```

Clean on Linux:

```sh
sh ./scripts/clean.sh --dry-run
sh ./scripts/clean.sh
```

The clean helpers remove only the repository's generated `build/` tree. They do
not remove source, configuration, runtime data, or model artifacts.

## First run

This walks through everything needed between "I just built MasterAI" and "I
sent a chat message and got a reply." Skipping a step is the most common
cause of the two errors new installs hit first:
`inference_backend_not_configured` (no runner executable configured) and
`model_not_ready` (no model has passed verification yet). Do the steps in
order.

### 1. Build the binary

See [Building from source](#building-from-source) above if you have not
already. The rest of this section assumes
`.\build\Windows-x64\Release\masterai.exe` (or the Linux equivalent) exists.

### 2. Get an inference backend and a model onto the machine

Chat needs two things this repository does not provide: a `llama.cpp`
server executable and at least one GGUF model with a valid manifest.

- Build or download a version-pinned `llama.cpp` server executable
  (`llama-server` / `llama-server.exe`) and note its full path. MasterAI
  supervises it as a separate process; it does not vendor or build it. A
  convenient (not required) place to keep it is `tools/llama.cpp/` at the
  repository root — already covered by `.gitignore`'s `*.exe`/`*.dll`
  patterns, so it never gets committed.
- Place a GGUF model file under `models/<category>/<model-id>/` (see
  [Models](#models) below for the category list) with a
  `manifest.json` next to it that validates against
  [models/manifest.schema.json](models/manifest.schema.json). The manifest
  must declare the model's exact size, SHA-256 digest, license, and
  approved source (Hugging Face, GitHub releases, or ModelScope).
- Optional: an approved `curl` executable, only if you want MasterAI to
  manage model downloads itself (`masterai download-model`) instead of
  placing files manually.

### 3. Run the configuration wizard

```powershell
.\scripts\configure.ps1 -BuildType Release
```

```sh
sh ./scripts/configure.sh ./config/settings.json Release
```

The wizard writes `config/settings.json` and asks for, in order: the
loopback port, the runtime data directory, the model directory, the
**approved `llama.cpp` server executable path from step 2** (leaving this
blank disables inference and every chat request will fail with
`inference_backend_not_configured`), an approved `curl` executable (blank
disables downloads), and whether to allow locally stored password accounts
and/or OS-integrated sign-in. Re-running the wizard against an existing
file offers **U**pdate, **R**eset, or **C**ancel.

### 4. Verify the model

MasterAI never hashes multi-gigabyte model files on a page load or chat
request — it only trusts a persisted verification cache. Build that cache
(or refresh it any time you add, replace, or remove a file under
`models-root`) before a model can reach `Ready` state:

```powershell
.\build\Windows-x64\Release\masterai.exe verify-models .\models
# or: .\scripts\rehash.ps1 -BuildType Release
```

```sh
./build/Linux-x86_64/Release/masterai verify-models ./models
```

Large model files print `NN%` progress lines while they hash (every 10%, for
files 256 MB or larger) instead of leaving the command silent until the whole
file finishes — helpful when verifying several multi-gigabyte GGUF files back
to back.

If this step is skipped, chat creation fails with `model_not_ready` even
though the model file is present.

### 5. Start the service

```powershell
.\scripts\start.ps1 -BuildType Release -Foreground
```

```sh
sh ./scripts/start.sh ./config/settings.json Release --foreground
```

To run in the background instead, drop `-Foreground` / `--foreground`:

```powershell
.\scripts\start.ps1 -BuildType Release
```

```sh
sh ./scripts/start.sh ./config/settings.json Release
```

`start` runs the configuration wizard automatically (interactively only) if
`config/settings.json` does not exist yet, so steps 3 and 5 can be combined
on a first run if you prefer.

On startup, before any administrator account exists, MasterAI logs a
one-time setup token at warning level, e.g.:

```text
One-time first-administrator token: 3f9c1a...
```

Keep this token; you need it in the next step and it is not shown again
(it is not persisted anywhere the process can hand back out).

### 6. Create the first administrator and sign in

Open the default local endpoint in a browser:

```text
http://127.0.0.1:7070
```

The page detects that no administrator exists yet and shows a setup form
instead of a login form. Paste the setup token from step 5, choose a
username and password (or complete OS-identity setup if
`allow_os_identity_accounts` was enabled), and submit. You are then signed
in as the first administrator.

### 7. Create a project (optional) and chat

From `/app/projects`, register a project pointing at a local source
directory if you want project-aware context; this is optional — chat also
works with no project attached. From `/app/chat`, pick the verified model
from step 4 and send a message.

### Health checks

```text
GET /health/live
GET /health/ready
```

`live` indicates that the process is running. `ready` stays unavailable
until the first administrator exists and the configured sign-in path
(local password and/or OS identity) is usable — it does not mean a model
is loaded or verified.

### Stopping and diagnostics

Stop a background service:

```powershell
.\scripts\stop.ps1 -BuildType Release
```

```sh
sh ./scripts/stop.sh ./config/settings.json Release
```

Run native security and hardware diagnostics:

```powershell
.\scripts\diagnose.ps1 -BuildType Release
```

```sh
sh ./scripts/diagnose.sh ./config/settings.json Release
```

## Configuration

For a complete, field-by-field explanation of every `config/settings.json`
entry — what it does, its default, and its enforced policy ceiling — see
[docs/architecture/configuration.md](docs/architecture/configuration.md).
The summary below covers precedence and environment overrides only.

Configuration precedence is:

1. Compiled safe defaults
2. Persisted settings
3. Approved environment variables
4. Command-line overrides

Supported environment overrides currently include:

```text
MASTERAI_HOST
MASTERAI_PORT
MASTERAI_ALLOW_INTRANET
MASTERAI_RUNTIME_ROOT
MASTERAI_MODELS_ROOT
MASTERAI_LLAMA_SERVER
MASTERAI_CURL
```

Unknown fields are rejected unless they belong to an explicitly approved
extension namespace. Changes are written through temporary files and atomic
replacement.

The default listener is loopback-only. Release 1 intranet access uses an
administrator-managed same-host reverse proxy with trusted TLS, while MasterAI
continues to enforce authentication, host/origin policy, CSRF controls, and
request limits. Direct plaintext non-loopback startup is prohibited.

## Models

Models are organized by programming purpose:

```text
models/
├── general-programming/
├── code-completion/
├── code-review/
├── debugging/
├── documentation/
└── embeddings-code-search/
```

Each installed model uses:

```text
models/<category>/<model-id>/manifest.json
```

The manifest must identify the model, category, format, backend, immutable
source revision, HTTPS source, exact file size, SHA-256 digest, license,
administrator acceptance, hardware estimates, and backend requirements.
Validation follows [models/manifest.schema.json](models/manifest.schema.json)
and the [model taxonomy](docs/architecture/model-taxonomy.md).

Model files are intentionally excluded from Git by `.gitignore`; only the
repository-owned directory markers and manifest schema are tracked.

Scan the model tree:

```powershell
.\build\Windows-x64\Release\masterai.exe scan-models .\models
```

```sh
./build/Linux-x86_64/Release/masterai scan-models ./models
```

A discovered file is not automatically trusted. `scan()` — used by every
inventory, chat, and download request — only ever reads a persisted
verification cache (`models_root/.verified-cache.json`); it never hashes
files itself, so a page load never blocks on multi-gigabyte SHA-256 work. Run
`verify-models` after adding, replacing, or removing files under
`models-root` to hash every discovered model against its manifest and refresh
that cache:

```powershell
.\build\Windows-x64\Release\masterai.exe verify-models .\models
# or: .\scripts\rehash.ps1 -BuildType Release
```

```sh
./build/Linux-x86_64/Release/masterai verify-models ./models
```

Verification covers size, digest, provenance, license state, path
containment, backend compatibility, and hardware suitability before
promotion to `Ready`; load time rechecks integrity again regardless of the
cache.

The current release baseline supports a separately supervised
`llama.cpp`-compatible GGUF backend. The pinned backend and a verified
programming GGUF have completed the Phase 4 end-to-end inference check;
remaining browser, download, MCP, IDE, packaging, and optional voice checks
are listed in [Project status](#project-status).

## Web application and API

The native service provides browser workflows for:

- First-administrator setup and login
- Project and auto-titled chat management, with each workspace section
  (`/app/chat`, `/app/projects`, `/app/models/inventory`,
  `/app/models/download`, `/app/models/benchmarks`, `/app/admin/create`,
  `/app/admin/users`) served at its own URL and gated by role
- Ready-model selection and loading, with best-effort background model
  pre-warming triggered the moment a chat is opened, created, or its model
  is switched — instead of only starting the cold load once the first
  message is sent — so the runner has a head start on the wait
- Incrementally streamed responses and cancellation
- Bounded UTF-8 source/text attachments
- Model inventory and suitability, including a cached verification state so
  page loads never block on hashing model files
- Download jobs and progress, with pause, resume, cancel, and remove controls
- Benchmarks and recommendations
- Resource, memory, request, and indexing status
- Administrator-only Machine Learning dashboard and lifecycle-management pages
- Administrative settings and operations

HTTP APIs are versioned under `/api/v1/`. Implemented capability areas include
authentication, users, projects, chats, models, downloads (including
`.../model-downloads/{id}/pause`, `.../cancel`, and `.../remove`),
benchmarks, resources, memory, request metrics, project indexes, MCP
integrations, IDE connections, and administrator-only Machine Learning
records under `/api/v1/ml/*`.

Inputs are bounded and strictly parsed. Administrative mutations require the
applicable identity, role, scope, project binding, host/origin checks, CSRF
protection, and audit record.

## Machine Learning

MasterAI now provides a native, administrator-only Machine Learning
administration area at `/app/ml`. Its current pages manage scoped lifecycle
records for projects, registered models, datasets, subject knowledge,
labeling, data preparation, training and fine-tuning jobs, evaluation,
experiments, model building, instruction examples, synthetic data, vector
stores, and RAG configurations.

The present Phase 50 boundary is a governed control plane, not an end-to-end
training engine. Creating or advancing a job records administrative intent
and state; it does not run a training framework. Likewise, registering a
vector store or RAG configuration does not generate embeddings, populate an
index, execute retrieval, or prove grounded output. Approved externally
trained models must still be deliberately packaged as verified GGUF artifacts
in the inference model tree before MasterAI can serve them.

For a classroom-style explanation of machine learning, model preparation,
the current interfaces, exact limitations, existing-model setup, and the
sequential workflow for teaching a specialized model, read
[How to Use Machine Learning and Models with MasterAI](docs/HowToUse-MachineLearning.md).

## MCP and IDE integration

MasterAI pins MCP protocol revision `2025-11-25`.

Inbound MCP supports:

- Newline-delimited UTF-8 JSON-RPC over `stdio`
- Streamable HTTP at `/mcp`
- Authenticated, project-bound tools and resources
- Scope checks, cancellation, bounded reads, and native conformance tests

Outbound MCP uses a separate registry and authority boundary. It supports
restricted native `stdio` processes and loopback Streamable HTTP clients with
pinned executable digests, tool allow-lists, project/scope policy, per-call
approval, timeouts, cancellation, response limits, protected secret
references, and hash-chained audit.

Legacy MCP HTTP+SSE is disabled.

Generate protected IDE material and connection profiles with:

```text
masterai ide-token-store <settings-file> <vscode|visual-studio>
masterai ide-profile <settings-file> <vscode|visual-studio>
masterai mcp-stdio <settings-file> <vscode|visual-studio>
```

Tokens are retrieved from the native OS secret provider. They must not be
placed in command arguments, environment variables, source control, or IDE
workspace files.

See:

- [VS Code and Agent-Coder integration](docs/ide/vscode-agent-coder.md)
- [Visual Studio integration](docs/ide/visual-studio.md)
- [IDE integration contract](docs/ide/integration-contract.md)

The native contracts are implemented. JavaScript/TypeScript VS Code packaging
and managed Visual Studio VSIX glue are outside the project's current
C++17/Assembly language authorization, and live-host exit validation remains
pending.

## Command-line interface

The native executable currently exposes:

```text
masterai configure [settings-file]
masterai serve [settings-file]
masterai probe [storage-root]
masterai scan-models [models-root]
masterai verify-models [models-root]
masterai download-model <settings> <url> <revision> <sha256> <category> <model-id> <filename> <display-name> <architecture> <quantization> <license-spdx-id> <size-bytes> <minimum-ram-mib> <recommended-ram-mib> --accept-license
masterai benchmark-model <settings> <model-id> <quick|standard|extended>
masterai mcp-stdio [settings-file] [vscode|visual-studio]
masterai ide-token-store <settings-file> <vscode|visual-studio>
masterai ide-profile <settings-file> <vscode|visual-studio>
masterai backup <settings-file>
masterai restore-backup <backup> <settings-destination> <runtime-destination>
masterai rotate-secret <settings> <secret-alias>
masterai rotate-logs <settings> <maximum-bytes> <retained-files>
masterai recover <settings>
masterai runtime-root <settings>
masterai models-root <settings>
masterai upgrade <settings> <active> <candidate> <rollback-root>
masterai rollback <settings> <active> <receipt>
masterai performance-probe [iterations]
masterai index-probe <project-root> [index-root]
masterai calibrate <settings> <model-id> <auto|minimal|balanced|performance>
masterai security-status [runtime-root]
```

Download sources require approved immutable HTTPS URLs, explicit license
acceptance, an immutable revision, exact size policy, and SHA-256 verification.
Failed integrity checks are quarantined rather than promoted.

`runtime-root <settings>` and `models-root <settings>` print the workspace's
resolved, absolute `runtimeRoot`/`modelsRoot` directories. A relative path in
`settings.json` resolves against **the directory holding that settings file**,
not the process's current working directory or the project root — every
lifecycle script (`start.ps1`, `stop.ps1`, `diagnose.ps1`, `rehash.ps1`) asks
the binary for these instead of re-deriving the path itself, so they always
agree with what the running server actually uses.

## Scripts reference

Every `scripts/*.ps1` (Windows) has a matching `scripts/*.sh` (Linux) with the
same behavior and argument order unless noted. Run them from the repository
root. `[settings-file]` always defaults to `config/settings.json` when
omitted; `[BuildType]` always defaults to `Release` except where noted.

| Script | Arguments | Purpose |
|---|---|---|
| `build.ps1` / `build.sh` | `-BuildType <Debug\|Release>` `-Platform <Windows-x64>` `-VerifyModels` `-ModelsRoot <path>` &nbsp;/&nbsp; `<BuildType> <Platform>` | Configures CMake (Ninja) and builds `masterai`, `masterai_core`, and the test binaries. Default `BuildType` is **Debug**. `-VerifyModels`/`-Platform Linux-x86_64\|Linux-arm64` runs `verify-models` after a successful build. |
| `configure.ps1` / `configure.sh` | `[settings-file] [BuildType]` (`.ps1` takes `-Settings`/`-BuildType` named params) | Runs the interactive first-run/update/reset configuration wizard (`masterai configure`) and writes validated settings. Requires the binary from `build` to already exist. |
| `start.ps1` / `start.sh` | `[settings-file] [BuildType] [-Foreground\|--foreground]` | Starts `masterai serve`. Runs `configure` automatically first if settings are missing (interactive terminals only). Without `-Foreground`/`--foreground`, launches detached and records the PID under `<runtime-root>/run/masterai.pid`. |
| `stop.ps1` / `stop.sh` | `[settings-file] [BuildType]` | Requests graceful shutdown of the PID recorded by `start` and waits for exit. |
| `test.ps1` / `test.sh` | `-BuildType <Debug\|Release> -Platform <Windows-x64>` &nbsp;/&nbsp; `<BuildType> <Platform>` | Runs `ctest` (the full native suite, `masterai_core_tests`) against an already-built tree. Default `BuildType` is **Debug**. |
| `diagnose.ps1` / `diagnose.sh` | `[settings-file] [BuildType]` | Runs `masterai security-status` against the settings' configured runtime root. |
| `rehash.ps1` (Windows only; Linux: run `masterai verify-models` directly) | `-Settings <path> -BuildType <Debug\|Release> -ModelsRoot <path>` | Hashes every file under `models-root` against its manifest and refreshes the verification cache (`models_root/.verified-cache.json`). Run after adding, replacing, or removing model files. When `-ModelsRoot` is omitted it resolves `workspace.modelsRoot` via `masterai models-root <settings>` (see above) rather than re-implementing the path resolution in PowerShell, so it always finds the same directory the server uses. Prints live `NN%` progress lines while hashing any file 256 MB or larger, instead of going silent until the whole model finishes. |
| `clean.ps1` / `clean.sh` | `-WhatIf` &nbsp;/&nbsp; `--dry-run` | Deletes only the generated `build/` tree. Source, configuration, runtime data, and models are untouched. |
| `verify-objectives.ps1` / `verify-objectives.sh` | none | Fails if `docs/objectives.md` has changed without a corresponding reassessed and re-cached hash in `docs/objectives.sha256`. |
| `install-systemd.sh` (Linux only) | `<binary> <settings> <runtime-root> <models-root> <service-user>` | Installs and enables a hardened `masterai.service` systemd unit from fixed absolute paths. Does not build, download, create users, or modify settings. |
| `uninstall-systemd.sh` (Linux only, run as root) | none | Disables and removes the installed systemd unit. Runtime data, settings, models, backups, and credentials are preserved. |

## Persistence, operations, and recovery

MasterAI uses an internally authored bounded record store with explicit schema
versions, checksummed journals, atomic replacement, recovery scanning,
checkpointing, and replaceable repository interfaces.

Runtime state is kept separate from source and model files and can include:

```text
runtime/
├── projects/
├── attachments/
├── indexes/
├── cache/
├── downloads/
├── benchmarks/
├── logs/
├── run/
└── backups/
```

Implemented operational controls include:

- Offline backup and clean-destination restore
- Manifest and integrity validation
- OS-protected secret rotation
- Bounded log rotation
- Crash-residue recovery
- Hash-bound executable upgrade and rollback
- Graceful stop requests and bounded drain
- Windows lifecycle helpers
- Hardened systemd install/start/stop/uninstall helpers

See the [operations documentation](docs/operations/) for current procedures and
validation boundaries.

## Security model

MasterAI treats browser input, source trees, attachments, model manifests,
model weights, downloads, inference responses, and MCP data as untrusted.

Key controls include:

- Locally stored, MasterAI-hashed password accounts as the default sign-in
  path (`allow_local_password_accounts`, on by default), so setup and login
  do not depend on an external identity provider being present.
- Optional native OS identity verification (`LogonUserW` on Windows and PAM
  on Linux) as an explicit opt-in (`allow_os_identity_accounts`, off by
  default) for operators who want accounts mapped to their OS/Windows
  identity instead of, or alongside, locally stored passwords.
- No storage, hashing, reversible encryption, logging, or forwarding of OS
  passwords
- Immediate erasure of transient credential buffers
- Random opaque sessions stored only as hashes
- Scoped, revocable, project-bound service and IDE tokens
- Windows DPAPI and approved Linux secret-provider boundaries
- Loopback-only native listener and explicit reverse-proxy trust
- Host, origin, CSRF, rate, body-size, timeout, and concurrency enforcement
- Canonical path containment and symlink traversal rejection
- Structured process arguments without shell interpolation
- Model and executable digest pinning
- Append-oriented, sanitized, hash-chained audit records
- Bounded queues, output, attachments, downloads, and MCP responses
- Deny-by-default inbound and outbound MCP policy

MasterAI never logs passwords, raw session tokens, API keys, or private keys.
Security is not disabled merely because the service listens on localhost.

For detailed assumptions and threats, read
[docs/security/threat-model.md](docs/security/threat-model.md).

## Performance and bounded-memory direction

MasterAI measures before optimizing. The system distinguishes time to first
token, prompt throughput, generation throughput, retrieval quality and latency,
peak memory, and output correctness.

The completed bounded-memory foundation provides:

- One system-wide memory admission authority
- A mandatory operating-system safety reserve
- Hard limits for request and background-work categories
- Minimal, balanced, and performance profiles
- Deterministic pressure handling
- Bounded prioritized work
- Inference admission and actionable rejection
- Resource and request telemetry

Deadline-bound hybrid retrieval, a security-partitioned cache hierarchy,
compatible prompt/KV reuse, and host-specific calibration are now
implemented on top of that foundation (Phases 16–19 in the status table
above). Phase 18's real-model repeated-turn reuse check is complete; Phases
16, 17, and 19 retain the evaluation or comparative evidence listed in the
status table. Optional advanced-throughput features (Phase 20) exist only as
a disabled-by-default registry scaffold — no candidate's optimization logic
is implemented.

A further, scoped-down layer (Phases 21–30) adds asynchronous storage and
prefetch, hierarchical content caching, tokenization/prompt-fragment caching,
staged retrieval fan-out, weighted-fair scheduling and backpressure, explicit
model load/warm-state management, bounded KV-cache lifecycle, topology-aware
placement decisions, model-tier routing decisions, and an immutable shared-
buffer/arena memory model with zero-copy chat streaming. These are real,
test-covered mechanisms, but the plan records which are wired into live paths
and which remain decision or policy layers awaiting backend integration and
measurement. None may bypass authorization, integrity, auditing,
cancellation, quality checks, or memory ceilings.

How a chat request moves through the scoped-down performance layer:

```mermaid
flowchart TD
    Req["Chat / retrieval request"]
    P21["Phase 21 — async storage engine\n(IOCP / bounded pread, coalesced, cancellable reads)"]
    P15_16["Phase 15/16 — disk-backed index + RetrievalPlanner"]
    P24["Phase 24 — staged retrieval fan-out\n(classifier, sticky sufficiency, in-flight join)"]
    P30_ref["Phase 30 — ChunkReference\n(reference-first candidates)"]
    Budget["ContextBudgeter admission"]
    P23["Phase 23 — tokenization + PromptSegment\nassembly cache"]
    P26["Phase 26 — warm-model state machine\n(load mode, pre-touch, warm-up)"]
    Runner["Isolated llama.cpp-compatible runner"]
    P30_stream["Phase 30 — BufferView zero-copy\nstreaming write"]
    Client["Client (browser / IDE / MCP)"]

    Req --> P21 --> P15_16 --> P24 --> P30_ref --> Budget --> P23
    P23 --> P26 --> Runner --> P30_stream --> Client
```

Each labeled stage is "Implemented at a scoped-down level" per the status
table above; the diagram shows where the mechanism sits in the request
path, not a claim that every listed optimization is fully realized end to
end. Scheduling, KV governance, topology, and model routing are supporting
decision layers and are intentionally omitted from this simplified data-flow
view. See [docs/PLAN.md](docs/PLAN.md) for the itemized scope of each.

## Repository layout

```text
MasterAI/
├── docs/       Architecture, security, operations, APIs, objectives, and plan
├── models/     Categorized local model layout and manifest schema
├── rules/      Durable project implementation rules
├── scripts/    CMake, build, test, lifecycle, clean, and service automation
├── src/        Native ISO C++17 production source
├── test/       Native test cases and isolated fixtures
├── .gitignore  Generated binary, build, model, archive, and editor exclusions
├── LICENSE     MIT license
└── README.md   Current GitHub project overview
```

Generated `build/` and runtime state are not source. Large model payloads and
compiled binaries must not be committed.

## Testing and validation

Run the native correctness suite after building:

```powershell
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Debug
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Release
```

```sh
sh ./scripts/test.sh Release Linux-x86_64
```

The test suite covers configuration, persistence and recovery, identity,
sessions and tokens, secrets, path security, model integrity and suitability,
runner isolation, projects/chats/attachments, downloads, benchmarks, MCP,
operations, query metrics, bounded memory, indexing, retrieval, caching,
scheduling, topology and routing decisions, and Machine Learning record,
permission, lifecycle, reload, and removal behavior.

Fixture and implementation tests do not replace external exit checks. Claims of
phase completion are governed by the evidence and exit criteria in
[docs/PLAN.md](docs/PLAN.md).

Verify that project objectives have not changed without reassessment:

```powershell
.\scripts\verify-objectives.ps1
```

```sh
sh ./scripts/verify-objectives.sh
```

## Roadmap

Near-term work is:

1. Author the Phase 16 retrieval-quality evaluation set demonstrating
   improvement over full-text-only retrieval.
2. Author the Phase 17 representative-query latency benchmark comparing
   cached against uncached retrieval preparation time.
3. Complete the Phase 19 comparative calibration evidence and the Phase 30A
   matched `auto`-versus-`cpu_only` real-model benchmark matrix; add GPU
   utilization/thermal-trend probing only if an approved vendor SDK is adopted.
4. Evaluate Phase 20 throughput options independently, each with its own
   baseline and evidence, and only after all prerequisite gates pass — the
   registry scaffold does not pre-approve any candidate.
5. Longer term: evaluate deeper language-aware symbol extraction for
   Phase 15.
6. Close the documented scope trims across Phases 21–30, including Linux
   `io_uring`, unadapted retrieval strategies, live backend batching,
   backend-actionable pre-touch, validated KV compression/prefix sharing,
   measured worker affinity, live cascade routing, and broader zero-copy use.
7. Implement the still-planned performance Phases 31–36 in prerequisite
   order, beginning with storage tiering and scratch-volume management.
8. Extend the Machine Learning foundation beyond Phase 50 metadata into
   evidence-producing ingestion, execution, evaluation, embedding/vector,
   and RAG workflows without allowing lifecycle state to substitute for
   proof that work ran successfully.

Outstanding operational certification also includes:

- Real-model browser rendering and generation cancellation validation
- A deliberately interrupted and resumed real HTTPS model transfer
- Live inbound MCP connection from a supported IDE or independent inspector
- Live IDE-host validation
- Ubuntu 24.04 and Debian 13 packaging-host certification
- Optional pinned `whisper.cpp` integration, if enabled
- Phase 19 comparative calibration evidence
- Phase 30A matched `auto`-versus-`cpu_only` real-model benchmark matrix

The detailed roadmap, deliverables, dependencies, installation outcomes, and
exit criteria are maintained in [docs/PLAN.md](docs/PLAN.md).

## Non-goals

The initial release does not aim to provide:

- In-process foundation-model training in the current release
- A custom transformer inference engine
- Public internet SaaS hosting
- Multi-node distributed inference
- Kubernetes deployment
- Custom cryptographic algorithms
- An unrestricted shell agent
- Automatic execution of model-proposed commands without policy and approval
- Unmeasured replacement of mature inference kernels
- Unrelated image, audio, or general media-generation hosting

MasterAI is the secure local programming-AI host and orchestration layer.

## Contributing

Before proposing a change:

1. Read [docs/objectives.md](docs/objectives.md),
   [docs/PLAN.md](docs/PLAN.md), and
   [rules/initial-ruleset.md](rules/initial-ruleset.md).
2. Search for the authoritative existing implementation before creating a new
   service, helper, workflow, or document.
3. Keep application source strictly ISO C++17 and isolate platform behavior
   behind explicit native boundaries.
4. Store production source in `src/`, tests in `test/`, automation in
   `scripts/`, documentation in `docs/`, models in `models/`, and durable rules
   in `rules/`.
5. Add focused responsibility and operational-flow comments to new or
   materially changed source.
6. Preserve deny-by-default behavior, input bounds, cancellation, integrity,
   authorization, audit, and memory ceilings.
7. Add or update tests in proportion to the change.
8. Update `docs/PLAN.md` when implementation or validation status changes.
9. Update this `README.md` whenever behavior, capabilities, commands,
   requirements, limitations, or validation status materially changes.
10. Do not mark a phase complete until every deliverable and real exit criterion
    has current evidence.

Third-party coding foundations, frameworks, source libraries, and
dependency-provided application foundations are prohibited. The current
explicit exception is an optional, isolated, replaceable `llama.cpp` inference
backend. Established audited cryptographic providers are required; MasterAI
does not implement cryptographic primitives.

## Documentation

- [Authoritative implementation plan](docs/PLAN.md)
- [How to use Machine Learning and models](docs/HowToUse-MachineLearning.md)
- [Project objectives](docs/objectives.md)
- [Architecture decisions](docs/architecture/)
- [`settings.json` field reference](docs/architecture/configuration.md)
- [Security design](docs/security/)
- [Operations guides](docs/operations/)
- [Performance evidence](docs/performance/)
- [IDE integration](docs/ide/)
- [API version policy](docs/api/version-policy.md)
- [Model manifest schema](models/manifest.schema.json)
- [Project rules](rules/initial-ruleset.md)

## License

MasterAI is released under the [MIT License](LICENSE).

Copyright © 2026 LordAries1972.
