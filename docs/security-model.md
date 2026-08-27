# Security model

> Part of the [MasterAI README](../README.md). For detailed assumptions and
> threats, read [docs/security/threat-model.md](security/threat-model.md).

MasterAI treats browser input, source trees, attachments, model manifests,
model weights, downloads, inference responses, and MCP data as untrusted.

Key controls include:

- Locally stored, MasterAI-hashed password accounts as the default sign-in
  path (`allow_local_password_accounts`, on by default), so setup and login
  do not depend on an external identity provider being present.
- Optional native OS identity verification (`LogonUserW` on Windows and PAM
  on Linux) as an explicit opt-in (`allow_os_identity_accounts`, off by
  default) for operators who want accounts mapped to their OS/Windows
  identity instead of, or alongside, locally stored passwords.
- An administrator can disable a user from the Users panel to immediately
  remove their ability to authenticate, without deleting the account or its
  audit history; disabling your own account is refused server-side.
- No storage, hashing, reversible encryption, logging, or forwarding of OS
  passwords
- Immediate erasure of transient credential buffers
- Random opaque sessions stored only as hashes
- Scoped, revocable, project-bound service and IDE tokens
- Windows DPAPI and approved Linux secret-provider boundaries
- Loopback-only native listener and explicit reverse-proxy trust
- Host, origin, CSRF, rate, body-size, timeout, and concurrency enforcement
- Canonical path containment and symlink traversal rejection
- Structured process arguments without shell interpolation
- Model and executable digest pinning
- Append-oriented, sanitized, hash-chained audit records
- Bounded queues, output, attachments, downloads, and MCP responses
- Deny-by-default inbound and outbound MCP policy

MasterAI never logs passwords, raw session tokens, API keys, or private keys.
Security is not disabled merely because the service listens on localhost.

For detailed assumptions and threats, read
[docs/security/threat-model.md](security/threat-model.md).
