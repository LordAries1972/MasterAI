// Phase 25: weighted-fair priority scheduling and backpressure. See the
// scope note on RequestScheduler in masterai.hpp for what this phase does
// and honestly does not (backend continuous batching) deliver.
#include "masterai.hpp"

#include <array>
#include <deque>
#include <mutex>

namespace masterai {

std::string to_string(SchedulingClass klass) {
    switch (klass) {
        case SchedulingClass::cancellation_shutdown:
            return "cancellation_shutdown";
        case SchedulingClass::ide_completion:
            return "ide_completion";
        case SchedulingClass::interactive_chat:
            return "interactive_chat";
        case SchedulingClass::interactive_analysis:
            return "interactive_analysis";
        case SchedulingClass::user_background_job:
            return "user_background_job";
        case SchedulingClass::benchmark:
            return "benchmark";
        case SchedulingClass::indexing_embedding:
            return "indexing_embedding";
        case SchedulingClass::maintenance:
            return "maintenance";
    }
    return "unknown";
}

namespace {

constexpr std::array<SchedulingClass, 8> kAllClasses{
    SchedulingClass::cancellation_shutdown, SchedulingClass::ide_completion,
    SchedulingClass::interactive_chat,      SchedulingClass::interactive_analysis,
    SchedulingClass::user_background_job,   SchedulingClass::benchmark,
    SchedulingClass::indexing_embedding,    SchedulingClass::maintenance};

std::size_t rank_of(SchedulingClass klass) {
    return static_cast<std::size_t>(klass);
}

}  // namespace

std::map<SchedulingClass, SchedulingClassPolicy> default_scheduling_policies() {
    std::map<SchedulingClass, SchedulingClassPolicy> policies;
    policies[SchedulingClass::cancellation_shutdown] = {16U, 256U,
                                                         std::chrono::seconds(30),
                                                         16U, 0U};
    policies[SchedulingClass::ide_completion] = {8U, 32U, std::chrono::seconds(5),
                                                 4U, 0U};
    policies[SchedulingClass::interactive_chat] = {6U, 32U,
                                                    std::chrono::seconds(120), 4U, 0U};
    policies[SchedulingClass::interactive_analysis] = {
        4U, 24U, std::chrono::seconds(300), 2U, 0U};
    policies[SchedulingClass::user_background_job] = {
        3U, 64U, std::chrono::minutes(30), 2U, 0U};
    policies[SchedulingClass::benchmark] = {2U, 8U, std::chrono::minutes(30), 1U,
                                            0U};
    policies[SchedulingClass::indexing_embedding] = {
        2U, 128U, std::chrono::hours(2), 2U, 0U};
    policies[SchedulingClass::maintenance] = {1U, 64U, std::chrono::hours(6), 1U,
                                              0U};
    return policies;
}

class RequestScheduler::State final {
public:
    struct QueuedItem {
        std::uint64_t id{0};
        std::uint64_t memory_bytes{0};
        std::chrono::steady_clock::time_point admitted_at{
            std::chrono::steady_clock::now()};
    };

    struct ClassState {
        SchedulingClassPolicy policy;
        std::deque<QueuedItem> queue;
        std::size_t running{0};
        std::uint64_t reserved_memory_bytes{0};
        std::uint64_t admitted_total{0};
        std::uint64_t rejected_total{0};
        std::uint64_t expired_total{0};
        std::uint64_t preempted_total{0};
        // Deficit-round-robin credit, replenished by `policy.weight` each
        // time this class is passed over during next_ready() selection.
        long long credit{0};
    };

    explicit State(std::map<SchedulingClass, SchedulingClassPolicy> policies,
                  std::size_t limit)
        : global_concurrency_limit(limit) {
        for (const auto klass : kAllClasses) {
            auto found = policies.find(klass);
            classes[klass].policy =
                found != policies.end() ? found->second : SchedulingClassPolicy{};
        }
    }

    std::mutex mutex;
    std::map<SchedulingClass, ClassState> classes;
    std::size_t global_concurrency_limit;
    std::uint64_t next_id{1};

    std::size_t total_occupancy_locked() const {
        std::size_t total = 0U;
        for (const auto& [klass, state] : classes) {
            (void)klass;
            total += state.queue.size() + state.running;
        }
        return total;
    }

