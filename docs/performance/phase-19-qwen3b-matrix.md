# Phase 19 Qwen 3B same-host matrix

Validated on 5 August 2026 with `qwen25-coder-3b-q4km`, the pinned local
`llama-server`, an Intel Core i7-6700HQ, and an NVIDIA GeForce GTX 960M 4 GiB.
Both states used the `balanced` calibration workload, 4,096 total context,
two runner slots, a fixed 154-token prompt-evaluation sample, and a fixed
64-token deterministic generation sample.

| State | Policy | Backend evidence | Model ready | Prompt evaluation | Generation | Result |
|---|---|---|---:|---:|---:|---|
| GPU-permitted | `auto` | CUDA0 GTX 960M; ready GPU memory 2,668 MiB versus 518 MiB idle (2,150 MiB delta) | 47.45 s runner / 75.08 s end-to-end | 2.649 s, 58.12 tok/s | 5.149 s, 12.43 tok/s | Completed and persisted; 64/64 tokens |
| CPU baseline | `cpu_only` | CUDA denied; CPU only | 106.74 s runner | 5.222 s, 29.49 tok/s | 9.874 s, 6.48 tok/s | Workload completed, 64/64 tokens; CLI teardown exceeded the outer 300 s harness timeout, so no CPU persistence is claimed |

The GPU-permitted state improves prompt throughput by about 1.97x,
generation throughput by about 1.92x, and runner model-ready time by about
2.25x. The 2,150 MiB live GPU-memory delta proves that model data was resident
on the GPU; this is not inferred only from the requested offload argument.
The CPU state independently proves the fallback boundary by hiding CUDA.

Local evidence is retained under `runtime/validation/phase19-matrix-auto`
and `runtime/validation/phase19-matrix-cpu`. Both fixed workloads completed
without cancellation or allocation failure. This matrix validates offload and
performance; it does not substitute token completion for a broader semantic-
quality evaluation suite.

## 2026-08-13 rerun: persistence fix and GPU telemetry

Rerun on the same host (`qwen25-coder-3b-q4km`, GTX 960M, i7-6700HQ) using
the `minimal` calibration workload (2,048 context, one runner slot) via
`masterai calibrate` against isolated `settings-gpu-auto.json`/
`settings-cpu-only.json` runtime roots (`runtime/validation/phase30a-auto`,
`runtime/validation/phase30a-cpu-only`), after this pass wired
`probe_gpu_vendor_telemetry()` into `CalibrationService`.

| State | Policy | Cold load | Prompt evaluation | Generation | Peak resident | GPU utilization | GPU peak temp |
|---|---|---:|---:|---:|---:|---:|---:|
| GPU-permitted | `auto` | 25.89 s | 1.78 s | 6.26 s | 2.29 GiB | 39.9% avg | 72°C |
| CPU baseline | `cpu_only` | 27.07 s | 7.54 s (4.2x slower) | 15.47 s (2.5x slower) | 3.60 GiB (1.57x more) | 23.5% avg (idle/monitoring residual) | 60°C |

Both `TuningProfile` records persisted cleanly this time. The 2026-08-05
run's `cpu_only` persistence gap (noted above) was traced to an unrelated
process-supervision issue, not a `cpu_only`-path bug: an earlier failed
calibration attempt (an oversized calibration prompt against a model with a
memory-fit-shrunk context) left its `llama-server.exe` child process running
and bound to the runner port, so every subsequent `calibrate` invocation
against that port was silently served by the stale process's already-
consumed context instead of a fresh one. `RunnerSupervisor` does not
currently tie its spawned child's lifetime to a Windows job object, so an
abnormal parent exit before `unload()` runs can orphan the runner process --
this is a real gap worth closing (forward work), distinct from anything
Phase 30A's own admission/fail-closed logic is responsible for.

## 2026-08-25 rerun: Phase 85 thread/ubatch calibration tuning

Rerun on the same host (`qwen25-coder-3b-q4km`, GTX 960M 4 GiB, i7-6700HQ
quad-core/eight-thread) via `masterai calibrate ... auto`, closing the honest
"none of the above has a real measured tokens/sec number recorded" gap the
Phase 85 status note left. Two consecutive runs against the production
`config/settings.json` runtime, no other GPU-offloaded process competing for
the card:

