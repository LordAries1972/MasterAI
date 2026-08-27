# Persistence, operations, and recovery

> Part of the [MasterAI README](../README.md).

MasterAI uses an internally authored bounded record store with explicit schema
versions, checksummed journals, atomic replacement, recovery scanning,
checkpointing, and replaceable repository interfaces.

Runtime state is kept separate from source and model files and can include:

```text
runtime/
├── projects/
├── attachments/
├── indexes/
├── cache/
├── downloads/
├── benchmarks/
├── logs/
├── run/
└── backups/
```

Implemented operational controls include:

- Offline backup and clean-destination restore
- Manifest and integrity validation
- OS-protected secret rotation
- Bounded log rotation
- Crash-residue recovery
- Hash-bound executable upgrade and rollback
- Graceful stop requests and bounded drain
- Windows lifecycle helpers
- Hardened systemd install/start/stop/uninstall helpers

See the [operations documentation](operations/) for current procedures and
validation boundaries.
