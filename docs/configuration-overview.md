# Configuration

> Part of the [MasterAI README](../README.md).

For a complete, field-by-field explanation of every `config/settings.json`
entry — what it does, its default, and its enforced policy ceiling — see
[docs/architecture/configuration.md](architecture/configuration.md).
The summary below covers precedence and environment overrides only.

Configuration precedence is:

1. Compiled safe defaults
2. Persisted settings
3. Approved environment variables
4. Command-line overrides

Supported environment overrides currently include:

```text
MASTERAI_HOST
MASTERAI_PORT
MASTERAI_ALLOW_INTRANET
MASTERAI_RUNTIME_ROOT
MASTERAI_MODELS_ROOT
MASTERAI_LLAMA_SERVER
MASTERAI_CURL
MASTERAI_PARQUET_HELPER
```

These approved overrides are resolved consistently by the service and every
settings-backed operational CLI command.

Unknown fields are rejected unless they belong to an explicitly approved
extension namespace. Changes are written through temporary files and atomic
replacement.

The default listener is loopback-only. Release 1 intranet access uses an
administrator-managed same-host reverse proxy with trusted TLS, while MasterAI
continues to enforce authentication, host/origin policy, CSRF controls, and
request limits. Direct plaintext non-loopback startup is prohibited.
