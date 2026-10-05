# Free model sourcing for MasterAI's download page

*Research pass, August 2026 — what free/open model sources exist, which ones the current download pipeline can actually reach, and which specific models are worth adding.*

## How the download page works today

Before picking sources, it matters what the pipeline can physically do. `DownloadManager` (`src/downloads.cpp`) shells out to `curl` with a fixed, narrow contract:

- **HTTPS GET only**, TLS 1.2+, no custom auth headers of any kind.
- The **source URL must embed an immutable revision** — `models.cpp` rejects any request where `source_url` doesn't contain `immutable_revision` (a commit hash), so "latest" links are refused by design.
- The URL's **host must be on an allow-list**: `huggingface.co`, `github.com`, or `modelscope.cn` (`models.cpp:213`).
- Every download is verified against a **pinned SHA-256** before it's accepted.
- The manifest's `licenseSpdx` must be in a **fixed allow-list**: `Apache-2.0, MIT, BSD-2-Clause, BSD-3-Clause, CC-BY-4.0, CC-BY-NC-4.0, Llama-3.1, Llama-3.2, Gemma`.

That combination — no auth headers, revision-pinned URLs, SHA-256 pinning — is a deliberate security posture (immutable, verifiable, no credentials held by the server). It also silently rules out entire categories of "free" sources. That filter did most of the work in this research: a source is only actually useful here if a script can `curl` a fixed URL and get bytes back with no login step.

## Sources evaluated

| Source | Reachable today? | Why |
|---|---|---|
| **Hugging Face** (`huggingface.co`) | Yes — already the primary source | Public repos serve versioned files at `/{repo}/resolve/{commit}/{file}`, no auth. This is where ~95% of the GGUF ecosystem lives, including community requantizers (bartowski, unsloth, mradermacher, second-state, QuantFactory) who convert almost everything to GGUF within days of release. |
| **ModelScope** (`modelscope.cn`) | Yes — already in use as a mirror | Alibaba's HF-equivalent. Same `/resolve/{revision}/{file}` URL shape, no auth for public repos. Mirrors most major open releases (Qwen natively, plus `second-state` GGUF mirrors of Western models). Useful as a fallback when a Hugging Face repo is geo-throttled, and it's the *native* home for Qwen/GLM/InternLM/Yi/DeepSeek releases, often first. |
| **GitHub Releases** (`github.com`) | Allowed, but essentially unused in the current catalog | Release assets (`github.com/{org}/{repo}/releases/download/{tag}/{file}`) are static, unauthenticated, and *already* fixed to a tag — a natural fit for the immutable-revision rule. Real free GGUF sources here: `ggml-org/models` (small llama.cpp test/demo models — already used for the tiny fixture), Google's `google/gemma.cpp`, Mozilla's `Mozilla-Ocho/llamafile` (bundles GGUF+runtime as single executables), and various `.gguf` release assets from smaller labs (StabilityAI, Nomic). Smaller pool than HF, but worth adding a couple of entries from. |
| **Ollama's registry** | Not compatible without new plumbing | Ollama doesn't serve one file at one URL — a "model" is a manifest referencing content-addressed *layer blobs* (`registry.ollama.ai/v2/{name}/blobs/sha256:{digest}`), fetched via a Docker-registry-style protocol, and reassembled locally. Individually the blob URLs are static and could technically be curled, but every model on Ollama is *also* on Hugging Face as plain GGUF (Ollama itself pulls from HF conversions), so this adds no new models — only extra integration cost for zero net-new content. Not recommended. |
| **Meta's official Llama download** (llama.com) | Not compatible | Requires a web form + emailed, expiring, single-use signed URLs. Structurally incompatible with a pinned static URL. Not a loss — bartowski/unsloth GGUF conversions of every Llama release are on Hugging Face within a day, under the same `Llama-3.x` community license already in the allow-list. |
| **Kaggle Models** | Not compatible without new plumbing | Requires a Kaggle account + API token in request headers. Auth-gated, same problem as gated HF repos below. |
| **NVIDIA NGC / build.nvidia.com (NIM)** | Not compatible, and not free-and-clear anyway | Requires an `nvapi-` key in headers; the "free tier" is rate-limited API access, not unrestricted local weight downloads, and many catalog entries are TensorRT-optimized engines, not portable GGUF. Not a fit for this feature. |
| **Gated Hugging Face repos** (official Meta Llama org, official Google Gemma org, official Mistral org) | Blocked only by *acceptance-click* gating, not payment | These require clicking "accept license" while logged in, then an HF access token (`Authorization: Bearer hf_...`) on every request. Nothing here costs money — it's a licensing click-through, not a paywall. But it needs the download pipeline to support a per-request bearer token, which it currently does not (by design — no auth headers today). Practically moot anyway: the community re-uploads (bartowski, unsloth) republish the *same* weights, already license-tagged `Llama-3.1`/`Llama-3.2`/`Gemma` in the existing allow-list, with no login needed. |

**Bottom line:** the download page doesn't need new allowed *domains* to reach far more free models — `huggingface.co`, `github.com`, and `modelscope.cn` already cover essentially everything worth having. What it's actually missing is a **bigger curated catalog** on the domains already allowed, plus (optionally, lower priority) a couple of specific `github.com` release entries. No free source outside this trio was found to be worth the engineering cost of new auth plumbing.

## Curated additions to the catalog

