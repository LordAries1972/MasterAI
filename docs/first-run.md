# First run

> Part of the [MasterAI README](../README.md).

This walks through everything needed between "I just built MasterAI" and "I
sent a chat message and got a reply." Skipping a step is the most common
cause of the two errors new installs hit first:
`inference_backend_not_configured` (no runner executable configured) and
`model_not_ready` (no model has passed verification yet). Do the steps in
order.

> [!NOTE]
> **Where to get `llama-server` / `llama-server.exe`.** MasterAI supervises
> this executable as a separate, process-isolated runner — it does not
> vendor, bundle, or build it. Prebuilt archives (CPU, CUDA, Vulkan, and HIP
> builds, for Windows, Linux, and macOS) are published on the upstream
> `llama.cpp` project's own GitHub Releases page:
> https://github.com/ggml-org/llama.cpp/releases. Download the archive
> matching your OS and backend, extract it anywhere, and note the full path
> to `llama-server.exe` (Windows) or `llama-server` (Linux/macOS) — you'll
> need it in step 2 below and again when the configuration wizard in step 3
> asks for the inference backend path. Building it yourself from the same
> repository's source is also supported, and is the only way to pin an
> exact revision (see [ADR-0003](architecture/ADR-0003-release-1-product-baseline.md)
> for the revision MasterAI's own Phase 4 validation was pinned against).

> [!NOTE]
> **Real LLM LoRA fine-tuning (formerly Phase 73) has been removed.** It
> shelled out to `llama.cpp`'s `finetune`/`export-lora` example tools via a
> `FineTuningJob` whose method started with `llm:` (e.g. `llm:code
> assistant`). Upstream `llama.cpp` removed those tools on 2024-07-25 (PR
> #8669); no release since then ships the binaries, so the feature could
> never actually run against a modern `llama.cpp` build. The only way to get
> compatible binaries would be building `llama.cpp` from a commit before
> that date, which also predates model architectures added afterward (e.g.
> Qwen3, from 2025) and so cannot load a base GGUF using one of those newer
> architectures at all — permanently unworkable for this codebase's own
> models, so the executor, its `AppConfig` fields
> (`llama_finetune_executable`/`llama_export_lora_executable`), its HTTP
> routes, and its web UI were deleted rather than left as dead code. Machine
> Learning's Fine-Tuning page still runs Phase 70's real tabular warm-start
> fine-tuning (continuing gradient descent from an existing trained model's
> weights), which needs none of this and is unaffected.

## 1. Build the binary

See [Building from source](building.md) if you have not already. The rest of
this section assumes `.\build\Windows-x64\Release\masterai.exe` (or the Linux
equivalent) exists.

## 2. Get an inference backend and a model onto the machine

Chat needs two things this repository does not provide: a `llama.cpp`
server executable and at least one GGUF model with a valid manifest.

- Build or download a version-pinned `llama.cpp` server executable
  (`llama-server` / `llama-server.exe`) and note its full path — see the
  note above for where to get one. MasterAI supervises it as a separate
  process; it does not vendor or build it. A convenient (not required)
  place to keep it is `tools/llama.cpp/` at the repository root — already
  covered by `.gitignore`'s `*.exe`/`*.dll` patterns, so it never gets
  committed.
- Place a GGUF model file under `models/<category>/<model-id>/` (see
  [Models](models.md) for the category list) with a
  `manifest.json` next to it that validates against
  [models/manifest.schema.json](../models/manifest.schema.json). The manifest
  must declare the model's exact size, SHA-256 digest, license, and
  approved source (Hugging Face, GitHub releases, or ModelScope) — or, for a
  self-produced model (see below), an internal `masterai-*:` marker instead.
- Optional: an approved `curl` executable, only if you want MasterAI to
  manage model downloads itself (`masterai download-model`) instead of
  placing files manually.
- Already have a GGUF on local disk instead — your own fine-tune, a
  conversion, or a file you trust? `POST /api/v1/model-imports` (or the web
  UI's **Import a local model** form on `/app/models/download`) copies it
  into the catalog and writes a correct manifest for you, computing the real
  SHA-256 and marking it Ready immediately — no manual manifest editing and
  no separate `verify-models` pass needed. See
  `docs/HowToUse-MachineLearning.md` Section 11 for the guided walkthrough.

## 3. Run the configuration wizard

```powershell
.\scripts\configure.ps1 -BuildType Release
```

```sh
sh ./scripts/configure.sh ./config/settings.json Release
```

The wizard writes `config/settings.json` and asks for, in order: the
loopback port, the runtime data directory, the model directory, the
**approved `llama.cpp` server executable path from step 2** (leaving this
blank disables inference and every chat request will fail with
`inference_backend_not_configured`), an approved `curl` executable (blank
disables downloads), and whether to allow locally stored password accounts
and/or OS-integrated sign-in. Re-running the wizard against an existing
file offers **U**pdate, **R**eset, or **C**ancel.

## 4. Verify the model

MasterAI never hashes multi-gigabyte model files on a page load or chat
request — it only trusts a persisted verification cache. Build that cache
(or refresh it any time you add, replace, or remove a file under
`models-root`) before a model can reach `Ready` state:

```powershell
.\build\Windows-x64\Release\masterai.exe verify-models .\models
# or: .\scripts\rehash.ps1 -BuildType Release
```

```sh
./build/Linux-x86_64/Release/masterai verify-models ./models
```

Large model files print `NN%` progress lines while they hash (every 10%, for
files 256 MB or larger) instead of leaving the command silent until the whole
file finishes — helpful when verifying several multi-gigabyte GGUF files back
to back.

If this step is skipped, chat creation fails with `model_not_ready` even
though the model file is present.

## 5. Start the service

```powershell
.\scripts\start.ps1 -BuildType Release -Foreground
```

```sh
sh ./scripts/start.sh ./config/settings.json Release --foreground
```

To run in the background instead, drop `-Foreground` / `--foreground`:

```powershell
.\scripts\start.ps1 -BuildType Release
```

```sh
sh ./scripts/start.sh ./config/settings.json Release
```

`start` runs the configuration wizard automatically (interactively only) if
`config/settings.json` does not exist yet, so steps 3 and 5 can be combined
on a first run if you prefer.

On startup, before any administrator account exists, MasterAI logs a
one-time setup token at warning level, e.g.:

```text
One-time first-administrator token: 3f9c1a...
```

Keep this token; you need it in the next step and it is not shown again
(it is not persisted anywhere the process can hand back out).

## 6. Create the first administrator and sign in

Open the default local endpoint in a browser:

```text
http://127.0.0.1:7070
```

The page detects that no administrator exists yet and shows a setup form
instead of a login form. Paste the setup token from step 5, choose a
username and password (or complete OS-identity setup if
`allow_os_identity_accounts` was enabled), and submit. You are then signed
in as the first administrator.

## 7. Create a project (optional) and chat

From `/app/projects`, register a project pointing at a local source
directory if you want project-aware context; this is optional — chat also
works with no project attached. From `/app/chat`, pick the verified model
from step 4 and send a message.

To save a detail for future conversations, send a message containing only:

```text
save to memory: I prefer concise answers with C++17 examples.
```

MasterAI confirms the save directly without running inference. Expand
**Chats → Memory** in the sidebar to inspect, add, or forget saved details.
Automatic captures are labelled in their tooltip and use only a small,
fixed set of self-disclosure phrases; the model does not decide what is
persisted.

## Health checks

```text
GET /health/live
GET /health/ready
```

`live` indicates that the process is running. `ready` stays unavailable
until the first administrator exists and the configured sign-in path
(local password and/or OS identity) is usable — it does not mean a model
is loaded or verified.

## Stopping and diagnostics

Stop a background service:

```powershell
.\scripts\stop.ps1 -BuildType Release
```

```sh
sh ./scripts/stop.sh ./config/settings.json Release
```

Run native security and hardware diagnostics:

```powershell
.\scripts\diagnose.ps1 -BuildType Release
```

```sh
sh ./scripts/diagnose.sh ./config/settings.json Release
```

## Certificate material for a future SSH transport

`scripts/generate-ssh-certificates.ps1` generates a long-lived self-signed
TLS certificate plus an `ed25519` SSH host key pair under
`config/certificates/` (already excluded from version control, both via
the existing `/config/` `.gitignore` rule and explicit `*.pem`/`*.key`/
`*_host_key*` entries). This is preparation only -- no SSH inbound
transport exists yet; MCP inbound currently supports `stdio` and
Streamable HTTP (see [Command-line interface](cli.md) and MCP inbound in
[docs/PLAN.md](PLAN.md)). Subject fields default to `Ultimanium
Designs` / `Melbourne, Victoria, AU` and can be overridden:

```powershell
.\scripts\generate-ssh-certificates.ps1 -Organization "Your Org" -Locality "Your City" -ValidityDays 3650
```
