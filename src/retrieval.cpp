// Phase 16: deadline-bound hybrid project retrieval.
//
// This unit turns a chat query into a small, ranked, explainable evidence
// set drawn from Phase 15's disk-backed project index, instead of ever
// injecting an entire project into a prompt. It owns strategy selection,
// bounded-parallel execution under a hard deadline, fusion/deduplication,
// and context budgeting. It never makes an authorization decision itself --
// callers must already have checked the caller's access to `project` before
// calling RetrievalPlanner::retrieve(), so a membership or policy change
// invalidates access on the very next call (nothing here caches identity).
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace masterai {
namespace {

bool identifier_character(const char character) noexcept {
    const auto value = static_cast<unsigned char>(character);
    return std::isalnum(value) != 0 || character == '_';
}

// Extracts distinct identifier-shaped tokens (3+ characters) from free text,
// longest first, bounded to a small count -- these are the cheap "exact
// symbol" strategy's candidate probes.
std::vector<std::string> identifier_tokens(const std::string& text,
                                           const std::size_t maximum_tokens) {
    std::vector<std::string> tokens;
    std::set<std::string> seen;
    std::string current;
    for (std::size_t index = 0U; index <= text.size(); ++index) {
        const bool boundary = index == text.size() ||
                              !identifier_character(text[index]);
        if (!boundary) {
            current.push_back(text[index]);
            continue;
        }
        if (current.size() >= 3U && current.size() <= 128U &&
            seen.insert(current).second) {
            tokens.push_back(current);
        }
        current.clear();
    }
    std::sort(tokens.begin(), tokens.end(),
             [](const std::string& left, const std::string& right) {
                 if (left.size() != right.size()) return left.size() > right.size();
                 return left < right;
             });
    if (tokens.size() > maximum_tokens) tokens.resize(maximum_tokens);
    return tokens;
}

// Runs independent tasks across a small bounded worker pool. Each worker
// checks the deadline before claiming its next task rather than mid-task,
// matching this codebase's established cooperative-cancellation shape (see
// mcp_outbound.cpp/inference.cpp). Tasks not yet claimed when the deadline
// passes are simply never started; run() reports whether every task ran.
class DeadlineTaskPool final {
public:
    DeadlineTaskPool(std::vector<std::function<void()>> tasks,
                     const std::chrono::steady_clock::time_point deadline,
                     const std::size_t maximum_workers)
        : tasks_(std::move(tasks)) {
        const auto worker_count = std::max<std::size_t>(
            1U, std::min(maximum_workers, tasks_.size()));
        std::vector<std::thread> workers;
        workers.reserve(worker_count);
        for (std::size_t index = 0U; index < worker_count; ++index) {
            workers.emplace_back([this, deadline]() { run(deadline); });
        }
        for (auto& worker : workers) worker.join();
    }

    bool all_completed() const noexcept {
        return next_.load() >= tasks_.size();
    }

private:
    void run(const std::chrono::steady_clock::time_point deadline) {
        while (true) {
            if (std::chrono::steady_clock::now() >= deadline) return;
            const auto index = next_.fetch_add(1U);
            if (index >= tasks_.size()) return;
            tasks_[index]();
        }
    }

    std::vector<std::function<void()>> tasks_;
    std::atomic<std::size_t> next_{0U};
};

RetrievalDisclosureEntry make_entry(const std::string& source,
                                    const IndexChunk& chunk,
                                    const std::uint64_t generation,
                                    const double score,
                                    std::string reason) {
    RetrievalDisclosureEntry entry;
    entry.source = source;
    entry.relative_path = chunk.relative_path;
    entry.offset = chunk.offset;
    entry.index_generation = generation;
    entry.score = score;
    entry.included = false;
    entry.reason = std::move(reason);
    return entry;
}

}  // namespace

RetrievalPlanner::RetrievalPlanner(ProjectIndexService& indexes)
    : indexes_(indexes) {}

// Chooses the least expensive sufficient strategy: try exact symbol
// matches on identifier-shaped tokens first (cheap, high precision); if
// those alone do not reach the requested chunk budget, add an exact literal
// match on the full query; if still short, add a per-token lexical union.
// Every step that actually ran contributes candidates keyed by the index's
// own canonical chunk identity, so overlap across steps collapses for free.
RetrievalOutcome RetrievalPlanner::retrieve(
    const RetrievalRequest& request) const {
    const auto deadline =
        std::chrono::steady_clock::now() + request.deadline;
    const std::string trimmed_query = [&request]() {
        std::string text = request.query_text;
        if (text.size() > 512U) text.resize(512U);
        return text;
    }();
    if (trimmed_query.size() < 3U) {
        return ContextBudgeter::apply({}, "none", false,
                                      "query too short for retrieval", 0U, 0U,
                                      0U);
    }

    const auto tokens = identifier_tokens(trimmed_query, 6U);
    const auto per_step_results = std::max<std::size_t>(
        4U, static_cast<std::size_t>(request.maximum_total_chunks));

    std::mutex mutex;
    std::map<std::string, RetrievalCandidate> fused;
    const auto merge = [&](const std::string& source,
                          const IndexSearchResult& result,
                          const double base_score, const std::string& reason) {
        if (!result.available) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& chunk : result.chunks) {
            auto existing = fused.find(chunk.id);
            if (existing == fused.end()) {
                fused.emplace(chunk.id,
                              RetrievalCandidate{
                                  chunk, make_entry(source, chunk,
                                                    result.generation,
                                                    base_score, reason)});
            } else {
                // Evidence surfaced by more than one strategy is stronger,
                // not merely duplicated: add a bounded corroboration bonus
                // instead of double-counting the chunk.
                existing->second.disclosure.score += base_score * 0.25;
            }
        }
    };

    std::vector<std::function<void()>> symbol_tasks;
    symbol_tasks.reserve(tokens.size());
    for (const auto& token : tokens) {
        symbol_tasks.push_back([&, token]() {
            merge("exact_symbol",
                  indexes_.search_symbol(request.project.id, token,
                                         per_step_results),
                  3.0, "identifier " + token + " matched by exact boundary");
        });
    }
    DeadlineTaskPool symbol_pool(std::move(symbol_tasks), deadline, 2U);
    bool partial = !symbol_pool.all_completed();

    std::string strategy = tokens.empty() ? "none" : "exact_symbol";
    // "Least expensive sufficient": exact symbol matches are the cheapest,
    // most precise strategy, so any hit at all is treated as sufficient and
    // broader (and more expensive) strategies are skipped entirely.
    const bool sufficient_after_symbols = !fused.empty();

    // Sufficiency is sticky: once any step actually found evidence, later
    // (more expensive) steps are skipped entirely rather than merely being
    // allowed to keep adding to a chunk budget that has not filled up yet.
    bool sufficient = sufficient_after_symbols;

    if (!sufficient && std::chrono::steady_clock::now() < deadline) {
        DeadlineTaskPool text_pool(
            {[&]() {
                merge("exact_text",
                      indexes_.search_text(request.project.id, trimmed_query,
                                           per_step_results),
                      2.5, "literal query text matched");
            }},
            deadline, 1U);
        partial = partial || !text_pool.all_completed();
        strategy = strategy == "none" ? "exact_text" : "hybrid";
        sufficient = !fused.empty();
    }

    if (!sufficient && std::chrono::steady_clock::now() < deadline &&
        tokens.size() > 1U) {
        std::vector<std::function<void()>> lexical_tasks;
        lexical_tasks.reserve(tokens.size());
        for (const auto& token : tokens) {
            lexical_tasks.push_back([&, token]() {
                merge("lexical",
                      indexes_.search_text(request.project.id, token,
                                           per_step_results),
                      1.0, "lexical token " + token + " matched");
            });
        }
        DeadlineTaskPool lexical_pool(std::move(lexical_tasks), deadline, 2U);
        partial = partial || !lexical_pool.all_completed();
        strategy = strategy == "none" ? "lexical" : "hybrid";
    }

    if (fused.empty()) {
        return ContextBudgeter::apply(
            {}, strategy, partial,
            partial ? "retrieval deadline expired before any evidence was found"
                    : "no matching project evidence found",
            0U, 0U, 0U);
    }

    std::vector<RetrievalCandidate> ranked;
    ranked.reserve(fused.size());
    for (auto& entry : fused) ranked.push_back(std::move(entry.second));
    std::sort(ranked.begin(), ranked.end(),
             [](const RetrievalCandidate& left, const RetrievalCandidate& right) {
                 if (left.disclosure.score != right.disclosure.score) {
                     return left.disclosure.score > right.disclosure.score;
                 }
                 if (left.chunk.relative_path != right.chunk.relative_path) {
                     return left.chunk.relative_path < right.chunk.relative_path;
                 }
                 return left.chunk.offset < right.chunk.offset;
             });

    return ContextBudgeter::apply(
        std::move(ranked), strategy, partial, partial ? "retrieval deadline expired" : "",
        request.maximum_context_bytes, request.maximum_chunks_per_source,
        request.maximum_total_chunks);
}

