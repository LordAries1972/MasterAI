# MasterAI Initial Rule-set

These rules apply to the `F:\Projects\C++\MasterAI` project and all work produced for it.

1. Project and produced code must use either Assembly or C++ Standard 17.
2. Save all documents in `docs/`.
3. Save all produced source code in `src/`.
4. Always save all LLM/AI models in `models/`.
5. Save test scripts, CMake compile scripts, and scripts used by the system in `scripts/`.
6. Generated code must always be strictly optimal and secure, using proper syntax and formatting, including `#define` and `#ifdef` directives.
7. Plan documentation is `docs/PLAN.md`. Use it with the Agent-Coder extension. It must contain the phases used to build and install the system and provide correct guidance for ongoing validation.
8. The Ultimanium.com local development web server is at `F:\Projects\web\effectualdesigns`, with these related files:
   - Online Help: `masterai-help.dat`
   - Master AI About page: `about-masterai.dat`. This is a sales page listing all features and reasons users should use Master AI; make it an exceptional sales pitch.
   - Main web navigation and access-verification script: `index.php`
9. Use Agent-Coder to its fullest. If a command is needed, ask the user or refer to `agent-coder-help.dat`.
10. Always teach Agent-Coder as work progresses so it learns and records information for this project, helping keep it on track, clean, and efficient.
11. Save any rules made for this project in the project `rules/` folder, never in `%USERPROFILE%` under Windows platform.
12. C++ must use strict conditional directives for Linux setup/version differences, and the applicable platform and build type must be specified. The relevant script will be added later.
13. Project objectives are in `docs/objectives.md` and will be updated often. Maintain a cached/hashed representation, verify whether the file changed, and reassess the project objectives whenever it does.
14. MasterAI must not use third-party coding foundations, frameworks, source libraries, or dependency-provided application foundations. Foundational implementation must be authored using ISO C++17 or approved Assembly and native operating-system APIs.
15. `llama.cpp` is the sole current exception to rule 14 and may be supported as an optional, process-isolated, replaceable inference backend behind a MasterAI C++17 adapter. It must not become the control-plane foundation.
16. MasterAI is an independent, ground-up system. Preserve room for the user's original architectural ideas rather than allowing inherited foundations to dictate the design. Optimization must be evidence-driven and must not weaken security or correctness.
17. Save all test cases under the project `test/` folder. Do not mix test implementations into `src/`.
18. Whenever the user requests a MasterAI memory upgrade or establishes a durable MasterAI rule, also save the actionable rule in this `rules/initial-ruleset.md` file so repository guidance and memory remain synchronized.
19. Keep `docs/PLAN.md`, implementation, validation evidence, and phase completion claims synchronized as work progresses. Inspect deliverables and exit criteria before reporting phase status; mark a phase complete immediately when its exit criteria are genuinely satisfied, otherwise record the exact remaining work.
20. Proactively own and correct stale or inaccurate project status. Research the actual source and validation state before reporting progress, and provide concise evidence-backed handoffs.
21. When a required tool or file is not at an expected location, inspect the active path and targeted likely installation locations, locate the exact file when needed, and configure against the confirmed path. Do not claim it is missing before this research, and avoid indiscriminate drive-wide recursive scans when narrow checks are sufficient.
22. Put an explanatory comment at the top of every source unit describing its responsibility and boundaries. Document the operational flow of every new or materially changed function so the user can understand its inputs, checks, state changes, and outputs. Keep comments current and focused on intent rather than narrating syntax.
23. Search the current source, scripts, tests, and documentation for an existing implementation before adding a new path. Do not create duplicated services, workflows, policies, helpers, scripts, or documentation. When duplication is detected, refactor it into one authoritative implementation and route callers through it while preserving behavior, security boundaries, tests, and unrelated user changes.
24. Complete outstanding phases in their documented sequential order while using Agent-Coder assistance and monitoring throughout the work. Mark each phase `Complete` immediately after every deliverable and real exit criterion has current validation evidence; never mark a phase complete from implementation alone. Reuse the existing authoritative implementation and refactor any duplication discovered instead of adding a parallel code path.
25. When MasterAI validation requires real model artifacts, approved testing models may be inspected and used from `F:\Projects\PhoenixAI\models\blobs`. Keep MasterAI manifests, runtime state, and any project-owned model artifacts in the MasterAI `models/` hierarchy; do not modify or relocate the shared PhoenixAI blobs as part of testing.
26. Update the root `README.md` whenever a change implements or materially changes project behavior, capabilities, requirements, commands, limitations, or validation status. Explain the change accurately and keep the README synchronized with the implementation and `docs/PLAN.md` so it never claims functionality or readiness that current evidence does not support.
