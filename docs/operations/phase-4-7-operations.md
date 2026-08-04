# Phase 4–7 Operations

This guide covers the inference, browser chat, secure download, and benchmark
surfaces introduced in phases 4 through 7.

## Configuration

Run `masterai configure <settings-file>` and provide absolute paths to the
approved executables when prompted:

- `inference.llamaServerExecutable` is the pinned `llama.cpp` server binary.
- `inference.runnerPort` is a loopback-only IPC port distinct from the MasterAI
  HTTP port. The default is `7081`.
- `downloads.curlExecutable` is the approved curl binary used only as an HTTPS
  transfer adapter.

Leaving either executable blank disables that optional subsystem. Existing
version-1 configuration files without these two sections remain valid and use
the disabled defaults.

MasterAI rechecks that configured executables are regular, non-symlink files.
Model size and SHA-256 are rechecked immediately before every runner launch.

## Inference lifecycle

The runner supervisor:

1. Builds a structured `llama.cpp` launch specification without a shell.
2. Starts the runner in the model directory with stdout and stderr redirected
   to `runtime/logs/runner-<model-id>.log`.
3. Polls the loopback `/health` endpoint until the bounded startup deadline.
4. Uses loopback HTTP IPC for tokenize and completion operations.
5. Dispatches streamed completion records as they arrive.
6. Treats a disconnected browser stream as cancellation and closes runner IPC.
7. Records process ID, resident memory, completed requests, cancellations, and
   failure diagnostics.
8. Stops the child process during unload or MasterAI shutdown.

Authenticated model load and unload routes are:

```text
POST /api/v1/models/{id}/load
POST /api/v1/models/{id}/unload
```

The load body is `{"contextLength":4096}`. A chat automatically loads its
selected Ready model when necessary.

## Browser programming chat

Open `/`, sign in with a mapped operating-system identity, and continue to
`/app`. The native browser application can create projects and chats, select a
Ready model, store bounded UTF-8 text attachments, send messages, display token
output incrementally, and cancel by aborting the response stream.

Projects, chats, messages, attachment metadata, and attachment digests are
stored in the MasterAI record journal. Attachment content is stored below
`runtime/attachments/<owner>/<project>/`; ownership, project association,
path containment, size, UTF-8 validity, and SHA-256 are checked again when the
content is assembled into a prompt.

The message endpoint returns newline-delimited JSON over HTTP chunked transfer:

```text
POST /api/v1/chats/{id}/messages
Content-Type: application/json

{"content":"Review this function","attachmentIds":["<id>"]}
```

Each token record has `type: "token"`. The terminal record has
`type: "complete"` plus prompt tokens, generated tokens, elapsed microseconds,
and cancellation state.

## Secure downloads

Downloads accept only these immutable HTTPS URL forms:

- `https://huggingface.co/.../resolve/<revision>/<file>`
- `https://github.com/.../releases/download/<revision>/<file>`

The immutable revision must occur in the source path. A request also requires
an expected lowercase SHA-256, explicit license acceptance, an approved model
category, a model ID, and minimum/recommended RAM values. MasterAI rejects a
request that is unsuitable after the configured memory reserve.

Partial files use a `.part` suffix and curl receives `--continue-at -`,
`--proto =https`, TLS 1.2 minimum, redirect handling, and fail-on-HTTP-error
arguments without shell evaluation. Progress is journaled as the partial file
grows. A matching digest atomically promotes the artifact into
`models/<category>/<model-id>/`; a mismatch moves it to a `.quarantine` file.
The normal model registry then applies manifest, provenance, licensing,
hardware, and integrity checks before the model can become Ready.

CLI usage:

```text
masterai download-model <settings> <url> <revision> <sha256> \
  <category> <model-id> <filename> --accept-license
```

The corresponding authenticated APIs are:

```text
GET  /api/v1/model-downloads
POST /api/v1/model-downloads
POST /api/v1/model-downloads/{id}/run
```

## Reproducible benchmarks

Quick, standard, and extended profiles run 2, 5, and 10 fixed programming
cases. Every result records:

- model, backend version, MasterAI build, and hardware identity;
- profile and exact prompt-suite SHA-256;
- deterministic generation settings;
- prompt/generated tokens and elapsed microseconds;
- peak runner resident memory;
- passed and total quality cases.

Only records with the same hardware identity, prompt-suite hash, and profile
are comparable. Recommendation selection ranks compatible results by quality
first and generation rate second.

CLI usage:

```text
masterai benchmark-model <settings> <model-id> <quick|standard|extended>
```

Authenticated APIs are:

```text
GET  /api/v1/benchmarks
POST /api/v1/benchmarks
POST /api/v1/benchmarks/recommend
```

Benchmark records are persisted in the MasterAI journal and appear in the
browser workspace comparison panel.

## Validation boundary

The repository test fixture launches a separate fake llama-compatible process
and validates readiness, tokenization, streamed generation, metrics, and
unload. It also validates durable phase-5 records, attachment ownership and
integrity, resumable download-job restoration and quarantine policy, and
benchmark compatibility, persistence, and recommendation ordering.

That fixture proves MasterAI orchestration behavior without claiming real-model
quality or performance. The pinned `llama.cpp`/verified-programming-GGUF flow,
same-host model comparison, and deliberately interrupted immutable HTTPS resume
have current operational evidence in `docs/PLAN.md`. Repeat them on each target
release host when backend, model, transport, or platform identity changes.
