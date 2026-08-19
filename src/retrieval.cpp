// Phase 16/24: deadline-bound hybrid project retrieval, adaptive query
// planning, and reference-first evidence assembly.
//
// This unit turns a chat query into a small, ranked, explainable evidence
// set drawn from Phase 15's disk-backed project index, instead of ever
// injecting an entire project into a prompt. It owns strategy selection,
// bounded-parallel execution under a hard deadline, fusion/deduplication,
// and context budgeting. It never makes an authorization decision itself --
// callers must already have checked the caller's access to `project` before
// calling RetrievalPlanner::retrieve(), so a membership or policy change
// invalidates access on the very next call (nothing here caches identity;
// Phase 24's in-flight join table below only ever joins genuinely
// concurrent, byte-identical requests and is emptied before retrieve()
// returns, never a standing cache).
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

// Phase 24: extracts whitespace-delimited tokens that look like a file path
// (contain a path separator, or a '.' followed by a short known-ish
// extension-shaped suffix) -- the filename/path-match strategy's probes.
// Deliberately conservative: a bare "." or "/" alone does not count, since
// that would fire on ordinary prose punctuation.
std::vector<std::string> path_like_tokens(const std::string& text,
                                          const std::size_t maximum_tokens) {
    std::vector<std::string> tokens;
    std::set<std::string> seen;
    std::string current;
    const auto flush = [&]() {
        if (current.size() < 3U) { current.clear(); return; }
        const bool has_separator =
            current.find('/') != std::string::npos ||
            current.find('\\') != std::string::npos;
        const auto dot = current.find_last_of('.');
        const bool has_extension_shape =
            dot != std::string::npos && dot > 0U &&
            current.size() - dot >= 2U && current.size() - dot <= 6U;
        if ((has_separator || has_extension_shape) &&
            seen.insert(current).second) {
            tokens.push_back(current);
        }
        current.clear();
    };
    for (const char character : text) {
        if (std::isspace(static_cast<unsigned char>(character)) != 0) {
            flush();
        } else {
            current.push_back(character);
        }
    }
    flush();
    if (tokens.size() > maximum_tokens) tokens.resize(maximum_tokens);
    return tokens;
}

