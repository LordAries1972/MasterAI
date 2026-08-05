// Phase 20 durable evidence and admission registry for optional throughput.
// This unit validates, persists, exposes, and independently disables advanced
// capabilities; backend implementations remain in their owning native units.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace masterai {
namespace {

bool valid_sha256(const std::string& value) {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

std::string pack(const std::vector<std::string>& fields) {
    std::string result;
    for (const auto& field : fields) {
        result += std::to_string(field.size()) + ":" + field;
    }
    return result;
}

std::vector<std::string> unpack(const std::string& value) {
    std::vector<std::string> fields;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto colon = value.find(':', position);
        if (colon == std::string::npos || colon == position) {
            throw std::runtime_error(
                "persisted advanced-optimization record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error(
                "persisted advanced-optimization record is truncated");
        }
        fields.push_back(value.substr(position, size));
        position += size;
    }
    return fields;
}

void validate_evidence(const std::string& feature_name,
                       const AdvancedOptimizationEvidence& evidence) {
    if (evidence.feature_name != feature_name ||
        evidence.baseline_description.empty() ||
        evidence.changed_setting.empty() ||
        !valid_sha256(evidence.host_hash) ||
        !valid_sha256(evidence.model_sha256) ||
        !valid_sha256(evidence.backend_hash) ||
        !std::isfinite(evidence.time_to_first_token_ms) ||
        evidence.time_to_first_token_ms <= 0.0 ||
        !std::isfinite(evidence.prompt_throughput_tokens_per_second) ||
        evidence.prompt_throughput_tokens_per_second <= 0.0 ||
        !std::isfinite(evidence.generation_throughput_tokens_per_second) ||
        evidence.generation_throughput_tokens_per_second <= 0.0 ||
        evidence.peak_resident_memory_bytes == 0U ||
        evidence.quality_notes.empty() ||
        evidence.power_thermal_notes.empty()) {
        throw std::invalid_argument(
            "advanced optimization evidence is incomplete");
    }
}

}  // namespace

AdvancedOptimizationRegistry::AdvancedOptimizationRegistry() {
    initialize_features();
}

AdvancedOptimizationRegistry::AdvancedOptimizationRegistry(RecordStore& records)
    : records_(&records) {
    initialize_features();
    restore();
}

void AdvancedOptimizationRegistry::initialize_features() {
    features_ = {
        {"continuous_batching",
         "Weighted-fair multi-priority inference scheduling and continuous "
         "batching for compatible requests.",
         false, true, true, std::nullopt},
        {"speculative_decoding",
         "Speculative decoding with exact target/draft compatibility, "
         "separate memory accounting, and quality parity checks.",
         false, true, false, std::nullopt},
        {"numa_affinity",
         "NUMA-aware placement and affinity for measured multi-node hosts.",
         false, true, false, std::nullopt},
        {"storage_prefetch",
         "Storage-specific prefetch/read-ahead and alternate asynchronous "
         "I/O.",
         false, true, true, std::nullopt},
        {"multiple_warm_runners",
         "Multiple warm runners, admitted only where their measured latency "
         "value justifies their full memory cost.",
         false, true, false, std::nullopt},
        {"gpu_cpu_kv_placement",
         "Backend-specific GPU/CPU KV placement, unified/separate KV, "
         "direct I/O, or model pre-touch policies.",
         false, true, true, std::nullopt},
    };
}

AdvancedOptimizationFeature& AdvancedOptimizationRegistry::find_mutable(
    const std::string& feature_name) {
    const auto found = std::find_if(
        features_.begin(), features_.end(),
        [&](const AdvancedOptimizationFeature& feature) {
            return feature.name == feature_name;
        });
    if (found == features_.end()) {
        throw std::invalid_argument("unknown advanced optimization feature");
    }
    return *found;
}

const AdvancedOptimizationFeature& AdvancedOptimizationRegistry::find(
    const std::string& feature_name) const {
    const auto found = std::find_if(
        features_.begin(), features_.end(),
        [&](const AdvancedOptimizationFeature& feature) {
            return feature.name == feature_name;
        });
    if (found == features_.end()) {
        throw std::invalid_argument("unknown advanced optimization feature");
    }
    return *found;
}

std::vector<AdvancedOptimizationFeature> AdvancedOptimizationRegistry::features()
    const {
    std::lock_guard<std::mutex> lock(mutex_);
    return features_;
}

bool AdvancedOptimizationRegistry::has_evidence(
    const std::string& feature_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(feature_name).evidence.has_value();
}

bool AdvancedOptimizationRegistry::is_enabled(
    const std::string& feature_name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return find(feature_name).enabled;
}

void AdvancedOptimizationRegistry::record_evidence(
    const std::string& feature_name,
    const AdvancedOptimizationEvidence& evidence) {
    validate_evidence(feature_name, evidence);
    std::lock_guard<std::mutex> lock(mutex_);
    auto& feature = find_mutable(feature_name);
    // Replacement evidence always returns the feature to fail-closed review.
    feature.enabled = false;
    feature.evidence = evidence;
    persist(feature);
}

void AdvancedOptimizationRegistry::admit(const std::string& feature_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& feature = find_mutable(feature_name);
    if (!feature.implementation_available ||
        !feature.evidence.has_value() ||
        feature.evidence->regression_detected ||
        !feature.evidence->fallback_verified) {
        throw std::invalid_argument(
            "advanced optimization lacks accepted evidence and fallback");
    }
    validate_evidence(feature_name, *feature.evidence);
    feature.enabled = true;
    persist(feature);
}

