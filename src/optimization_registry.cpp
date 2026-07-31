// Phase 20: optional advanced throughput, explicitly gated by docs/PLAN.md
// section 25 -- "no feature in this phase is pre-approved for
// implementation merely by appearing in the plan". This unit is scaffolding
// only: it declares the candidate features and a place to record evidence
// for them, but implements none of the optimizations themselves, and no
// code path here can ever set AdvancedOptimizationFeature::enabled to true.
#include "masterai.hpp"

#include <algorithm>
#include <stdexcept>

namespace masterai {

AdvancedOptimizationRegistry::AdvancedOptimizationRegistry() {
    features_ = {
        {"continuous_batching",
         "Weighted-fair multi-priority inference scheduling and continuous "
         "batching for compatible requests.",
         false, true, std::nullopt},
        {"speculative_decoding",
         "Speculative decoding with exact target/draft compatibility, "
         "separate memory accounting, and quality parity checks.",
         false, true, std::nullopt},
        {"numa_affinity",
         "NUMA-aware placement and affinity for measured multi-node hosts.",
         false, true, std::nullopt},
        {"storage_prefetch",
         "Storage-specific prefetch/read-ahead and alternate asynchronous "
         "I/O.",
         false, true, std::nullopt},
        {"multiple_warm_runners",
         "Multiple warm runners, admitted only where their measured latency "
         "value justifies their full memory cost.",
         false, true, std::nullopt},
        {"gpu_cpu_kv_placement",
         "Backend-specific GPU/CPU KV placement, unified/separate KV, "
         "direct I/O, or model pre-touch policies.",
         false, true, std::nullopt},
    };
}

std::vector<AdvancedOptimizationFeature> AdvancedOptimizationRegistry::features()
    const {
    return features_;
}

bool AdvancedOptimizationRegistry::has_evidence(
    const std::string& feature_name) const {
    const auto found = std::find_if(
        features_.begin(), features_.end(),
        [&](const AdvancedOptimizationFeature& feature) {
            return feature.name == feature_name;
        });
    return found != features_.end() && found->evidence.has_value();
}

void AdvancedOptimizationRegistry::record_evidence(
    const std::string& feature_name,
    const AdvancedOptimizationEvidence& evidence) {
    const auto found = std::find_if(
        features_.begin(), features_.end(),
        [&](const AdvancedOptimizationFeature& feature) {
            return feature.name == feature_name;
        });
    if (found == features_.end()) {
        throw std::invalid_argument("unknown advanced optimization feature");
    }
    // Recording evidence never sets enabled=true: whether a feature is ever
    // admitted remains a separate, later decision per docs/PLAN.md section
    // 25, not something this call can grant.
    found->evidence = evidence;
}

// name/description are hardcoded literals (see the registry constructor
// above), never free-form input, so no JSON escaping is required.
std::string advanced_optimization_registry_json(
    const std::vector<AdvancedOptimizationFeature>& features) {
    std::string body = "[";
    bool first = true;
    for (const auto& feature : features) {
        if (!first) body += ",";
        first = false;
        body += "{\"name\":\"" + feature.name + "\"" + ",\"description\":\"" +
               feature.description + "\"" + ",\"enabled\":" +
               (feature.enabled ? "true" : "false") +
               ",\"requiresEvidence\":" +
               (feature.requires_evidence ? "true" : "false") +
               ",\"hasEvidence\":" +
               (feature.evidence.has_value() ? "true" : "false") + "}";
    }
    return body + "]";
}

}  // namespace masterai
