# Scripts reference

> Part of the [MasterAI README](../README.md).

Every `scripts/*.ps1` (Windows) has a matching `scripts/*.sh` (Linux/macOS)
with the same behavior and argument order unless noted. Run them from the
repository root. `[settings-file]` always defaults to `config/settings.json`
when omitted; `[BuildType]` always defaults to `Release` except where noted.
`[Platform]` (the `.sh` scripts only) always defaults to `Linux-x86_64` when
omitted; pass `Linux-arm64` or `Darwin-arm64` (macOS, Apple Silicon) to
target those builds instead.

| Script | Arguments | Purpose |
|---|---|---|
| `build.ps1` / `build.sh` | `-BuildType <Debug\|Release>` `-Platform <Windows-x64>` `-VerifyModels` `-ModelsRoot <path>` &nbsp;/&nbsp; `<BuildType> <Platform>` | Configures CMake (Ninja on Windows; the platform's default generator on Linux/macOS) and builds `masterai`, `masterai_core`, and the test binaries. Default `BuildType` is **Debug**. `-VerifyModels` runs `verify-models` after a successful build. `build.sh`'s `<Platform>` accepts `Linux-x86_64`, `Linux-arm64`, or `Darwin-arm64` (macOS). |
| `configure.ps1` / `configure.sh` | `[settings-file] [BuildType]` (`.ps1` takes `-Settings`/`-BuildType` named params); `configure.sh` also takes an optional trailing `[Platform]` | Runs the interactive first-run/update/reset configuration wizard (`masterai configure`) and writes validated settings. Requires the binary from `build` to already exist. |
| `start.ps1` / `start.sh` | `[settings-file] [BuildType] [-Foreground\|--foreground]`; `start.sh` also takes an optional trailing `[Platform]` (after `--foreground`) | Starts `masterai serve`. Runs `configure` automatically first if settings are missing (interactive terminals only). Without `-Foreground`/`--foreground`, launches detached and records the PID under `<runtime-root>/run/masterai.pid`. |
| `stop.ps1` / `stop.sh` | `[settings-file] [BuildType]`; `stop.sh` also takes an optional trailing `[Platform]` | Requests graceful shutdown of the PID recorded by `start` and waits for exit. |
| `test.ps1` / `test.sh` | `-BuildType <Debug\|Release> -Platform <Windows-x64>` &nbsp;/&nbsp; `<BuildType> <Platform>` | Runs `ctest` (the full native suite, `masterai_core_tests`) against an already-built tree. Default `BuildType` is **Debug**. |
| `diagnose.ps1` / `diagnose.sh` | `[settings-file] [BuildType]`; `diagnose.sh` also takes an optional trailing `[Platform]` | Runs `masterai security-status` against the settings' configured runtime root. |
| `rehash.ps1` (Windows only; Linux/macOS: run `masterai verify-models` directly) | `-Settings <path> -BuildType <Debug\|Release> -ModelsRoot <path>` | Hashes every file under `models-root` against its manifest and refreshes the verification cache (`models_root/.verified-cache.json`). Run after adding, replacing, or removing model files. When `-ModelsRoot` is omitted it resolves `workspace.modelsRoot` via `masterai models-root <settings>` (see above) rather than re-implementing the path resolution in PowerShell, so it always finds the same directory the server uses. Prints live `NN%` progress lines while hashing any file 256 MB or larger, instead of going silent until the whole model finishes. |
| `clean.ps1` / `clean.sh` | `-WhatIf` &nbsp;/&nbsp; `--dry-run` | Deletes only the generated `build/` tree (every platform's build output under it). Source, configuration, runtime data, and models are untouched. |
| `verify-objectives.ps1` / `verify-objectives.sh` | none | Fails if `docs/objectives.md` has changed without a corresponding reassessed and re-cached hash in `docs/objectives.sha256`. |
| `install-systemd.sh` (Linux only) | `<binary> <settings> <runtime-root> <models-root> <service-user>` | Installs and enables a hardened `masterai.service` systemd unit from fixed absolute paths. Does not build, download, create users, or modify settings. |
| `uninstall-systemd.sh` (Linux only, run as root) | none | Disables and removes the installed systemd unit. Runtime data, settings, models, backups, and credentials are preserved. |
| `install-launchd.sh` (macOS only) | `<binary> <settings> <runtime-root> <models-root> <service-user>` | Installs and bootstraps a `com.masterai.server` `launchd` daemon plist from fixed absolute paths, mirroring `install-systemd.sh`'s contract. launchd has no equivalent to systemd's kernel-level hardening keys (`ProtectSystem=strict`, `NoNewPrivileges=`, etc.) -- see [ADR-0004](architecture/ADR-0004-linux-arm64-and-macos-apple-silicon.md) for that gap. Does not build, download, create users, or modify settings. |
| `uninstall-launchd.sh` (macOS only, run as root) | none | Disables and removes the installed `launchd` plist. Runtime data, settings, models, backups, and credentials are preserved. |
