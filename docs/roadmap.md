# Roadmap

> Part of the [MasterAI README](../README.md). The detailed roadmap,
> deliverables, dependencies, installation outcomes, and exit criteria are
> maintained in [docs/PLAN.md](PLAN.md).

Near-term work is:

1. ~~Author the Phase 16 retrieval-quality evaluation set demonstrating
   improvement over full-text-only retrieval~~ — done (2026-08-05): see
   [docs/performance/phase-16-retrieval-evaluation.md](performance/phase-16-retrieval-evaluation.md),
   2/2 hybrid hits versus 0/2 literal full-query hits.
2. ~~Author the Phase 17 representative-query latency benchmark comparing
   cached against uncached retrieval preparation time~~ — done
   (2026-08-05): see [docs/performance/phase-17-cache-benchmark.md](performance/phase-17-cache-benchmark.md).
3. ~~Complete the Phase 19 comparative calibration evidence and the Phase 30A
   matched `auto`-versus-`cpu_only` real-model benchmark matrix~~ — done
   (2026-08-13): GPU utilization/thermal-trend probing is wired into
   `CalibrationService` with real measured evidence, and both `auto`/
   `cpu_only` states now persist a `TuningProfile` on the same pinned host/
   model; broadened 2026-08-26 to a smaller and a larger model beyond the
   original Qwen 3B, see [docs/performance/phase-19-qwen3b-matrix.md](performance/phase-19-qwen3b-matrix.md).
4. Evaluate Phase 20 throughput options independently after prerequisite
   evidence closes; use the durable admission API and never enable an
   unavailable, regressing, or fallback-unverified candidate.
5. Longer term: evaluate deeper language-aware symbol extraction for
   Phase 15.
6. Close the documented scope trims across Phases 21–30, including Linux
   `io_uring`, unadapted retrieval strategies, live backend batching,
   backend-actionable pre-touch, validated KV compression/prefix sharing,
   measured worker affinity, live cascade routing, and broader zero-copy use.
7. Phases 31-36 (storage tiering, speculative decoding, distributed local
   and intranet-worker runners, the adaptive performance controller,
   performance administration, and the full benchmark matrix/regression
   gate) are all now implemented. Phase 32's dual-model launch path now has
   real-hardware evidence (2026-08-18, corroborated 2026-08-26) across two
   compatible model pairs on this low-VRAM host -- both a real, honest
   *negative* result (speculative decoding measured slower here, not
   faster), which is a valid closed measurement, not an open gap; see
   [docs/performance/phase-32-speculative-matrix.md](performance/phase-32-speculative-matrix.md).
   Phase 36's single-host software-controllable matrix now covers two
   model/profile combinations (2026-08-26); its cross-device physical
   matrix (other storage media, other physical machines/GPUs) still needs
   an administrator to run it on each further real target host (see
   docs/PLAN.md). Remaining forward work: wire `IntranetWorkerPool`
   into the live chat-generation dispatch path the way `LocalRunnerPool`
   already is, and extend the Phase 35 administration page toward the
   plan's remaining named pages (Query Traces, Runner Configuration, Model
   Comparison).
8. Generated-answer RAG (Phase 76) is now implemented (LLM LoRA
   fine-tuning, Phase 73, was implemented and later removed -- see its
   row in the [Project status](project-status.md) table) -- remaining
   forward work is the ML executors the Project status table still marks
   `Planned` (e.g. safety/
   governance's PII/copyright/data-poisoning detectors and retention/
   export/network policy enforcement, section 2's interfaces with no
   phase of their own yet), without allowing lifecycle state to
   substitute for proof that work ran successfully.

Outstanding operational certification also includes:

- Real-model browser generation-cancellation validation
- Ubuntu 24.04 and Debian 13 packaging-host certification
- Optional pinned `whisper.cpp` integration, if enabled
- Broader Phase 19 semantic-quality scoring beyond the fixed successful calibration workload

The detailed roadmap, deliverables, dependencies, installation outcomes, and
exit criteria are maintained in [docs/PLAN.md](PLAN.md).
