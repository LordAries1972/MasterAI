// Phase 33 (LOCAL-ONLY slice, 2026-08-13): local multi-runner orchestration.
// See LocalRunnerConfig/RunnerSelectionSignals/LocalRunnerPool's class
// comments in masterai.hpp for the scope of what this file does and
// deliberately does not do (the optional intranet/mTLS worker protocol,
// which remains Planned -- see docs/PLAN.md Phase 33).
#include "masterai.hpp"
#include "json.hpp"

#include <mutex>
#include <stdexcept>

namespace masterai {

// A retry is only ever safe when nothing from the previous attempt has
// already reached the caller and nothing has already been durably
// persisted from it -- either one being true means resending could
// duplicate a response the user (or a durable store) has already
// received. See docs/PLAN.md Phase 33 exit criterion "retries occur only
// when semantically safe and never duplicate a persisted response."
bool retry_is_semantically_safe(const bool any_bytes_already_emitted_to_caller,
                                const bool request_already_marked_persisted) {
    return !any_bytes_already_emitted_to_caller &&
           !request_already_marked_persisted;
}

// One pool member: its declared configuration, its own independent
// RunnerSupervisor (own process, own mutex -- exactly the same isolation
// the single default `inference` supervisor already has), and a small
// failure-tracking counter LocalRunnerPool uses to exclude a repeatedly
// failing runner from routing without ever touching any other entry.
struct LocalRunnerPool::Entry {
    LocalRunnerConfig config;
    std::unique_ptr<RunnerSupervisor> supervisor;
    // Guards consecutive_failures/healthy below. Never held while a
    // RunnerSupervisor call is in flight -- RunnerSupervisor already
    // serializes its own callers with its own mutex, so this lock is only
    // ever taken to read/update the small bookkeeping fields themselves.
    mutable std::mutex health_mutex;
    std::uint64_t consecutive_failures{0U};
    bool healthy{true};

    void record_success() {
        std::lock_guard<std::mutex> lock(health_mutex);
        consecutive_failures = 0U;
        healthy = true;
    }

    void record_failure() {
        std::lock_guard<std::mutex> lock(health_mutex);
        ++consecutive_failures;
        if (consecutive_failures >= LocalRunnerPool::kMaxConsecutiveFailures) {
            healthy = false;
        }
    }

    bool is_healthy() const {
        std::lock_guard<std::mutex> lock(health_mutex);
        return healthy;
    }

