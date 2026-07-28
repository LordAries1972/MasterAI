# Phase 11 operational hardening

MasterAI lifecycle commands are offline administrative operations. They do not
run through the browser API, accept shell fragments, elevate privileges, or
follow symlinks.

Security boundaries:

- Backups are allow-listed to settings, database, audit, and attachments.
- Every restored entry is contained beneath the backup root and verified by
  exact size and SHA-256 before any destination is published.
- OS-bound secrets are excluded from backup and must be rotated or reissued.
- Operational logs rotate separately from the append-only audit hash chain.
- Upgrade and rollback require regular non-symlink files and hash-bound
  receipts; modified active or rollback binaries fail closed.
- Recovery deletes only named crash residue and delegates journal validation to
  the existing `RecordStore`.
- The systemd unit runs without capabilities, with protected system/kernel
  surfaces and explicit writable paths.

Operators should protect settings and backup directories with permissions
limited to the service administrator, keep rollback media on the same trusted
host, and test restore into a clean location before retiring older backups.
