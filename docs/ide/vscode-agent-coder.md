# VS Code and Agent-Coder integration

MasterAI uses Agent-Coder's real outbound MCP client rather than introducing a
second VS Code reasoning or approval path.

1. Start the configured MasterAI server.
2. Create a token with `ide.connect`, `mcp.connect`, `projects.read`, required
   chat/model scopes, and explicit project bindings.
3. Run:

   ```text
   masterai ide-token-store <settings-file> vscode
   masterai ide-profile <settings-file> vscode
   ```

4. In VS Code, run **Agent-Coder: Open Connected AI Platforms** or `/platforms`.
5. In **Approved outbound MCP servers**, choose `stdio`.
6. Copy the generated profile's exact `command`. Enter these arguments
   separately, with no shell joining:

   ```text
   mcp-stdio
   <absolute-settings-file>
   vscode
   ```

7. Use the executable directory as the working directory. Keep restricted trust
   and the current-workspace binding, then select only the MasterAI tools and
   resources needed for the task.
8. Choose **Save & Start**, confirm discovery succeeds, and review
   `.agent-coder/integration-policy.json` plus
   `.agent-coder/platform-audit.jsonl`.

Do not put a token in Agent-Coder arguments, environment values, headers, or the
workspace. The stdio process reads the validated token from the native
OS-protected alias.

An MCP argument such as `approved: true` requests an operation; it does not
approve itself. Agent-Coder's authenticated prompt and policy still apply, and
MasterAI independently enforces its registry, allow-list, project, scope, and
per-call approval boundary.

This implementation session prepared and validated the MasterAI side and
confirmed Agent-Coder's MCP client and live validation settings are enabled. An
interactive VS Code user must perform **Save & Start** because this API session
cannot operate the extension UI.

