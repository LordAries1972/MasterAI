// MasterAI system-wide bounded-memory admission and pressure governance.
//
// This unit is the only authority that creates runtime memory reservations.
// It accounts categories, preserves an OS reserve, publishes deterministic
// pressure actions, and returns actionable denial without allocating payloads.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <limits>
#include <stdexcept>
#include <thread>
#include <utility>

namespace masterai {
namespace {

std::uint64_t sweeper_epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

constexpr std::uint64_t mib = 1024ULL * 1024ULL;

const char* pressure_name(const MemoryPressure pressure) noexcept {
    switch (pressure) {
        case MemoryPressure::normal: return "normal";
        case MemoryPressure::elevated: return "elevated";
        case MemoryPressure::high: return "high";
        case MemoryPressure::critical: return "critical";
    }
    return "critical";
}

const char* category_name(const MemoryCategory category) noexcept {
    switch (category) {
        case MemoryCategory::control_plane: return "controlPlane";
        case MemoryCategory::runner_weights: return "runnerWeights";
        case MemoryCategory::compute_buffers: return "computeBuffers";
        case MemoryCategory::kv_cache: return "kvCache";
        case MemoryCategory::prompt_cache: return "promptCache";
        case MemoryCategory::retrieval_index_cache: return "retrievalIndexCache";
        case MemoryCategory::file_content: return "fileContent";
        case MemoryCategory::attachments: return "attachments";
        case MemoryCategory::downloads: return "downloads";
        case MemoryCategory::background_jobs: return "backgroundJobs";
    }
    return "unknown";
}

std::uint64_t checked_add(const std::uint64_t left,
                          const std::uint64_t right) noexcept {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        return std::numeric_limits<std::uint64_t>::max();
    }
    return left + right;
}

std::vector<std::string> actions_for(const MemoryPressure pressure) {
    std::vector<std::string> actions;
    if (pressure >= MemoryPressure::elevated) {
        actions.emplace_back("stop_prefetch");
        actions.emplace_back("throttle_indexing");
    }
    if (pressure >= MemoryPressure::high) {
        actions.emplace_back("trim_low_value_caches");
        actions.emplace_back("reject_non_interactive_work");
        actions.emplace_back("reduce_context_and_concurrency");
    }
    if (pressure >= MemoryPressure::critical) {
        actions.emplace_back("unload_idle_embedding_runner");
        actions.emplace_back("unload_idle_generation_runner");
        actions.emplace_back("reject_new_inference");
    }
    return actions;
}

}  // namespace

std::uint64_t MemoryEstimate::total_bytes() const noexcept {
    auto total = checked_add(weights_bytes, runtime_buffer_bytes);
    const auto sequences_bytes =
        sequences != 0U &&
                kv_bytes_per_sequence >
                    std::numeric_limits<std::uint64_t>::max() / sequences
            ? std::numeric_limits<std::uint64_t>::max()
            : kv_bytes_per_sequence * sequences;
    total = checked_add(total, sequences_bytes);
    total = checked_add(total, transient_bytes);
    return checked_add(total, safety_margin_bytes);
}

class MemoryBudgetManager::State final {
public:
    struct Lease {
        MemoryCategory category{MemoryCategory::control_plane};
        std::uint64_t bytes{0};
        bool interactive{false};
    };

    MemoryPolicy policy;
    HardwareInfo hardware;
    mutable std::mutex mutex;
    std::map<std::string, Lease> leases;
    MemoryStatus current;
};

