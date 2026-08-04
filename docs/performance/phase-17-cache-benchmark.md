# Phase 17 representative cache benchmark

`test_phase_seventeen_security_partitioned_cache` builds a representative
64-file project index and runs 32 identical authorized retrieval preparations.
`benchmark_retrieval_cache` compares fresh `RetrievalPlanner` work with the
production `CacheManager` plus retrieval serializer path.

The benchmark requires byte-identical context, strategy, and disclosure count,
and requires total cached preparation time to be lower than uncached time. It
uses the same user, project, policy, query, version, and index-generation key
fields as the live server, so the result cannot bypass Phase 17's security or
staleness partitioning.

