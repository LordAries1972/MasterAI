# Testing and validation

> Part of the [MasterAI README](../README.md).

Run the native correctness suite after building:

```powershell
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Debug
.\scripts\test.ps1 -Platform Windows-x64 -BuildType Release
```

```sh
sh ./scripts/test.sh Release Linux-x86_64
```

The test suite covers configuration, persistence and recovery, identity,
sessions and tokens, secrets, path security, model integrity and suitability,
runner isolation, projects/chats/attachments, downloads, benchmarks, MCP,
operations, query metrics, bounded memory, indexing, retrieval, caching,
scheduling, topology and routing decisions, and Machine Learning record,
permission, lifecycle, reload, and removal behavior.

Fixture and implementation tests do not replace external exit checks. Claims of
phase completion are governed by the evidence and exit criteria in
[docs/PLAN.md](PLAN.md).

Verify that project objectives have not changed without reassessment:

```powershell
.\scripts\verify-objectives.ps1
```

```sh
sh ./scripts/verify-objectives.sh
```
