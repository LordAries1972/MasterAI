# MasterAI for Visual Studio

This VSIX is thin host glue over MasterAI's generic loopback API. It provides a
chat tool window, DPAPI-protected scoped-token storage, connection validation,
and cancellation. It contains no inference-backend, authorization, or
persistence logic. `MASTERAI_IDE_URL` may select a non-default loopback port;
non-loopback URLs are rejected.

For native MCP validation, set non-secret `MASTERAI_EXE` and
`MASTERAI_SETTINGS`, install the project-bound token with `masterai
ide-token-store ... visual-studio`, and start Visual Studio with
`MASTERAI_VALIDATE_ON_STARTUP=1`. The package records only protocol/tool-count
evidence under `%LOCALAPPDATA%\MasterAI`.
