// Phase 24: semantic_embedding retrieval adapter. Real cosine-similarity
// search over chunk embeddings computed through the already-live
// RunnerSupervisor::embed() -> POST /v1/embeddings backend call
// (inference.cpp) -- no stub, no bag-of-words substitute. Per-chunk
// embeddings are cached (CacheCategory::embedding, already declared for
// exactly this purpose) keyed by chunk digest so a repeat query against an
// unchanged index generation never re-embeds the same chunk text twice; a
// caller with no CacheManager wired in still works, just re-embeds every
// call.
#include "masterai.hpp"

#include <atomic>
#include <cmath>
#include <mutex>
#include <sstream>
#include <thread>

namespace masterai {
namespace {

std::string serialize_embedding(const std::vector<double>& values) {
    std::ostringstream stream;
    stream.precision(17);
    for (std::size_t index = 0U; index < values.size(); ++index) {
        if (index != 0U) stream << ',';
        stream << values[index];
    }
    return stream.str();
}

std::vector<double> deserialize_embedding(const std::string& text) {
    std::vector<double> values;
    std::istringstream stream(text);
    std::string token;
    while (std::getline(stream, token, ',')) {
        if (token.empty()) continue;
        try {
            values.push_back(std::stod(token));
        } catch (const std::exception&) {
            return {};
        }
    }
    return values;
}

double cosine_similarity(const std::vector<double>& left,
                         const std::vector<double>& right) {
    if (left.empty() || right.empty() || left.size() != right.size()) {
        return 0.0;
    }
    double dot = 0.0;
    double left_norm = 0.0;
    double right_norm = 0.0;
    for (std::size_t index = 0U; index < left.size(); ++index) {
        dot += left[index] * right[index];
        left_norm += left[index] * left[index];
        right_norm += right[index] * right[index];
    }
    if (left_norm <= 0.0 || right_norm <= 0.0) return 0.0;
    return dot / (std::sqrt(left_norm) * std::sqrt(right_norm));
}

}  // namespace

std::vector<IndexChunk> semantic_embedding_search(
    RunnerSupervisor& embedding_runner, CacheManager* cache,
    const std::string& project_id, const std::vector<IndexChunk>& candidates,
    const std::string& query_text, const std::size_t maximum_results,
    const std::chrono::steady_clock::time_point deadline) {
    std::vector<IndexChunk> results;
    if (maximum_results == 0U || candidates.empty() || query_text.empty()) {
        return results;
    }

    std::vector<double> query_embedding;
    try {
        query_embedding = embedding_runner.embed(query_text).values;
    } catch (const std::exception&) {
        // No embedding model available for this call -- semantic search
        // stays "no evidence" rather than an error, matching every other
        // best-effort strategy.
        return results;
    }
    if (query_embedding.empty()) return results;

    struct Scored {
        double score;
        const IndexChunk* chunk;
    };
    std::vector<Scored> scored;
    scored.reserve(candidates.size());
    std::mutex scored_mutex;
    std::atomic<std::size_t> next_candidate{0U};
    // Phase 85: each candidate's embed()/cache round trip is independent of
    // every other candidate's -- embed() opens its own socket per call (see
    // local_http() in inference.cpp), so it carries no shared mutable state
    // that would make concurrent calls unsafe, and CacheManager is already
    // shared safely across this codebase's other concurrent retrieval
    // strategies (the lexical fan-out in retrieval.cpp). Dispatching this
    // loop across a small bounded worker pool instead of one HTTP round
    // trip at a time lets those round trips overlap. Only `scored`'s
    // push_back needs its own lock; everything else a worker touches is
    // either already thread-safe or purely local to that worker.
    const auto worker = [&]() {
        while (true) {
            if (std::chrono::steady_clock::now() >= deadline) return;
            const auto index = next_candidate.fetch_add(1U);
            if (index >= candidates.size()) return;
            const auto& chunk = candidates[index];

            std::vector<double> chunk_embedding;
            CacheKey key;
            key.project_id = project_id;
            key.canonical_identity = "semantic-embedding:" + chunk.id;
            key.content_digest = chunk.digest;
            key.version_tag = "phase24-embedding-v1";
            const bool have_cache = cache != nullptr;
            if (have_cache) {
                if (const auto cached = cache->get(CacheCategory::embedding, key)) {
                    chunk_embedding = deserialize_embedding(*cached);
                }
            }
            if (chunk_embedding.empty()) {
                try {
                    chunk_embedding = embedding_runner.embed(chunk.text).values;
                } catch (const std::exception&) {
                    continue;
                }
                if (chunk_embedding.empty()) continue;
                if (have_cache) {
                    cache->put(CacheCategory::embedding, key,
                              serialize_embedding(chunk_embedding));
                }
            }

            const auto score = cosine_similarity(query_embedding, chunk_embedding);
            if (score <= 0.0) continue;
            std::lock_guard<std::mutex> lock(scored_mutex);
            scored.push_back({score, &chunk});
        }
    };
    const auto worker_count = std::max<std::size_t>(
        1U,
        std::min<std::size_t>(
            {candidates.size(), 8U,
             static_cast<std::size_t>(std::max(1U, std::thread::hardware_concurrency()))}));
    if (worker_count <= 1U) {
        worker();
    } else {
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t index = 0U; index < worker_count; ++index) {
            workers.emplace_back(worker);
        }
        for (auto& thread : workers) thread.join();
    }

    std::sort(scored.begin(), scored.end(),
             [](const Scored& left, const Scored& right) {
                 return left.score > right.score;
             });
    for (const auto& entry : scored) {
        if (results.size() == maximum_results) break;
        results.push_back(*entry.chunk);
    }
    return results;
}

}  // namespace masterai