// Accepts ranked candidates in order, capping per-source (per-file) chunk
// count, total chunk count, and total context bytes. The first candidate
// that would overflow a cap is omitted and every later one is omitted too
// (they are already ranked lower), never partially truncating a kept
// chunk's text to squeeze in more evidence.
RetrievalOutcome ContextBudgeter::apply(
    std::vector<RetrievalCandidate> ranked, const std::string& strategy,
    const bool partial, std::string diagnostic,
    const std::uint64_t maximum_context_bytes,
    const std::uint64_t maximum_chunks_per_source,
    const std::uint64_t maximum_total_chunks) {
    RetrievalOutcome outcome;
    outcome.strategy = strategy;
    outcome.partial = partial;
    outcome.diagnostic = std::move(diagnostic);

    std::map<std::string, std::uint64_t> per_source_count;
    std::uint64_t total_chunks = 0U;
    std::uint64_t total_bytes = 0U;
    std::vector<RetrievalCandidate*> included;
    included.reserve(ranked.size());

    for (auto& candidate : ranked) {
        const auto chunk_bytes =
            static_cast<std::uint64_t>(candidate.chunk.text.size());
        auto& source_count = per_source_count[candidate.chunk.relative_path];
        const bool fits = total_chunks < maximum_total_chunks &&
                          source_count < maximum_chunks_per_source &&
                          total_bytes + chunk_bytes <= maximum_context_bytes;
        if (fits) {
            candidate.disclosure.included = true;
            ++source_count;
            ++total_chunks;
            total_bytes += chunk_bytes;
            included.push_back(&candidate);
        } else {
            candidate.disclosure.included = false;
            if (candidate.disclosure.reason.find("budget") == std::string::npos) {
                candidate.disclosure.reason += "; omitted by context budget";
            }
        }
        outcome.disclosure.push_back(candidate.disclosure);
    }

    std::sort(included.begin(), included.end(),
             [](const RetrievalCandidate* left, const RetrievalCandidate* right) {
                 if (left->chunk.relative_path != right->chunk.relative_path) {
                     return left->chunk.relative_path < right->chunk.relative_path;
                 }
                 return left->chunk.offset < right->chunk.offset;
             });

    std::string context;
    for (const auto* candidate : included) {
        context += "\n\n[Project context: " + candidate->chunk.relative_path +
                   " @" + std::to_string(candidate->chunk.offset) +
                   " (index generation " +
                   std::to_string(candidate->disclosure.index_generation) +
                   ")]\n" + candidate->chunk.text;
    }
    outcome.context_text = std::move(context);
    return outcome;
}

}  // namespace masterai
