# ADR-0004: Extend platform support to Linux ARM64 and macOS (Apple Silicon)

- Status: Accepted
- Decision date: 2026-08-27
- Applies to: MasterAI platform support, amending ADR-0003's operating-system
  and non-password-secret decisions

## Context

ADR-0003 pinned release-1 platform support to Windows x86-64 and two Linux
x86-64 distributions, and left ARM64 Linux and macOS as explicitly
unsupported ("buildable experiments, not supported release targets").
`scripts/build.sh` already accepted a `Linux-arm64` platform argument despite
the matrix marking it unsupported -- an inconsistency `docs/architecture/
platform-matrix.md` itself flags as a rule violation. Separately, there was
demand to run MasterAI on Apple Silicon Macs (M4/M5/M6-class hardware).

No macOS code existed anywhere in `src/` prior to this ADR: every
platform-conditional file used a strict `#if defined(_WIN32) ... #elif
defined(__linux__) ... #else #error` chain. This ADR authorizes closing both
gaps in one pass, since they are the same category of change (extending
ADR-0003's platform scope) and touch overlapping code.

## Decisions

### Linux ARM64

Ubuntu Server 24.04 LTS arm64 joins the release-1 Linux target list,
alongside the existing x86-64 targets. The code side required no new
platform branches: `platform.cpp`'s CPU-feature probe already had an
`__aarch64__` branch (inserting `"neon"`), and every other Linux code path is
architecture-generic. No code compiled for x86-64 or Windows changed.

Validation gap: no ARM64 Linux hardware was available to build or run this
target. It is marked in the platform matrix as code-complete but
unvalidated. Whoever validates it first should update the matrix's status
column and this ADR's Consequences section.

### macOS (Apple Silicon only)

macOS 15+ (Sequoia or later) on Apple Silicon (arm64) joins the release-1
target list. Intel/x86-64 macOS is explicitly **not** supported -- a
deliberate scope choice, not an oversight, matching this decision's Apple
Silicon-only mandate. `scripts/CMakeLists.txt` fails the configure step with
a clear error if a non-arm64 Apple build is attempted. Any M-series chip is
in scope (the arm64 check is chip-generation-agnostic), but this support was
written with the newer M4/M5/M6-class chipsets as the primary target,
reflecting where current and near-term Apple Silicon hardware demand sits.

Every macOS code path added under this ADR reuses the platform's own native
mechanism in place of the Linux/Windows equivalent it has no direct analog
for:

- **OS-backed identity** (ADR-0002): macOS still ships PAM headers/libpam,
  reused as-is from the Linux path -- it authenticates against the local OS
  account password, the same role PAM plays on Linux.
- **Non-password secret storage** (amending ADR-0003's "Non-password
  secrets" section): macOS has no kernel-keyring syscalls analogous to
  Linux's `SYS_add_key`/`SYS_keyctl`. Secrets are stored in the login
  Keychain via Keychain Services (`SecItemAdd`/`SecItemCopyMatching`/
  `SecItemDelete`), MasterAI's own OS-protected secret store on this
  platform, mirroring DPAPI's and the Linux keyring's "the OS holds the key
  material" property.
- **Cryptographic primitives**: CommonCrypto (SHA-256, PBKDF2-HMAC-SHA256)
  and Security.framework's `SecRandomCopyBytes` replace the Linux AF_ALG
  kernel-crypto-socket path, which has no Darwin equivalent.
- **Live file watching**: FSEvents replaces inotify. FSEvents watches an
  entire directory subtree with one stream (no per-directory watch
  descriptor accounting, unlike inotify).
- **Service management**: `scripts/install-launchd.sh` /
  `uninstall-launchd.sh` replace `install-systemd.sh`/`uninstall-systemd.sh`.
  launchd has no equivalent to systemd's `ProtectSystem=strict`/
  `NoNewPrivileges=`/`RestrictAddressFamilies=`/`CapabilityBoundingSet=`
  kernel-level hardening keys; the launchd unit relies on a dedicated
  unprivileged service account plus `ProcessType=Background` and resource
  limits instead, and does not claim equivalent confinement.
- **Sandboxed process hardening**: `<sys/prctl.h>`'s `PR_SET_NO_NEW_PRIVS` is
  Linux-only. The macOS sandboxed-tool-execution and sandboxed-MCP-child
  paths (`tool_exec.cpp`, `mcp_outbound.cpp`) skip that one hardening step on
  macOS rather than approximate it; the `setrlimit()`-based resource caps
  remain in effect on every POSIX platform including macOS (noting that
  Darwin does not enforce `RLIMIT_AS`, a known platform limitation).
- **GPU vendor telemetry** (NVML/ADLX, Windows-only): stays Windows-only.
  Apple Silicon has no discrete GPU or comparable vendor SDK to probe.

Validation gap: no Apple Silicon hardware was available to build or run this
target. Every macOS code path is implemented in full (not a stub), consistent
with this project's stance that partial/deferred implementations are not an
acceptable default, but it is marked in the platform matrix as code-complete
and unvalidated until real hardware confirms it. Whoever validates it first
should update the matrix's status column and this ADR's Consequences section.

## Consequences

The release-1 platform list grows from three targets (Windows x86-64,
Ubuntu 24.04 x86-64, Debian 13 x86-64) to five: those three plus Ubuntu
24.04 arm64 and macOS 15+ arm64. Two of the five (Linux arm64, macOS
arm64) are code-complete but unvalidated pending hardware access; they
carry that caveat in the platform matrix until someone validates them.

Non-password secret storage (ADR-0003) now has three OS-backed mechanisms
instead of two: DPAPI (Windows), Linux kernel keyring, and macOS Keychain
Services. No change to the "unavailable OS protection fails closed" rule.

## Primary references

- Apple PAM (`security/pam_appl.h`) availability on macOS.
- Apple Keychain Services (`Security/Security.h`) documentation.
- Apple CommonCrypto (`CommonCrypto/CommonDigest.h`,
  `CommonCrypto/CommonKeyDerivation.h`) documentation.
- Apple FSEvents (`CoreServices/CoreServices.h`) documentation.
- Apple launchd and `launchctl`/`plutil` documentation.
- `docs/architecture/ADR-0003-release-1-product-baseline.md` (the baseline
  this ADR amends).
