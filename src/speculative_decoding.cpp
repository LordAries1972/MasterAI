// Phase 32 (2026-08-13, evidence-pending): speculative decoding
// compatibility checking, acceptance-rate tracking, and the per-request
// enable/disable decision engine. See the class/function comments in
// masterai.hpp for this phase's honest scope -- none of this is wired to a
// live generation call site yet (no dual-model launch path exists in this
// codebase's RunnerSupervisor/LlamaCppAdapter), so nothing here changes any
// existing request's behavior.
#include "masterai.hpp"

#include <cmath>

namespace masterai {
namespace {

// Mirrors inference.cpp's own json_escape() exactly (this file cannot
// include inference.cpp's anonymous-namespace helper directly) -- escapes a
// raw string for embedding inside a manually-built JSON string literal.
std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result.push_back(digits[character >> 4U]);
                    result.push_back(digits[character & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    return result;
}

}  // namespace

DraftTargetCompatibilityResult check_draft_target_compatibility(
    const ModelManifest& target, const ModelManifest& draft) {
    DraftTargetCompatibilityResult result;
    result.compatible = true;
    if (target.id == draft.id) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "draft model must be a different model than the target");
    }
    if (target.architecture.empty() || draft.architecture.empty() ||
        target.architecture != draft.architecture) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "target and draft architectures do not match (proxy for "
            "tokenizer/vocabulary identity -- see this function's class "
            "comment for the honest limitation this approximates)");
    }
    if (target.format != draft.format) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "target and draft model file formats do not match");
    }
    if (target.backend != draft.backend) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "target and draft models require different backends");
    }
    if (draft.model_size_bytes == 0U || target.model_size_bytes == 0U) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "target or draft model size is unknown");
    } else if (draft.model_size_bytes >= target.model_size_bytes) {
        // Not a hard correctness requirement, but a draft model that is not
        // meaningfully smaller than its target cannot speed anything up --
        // admitting one here would be enabling a feature with no possible
        // benefit, which this codebase's own "evidence never self-enables"
        // discipline (AdvancedOptimizationRegistry, Phase 20) already
        // treats as equivalent to not supporting it.
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "draft model is not smaller than the target model, so "
            "speculative decoding could not improve throughput");
    }
    if (!draft.license_accepted || !target.license_accepted) {
        result.compatible = false;
        result.incompatibility_reasons.push_back(
            "target or draft model license has not been accepted");
    }
    return result;
}

SpeculativeDecodingStats::SpeculativeDecodingStats(const std::size_t rolling_window)
    : rolling_window_(rolling_window == 0U ? 1U : rolling_window) {}

void SpeculativeDecodingStats::record_step(
    const std::uint32_t draft_tokens_proposed,
    const std::uint32_t draft_tokens_accepted) {
    std::lock_guard<std::mutex> lock(mutex_);
    steps_.emplace_back(draft_tokens_proposed,
                        std::min(draft_tokens_accepted, draft_tokens_proposed));
    while (steps_.size() > rolling_window_) steps_.pop_front();
}

std::optional<double> SpeculativeDecodingStats::acceptance_rate() const {
    std::lock_guard<std::mutex> lock(mutex_);
    if (steps_.empty()) return std::nullopt;
    std::uint64_t proposed_total = 0U;
    std::uint64_t accepted_total = 0U;
    for (const auto& [proposed, accepted] : steps_) {
        proposed_total += proposed;
        accepted_total += accepted;
    }
    if (proposed_total == 0U) return std::nullopt;
    return static_cast<double>(accepted_total) / static_cast<double>(proposed_total);
}

std::size_t SpeculativeDecodingStats::steps_recorded() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return steps_.size();
}

SpeculativeDecodingDecision decide_speculative_decoding_for_request(
    const SpeculativeDecodingRequestContext& context,
    const double minimum_acceptance_rate,
    const std::uint32_t minimum_tokens_to_bother) {
    // No measured evidence yet for this (target, draft) pair: fail closed
    // rather than assuming a favorable acceptance rate -- an unproven pair
    // never gets the benefit of the doubt.
    if (!context.measured_acceptance_rate.has_value()) {
        return {false, "no measured acceptance-rate evidence yet for this "
                       "draft/target pair"};
    }
    if (*context.measured_acceptance_rate < minimum_acceptance_rate) {
        return {false, "measured acceptance rate is below the configured "
                       "minimum"};
    }
    if (context.requested_max_tokens < minimum_tokens_to_bother) {
        return {false, "request is too short for the draft/verify overhead "
                       "to pay off"};
    }
    if (context.available_memory_bytes == 0U ||
        context.combined_memory_estimate_bytes >= context.available_memory_bytes) {
        return {false, "combined draft+target memory does not fit in "
                       "available memory"};
    }
    if (context.draft_runner_queued) {
        return {false, "the draft runner is already busy with another "
                       "request"};
    }
    if (!context.sampling_supported_by_speculative_verification) {
        return {false, "requested sampling uses a feature (grammar/logit-bias "
                       "constrained decoding) llama.cpp's speculative "
                       "rejection-sampling verification does not support"};
    }
    if (context.thermal_headroom_percent.has_value() &&
        *context.thermal_headroom_percent < 10.0) {
        return {false, "thermal headroom is too low to run a second "
                       "concurrent model"};
    }
    return {true, "draft/target compatible, acceptance rate and headroom "
                 "sufficient"};
}