    std::uint64_t failure_count() const {
        std::lock_guard<std::mutex> lock(health_mutex);
        return consecutive_failures;
    }
};

LocalRunnerPool::LocalRunnerPool(std::vector<LocalRunnerConfig> runners,
                                 std::filesystem::path approved_backend,
                                 std::filesystem::path runtime_root) {
    std::set<std::string> seen_ids;
    std::set<std::uint16_t> seen_ports;
    for (auto& config : runners) {
        if (config.id.empty() || !seen_ids.insert(config.id).second) {
            throw std::invalid_argument(
                "local runner pool entries must have unique, non-empty ids");
        }
        if (config.port == 0U || !seen_ports.insert(config.port).second) {
            throw std::invalid_argument(
                "local runner pool entries must have unique, non-zero ports");
        }
        auto entry = std::make_unique<Entry>();
        entry->config = config;
        // Each runner gets its own runtime-root subdirectory (logs, etc.)
        // so two runners loading the same model id concurrently never race
        // over the same log file the way they would under a single shared
        // runtime_root (see RunnerSupervisor::load()'s per-model-id log
        // path in inference.cpp).
        entry->supervisor = std::make_unique<RunnerSupervisor>(
            approved_backend, runtime_root / "runner_pool" / config.id);
        entries_.push_back(std::move(entry));
    }
}

LocalRunnerPool::~LocalRunnerPool() = default;

bool LocalRunnerPool::empty() const noexcept { return entries_.empty(); }

LocalRunnerPool::Entry& LocalRunnerPool::required(const std::string& runner_id) {
    for (auto& entry : entries_) {
        if (entry->config.id == runner_id) return *entry;
    }
    throw std::runtime_error("unknown local runner pool id: " + runner_id);
}

const LocalRunnerPool::Entry& LocalRunnerPool::required(
    const std::string& runner_id) const {
    for (const auto& entry : entries_) {
        if (entry->config.id == runner_id) return *entry;
    }
    throw std::runtime_error("unknown local runner pool id: " + runner_id);
}

// Routing rule: authorization and capability are hard filters (a request
// never lands on a runner it is not authorized for or that cannot serve
// its capability, no matter how otherwise attractive that runner looks),
// then health excludes any runner past kMaxConsecutiveFailures, then the
// remaining candidates are scored -- a resident-model match (avoids an
// unnecessary reload) dominates, then an idle/ready runner beats a busy
// one (queue-depth proxy: RunnerMetrics has no separate queue counter, but
// RunnerState::busy already means "this runner's one admitted generation
// slot is occupied", which is the real signal available in this pass),
// then higher configured priority wins ties.
std::optional<std::string> LocalRunnerPool::select_runner(
    const RunnerSelectionSignals& signals) const {
    const Entry* best = nullptr;
    int best_score = -1;
    for (const auto& entry : entries_) {
        const auto& config = entry->config;
        // Hard filter: project-bound authorization (Phase 2 pattern reused,
        // not reinvented -- see McpIdentity::project_ids /
        // IdeIntegrationService's authorized_project_ids parameter for the
        // equivalent existing convention). Empty means unrestricted.
        if (!config.authorized_project_ids.empty() &&
            !signals.project_id.empty() &&
            config.authorized_project_ids.count(signals.project_id) == 0U) {
            continue;
        }
        // Hard filter: capability. Empty on the runner means "generalist".
        if (!signals.required_capability.empty() &&
            !config.capabilities.empty() &&
            config.capabilities.count(signals.required_capability) == 0U) {
            continue;
        }
        if (!entry->is_healthy()) continue;

        const auto snapshot = entry->supervisor->metrics();
        int score = 0;
        if (!signals.model_id.empty() && snapshot.model_id == signals.model_id &&
            (snapshot.state == RunnerState::ready ||
             snapshot.state == RunnerState::busy)) {
            score += 1000;
        }
        if (snapshot.state == RunnerState::ready) {
            score += 200;
        } else if (snapshot.state == RunnerState::unloaded) {
            score += 100;  // cold but immediately loadable, better than busy
        } else if (snapshot.state == RunnerState::busy) {
            score += 10;   // still usable once its one slot frees up
        }
        // else starting/stopping/failed: score stays at 0, only chosen if
        // nothing else qualifies.
        score += static_cast<int>(config.priority);

        if (score > best_score) {
            best_score = score;
            best = entry.get();
        }
    }
    if (best == nullptr) return std::nullopt;
    return best->config.id;
}

void LocalRunnerPool::ensure_model_loaded(
    const std::string& runner_id, const ModelRecord& model,
    const unsigned int context_length,
    const std::uint32_t startup_timeout_seconds,
    const unsigned int parallel_slots, const LaunchTuning& tuning) {
    auto& entry = required(runner_id);
    try {
        const auto current = entry.supervisor->metrics();
        if ((current.state == RunnerState::ready ||
             current.state == RunnerState::busy) &&
            current.model_id == model.manifest.id) {
            entry.record_success();
            return;
        }
        if (current.state == RunnerState::ready ||
            current.state == RunnerState::failed) {
            entry.supervisor->unload();
        }
        entry.supervisor->load(model, context_length, entry.config.port,
                               startup_timeout_seconds, parallel_slots, tuning,
                               entry.config.accelerator_policy);
        entry.record_success();
    } catch (...) {
        // Failure isolation: a load failure on this runner is recorded
        // against this entry only -- every other entry's state and every
        // in-flight call against them is untouched -- and re-thrown so the
        // caller (server.cpp) can fall back to another runner or the
        // default single-runner path rather than the control plane ever
        // treating this as fatal.
        entry.record_failure();
        throw;
    }
}

GenerationResult LocalRunnerPool::generate(
    const std::string& runner_id, const std::string& prompt,
    const GenerationOptions& options,
    const std::function<void(const std::string&)>& on_chunk,
    const std::atomic_bool& cancellation,
    const std::uint32_t stall_timeout_seconds) {
    auto& entry = required(runner_id);
    // Tracks whether any byte of this generation has already been handed
    // to the caller's own on_chunk -- the exact fact
    // retry_is_semantically_safe() needs, captured directly at the source
    // instead of inferred after the fact.
    bool any_emitted = false;
    const auto tracking_chunk = [&](const std::string& chunk) {
        any_emitted = true;
        on_chunk(chunk);
    };
    try {
        auto result = entry.supervisor->generate(prompt, options, tracking_chunk,
                                                  cancellation, stall_timeout_seconds);
        entry.record_success();
        return result;
    } catch (const std::exception& failure) {
        entry.record_failure();
        throw RunnerGenerationFailure(
            std::string("runner \"") + runner_id + "\" failed: " + failure.what(),
            runner_id, any_emitted);
    }
}

EmbeddingResult LocalRunnerPool::embed(const std::string& runner_id,
                                       const std::string& text) {
    auto& entry = required(runner_id);
    try {
        auto result = entry.supervisor->embed(text);
        entry.record_success();
        return result;
    } catch (...) {
        // Embedding calls are never partial (see the class-comment note on
        // embed() in masterai.hpp), so no idempotency wrapper is needed
        // here -- a caller may always safely retry on a different runner.
        entry.record_failure();
        throw;
    }
}

RunnerSupervisor& LocalRunnerPool::runner(const std::string& runner_id) {
    return *required(runner_id).supervisor;
}

RunnerMetrics LocalRunnerPool::metrics(const std::string& runner_id) const {
    return required(runner_id).supervisor->metrics();
}

bool LocalRunnerPool::healthy(const std::string& runner_id) const {
    return required(runner_id).is_healthy();
}

std::vector<RunnerPoolEntrySnapshot> LocalRunnerPool::status() const {
    std::vector<RunnerPoolEntrySnapshot> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        RunnerPoolEntrySnapshot snapshot;
        snapshot.id = entry->config.id;
        snapshot.metrics = entry->supervisor->metrics();
        snapshot.capabilities = entry->config.capabilities;
        snapshot.authorized_project_ids = entry->config.authorized_project_ids;
        snapshot.priority = entry->config.priority;
        snapshot.healthy = entry->is_healthy();
        snapshot.consecutive_failures = entry->failure_count();
        result.push_back(std::move(snapshot));
    }
    return result;
}

