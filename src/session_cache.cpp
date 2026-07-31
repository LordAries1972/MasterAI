// Phase 18: runner prompt-prefix / KV-session reuse.
//
// PromptSessionManager tracks, per chat, whether the llama.cpp server's own
// internal KV cache for a previously used slot can safely be resumed for the
// chat's next turn instead of the runner re-evaluating the whole prompt from
// token zero. Reuse is granted only when every field of SessionFingerprint
// still matches exactly (model, backend, chat-template architecture, context
// length, and the chat's project index generation) and the new prompt is a
// literal byte-prefix extension of the prompt the slot was last warmed with
// -- any uncertainty at all (a model switch, a project reindex, an edited or
// resubmitted earlier turn) means "start fresh," never "guess."
//
// This state is intentionally process-lifetime only: the KV cache it
// describes lives inside the runner's own address space and is gone the
// moment that process restarts, so persisting this registry across a
// MasterAI restart would just describe slots that no longer exist.
#include "masterai.hpp"

#include <chrono>
#include <list>
#include <map>
#include <mutex>

namespace masterai {

bool SessionFingerprint::operator==(const SessionFingerprint& other) const {
    return model_sha256 == other.model_sha256 &&
           backend_executable == other.backend_executable &&
           architecture == other.architecture &&
           context_length == other.context_length &&
           project_index_generation == other.project_index_generation &&
           settings_fingerprint == other.settings_fingerprint;
}

namespace {

struct Entry {
    std::string chat_id;
    SessionFingerprint fingerprint;
    std::string last_prompt;
    unsigned int slot_id{0};
    std::chrono::steady_clock::time_point last_used;
};

}  // namespace

class PromptSessionManager::State {
public:
    State(unsigned int max_slots, std::uint32_t idle_retention_seconds)
        : max_slots_(max_slots),
          idle_retention_(std::chrono::seconds(idle_retention_seconds)) {
        if (max_slots_ == 0U) {
            throw std::invalid_argument("session reuse requires at least one slot");
        }
    }

    SessionDecision try_reuse(const std::string& chat_id,
                              const SessionFingerprint& fingerprint,
                              const std::string& generation_prompt) const {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = by_chat_.find(chat_id);
        if (it == by_chat_.end()) return {};
        const auto& entry = *it->second;
        const auto now = std::chrono::steady_clock::now();
        if (now - entry.last_used > idle_retention_) return {};
        if (!(entry.fingerprint == fingerprint)) return {};
        // Stable-prefix detection: the model's own chat template guarantees
        // every prior turn is an immutable prefix of the next turn's fully
        // wrapped prompt, so a literal, case-sensitive prefix match is the
        // whole compatibility check -- an edited/resubmitted earlier turn or
        // any other divergence fails this and correctly falls back to a
        // fresh, uncached request instead of feeding the runner a KV cache
        // that no longer matches what it thinks it already evaluated.
        if (generation_prompt.size() <= entry.last_prompt.size() ||
            generation_prompt.compare(0, entry.last_prompt.size(),
                                      entry.last_prompt) != 0) {
            return {};
        }
        return {true, entry.slot_id};
    }

    unsigned int record(const std::string& chat_id,
                        const SessionFingerprint& fingerprint,
                        const std::string& generation_prompt,
                        std::optional<unsigned int> reused_slot) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        auto existing = by_chat_.find(chat_id);
        if (existing != by_chat_.end()) {
            // Refresh in place: move to the back of the LRU list without
            // reallocating a slot.
            order_.splice(order_.end(), order_, existing->second);
            existing->second->fingerprint = fingerprint;
            existing->second->last_prompt = generation_prompt;
            existing->second->last_used = now;
            if (reused_slot.has_value()) {
                existing->second->slot_id = *reused_slot;
            }
            return existing->second->slot_id;
        }
        const unsigned int slot_id =
            reused_slot.has_value() ? *reused_slot : allocate_slot_locked();
        Entry entry{chat_id, fingerprint, generation_prompt, slot_id, now};
        order_.push_back(std::move(entry));
        auto position = std::prev(order_.end());
        by_chat_[chat_id] = position;
        return slot_id;
    }

    void release(const std::string& chat_id) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = by_chat_.find(chat_id);
        if (it == by_chat_.end()) return;
        order_.erase(it->second);
        by_chat_.erase(it);
    }

    void reset() {
        std::lock_guard<std::mutex> lock(mutex_);
        order_.clear();
        by_chat_.clear();
    }

    std::size_t active_sessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.size();
    }

private:
    // Allocates a fresh slot id when the pool still has room, otherwise
    // evicts the least-recently-used chat's entry (the front of order_) and
    // reuses its slot -- bounded pool, no unbounded growth, and eviction is
    // scoped to individual chats rather than any user/project-wide sweep.
    unsigned int allocate_slot_locked() {
        if (order_.size() < max_slots_) {
            return static_cast<unsigned int>(order_.size());
        }
        const auto victim = order_.front();
        const unsigned int reused_slot_id = victim.slot_id;
        by_chat_.erase(victim.chat_id);
        order_.pop_front();
        return reused_slot_id;
    }

    unsigned int max_slots_;
    std::chrono::steady_clock::duration idle_retention_;
    mutable std::mutex mutex_;
    std::list<Entry> order_;
    std::map<std::string, std::list<Entry>::iterator> by_chat_;
};

PromptSessionManager::PromptSessionManager(unsigned int max_slots,
                                           std::uint32_t idle_retention_seconds)
    : state_(std::make_unique<State>(max_slots, idle_retention_seconds)) {}

PromptSessionManager::~PromptSessionManager() = default;

SessionDecision PromptSessionManager::try_reuse(
    const std::string& chat_id, const SessionFingerprint& fingerprint,
    const std::string& generation_prompt) const {
    return state_->try_reuse(chat_id, fingerprint, generation_prompt);
}

unsigned int PromptSessionManager::record(
    const std::string& chat_id, const SessionFingerprint& fingerprint,
    const std::string& generation_prompt,
    std::optional<unsigned int> reused_slot) {
    return state_->record(chat_id, fingerprint, generation_prompt, reused_slot);
}

void PromptSessionManager::release(const std::string& chat_id) {
    state_->release(chat_id);
}

void PromptSessionManager::reset() { state_->reset(); }

std::size_t PromptSessionManager::active_sessions() const {
    return state_->active_sessions();
}

}  // namespace masterai
