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

#include <algorithm>
#include <chrono>
#include <iterator>
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

std::string SharedTemplateKey::to_key() const {
    std::string key = "shared:";
    key += prefix_content_sha256;
    key += ':';
    key += model_sha256;
    key += ':';
    key += std::to_string(policy_generation);
    key += ':';
    // authorized_roles is a std::set, already sorted, so this join is
    // deterministic regardless of insertion order at the call site.
    bool first = true;
    for (const auto& role : authorized_roles) {
        if (!first) key += ',';
        first = false;
        key += role;
    }
    return key;
}

namespace {

struct Entry {
    // Exactly one of chat_id/shared_key is non-empty: a private per-chat
    // entry (try_reuse()/record()) or a cross-request shared-template entry
    // (try_reuse_shared_template()/record_shared_template()). Both live in
    // the same unified LRU ring/slot pool below -- see the class comment on
    // KvSlotAccounting::reusable_prefix for why sharing the slot identity
    // space is intentional.
    std::string chat_id;
    std::string shared_key;
    SessionFingerprint fingerprint;
    std::string last_prompt;
    unsigned int slot_id{0};
    // Exact count reported by RunnerSupervisor for last_prompt. Optional so
    // callers reconstructing older/test-only state never fabricate a count.
    std::optional<std::uint64_t> prompt_token_count;
    std::chrono::steady_clock::time_point last_used;
};

// Shared by try_reuse() and try_reuse_shared_template(): idle-expiry, then
// stable-prefix detection (the model's own chat template guarantees every
// prior turn is an immutable prefix of the next turn's fully wrapped
// prompt, so a literal, case-sensitive prefix match is the whole
// compatibility check -- no fuzzy matching is performed anywhere here),
// then the retained-prefix-byte ceiling. Callers are responsible for
// whatever identity check (fingerprint equality for a private chat; key
// equality, which already encodes model identity, for a shared template)
// must pass before this is even called.
SessionDecision evaluate_prefix_reuse(
    const Entry& entry, const std::string& generation_prompt,
    const std::chrono::steady_clock::duration idle_retention,
    const std::size_t max_retained_prefix_bytes) {
    SessionDecision decision;
    decision.prefix_byte_ceiling = max_retained_prefix_bytes;
    const auto now = std::chrono::steady_clock::now();
    if (now - entry.last_used > idle_retention) {
        decision.invalidation_reason = SessionInvalidationReason::idle_expired;
        return decision;
    }
    const auto mismatch = std::mismatch(
        entry.last_prompt.begin(), entry.last_prompt.end(),
        generation_prompt.begin(),
        generation_prompt.begin() +
            static_cast<std::ptrdiff_t>(std::min(generation_prompt.size(),
                                                 entry.last_prompt.size())));
    const std::size_t common_prefix_bytes = static_cast<std::size_t>(
        std::distance(entry.last_prompt.begin(), mismatch.first));
    decision.reusable_prefix_bytes = common_prefix_bytes;
    decision.divergence_offset = common_prefix_bytes;
    const bool is_literal_extension =
        generation_prompt.size() > entry.last_prompt.size() &&
        common_prefix_bytes == entry.last_prompt.size();
    if (!is_literal_extension) {
        // Either the new prompt is not longer (nothing new to
        // generate/resume), or it diverges from the recorded prompt before
        // reaching its end (an earlier turn was edited or resubmitted) --
        // either way this is a correctness failure, not a policy one, so it
        // is reported distinctly from the ceiling case below.
        decision.invalidation_reason = SessionInvalidationReason::prefix_diverged;
        return decision;
    }
    if (entry.last_prompt.size() > max_retained_prefix_bytes) {
        // A genuine literal-prefix match exists, but replaying/retaining
        // that much KV state exceeds the configured ceiling -- refuse reuse
        // to bound worst-case KV memory/latency growth, rather than reusing
        // an unboundedly large slot.
        decision.invalidation_reason =
            SessionInvalidationReason::prefix_ceiling_exceeded;
        return decision;
    }
    decision.reuse = true;
    decision.slot_id = entry.slot_id;
    if (entry.prompt_token_count.has_value()) {
        decision.reusable_prefix_tokens = *entry.prompt_token_count;
        decision.reusable_prefix_tokens_exact = true;
    }
    decision.invalidation_reason = SessionInvalidationReason::none;
    return decision;
}

}  // namespace