std::optional<ModelRecord> select_speculative_draft_candidate(
    const ModelManifest& target, const std::vector<ModelRecord>& candidates) {
    const ModelRecord* best = nullptr;
    for (const auto& candidate : candidates) {
        if (candidate.manifest.id == target.id) continue;
        if (!check_draft_target_compatibility(target, candidate.manifest)
                 .compatible) {
            continue;
        }
        if (best == nullptr ||
            candidate.manifest.model_size_bytes <
                best->manifest.model_size_bytes) {
            best = &candidate;
        }
    }
    if (best == nullptr) return std::nullopt;
    return *best;
}

SpeculativeDecodingPairEvidenceStore::SpeculativeDecodingPairEvidenceStore(
    RecordStore& records)
    : records_(&records) {
    for (const auto& item : records_->list("speculative_decoding_pairs")) {
        // No separator: both halves are always exactly 64 hex chars, so the
        // split point is unambiguous, and this keeps the key at exactly 128
        // characters -- RecordStore::safe_name()'s 128-char/alnum-plus-
        // "._-" policy would otherwise reject the previous "target|draft"
        // key (129 chars, and "|" isn't an allowed character) on every
        // single record() call.
        if (item.first.size() != 128U) {
            throw std::runtime_error(
                "persisted speculative-decoding pair record is malformed");
        }
        PairRecord record;
        record.target_model_sha256 = item.first.substr(0U, 64U);
        record.draft_model_sha256 = item.first.substr(64U);
        record.acceptance_rate = std::stod(item.second);
        pairs_[item.first] = record;
    }
}

std::string SpeculativeDecodingPairEvidenceStore::pair_key(
    const std::string& target_model_sha256,
    const std::string& draft_model_sha256) {
    return target_model_sha256 + draft_model_sha256;
}

void SpeculativeDecodingPairEvidenceStore::record(
    const std::string& target_model_sha256,
    const std::string& draft_model_sha256, const double acceptance_rate) {
    if (target_model_sha256.size() != 64U || draft_model_sha256.size() != 64U) {
        throw std::invalid_argument(
            "speculative-decoding pair evidence requires two full sha256 "
            "digests");
    }
    if (!std::isfinite(acceptance_rate) || acceptance_rate < 0.0 ||
        acceptance_rate > 1.0) {
        throw std::invalid_argument(
            "speculative-decoding acceptance rate must be within [0.0, 1.0]");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    PairRecord entry;
    entry.target_model_sha256 = target_model_sha256;
    entry.draft_model_sha256 = draft_model_sha256;
    entry.acceptance_rate = acceptance_rate;
    const auto key = pair_key(target_model_sha256, draft_model_sha256);
    pairs_[key] = entry;
    if (records_ != nullptr) {
        records_->put("speculative_decoding_pairs", key,
                      std::to_string(acceptance_rate));
    }
}

std::optional<double> SpeculativeDecodingPairEvidenceStore::lookup(
    const std::string& target_model_sha256,
    const std::string& draft_model_sha256) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found =
        pairs_.find(pair_key(target_model_sha256, draft_model_sha256));
    if (found == pairs_.end()) return std::nullopt;
    return found->second.acceptance_rate;
}

std::vector<SpeculativeDecodingPairEvidenceStore::PairRecord>
SpeculativeDecodingPairEvidenceStore::all() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<PairRecord> result;
    result.reserve(pairs_.size());
    for (const auto& [key, record] : pairs_) {
        static_cast<void>(key);
        result.push_back(record);
    }
    return result;
}

std::string speculative_decoding_pairs_json(
    const std::vector<SpeculativeDecodingPairEvidenceStore::PairRecord>& pairs) {
    std::string result = "[";
    bool first = true;
    for (const auto& pair : pairs) {
        if (!first) result += ",";
        first = false;
        result += "{\"targetModelSha256\":\"" +
                  json_escape(pair.target_model_sha256) +
                  "\",\"draftModelSha256\":\"" +
                  json_escape(pair.draft_model_sha256) +
                  "\",\"acceptanceRate\":" +
                  std::to_string(pair.acceptance_rate) + "}";
    }
    result += "]";
    return result;
}

}  // namespace masterai
