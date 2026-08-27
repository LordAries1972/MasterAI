# Web application and API

> Part of the [MasterAI README](../README.md).

The native service provides browser workflows for:

- First-administrator setup and login
- Project and auto-titled chat management, with each workspace section
  (`/app/chat`, `/app/projects`, `/app/models/inventory`,
  `/app/models/download`, `/app/models/benchmarks`,
  `/app/settings/api-reference`, `/app/admin/create`, `/app/admin/users`)
  served at its own URL and gated by role
- An "API Reference" settings page (`/app/settings/api-reference`)
  documenting every HTTP/MCP endpoint an external system can call against
  this instance -- chat/completions, models/system information, and MCP
  inbound/outbound integration -- including how a system with no
  interactive login authenticates (a `POST /api/v1/tokens` Bearer API
  token, scoped independently of any person's own password)
- Ready-model selection and loading, with best-effort background model
  pre-warming triggered the moment a chat is opened, created, or its model
  is switched — instead of only starting the cold load once the first
  message is sent — so the runner has a head start on the wait. Concurrent
  first-message loads wait on that same bounded operation, and duplicate
  background warm requests are coalesced
- Incrementally streamed responses and cancellation, rendered at most once per
  painted frame so a fast-streaming reply never stalls the page
- Persistent HTTP keep-alive connections for ordinary (non-streaming) API
  calls, so chat polling and background status requests do not exhaust the
  browser's per-origin connection limit while a streamed reply is open
- A "Copy" and "Save" button on every code block in a reply, letting the
  generated snippet be copied or saved to a real file independent of any
  project binding or tool call
- Model-independent, per-user remembered chat details with explicit
  `save to memory:` commands, bounded automatic capture, later-turn recall,
  and an inspect/add/forget sidebar
- Bounded UTF-8 source/text attachments
- Model inventory and suitability, including a cached verification state so
  page loads never block on hashing model files
- Download jobs and progress, with pause, resume, cancel, and remove controls
- Benchmarks and recommendations
- Resource, memory, request, and indexing status
- Administrator-only Machine Learning dashboard and lifecycle-management pages
- Administrative settings and operations

HTTP APIs are versioned under `/api/v1/`. Implemented capability areas include
authentication, users, projects, chats, user memories (`GET/POST
/api/v1/memories` and `POST /api/v1/memories/{id}/delete`), models, downloads (including
`.../model-downloads/{id}/pause`, `.../cancel`, and `.../remove`), local
model import (`POST /api/v1/model-imports`), benchmarks, resources, memory,
request metrics, project indexes, MCP integrations, IDE connections, and
administrator-only Machine Learning records under `/api/v1/ml/*` (including
`POST /api/v1/ml/models/{id}/source` to correct a Model Registry entry's
source file path in place).

Two additional routes exist specifically so a third-party client (e.g. the
separate Agent-Coder VS Code extension) can use a running MasterAI instance
as a local LLM backend without learning a MasterAI-specific request shape:

- `GET /api/v1/model-catalog` -- the same curated, size-tiered GGUF download
  suggestions the Settings -> Models -> Download page's picker offers,
  as `{"models":[...]}`, sorted by `displayName` ascending
  (case-insensitive). Requires the same `models.read` bearer scope as
  `GET /api/v1/models`.
- `GET /api/v1/models` -- the locally scanned/verified inventory of models
  actually on disk, also sorted by `displayName` ascending
  (case-insensitive) and including each model's `quantization` string (e.g.
  `Q4_K_M`). A model directory whose manifest fails to parse still appears
  (state `invalid`, with a diagnostic) but is never returned with a blank
  name -- it falls back to its directory name so it can be identified and
  fixed rather than showing up as an empty row.
- `POST /v1/chat/completions` -- a stateless, non-streaming, OpenAI Chat
  Completions-shaped endpoint (top-level, not under `/api/v1`, to mirror
  OpenAI's own URL shape). Body: `{"model":"...","messages":[{"role":
  "system"|"user"|"assistant","content":"..."}]}` (any other OpenAI field,
  including `stream`, is accepted and ignored). Returns the standard
  `chat.completion` envelope, `404` for an unknown model id, or `503` if the
  model exists but is not yet downloaded/verified. Requires the same
  `chats.write` bearer scope as `POST /api/v1/chats`. Distinct from the
  per-`InferenceEndpoint` `/v1/completions` listener each configured
  Inference Endpoint opens on its own port -- that is a separate feature
  with a narrower (`{"prompt":"..."}`) request shape.

By default MasterAI listens on `127.0.0.1:7070` (see `config/settings.json`'s
`server.host`/`server.port`).

An operator can also disable authentication entirely (`auth.enabled:false`
in `settings.json`, or the "Require sign-in" toggle in Settings ->
Administration) so every request -- including the two routes above -- is
served with no login, session, or bearer token at all. This is only ever
permitted while `server.host` is loopback (`127.0.0.1`/`localhost`/`::1`);
MasterAI refuses to start with authentication disabled on any other host, so
a misconfigured instance can never be exposed to the network unauthenticated.

All HTTP endpoints serve over persistent (keep-alive) connections by
default: a connection stays open across multiple ordinary requests (up to a
100-request cap and a 5-second idle timeout) unless the client sends
`Connection: close` or uses HTTP/1.0 without requesting keep-alive. A
streaming chat-completion response is the one exception — it holds the
socket for the whole generation and always closes the connection when the
stream ends.

Inputs are bounded and strictly parsed. Administrative mutations require the
applicable identity, role, scope, project binding, host/origin checks, CSRF
protection, and audit record.
