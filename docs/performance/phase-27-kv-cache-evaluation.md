# Phase 27 KV-cache reduced-precision quality-parity evaluation

`run_kv_precision_quality_check()` (`src/kv_quality.cpp`) is the real,
backend-validated comparison Phase 27's exit criterion requires before any
reduced-precision KV policy can ever be admitted (`KvCacheManager::
admit_precision()`). Recording its resulting `KvPrecisionEvidence` never
itself flips admission -- an administrator must still call `admit_precision()`
explicitly after reviewing it, exactly as `AdvancedOptimizationRegistry`
already required for Phase 20's evidence-gated features.

## What the function does

Given two already-loaded `RunnerSupervisor` instances -- one launched with
`LaunchTuning::kv_precision = KvPrecision::full` (the default), one launched
with the candidate reduced precision -- it runs the same authored prompt set
through both via `generate()` and compares the resulting text byte-for-byte.
`quality_parity_verified` is `true` only when every prompt produced identical
output between the two runners; any mismatch, or a generation failure on
either runner, marks it `false` with an explanatory note.

## Fixture-backend limitation

`test_phase_twentyseven_kv_cache_accounting_and_eviction` (`test/tests.cpp`)
exercises this function against `masterai_fake_llama`, the native test suite's
fake `llama-server` (`test/fake_llama_server.cpp`). That fixture's
`/completion` response is a fixed canned reply per request shape, independent
of which `--cache-type-k`/`--cache-type-v` launch flags were actually passed
-- so a full-precision and a `KvPrecision::half` run against it are *expected*
to compare byte-identical. This proves the comparison mechanism itself is
real (it genuinely launches two runners, generates from both, and compares),
not that the fixture can demonstrate genuine quality parity or divergence
against a real model's actual reduced-precision KV behavior.

## What an administrator must do before enabling reduced precision

1. Launch a runner pair against a real GGUF model with `KvPrecision::full`
   and the candidate reduced precision.
2. Author a representative prompt set (ideally the same kind of corpus
   `docs/performance/phase-16-retrieval-evaluation.md` and
   `phase-24-extended-retrieval-evaluation.md` use for retrieval quality --
   real project-relevant queries, not synthetic filler).
3. Call `run_kv_precision_quality_check()` with both runners and that corpus,
   and review the resulting `KvPrecisionEvidence.quality_notes` -- an exact
   byte-for-byte match across every prompt is the current, conservative bar
   this function enforces (no fuzzy/similarity threshold).
4. Record the evidence (`POST /api/v1/performance/kv-cache` with
   `action: "record-precision-evidence"`) and, only after reviewing it,
   explicitly admit the precision (`action: "admit-precision"`) or leave it
   un-admitted.

## Cross-request prefix-tree sharing

`admit_prefix_sharing()` follows the identical explicit-only discipline (same
endpoint, `action: "admit-prefix-sharing"`). There is no automated
quality-parity check for this deliverable -- its risk is isolation
(unauthorized KV state exposure across users), not generation quality, and
`PromptSessionManager::try_reuse_shared_template()`'s `SharedTemplateKey`
(prefix content hash + model fingerprint + policy generation + authorized
role set) is the actual boundary enforcement, verified directly in
`test_phase_twentyseven_kv_cache_accounting_and_eviction` (a private per-chat
lookup can never resolve to a shared-template entry or vice versa; two
`SharedTemplateKey`s differing only in `policy_generation` never share a
slot).