MemoryBudgetManager::MemoryBudgetManager(MemoryPolicy policy,
                                         HardwareInfo hardware)
    : state_(std::make_unique<State>()) {
    if (hardware.total_ram_mib == 0U) {
        throw std::invalid_argument("memory manager requires host RAM evidence");
    }
    if (policy.hard_limit_bytes == 0U) {
        policy = policy_for(ResourceProfile::balanced, hardware);
    }
    if (policy.hard_limit_bytes < 256U * mib ||
        policy.minimum_free_percent > 50U ||
        policy.elevated_percent >= policy.high_percent ||
        policy.high_percent >= policy.critical_percent ||
        policy.critical_percent > 99U ||
        policy.maximum_active_inference == 0U ||
        policy.maximum_queued_inference == 0U ||
        policy.maximum_index_workers == 0U) {
        throw std::invalid_argument("memory policy violates bounded limits");
    }
    state_->policy = policy;
    state_->hardware = std::move(hardware);
    state_->current.hard_limit_bytes = policy.hard_limit_bytes;
    state_->current.available_physical_bytes =
        state_->hardware.available_ram_mib * mib;
}

MemoryBudgetManager::~MemoryBudgetManager() = default;

// Calculates the complete estimate before mutating accounting. Admission fails
// closed on arithmetic overflow, pressure policy, hard limit, or OS reserve.
MemoryAdmission MemoryBudgetManager::reserve(
    const MemoryCategory category, const MemoryEstimate& estimate,
    const bool interactive) {
    const auto requested = estimate.total_bytes();
    MemoryAdmission result;
    result.reserved_bytes = requested;
    if (requested == 0U ||
        requested == std::numeric_limits<std::uint64_t>::max()) {
        result.diagnostic = "memory estimate is zero or overflowed";
        result.corrective_actions = {"reduce_context", "use_smaller_model"};
        return result;
    }

    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto current = state_->current.reserved_bytes;
    const auto after = checked_add(current, requested);
    const auto os_safe_available =
        state_->current.available_physical_bytes >
                state_->policy.minimum_os_reserve_bytes
            ? state_->current.available_physical_bytes -
                  state_->policy.minimum_os_reserve_bytes
            : 0U;
    if (state_->current.pressure == MemoryPressure::critical ||
        (!interactive && state_->current.pressure >= MemoryPressure::high) ||
        after > state_->policy.hard_limit_bytes ||
        requested > os_safe_available) {
        result.diagnostic = kMemoryAdmissionResourcePressureDiagnostic;
        result.corrective_actions = {
            "reduce_context", "reduce_concurrency", "use_smaller_model",
            "unload_idle_model"};
        return result;
    }
    const auto random = secure_random(16U);
    result.lease_id = sha256_hex(
        std::string(random.begin(), random.end()) +
        std::to_string(state_->leases.size())).substr(0U, 32U);
    state_->leases.emplace(
        result.lease_id, State::Lease{category, requested, interactive});
    state_->current.reserved_bytes = after;
    state_->current.category_bytes[category] += requested;
    result.admitted = true;
    return result;
}

void MemoryBudgetManager::release(const std::string& lease_id) noexcept {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->leases.find(lease_id);
    if (found == state_->leases.end()) return;
    state_->current.reserved_bytes -= found->second.bytes;
    auto& category = state_->current.category_bytes[found->second.category];
    category -= found->second.bytes;
    state_->leases.erase(found);
}

// Refreshes process and OS observations, derives pressure from the stricter of
// reservation usage and physical availability, then publishes ordered actions.
MemoryStatus MemoryBudgetManager::sample() {
    const auto process = probe_process_resources();
    const auto hardware = probe_hardware(std::filesystem::current_path());
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->current.observed_process_bytes =
        std::max(process.private_memory_bytes, process.resident_memory_bytes);
    state_->current.available_physical_bytes =
        hardware.available_ram_mib * mib;
    const auto used_percent = static_cast<unsigned int>(
        std::min<std::uint64_t>(
            100U, state_->current.reserved_bytes * 100U /
                      state_->policy.hard_limit_bytes));
    const auto free_percent = hardware.total_ram_mib == 0U
                                  ? 0U
                                  : static_cast<unsigned int>(
                                        hardware.available_ram_mib * 100U /
                                        hardware.total_ram_mib);
    if (used_percent >= state_->policy.critical_percent ||
        free_percent < state_->policy.minimum_free_percent) {
        state_->current.pressure = MemoryPressure::critical;
    } else if (used_percent >= state_->policy.high_percent) {
        state_->current.pressure = MemoryPressure::high;
    } else if (used_percent >= state_->policy.elevated_percent) {
        state_->current.pressure = MemoryPressure::elevated;
    } else {
        state_->current.pressure = MemoryPressure::normal;
    }
    state_->current.active_pressure_actions =
        actions_for(state_->current.pressure);
    return state_->current;
}