std::string to_lower_copy(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

// Phase 24: cheap, deterministic "does this look like a diagnostic" check --
// compiler/linker error shapes, exception names, or explicit error/warning
// wording. No parsing of an actual diagnostic format, just shape/keyword
// signals, matching the rest of this classifier's non-ML design.
bool looks_like_diagnostic(const std::string& lower_text) {
    static const char* const markers[] = {
        "error:", "error c", "warning:", "exception", "stack trace",
        "undefined reference", "segmentation fault", "traceback",
        "unhandled exception", "assertion failed", "fatal error"};
    for (const auto* marker : markers) {
        if (lower_text.find(marker) != std::string::npos) return true;
    }
    return false;
}

bool looks_like_documentation_request(const std::string& lower_text) {
    static const char* const markers[] = {
        "document", "readme", "how do i use", "how to use", "usage",
        "what does this project do"};
    for (const auto* marker : markers) {
        if (lower_text.find(marker) != std::string::npos) return true;
    }
    return false;
}

// Phase 24: distinguishes an actual code-symbol-shaped token (snake_case,
// camelCase, or containing a digit -- the shapes real identifiers take)
// from an ordinary English word that also happens to be 3+ characters, so
// the classifier's final fallback does not misfire on plain prose.
bool looks_like_code_identifier(const std::string& token) {
    if (token.find('_') != std::string::npos) return true;
    bool has_upper = false;
    bool has_lower = false;
    bool has_digit = false;
    for (const char character : token) {
        if (std::isupper(static_cast<unsigned char>(character)) != 0) has_upper = true;
        if (std::islower(static_cast<unsigned char>(character)) != 0) has_lower = true;
        if (std::isdigit(static_cast<unsigned char>(character)) != 0) has_digit = true;
    }
    return has_digit || (has_upper && has_lower);
}

bool looks_like_completion_request(const std::string& lower_text) {
    static const char* const markers[] = {
        "complete ", "continue ", "finish ", "autocomplete", "keep writing"};
    for (const auto* marker : markers) {
        if (lower_text.find(marker) != std::string::npos) return true;
    }
    return false;
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

// Phase 24: priority -> worker-count mapping used by every staged fan-out
// step below. Interactive requests get this codebase's existing bounded
// worker count (2); background/speculative requests get 1 so they cannot
// starve an interactive request sharing the same small thread budget --
// this is the "added priority tagging" the plan calls for, implemented by
// parametrizing DeadlineTaskPool's existing maximum_workers argument rather
// than building a second pool implementation.
std::size_t workers_for(const RetrievalPriority priority,
                        const std::size_t interactive_workers) noexcept {
    return priority == RetrievalPriority::interactive ? interactive_workers
                                                       : 1U;
}

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

// Phase 24: request-key the in-flight join table dedups on. Every field the
// plan requires ("auth+project+policy-generation+settings identical") is
// concatenated with a separator that cannot appear inside any one field's
// natural content (queries are already validated as UTF-8 text elsewhere;
// 0x1f is a control character no legitimate query text will contain), so
// two requests can only ever collide here if every one of these fields is
// byte-identical -- in particular requester_id and policy_generation, which
// is what guarantees joining never crosses an authorization boundary.
// Phase 85: retrieval-result cache key. CacheCategory::retrieval_result and
// its (de)serialization already existed (see cache.cpp's
// benchmark_retrieval_cache(), which proved the round trip) but nothing on
// the real request path ever wrote or read it -- every call re-ran the full
// multi-stage scan even for a byte-identical repeated query against an
// unchanged index. index_generation is folded in exactly like every other
// CacheKey consumer, so a project reindex is an automatic miss with no
// explicit invalidation call needed; version_tag folds in the parts of the
// request that can change the outcome (priority and the three optional-
// strategy enable flags) but aren't already covered by canonical_identity/
// content_digest, so two requests can only share a cached entry when both
// would genuinely compute the same thing. Distinct from request_key() above
// (the in-flight join key): that one joins only genuinely concurrent
// byte-identical calls and is emptied before retrieve() returns; this one
// backs a standing cache that serves repeated *sequential* calls too.
CacheKey retrieval_cache_key(const RetrievalRequest& request,
                             const std::string& trimmed_query,
                             const std::uint64_t index_generation) {
    CacheKey key;
    key.user_id = request.requester_id;
    key.project_id = request.project.id;
    key.policy_generation = request.policy_generation;
    key.canonical_identity = trimmed_query;
    key.content_digest = sha256_hex(trimmed_query);
    key.version_tag =
        std::string("retrieval-v1|") +
        (request.priority == RetrievalPriority::interactive ? "i" : "b") +
        (request.semantic_embedding_enabled ? "1" : "0") +
        (request.mcp_resource_enabled ? "1" : "0") +
        (request.git_diff_enabled ? "1" : "0") + "|" +
        std::to_string(request.maximum_context_bytes) + "|" +
        std::to_string(request.maximum_chunks_per_source) + "|" +
        std::to_string(request.maximum_total_chunks);
    key.index_generation = index_generation;
    return key;
}

// Phase 85: a cached outcome is only ever safe to serve again when every
// strategy that contributed to it is purely a function of the indexed
// project state at `index_generation` -- mcp_resource (external server
// content), git_diff (working-tree state, which can change without any
// index republication), and conversation_memory (grows with every new
// message) are all excluded from caching by inspecting which strategies
// actually contributed disclosed evidence, rather than trusting the
// request's enable flags alone (a stage can be "enabled" yet simply find
// nothing, which is still safe to cache).
bool retrieval_outcome_cache_eligible(const RetrievalOutcome& outcome) {
    for (const auto& entry : outcome.disclosure) {
        if (entry.source == "mcp_resource" || entry.source == "git_diff" ||
            entry.source == "conversation_memory") {
            return false;
        }
    }
    return true;
}

std::string request_key(const RetrievalRequest& request) {
    static constexpr char separator = '\x1f';
    std::string key = request.project.id;
    key += separator; key += request.requester_id;
    key += separator; key += std::to_string(request.policy_generation);
    key += separator; key += request.query_text;
    key += separator; key += std::to_string(request.deadline.count());
    key += separator; key += std::to_string(request.maximum_context_bytes);
    key += separator; key += std::to_string(request.maximum_chunks_per_source);
    key += separator; key += std::to_string(request.maximum_total_chunks);
    return key;
}

}  // namespace

std::string to_string(const RetrievalStrategy strategy) {
    switch (strategy) {
        case RetrievalStrategy::exact_symbol: return "exact_symbol";
        case RetrievalStrategy::exact_text: return "exact_text";
        case RetrievalStrategy::lexical: return "lexical";
        case RetrievalStrategy::filename_path: return "filename_path";
        case RetrievalStrategy::recent_change: return "recent_change";
        case RetrievalStrategy::semantic_embedding: return "semantic_embedding";
        case RetrievalStrategy::mcp_resource: return "mcp_resource";
        case RetrievalStrategy::call_graph: return "call_graph";
        case RetrievalStrategy::type_reference: return "type_reference";
        case RetrievalStrategy::git_diff: return "git_diff";
        case RetrievalStrategy::dependency_neighbour: return "dependency_neighbour";
        case RetrievalStrategy::conversation_memory: return "conversation_memory";
    }
    return "unknown";
}

// Phase 24: single source of truth for which strategies this *build* can
// execute. Every RetrievalStrategy now has a real adapter implementation
// (indexing.cpp's heuristic scans; git_retrieval.cpp; mcp_retrieval.cpp;
// semantic_retrieval.cpp; UserMemoryStore for conversation_memory), so this
// is unconditionally true for every entry -- it is deliberately kept as an
// explicit switch (rather than collapsing to `return true;`) so a future
// strategy the enum grows must be added here consciously, matching this
// function's original "no silent default" discipline. Whether a *specific*
// RetrievalPlanner instance actually runs semantic_embedding/mcp_resource/
// conversation_memory depends on optional constructor dependencies it may
// not have been given -- see retrieve_uncached()'s per-instance skip
// reasons below, which is a distinct, runtime question this function does
// not answer.
bool retrieval_strategy_has_adapter(const RetrievalStrategy strategy) noexcept {
    switch (strategy) {
        case RetrievalStrategy::exact_symbol:
        case RetrievalStrategy::exact_text:
        case RetrievalStrategy::lexical:
        case RetrievalStrategy::filename_path:
        case RetrievalStrategy::recent_change:
        case RetrievalStrategy::semantic_embedding:
        case RetrievalStrategy::mcp_resource:
        case RetrievalStrategy::call_graph:
        case RetrievalStrategy::type_reference:
        case RetrievalStrategy::git_diff:
        case RetrievalStrategy::dependency_neighbour:
        case RetrievalStrategy::conversation_memory:
            return true;
    }
    return false;
}

// After this pass every strategy has a build-level adapter, so this list is
// always empty -- kept (rather than removed) so RetrievalOutcome's
// disabled_strategy_reasons contract and every existing caller/test that
// reads it keep working unchanged; the per-instance runtime skip reasons
// retrieve_uncached() appends below now carry the honesty this static list
// used to.
const std::vector<std::pair<RetrievalStrategy, std::string>>&
disabled_retrieval_strategy_reasons() {
    static const std::vector<std::pair<RetrievalStrategy, std::string>> reasons;
    return reasons;
}

std::string to_string(const RetrievalRequestClassification classification) {
    switch (classification) {
        case RetrievalRequestClassification::completion: return "completion";
        case RetrievalRequestClassification::symbol_explanation:
            return "symbol_explanation";
        case RetrievalRequestClassification::navigation: return "navigation";
        case RetrievalRequestClassification::documentation:
            return "documentation";
        case RetrievalRequestClassification::generic_lexical:
            return "generic_lexical";
    }
    return "generic_lexical";
}

// Phase 24: small deterministic heuristic over query shape/keywords -- no
// model, no embeddings. Checked in priority order: an explicit file path
// is the strongest, least ambiguous signal (navigation); a diagnostic-
// looking string is the next strongest (the user almost certainly wants an
// error/symbol explained); then explicit documentation/completion wording;
// then a bare identifier-shaped token defaults to "explain this symbol";
// anything else is generic lexical search.
RetrievalRequestClassification classify_retrieval_request(
    const std::string& query_text) {
    if (!path_like_tokens(query_text, 1U).empty()) {
        return RetrievalRequestClassification::navigation;
    }
    const auto lower = to_lower_copy(query_text);
    if (looks_like_diagnostic(lower)) {
        return RetrievalRequestClassification::symbol_explanation;
    }
    if (looks_like_documentation_request(lower)) {
        return RetrievalRequestClassification::documentation;
    }
    if (looks_like_completion_request(lower)) {
        return RetrievalRequestClassification::completion;
    }
    // A code-symbol-shaped token (snake_case/camelCase/contains a digit) is
    // the "bare identifier" signal; an ordinary 3+ letter English word (as
    // plain identifier_tokens() alone would match) does not count, so
    // everyday prose falls through to generic lexical search instead of
    // being misclassified as "explain this symbol".
    for (const auto& token : identifier_tokens(query_text, 6U)) {
        if (looks_like_code_identifier(token)) {
            return RetrievalRequestClassification::symbol_explanation;
        }
    }
    return RetrievalRequestClassification::generic_lexical;
}

RetrievalPlanner::RetrievalPlanner(ProjectIndexService& indexes,
                                   MemoryBudgetManager* memory,
                                   UserMemoryStore* user_memory,
                                   RunnerSupervisor* embedding_runner,
                                   CacheManager* embedding_cache,
                                   McpOutboundRegistry* mcp_registry,
                                   McpOutboundGateway* mcp_gateway)
    : indexes_(indexes), memory_(memory), user_memory_(user_memory),
      embedding_runner_(embedding_runner), embedding_cache_(embedding_cache),
      mcp_registry_(mcp_registry), mcp_gateway_(mcp_gateway) {}

std::uint64_t RetrievalPlanner::uncached_invocation_count() const noexcept {
    return uncached_invocations_.load(std::memory_order_relaxed);
}

// Phase 24: joins genuinely concurrent identical requests instead of
// duplicating the work retrieve_uncached() below performs. The leader (the
// caller that finds no entry for this key) runs the real computation and
// publishes it to every follower that arrived while it was in flight; the
// entry is erased before the leader returns, so two sequential calls (even
// back-to-back with an identical request) never join -- only genuinely
// overlapping concurrent calls do. request_key() folds requester_id and
// policy_generation into the key precisely so two callers can never share
// a result unless they are also the same authorized identity under the
// same policy generation.
RetrievalOutcome RetrievalPlanner::retrieve(
    const RetrievalRequest& request) const {
    const auto key = request_key(request);
    auto state = std::make_shared<InflightRetrieval>();
    bool leader = false;
    {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        const auto found = inflight_.find(key);
        if (found != inflight_.end()) {
            state = found->second;
            ++state->followers;
        } else {
            leader = true;
            inflight_.emplace(key, state);
        }
    }
    if (!leader) {
        std::unique_lock<std::mutex> lock(state->mutex);
        state->ready.wait(lock, [&state] { return state->completed; });
        if (state->exception) std::rethrow_exception(state->exception);
        return *state->outcome;
    }

    uncached_invocations_.fetch_add(1U, std::memory_order_relaxed);
    RetrievalOutcome outcome;
    try {
        outcome = retrieve_uncached(request);
    } catch (...) {
        bool publish = false;
        {
            std::lock_guard<std::mutex> lock(inflight_mutex_);
            const auto found = inflight_.find(key);
            publish = found != inflight_.end() && found->second->followers > 0U;
            inflight_.erase(key);
        }
        if (publish) {
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                state->exception = std::current_exception();
                state->completed = true;
            }
            state->ready.notify_all();
        }
        throw;
    }
    bool publish = false;
    {
        std::lock_guard<std::mutex> lock(inflight_mutex_);
        const auto found = inflight_.find(key);
        publish = found != inflight_.end() && found->second->followers > 0U;
        inflight_.erase(key);
    }
    // The overwhelmingly common single-caller path avoids constructing a
    // promise/shared-future state or copying a full RetrievalOutcome into a
    // result nobody observes. Concurrent followers wait on this one small
    // condition-variable state and receive the same immutable result.
    if (publish) {
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            state->outcome = outcome;
            state->completed = true;
        }
        state->ready.notify_all();
    }
    return outcome;
}

