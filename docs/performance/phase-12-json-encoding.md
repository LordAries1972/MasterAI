# Phase 12 control-plane JSON encoding evidence

Date: 2026-07-29  
Host target: Windows x86-64 Release  
Compiler: Microsoft Visual C++ 19.38.33145  
Probe: `masterai performance-probe 300000`  
Samples: five baseline runs and five optimized runs; table values are medians.

The fixed probe measures the canonical JSON string encoder with a small
protocol value and a 16 KiB source-like value. Strict JSON object parsing is an
unchanged control. Every run produced checksum `92736000`.

| Sample | Baseline ns/op | Optimized ns/op | Latency reduction | Baseline MiB/s | Optimized MiB/s | Throughput multiple |
|---|---:|---:|---:|---:|---:|---:|
| `json-string-small` | 9,524.203 | 335.000 | 96.48% | 4.606 | 130.952 | 28.43x |
| `json-string-16k` | 2,124,529.433 | 79,818.667 | 96.24% | 7.355 | 195.756 | 26.62x |
| `json-parse-object` control | 3,432.643 | 3,067.282 | not optimized | 21.393 | 23.941 | observational only |

The accepted change replaces `std::ostringstream` formatting and repeated
growth with one reserved `std::string` and direct equivalent escape emission.
The functional suite checks the exact quote, slash, short-control, and
`\u0001` output, while MCP/IDE tests exercise downstream protocol use.

No buffer pool, streaming transport, prompt-prefix cache, batching/context
reuse, alternate IPC, or hardware adapter change was made. This probe did not
measure those paths, so changing them would violate Phase 12's measurement-first
rule. They require representative live inference/IDE workloads and separate
before/after evidence.
