# Phase 8 MCP inbound: external-client operational check

Validated on 18 August 2026 against a live `masterai serve` instance
(loopback `127.0.0.1:7070`) using the **official, independent**
`@modelcontextprotocol/inspector` CLI (run via `npx`, not this codebase's
own test fixtures) as the external client, exactly as the Phase 8 exit
criteria describe.

## A real bug this check caught and fixed

The first connection attempt failed: the Inspector's `initialize` call was
rejected with `{"error":"unsupported_mcp_protocol"}`. Root cause
(`src/integration_http.cpp`, `handle_inbound_mcp`): the HTTP layer required
the `MCP-Protocol-Version` header on **every** POST, including the very
first `initialize` call. Per the Streamable HTTP transport spec, a client
cannot know the negotiated protocol version before `initialize` completes
-- negotiation happens through the JSON-RPC body's own `protocolVersion`
field (already handled correctly in `McpInboundServer::handle`,
`src/mcp.cpp`), not this header. Every native in-process test
(`test/tests.cpp`'s `test_phase_eight_mcp_inbound`) had always driven
`McpInboundServer` directly and never exercised this HTTP-layer header
check, so the bug was invisible until a real external client -- which
correctly omits the header on its first request -- tried to connect. Fixed
by only enforcing the header from the second request of a session onward
(`is_initialize_call` check in `handle_inbound_mcp`).

## Full check sequence, after the fix

1. Logged in as the local `Test` account (`POST /api/v1/auth/login`),
   obtaining a session cookie and CSRF token.
2. Minted a short-lived (30 minute), project-bound bearer token via
   `POST /api/v1/tokens`, scoped `mcp.connect` (+ `models.read`,
   `projects.read` for the tool-call step), bound to project `Test-ID` only.
3. `npx @modelcontextprotocol/inspector --cli http://127.0.0.1:7070/mcp
   --transport http --header "Authorization: Bearer <token>" --method
   tools/list` -- negotiated protocol `2025-11-25` and listed all four
   tools (`masterai.projects.list`, `masterai.project.read_file`,
   `masterai.project.search`, `masterai.models.list`).
4. `--method resources/list` -- returned exactly one resource,
   `masterai://project/Test-ID`, confirming the token's project binding is
   enforced (the other two local projects, `phase4-validation` and
   `real-model-validation`, were not listed).
5. `--method tools/call --tool-name masterai.models.list` -- real tool
   invocation succeeded and returned the live local model inventory (9
   models, verified states) once the token carried `models.read`; the same
   call correctly returned `permission_denied` when tried against a token
   scoped only `mcp.connect`, confirming per-tool scope enforcement.
6. Revoked the token via `POST /api/v1/tokens/revoke`; an identical
   `tools/list` call with the same (now-revoked) token was rejected `403
   Forbidden`, confirming revocation takes effect immediately.

## Result

A supported, independent MCP client can connect, negotiate, discover
tools/resources within its bound project, invoke an authorized tool, and
have its token revoked -- Phase 8's exit criteria are met, and the exact
protocol-negotiation bug a synthetic/in-process test could never have
caught was found and fixed by this exercise.