MemoryStatus MemoryBudgetManager::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->current;
}

bool MemoryBudgetManager::permits_background_work() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->current.pressure < MemoryPressure::high;
}

MemoryPolicy MemoryBudgetManager::policy() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->policy;
}

// Phase 34: reuses the exact bound checks the constructor already enforces
// -- see MemoryBudgetManager::MemoryBudgetManager() above -- so a policy
// applied here can never relax a safety ceiling the constructor would have
// refused to start with.
void MemoryBudgetManager::set_policy(MemoryPolicy policy) {
    if (policy.hard_limit_bytes < 256U * mib ||
        policy.minimum_free_percent > 50U ||
        policy.elevated_percent >= policy.high_percent ||
        policy.high_percent >= policy.critical_percent ||
        policy.critical_percent > 99U ||
        policy.maximum_active_inference == 0U ||
        policy.maximum_queued_inference == 0U ||
        policy.maximum_index_workers == 0U) {
        throw std::invalid_argument("memory policy violates bounded limits");
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->policy = std::move(policy);
    state_->current.hard_limit_bytes = state_->policy.hard_limit_bytes;
}

MemoryPolicy MemoryBudgetManager::policy_for(
    const ResourceProfile profile, const HardwareInfo& hardware,
    const std::uint64_t hard_limit_bytes) {
    MemoryPolicy policy;
    const auto total = hardware.total_ram_mib * mib;
    const auto reserve = std::max<std::uint64_t>(2ULL * 1024ULL * mib,
                                                total * 15U / 100U);
    policy.minimum_os_reserve_bytes = reserve;
    policy.hard_limit_bytes =
        hard_limit_bytes != 0U
            ? hard_limit_bytes
            : std::max<std::uint64_t>(256U * mib,
                                      total > reserve ? total - reserve
                                                      : total / 2U);
    if (profile == ResourceProfile::minimal) {
        policy.maximum_active_inference = 1U;
        policy.maximum_queued_inference = 4U;
        policy.maximum_index_workers = 1U;
        policy.default_context_tokens = 2048U;
        policy.keep_idle_model = false;
    } else if (profile == ResourceProfile::performance) {
        policy.maximum_active_inference = 2U;
        policy.maximum_queued_inference = 16U;
        policy.maximum_index_workers =
            std::max(1U, hardware.physical_cpu_count / 2U);
        policy.default_context_tokens = 8192U;
    }
    return policy;
}

std::string MemoryBudgetManager::to_json(const MemoryStatus& status) {
    std::string categories{"{"};
    bool first = true;
    for (const auto& item : status.category_bytes) {
        if (!first) categories += ',';
        first = false;
        categories += json_string(category_name(item.first)) + ":" +
                      std::to_string(item.second);
    }
    categories += "}";
    std::string actions{"["};
    for (std::size_t index = 0U;
         index < status.active_pressure_actions.size(); ++index) {
        if (index != 0U) actions += ',';
        actions += json_string(status.active_pressure_actions[index]);
    }
    actions += "]";
    return "{\"pressure\":" + json_string(pressure_name(status.pressure)) +
           ",\"hardLimitBytes\":" +
           std::to_string(status.hard_limit_bytes) +
           ",\"reservedBytes\":" +
           std::to_string(status.reserved_bytes) +
           ",\"observedProcessBytes\":" +
           std::to_string(status.observed_process_bytes) +
           ",\"availablePhysicalBytes\":" +
           std::to_string(status.available_physical_bytes) +
           ",\"categories\":" + categories +
           ",\"activePressureActions\":" + actions + "}";
}

namespace {

const char* memory_cleanup_state_name(const MemoryCleanupState state) noexcept {
    switch (state) {
        case MemoryCleanupState::idle: return "idle";
        case MemoryCleanupState::running: return "running";
        case MemoryCleanupState::complete: return "complete";
        case MemoryCleanupState::failed: return "failed";
    }
    return "idle";
}

}  // namespace

bool MemoryCleanupTracker::begin(std::string id, const unsigned int total_steps) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_.state == MemoryCleanupState::running) return false;
    current_ = MemoryCleanupResult{};
    current_.id = std::move(id);
    current_.state = MemoryCleanupState::running;
    total_steps_ = total_steps;
    completed_steps_ = 0U;
    return true;
}

