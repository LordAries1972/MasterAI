# MasterAI IDE integration contract

## Purpose

MasterAI exposes one backend-neutral contract to VS Code/Agent-Coder and Visual
Studio. IDE-side code never selects or launches a model backend directly.

## Authentication

Create a scoped API token through the authenticated
`POST /api/v1/tokens` route with:

- `ide.connect`
- `mcp.connect`
- `projects.read`
- any additional chat/model scopes the user deliberately needs
- an explicit `projects` array

Install the token without echoing it or writing it to a workspace file:

```text
masterai ide-token-store <settings-file> <vscode|visual-studio>
```

MasterAI validates both required scopes, the active mapped user, expiry, and at
least one project binding before storing the token in DPAPI on Windows or the
native Linux secret provider. `masterai mcp-stdio` reads that OS-protected
alias; profiles and command arguments contain no credential.

## Connection profile

Generate a secret-free profile:

```text
masterai ide-profile <settings-file> <vscode|visual-studio>
```

The profile identifies:

- the exact `masterai` executable and settings file;
- the `mcp-stdio` command arguments;
- the loopback API and Streamable HTTP MCP addresses;
- the OS-secret alias;
- chat, context, diagnostics, diff-preview, and cancellation capabilities.

## Shared operations

| Capability | Contract | Mutation |
|---|---|---|
| Chat | Existing `/api/v1/chats` and message streaming routes | Persists chat messages; model generation cancels on disconnect |
| Context | MCP `2025-11-25` tools/resources at `/mcp` or `mcp-stdio` | Read-only and token/project bound |
| Diagnostics | `POST /api/v1/ide/diagnostics` with `projectId` and `path` | Read-only; bounded UTF-8 source scan |
| Diff preview | `POST /api/v1/ide/diff-preview` with `projectId` and `unifiedDiff` | Never applies changes |
| Capabilities | `GET /api/v1/ide/capabilities` | Read-only |
| Cancellation | HTTP disconnect and MCP cancellation notification | Stops associated work where cancellable |

Diagnostics currently report the missing source-unit explanation required by
the project rule, unfinished markers, trailing whitespace, and lines over 160
bytes. They are deterministic local checks, not compiler or model claims.

Diff preview accepts bounded UTF-8 unified diffs only, checks every path against
the registered project, rejects symlinks, traversal and binary patches, and
returns file/hunk/addition/deletion counts. It never writes a source file.

