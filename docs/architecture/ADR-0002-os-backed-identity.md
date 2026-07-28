# ADR-0002: Native Operating-System Identity

Status: Accepted

Date: 2026-07-28

## Decision

Interactive username-and-password authentication delegates verification to the
host operating system. MasterAI does not implement a password hashing scheme,
does not maintain a password database, and does not reversibly encrypt
passwords.

- Windows uses `LogonUserW` with the network logon type and closes the returned
  token immediately after verification.
- Linux uses PAM when its development interface is available at build time. A
  Linux build without PAM remains fail-closed.
- The mutable password input and native converted buffer are erased immediately
  after the operating-system call.
- Passwords are never logged, persisted, included in command-line arguments, or
  forwarded to an inference model, Agent-Coder, MCP server, or model backend.
- Browser-to-MasterAI credentials require TLS whenever traffic can leave
  loopback. The operating system performs identity verification; MasterAI does
  not decrypt a stored password.
- Non-password application secrets use Windows Credential Manager, a supported
  Linux secret service or keyring, or a separately protected encrypted secrets
  file. That encryption boundary is distinct from password authentication.

## Consequences

The authenticated operating-system principal is mapped to MasterAI roles and
project permissions. MasterAI sessions remain opaque, random, hashed at rest,
rotatable, expiring, and revocable. IDE and service clients use scoped,
revocable tokens rather than retaining a user's operating-system password.

An installation cannot become ready for authenticated use when its native
identity provider is unavailable. Authentication errors remain generic to
avoid account enumeration.