std::string LocalRunnerPool::status_json(
    const std::vector<RunnerPoolEntrySnapshot>& entries) {
    std::string body = "{\"runners\":[";
    bool first = true;
    for (const auto& entry : entries) {
        if (!first) body += ",";
        first = false;
        std::string capabilities = "[";
        bool first_capability = true;
        for (const auto& capability : entry.capabilities) {
            if (!first_capability) capabilities += ",";
            first_capability = false;
            capabilities += json_string(capability);
        }
        capabilities += "]";
        std::string projects = "[";
        bool first_project = true;
        for (const auto& project : entry.authorized_project_ids) {
            if (!first_project) projects += ",";
            first_project = false;
            projects += json_string(project);
        }
        projects += "]";
        body += "{\"id\":" + json_string(entry.id) +
               ",\"state\":" + json_string(to_string(entry.metrics.warm_state)) +
               ",\"modelId\":" + json_string(entry.metrics.model_id) +
               ",\"residentMemoryBytes\":" +
               std::to_string(entry.metrics.resident_memory_bytes) +
               ",\"requestsCompleted\":" +
               std::to_string(entry.metrics.requests_completed) +
               ",\"capabilities\":" + capabilities +
               ",\"authorizedProjectIds\":" + projects +
               ",\"priority\":" + std::to_string(entry.priority) +
               ",\"healthy\":" + (entry.healthy ? "true" : "false") +
               ",\"consecutiveFailures\":" +
               std::to_string(entry.consecutive_failures) + "}";
    }
    return body + "]}";
}

}  // namespace masterai
