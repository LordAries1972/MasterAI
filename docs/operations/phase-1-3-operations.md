# MasterAI Phase 1–3 operations

## Configure

Build first, then run `scripts/configure.ps1` on Windows or
`scripts/configure.sh` on Linux. The wizard creates a validated schema-version
1 settings file through a checked temporary file and atomic replacement.
Existing settings can be updated or reset; reset requires typing `RESET` and
preserves runtime records, models, and backups. The prior settings file is
retained as `.bak`.

Configuration precedence, lowest to highest, is safe compiled defaults,
settings JSON, the five documented `MASTERAI_*` environment values, then
explicit command overrides. Unknown fields and unknown overrides are errors.

## Start, stop, and diagnose

- `scripts/start.ps1` / `scripts/start.sh` runs the wizard only from an
  interactive terminal. Non-interactive startup fails clearly if settings are
  absent.
- `scripts/stop.ps1` / `scripts/stop.sh` creates the runtime stop request and
  waits up to 30 seconds for graceful shutdown.
- `scripts/diagnose.ps1` / `scripts/diagnose.sh` checks native identity,
  non-password OS secret storage, hardware, disk, CPU features, and GPU backend
  libraries.
- `masterai backup <settings>` checkpoints the checksummed journal and copies a
  consistent snapshot plus journal into the configured backup directory.

## First administrator and authentication

On an empty record store the process logs a random one-time setup token. Send
it with the administrator's OS username, transient password, and display name
to `POST /api/v1/setup` over loopback. The password is consumed by the native
OS provider and erased; only the OS principal mapping is persisted.

Login, logout, rotation, and current-user routes are:

- `POST /api/v1/auth/login`
- `POST /api/v1/auth/logout`
- `POST /api/v1/auth/refresh`
- `GET /api/v1/users/me`

Browser sessions use opaque `HttpOnly`, `SameSite=Strict` cookies and a
separate CSRF token. IDE, Agent-Coder, and MCP clients create scoped,
time-bounded tokens with `POST /api/v1/tokens`, use
`Authorization: Bearer <token>`, and revoke them with
`POST /api/v1/tokens/revoke`. OS passwords must never be sent to those clients,
model processes, or MCP servers.

## Model inventory

`GET /api/v1/models` returns authenticated JSON inventory.
`GET /models` renders the authenticated native inventory page. A model reaches
Ready only after strict manifest, source/revision, accepted license, exact
size, SHA-256, CPU/GPU feature, and memory suitability checks. The adapter
rechecks size and SHA-256 immediately before building the actual load command.

## Network boundary

The native release-1 listener accepts only `127.0.0.1`. Intranet TLS is
terminated by an administrator-managed same-host reverse proxy using an
organisation/private-PKI or publicly trusted certificate. Direct intranet
binding is rejected even if settings claim TLS; this prevents accidental
plaintext exposure before a separately validated native TLS provider exists.
