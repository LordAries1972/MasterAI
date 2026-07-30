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

## Live watcher/branch-switch adapter (2026-07-31)

`ProjectWatcher` (`src/project_watcher.cpp`) is the native process that closes
the gap between the typed trigger vocabulary above and an actual unattended
caller. It owns one background thread, started and stopped by
`HttpServer::State` alongside `ProjectIndexService`, and is on by default
(`indexing.watchProjectFiles`, `AppConfig::watch_project_files`).

- Windows: one `ReadDirectoryChangesW` handle per project root
  (`bWatchSubtree=TRUE`, so the whole tree is covered natively) multiplexed
  through a single I/O completion port on the watcher's one thread.
- Linux: `inotify_init1` with directories added recursively
  (`IN_CREATE`/`IN_DELETE`/`IN_MODIFY`/`IN_MOVED_FROM`/`IN_MOVED_TO`/
  `IN_CLOSE_WRITE`/`IN_DELETE_SELF`), extending the watch tree on observed
  subdirectory creation, bounded to 16,384 watched directories per process so a
  pathological tree cannot exhaust the inotify instance.
- Both platforms coalesce a burst of raw OS events per project into one
  `IndexTrigger::watcher` call after a 300&nbsp;ms quiet debounce window, so one
  logical save (temp-file write, rename, metadata touch) does not enqueue
  multiple rebuilds.
- A change to a project's `.git/HEAD` is special-cased to
  `IndexTrigger::branch_switch` (forcing the existing full-rescan path) instead
  of being folded into the affected-path set.
- A project noticed for the first time (added to the catalog, or seen on
  process start) has no baseline generation yet, so the watcher also issues one
  `request_rebuild` for it immediately, before relying on live events.
- Every 30 minutes per project, absent any real event, the watcher also issues
  an `IndexTrigger::periodic` full rescan as a safety net against an OS
  notification silently dropped (queue overflow, watch removed under load).
- `test/tests.cpp`'s `test_phase_fifteen_project_watcher` exercises this
  end-to-end on Windows with no HTTP layer involved: a real `ProjectWatcher`
  observing a real directory discovers a newly cataloged project on its own,
  picks up a real file save as an affected-path `watcher` update, and picks up
  a real `.git/HEAD` rewrite as a `branch_switch` full rescan.

## Representative large-project ceiling run (2026-07-31)

The `masterai index-probe <project-root> [index-root]` CLI command (registered
in `src/main.cpp`, alongside `performance-probe`) drives the real
`ProjectIndexer` used by the running service's worker thread through one full
rebuild and one single-file incremental update over an actual directory tree,
and reports elapsed time, disk bytes, and this process's own resident-memory
delta/peak (sampled every 50&nbsp;ms during the run) so results are directly
comparable to the configured memory ceiling.

Two runs on the Windows x64 Debug build, Release/`balanced` default memory
policy (6.29&nbsp;GiB hard limit):

| Project | Files | Source bytes | Rebuild | Incremental update | Peak resident | Hard limit |
|---:|---:|---:|---:|---:|---:|---:|
| `src/` (this repository) | 35 | ~250&nbsp;KB | 106&nbsp;ms | 24&nbsp;ms | 9.7&nbsp;MiB | 6.29&nbsp;GiB |
| Synthetic 10,000-file tree (200 directories &times; 50 files, ~41&nbsp;MiB) | 10,000 | ~41&nbsp;MiB | 11.9&nbsp;s | 861&nbsp;ms | 72.7&nbsp;MiB | 6.29&nbsp;GiB |

The synthetic tree stands in for a large real project as a controlled,
reproducible stand-in (this repository itself is not yet large enough to
stress the ceiling). Rebuild throughput was roughly 840 files/s (~3.4&nbsp;MiB/s)
using the existing portable synchronous reader; peak resident memory stayed
two orders of magnitude below the configured hard limit at 10,000 files,
confirming the bounded design holds with headroom at this scale. The
incremental update's cost (861&nbsp;ms for one changed file out of 10,000)
comes from `ProjectIndexer::update` copying the full existing chunk vector
before splicing in the change — expected given the current in-memory
publish-then-swap design, and still well inside the ceiling.

## Platform-specific I/O decision (2026-07-31)

Given the measured throughput above, the portable, synchronous,
worker-bounded file reader already in `ProjectIndexer::rebuild`/`update` meets
the representative ceiling with wide headroom. Per the project's own
measured-optimization rule, a Windows overlapped-I/O or Linux `io_uring` file
reader is **not** adopted at this time — profiling has not shown a bottleneck
it would fix. This decision should be revisited only if a real deployment's
measured indexing time or memory profile regresses against the evidence
above; the portable reader remains the correctness baseline either way.

## Remaining exit evidence

Phase 15's two exit criteria (a representative large-project ceiling run, and
restart/corrupt-segment/partial-publication/cancellation correctness) are now
both backed by passing evidence above and in `test/tests.cpp`. One deliverable
remains open as a forward-looking enhancement rather than a blocker:

- Language-aware symbol extraction beyond exact identifier-boundary lookup
  (the current lookup already prevents `token` from matching `tokenizer` for
  C-family identifiers, which covers this project's own primary language).
