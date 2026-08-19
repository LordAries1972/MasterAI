# Phase 32 speculative-decoding same-host matrix

Validated on 18 August 2026 with `masterai speculative-benchmark`
(`src/main.cpp`), the same host as the Phase 19/30A matrix (Intel Core
i7-6700HQ, NVIDIA GeForce GTX 960M 4 GiB, `hardware.acceleratorPolicy=auto`).
Target model `qwen25-coder-3b-q4km`, draft model `qwen25-coder-1.5b-q4km` --
both declare `"architecture":"qwen2"`, so
`check_draft_target_compatibility()` (`src/speculative_decoding.cpp`) admits
the pair. `standard` benchmark profile, real dual-model launch path
(`LlamaCppAdapter::build_launch_spec`, `src/models.cpp`).

This run also surfaced and fixed a real vendored-binary incompatibility:
the exact pinned `llama-server.exe` build rejects the `--draft-max`/
`--draft-min` flags this codebase previously emitted ("the argument has
been removed. use --spec-draft-n-max ..."), exactly the risk
`LaunchTuning::speculative_draft_model_file`'s own class comment already
disclosed. `src/models.cpp` now emits `--spec-draft-n-max`/
`--spec-draft-n-min` instead.

| Run | Draft model | Generated tokens | Elapsed | Tokens/sec |
|---|---|---:|---:|---:|
| Baseline (no draft) | none | 952 | 148.33 s | 6.42 |
| Speculative (draft admitted) | `qwen25-coder-1.5b-q4km` | 975 | 196.08 s | 4.97 |

**Result: -22.52% throughput on this host** -- speculative decoding measured
*slower*, not faster, here. This is a real, negative result, not a
placeholder: both models loaded and generated successfully (975 tokens
produced under the dual-model launch path, proving the path itself works),
but on this GPU's 4 GiB VRAM budget and 4-thread CPU allocation, the extra
draft-model inference and verification overhead outweighs any tokens-per-
step gain. This is plausible and consistent with upstream llama.cpp guidance
that speculative decoding pays off mainly when the draft model is
substantially cheaper than the target relative to available compute
headroom -- headroom this particular low-VRAM host does not have with two
q4_k_m models resident simultaneously. A host with more VRAM/compute margin
(desktop-class GPU, more free system RAM) may show a positive result; that
remains an open comparison for whoever runs this command on such a host.

Reproduce with:

```
masterai speculative-benchmark <settings> qwen25-coder-3b-q4km qwen25-coder-1.5b-q4km standard
```
