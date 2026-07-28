# Phase 11 operations and recovery

This guide covers the implemented offline lifecycle boundary. Stop MasterAI
before backup, rotation, recovery, upgrade, or rollback. Each command refuses
to run while `runtime/run/masterai.pid` exists.

## systemd installation

Create the non-login service account and validated settings first. Then run:

```sh
sudo scripts/install-systemd.sh \
  /opt/masterai/bin/masterai \
  /etc/masterai/settings.json \
  /var/lib/masterai \
  /var/lib/masterai-models \
  masterai
sudo systemctl start masterai
sudo systemctl status masterai
```

The generated unit has no capabilities, enables `NoNewPrivileges`, protects
the kernel and system directories, makes home directories read-only, permits
only IPv4/Unix sockets, and grants writes only to the configured runtime and
model roots. The installer validates the unit with `systemd-analyze verify`
before publication. It does not create accounts, build code, alter settings,
or download artifacts.

Use `sudo scripts/uninstall-systemd.sh` to remove only the unit. It preserves
configuration, models, runtime records, backups, accounts, and credentials.

## Backup and clean-host restore

Create a backup while the service is stopped:

```text
masterai backup <settings>
```

The backup contains the validated settings plus runtime database, audit, and
attachment files. `manifest.tsv` records the exact size and SHA-256 of every
file, and `manifest.sha256` protects the manifest. External registered project
roots and model files are not copied. DPAPI ciphertext, Linux keyring values,
PID/control files, and other host-bound secrets are not exported.

Restore only to clean destinations:

```text
masterai restore-backup <backup-directory> <new-settings> <new-runtime>
```

Restore verifies the manifest and every file before writing a staging tree,
publishes the runtime atomically, checkpoints the recovered record store, and
updates `runtimeRoot` in the restored settings. Reinstall/verify model files and
rotate or reissue host-bound secrets separately.

The native Phase 11 test performs this clean-host exercise and also proves that
a modified backup entry is rejected.

## Secret and log rotation

```text
masterai rotate-secret <settings> <secret-alias>
masterai rotate-logs <settings> <maximum-bytes> <retained-files>
```

Secret rotation generates 256 random bits and writes only to the existing
DPAPI or Linux-keyring `SecretStore`; plaintext is not printed or audited.
Log rotation applies to operational `masterai.log` files only. The
tamper-evident audit log is never truncated or moved, preserving its hash
chain.

## Offline upgrade and rollback

Run upgrades from a separate maintenance copy of `masterai`, not the active
binary being replaced:

```text
maintenance-masterai upgrade <settings> <active> <candidate> <rollback-root>
maintenance-masterai rollback <settings> <active> <receipt.tsv>
```

Upgrade hashes both binaries, copies and verifies the previous binary, stages
the candidate beside the active path, performs an atomic replacement, verifies
the installed hash, and emits a rollback receipt. Rollback proceeds only when
the active and previous hashes still match that receipt. The HTTP service has
no executable-replacement route.

## Crash recovery

```text
masterai recover <settings>
```

Recovery removes only the known stale stop request and incomplete record
checkpoint, then opens the existing checksummed journal and writes a fresh
checkpoint. Unknown files are preserved. Normal server startup also clears a
stale stop request before listening.

After recovery, run `scripts/diagnose.ps1` or `scripts/diagnose.sh`, start the
service, and confirm `/health/ready`. A non-ready result remains a failure,
not a silently degraded success.
