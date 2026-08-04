# Visual Studio integration

Visual Studio uses the same native MasterAI executable and generic HTTP/MCP
contract as VS Code. No inference-backend-specific logic belongs in the IDE.

1. Create a scoped, project-bound IDE token.
2. Run:

   ```text
   masterai ide-token-store <settings-file> visual-studio
   masterai ide-profile <settings-file> visual-studio
   ```

3. Build `integrations/visual-studio/MasterAI.VisualStudio.csproj` with the
   Visual Studio 2022 SDK and install the generated `MasterAI.VisualStudio.vsix`.
   The VSIX exposes chat, token storage, connection validation, and cancellation
   commands on the **Tools** menu. It launches:

   ```text
   masterai mcp-stdio <absolute-settings-file> visual-studio
   ```

4. The Visual Studio client can also use the profile's loopback API for streamed
   chat, capabilities, deterministic diagnostics, and read-only diff preview.
   It must retrieve its credential from the native secret provider rather than
   a solution file or command argument.

The managed code is a narrowly authorized host adapter isolated under
`integrations/visual-studio`; inference, policy, storage, validation, and MCP
authority remain in native C++17. The installed Visual Studio 2022 Enterprise
host was validated on 5 August 2026 against MCP `2025-11-25` with four
project-bound tools.