void AdvancedOptimizationRegistry::disable(const std::string& feature_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& feature = find_mutable(feature_name);
    feature.enabled = false;
    persist(feature);
}

void AdvancedOptimizationRegistry::persist(
    const AdvancedOptimizationFeature& feature) {
    if (records_ == nullptr) return;
    std::vector<std::string> fields{
        feature.enabled ? "true" : "false",
        feature.evidence.has_value() ? "true" : "false"};
    if (feature.evidence.has_value()) {
        const auto& evidence = *feature.evidence;
        fields.insert(fields.end(), {
            evidence.feature_name, evidence.baseline_description,
            evidence.changed_setting, evidence.host_hash,
            evidence.model_sha256, evidence.backend_hash,
            std::to_string(evidence.time_to_first_token_ms),
            std::to_string(evidence.prompt_throughput_tokens_per_second),
            std::to_string(evidence.generation_throughput_tokens_per_second),
            std::to_string(evidence.peak_resident_memory_bytes),
            evidence.quality_notes, evidence.power_thermal_notes,
            evidence.regression_detected ? "true" : "false",
            evidence.fallback_verified ? "true" : "false"});
    }
    records_->put("advanced_optimizations", feature.name, pack(fields));
}

void AdvancedOptimizationRegistry::restore() {
    for (const auto& item : records_->list("advanced_optimizations")) {
        auto& feature = find_mutable(item.first);
        const auto fields = unpack(item.second);
        if (fields.size() != 2U && fields.size() != 16U) {
            throw std::runtime_error(
                "persisted advanced-optimization record is invalid");
        }
        feature.enabled = fields[0] == "true";
        if (fields[1] == "true") {
            if (fields.size() != 16U) {
                throw std::runtime_error(
                    "persisted advanced-optimization evidence is missing");
            }
            AdvancedOptimizationEvidence evidence;
            evidence.feature_name = fields[2];
            evidence.baseline_description = fields[3];
            evidence.changed_setting = fields[4];
            evidence.host_hash = fields[5];
            evidence.model_sha256 = fields[6];
            evidence.backend_hash = fields[7];
            evidence.time_to_first_token_ms = std::stod(fields[8]);
            evidence.prompt_throughput_tokens_per_second = std::stod(fields[9]);
            evidence.generation_throughput_tokens_per_second =
                std::stod(fields[10]);
            evidence.peak_resident_memory_bytes = std::stoull(fields[11]);
            evidence.quality_notes = fields[12];
            evidence.power_thermal_notes = fields[13];
            evidence.regression_detected = fields[14] == "true";
            evidence.fallback_verified = fields[15] == "true";
            validate_evidence(feature.name, evidence);
            if (feature.enabled &&
                (evidence.regression_detected ||
                 !evidence.fallback_verified)) {
                throw std::runtime_error(
                    "persisted advanced optimization is unsafely enabled");
            }
            feature.evidence = std::move(evidence);
        } else if (feature.enabled) {
            throw std::runtime_error(
                "persisted advanced optimization is enabled without evidence");
        }
    }
}

std::string advanced_optimization_registry_json(
    const std::vector<AdvancedOptimizationFeature>& features) {
    std::string body = "[";
    bool first = true;
    for (const auto& feature : features) {
        if (!first) body += ",";
        first = false;
        body += "{\"name\":" + json_string(feature.name) +
               ",\"description\":" + json_string(feature.description) +
               ",\"enabled\":" +
               (feature.enabled ? "true" : "false") +
               ",\"requiresEvidence\":" +
               (feature.requires_evidence ? "true" : "false") +
               ",\"implementationAvailable\":" +
               (feature.implementation_available ? "true" : "false") +
               ",\"hasEvidence\":" +
               (feature.evidence.has_value() ? "true" : "false");
        if (feature.evidence.has_value()) {
            const auto& evidence = *feature.evidence;
            body += ",\"evidence\":{\"baselineDescription\":" +
                    json_string(evidence.baseline_description) +
                    ",\"changedSetting\":" +
                    json_string(evidence.changed_setting) +
                    ",\"hostHash\":" + json_string(evidence.host_hash) +
                    ",\"modelSha256\":" +
                    json_string(evidence.model_sha256) +
                    ",\"backendHash\":" +
                    json_string(evidence.backend_hash) +
                    ",\"timeToFirstTokenMs\":" +
                    std::to_string(evidence.time_to_first_token_ms) +
                    ",\"promptThroughputTokensPerSecond\":" +
                    std::to_string(
                        evidence.prompt_throughput_tokens_per_second) +
                    ",\"generationThroughputTokensPerSecond\":" +
                    std::to_string(
                        evidence.generation_throughput_tokens_per_second) +
                    ",\"peakResidentMemoryBytes\":" +
                    std::to_string(evidence.peak_resident_memory_bytes) +
                    ",\"qualityNotes\":" +
                    json_string(evidence.quality_notes) +
                    ",\"powerThermalNotes\":" +
                    json_string(evidence.power_thermal_notes) +
                    ",\"regressionDetected\":" +
                    (evidence.regression_detected ? "true" : "false") +
                    ",\"fallbackVerified\":" +
                    (evidence.fallback_verified ? "true" : "false") + "}";
        }
        body += "}";
    }
    return body + "]";
}

}  // namespace masterai
