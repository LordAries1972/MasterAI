# Phase 13 Query Measurement Baseline

## Scope

Phase 13 adds measurement only. It does not enable speculative optimization or
move model memory into the control plane.

The authoritative `QueryCoordinator` assigns an opaque request identity and
records real monotonic transitions through admission, authentication,
normalization, classification, retrieval planning, retrieval, ranking, prompt
assembly, runner queueing, prompt evaluation, generation, persistence, and
release. Retention is bounded and defaults to 256 traces.

Native Windows and Linux probes report physical and logical CPUs, CPU features,
NUMA availability, physical and virtual memory, process resident/private/commit
memory, cumulative page faults, GPU backend libraries, storage class/capacity,
and the isolated runner's separately attributed resident memory.

Authenticated interfaces are:

- `GET /api/v1/system/resources`
- `GET /api/v1/requests/{request-id}`
- `POST /api/v1/performance/baseline`

Baseline records are keyed by SHA-256 host identity plus caller-supplied exact
model, backend, build, settings, and prompt-suite hashes. Cold and warm runs are
explicit. Instrumentation overhead is timed and included in every baseline.

## Validation

Validation on 2026-07-29 used MSVC 19.38 with strict ISO C++17:

- Windows x86-64 Debug configured and built all 34 steps.
- Windows x86-64 Release configured and built all 34 steps.
- Debug and Release `masterai_core_tests` passed.
- The Phase 13 case drove the isolated fake runner from admission through a
  streamed first token, inference metrics, resource high-water sampling,
  runner unload, final release, JSON disclosure, and reproducible baseline.
- Existing Phase 1–12 correctness cases remained unchanged and passed.

The fake runner validates the control-plane instrumentation boundary without
claiming real-model throughput. Exact real-model baselines remain model-specific
operational evidence, not a prerequisite for the instrumentation phase.
