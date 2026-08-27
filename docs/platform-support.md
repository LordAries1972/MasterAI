# Supported targets and baseline hardware

> Part of the [MasterAI README](../README.md).

## Release 1 target matrix

- Windows 11 x86-64
- Windows Server 2022 x86-64
- Ubuntu Server 24.04 LTS x86-64
- Ubuntu Server 24.04 LTS arm64 (ADR-0004; code-complete, not yet build/run
  validated on real ARM64 Linux hardware)
- Debian 13 x86-64
- macOS 15+ (Sequoia or later) on Apple Silicon (arm64) only, no Intel Mac
  (ADR-0004; code-complete, not yet build/run validated on real Apple
  Silicon hardware). Any M-series chip is in scope, with the newer
  M4/M5/M6-class chips as the primary chipsets this support targets

Other distributions, Intel/x86-64 macOS, WSL, and containers are
experimental build or validation environments rather than supported release
targets. See [the platform matrix](architecture/platform-matrix.md) for
per-target validation status.

## Release 1 baseline hardware

- x86-64 CPU with SSE4.2 and at least four logical processors on Windows and
  x86-64 Linux; Apple Silicon and ARM64 Linux targets have no equivalent
  SSE4.2 floor (NEON is detected instead)
- 16 GiB physical RAM, retaining at least 2 GiB for the operating system
- 20 GiB free storage plus model and backup requirements
- Optional GPU; CPU inference remains the compatibility baseline
- For GPU acceleration: a supported CUDA, Vulkan, or HIP backend, a compatible
  driver, and at least 8 GiB dedicated VRAM. Dedicated VRAM size is
  auto-detected on Windows (DXGI); the largest real (non-software) adapter
  found decides whether a given model's weights are recommended for full
  GPU offload. Discrete-GPU vendor telemetry (NVML/ADLX) is Windows-only;
  Apple Silicon has no discrete GPU or comparable vendor SDK to probe.

Individual model manifests may require more capable hardware. MasterAI reports
`Unsupported`, `Memory Risk`, `Slow`, `Usable`, or `Recommended` and blocks
unsafe loads according to policy.