void MemoryCleanupTracker::advance_step(std::string step_label) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (current_.state != MemoryCleanupState::running) return;
    completed_steps_ = std::min(completed_steps_ + 1U, total_steps_);
    current_.current_step = std::move(step_label);
    current_.percent = total_steps_ == 0U
                            ? 100U
                            : completed_steps_ * 100U / total_steps_;
}

void MemoryCleanupTracker::finish(MemoryCleanupResult partial) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto id = current_.id;
    current_ = std::move(partial);
    current_.id = id;
    current_.state = MemoryCleanupState::complete;
    current_.percent = 100U;
}

void MemoryCleanupTracker::fail(std::string diagnostic) {
    std::lock_guard<std::mutex> lock(mutex_);
    current_.state = MemoryCleanupState::failed;
    current_.diagnostic = std::move(diagnostic);
}

MemoryCleanupResult MemoryCleanupTracker::status() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_;
}

std::string memory_cleanup_result_json(const MemoryCleanupResult& result) {
    std::string privilege_denied{"["};
    for (std::size_t index = 0U; index < result.privilege_denied_steps.size();
         ++index) {
        if (index != 0U) privilege_denied += ',';
        privilege_denied += json_string(result.privilege_denied_steps[index]);
    }
    privilege_denied += "]";
    return "{\"id\":" + json_string(result.id) +
           ",\"state\":" + json_string(memory_cleanup_state_name(result.state)) +
           ",\"percent\":" + std::to_string(result.percent) +
           ",\"currentStep\":" + json_string(result.current_step) +
           ",\"cacheBytesFreed\":" + std::to_string(result.cache_bytes_freed) +
           ",\"scratchOrphansRemoved\":" +
           std::to_string(result.scratch_orphans_removed) +
           ",\"processWorkingSetBytesFreed\":" +
           std::to_string(result.process_working_set_bytes_freed) +
           ",\"modelUnloaded\":" + (result.model_unloaded ? "true" : "false") +
           ",\"otherProcessesTrimmed\":" +
           std::to_string(result.other_processes_trimmed) +
           ",\"modifiedPageListFlushed\":" +
           (result.modified_page_list_flushed ? "true" : "false") +
           ",\"standbyListPurged\":" +
           (result.standby_list_purged ? "true" : "false") +
           ",\"lowPriorityStandbyPurged\":" +
           (result.low_priority_standby_purged ? "true" : "false") +
           ",\"systemWorkingSetsEmptied\":" +
           std::to_string(result.system_working_sets_emptied) +
           ",\"systemFileCacheCleared\":" +
           (result.system_file_cache_cleared ? "true" : "false") +
           ",\"privilegeDeniedSteps\":" + privilege_denied +
           ",\"diagnostic\":" + json_string(result.diagnostic) + "}";
}

