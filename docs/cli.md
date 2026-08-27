# Command-line interface

> Part of the [MasterAI README](../README.md).

The native executable currently exposes:

```text
masterai configure [settings-file]
masterai serve [settings-file]
masterai probe [storage-root]
masterai scan-models [models-root]
masterai verify-models [models-root]
masterai download-model <settings> <url> <revision> <sha256> <category> <model-id> <filename> <display-name> <architecture> <quantization> <license-spdx-id> <size-bytes> <minimum-ram-mib> <recommended-ram-mib> --accept-license
masterai benchmark-model <settings> <model-id> <quick|standard|extended>
masterai mcp-stdio [settings-file] [vscode|visual-studio]
masterai ide-token-store <settings-file> <vscode|visual-studio>
masterai ide-profile <settings-file> <vscode|visual-studio>
masterai backup <settings-file>
masterai restore-backup <backup> <settings-destination> <runtime-destination>
masterai rotate-secret <settings> <secret-alias>
masterai rotate-logs <settings> <maximum-bytes> <retained-files>
masterai recover <settings>
masterai runtime-root <settings>
masterai models-root <settings>
masterai upgrade <settings> <active> <candidate> <rollback-root>
masterai rollback <settings> <active> <receipt>
masterai performance-probe [iterations]
masterai index-probe <project-root> [index-root]
masterai calibrate <settings> <model-id> <auto|minimal|balanced|performance>
masterai speculative-benchmark <settings> <target-model-id> <draft-model-id> <quick|standard|extended>
masterai security-status [runtime-root]
```

Download sources require approved immutable HTTPS URLs, explicit license
acceptance, an immutable revision, exact size policy, and SHA-256 verification.
Failed integrity checks are quarantined rather than promoted.

`runtime-root <settings>` and `models-root <settings>` print the workspace's
resolved, absolute `runtimeRoot`/`modelsRoot` directories. A relative path in
`settings.json` resolves against **the directory holding that settings file**,
not the process's current working directory or the project root — every
lifecycle script (`start.ps1`, `stop.ps1`, `diagnose.ps1`, `rehash.ps1`) asks
the binary for these instead of re-deriving the path itself, so they always
agree with what the running server actually uses.
