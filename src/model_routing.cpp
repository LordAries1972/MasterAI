// Phase 29: model tiering, routing, and cascade inference decision logic.
// See the scope note on ModelRouter in masterai.hpp for what is and is not
// wired into a live request in this pass.
#include "masterai.hpp"

#include <array>

namespace masterai {

std::string to_string(ModelTier tier) {
    switch (tier) {
        case ModelTier::deterministic_processing:
            return "deterministic_processing";
        case ModelTier::compact_router:
            return "compact_router";
        case ModelTier::small_fast:
            return "small_fast";
        case ModelTier::medium_general:
            return "medium_general";
        case ModelTier::large_specialist:
            return "large_specialist";
    }
    return "unknown";
}

std::string to_string(ResidentModelProfile profile) {
    switch (profile) {
        case ResidentModelProfile::minimal:
            return "minimal";
        case ResidentModelProfile::balanced:
            return "balanced";
        case ResidentModelProfile::performance:
            return "performance";
    }
    return "unknown";
}

std::string to_string(EscalationReason reason) {
    switch (reason) {
        case EscalationReason::none:
            return "none";
        case EscalationReason::low_confidence:
            return "low_confidence";
        case EscalationReason::unsupported_syntax:
            return "unsupported_syntax";
        case EscalationReason::conflicting_retrieval_evidence:
            return "conflicting_retrieval_evidence";
        case EscalationReason::failed_deterministic_validation:
            return "failed_deterministic_validation";
        case EscalationReason::security_sensitivity:
            return "security_sensitivity";
        case EscalationReason::explicit_user_request:
            return "explicit_user_request";
        case EscalationReason::context_exceeds_capacity:
            return "context_exceeds_capacity";
    }
    return "unknown";
}

ModelTier parse_model_tier(const std::string& text) {
    if (text == "deterministic_processing") return ModelTier::deterministic_processing;
    if (text == "compact_router") return ModelTier::compact_router;
    if (text == "small_fast") return ModelTier::small_fast;
    if (text == "medium_general") return ModelTier::medium_general;
    if (text == "large_specialist") return ModelTier::large_specialist;
    throw std::invalid_argument("unknown model tier: " + text);
}

namespace {

constexpr std::array<ModelTier, 5> kTierOrder{
    ModelTier::deterministic_processing, ModelTier::compact_router,
    ModelTier::small_fast, ModelTier::medium_general,
    ModelTier::large_specialist};

// Notional minimum RAM footprint per tier, used only to decide whether a
// selected tier must be downgraded to fit reported available memory -- not
// a claim about any specific model's real footprint (ModelManifest already
// carries that per-model; this is a coarse, tier-level fallback for when a
// caller has not yet resolved a concrete model).
std::uint64_t notional_minimum_ram_mib(ModelTier tier) {
    switch (tier) {
        case ModelTier::deterministic_processing:
            return 0U;
        case ModelTier::compact_router:
            return 512U;
        case ModelTier::small_fast:
            return 2048U;
        case ModelTier::medium_general:
            return 6144U;
        case ModelTier::large_specialist:
            return 16384U;
    }
    return 0U;
}

std::size_t tier_index(ModelTier tier) {
    for (std::size_t i = 0U; i < kTierOrder.size(); ++i) {
        if (kTierOrder[i] == tier) return i;
    }
    return 0U;
}

}  // namespace

ModelRouter::ModelRouter(std::map<ModelTier, std::vector<std::string>> tier_models)
    : tier_models_(std::move(tier_models)) {}

std::optional<ModelTierAssignment> ModelRouter::select_initial_tier(
    const RoutingSignals& signals) const {
    // Explicit user pin always wins when it resolves to a tier this router
    // actually has a model registered for.
    if (signals.user_pinned_model_id.has_value()) {
        for (const auto& [tier, models] : tier_models_) {
            for (const auto& model_id : models) {
                if (model_id == *signals.user_pinned_model_id) {
                    return ModelTierAssignment{tier, model_id};
                }
            }
        }
        // Unresolvable pin: fall through to the normal rules rather than
        // silently dropping the request.
    }

    ModelTier chosen = ModelTier::medium_general;
    if (signals.required_capabilities.count("security_sensitive") != 0U) {
        chosen = ModelTier::medium_general;
    } else if (signals.task_category == "deterministic" &&
              tier_models_.count(ModelTier::deterministic_processing) != 0U) {
        chosen = ModelTier::deterministic_processing;
    } else if (signals.task_category == "classification" ||
              signals.task_category == "routing") {
        chosen = ModelTier::compact_router;
    } else if (signals.requested_quality == "high" ||
              signals.context_size_tokens > 8192U) {
        chosen = ModelTier::large_specialist;
    } else if (signals.requested_quality == "draft" ||
              signals.benchmark_evidence_favours_small_model) {
        chosen = ModelTier::small_fast;
    } else {
        chosen = ModelTier::medium_general;
    }

    // Downgrade until the tier's notional footprint fits available RAM, but
    // never below compact_router purely for memory reasons (deterministic
    // tier is a distinct capability choice, not a memory fallback).
    if (signals.available_ram_mib > 0U) {
        while (tier_index(chosen) > tier_index(ModelTier::compact_router) &&
              notional_minimum_ram_mib(chosen) > signals.available_ram_mib) {
            chosen = kTierOrder[tier_index(chosen) - 1U];
        }
    }

    // Resolve to a concrete registered model for the chosen tier, falling
    // back downward through cheaper tiers if none is registered, since an
    // unresolvable tier is worse than a slightly smaller real model.
    for (std::size_t index = tier_index(chosen) + 1U; index-- > 0U;) {
        const auto candidate_tier = kTierOrder[index];
        const auto found = tier_models_.find(candidate_tier);
        if (found != tier_models_.end() && !found->second.empty()) {
            return ModelTierAssignment{candidate_tier, found->second.front()};
        }
        if (index == 0U) break;
    }
    return std::nullopt;
}

CascadeDecision ModelRouter::evaluate_cascade(const CascadeStageOutcome& outcome,
                                              double confidence_threshold) {
    CascadeDecision decision;
    if (outcome.deterministic_validation_failed) {
        decision.reason = EscalationReason::failed_deterministic_validation;
    } else if (outcome.security_sensitive) {
        decision.reason = EscalationReason::security_sensitivity;
    } else if (outcome.user_requested_escalation) {
        decision.reason = EscalationReason::explicit_user_request;
    } else if (outcome.unsupported_syntax) {
        decision.reason = EscalationReason::unsupported_syntax;
    } else if (outcome.retrieval_conflict) {
        decision.reason = EscalationReason::conflicting_retrieval_evidence;
    } else if (!outcome.succeeded || outcome.confidence < confidence_threshold) {
        decision.reason = EscalationReason::low_confidence;
    } else {
        return decision;  // reason stays `none`, escalate stays false
    }

    const auto next = next_tier_after(outcome.tier);
    if (next.has_value()) {
        decision.escalate = true;
        decision.next_tier = next;
    }
    // else: a triggering condition fired but the cascade is already at its
    // ceiling -- escalate stays false, reason stays set so the caller can
    // still see *why* it would have escalated (see the struct comment).
    return decision;
}

std::optional<ModelTier> ModelRouter::next_tier_after(ModelTier tier) {
    const auto index = tier_index(tier);
    if (index + 1U >= kTierOrder.size()) return std::nullopt;
    return kTierOrder[index + 1U];
}

bool ModelRouter::resident_set_within_profile(
    ResidentModelProfile profile, const std::vector<ModelTier>& resident_tiers) {
    if (profile == ResidentModelProfile::performance) return true;

    std::size_t generation_tier_count = 0U;
    std::size_t compact_router_count = 0U;
    for (const auto tier : resident_tiers) {
        if (tier == ModelTier::small_fast || tier == ModelTier::medium_general ||
            tier == ModelTier::large_specialist) {
            ++generation_tier_count;
        } else if (tier == ModelTier::compact_router) {
            ++compact_router_count;
        }
    }

    if (profile == ResidentModelProfile::minimal) {
        // "Minimal: one model" -- at most one generation model, and no
        // separate compact/router model resident alongside it.
        return generation_tier_count <= 1U && compact_router_count == 0U;
    }
    // Balanced: one generation model plus an optional compact/router model.
    return generation_tier_count <= 1U && compact_router_count <= 1U;
}

}  // namespace masterai
