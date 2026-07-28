# Visual Studio integration

Visual Studio uses the same native MasterAI executable and generic HTTP/MCP
contract as VS Code. No inference-backend-specific logic belongs in the IDE.

1. Create a scoped, project-bound IDE token.
2. Run:

   ```text
   masterai ide-token-store <settings-file> visual-studio
   masterai ide-profile <settings-file> visual-studio
   ```

3. Register the generated command as a Visual Studio external tool or configure
   an approved MCP-capable Visual Studio host to launch:

   ```text
   masterai mcp-stdio <absolute-settings-file> visual-studio
   ```

4. A Visual Studio client can also use the profile's loopback API for streamed
   chat, capabilities, deterministic diagnostics, and read-only diff preview.
   It must retrieve its credential from the native secret provider rather than
   a solution file or command argument.

The project rule permits produced code only in Assembly or strict C++17.
Modern Visual Studio VSIX packages normally require managed host glue, while a
VS Code extension requires JavaScript/TypeScript host glue. This phase therefore
implements the native C++17 bridge and installable connection contract, but it
does not mislabel noncompliant managed or JavaScript packaging as project code.
If the user later authorizes a narrow exception for host glue, that packaging
can consume this stable contract without changing MasterAI's backend.

