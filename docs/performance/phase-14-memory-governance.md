# Phase 14 Bounded-Memory Governance

## Runtime contract

`MemoryBudgetManager` is the single process-wide reservation authority. Each
admission includes weights, runtime buffers, KV bytes per sequence, sequence
count, transient request bytes, and a safety margin. Arithmetic overflow,
critical pressure, hard-limit excess, or violation of the live OS reserve fails
closed with corrective actions.

The strict configuration accepts an optional `memory` object containing
`hardLimitMiB` (`0` derives a safe limit), `minimumFreePercent`,
`criticalPressurePercent`, and `profile`. Minimal, balanced, and performance
profiles define bounded inference queues, worker counts, context defaults,
idle-model policy, and background throttling. The configured hard limit always
wins.

Pressure actions are ordered: stop prefetch, throttle indexing, trim caches,
reject non-interactive work, reduce context/concurrency, unload idle embedding
and generation runners, then reject new inference. `BoundedWorkQueue` enforces
both item and aggregate payload ceilings and prioritizes interactive work.

Authenticated live status is exposed at `GET /api/v1/system/memory`. Chat
generation must obtain and release an explicit compute/KV/transient lease.

## Validation

On 2026-07-29, strict ISO C++17 Windows Debug and Release builds completed all
35 steps and both complete test suites passed. The Phase 14 case verifies:

- complete admission arithmetic and a 512 MiB hard ceiling;
- actionable oversized-background-work rejection;
- native pressure sampling and bounded JSON status;
- deterministic lease release preserving shutdown headroom;
- item/byte queue saturation, interactive priority, cancellation, and cleanup.

Existing Phase 1–13 tests, including isolated inference and query tracing,
continue to pass.