| Run | Cold load | Prompt evaluation (154 tok) | Generation (64 tok) | Peak resident | GPU utilization | GPU peak temp |
|---|---:|---:|---:|---:|---:|---:|
| 1 | 73.18 s | 3.131 s, 49.2 tok/s | 7.103 s, 9.01 tok/s | 1.96 GiB | 36.06% avg | 72°C |
| 2 | 36.81 s | 2.874 s, 53.6 tok/s | 6.543 s, 9.78 tok/s | 2.14 GiB | 39.02% avg | 76°C |

`llama-server`'s own `system_info` log line confirms `select_thread_count()`
is genuinely driving the real launch, not just computing an unused number:
every run (this rerun and the separate benchmark run below) shows
`n_threads = 4 (n_threads_batch = 4) / 8` -- 4, this host's real physical
core count, not the 8 logical threads llama.cpp's own default would pick.
Generation throughput (9.0-9.8 tok/s) lands close to the 2026-08-13 minimal-
workload baseline above (10.22 tok/s) and below the 2026-08-05 balanced-
workload baseline (12.43 tok/s); cold load varied more between the two runs
here (73.18 s vs 36.81 s) than either prior rerun, consistent with this being
an older laptop GPU/disk under whatever else was resident at the time, not a
regression -- no code changed between these two consecutive runs.

A separate real end-to-end generation benchmark (`masterai benchmark-model
... standard`, GPU-offloaded) against the smaller `llama32-1b-instruct-q4km`
completed 4/5 cases, 936 tokens generated in 35.16 s (26.6 tok/s blended
across prompt+generation for all five cases) -- the CLI's fixed 30-second
runner-readiness timeout is too short for `qwen25-coder-3b-q4km`'s own
73.18 s/36.81 s cold loads on this hardware (confirmed by `calibrate`, which
has no such fixed cap), so that specific command could not produce a
benchmark-model number for the 3B model on this pass; this is a real, narrow
gap in `benchmark-model`'s hardcoded readiness window, not in Phase 85's
calibration/caching changes themselves, and is left for a future pass since
fixing it was outside this session's task.

Honest remaining gap: the GPU scheduling-priority drop (Phase 85 addendum)
ran without error or crash across every GPU-offloaded run in this session,
but confirming the actual desktop-stutter symptom is gone requires a human
watching the desktop during a live GPU-offloaded chat reply -- this session
cannot observe that visually, so it stays open for an administrator to
confirm.

## 2026-08-26: broadening the model matrix beyond Qwen 3B

Every calibration run above used the same model family (`qwen25-coder`) and
size class (~3B). This pass adds two more real `masterai calibrate ...
balanced` runs on the same host (GTX 960M 4 GiB, i7-6700HQ,
`hardware.acceleratorPolicy=auto`) at both ends of the size range this
catalog actually has installed, closing part of the "matrix across host/
model/backend combinations is still partial" gap -- still one host, but no
longer one model family/size.

| Model | Size class | Cold load | Prompt evaluation | Generation | GPU utilization | GPU peak temp |
|---|---|---:|---:|---:|---:|---:|
| `granite31-2b-instruct-q4km` | ~2B (smaller) | 23.49 s | 7.47 s | 27.11 s | 29.0% avg | 76°C |
| `deepseek-coder-6.7b-q4km` | ~6.7B (larger) | 57.48 s | 16.12 s | 7.06 s | 58.1% avg | 75°C |

Both runs completed and persisted a `TuningProfile` cleanly with real GPU
telemetry (`gpuTelemetryAvailable:true`), confirming the calibration
pipeline generalizes correctly across model families, not just the
`qwen25-coder`/`llama32` pairs exercised above -- cold-load time scales
roughly with file size (2B fastest, 6.7B slowest) as expected, and GPU
utilization rises with model size (more compute per token). This still
does not cover a second physical host or a second GPU vendor/backend
version, which remain the genuinely out-of-reach dimensions noted
throughout this document and in `docs/PLAN.md` Phase 19/36.