RetrievalEvaluationReport evaluate_retrieval_quality(
    ProjectIndexService& indexes, const RetrievalPlanner& planner,
    RetrievalRequest request,
    const std::vector<RetrievalEvaluationCase>& cases) {
    RetrievalEvaluationReport report;
    report.cases = cases.size();
    for (const auto& item : cases) {
        request.query_text = item.query_text;
        const auto started = std::chrono::steady_clock::now();
        const auto hybrid = planner.retrieve(request);
        const auto elapsed = std::chrono::steady_clock::now() - started;
        const auto hybrid_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(elapsed)
                .count());
        report.hybrid_total_microseconds += hybrid_microseconds;
        report.hybrid_highest_case_microseconds = std::max(
            report.hybrid_highest_case_microseconds, hybrid_microseconds);
        if (elapsed > request.deadline + std::chrono::milliseconds(100)) {
            ++report.deadline_violations;
        }
        if (hybrid.context_text.size() > request.maximum_context_bytes + 4096U) {
            ++report.context_budget_violations;
        }
        bool hybrid_hit =
            hybrid.context_text.find(item.expected_context_marker) !=
            std::string::npos;
        if (hybrid_hit) {
            hybrid_hit = std::any_of(
                hybrid.disclosure.begin(), hybrid.disclosure.end(),
                [&](const RetrievalDisclosureEntry& entry) {
                    return entry.included &&
                           entry.relative_path == item.expected_relative_path;
                });
        }
        if (hybrid_hit) ++report.hybrid_hits;

        // Faithful Phase 16 strategy baseline: before Phase 24's staged
        // sticky sufficiency, identifier, exact-phrase, and per-token
        // lexical probes all ran for the request. The result quality check
        // remains the original literal full-query baseline below, while the
        // elapsed time includes the complete old fan-out workload.
        const auto baseline_started = std::chrono::steady_clock::now();
        const auto baseline_tokens = identifier_tokens(item.query_text, 6U);
        std::map<std::string, IndexChunk> baseline_fused;
        std::mutex baseline_mutex;
        const auto merge_baseline = [&](const IndexSearchResult& result) {
            std::lock_guard<std::mutex> lock(baseline_mutex);
            for (const auto& chunk : result.chunks) {
                baseline_fused.emplace(chunk.id, chunk);
            }
        };
        std::vector<std::function<void()>> baseline_tasks;
        for (const auto& token : baseline_tokens) {
            baseline_tasks.push_back([&, token] {
                merge_baseline(indexes.search_symbol(
                    request.project.id, token,
                    static_cast<std::size_t>(request.maximum_total_chunks)));
            });
        }
        IndexSearchResult baseline;
        baseline_tasks.push_back([&] {
            baseline = indexes.search_text(
                request.project.id, item.query_text,
                static_cast<std::size_t>(request.maximum_total_chunks));
            merge_baseline(baseline);
        });
        for (const auto& token : baseline_tokens) {
            baseline_tasks.push_back([&, token] {
                merge_baseline(indexes.search_text(
                    request.project.id, token,
                    static_cast<std::size_t>(request.maximum_total_chunks)));
            });
        }
        // Phase 16 dispatched its independent probes through this same
        // bounded two-worker shape. Include scheduling/join cost so the
        // comparison is end-to-end rather than raw search calls versus a
        // complete Phase 24 planner invocation.
        DeadlineTaskPool baseline_pool(
            std::move(baseline_tasks),
            baseline_started + request.deadline, 2U);
        // Phase 16 also fused duplicate chunks and materialized an admitted
        // context, so include that work rather than timing raw searches
        // against Phase 24's complete RetrievalOutcome construction.
        std::string baseline_context;
        std::map<std::string, std::uint64_t> baseline_per_source;
        std::uint64_t baseline_chunks = 0U;
        for (const auto& [id, chunk] : baseline_fused) {
            static_cast<void>(id);
            auto& source_count = baseline_per_source[chunk.relative_path];
            const std::string framed =
                "\n\n[Project context: " + chunk.relative_path + " @" +
                std::to_string(chunk.offset) + "]\n" + chunk.text;
            if (baseline_chunks >= request.maximum_total_chunks ||
                source_count >= request.maximum_chunks_per_source ||
                baseline_context.size() + framed.size() >
                    request.maximum_context_bytes) {
                continue;
            }
            baseline_context += framed;
            ++source_count;
            ++baseline_chunks;
        }
        volatile std::size_t baseline_sink = baseline_context.size();
        static_cast<void>(baseline_sink);
        const auto baseline_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - baseline_started).count());
        report.phase_sixteen_total_microseconds += baseline_microseconds;
        report.phase_sixteen_highest_case_microseconds = std::max(
            report.phase_sixteen_highest_case_microseconds,
            baseline_microseconds);
        const bool baseline_hit = std::any_of(
            baseline.chunks.begin(), baseline.chunks.end(),
            [&](const IndexChunk& chunk) {
                return chunk.relative_path == item.expected_relative_path &&
                       chunk.text.find(item.expected_context_marker) !=
                           std::string::npos;
            });
        if (baseline_hit) ++report.full_text_hits;
    }
    report.latency_improved = report.cases > 0U &&
                              report.hybrid_total_microseconds <
                                  report.phase_sixteen_total_microseconds &&
                              report.hybrid_highest_case_microseconds <
                                  report.phase_sixteen_highest_case_microseconds;
    report.improved = report.cases > 0U &&
                      report.hybrid_hits > report.full_text_hits &&
                      report.deadline_violations == 0U &&
                      report.context_budget_violations == 0U;
    return report;
}

