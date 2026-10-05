# Models

> Part of the [MasterAI README](../README.md).

Models are organized by category -- one workload per folder, plus a
`music` category for Text-to-Music, Voice-to-Music, and Text-to-Voice
models (download/catalog support only; MasterAI's inference runtime is
GGUF/llama.cpp-only, so a music-category model reaches Ready but has no
runner yet):

```text
models/
├── general-programming/
├── code-completion/
├── code-review/
├── debugging/
├── documentation/
├── embeddings-code-search/
├── conversation/
└── music/
```

Each installed model uses:

```text
models/<category>/<model-id>/manifest.json
```

The manifest must identify the model, category, format, backend, immutable
source revision, HTTPS source, exact file size, SHA-256 digest, license,
administrator acceptance, hardware estimates, and backend requirements.
Validation follows [models/manifest.schema.json](../models/manifest.schema.json)
and the [model taxonomy](architecture/model-taxonomy.md).

Model files are intentionally excluded from Git by `.gitignore`; only the
repository-owned directory markers and manifest schema are tracked.

Scan the model tree:

```powershell
.\build\Windows-x64\Release\masterai.exe scan-models .\models
```

```sh
./build/Linux-x86_64/Release/masterai scan-models ./models
```

A discovered file is not automatically trusted. `scan()` — used by every
inventory, chat, and download request — only ever reads a persisted
verification cache (`models_root/.verified-cache.json`); it never hashes
files itself, so a page load never blocks on multi-gigabyte SHA-256 work. Run
`verify-models` after adding, replacing, or removing files under
`models-root` to hash every discovered model against its manifest and refresh
that cache:

```powershell
.\build\Windows-x64\Release\masterai.exe verify-models .\models
# or: .\scripts\rehash.ps1 -BuildType Release
```

```sh
./build/Linux-x86_64/Release/masterai verify-models ./models
```

Verification covers size, digest, provenance, license state, path
containment, backend compatibility, and hardware suitability before
promotion to `Ready`; load time rechecks integrity again regardless of the
cache.

The current release baseline supports a separately supervised
`llama.cpp`-compatible GGUF backend. The pinned backend and a verified
programming GGUF have completed the Phase 4 end-to-end inference check;
remaining browser, download, MCP, IDE, packaging, and optional voice checks
are listed in [Project status](project-status.md).
