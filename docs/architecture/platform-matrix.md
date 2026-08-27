# MasterAI Supported Platform Matrix

| Platform | Architecture | Build type | Compiler | Status |
|---|---|---|---|---|
| Windows 11 / Server 2022 | x86-64 | Debug, Release | MSVC with C++17 | Supported; Debug and Release validated 2026-07-28 |
| Ubuntu Server 24.04 LTS | x86-64 | Debug, Release | GCC with C++17 | Supported release target; packaging-host certification deferred |
| Ubuntu Server 24.04 LTS | arm64 | Debug, Release | GCC with C++17 | Supported release target per ADR-0004; code-path complete, not yet build/run-validated (no ARM64 Linux hardware available) |
| Debian 13 | x86-64 | Debug, Release | GCC with C++17 | Supported release target; CI/runtime validation required before packaging |
| Ubuntu 26.04 LTS | x86-64 | Release | GCC 15.2 with C++17 | Linux code-path validation passed 2026-07-28; not a release packaging target |
| macOS 15+ (Sequoia or later) | arm64 (Apple Silicon; any M-series chip, primarily targeting M4/M5/M6-class chipsets) | Debug, Release | Clang with C++17 | Supported per ADR-0004; code-complete, unvalidated -- no Apple Silicon hardware available for build/run verification |

Unsupported: Windows 10, 32-bit targets, Intel/x86-64 macOS (a deliberate
scope choice -- Apple Silicon only, no Rosetta path), mobile platforms,
WebAssembly, and container-only deployment. Ubuntu 26.04 is not a substitute
for the pinned Ubuntu 24.04 release gate.

Every build script must state its target platform and build type. Cross-platform code must use explicit, narrow conditional compilation and provide an unsupported-platform error when no implementation exists.
