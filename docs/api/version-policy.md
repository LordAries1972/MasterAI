# MasterAI API and MCP Version Policy

- HTTP APIs use an explicit `/api/v1/` prefix.
- Breaking API changes require a new major path.
- Additive response fields are permitted within a major version.
- Unknown request fields are rejected unless explicitly contained in a documented extension object.
- MCP support is pinned to stable protocol revision `2025-11-25`.
- Streamable HTTP requires `MCP-Protocol-Version: 2025-11-25` after
  initialization; missing/unsupported revisions fail closed as defined by the
  pinned specification.
- Legacy SSE, if introduced, remains disabled by default and isolated from Streamable HTTP.
- Version negotiation failures are fail-closed and return a bounded diagnostic.
