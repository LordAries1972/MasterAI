# ADR-0001: C++17 Native Architecture

Status: Accepted

## Decision

MasterAI application source uses ISO C++17 exclusively. Optional Assembly is permitted only behind a C++17 interface when profiling proves a measurable benefit. C++20 or later features and additional application languages are prohibited.

The control plane is a native executable. Inference backends run in separately supervised processes and communicate only through validated local interfaces. The main process never maps large model weights into its own address space.

MasterAI is deliberately built independently from the ground up so original architectural ideas can be introduced without being constrained by inherited frameworks. Optional adapters do not control the internal architecture.

Production source is stored below `src/`, test cases below `test/`, CMake and
operational automation below `scripts/`, documentation below `docs/`, and model
artifacts below `models/`.

## Consequences

- Every target sets `CXX_STANDARD 17`, disables compiler extensions, and treats unsupported newer-language requirements as configuration errors.
- Platform differences are isolated with explicit `_WIN32` and `__linux__` conditional directives.
- The browser interface will be served by the native process without TypeScript.
- Foundational components use ISO C++17 and native operating-system APIs. Third-party coding foundations, frameworks, and source libraries are prohibited.
- `llama.cpp` is an explicit optional exception for a process-isolated, replaceable inference backend. It is not part of the control-plane foundation.
- Cryptographic operations use native operating-system providers. Capabilities remain fail-closed where a validated native provider is not yet available.