BoundedWorkQueue::BoundedWorkQueue(
    const std::size_t maximum_items,
    const std::uint64_t maximum_payload_bytes)
    : maximum_items_(maximum_items),
      maximum_payload_bytes_(maximum_payload_bytes) {
    if (maximum_items == 0U || maximum_items > 65536U ||
        maximum_payload_bytes == 0U) {
        throw std::invalid_argument("work queue bounds are invalid");
    }
}

// Admits metadata only while enforcing item and aggregate payload ceilings.
// Interactive entries retain FIFO order within their priority tier.
bool BoundedWorkQueue::enqueue(const std::string& id,
                               const std::uint64_t payload_bytes,
                               const bool interactive) {
    if (id.empty() || payload_bytes == 0U) return false;
    std::lock_guard<std::mutex> lock(mutex_);
    if (queued_.size() + active_.size() >= maximum_items_ ||
        payload_bytes > maximum_payload_bytes_ - payload_bytes_) {
        return false;
    }
    const Item item{id, payload_bytes, interactive};
    const auto position = interactive
                              ? std::find_if(
                                    queued_.begin(), queued_.end(),
                                    [](const Item& queued) {
                                        return !queued.interactive;
                                    })
                              : queued_.end();
    queued_.insert(position, item);
    payload_bytes_ += payload_bytes;
    return true;
}

std::optional<std::string> BoundedWorkQueue::begin_next() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queued_.empty()) return std::nullopt;
    const auto item = queued_.front();
    queued_.erase(queued_.begin());
    active_.emplace(item.id, item.bytes);
    return item.id;
}

void BoundedWorkQueue::complete(const std::string& id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = active_.find(id);
    if (found == active_.end()) return;
    payload_bytes_ -= found->second;
    active_.erase(found);
}

void BoundedWorkQueue::cancel_all() noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    queued_.clear();
    active_.clear();
    payload_bytes_ = 0U;
}

std::size_t BoundedWorkQueue::queued() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queued_.size();
}

std::size_t BoundedWorkQueue::active() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return active_.size();
}

std::uint64_t BoundedWorkQueue::payload_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return payload_bytes_;
}

class MemorySweeper::State final {
public:
    State(MemoryBudgetManager& memory, RunnerSupervisor* inference,
         CacheManager* cache, PromptSessionManager* prompt_sessions,
         std::uint32_t idle_unload_seconds,
         std::function<void()> on_idle_unload,
         LocalRunnerPool* runner_pool)
        : memory_(memory), inference_(inference), cache_(cache),
          prompt_sessions_(prompt_sessions),
          on_idle_unload_(std::move(on_idle_unload)),
          runner_pool_(runner_pool),
          worker_([this]() { run(); }) {
        // Phase 34 (full completion pass): seeds the live policy's
        // idle_unload_seconds from this constructor argument once, at
        // startup -- the resource-profile-derived default server.cpp
        // computes (see runner_idle_unload_seconds). From here on this is a
        // live field AdaptiveController can adjust via set_policy(); this
        // sweeper always reads the current policy, never this argument
        // again.
        auto seeded_policy = memory_.policy();
        seeded_policy.idle_unload_seconds = idle_unload_seconds;
        memory_.set_policy(seeded_policy);
    }

