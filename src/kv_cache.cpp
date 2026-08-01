// Phase 27: per-slot KV-cache accounting, bounded context-aware
// reservation, and deterministic eviction ordering. See the scope note on
// KvCacheManager in masterai.hpp for what reduced-precision KV and
// prefix-tree sharing deliberately do NOT do in this pass.
#include "masterai.hpp"

#include <map>
#include <mutex>

namespace masterai {

std::string to_string(KvPrecision precision) {
    switch (precision) {
        case KvPrecision::full:
            return "full";
        case KvPrecision::half:
            return "half";
        case KvPrecision::quantized_k:
            return "quantized_k";
        case KvPrecision::quantized_v:
            return "quantized_v";
    }
    return "unknown";
}

std::string to_string(KvPlacement placement) {
    switch (placement) {
        case KvPlacement::cpu:
            return "cpu";
        case KvPlacement::gpu:
            return "gpu";
        case KvPlacement::split:
            return "split";
    }
    return "unknown";
}

std::string to_string(KvSlotState state) {
    switch (state) {
        case KvSlotState::active:
            return "active";
        case KvSlotState::idle:
            return "idle";
        case KvSlotState::failed_cancelled:
            return "failed_cancelled";
        case KvSlotState::expired:
            return "expired";
    }
    return "unknown";
}

class KvCacheManager::State final {
public:
    struct Slot {
        KvSlotAccounting accounting;
        std::string memory_lease_id;
    };

    State(MemoryBudgetManager& manager, std::uint64_t hard_max,
         std::uint64_t step)
        : memory(manager), hard_max_bytes_per_slot(hard_max),
          growth_step_bytes(step == 0U ? 1U : step) {}

    std::uint64_t round_up_to_step(std::uint64_t bytes) const {
        const auto steps = (bytes + growth_step_bytes - 1U) / growth_step_bytes;
        return steps * growth_step_bytes;
    }

    MemoryBudgetManager& memory;
    std::uint64_t hard_max_bytes_per_slot;
    std::uint64_t growth_step_bytes;
    mutable std::mutex mutex;
    std::map<unsigned int, Slot> slots;
    std::vector<KvPrecisionEvidence> precision_evidence;
};

KvCacheManager::KvCacheManager(MemoryBudgetManager& memory,
                               std::uint64_t hard_max_bytes_per_slot,
                               std::uint64_t growth_step_bytes)
    : state_(std::make_unique<State>(memory, hard_max_bytes_per_slot,
                                     growth_step_bytes)) {}

KvCacheManager::~KvCacheManager() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (auto& [id, slot] : state_->slots) {
        (void)id;
        if (!slot.memory_lease_id.empty()) {
            state_->memory.release(slot.memory_lease_id);
        }
    }
}

KvReservationResult KvCacheManager::reserve(KvSlotAccounting initial) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    KvReservationResult result;
    if (state_->slots.find(initial.slot_id) != state_->slots.end()) {
        result.reason = "slot id already reserved";
        return result;
    }
    const auto granted = state_->round_up_to_step(initial.bytes_reserved);
    if (granted > state_->hard_max_bytes_per_slot) {
        result.reason = "requested KV reservation exceeds the per-slot hard "
                        "memory ceiling";
        return result;
    }
    MemoryEstimate estimate;
    estimate.kv_bytes_per_sequence = granted;
    estimate.sequences = 1U;
    const auto admission = state_->memory.reserve(MemoryCategory::kv_cache,
                                                   estimate, true);
    if (!admission.admitted) {
        result.reason = admission.diagnostic.empty()
                            ? "KV memory admission refused"
                            : admission.diagnostic;
        return result;
    }
    State::Slot slot;
    slot.accounting = initial;
    slot.accounting.bytes_reserved = granted;
    slot.memory_lease_id = admission.lease_id;
    state_->slots[initial.slot_id] = std::move(slot);
    result.admitted = true;
    result.granted_bytes = granted;
    return result;
}

KvReservationResult KvCacheManager::grow(unsigned int slot_id,
                                         std::uint64_t additional_bytes_requested) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    KvReservationResult result;
    const auto found = state_->slots.find(slot_id);
    if (found == state_->slots.end()) {
        result.reason = "unknown slot id";
        return result;
    }
    auto& slot = found->second;
    const auto current = slot.accounting.bytes_reserved;
    const auto candidate =
        state_->round_up_to_step(current + additional_bytes_requested);
    if (candidate > state_->hard_max_bytes_per_slot) {
        // Never optimistic: refuse the whole growth rather than partially
        // grant less than requested against a backend that (per the plan)
        // may preallocate full slot capacity anyway.
        result.reason = "growth would exceed the per-slot hard memory ceiling";
        return result;
    }
    const auto additional_granted = candidate - current;
    if (additional_granted == 0U) {
        result.admitted = true;
        result.granted_bytes = current;
        return result;
    }
    MemoryEstimate estimate;
    estimate.kv_bytes_per_sequence = additional_granted;
    estimate.sequences = 1U;
    const auto admission = state_->memory.reserve(MemoryCategory::kv_cache,
                                                   estimate, true);
    if (!admission.admitted) {
        result.reason = admission.diagnostic.empty()
                            ? "KV growth memory admission refused"
                            : admission.diagnostic;
        return result;
    }
    if (!slot.memory_lease_id.empty()) {
        state_->memory.release(slot.memory_lease_id);
    }
    slot.memory_lease_id = admission.lease_id;
    slot.accounting.bytes_reserved = candidate;
    result.admitted = true;
    result.granted_bytes = candidate;
    return result;
}

