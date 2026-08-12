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