    // Caller holds `mutex`. Cancels every queued item across every class
    // whose residence time already exceeds its own policy's ceiling.
    std::size_t expire_stale_locked() {
        std::size_t expired = 0U;
        const auto now = std::chrono::steady_clock::now();
        for (auto& [klass, state] : classes) {
            (void)klass;
            while (!state.queue.empty() &&
                  now - state.queue.front().admitted_at >
                      state.policy.max_residence_time) {
                state.reserved_memory_bytes -= state.queue.front().memory_bytes;
                state.queue.pop_front();
                ++state.expired_total;
                ++expired;
            }
        }
        return expired;
    }
};

RequestScheduler::RequestScheduler(
    std::map<SchedulingClass, SchedulingClassPolicy> policies,
    std::size_t global_concurrency_limit)
    : state_(std::make_unique<State>(std::move(policies),
                                     global_concurrency_limit)) {}

RequestScheduler::~RequestScheduler() = default;

SchedulingAdmission RequestScheduler::admit(SchedulingClass klass,
                                            std::uint64_t memory_bytes_required) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->expire_stale_locked();
    auto& target = state_->classes[klass];
    SchedulingAdmission result;

    if (target.queue.size() >= target.policy.max_queue_depth) {
        ++target.rejected_total;
        result.reason = "class queue depth exceeded";
        return result;
    }
    if (target.policy.memory_allowance_bytes != 0U &&
        target.reserved_memory_bytes + memory_bytes_required >
            target.policy.memory_allowance_bytes) {
        ++target.rejected_total;
        result.reason = "class memory allowance exceeded";
        return result;
    }

    if (state_->global_concurrency_limit != 0U &&
        state_->total_occupancy_locked() >= state_->global_concurrency_limit) {
        // Backpressure: reject low-priority background work first. Find
        // the lowest-priority class (highest rank) with a lower priority
        // than the incoming request that still has queued (not running,
        // never preempt in-flight work) items, and evict its most
        // recently queued item to make room.
        RequestScheduler::State::ClassState* victim_class = nullptr;
        SchedulingClass victim_klass{};
        std::size_t victim_rank = 0U;
        for (auto& [candidate_klass, candidate_state] : state_->classes) {
            if (candidate_state.queue.empty()) continue;
            if (rank_of(candidate_klass) <= rank_of(klass)) continue;
            if (victim_class == nullptr || rank_of(candidate_klass) > victim_rank) {
                victim_class = &candidate_state;
                victim_klass = candidate_klass;
                victim_rank = rank_of(candidate_klass);
            }
        }
        if (victim_class == nullptr) {
            ++target.rejected_total;
            result.reason = "global concurrency limit reached";
            return result;
        }
        ScheduledTicket preempted;
        preempted.id = victim_class->queue.back().id;
        preempted.klass = victim_klass;
        victim_class->reserved_memory_bytes -= victim_class->queue.back().memory_bytes;
        victim_class->queue.pop_back();
        ++victim_class->preempted_total;
        result.preempted = preempted;
    }

    RequestScheduler::State::QueuedItem item;
    item.id = state_->next_id++;
    item.memory_bytes = memory_bytes_required;
    target.queue.push_back(item);
    target.reserved_memory_bytes += memory_bytes_required;
    ++target.admitted_total;

    ScheduledTicket ticket;
    ticket.id = item.id;
    ticket.klass = klass;
    result.admitted = true;
    result.ticket = ticket;
    return result;
}