// Chooses the least expensive sufficient strategy across an explicit staged
// list: stage 1 (interactive priority) is exact symbol matches on
// identifier-shaped tokens, the cheapest, highest-precision probe; stage 2
// (interactive) adds exact literal text and filename/path matches if stage
// 1 alone was not sufficient; stage 3 (background priority, since it is the
// most expensive and least precise of the enabled strategies) adds
// per-token lexical union if still short. Sufficiency is sticky: once any
// stage actually finds evidence, later stages are skipped entirely. A final
// cheap recency boost (recent-change) always runs over whatever was fused,
// when reached, before ranking -- it never introduces new candidates, only
// reorders existing ones toward recently-edited files.
RetrievalOutcome RetrievalPlanner::retrieve_uncached(
    const RetrievalRequest& request) const {
    const auto deadline =
        std::chrono::steady_clock::now() + request.deadline;
    const std::string trimmed_query = [&request]() {
        std::string text = request.query_text;
        if (text.size() > 512U) text.resize(512U);
        return text;
    }();

    const auto classification = classify_retrieval_request(trimmed_query);
    // Phase 24: per-instance runtime skip reasons -- distinct from (and in
    // addition to) disabled_retrieval_strategy_reasons()'s now-always-empty
    // build-capability list. Populated below whenever a candidate-generating
    // stage is skipped because this particular RetrievalPlanner instance
    // wasn't constructed with the optional dependency that strategy needs.
    std::vector<std::string> runtime_skip_reasons;
    const auto stamp_common = [&](RetrievalOutcome outcome) {
        outcome.classification = to_string(classification);
        for (const auto& entry : disabled_retrieval_strategy_reasons()) {
            outcome.disabled_strategy_reasons.push_back(entry.second);
        }
        for (const auto& reason : runtime_skip_reasons) {
            outcome.disabled_strategy_reasons.push_back(reason);
        }
        return outcome;
    };

    if (trimmed_query.size() < 3U) {
        return stamp_common(ContextBudgeter::apply(
            {}, BufferView{}, "none", false,
            "query too short for retrieval", 0U, 0U, 0U));
    }

    // Phase 85: retrieval-result cache. index_generation is looked up once,
    // up front (a cheap in-memory status read, not a scan), so both the
    // lookup below and finish()'s write at the bottom key off the exact same
    // "as of" index snapshot. No cache backing (embedding_cache_ == nullptr,
    // the common case for a test fixture or a planner instance that opted
    // out) or no published index yet for this project both mean caching is
    // simply skipped -- retrieve_uncached() behaves exactly as it did before
    // this change.
    std::optional<std::uint64_t> cache_index_generation;
    if (embedding_cache_ != nullptr) {
        if (const auto status = indexes_.status(request.project.id)) {
            cache_index_generation = status->index.generation;
        }
    }
    if (cache_index_generation.has_value()) {
        const auto cache_key = retrieval_cache_key(request, trimmed_query,
                                                    *cache_index_generation);
        if (const auto cached =
                embedding_cache_->get(CacheCategory::retrieval_result, cache_key)) {
            return deserialize_retrieval_outcome(*cached);
        }
    }
    // Only a complete (non-partial) outcome built entirely from strategies
    // that are pure functions of the indexed project state is ever written
    // back -- see retrieval_outcome_cache_eligible()'s rationale above.
    const auto finish = [&](RetrievalOutcome outcome) {
        if (cache_index_generation.has_value() && !outcome.partial &&
            retrieval_outcome_cache_eligible(outcome)) {
            const auto cache_key = retrieval_cache_key(request, trimmed_query,
                                                        *cache_index_generation);
            embedding_cache_->put(CacheCategory::retrieval_result, cache_key,
                                  serialize_retrieval_outcome(outcome));
        }
        return outcome;
    };

    const auto tokens = identifier_tokens(trimmed_query, 6U);
    const auto path_tokens = path_like_tokens(trimmed_query, 4U);
    const auto per_step_results = std::max<std::size_t>(
        4U, static_cast<std::size_t>(request.maximum_total_chunks));
    std::mutex mutex;
    // Phase 30: RetrievalCandidate objects for this call's fused evidence
    // set are pool-allocated (FixedSizePool) instead of living directly as
    // std::map values, so their combined footprint is a real, observable
    // bytes_reserved() number instead of scattered node allocations. When
    // this planner was constructed with a MemoryBudgetManager (memory_ !=
    // nullptr), on_reserved_bytes_changed below registers/re-registers a
    // lease against MemoryCategory::retrieval_index_cache every time the
    // pool actually grows or shrinks a block, so the accounting is live for
    // the duration of this call, not a one-time estimate. This is the one
    // FixedSizePool consumer Phase 30 wires end-to-end this session (see
    // docs/PLAN.md's Phase 30 status note for the scope call); it is a
    // best-effort accounting signal, not admission control -- a declined
    // reservation never blocks or fails retrieval. `pool_lease_guard`
    // releases whatever lease is outstanding on every return path
    // (including the early-return branches above/below) via RAII, since
    // this pool -- and its lease -- are scoped to this one call.
    const bool interactive_pool = request.priority == RetrievalPriority::interactive;
    // Retrieval usually stops after one exact hit. Grow one candidate at a
    // time so that fast path does not construct/reserve a 32-object block;
    // the pool remains bounded by maximum_total_chunks through fusion and
    // still reports every byte it actually reserves to MemoryBudgetManager.
    FixedSizePool<RetrievalCandidate> candidate_pool(1U);
    std::string candidate_pool_lease;
    if (memory_ != nullptr) {
        candidate_pool.on_reserved_bytes_changed =
            [this, &candidate_pool_lease, interactive_pool](std::size_t total_bytes) {
                if (!candidate_pool_lease.empty()) {
                    memory_->release(candidate_pool_lease);
                    candidate_pool_lease.clear();
                }
                if (total_bytes == 0U) return;
                MemoryEstimate estimate;
                estimate.transient_bytes = total_bytes;
                const auto admission = memory_->reserve(
                    MemoryCategory::retrieval_index_cache, estimate, interactive_pool);
                if (admission.admitted) candidate_pool_lease = admission.lease_id;
            };
    }
    struct PoolLeaseGuard final {
        MemoryBudgetManager* memory;
        std::string* lease_id;
        ~PoolLeaseGuard() {
            if (memory != nullptr && lease_id != nullptr && !lease_id->empty()) {
                memory->release(*lease_id);
                lease_id->clear();
            }
        }
    } pool_lease_guard{memory_, &candidate_pool_lease};

    std::map<std::string, RetrievalCandidate*> fused;
    // Phase 24: every unique chunk's bytes live exactly once, appended here
    // the first time its id is seen; RetrievalCandidate::reference then
    // addresses this arena instead of carrying its own copy. Corroborating
    // hits from a later strategy only bump the score (see the `else`
    // branch), never append a second copy of already-captured bytes.
    std::vector<std::uint8_t> arena;
    const auto merge = [&](const std::string& source,
                          const IndexSearchResult& result,
                          const double base_score, const std::string& reason) {
        if (!result.available) return;
        std::lock_guard<std::mutex> lock(mutex);
        for (const auto& chunk : result.chunks) {
            auto existing = fused.find(chunk.id);
            if (existing == fused.end()) {
                ChunkReference reference;
                reference.segment_key = "retrieval-arena";
                reference.offset = arena.size();
                reference.length = chunk.text.size();
                arena.insert(arena.end(), chunk.text.begin(), chunk.text.end());
                IndexChunk metadata = chunk;
                metadata.text.clear();  // bytes now live only in `arena`
                // Phase 30: acquire() from the pool above instead of
                // constructing a std::map value in place -- merge() already
                // holds `mutex` for its whole body, so single-threaded
                // access to the (not itself thread-safe) pool is guaranteed.
                RetrievalCandidate* candidate = candidate_pool.acquire();
                candidate->chunk = std::move(metadata);
                candidate->reference = reference;
                candidate->disclosure = make_entry(source, chunk, result.generation,
                                                   base_score, reason);
                fused.emplace(chunk.id, candidate);
            } else {
                // Evidence surfaced by more than one strategy is stronger,
                // not merely duplicated: add a bounded corroboration bonus
                // instead of double-counting the chunk.
                existing->second->disclosure.score += base_score * 0.25;
            }
        }
    };

    // Stage 1 (interactive): exact symbol matches. These are in-memory,
    // sub-millisecond index probes; creating/joining a worker pool costs
    // more than the lookup on small/medium indexes. Run them inline and
    // stop at the first sufficient hit. Broader lexical work below retains
    // bounded task parallelism where the per-task cost can amortize it.
    bool partial = false;
    for (const auto& token : tokens) {
        if (std::chrono::steady_clock::now() >= deadline) {
            partial = true;
            break;
        }
        merge("exact_symbol",
              indexes_.search_symbol(request.project.id, token,
                                     per_step_results),
              3.0, "identifier " + token + " matched by exact boundary");
        if (!fused.empty()) break;
    }

    std::string strategy = tokens.empty() ? "none" : "exact_symbol";
    // "Least expensive sufficient": exact symbol matches are the cheapest,
    // most precise strategy, so any hit at all is treated as sufficient and
    // broader (and more expensive) stages are skipped entirely.
    bool sufficient = !fused.empty();

    // Stage 2 (interactive): exact literal text, then filename/path match.
    if (!sufficient && std::chrono::steady_clock::now() < deadline) {
        merge("exact_text",
              indexes_.search_text(request.project.id, trimmed_query,
                                   per_step_results),
              2.5, "literal query text matched");
        strategy = strategy == "none" ? "exact_text" : "hybrid";
        sufficient = !fused.empty();
    }

    if (!sufficient && !path_tokens.empty() &&
        std::chrono::steady_clock::now() < deadline) {
        for (const auto& token : path_tokens) {
            if (std::chrono::steady_clock::now() >= deadline) {
                partial = true;
                break;
            }
            merge("filename_path",
                  indexes_.search_path(request.project.id, token,
                                       per_step_results),
                  2.0, "path fragment " + token + " matched");
            if (!fused.empty()) break;
        }
        strategy = strategy == "none" ? "filename_path" : "hybrid";
        sufficient = !fused.empty();
    }

    // Stage 3 (background priority: most expensive, least precise enabled
    // strategy): per-token lexical union.
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
        DeadlineTaskPool lexical_pool(
            std::move(lexical_tasks), deadline,
            workers_for(RetrievalPriority::background, 2U));
        partial = partial || !lexical_pool.all_completed();
        strategy = strategy == "none" ? "lexical" : "hybrid";
    }

    // Stage 4 (interactive: cheap, in-memory heuristic scans, same cost
    // class as Stage 1): call_graph then type_reference over the same
    // identifier-shaped tokens already extracted above.
    if (!sufficient && std::chrono::steady_clock::now() < deadline) {
        for (const auto& token : tokens) {
            if (std::chrono::steady_clock::now() >= deadline) {
                partial = true;
                break;
            }
            merge("call_graph",
                  indexes_.search_calls(request.project.id, token, per_step_results),
                  1.8, "call site of " + token + " matched");
            if (!fused.empty()) break;
        }
        strategy = strategy == "none" ? "call_graph" : "hybrid";
        sufficient = !fused.empty();
    }
    if (!sufficient && std::chrono::steady_clock::now() < deadline) {
        for (const auto& token : tokens) {
            if (std::chrono::steady_clock::now() >= deadline) {
                partial = true;
                break;
            }
            merge("type_reference",
                  indexes_.search_type_usage(request.project.id, token,
                                             per_step_results),
                  1.8, "type reference to " + token + " matched");
            if (!fused.empty()) break;
        }
        strategy = strategy == "none" ? "type_reference" : "hybrid";
        sufficient = !fused.empty();
    }

    // Stage 5 (background: the most expensive stage, a network round trip
    // per candidate chunk): semantic_embedding. Only runs when this planner
    // instance was constructed with an embedding runner.
    if (!sufficient && request.semantic_embedding_enabled &&
        std::chrono::steady_clock::now() < deadline) {
        if (embedding_runner_ != nullptr) {
            const auto candidates = indexes_.all_chunks(
                request.project.id,
                std::min<std::size_t>(2048U, per_step_results * 64U));
            if (candidates.available && !candidates.chunks.empty()) {
                auto semantic_chunks = semantic_embedding_search(
                    *embedding_runner_, embedding_cache_, request.project.id,
                    candidates.chunks, trimmed_query, per_step_results, deadline);
                merge("semantic_embedding",
                      IndexSearchResult{std::move(semantic_chunks),
                                        candidates.generation, true},
                      2.2, "semantic similarity matched");
                strategy = strategy == "none" ? "semantic_embedding" : "hybrid";
                sufficient = !fused.empty();
            }
        } else {
            runtime_skip_reasons.push_back(
                "semantic_embedding: no embedding runner configured for this "
                "planner instance");
        }
    }

    // Stage 6 (background, I/O-bound, best-effort): mcp_resource then
    // conversation_memory. A failure here degrades to "no evidence", never
    // a retrieval error.
    if (!sufficient && request.mcp_resource_enabled &&
        std::chrono::steady_clock::now() < deadline) {
        if (mcp_registry_ != nullptr && mcp_gateway_ != nullptr) {
            std::atomic_bool mcp_cancellation{false};
            auto mcp_chunks = mcp_resource_search(
                *mcp_registry_, *mcp_gateway_, request.requester_id,
                request.requester_scopes, request.project.id, tokens,
                per_step_results, mcp_cancellation);
            merge("mcp_resource",
                  IndexSearchResult{std::move(mcp_chunks), 0U, true}, 1.5,
                  "MCP resource content matched");
            strategy = strategy == "none" ? "mcp_resource" : "hybrid";
            sufficient = !fused.empty();
        } else {
            runtime_skip_reasons.push_back(
                "mcp_resource: no MCP outbound registry/gateway configured "
                "for this planner instance");
        }
    }
    if (!sufficient && std::chrono::steady_clock::now() < deadline) {
        if (user_memory_ != nullptr) {
            const auto recalled = user_memory_->recall_context(
                request.requester_id,
                static_cast<std::size_t>(request.maximum_context_bytes));
            if (!recalled.empty()) {
                IndexChunk memory_chunk;
                memory_chunk.relative_path = "conversation-memory";
                memory_chunk.language = "text";
                memory_chunk.offset = 0U;
                memory_chunk.text = recalled;
                memory_chunk.digest = sha256_hex(recalled);
                memory_chunk.id =
                    sha256_hex("conversation-memory:" + request.requester_id +
                              ":" + memory_chunk.digest);
                merge("conversation_memory",
                      IndexSearchResult{{std::move(memory_chunk)}, 0U, true},
                      1.5, "recorded user memory matched");
                strategy = strategy == "none" ? "conversation_memory" : "hybrid";
                sufficient = !fused.empty();
            }
        } else {
            runtime_skip_reasons.push_back(
                "conversation_memory: no user memory store configured for "
                "this planner instance");
        }
    }

    // Stage 7 (background, cheap sandboxed subprocess): git_diff. No
    // optional dependency to check -- silently empty when the project isn't
    // a git working tree or `git` can't be run (see git_diff_search).
    if (!sufficient && request.git_diff_enabled &&
        std::chrono::steady_clock::now() < deadline) {
        auto git_chunks =
            git_diff_search(request.project.root, per_step_results);
        merge("git_diff", IndexSearchResult{std::move(git_chunks), 0U, true},
              1.6, "uncommitted/recent change matched");
        strategy = strategy == "none" ? "git_diff" : "hybrid";
    }

    if (fused.empty()) {
        return finish(stamp_common(ContextBudgeter::apply(
            {}, BufferView{}, strategy, partial,
            partial ? "retrieval deadline expired before any evidence was found"
                    : "no matching project evidence found",
            0U, 0U, 0U)));
    }

    // Stage 8 (always runs post-fusion, like recent_change below, since it
    // only ever adds corroborating evidence rather than gating sufficiency):
    // dependency_neighbour. Anchored on whichever already-fused chunk
    // currently scores highest.
    {
        const RetrievalCandidate* anchor = nullptr;
        for (const auto& entry : fused) {
            if (anchor == nullptr ||
                entry.second->disclosure.score > anchor->disclosure.score) {
                anchor = entry.second;
            }
        }
        if (anchor != nullptr &&
            std::chrono::steady_clock::now() < deadline) {
            merge("dependency_neighbour",
                  indexes_.search_dependency_neighbours(
                      request.project.id, anchor->chunk.relative_path,
                      per_step_results),
                  1.2, "dependency neighbour of " + anchor->chunk.relative_path);
        }
    }

    // Remaining enabled strategy (recent_change): a cheap post-fusion
    // recency boost over whatever was already found, reusing the mtime the
    // filesystem already tracks for every project file -- no new git
    // integration, no per-chunk metadata added to the index. Runs whenever
    // fusion produced anything (it never introduces new candidates, so
    // sticky sufficiency does not gate it the way it gates candidate-
    // generating stages above).
    static constexpr auto recent_change_window = std::chrono::hours(24);
    const auto now = std::filesystem::file_time_type::clock::now();
    for (auto& entry : fused) {
        std::error_code error;
        const auto file_path = request.project.root / entry.second->chunk.relative_path;
        const auto written = std::filesystem::last_write_time(file_path, error);
        if (error) continue;
        const auto age = now - written;
        if (age >= decltype(age)::zero() && age <= recent_change_window) {
            entry.second->disclosure.score += 0.5;
            entry.second->disclosure.reason += "; recently edited file boosted";
        }
    }

    std::vector<RetrievalCandidate> ranked;
    ranked.reserve(fused.size());
    // Phase 30: moves each candidate's contents out of its pool slot -- the
    // pool slot itself (and, at function exit, the whole candidate_pool and
    // its lease) is torn down once `ranked` owns independent copies here,
    // matching the arena's own "this call's scratch, not a standing cache"
    // lifetime.
    for (auto& entry : fused) ranked.push_back(std::move(*entry.second));
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

    // The arena's bytes must outlive ContextBudgeter::apply()'s materialize
    // calls; building the SharedBuffer/BufferView here (after every merge()
    // call has finished appending) and handing it in by const reference
    // keeps that lifetime obviously correct without any extra copying.
    const auto arena_size = arena.size();
    SharedBuffer arena_buffer(std::move(arena));
    BufferView arena_view(arena_buffer, 0U, arena_size);

    return finish(stamp_common(ContextBudgeter::apply(
        std::move(ranked), arena_view, strategy, partial,
        partial ? "retrieval deadline expired" : "",
        request.maximum_context_bytes, request.maximum_chunks_per_source,
        request.maximum_total_chunks)));
}

// Accepts ranked candidates in order, capping per-source (per-file) chunk
// count, total chunk count, and total context bytes. The first candidate
// that would overflow a cap is omitted and every later one is omitted too
// (they are already ranked lower), never partially truncating a kept
// chunk's text to squeeze in more evidence. `segment` is materialized
// exactly once per admitted candidate, here -- ranking/fusion above never
// touches candidate bytes, only `reference.length` (a plain integer) for
// the byte-budget check.
RetrievalOutcome ContextBudgeter::apply(
    std::vector<RetrievalCandidate> ranked, const BufferView& segment,
    const std::string& strategy, const bool partial, std::string diagnostic,
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
        const auto chunk_bytes = candidate.reference.length;
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
        // The one and only materialize() call for this candidate's bytes.
        context += "\n\n[Project context: " + candidate->chunk.relative_path +
                   " @" + std::to_string(candidate->chunk.offset) +
                   " (index generation " +
                   std::to_string(candidate->disclosure.index_generation) +
                   ")]\n" + candidate->reference.materialize(segment);
    }
    outcome.context_text = std::move(context);
    return outcome;
}

}  // namespace masterai