All of these are Apache-2.0/MIT/permissive, have community GGUF conversions on Hugging Face or ModelScope. Sizes below are full-precision param counts; actual download size depends on quantization.

### Best candidates for teaching/fine-tuning specifically

**Note:** real LLM LoRA fine-tuning (Phase 73) was removed -- it depended on `llama.cpp`'s `finetune`/`export-lora` CLI tools, which upstream stopped shipping in every release since 2024-07-25, making the feature permanently non-functional (see README.md's Machine Learning section). The small models below remain good general download candidates but can no longer actually be LoRA fine-tuned through this codebase; only tabular classification/regression fine-tuning (Phase 70) is currently real.

| Model | Params | License | Why it's a good teaching target |
|---|---|---|---|
| **AI2 OLMo 2** | 7B | Apache-2.0 | The one genuinely "fully open" model line: released weights *and* training data *and* training recipe *and* intermediate checkpoints. |
| **HuggingFaceTB SmolLM2** | 360M / 1.7B | Apache-2.0 | Purpose-built as a small, well-documented, fine-tuning-friendly family. Cheap enough to iterate LoRA runs on in minutes on CPU. |
| **TinyLlama-1.1B** | 1.1B | Apache-2.0 | Llama architecture at a fraction of the size — same tooling/prompt format knowledge transfers, smallest real "chat" model in the Llama family tree. |
| **IBM Granite 3.x** (2B/8B instruct) | 2B / 8B | Apache-2.0 | Enterprise-governed provenance (IBM publishes exactly what data went in), clean license story. |

### Small general-purpose chat models (fill out the `conversation` category)

| Model | Params | License | Notes |
|---|---|---|---|
| Gemma 2 (2B instruct) | 2B | Gemma | Already an allowed license (used for CodeGemma). Strong quality-per-size, Google-maintained. |
| Qwen2.5 (3B, 7B instruct) | 3B–7B | Apache-2.0 | Fills the gap between the existing 1.5B and 8B (Llama) — previously no 3B/7B *chat* option, only *coder* variants at those sizes. |
| Mistral-7B-Instruct-v0.3 | 7B | Apache-2.0 | Notable prior omission — Mistral is one of the most-referenced open 7B baselines. |
| Llama-3.2-1B-Instruct | 1B | Llama-3.2 | Smaller sibling of the existing 3B entry — good for the lowest RAM tier. |

### Code-focused (fill out `general-programming` further)

| Model | Params | License | Notes |
|---|---|---|---|
| Qwen2.5-Coder-0.5B-Instruct | 0.5B | Apache-2.0 | Smallest coder tier — nothing below 1.5B previously existed for the lowest-RAM machines. |

A `bartowski/granite-8b-code-instruct-GGUF` entry was investigated but the repository is currently gated (401 from the Hugging Face API for an unauthenticated request), so it was left out rather than added with an unverifiable hash.

## What was implemented

1. **No architecture change.** The three already-allowed domains cover the useful free ecosystem; no auth plumbing (gated-repo tokens, Ollama registry client, NGC/Kaggle auth) was added — the return (near-zero net-new *unique* models) didn't justify the added attack surface of holding third-party credentials server-side.
2. **Expanded the curated `PRESETS` catalog** in `src/web_ui.cpp` with 12 new entries covering the teaching tier, the general-chat gap between 1.5B and 8B, and the smallest code tier. Every commit hash and SHA-256 was read directly from the Hugging Face repo-files API before being added, matching how the existing entries were sourced — none are fabricated or computed locally.
3. **Prioritized the "teaching" tier** (OLMo 2, SmolLM2 x2, TinyLlama, Granite 3.1 2B/8B) explicitly, since that's the stated use case for the ML module now being operational.
4. All new entries use licenses already in `models.cpp`'s `allowed_licenses` list (`Apache-2.0`, `Gemma`, `Llama-3.2`), so no allow-list changes were needed.

## Music category (September 2026)

A new `music` category was added (Phase 98, `docs/PLAN.md`) for Text-to-Music, Voice-to-Music, and Text-to-Voice models — catalog/download only, since MasterAI's inference runtime is GGUF/llama.cpp-only and cannot yet run a safetensors or ONNX model. Two entries were seeded:

| Model | Subtype | Format | License | Notes |
|---|---|---|---|---|
| `facebook/musicgen-small` | Text-to-Music | safetensors | CC-BY-NC-4.0 | Official Meta repo. Required adding `CC-BY-NC-4.0` to the license allow-list (see above) since MusicGen isn't commercially licensed. |
| `rhasspy/piper-voices` (`en_US-amy-medium`) | Text-to-Voice | ONNX | MIT | Official Piper voices mirror; repo-level license is MIT. |

**Voice-to-Music was investigated but not seeded.** Candidates found (`Mothersuperior/YuE2-hum-to-song`, singing-voice-conversion models) were either community adapter weights requiring a separate gated base model, or disabled/policy-restricted repos — nothing self-contained, reliably hosted, and adequately licensed turned up. The `music` category and its allow-list entries accept a Voice-to-Music model whenever a suitable one is found; none is in the curated suggestion list yet.

Every source URL, pinned commit, SHA-256, and byte size for both seeded entries was read from the file's own Hugging Face Git LFS pointer (`.../raw/<revision>/<path>`), the same verification standard used for every other catalog entry.