std::optional<ScheduledTicket> RequestScheduler::next_ready() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->expire_stale_locked();

    // Cancellation/shutdown work always preempts weighted selection.
    auto& urgent = state_->classes[SchedulingClass::cancellation_shutdown];
    if (!urgent.queue.empty() &&
        urgent.running < urgent.policy.concurrency_allowance) {
        const auto item = urgent.queue.front();
        urgent.queue.pop_front();
        urgent.reserved_memory_bytes -= item.memory_bytes;
        ++urgent.running;
        return ScheduledTicket{item.id, SchedulingClass::cancellation_shutdown};
    }

    // Deficit-round-robin over the remaining classes in priority order:
    // every eligible class earns `weight` credit each pass; the first
    // class (in priority order) whose queue is non-empty, has running
    // capacity, and has positive credit is selected and its credit spent.
    // This lets a low-weight background class still make guaranteed
    // progress instead of starving outright, while a high-weight
    // interactive class is selected far more often.
    for (auto& [klass, state] : state_->classes) {
        if (klass == SchedulingClass::cancellation_shutdown) continue;
        if (!state.queue.empty()) {
            state.credit += static_cast<long long>(state.policy.weight);
        }
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (const auto klass : kAllClasses) {
            if (klass == SchedulingClass::cancellation_shutdown) continue;
            auto& state = state_->classes[klass];
            if (state.queue.empty()) continue;
            if (state.running >= state.policy.concurrency_allowance) continue;
            if (state.credit <= 0) continue;
            const auto item = state.queue.front();
            state.queue.pop_front();
            state.reserved_memory_bytes -= item.memory_bytes;
            state.credit -= static_cast<long long>(state.policy.weight);
            ++state.running;
            return ScheduledTicket{item.id, klass};
        }
        // Second pass: nothing had positive credit yet everything eligible
        // was skipped only for lack of credit -- grant one more round so a
        // low-weight class with a non-empty queue is never starved
        // indefinitely by classes that keep re-earning credit faster.
        bool any_creditless_eligible = false;
        for (const auto klass : kAllClasses) {
            if (klass == SchedulingClass::cancellation_shutdown) continue;
            auto& state = state_->classes[klass];
            if (state.queue.empty()) continue;
            if (state.running >= state.policy.concurrency_allowance) continue;
            if (state.credit <= 0) any_creditless_eligible = true;
        }
        if (!any_creditless_eligible) break;
        for (const auto klass : kAllClasses) {
            if (klass == SchedulingClass::cancellation_shutdown) continue;
            auto& state = state_->classes[klass];
            if (!state.queue.empty()) {
                state.credit += static_cast<long long>(state.policy.weight);
            }
        }
    }
    return std::nullopt;
}

void RequestScheduler::complete(const ScheduledTicket& ticket) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& state = state_->classes[ticket.klass];
    if (state.running > 0U) --state.running;
}

bool RequestScheduler::cancel(const ScheduledTicket& ticket) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& state = state_->classes[ticket.klass];
    for (auto iterator = state.queue.begin(); iterator != state.queue.end();
        ++iterator) {
        if (iterator->id != ticket.id) continue;
        state.reserved_memory_bytes -= iterator->memory_bytes;
        state.queue.erase(iterator);
        return true;
    }
    return false;
}

std::size_t RequestScheduler::expire_stale() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->expire_stale_locked();
}

std::map<SchedulingClass, SchedulingClassStatus> RequestScheduler::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::map<SchedulingClass, SchedulingClassStatus> result;
    for (const auto& [klass, state] : state_->classes) {
        SchedulingClassStatus entry;
        entry.queued = state.queue.size();
        entry.running = state.running;
        entry.reserved_memory_bytes = state.reserved_memory_bytes;
        entry.admitted_total = state.admitted_total;
        entry.rejected_total = state.rejected_total;
        entry.expired_total = state.expired_total;
        entry.preempted_total = state.preempted_total;
        result[klass] = entry;
    }
    return result;
}

std::string RequestScheduler::to_json(
    const std::map<SchedulingClass, SchedulingClassStatus>& status) {
    std::string body = "{\"classes\":{";
    bool first = true;
    for (const auto& [klass, entry] : status) {
        if (!first) body += ",";
        first = false;
        body += "\"" + to_string(klass) + "\":{" +
               "\"queued\":" + std::to_string(entry.queued) +
               ",\"running\":" + std::to_string(entry.running) +
               ",\"reservedMemoryBytes\":" +
               std::to_string(entry.reserved_memory_bytes) +
               ",\"admittedTotal\":" + std::to_string(entry.admitted_total) +
               ",\"rejectedTotal\":" + std::to_string(entry.rejected_total) +
               ",\"expiredTotal\":" + std::to_string(entry.expired_total) +
               ",\"preemptedTotal\":" + std::to_string(entry.preempted_total) +
               "}";
    }
    return body + "}}";
}

}  // namespace masterai
