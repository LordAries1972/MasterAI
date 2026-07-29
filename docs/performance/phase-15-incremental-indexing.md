# Phase 15 incremental indexing evidence

## Responsibility boundary

`ProjectIndexer` remains the sole authoritative disk-generation implementation.
It owns bounded canonical file reads, chunk reuse/replacement, immutable segment
publication, manifest recovery, and literal or exact-boundary symbol lookup. It
does not own authorization, retrieval ranking, editor integration, or inference.

`ProjectIndexService` owns one fixed-capacity background queue. It admits manual
rebuilds and typed save, watcher, branch-switch, or periodic notifications.
Queued save/watcher paths are deduplicated up to the hard 4,096-path limit.
Branch-switch and periodic notifications become full scans because their
affected path set cannot be proven complete.

## Implemented flow

1. A full scan filters canonical regular project files and reserves their known
   byte size before reading.
2. Existing per-file chunks are reassembled for exact content comparison.
   Unchanged content reuses its stable chunks and increments
   `filesUnchanged`.
3. An affected-path update validates every supplied path against the project
   root, removes only that path's old chunks, and adds bounded chunks when the
   file still exists and remains indexable.
4. Deleted files therefore disappear without rescanning unrelated files. When
   the final file is deleted, a valid empty segment and manifest generation are
   still published.
5. Publication sorts chunks deterministically, enforces the disk ceiling,
   replaces only the named stale generation, hashes the completed segment, and
   advances the manifest while retaining the prior manifest for recovery.
6. Symbol lookup accepts identifier characters only and requires boundaries on
   both sides, so `token` does not silently match `tokenizer`.

## Current validation

On 2026-07-29, Windows x64 Debug and Release strict-C++17 builds completed and
`masterai_core_tests` passed. The Phase 15 case covers:

- Stable full-scan unchanged elimination.
- One-file affected-path replacement while unaffected chunks remain searchable.
- Exact symbol matches and substring rejection.
- Project-escape rejection.
- Deleted-file removal and durable empty-generation restart recovery.
- Corrupt-active fallback followed by safe replacement of the stale generation.
- Cancellation without replacement of the last valid generation.
- Save-trigger admission, background execution, and authenticated trigger
  status reporting.

## Remaining exit evidence

Phase 15 remains `In progress`. Completion still requires:

- A representative large-project run with measured peak RAM, disk use, elapsed
  time, and incremental-update evidence under configured ceilings.
- Live editor-save, filesystem-watcher, branch-switch, and periodic adapters
  wired to the typed service boundary with debounce evidence.
- Language-aware symbol extraction beyond exact identifier-boundary lookup.
- Portable worker-reader benchmarks against Windows overlapped I/O and Linux
  `io_uring` on the supported hosts before either optional backend is adopted.