class PromptSessionManager::State {
public:
    State(unsigned int max_slots, std::uint32_t idle_retention_seconds,
          std::size_t max_retained_prefix_bytes)
        : max_slots_(max_slots),
          idle_retention_(std::chrono::seconds(idle_retention_seconds)),
          max_retained_prefix_bytes_(max_retained_prefix_bytes) {
        if (max_slots_ == 0U) {
            throw std::invalid_argument("session reuse requires at least one slot");
        }
    }

    SessionDecision try_reuse(const std::string& chat_id,
                              const SessionFingerprint& fingerprint,
                              const std::string& generation_prompt) const {
        std::lock_guard<std::mutex> lock(mutex_);
        SessionDecision decision;
        decision.prefix_byte_ceiling = max_retained_prefix_bytes_;
        const auto it = by_chat_.find(chat_id);
        if (it == by_chat_.end()) {
            decision.invalidation_reason =
                SessionInvalidationReason::no_prior_session;
            return decision;
        }
        const auto& entry = *it->second;
        if (!(entry.fingerprint == fingerprint)) {
            decision.invalidation_reason =
                SessionInvalidationReason::fingerprint_mismatch;
            return decision;
        }
        return evaluate_prefix_reuse(entry, generation_prompt, idle_retention_,
                                     max_retained_prefix_bytes_);
    }

    unsigned int record(const std::string& chat_id,
                        const SessionFingerprint& fingerprint,
                        const std::string& generation_prompt,
                        std::optional<unsigned int> reused_slot,
                        std::optional<std::uint64_t> prompt_token_count) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        auto existing = by_chat_.find(chat_id);
        if (existing != by_chat_.end()) {
            // Refresh in place: move to the back of the LRU list without
            // reallocating a slot.
            order_.splice(order_.end(), order_, existing->second);
            existing->second->fingerprint = fingerprint;
            existing->second->last_prompt = generation_prompt;
            existing->second->prompt_token_count = prompt_token_count;
            existing->second->last_used = now;
            if (reused_slot.has_value()) {
                existing->second->slot_id = *reused_slot;
            }
            return existing->second->slot_id;
        }
        const unsigned int slot_id =
            reused_slot.has_value() ? *reused_slot : allocate_slot_locked();
        Entry entry{chat_id, {}, fingerprint, generation_prompt, slot_id,
                    prompt_token_count, now};
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
        by_shared_key_.clear();
    }

    std::size_t active_sessions() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return order_.size();
    }

    // Phase 27: same fingerprint/literal-prefix-extension discipline as
    // try_reuse() above, keyed by SharedTemplateKey::to_key() instead of
    // chat_id. Refuses outright, without even consulting the table, unless
    // `prefix_sharing_admitted` is true -- so this table can be populated
    // (by record_shared_template()) before an administrator ever admits
    // sharing, and nothing reuses across identities until they explicitly
    // do.
    SessionDecision try_reuse_shared_template(
        const SharedTemplateKey& key, const std::string& generation_prompt,
        const bool prefix_sharing_admitted) const {
        std::lock_guard<std::mutex> lock(mutex_);
        SessionDecision decision;
        decision.prefix_byte_ceiling = max_retained_prefix_bytes_;
        if (!prefix_sharing_admitted) {
            decision.invalidation_reason =
                SessionInvalidationReason::no_prior_session;
            return decision;
        }
        const auto it = by_shared_key_.find(key.to_key());
        if (it == by_shared_key_.end()) {
            decision.invalidation_reason =
                SessionInvalidationReason::no_prior_session;
            return decision;
        }
        return evaluate_prefix_reuse(*it->second, generation_prompt,
                                     idle_retention_, max_retained_prefix_bytes_);
    }

    unsigned int record_shared_template(
        const SharedTemplateKey& key, const std::string& generation_prompt,
        std::optional<unsigned int> reused_slot,
        std::optional<std::uint64_t> prompt_token_count) {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto now = std::chrono::steady_clock::now();
        const auto serialized_key = key.to_key();
        auto existing = by_shared_key_.find(serialized_key);
        if (existing != by_shared_key_.end()) {
            order_.splice(order_.end(), order_, existing->second);
            existing->second->last_prompt = generation_prompt;
            existing->second->prompt_token_count = prompt_token_count;
            existing->second->last_used = now;
            if (reused_slot.has_value()) {
                existing->second->slot_id = *reused_slot;
            }
            return existing->second->slot_id;
        }
        const unsigned int slot_id =
            reused_slot.has_value() ? *reused_slot : allocate_slot_locked();
        SessionFingerprint fingerprint;
        fingerprint.model_sha256 = key.model_sha256;
        Entry entry{{}, serialized_key, fingerprint, generation_prompt,
                    slot_id, prompt_token_count, now};
        order_.push_back(std::move(entry));
        auto position = std::prev(order_.end());
        by_shared_key_[serialized_key] = position;
        return slot_id;
    }

