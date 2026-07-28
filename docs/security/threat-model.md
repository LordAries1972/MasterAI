# MasterAI Threat Model

## Protected assets

Administrator credentials, session secrets, project source, model files, configuration, audit history, inference prompts and responses, backend processes, and host compute resources.

## Trust boundaries

1. Browser or IDE client to the control-plane listener.
2. Control plane to the native MasterAI record store and filesystem state.
3. Control plane to model-runner child processes.
4. Model manifests and weights entering from untrusted sources.
5. Project and attachment content entering inference context.

## Principal threats and required controls

| Threat | Required control |
|---|---|
| Unauthenticated access | Loopback default, setup-only first run, authenticated normal routes |
| Credential attack | Native OS identity verification, fail-closed provider availability, rate limits, generic failures, no credential logging or storage |
| Credential disclosure | TLS outside loopback; transient buffers erased after verification; passwords never reach Agent-Coder, inference, MCP, logs, configuration, or command lines |
| Session theft and CSRF | Opaque random sessions, hashed storage, strict cookies, CSRF binding |
| Path traversal or symlink escape | Canonical containment checks, deny `..`, deny symlinks by default |
| SQL injection | Prepared statements only |
| Malicious model | Strict manifest parser, allowed categories, digest checks, quarantine state |
| Command injection | Structured launch arguments, no shell interpolation, executable allow-list |
| Runner escape or crash | Separate process, restricted environment, bounded restart, cancellation |
| Resource exhaustion | Request limits, model suitability gate, memory reserve, concurrency caps |
| Log injection or secret leakage | Structured sanitized fields, bounded values, secret redaction |
| Intranet exposure | TLS, authentication, host/origin allow-lists, explicit configuration |

## Phase 0 security gate

No Phase 1–4 component may silently weaken a required control. Third-party coding foundations are prohibited except for the explicitly optional, isolated `llama.cpp` backend. When a validated native operating-system security facility is unavailable, the related capability remains unavailable and reports the missing prerequisite. Windows uses `LogonUserW`; Linux uses PAM when compiled in. MasterAI never stores or reversibly encrypts an operating-system password.
