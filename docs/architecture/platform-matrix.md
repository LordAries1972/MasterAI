# MasterAI Supported Platform Matrix

| Platform | Architecture | Build type | Compiler | Status |
|---|---|---|---|---|
| Windows 11 / Server 2022 | x86-64 | Debug, Release | MSVC with C++17 | Supported; Debug and Release validated 2026-07-28 |
| Ubuntu Server 24.04 LTS | x86-64 | Debug, Release | GCC with C++17 | Supported release target; packaging-host certification deferred |
| Debian 13 | x86-64 | Debug, Release | GCC with C++17 | Supported release target; CI/runtime validation required before packaging |
| Ubuntu 26.04 LTS | x86-64 | Release | GCC 15.2 with C++17 | Linux code-path validation passed 2026-07-28; not a release packaging target |

Unsupported: Windows 10, Linux ARM64, 32-bit targets, macOS, mobile
platforms, WebAssembly, and container-only deployment. Ubuntu 26.04 is not a
substitute for the pinned Ubuntu 24.04 release gate.

Every build script must state its target platform and build type. Cross-platform code must use explicit, narrow conditional compilation and provide an unsupported-platform error when no implementation exists.
