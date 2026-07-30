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
- Phase 15: In progress — cancellable bounded discovery, unchanged-file
  elimination, affected-path incremental updates, exact identifier-boundary
  symbol lookup, immutable checksummed disk generations, prior-generation
  recovery, empty/deleted-path publication, typed and coalesced change
  triggers, bounded background work, authenticated project-bound routes, an
  authenticated `index/notify` ingress for live save/watcher/branch-switch/
  periodic trigger adapters, and Debug/Release validation are implemented.
  Representative large-project ceiling evidence, an actual native or
  editor-side file-watcher/branch-switch adapter that calls the new ingress,
  and platform-specific I/O benchmark decisions remain.
- Phase 16: Planned — add deadline-bound hybrid project retrieval, ranking,
  deduplication, context budgeting, and context-source disclosure.
- Phase 17: Planned — introduce a security-partitioned, byte-bounded cache
  hierarchy with precise invalidation and administrative trimming.
- Phase 18: Planned — integrate compatible prompt-prefix and runner
  KV/session reuse under hard memory and authorization boundaries.
- Phase 19: Planned — calibrate each hardware/model/backend combination and
  persist evidence-backed low-memory, balanced, or performance tuning.
- Phase 20: Planned — evaluate advanced throughput features only after Phases
  13–19 pass their gates; every feature remains independently disableable.

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
- Remaining Phase 15–20 performance expansion: representative indexing ceiling
  evidence, live change-source adapters and portable-versus-native I/O
  benchmark decisions, hybrid retrieval, bounded cache layers, safe prompt
  reuse, host calibration, and optional advanced throughput work.

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

Status: In progress. The native disk segment/generation foundation,
fixed-capacity background service, unchanged-file elimination, affected-path
updates, exact identifier-boundary symbol lookup, deleted/empty generation
publication, typed/coalesced trigger admission, authenticated project-bound
status/rebuild/cancel/notify routes, and focused Windows Debug/Release
validation exist. The `index/notify` route gives an external editor,
watcher, or version-control process an authenticated, project-bound way to
report a save/watcher/branch-switch/periodic event; no such native or
editor-side process calls it automatically yet. A representative
large-project ceiling run, an actual live file-watcher/branch-switch adapter,
deeper language-aware symbol extraction, and platform-specific I/O benchmark
decisions remain.

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
  authenticated `POST /api/v1/projects/{id}/index/notify` route now gives an
  external adapter a stable way to submit save/watcher/branch-switch/periodic
  events; the live native or editor-side process that calls it on real file
  and branch changes is a remaining deliverable.
- A portable bounded worker-based file reader plus replaceable Windows
  overlapped-I/O and Linux `io_uring` implementations only where benchmarks
  justify them; correctness never depends on an optional I/O backend.

Installation/completion outcome:

- Each approved project gains a versioned local index stored under bounded
  runtime storage, with visible progress, cancellation, and safe rebuild.

Exit criteria:

- A representative large project is indexed and incrementally updated within
  configured peak-RAM and disk ceilings.
- Restart, corrupt-segment quarantine, partial publication, and cancellation
  tests pass without losing the last valid index generation.

### Phase 16 — Deadline-bound hybrid retrieval

Status: Planned.

Purpose:

- Retrieve a small, strong, explainable context set quickly instead of
  injecting an entire project into the model.

Dependencies:

- Phase 15 index formats and Phase 13 stage timing.

Deliverables:

- A `RetrievalPlanner` that chooses the least expensive sufficient strategy:
  no retrieval, current/open files, exact path/symbol/text, recent changes,
  diagnostics/build logs, lexical, semantic, dependency neighbours,
  conversation memory, or authorized MCP resources.
- Parallel execution only across independent retrieval paths using bounded
  workers and cooperative cancellation.
- Hybrid fusion and reranking with canonical chunk identity, duplicate and
  overlap removal, freshness, source authority, symbol/dependency proximity,
  and query intent.
- A `ContextBudgeter` that reserves generation tokens, applies per-source and
  total chunk/token caps, prefers compact high-value evidence, and never
  truncates security policy to admit more project text.
- A request deadline budget. On expiry, the coordinator uses the strongest
  authorized evidence already found or proceeds without retrieval when safe;
  it does not block indefinitely.
- Context disclosure recording which files/chunks and index generation were
  used, why they ranked, whether results were partial, and what was omitted.
- Retrieval quality, latency, memory, stale-index, membership-change, and
  authorization-boundary tests.

Installation/completion outcome:

- Browser, API, IDE, and MCP clients receive prompt status quickly and can
  inspect the actual project sources supplied to the model.

Exit criteria:

- The authored retrieval evaluation set demonstrates improvement over
  full-text-only retrieval while meeting the selected interactive deadline and
  hard memory budget.
- Project membership or policy changes invalidate access immediately.

### Phase 17 — Security-partitioned cache hierarchy

Status: Planned.

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

Status: Planned.

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
implementation merely by appearing in the plan.

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
