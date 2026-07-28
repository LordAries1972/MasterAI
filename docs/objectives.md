# MasterAI — Project Objectives

## 1. Project Purpose

MasterAI is a native, secure, modular local programming AI system. It coordinates local language models, authenticated users, projects, chats, files, benchmarks, and MCP integrations without requiring Docker at runtime.

The system prioritizes:

- ISO C++17 implementation and predictable native performance.
- Local ownership of models, project data, and operational state.
- Deny-by-default security.
- Process isolation between the control plane and inference engines.
- Replaceable inference, hardware, storage, and integration adapters.
- Observable and reproducible validation.

## 2. Scope Boundary

The initial release provides a secure orchestration and hosting platform. It does not implement a transformer inference engine from first principles. Initial inference uses a separately supervised `llama.cpp`-compatible GGUF backend.

All produced application source is C++17 or explicitly approved Assembly. Documents belong under `docs/`, production source under `src/`, test cases under `test/`, automation under `scripts/`, model artifacts under `models/`, and project rules under `rules/`.

## 3. Deployment Objectives

- Run natively on Windows x86-64 and Linux x86-64.
- Keep Linux ARM64 as a validation target.
- Support Debug and Release build types through CMake.
- Bind only to loopback by default.
- Require explicit TLS and authentication controls before intranet exposure.
- Support graceful startup, shutdown, restart, and service management.
- Keep generated runtime state separate from source and model artifacts.

## 4. Security Objectives

- Use native operating-system cryptographic providers and never substitute unvalidated cryptography.
- Delegate password verification to native operating-system identity
  (`LogonUserW` on Windows and PAM on Linux), and fail closed when that provider
  is absent.
- Never store, hash, reversibly encrypt, log, or forward an operating-system
  password; erase transient credential buffers after verification.
- Use cryptographically random opaque sessions stored only as hashes.
- Enforce role and permission checks on privileged operations.
- Apply CSRF, origin, host, rate, request-size, and timeout controls.
- Use an in-house bounded persistence layer with explicit schema versions and atomic records.
- Canonicalize and authorize filesystem paths before access.
- Reject symlink traversal by default.
- Treat model manifests, model weights, project content, attachments, and MCP output as untrusted.
- Launch inference backends without shell interpolation.
- Keep append-oriented, sanitized audit records.
- Never log passwords, raw session tokens, API keys, or private keys.

## 5. Configuration Objectives

- Provide safe compiled defaults.
- Validate persisted configuration against a versioned schema.
- Reject unknown fields unless inside an approved extension namespace.
- Apply precedence in this order: defaults, persisted settings, approved environment variables, command-line overrides.
- Write changes through a temporary file followed by an atomic replacement.
- Preserve a recoverable backup before reset.
- Fail clearly instead of prompting when running non-interactively.

## 6. Model Management Objectives

- Use `models/<category>/<model-id>/manifest.json`.
- Support the approved model categories documented in `docs/architecture/model-taxonomy.md`.
- Validate schema version, identity, category, format, backend, paths, sizes, and digests.
- Never infer trust solely because a file exists.
- Track discovery, validation, readiness, loading, failure, and quarantine states.
- Prevent model paths from escaping their assigned directory.
- Preserve provenance and licensing metadata.

## 7. Hardware Assessment Objectives

- Detect platform, architecture, logical CPU count, total and available RAM, and free disk.
- Add GPU and instruction-set probes through replaceable platform adapters.
- Estimate model weights, context cache, backend working memory, graph buffers, reserve, concurrency, and safety margin.
- Return one of: Unsupported, Memory Risk, Slow, Usable, or Recommended.
- Block unsafe loads and provide required versus available resources with a corrective action.

## 8. Benchmarking Objectives

- Provide quick, standard, and extended profiles.
- Record exact model, backend, build, settings, hardware, prompts, and timing.
- Compare models only with compatible benchmark inputs.
- Preserve correctness and security controls during performance measurement.

## 9. Web Interface Objectives

- Provide first-administrator setup, login, chat, project, model inventory, settings, job progress, benchmark, and audit views.
- Stream inference output with cancellation.
- Clearly distinguish ready, blocked, failed, and unverified states.
- Present errors with the failed control, safe corrective action, and next validation step.
- Meet keyboard-navigation and accessible-label requirements.
- Serve browser assets through the native C++17 application without TypeScript.

## 10. API Objectives

- Version HTTP routes under `/api/v1/`.
- Provide live and ready health endpoints.
- Use bounded request headers, bodies, timeouts, and concurrency.
- Reject unknown request fields by default.
- Support streaming and cancellation for generation.
- Return structured, sanitized errors without leaking sensitive internals.

## 11. MCP Objectives

### Inbound MCP

- Expose only explicitly approved MasterAI tools and resources.
- Map authenticated client identity to scopes and project access.
- Support `stdio` and Streamable HTTP with conformance tests.

### Outbound MCP

- Keep outbound tool authority separate from inbound client identity.
- Require registry approval, tool allow-lists, user and project authorization, cancellation, sandboxing, and auditing.
- Keep optional legacy SSE isolated and disabled by default.

## 12. Script Objectives

- Store all CMake and operational automation under `scripts/`.
- Require every build invocation to declare its platform and build type.
- Provide configure, build, test, start, stop, model, benchmark, and diagnostic operations as the relevant phases are implemented.
- Avoid shell evaluation of untrusted values.
- Return nonzero status on failure with an actionable diagnostic.

## 13. Availability and Lifecycle Objectives

- Validate configuration and security prerequisites before readiness.
- Supervise inference runners separately from the control plane.
- Bound restart attempts and preserve crash diagnostics.
- Drain active work during graceful shutdown.
- Make restart and shutdown authenticated, authorized, and audited.
- Provide backup, restore, upgrade, rollback, and recovery validation before production release.

## 14. Performance Objectives

- Establish measurements before optimization.
- Keep large model memory outside the control-plane process.
- Bound queues, buffers, contexts, and concurrent requests.
- Minimize copies and allocations only when profiling identifies them.
- Never bypass authorization, validation, auditing, cancellation, or resource limits for speed.

## 15. Documentation Deliverables

- `docs/objectives.md`
- `docs/PLAN.md`
- Architecture decision records
- Platform matrix
- Threat model
- API and MCP version policy
- Model taxonomy and manifest specification
- Configuration reference
- Operations and hardening guides
- Backup and recovery guide
- IDE integration guides
- Contributor and test-strategy guides

## 16. Acceptance Criteria

The first production-capable release is accepted only when:

1. It runs natively without Docker.
2. First-run configuration works and persists safely.
3. Loopback is the default network binding.
4. Intranet mode requires deliberate configuration, authentication, and TLS.
5. Authentication, sessions, roles, authorization, and audit logging pass security tests.
6. Models are discovered from categorized directories under `models/`.
7. Unsuitable or unverified models are blocked according to policy.
8. Downloads resume and verify correctly.
9. At least one isolated inference adapter works reliably; `llama.cpp` may be offered as an optional backend.
10. Chat, projects, attachments, model selection, streaming, and cancellation work.
11. Benchmarking produces reproducible records.
12. Inbound and outbound MCP work over approved transports.
13. Optional legacy compatibility remains isolated and disabled by default.
14. VS Code and Visual Studio connect through documented APIs.
15. Shutdown and restart are graceful and authorized.
16. Security, integration, recovery, conformance, and performance tests pass.
17. Every application target compiles as strict ISO C++17 without C++20 dependencies.
