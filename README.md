# MasterAI

**A secure, native, local programming AI control plane built from the ground up
in ISO C++17.**

[![Language: C++17](https://img.shields.io/badge/language-ISO%20C%2B%2B17-00599C.svg)](docs/architecture/ADR-0001-cpp17-native-architecture.md)
[![Platforms](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-4C8BF5.svg)](docs/architecture/platform-matrix.md)
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
> Several major capabilities are implemented and test-covered, but
> distribution-packaging certification and the full multi-machine/multi-
> storage-media performance certification matrix remain outstanding (both
> genuinely require physical hardware this checkout does not have). See
> [Project status](docs/project-status.md) and the authoritative
> [implementation plan](docs/PLAN.md).

## Table of contents

| Section | What it covers |
| --- | --- |
| [Overview and architecture](docs/overview.md) | Why MasterAI, project aims, the control-plane/runner split, and core design principles |
| [Implemented capability areas](docs/capabilities.md) | Everything the current native source implements, feature by feature |
| [Project status](docs/project-status.md) | Per-phase implementation and certification status, plus the validation narrative |
| [Supported targets and baseline hardware](docs/platform-support.md) | Release 1 OS/architecture matrix and minimum hardware |
| [Building from source](docs/building.md) | Prerequisites and per-platform build/test commands |
| [First run](docs/first-run.md) | Step-by-step from a fresh build to a working chat reply |
| [Configuration](docs/configuration-overview.md) | Settings precedence and approved environment overrides |
| [Models](docs/models.md) | Model tree layout, manifests, scanning, and verification |
| [Web application and API](docs/web-api.md) | Browser workflows and the versioned HTTP/OpenAI-compatible APIs |
| [Machine Learning](docs/machine-learning.md) | The administrator-only ML administration area and real executors |
| [MCP and IDE integration](docs/mcp-ide-integration.md) | Inbound/outbound MCP and the VS Code / Visual Studio adapters |
| [Command-line interface](docs/cli.md) | Every `masterai` subcommand |
| [Scripts reference](docs/scripts-reference.md) | Every `scripts/*` helper and its arguments |
| [Persistence, operations, and recovery](docs/persistence-operations.md) | Record store, runtime layout, and operational controls |
| [Security model](docs/security-model.md) | Trust boundaries and the key controls |
| [Performance and bounded-memory direction](docs/performance.md) | The measured performance layer and request path |
| [Testing and validation](docs/testing.md) | Running the native correctness suite |
| [Roadmap](docs/roadmap.md) | Near-term work and outstanding certification |
| [Repository layout](#repository-layout) | Top-level directory map |
| [Non-goals](#non-goals) | What the initial release deliberately does not do |
| [Contributing](docs/contributing.md) | Rules and expectations for changes |
| [Documentation](#documentation) | Index of all reference documents |
| [License](#license) | MIT |

## Overview and architecture

MasterAI separates a security-sensitive native C++17 control plane from
replaceable, process-isolated model runner processes. The control plane owns
configuration, authentication, authorization, users, projects, chats, model
metadata, downloads, benchmarks, MCP policy, process supervision, health,
metrics, and graceful lifecycle operations — it never maps large model weights
into its own address space. Initial inference uses an optional, version-pinned
`llama.cpp` server behind a validated process adapter that can be replaced
without changing MasterAI's external contract.

Read more: [docs/overview.md](docs/overview.md).

## Implemented capability areas

The current source includes native implementations spanning schema-versioned
configuration, OS-principal and local-password authentication with hash-chained
audit, verified model discovery and lifecycle, process-isolated `llama.cpp`
supervision, the authenticated chat/project web application (including agentic
tool use, durable chat memory, and per-model effort settings), resumable
verified downloads, reproducible benchmarking, inbound and outbound MCP,
IDE integrations, operations hardening, a deadline-bound retrieval and
security-partitioned cache layer, hardware/model calibration and GPU-offload
decisions, CPU-only low-memory operation, storage tiering, speculative
decoding, distributed local and intranet-worker runners, an adaptive
performance controller, a regression gate, and an administrator-only Machine
Learning control plane with real tabular training/evaluation/RAG executors.

Full feature list: [docs/capabilities.md](docs/capabilities.md).

## Project status

MasterAI is **not yet production-ready**. Most phases are implemented and
test-covered; the outstanding gaps are distribution-packaging certification and
the cross-device physical performance matrix, both of which need hardware this
checkout does not have. The per-phase status table and validation narrative
live in [docs/project-status.md](docs/project-status.md); the authoritative
record is [docs/PLAN.md](docs/PLAN.md).

## Supported targets and baseline hardware

Release 1 targets Windows 11 / Server 2022 x86-64, Ubuntu Server 24.04 LTS
(x86-64 and arm64), Debian 13 x86-64, and macOS 15+ on Apple Silicon.
Baseline hardware is a 4-core SSE4.2 (or NEON) CPU, 16 GiB RAM, and 20 GiB free
storage; GPU acceleration is optional and needs a supported CUDA/Vulkan/HIP
backend with at least 8 GiB VRAM.

Details and per-target validation status: [docs/platform-support.md](docs/platform-support.md)
and [the platform matrix](docs/architecture/platform-matrix.md).

## Building from source

You need Git, CMake, and a C++17 compiler. On Windows:

```powershell
.\scripts\build.ps1 -Platform Windows-x64 -BuildType Release
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Release
```

On Linux / macOS:

```sh
sh ./scripts/build.sh Release Linux-x86_64
sh ./scripts/test.sh Release Linux-x86_64
```

Prerequisites, ARM64 / Apple Silicon builds, and the clean helpers:
[docs/building.md](docs/building.md).

## First run

Getting from a fresh build to a chat reply takes seven ordered steps: build the
binary, put a `llama.cpp` server executable and a GGUF model with a valid
manifest on the machine, run the configuration wizard, `verify-models`, start
the service, create the first administrator with the one-time setup token, then
create a project and chat. Skipping the model-verification step is the most
common cause of `model_not_ready`.

The full walkthrough, health checks, and diagnostics:
[docs/first-run.md](docs/first-run.md).

## Configuration

Configuration precedence is compiled defaults → persisted settings → approved
environment variables → command-line overrides. Unknown fields are rejected;
the default listener is loopback-only. A complete field-by-field reference is
[docs/architecture/configuration.md](docs/architecture/configuration.md); the
precedence and environment-override summary is
[docs/configuration-overview.md](docs/configuration-overview.md).

## Models

Models live under `models/<category>/<model-id>/` with a `manifest.json` that
binds identity, provenance, license, exact size, and SHA-256 digest. Discovery
never hashes files on a page load — it trusts a persisted verification cache
built by `verify-models`, which must be refreshed after any model file change.

Layout, manifest schema, and the scan/verify workflow:
[docs/models.md](docs/models.md).

## Web application and API

The native service serves role-gated browser workflows (setup/login, projects
and chats, model inventory and downloads, benchmarks, status, and the
administrator-only ML pages) and versioned `/api/v1/` HTTP APIs. Three
additional routes (`GET /api/v1/model-catalog`, `GET /api/v1/models`, and an
OpenAI-shaped `POST /v1/chat/completions`) let a third-party client use a
running instance as a local LLM backend. By default it listens on
`127.0.0.1:7070`.

Full workflow and endpoint list: [docs/web-api.md](docs/web-api.md).

## Machine Learning

MasterAI provides a native, administrator-only Machine Learning administration
area at `/app/ml` with durable lifecycle records for every interface the plan
names, plus real executors: tabular training by gradient descent, evaluation
scoring, model comparison, knowledge-file ingestion, local/learned embedding
vector stores, RAG retrieval and grounded answer generation, web research
(`/app/ml/research`, off by default), and more. Job types still marked
`Planned` record intent and state, not execution.

Web research (Phase 103) queries the official Google Custom Search and Bing
Web Search APIs (administrator-supplied keys), scores each result's source
domain against an editable reliability-tier table, fetches and reads only
the sources at or above a configurable reliability threshold (80% by
default), and saves relevant findings into the same knowledge base RAG
retrieval draws on. Disabled until an administrator turns it on and
configures at least one provider.

Overview and the honest capability boundary:
[docs/machine-learning.md](docs/machine-learning.md). Classroom-style guide:
[How to Use Machine Learning and Models with MasterAI](docs/HowToUse-MachineLearning.md).

## MCP and IDE integration

MasterAI pins MCP protocol revision `2025-11-25`. Inbound MCP supports
newline-delimited `stdio` and Streamable HTTP with authenticated, project-bound
tools; outbound MCP uses a separate deny-by-default registry with pinned
executable digests, allow-lists, per-call approval, and audit. Protected VS
Code / Agent-Coder and Visual Studio 2022 connection profiles are generated
through the CLI.

Details: [docs/mcp-ide-integration.md](docs/mcp-ide-integration.md).

## Command-line interface

The native executable exposes subcommands for configuration, serving, model
scanning/verification/download, benchmarking, MCP and IDE profile generation,
backup/restore, secret and log rotation, recovery, upgrade/rollback, and
several probes and calibrations.

Full command list: [docs/cli.md](docs/cli.md).

## Scripts reference

Every `scripts/*.ps1` (Windows) has a matching `scripts/*.sh` (Linux/macOS)
with the same behavior: build, configure, start, stop, test, diagnose, rehash,
clean, verify-objectives, and the systemd / launchd service installers.

Argument-by-argument table: [docs/scripts-reference.md](docs/scripts-reference.md).

## Persistence, operations, and recovery

MasterAI uses an internally authored bounded record store with explicit schema
versions, checksummed journals, atomic replacement, and recovery scanning.
Runtime state is kept separate from source and models. Operational controls
include offline backup/restore, secret and log rotation, crash-residue
recovery, hash-bound upgrade/rollback, and hardened service units.

Details: [docs/persistence-operations.md](docs/persistence-operations.md) and
the [operations documentation](docs/operations/).

## Security model

MasterAI treats browser input, source trees, attachments, model manifests and
weights, downloads, inference responses, and MCP data as untrusted. Controls
include local-password and optional OS-identity sign-in, hashed opaque
sessions, scoped project-bound tokens, DPAPI / OS secret providers, loopback
binding, host/origin/CSRF/rate limits, canonical path containment, digest
pinning, and hash-chained audit. Security is not disabled just because the
service listens on localhost.

Details: [docs/security-model.md](docs/security-model.md). Threat model:
[docs/security/threat-model.md](docs/security/threat-model.md).

## Performance and bounded-memory direction

MasterAI measures before optimizing. A completed bounded-memory foundation
provides one system-wide admission authority, an OS safety reserve, hard
per-category limits, and deterministic pressure handling. On top of it sit
asynchronous storage, hierarchical and prompt caches, staged retrieval
fan-out, weighted-fair scheduling, warm-model management, KV-cache lifecycle,
topology-aware placement, and model-tier routing — none of which may bypass
authorization, integrity, auditing, cancellation, quality checks, or memory
ceilings.

Details and the request-path diagram: [docs/performance.md](docs/performance.md).

## Testing and validation

Run the native correctness suite with `scripts/test.ps1` / `scripts/test.sh`
after building. It covers configuration, persistence and recovery, identity,
security, model integrity, runner isolation, projects/chats, downloads,
benchmarks, MCP, operations, memory, indexing, retrieval, caching, scheduling,
routing, and Machine Learning record behavior. Fixture tests do not replace the
exit criteria in [docs/PLAN.md](docs/PLAN.md).

Details: [docs/testing.md](docs/testing.md).

## Roadmap

Near-term work covers independent evaluation of Phase 20 throughput options,
closing the documented Phase 21–30 scope trims, extending the performance
administration pages, wiring the intranet worker pool into live dispatch, and
finishing the ML executors still marked `Planned`. Outstanding certification
includes real-model browser cancellation, Ubuntu 24.04 / Debian 13 packaging
hosts, optional `whisper.cpp`, and broader Phase 19 semantic-quality scoring.

Details: [docs/roadmap.md](docs/roadmap.md). Authoritative plan:
[docs/PLAN.md](docs/PLAN.md).

## Repository layout

```text
MasterAI/
├── docs/       Architecture, security, operations, APIs, objectives, plan, and the split README sections
├── models/     Categorized local model layout and manifest schema
├── rules/      Durable project implementation rules
├── scripts/    CMake, build, test, lifecycle, clean, and service automation
├── src/        Native ISO C++17 production source
├── test/       Native test cases and isolated fixtures
├── integrations/ Thin, authorized VS Code and Visual Studio host glue
├── .gitignore  Generated binary, build, model, archive, and editor exclusions
├── LICENSE     MIT license
└── README.md   Current GitHub project overview
```

Generated `build/` and runtime state are not source. Large model payloads and
compiled binaries must not be committed.

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

## Documentation

Split README sections:

- [Overview and architecture](docs/overview.md)
- [Implemented capability areas](docs/capabilities.md)
- [Project status](docs/project-status.md)
- [Supported targets and baseline hardware](docs/platform-support.md)
- [Building from source](docs/building.md)
- [First run](docs/first-run.md)
- [Configuration](docs/configuration-overview.md)
- [Models](docs/models.md)
- [Web application and API](docs/web-api.md)
- [Machine Learning](docs/machine-learning.md)
- [MCP and IDE integration](docs/mcp-ide-integration.md)
- [Command-line interface](docs/cli.md)
- [Scripts reference](docs/scripts-reference.md)
- [Persistence, operations, and recovery](docs/persistence-operations.md)
- [Security model](docs/security-model.md)
- [Performance and bounded-memory direction](docs/performance.md)
- [Testing and validation](docs/testing.md)
- [Roadmap](docs/roadmap.md)
- [Contributing](docs/contributing.md)

Reference material:

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
