# Contributing

> Part of the [MasterAI README](../README.md).

Before proposing a change:

1. Read [docs/objectives.md](objectives.md),
   [docs/PLAN.md](PLAN.md), and
   [rules/initial-ruleset.md](../rules/initial-ruleset.md).
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
9. Update the root `README.md` (and any split documentation page under `docs/`
   it links to) whenever behavior, capabilities, commands, requirements,
   limitations, or validation status materially changes.
10. Do not mark a phase complete until every deliverable and real exit criterion
    has current evidence.

Third-party coding foundations, frameworks, source libraries, and
dependency-provided application foundations are prohibited. The current
explicit exceptions are an optional, isolated, replaceable `llama.cpp`
inference backend, an optional, isolated, replaceable DuckDB CLI
(`duckdb.exe`) backend used for Parquet-document ingestion, and NVIDIA
NVML/AMD ADLX for GPU utilization/thermal-trend probing (Phase 19). The
first two are distributed separately, never vendored or linked, and
configured by filesystem path in `settings.json`. NVML/ADLX are narrower:
no vendor binary is linked (both are loaded from the driver-installed DLL
at runtime and fail closed when that driver is absent), but AMD's public
interface headers and helper source are vendored under `third_party/ADLX`
since ADLX has no CUDA-Toolkit-style pre-installed system location to load
headers from; see `docs/architecture/ADR-0001-cpp17-native-architecture.md`.
Established audited cryptographic providers are required; MasterAI does not
implement cryptographic primitives.
