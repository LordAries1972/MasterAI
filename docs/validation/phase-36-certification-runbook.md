# Phase 36 certification runbook

`src/regression_gate.cpp`'s `PerformanceCertificationRunner`/
`PerformanceCertificationStore` already measure every software-controllable
dimension for real on whatever host they run on: cold/warm cache state
(`CacheManager::trim()`), concurrency depth (sequential repetitions through
the real `RequestScheduler`), real queue wait, real disk-read bytes, real
CPU percent, real accelerator mode as actually used (not just requested),
and a five-group regression-check suite compared against the previous
*accepted* run sharing the exact same fingerprint (model/backend/hardware/
settings/prompt-suite/cache-state/profile).

What's left is physical, not software: the plan's full matrix additionally
spans multiple storage media (HDD/SATA SSD/NVMe), multiple physical
machines, and GPU-offloaded hardware a given host may not have. No single
host can manufacture those dimensions on demand, so closing them is an
administrator's per-real-host exercise, not a code gap.

## Running one certification on a host

Use `scripts/run-certification.ps1`:

```powershell
$sp = ConvertTo-SecureString "<password>" -AsPlainText -Force
scripts\run-certification.ps1 -Username "<admin-user>" -Password $sp `
    -ModelId "<model-id>" -ProjectId "<project-id>" -Profile quick
```

- `-ProjectId` is only needed the first time (or after a restart) to open a
  chat and send one message so the model is actually resident before
  certifying; omit it if you already know the model is loaded.
- Repeat with `-CacheState cold` for a cold-cache record, or a higher
  `-Concurrency` for a concurrency-scaled record — each becomes its own
  fingerprinted baseline the *next* matching run compares against.
- The script prints the full `PerformanceCertificationRecord` JSON,
  including the `comparisons` array against the previous accepted run of
  the same fingerprint (empty on the very first run for a given
  fingerprint, since there is nothing yet to compare against).

## Real evidence recorded so far (this host)

18 August 2026, Intel Core i7-6700HQ / NVIDIA GeForce GTX 960M 4 GiB,
`llama32-1b-instruct-q4km`, `quick` profile, `warm` cache, concurrency 1:
first run `accepted: true` with no prior baseline to compare against; a
second identical-fingerprint run then compared cleanly against it (all
seven regression comparisons passed, e.g. TTFT +2.7% against a 20%
threshold, generation tokens/sec -2.6% against a -10% floor).

## Extending to another physical host

1. Copy this repository (or just `build/`, `config/`, `models/`, and
   `scripts/`) to the target machine and build there
   (`scripts\build.ps1 -BuildType Release`).
2. Start the server (`scripts\start.ps1`) and run
   `scripts\run-certification.ps1` there with the same `-ModelId`/
   `-Profile`/`-CacheState` as a prior host's run, so the two records'
   `hardwareId` differs but everything else about the fingerprint lines up
   for a direct comparison.
3. Record the resulting `hardwareId`/`fingerprint`/`accepted` outcome
   alongside this file (or a dated addendum) the same way the "real
   evidence recorded so far" section above does, so the record stays
   auditable rather than only living in that host's own runtime database.

No part of this requires a code change — every axis above is already real
and wired; only the physical hosts to run it on are outside this session's
reach.
