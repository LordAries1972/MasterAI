# Project status

> Part of the [MasterAI README](../README.md).
>
> MasterAI is under active development and is **not yet production-ready**.
> Several major capabilities are implemented and test-covered, but
> distribution-packaging certification and the full multi-machine/multi-
> storage-media performance certification matrix remain outstanding (both
> genuinely require physical hardware this checkout does not have). This
> page records the distinction between *implemented* and *certified*. The
> authoritative source is [docs/PLAN.md](PLAN.md).

Implementation does not automatically mean operational certification. Status
below reflects the evidence recorded in [docs/PLAN.md](PLAN.md) on
**18 August 2026**.

| Phase | Area | Status |
|---:|---|---|
| 0 | Requirements and release decisions | Complete |
| 1 | Native foundation and lifecycle | Complete |
| 2 | Identity and security baseline | Complete |
| 3 | Model registry and hardware assessment | Complete |
| 4 | First isolated inference adapter | Complete; real pinned backend/GGUF path validated |
| 5 | Chat and project web application | Complete; actual-browser real-model chat validated |
| 6 | Secure resumable downloads | Complete; interrupted immutable HTTPS resume and digest promotion validated |
| 7 | Reproducible benchmarking | Complete; same-host real-model comparison validated |
| 8 | Inbound MCP | Complete; live project-bound independent-inspector connection validated |
| 9 | Outbound MCP | Complete |
| 10 | IDE integrations | Complete; VS Code and Visual Studio 2022 hosts live-validated |
| 11 | Operations hardening | Complete |
| 12 | Measured control-plane optimization | Complete for measured native scope |
| 13 | Query measurement and resource baseline | Complete |
| 14 | Bounded-memory foundation | Complete |
| 15 | Incremental disk-backed indexing | Complete; deeper symbol extraction remains a forward enhancement |
| 16 | Deadline-bound hybrid retrieval | Complete; authored hybrid-vs-full-text evaluation passes |
| 17 | Security-partitioned cache hierarchy | Complete; representative cache latency benchmark passes |
| 18 | Prompt-prefix and KV/session reuse | Complete; real repeated-turn prefix reuse validated |
| 19 | Hardware/model calibration | Comparative Qwen 3B offload/throughput matrix validated; broadened 2026-08-26 to a smaller (~2B) and a larger (~6.7B) model on the same host; broader semantic-quality scoring and a second physical host remain forward work |
| 20 | Optional advanced throughput | Complete admission layer; every candidate (including `multiple_warm_runners`, closed 2026-08-23) now has a real implementation and a real caller gated on this registry; all candidates remain disabled by default until an administrator records real evidence and admits one |
| 21 | Native asynchronous storage and prefetch engine | Implementation complete; bounded IOCP/`pread` fallback, mapped regions, coalescing, cancellation |
| 22 | Hierarchical content and model-data caching | Implementation complete; immutable resident L1 and streaming-aware segmented eviction |
| 23 | Tokenization, template, and prompt-fragment caching | Implementation complete; exact recorded token-prefix reuse |
| 24 | Advanced retrieval fan-out and adaptive query planning | Implementation complete; authored Release evaluation clears Phase 16 baseline |
| 25 | Continuous inference batching and request scheduling | Implementation complete; backend activation remains calibrated and default-off |
| 26 | Model loading, mapping, pre-touch, and warm-state management | Implementation complete; all selective pre-touch levels actionable |
| 27 | KV-cache compression, placement, and lifecycle management | Accounting/placement/lifecycle, reduced-precision launch flags + quality-parity check, and cross-request prefix sharing all real; every admission stays explicit-administrator-gated (never self-enabling); chats can now be marked as shareable templates and `send_chat_message()` uses the shared-template reuse path in production once prefix sharing is admitted |
| 28 | NUMA, processor-group, and topology-aware execution | Discovery and recommendation implemented; real thread-pinning wired into every connection worker thread, gated on an off-by-default configuration flag (now a working Settings checkbox) plus the Phase 20 admission; live per-host benefit evidence (Phase 36) still pending |
| 29 | Model tiering, routing, and cascade inference | Complete; live chat routing via the "Auto (Tiered)" model-picker entry, with a real buffered cascade (confidence-checked, escalated once) for any tier below the largest |
| 30 | Memory deduplication and immutable shared-data architecture | Implemented at a scoped-down level |
| 30A | CPU-only and GPU-disabled low-memory operation | Implemented; matched real-model benchmark matrix recorded |
| 31 | Storage tiering, virtual drives, and scratch-volume management | Implemented, including Priority B tier-migration tooling |
| 32 | Speculative decoding and draft-model acceleration | Implemented; real dual-model launch path measured on real hardware (negative result on this low-VRAM host) |
| 33 | Distributed local runners and multi-device orchestration | Implemented; local runner pool and intranet mTLS worker protocol |
| 34 | Adaptive performance controller | Complete; every named knob has a real, genuinely-consumed target (instant, next-natural-load, or a documented permanent scope limit) |
| 35 | Performance administration interfaces | Implemented at a scoped-down level; one consolidated Performance page |
| 36 | Full performance certification and regression gates | Implemented at a scoped-down level; real quality-plus-five-regression-check-group certification with threshold-gated build comparison, real evidence recorded on this host via `scripts/run-certification.ps1`; physical cross-device matrix remains an administrator-run exercise on further hosts. 2026-08-24: added real commit-bytes/page-fault/storage-operation-count metrics and widened the CPU sample window (100ms to 500ms) to stop a too-short sample from spuriously tripping the regression gate; model-load-time, cancellation/shutdown latency, per-stage retrieval/prompt/tokenization latency, mapped-bytes, and cache-hit-rate remain out of scope (no reusable measurement exists for them in this codebase -- see docs/PLAN.md's Phase 36 status note) |
| 37 | Machine Learning module foundation | Implemented at a scoped-down level |
| 38 | Machine Learning projects | Implemented; Phase 93 (2026-08-24) added the section 5 governance/target fields (administrators, validated against real accounts; approved data sources; security classification; target architecture/deployment; success/evaluation/safety criteria; a declared storage allocation) |
| 39 | ML model registry and dataset manager | Model Registry implemented at a scoped-down level; Dataset Manager's remaining fields (record/file count, schema, content hash, duplicate rate, data quality score) are now real, computed at upload time, and Dataset Versioning (section 11) is implemented -- Phase 94 (2026-08-24) |
| 40 | Subject Knowledge Manager | Implemented at a scoped-down level |
| 41 | Data labeling and preparation | Implemented at a scoped-down level |
| 42 | Training Jobs | Implemented; real tabular training executor (Phase 56); Phase 95 (2026-08-24) added the section 16 execution-policy fields, with max runtime, retry-once failure recovery, and checkpoint cadence genuinely enforced by the executor |
| 43 | Evaluation Lab | Implemented; real tabular scoring harness (Phase 56); Phase 96 (2026-08-24) added latency, throughput, memory use, a stability score, a robustness score, and an optional bias/fairness breakdown -- every metric category honestly computable for a tabular model; LLM-only categories (perplexity, hallucination rate, ...) remain out of scope |
| 44 | Experiment Tracking | Implemented at a scoped-down level initially; Phase 80 adds a real training/evaluation executor and side-by-side comparison |
| 45 | Fine-Tuning Interface | Implemented; Phase 70 adds a warm-start executor that continues gradient descent from an existing tabular model's learned weights. (Phase 73 added a real LLM LoRA fine-tuning executor via the `llama.cpp` `finetune`/`export-lora` CLI, triggered by a `llm:`-prefixed method; removed -- see Phase 73's row below.) |
| 46 | Model Builder | Fully implemented (full section 9 design sheet, basic/advanced modes); `POST .../run` hands a submitted configuration off to a real `TrainingJob`, which now trains against a real from-scratch, configurable multi-layer-perceptron (MLP) trainer when the settings specify a hidden-layer architecture, genuinely consuming layer/activation/dropout/optimiser/batch-size/gradient-clip/accumulation/LR-schedule/early-stopping/init/epoch/seed/checkpoint settings (2026-08-23); attention/tokenizer/sequence-length/mixed-precision fields stay honestly unconsumed as transformer-only concepts this tabular-row engine does not implement |
| 47 | Prompt and Instruction Training | Implemented at a scoped-down metadata level initially; Phase 81 adds the real content record, generation, multi-model testing, duplicate/contradiction detection, and structured-output validation |
| 48 | Synthetic Data Generation | Implemented at a scoped-down metadata level initially; Phase 82 adds a real generation executor |
| 49 | Embeddings and Vector Stores | Registry implemented; real local hashing-vector index added in Phase 59 |
| 50 | Retrieval-Augmented Generation | Configuration implemented; real retrieval/context executor added in Phase 60 |
| 51 | Subject Examination System | Implemented at a scoped-down record level; `POST .../run` genuinely asks the target model each configured question and grades it with a real LLM-as-judge (fixed JSON-prompt pattern, 2026-08-23), falling back to the original plain text-overlap heuristic whenever the judge call fails or its reply is unparseable — every result records which grading path produced it |
| 52 | Hyperparameter Optimization | Implemented at a scoped-down record level; `POST .../run` runs a real, bounded grid search over learning rate and epochs (plus batch size, dropout, and optimiser choice for an MLP-architecture job, 2026-08-23) against the referenced training job's dataset |
| 53 | Model Optimization | Implemented at a scoped-down record level; no optimizer executor |
| 54 | Checkpoint Management | Implemented at a scoped-down retention-record level initially; Phase 79 adds real mid-training weight-snapshot capture and a resume-training executor |
| 55 | Deployment Manager | Implemented at a scoped-down approval-record level initially; Phase 82 adds a real deploy/health/rollback executor |
| 56 | Real ML execution engine (tabular training, evaluation, prediction) | Implemented; dataset content upload accepts CSV, JSON (array of flat objects), JSONL, and Parquet (via the same DuckDB helper Knowledge ingestion uses), all converted to CSV before validation |
| 57 | Model Comparison (real baseline-vs-candidate benchmark executor) | Implemented |
| 58 | Knowledge-file ingestion | Implemented; bounded text upload, SHA-256 provenance, durable chunk records |
| 59 | Local embedding and vector indexing | Implemented; authored 128-dimensional hashing vectors and index profiles |
| 60 | RAG retrieval and grounded context assembly | Implemented; approved-config query execution with ranked chunks and citations |
| 61 | Learned embeddings and once-per-chat memory recall | Implemented; isolated llama.cpp embedding adapter, durable vector provenance, and retained chat memory snapshots |
| 62 | Inference Endpoints | Implemented at a scoped-down record level initially; Phase 77 adds a real network listener; Phase 82 completes the auth-token/policy UI and the roster entry |
| 63 | Hardware and Compute | Implemented at a scoped-down record level initially; Phase 67/75 add real local and remote telemetry |
| 64 | Automation Pipelines | Implemented at a scoped-down record level initially; Phase 69/71/72 add real stage execution |
| 65 | Safety and Governance | Implemented at a scoped-down policy/model-card approval level initially; Phase 74 adds real heuristic content scanning |
| 66 | Audit Logs and Machine Learning Settings | Implemented; real read over the existing audit trail, and a real ML-scoped subset of System Configuration |
| 67 | Hardware and Compute live telemetry | Implemented; a compute node flagged as the local host reports a genuinely fresh `probe_hardware()` snapshot on demand |
| 68 | Monitoring and Diagnostics | Implemented; real aggregation of live local-host hardware, training-job counts, evaluation metrics, and benchmark throughput |
| 69 | Automation Pipelines real executor | Implemented; "Train model"/"Evaluate model" stages genuinely run, every other named stage honestly reported as skipped |
| 70 | Fine-Tuning real executor | Implemented; genuine warm-start gradient descent from a base model's trained weights, registered as a new model |
| 71 | Automation Pipelines: more real stages and live progress | Implemented; "Validate data"/"Validate model"/"Safety tests"/"Request approval"/"Deploy staging"/"Deploy production"/"Rollback"/"Monitor" all genuinely execute (Import/clean/label/split data, optimize, and staging tests remain honestly skipped); a run now executes on a background thread and reports live per-stage progress the web UI renders as a progress bar |
| 72 | Automation Pipelines: final six real stages | Implemented; all six stages genuinely execute, including "Label data", which now runs a real heuristic auto-labeler (quantile-binning or existing-label validation) when no completed labeling task is on record |
| 73 | Real LLM LoRA fine-tuning | Removed (2026-08-25). Depended on `llama.cpp`'s `finetune`/`export-lora` CLI tools, which upstream removed from all releases on 2024-07-25 (PR #8669) -- no compatible binary has existed since, and any build old enough to still have them predates model architectures added afterward (e.g. Qwen3), so the feature could never load this codebase's own models. The executor, its `AppConfig` fields, HTTP routes, and web UI were deleted rather than kept as permanently-broken dead code; Phase 70's tabular warm-start fine-tuning is unaffected |
| 74 | Safety and Governance real content scanning | Implemented; local heuristic secret/prompt-injection/restricted-term scanning (`scan_content_for_risks`) plus a real LLM-as-judge ML classifier pass (`scan_content_with_model_classifier`) for bias/hallucination/subtler harmful content |
| 75 | Hardware and Compute remote telemetry agent | Implemented; `masterai telemetry-agent` run on a remote node, polled for real by a `ComputeNode` with a matching `agentUrl` |
| 76 | RAG real answer generation | Implemented; the RAG query route's optional `"generate":true` mode produces a real generated answer grounded in the same retrieved context |
| 77 | Inference Endpoints real listener | Implemented; an `active` endpoint opens a real listener enforcing authentication, rate limiting, and a real per-endpoint safety policy (scan on/off, block-on-finding, attached `SafetyPolicy`, opt-in Phase 74 model classifier), re-read live on every request |
| 78 | Monitoring real per-request telemetry | Implemented; real latency percentiles, queue depth, requests/minute, live per-step tabular-training progress, and KV-session cache-hit rate |
| 79 | Checkpoint Management real capture and resume | Implemented; training/fine-tuning runs capture a genuine weight snapshot at each checkpoint epoch, and "Resume training" continues gradient descent from one, registering the result as a new model |
| 80 | Experiment Tracking real executor | Implemented; "Run now" genuinely trains and evaluates the experiment's dataset (real training/validation/evaluation metrics, checkpoints, hardware, runtime), and "Compare" builds a genuine side-by-side diff of two or more already-run experiments |
| 81 | Prompt and Instruction Training real content and operations | Implemented; a real content record (system/user instruction, context, expected/rejected response, tool calls/results, output format, difficulty, safety classification), real draft generation and multi-model testing via `execute_rag_generation`, heuristic duplicate/contradiction detection, real JSON structured-output validation, and enforced approval-requires-content |
| 82 | Deployment Manager, Inference Endpoints, and Synthetic Data completion | Implemented; Synthetic Data gets a real generation executor (technique-specific prompts via `execute_rag_generation`); Deployment Manager gets its own real deploy/health/rollback action (approved-Model-Card gate, trained-weights health signal, supersede/rollback tracking); Inference Endpoints' already-real Phase 77 listener gets its missing auth-token/policy UI. All three roster entries move from `planned` to `available` |
| 83 | Safety and Governance roster completion | Implemented; no new executor needed — Phase 74's real content scanning and Phase 82's approval-gated deployment/inference enforcement already met the bar, so the roster entry moves from `planned` to `available`; added the missing `SafetyGovernanceStore`/`scan_content_for_risks` store-level test |
| — | ML forms clarity pass | Implemented; hover/focus "?" hint bubbles on ambiguous fields (toggleable off per-browser from Machine Learning Settings), every remaining raw-ID text field converted to a named dropdown, and every remaining comma-separated multi-id field converted to a checkbox multi-select |
| 84 | Agentic tool use in chat | Implemented, not yet build/host validated; real `read_file`/`list_directory`/`search`/`write_file`/`delete_file`/`run_command` tools, an auto-drive mode ("confirm"/"implement"/"proceed"-style composer messages) that keeps a turn working a stated plan across turns with no further input until the model reports it done, a per-chat Tool execution mode (Auto/Confirm every action/Off, set from the composer's Model settings panel), mandatory Approve/Deny for destructive actions regardless of mode, every tool call/result shown live in the transcript, admin allow-list management routes/UI, and the same six tools reachable through MCP `tools/call` (`masterai.project.*`) sharing the identical `execute_chat_tool()` dispatch — MCP refuses rather than executes a high-risk call, since it has no Approve/Deny UI of its own yet |
| 85 | Inference and retrieval throughput tuning | Implemented, not yet build/host validated; evidence-based `--threads`/`--ubatch-size` launch tuning derived from the host's real core count (Phase 19's `thread_count`/`ubatch_tokens` fields existed since their introduction but were never populated until now); `CacheCategory::retrieval_result` actually wired into the live retrieval request path (the category and its serializer existed since Phase 17 but nothing had ever called them); parallel worker-pool dispatch for the semantic-embedding adapter's per-chunk embed calls and for model pre-touch; a cached (not rebuilt-per-call) dependency-graph map for the `dependency_neighbour` retrieval strategy |
| 88 | Downloaded models as a fine-tuning/model-builder base, and a live-streaming cleanup fix | Implemented; Fine-Tuning Job's and Model Builder Config's "Base model" dropdowns now also list every downloaded (not just previously-registered) model, tagged "(downloaded)"; picking one registers it into the Model Registry transparently on submit (`resolve_or_register_base_model()`, `src/server.cpp`) with no separate manual step. Also fixes the chat UI occasionally rendering a raw, unstripped fragment of a hallucinated tool-call attempt: the `"complete"` streaming event now carries the server's fully-cleaned reply text, which the client swaps in before its final render instead of trusting its own pre-cleanup streamed buffer |
| 89 | ML web UI clarity pass, part 1: core training pipeline | Implemented (core pipeline only — see `docs/PLAN.md` Phase 89 for the remaining-panels list); every dataset picker and the Dataset Manager table now show real content status ("N row(s) ready" / "⚠ no content uploaded yet", `datasets_json_with_content_status()`); Training Jobs' "Train now" is disabled with an explanatory tooltip when its dataset has no uploaded content, instead of only failing after the click with `ml_dataset_has_no_content`; a numbered "Step 1 → 5" banner (`ml_step_flow()`) now orients a user landing on any of Projects/Model Registry/Dataset Manager/Training Jobs/Evaluation Lab/Experiment Tracking; sidebar nav fixed to put Model Registry beside Dataset Manager instead of mislabeled under "Logs and Settings", and Hyperparameter Optimization after Training Jobs instead of before it |
| 90 | ML web UI clarity pass, part 2: responsive step banner, and automatic categorical feature encoding | Implemented; `.mlStepFlow` is now a CSS grid that wraps at any browser width/height (was a fixed-width scrolling flex row that clipped on resize), and Dataset Manager renders one banner spanning steps 2–3 instead of a squeezed duplicate copy; `parse_tabular_csv()` gained an opt-in `encode_categorical_features` flag (default off, every prior caller unchanged) that one-hot encodes a text/category feature column instead of rejecting it with "every feature column must be numeric" — fit mode for a fresh training/experiment run, apply mode (reusing the trained model's own persisted `categorical_encoding`) for evaluation/comparison/fine-tuning/checkpoint-resume, so a dataset like one with a `record_type` text column now uploads, trains, and predicts (via its natural value, e.g. `{"record_type":"document"}`) with zero manual CSV pre-processing |
| 91 | Dataset Manager instruction-purpose datasets, and quote-aware CSV row parsing | Implemented; a `Dataset` now carries a `purpose` ("Tabular data" or "Instruction / fine-tuning text", chosen at registration and shown as a Dataset Manager list column) that the content-upload endpoint validates against — a Tabular dataset is unchanged (classification/regression target column, up to 64 distinct labels); an Instruction dataset skips that entirely and instead just checks for an instruction/prompt column and a response/output/completion column, since LLM fine-tuning data has no target-column concept and was previously forced through — and could fail — classification validation that never applied to it. Also fixes a real parsing bug the same investigation surfaced: CSV row splitting in `parse_tabular_csv`, `auto_label_tabular_dataset`, and the LLM fine-tuning text writer was a raw split on `\n`, silently corrupting any RFC 4180 quoted field with an embedded newline (any multi-paragraph text cell) into multiple broken rows; all three now use a quote-aware row splitter |
| 92 | Model Registry quantization tracking | Implemented; `ModelRegistryEntry` gained a `quantization` field, auto-carried over from a downloaded model's manifest when it becomes a Fine-Tuning base (previously silently dropped), settable manually otherwise, and shown as a Model Registry list column. The real LLM LoRA fine-tuning executor now checks it against the precisions llama.cpp's finetune tooling trains against reliably (F32/F16/BF16/Q8_0) and adds a non-blocking `quantizationWarning` to the job's result when the base is more heavily quantized. Also closes a pre-existing gap found while wiring that warning to the UI: the Fine-Tuning Jobs page's "Fine-tune now" button previously only handled the tabular path's synchronous response and left a real (`llm:`-method) job's 202 "queued" response completely unrendered — it now polls the job's result until it completes or fails |
| 98 | Instant natural-language execution for mechanical chat tools | Implemented; a plain request like "show me the directory of ..."/"list files in ..."/"search the project for ..."/"run the command ..." is now matched against a bounded natural-language phrase table and, when matched, its `list_directory`/`read_file`/`search`/`run_command` tool call executes immediately — before the message ever reaches the model — instead of waiting on a full generation just to decide to call a tool a fixed phrase already answers unambiguously. Destructive/high-risk calls (and any chat set to "Confirm every action") still pause for an explicit Approve/Deny click exactly as the model-mediated tool path does; a safe call still triggers one real model turn afterward so it can comment on the result. `write_file`/`delete_file` and unmatched/ambiguous phrasing are untouched, falling straight through to the normal model-mediated `[[TOOL_CALL]]` path |

Current validation includes Windows x64 Debug and Release builds and tests under
strict C++17, plus a Linux x86-64 Release build and test run under Ubuntu 26.04
WSL. Release packaging certification on the pinned Ubuntu 24.04 and Debian 13
hosts is still outstanding.

Phase 1 was revalidated on the current tree on 5 August 2026 without an
additional rebuild: the Windows Debug native suite passed, both loopback health
endpoints returned HTTP 200 on port 7070, and the documented graceful-stop path
completed successfully.

Phase 15 has a bounded native indexing service, affected-path updates,
typed/coalesced trigger admission, disk-generation recovery tests, a native
`ProjectWatcher` file-watcher/branch-switch adapter that calls the indexing
service automatically (no external editor or VCS hook required), a
representative large-project ceiling measurement (`masterai index-probe`),
and a recorded platform-I/O backend decision. Both of its exit criteria now
have current evidence on Windows Debug and Release; deeper language-aware
symbol extraction remains a forward enhancement, not an exit-criterion
blocker.

Phase 16 adds a `RetrievalPlanner` that reads a project's Phase 15 index
directly (exact symbol, exact text, and per-token lexical strategies),
runs bounded parallel strategy steps under a hard request deadline, fuses
and deduplicates results on canonical chunk identity, and discloses exactly
which evidence was used through `/api/v1/queries/{id}`. Phase 17 adds a
byte-bounded, security-partitioned `CacheManager` in front of Phase 16
retrieval results, keyed so that a file change, an index republish, or a
membership/policy change makes a stale entry unreachable automatically,
with authenticated `GET/POST /api/v1/system/cache*` administrative routes.
Both are Windows Debug- and Release-validated; Phase 24's authored evaluation
also closes Phase 16's previously outstanding evaluation-set criterion.

Phases 21–30 add a native performance/architecture layer on top of Phases 15–19:
an asynchronous storage/prefetch engine, hierarchical and prompt caches,
staged retrieval fan-out, weighted-fair scheduling, model warm-state
management, KV-cache governance, hardware-topology decisions, model-routing
logic, and an immutable shared-buffer/arena architecture with a zero-copy
chat-streaming write path. Phases 21–26 are implementation-complete; Phases
27–30 retain the explicit scope limits recorded in [docs/PLAN.md](PLAN.md).
Linux uses the documented bounded `pread` fallback instead of `io_uring`, and
continuous batching remains default-off until host/model/backend calibration
admits it. Phase 31 adds storage tiering (`StorageTier`, measured from the
same Phase 21 device-type/latency evidence), a `ScratchVolumeManager` (bounded
per-job/global quotas, atomic publish, crash-recovery journal, orphan cleanup,
free-space reserve), a hard code-enforced prohibition on placing durable data
(models, chats, audit/security records, user databases, downloads, backups,
sole-copy indexes) on RAM-backed storage, separate physical/commit/pagefile/
page-fault/model-resident memory accounting, and best-effort Windows
filesystem-integrity detection (compression/encryption/dedup/virtual-disk/
network-redirection) feeding a measured, not assumed, model-placement
recommendation. Its tier-migration workflow remains future work; Phases
32–36 remain planned.

Phases 37–55 establish the Machine Learning administration layer:
administrators manage durable, permission-gated records through native web
pages and `/api/v1/ml/*` routes, with lifecycle, review, approval, reload,
and deletion behavior covered by native tests. Phase 56 adds the module's
first real execution engine on top of those records: administrators upload
CSV dataset content (validated server-side), run Training Jobs that
actually train tabular models by gradient descent (linear regression, and
logistic/softmax classification) with genuine loss curves, deterministic
held-out splits, measured-loss checkpoints, and persisted weight artifacts;
run Evaluation Lab scoring that produces real accuracy/precision/recall/F1/
confusion-matrix or MSE/MAE/R² metrics; and serve live predictions from any
trained model through the Model Registry, all surviving server restarts.
Phase 57 extends that engine with Model Comparison: a baseline and a
candidate model are both evaluated against one shared benchmark dataset,
and the stored verdict reports both metric sets, the primary-metric delta,
and the measured winner.
Phases 58–61 add a second evidence-producing path: administrators select a
local text, Markdown, CSV, JSON, JSONL, or Parquet file in the Subject
Knowledge Manager (Parquet is converted to text via a configured,
process-isolated DuckDB CLI helper); MasterAI bounds and hashes the accepted
content, creates overlapping
chunks, persists authored 128-dimensional hashing vectors, and exposes a
measured vector-store profile. An approved RAG configuration can then run
hybrid, vector, or keyword retrieval and return ranked source chunks plus a
citation-ready context package.
Phase 61 lets a Vector Store instead select a verified, ready GGUF from the
`embeddings-code-search` category. MasterAI loads it through the existing
isolated llama.cpp runner, validates and normalizes its learned vectors, and
persists the exact model and dimensions so retrieval cannot silently mix
embedding spaces. Durable user-memory records are also recalled only when a
conversation starts; the bounded snapshot is retained with that chat and
reused on later turns and after restart.
The honest remaining boundary: the authored fallback is still lexical
feature hashing, and no learned embedding GGUF is installed in this
checkout for model-specific semantic-quality or latency evidence. RAG
generation (Phase 76) runs for real now -- see the status table
above for its own boundary. For the concepts, current workflows, exact
capability boundary,
and a sequential teaching guide, read
[How to Use Machine Learning and Models with MasterAI](HowToUse-MachineLearning.md).

No release may be called production-ready until the functional, security,
migration, recovery, MCP conformance, performance, resource-ceiling, retrieval,
cache-isolation, licensing, provenance, and documentation gates all pass.