private:
    // Allocates a fresh slot id when the pool still has room, otherwise
    // evicts the least-recently-used entry (private or shared-template,
    // whichever is at the front of the unified order_ ring) and reuses its
    // slot -- bounded pool, no unbounded growth.
    unsigned int allocate_slot_locked() {
        if (order_.size() < max_slots_) {
            return static_cast<unsigned int>(order_.size());
        }
        const auto victim = order_.front();
        const unsigned int reused_slot_id = victim.slot_id;
        if (!victim.chat_id.empty()) {
            by_chat_.erase(victim.chat_id);
        } else {
            by_shared_key_.erase(victim.shared_key);
        }
        order_.pop_front();
        return reused_slot_id;
    }

    unsigned int max_slots_;
    std::chrono::steady_clock::duration idle_retention_;
    // Phase 23: maximum reusable prefix length, in bytes, try_reuse() will
    // grant reuse for -- see PromptSessionManager::kDefaultMaxRetainedPrefixBytes.
    std::size_t max_retained_prefix_bytes_;
    mutable std::mutex mutex_;
    std::list<Entry> order_;
    std::map<std::string, std::list<Entry>::iterator> by_chat_;
    // Phase 27: cross-request shared-template lookup, sharing the same
    // order_ LRU ring/slot pool as by_chat_ above.
    std::map<std::string, std::list<Entry>::iterator> by_shared_key_;
};

PromptSessionManager::PromptSessionManager(
    unsigned int max_slots, std::uint32_t idle_retention_seconds,
    std::size_t max_retained_prefix_bytes)
    : state_(std::make_unique<State>(max_slots, idle_retention_seconds,
                                     max_retained_prefix_bytes)) {}

PromptSessionManager::~PromptSessionManager() = default;

SessionDecision PromptSessionManager::try_reuse(
    const std::string& chat_id, const SessionFingerprint& fingerprint,
    const std::string& generation_prompt) const {
    return state_->try_reuse(chat_id, fingerprint, generation_prompt);
}

unsigned int PromptSessionManager::record(
    const std::string& chat_id, const SessionFingerprint& fingerprint,
    const std::string& generation_prompt,
    std::optional<unsigned int> reused_slot,
    std::optional<std::uint64_t> prompt_token_count) {
    return state_->record(chat_id, fingerprint, generation_prompt, reused_slot,
                          prompt_token_count);
}

void PromptSessionManager::release(const std::string& chat_id) {
    state_->release(chat_id);
}

void PromptSessionManager::reset() { state_->reset(); }

std::size_t PromptSessionManager::active_sessions() const {
    return state_->active_sessions();
}

SessionDecision PromptSessionManager::try_reuse_shared_template(
    const SharedTemplateKey& key, const std::string& generation_prompt,
    const bool prefix_sharing_admitted) const {
    return state_->try_reuse_shared_template(key, generation_prompt,
                                             prefix_sharing_admitted);
}

unsigned int PromptSessionManager::record_shared_template(
    const SharedTemplateKey& key, const std::string& generation_prompt,
    std::optional<unsigned int> reused_slot,
    std::optional<std::uint64_t> prompt_token_count) {
    return state_->record_shared_template(key, generation_prompt, reused_slot,
                                          prompt_token_count);
}

}  // namespace masterai