    ~State() {
        stopping_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

private:
    // Polling granularity: fine enough that apply_idle_timeout() (whose own
    // threshold is idle_unload_seconds, typically minutes) fires within a
    // few seconds of actually going idle, without spinning.
    static constexpr std::chrono::milliseconds kPollInterval{2000};

    // "no persistent idle runner by default" (docs/PLAN.md Phase 30A): under
    // a profile that already asked for no idle model retention
    // (keep_idle_model=false -- minimal/cpu_only-shaped profiles), the idle
    // timeout unloads unconditionally. Under a profile that does want to
    // keep a warm idle model (balanced/performance default), the same
    // timeout still applies once real memory pressure appears, so an idle
    // runner never outlives actual contention for the RAM it's holding.
    void apply_idle_unload(const MemoryPolicy& policy,
                           const MemoryStatus& status) {
        if (policy.keep_idle_model && status.pressure < MemoryPressure::elevated) {
            return;
        }
        // Phase 34 (full completion pass): read live -- AdaptiveController
        // can now shrink this under memory pressure via MemoryBudgetManager::
        // set_policy(), and this sweeper must see that change on its very
        // next poll, not only at construction time.
        if (inference_ != nullptr &&
            inference_->apply_idle_timeout(sweeper_epoch_seconds(),
                                           policy.idle_unload_seconds)) {
            inference_->unload();
            if (prompt_sessions_ != nullptr) prompt_sessions_->reset();
            if (on_idle_unload_) on_idle_unload_();
        }
        apply_pool_idle_unload(policy);
    }

    // Phase 29/26: a tiering cascade can leave more than one pool runner
    // warm (the smaller tier that answered plus, on escalation, the larger
    // one) -- without this, nothing ever idle-unloads them, since the
    // default `inference_` supervisor above is a completely separate
    // instance from any LocalRunnerPool entry. Each pool runner is checked
    // and unloaded independently, exactly like the default supervisor just
    // above, using the same live policy.idle_unload_seconds threshold (no
    // per-tier override exists this pass -- every pool runner shares one
    // policy).
    void apply_pool_idle_unload(const MemoryPolicy& policy) {
        if (runner_pool_ == nullptr) return;
        for (const auto& entry : runner_pool_->status()) {
            auto& supervisor = runner_pool_->runner(entry.id);
            if (!supervisor.apply_idle_timeout(sweeper_epoch_seconds(),
                                               policy.idle_unload_seconds)) {
                continue;
            }
            supervisor.unload();
        }
    }

    void sleep_one_poll_interval() {
        auto remaining = kPollInterval;
        while (remaining > std::chrono::milliseconds::zero() &&
              !stopping_.load()) {
            const auto step =
                std::min(remaining, std::chrono::milliseconds(200));
            std::this_thread::sleep_for(step);
            remaining -= step;
        }
    }

    void run() {
        while (!stopping_.load()) {
            const auto policy = memory_.policy();
            const auto status = memory_.sample();
            apply_idle_unload(policy, status);
            // Memory-pressure release cascade (docs/PLAN.md Phase 30A
            // deliverable "memory reduction actions applied before load and
            // between requests"): trims CacheManager's bounded categories
            // down to their configured capacity -- the same forced eviction
            // an administrator can trigger by hand via
            // POST /api/v1/system/cache/trim -- once reservation usage or
            // physical availability crosses the "high" pressure threshold.
            // KV-slot eviction (KvCacheManager::evict_one()) and per-request
            // RequestArena::reset() are already invoked at their own natural
            // request-scoped boundaries (Phase 27/30) and need no sweep-level
            // duplicate; this loop only ever forces the standing,
            // non-request-scoped caches down.
            if (cache_ != nullptr && status.pressure >= MemoryPressure::high) {
                cache_->trim();
            }
            sleep_one_poll_interval();
        }
    }

    MemoryBudgetManager& memory_;
    RunnerSupervisor* inference_;
    CacheManager* cache_;
    PromptSessionManager* prompt_sessions_;
    std::function<void()> on_idle_unload_;
    LocalRunnerPool* runner_pool_;
    std::atomic_bool stopping_{false};
    std::thread worker_;
};

MemorySweeper::MemorySweeper(MemoryBudgetManager& memory,
                             RunnerSupervisor* inference, CacheManager* cache,
                             PromptSessionManager* prompt_sessions,
                             std::uint32_t idle_unload_seconds,
                             std::function<void()> on_idle_unload,
                             LocalRunnerPool* runner_pool)
    : state_(std::make_unique<State>(memory, inference, cache,
                                     prompt_sessions, idle_unload_seconds,
                                     std::move(on_idle_unload), runner_pool)) {}

MemorySweeper::~MemorySweeper() = default;

}  // namespace masterai
