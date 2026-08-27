# MCP and IDE integration

> Part of the [MasterAI README](../README.md).

MasterAI pins MCP protocol revision `2025-11-25`.

Inbound MCP supports:

- Newline-delimited UTF-8 JSON-RPC over `stdio`
- Streamable HTTP at `/mcp`
- Authenticated, project-bound tools and resources
- Scope checks, cancellation, bounded reads, and native conformance tests

Outbound MCP uses a separate registry and authority boundary. It supports
restricted native `stdio` processes and loopback Streamable HTTP clients with
pinned executable digests, tool allow-lists, project/scope policy, per-call
approval, timeouts, cancellation, response limits, protected secret
references, and hash-chained audit.

Legacy MCP HTTP+SSE is disabled.

Generate protected IDE material and connection profiles with:

```text
masterai ide-token-store <settings-file> <vscode|visual-studio>
masterai ide-profile <settings-file> <vscode|visual-studio>
masterai mcp-stdio <settings-file> <vscode|visual-studio>
```

Tokens are retrieved from the native OS secret provider. They must not be
placed in command arguments, environment variables, source control, or IDE
workspace files.

See:

- [VS Code and Agent-Coder integration](ide/vscode-agent-coder.md)
- [Visual Studio integration](ide/visual-studio.md)
- [IDE integration contract](ide/integration-contract.md)

The native contracts and narrowly authorized host adapters are implemented.
The VS Code extension under `integrations/vscode` stores its token in VS Code
`SecretStorage`; the Visual Studio 2022 VSIX under `integrations/visual-studio`
uses current-user DPAPI. Both launch the native `mcp-stdio` profile without a
shell or plaintext credential and were live-validated on 5 August 2026 against
protocol `2025-11-25`, discovering four project-bound tools.
