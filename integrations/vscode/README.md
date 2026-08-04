# MasterAI for Visual Studio Code

This dependency-free host adapter uses the generic MasterAI HTTP contract. It
stores the scoped bearer token in VS Code `SecretStorage`, restricts the server
address to loopback, publishes deterministic diagnostics, previews unified
diffs without applying them, opens the native chat UI, and cancels active API
requests. It contains no model or inference-backend logic.

For Agent-Coder MCP context and tools, retain the native secret-free
`masterai ide-profile ... vscode` profile described in
`docs/ide/vscode-agent-coder.md`.

Set `masterai.executable` and `masterai.settingsFile`, then run **MasterAI:
Validate Native MCP Profile** to negotiate MCP and confirm tool discovery
through the OS-protected native token alias.
