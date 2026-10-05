# Machine Learning

> Part of the [MasterAI README](../README.md). For a classroom-style guide,
> read [How to Use Machine Learning and Models with MasterAI](HowToUse-MachineLearning.md).

MasterAI now provides a native, administrator-only Machine Learning
administration area at `/app/ml`. Its current pages manage scoped lifecycle
records for projects, registered models, datasets, subject knowledge,
labeling, data preparation, training and fine-tuning jobs, evaluation,
experiments, model building, instruction examples, synthetic data, vector
stores, RAG configurations, subject exams, hyperparameter searches,
model-optimization runs, training checkpoints, deployments, model
comparisons, inference endpoints, compute nodes, automation pipelines, and
safety-governance policies/model cards. All 29 interfaces the plan names are
now present in the sidebar and marked available in the Dashboard's own
roster; Audit Logs and Machine Learning Settings are genuinely functional
rather than lifecycle metadata (a real read over the existing audit trail,
and a real ML-scoped subset of System Configuration), covered in more
detail below. The Dashboard (`/app/ml`) itself leads with the five pages
that form the one real end-to-end path -- create a project, register a
dataset, upload its content, train, evaluate -- as clickable step boxes in
order, before listing everything else as optional supporting tooling.

Since Phase 56, the module executes real machine learning for tabular data:
uploading CSV content to a dataset makes it trainable, "Train now" on a
training job runs actual gradient-descent optimization (linear regression
for numeric targets, logistic/softmax classification for categorical ones)
with a genuine loss curve, a deterministic held-out split, and measured-loss
checkpoints; "Evaluate now" on an evaluation run computes real
accuracy/precision/recall/F1/confusion-matrix or MSE/MAE/R² scores; and the
Model Registry serves live predictions from the persisted learned weights.
Since Phase 57, "Compare now" on a Model Comparison evaluates a baseline
and a candidate model against the same benchmark dataset and stores the
measured verdict — both metric sets, the macro-F1 (classification) or MSE
(regression) delta, and the winner.

Since Phases 58–61, the Subject Knowledge Manager uses a native browser file
dialog to ingest bounded text sources into a selected vector store. The
server records the source filename, media type, byte count, SHA-256 digest,
and real chunk count, then persists deterministic 128-dimensional hashing
vectors, or learned vectors from a selected verified embedding GGUF through
the isolated llama.cpp adapter. The RAG page executes approved hybrid,
vector-only, or keyword-only retrieval and displays the ranked scores, source
citations, exact source text, and assembled context. Dataset upload and all
cross-record ML forms now
use file dialogs and named selectors instead of requiring pasted file content
or copied opaque IDs. Knowledge ingestion also recognizes the fixed JSON/
JSONL export shapes produced by the separate Scraper Project dataset
builder and rewrites them to clean text before chunking, instead of
chunking raw JSON syntax.

Since Phase 103, a Web Research page (`/app/ml/research`, off by default)
adds a way to populate the knowledge base beyond manual uploads: it queries
the enabled, administrator-keyed search provider(s) (Google Custom Search,
Bing Web Search — official APIs only, no scraping), scores each result's
source domain against an editable reliability-tier table, fetches and reads
only the sources at or above a configurable reliability threshold (80% by
default), and ingests relevant findings through the same
`KnowledgeIndexStore::ingest()` path the Subject Knowledge Manager above
uses — so research findings are immediately retrievable by the RAG page.
Bounded by configuration (results per query, pages fetched, fetch timeout);
relevance is judged by a simple keyword-overlap check, not a model call.

Since Phase 66, every `ml.*` administrator action across every interface is
visible in a dedicated read-only Audit Logs page (the most recent 200
entries, newest first), and a Machine Learning Settings page exposes the
genuinely-enforced subset of server configuration (the Dataset Manager's
CSV upload cap, the Subject Knowledge Manager's document cap and Parquet
helper path) without requiring a trip to the general System Configuration
page.

Phases 67-71 turned the module's remaining "records intent" interfaces into
real executors one by one: Hardware and Compute reports genuinely fresh
local-host telemetry (Phase 67), Monitoring and Diagnostics aggregates real
hardware/training/evaluation/benchmark data (Phase 68), Automation
Pipelines and Fine-Tuning gained real "Train model"/"Evaluate model" and
warm-start fine-tuning executors (Phases 69-70), and Phase 71 wires seven
more Automation Pipeline stages to real, already-existing executors:
"Validate data" (the same CSV structural check the trainer relies on),
"Validate model" (a trained-weights lookup), "Safety tests" (an approved
Model Card lookup), "Request approval"/"Deploy staging"/"Deploy
production" (a real Deployment record created and approved for that
environment — an approval decision, not a live traffic cutover), and
"Rollback" (that deployment's approval revoked); "Monitor" also becomes
real, reusing Phase 68's aggregation. A pipeline run now executes on a
background thread and reports live per-stage progress that the web UI
renders as a progress bar, instead of blocking the page until every stage
finishes. Phase 72 then closed the remaining six stages Phase 71 left
honestly `"skipped"`: "Import data" checks the dataset actually has stored
content, "Clean data" removes blank/exact-duplicate rows and persists the
cleaned CSV back over the dataset, "Split data" reports real train/holdout
counts using the same formula the trainer itself uses, "Optimize" runs a
real magnitude-pruning pass against the pipeline's model's actual learned
weights, "Staging tests" reuses the same real evaluation executor "Evaluate
model" calls, and "Label data" either references a completed real labeling
task or runs a real deterministic heuristic labeler
(percentile-threshold-based) when none exists. Phase 76 added real
generative RAG answers, summarized in the [Project status](project-status.md)
table. (Phase 73 added a real LLM LoRA fine-tuning executor via the pinned
llama.cpp `finetune`/`export-lora` CLI, triggered by a `llm:`-prefixed
Fine-Tuning method; it was later removed once those CLI tools turned out
to be permanently unavailable from any current `llama.cpp` release -- see
Phase 73's row in the [Project status](project-status.md) table.)

The honest boundary that remains: creating or advancing the other job types
(section 2's interfaces the [Project status](project-status.md) table still
marks `Planned`) records administrative intent and state, not execution.
Creating an empty vector-store record alone does not populate it. The
authored embedding fallback is not a learned semantic embedding model, and
learned-adapter quality depends on the verified embedding GGUF an
administrator installs.
Approved externally trained models must still be deliberately packaged
as verified GGUF artifacts in the inference model tree before MasterAI can
serve them for chat.

For a classroom-style explanation of machine learning, model preparation,
the current interfaces, exact limitations, existing-model setup, and the
sequential workflow for teaching a specialized model, read
[How to Use Machine Learning and Models with MasterAI](HowToUse-MachineLearning.md).
