// Phase 32 (2026-08-13, evidence-pending): speculative decoding
// compatibility checking, acceptance-rate tracking, and the per-request
// enable/disable decision engine. See the class/function comments in
// masterai.hpp for this phase's honest scope -- none of this is wired to a
// live generation call site yet (no dual-model launch path exists in this
// codebase's RunnerSupervisor/LlamaCppAdapter), so nothing here changes any
// existing request's behavior.
#include "masterai.hpp"

namespace masterai {

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
    if (!context.sampling_is_greedy_or_deterministic) {
        return {false, "requested sampling settings are not compatible with "
                       "speculative decoding's verification step"};
    }
    if (context.thermal_headroom_percent.has_value() &&
        *context.thermal_headroom_percent < 10.0) {
        return {false, "thermal headroom is too low to run a second "
                       "concurrent model"};
    }
    return {true, "draft/target compatible, acceptance rate and headroom "
                 "sufficient"};
}

}  // namespace masterai