void KvCacheManager::touch(unsigned int slot_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->slots.find(slot_id);
    if (found == state_->slots.end()) return;
    ++found->second.accounting.reuse_count;
    found->second.accounting.state = KvSlotState::active;
}

void KvCacheManager::set_state(unsigned int slot_id, KvSlotState state) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->slots.find(slot_id);
    if (found == state_->slots.end()) return;
    found->second.accounting.state = state;
}

void KvCacheManager::release(unsigned int slot_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->slots.find(slot_id);
    if (found == state_->slots.end()) return;
    if (!found->second.memory_lease_id.empty()) {
        state_->memory.release(found->second.memory_lease_id);
    }
    state_->slots.erase(found);
}

std::optional<unsigned int> KvCacheManager::evict_one() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    // Bucket 1: failed/cancelled slots.
    for (const auto& [id, slot] : state_->slots) {
        if (slot.accounting.pinned) continue;
        if (slot.accounting.state == KvSlotState::failed_cancelled) return id;
    }
    // Bucket 2: expired idle prefixes.
    for (const auto& [id, slot] : state_->slots) {
        if (slot.accounting.pinned) continue;
        if (slot.accounting.state == KvSlotState::expired &&
            slot.accounting.reusable_prefix) {
            return id;
        }
    }
    // Bucket 3: lowest-reuse private (non-prefix) slots, never an
    // in-flight active slot.
    {
        const State::Slot* victim = nullptr;
        unsigned int victim_id = 0U;
        for (const auto& [id, slot] : state_->slots) {
            if (slot.accounting.pinned) continue;
            if (slot.accounting.state == KvSlotState::active) continue;
            if (slot.accounting.reusable_prefix) continue;
            if (victim == nullptr ||
                slot.accounting.reuse_count < victim->accounting.reuse_count) {
                victim = &slot;
                victim_id = id;
            }
        }
        if (victim != nullptr) return victim_id;
    }
    // Bucket 4: large, low-value reusable prefixes (lowest reuse first,
    // largest bytes as the tiebreaker).
    {
        const State::Slot* victim = nullptr;
        unsigned int victim_id = 0U;
        for (const auto& [id, slot] : state_->slots) {
            if (slot.accounting.pinned) continue;
            if (slot.accounting.state == KvSlotState::active) continue;
            if (!slot.accounting.reusable_prefix) continue;
            if (victim == nullptr ||
                slot.accounting.reuse_count < victim->accounting.reuse_count ||
                (slot.accounting.reuse_count == victim->accounting.reuse_count &&
                 slot.accounting.bytes_reserved > victim->accounting.bytes_reserved)) {
                victim = &slot;
                victim_id = id;
            }
        }
        if (victim != nullptr) return victim_id;
    }
    // Bucket 5: idle, non-pinned sessions (anything left that is not
    // currently active).
    for (const auto& [id, slot] : state_->slots) {
        if (slot.accounting.pinned) continue;
        if (slot.accounting.state == KvSlotState::idle) return id;
    }
    // Bucket 6: safe rejection -- nothing evictable exists (every remaining
    // slot is pinned or actively in-flight).
    return std::nullopt;
}

std::optional<KvSlotAccounting> KvCacheManager::find(unsigned int slot_id) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->slots.find(slot_id);
    if (found == state_->slots.end()) return std::nullopt;
    return found->second.accounting;
}

std::vector<KvSlotAccounting> KvCacheManager::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<KvSlotAccounting> result;
    result.reserve(state_->slots.size());
    for (const auto& [id, slot] : state_->slots) {
        (void)id;
        result.push_back(slot.accounting);
    }
    return result;
}

bool KvCacheManager::precision_admitted(KvPrecision precision) const {
    // See the class-level scope note: reduced precision never self-enables
    // from recorded evidence in this pass, regardless of how much evidence
    // exists, matching AdvancedOptimizationRegistry's discipline.
    return precision == KvPrecision::full;
}

void KvCacheManager::record_precision_evidence(const KvPrecisionEvidence& evidence) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->precision_evidence.push_back(evidence);
}

std::string KvCacheManager::to_json(const std::vector<KvSlotAccounting>& slots) {
    std::string body = "{\"slots\":[";
    bool first = true;
    for (const auto& slot : slots) {
        if (!first) body += ",";
        first = false;
        body += "{\"slotId\":" + std::to_string(slot.slot_id) +
               ",\"owningUserId\":\"" + slot.owning_user_id + "\"" +
               ",\"owningChatId\":\"" + slot.owning_chat_id + "\"" +
               ",\"owningProjectId\":\"" + slot.owning_project_id + "\"" +
               ",\"contextLengthTokens\":" +
               std::to_string(slot.context_length_tokens) +
               ",\"tokenCount\":" + std::to_string(slot.token_count) +
               ",\"precision\":\"" + to_string(slot.precision) + "\"" +
               ",\"placement\":\"" + to_string(slot.placement) + "\"" +
               ",\"bytesReserved\":" + std::to_string(slot.bytes_reserved) +
               ",\"pinned\":" + (slot.pinned ? "true" : "false") +
               ",\"reusablePrefix\":" +
               (slot.reusable_prefix ? "true" : "false") +
               ",\"reuseCount\":" + std::to_string(slot.reuse_count) +
               ",\"state\":\"" + to_string(slot.state) + "\"}";
    }
    return body + "]}";
}

}  // namespace masterai
