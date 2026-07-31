// Phase 19: adaptive hardware/model calibration.
//
// CalibrationService drives a real RunnerSupervisor through a cold load and
// two generate() calls to measure evidence (timings, peak resident memory,
// CPU%/disk bytes), then persists the result as a TuningProfile keyed by
// host/model/backend/build identity. It never applies its own
// recommendation automatically and never overrides the administrator's
// configured memory ceiling -- callers decide whether and when to load a
// model with the resulting LaunchTuning.
#include "masterai.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <set>
#include <stdexcept>
#include <thread>

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
            throw std::runtime_error("persisted tuning profile is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted tuning profile is truncated");
        }
        fields.push_back(value.substr(position, size));
        position += size;
    }
    return fields;
}

std::string profile_key(const std::string& host_hash,
                        const std::string& model_sha256,
                        const std::string& backend_hash,
                        const std::string& build_id) {
    return sha256_hex(host_hash + "|" + model_sha256 + "|" + backend_hash +
                      "|" + build_id);
}

void raise_atomic_maximum(std::atomic<std::uint64_t>& target,
                          const std::uint64_t candidate) {
    std::uint64_t current = target.load();
    while (candidate > current &&
           !target.compare_exchange_weak(current, candidate)) {
    }
}

// A fixed, deterministic filler prompt sized in roughly-4-bytes-per-token
// increments -- exact tokenization is backend-specific, so this is only ever
// used to produce a repeatable relative measurement, never an exact token
// count (prompt_tokens_measured records what the runner actually reports).
std::string padded_prompt(const std::size_t approximate_tokens) {
    std::string prompt = "You are calibrating prompt evaluation throughput. ";
    while (prompt.size() < approximate_tokens * 4U) {
        prompt += "Repeat this fixed calibration phrase without commentary. ";
    }
    return prompt;
}

}  // namespace

TuningProfile safe_default_profile(const std::string& profile_name) {
    static const std::set<std::string> known{"auto", "minimal", "balanced",
                                             "performance"};
    const std::string name = profile_name.empty() ? "balanced" : profile_name;
    if (known.find(name) == known.end()) {
        throw std::invalid_argument("unknown calibration profile name");
    }
    TuningProfile profile;
    profile.profile_name = name;
    if (name == "minimal") {
        profile.recommended_context_length = 2048U;
        profile.recommended_parallel_slots = 1U;
        profile.recommended_idle_unload_seconds = 300U;
    } else if (name == "performance") {
        profile.recommended_context_length = 8192U;
        profile.recommended_parallel_slots = 4U;
        profile.recommended_idle_unload_seconds = 1800U;
    } else {
        // "balanced" and "auto" share the same safe starting point; "auto"
        // only diverges once a real measurement exists to act on.
        profile.recommended_context_length = 4096U;
        profile.recommended_parallel_slots = 2U;
        profile.recommended_idle_unload_seconds = 600U;
    }
    return profile;
}

LaunchTuning launch_tuning_from_profile(const TuningProfile& profile) {
    LaunchTuning tuning;
    tuning.gpu_layers = profile.recommended_gpu_layers;
    tuning.allow_memory_map = profile.recommended_allow_memory_map;
    tuning.allow_memory_lock = profile.recommended_allow_memory_lock;
    tuning.batch_tokens = profile.recommended_batch_tokens;
    return tuning;
}

TuningProfileStore::TuningProfileStore(RecordStore& records)
    : record_store_(&records) {
    restore();
}

void TuningProfileStore::save(const TuningProfile& profile) {
    if (!valid_sha256(profile.host_hash) ||
        !valid_sha256(profile.model_sha256) || profile.backend_hash.empty() ||
        profile.build_id.empty()) {
        throw std::invalid_argument("tuning profile identity is incomplete");
    }
    const auto key = profile_key(profile.host_hash, profile.model_sha256,
                                 profile.backend_hash, profile.build_id);
    profiles_.erase(
        std::remove_if(profiles_.begin(), profiles_.end(),
                       [&](const TuningProfile& existing) {
                           return profile_key(existing.host_hash,
                                              existing.model_sha256,
                                              existing.backend_hash,
                                              existing.build_id) == key;
                       }),
        profiles_.end());
    profiles_.push_back(profile);
    if (record_store_ != nullptr) {
        const auto value = pack(
            {profile.host_hash, profile.model_sha256, profile.backend_hash,
             profile.build_id, profile.profile_name,
             std::to_string(profile.recommended_context_length),
             std::to_string(profile.recommended_parallel_slots),
             std::to_string(profile.recommended_batch_tokens),
             std::to_string(profile.recommended_gpu_layers),
             profile.recommended_allow_memory_map ? "true" : "false",
             profile.recommended_allow_memory_lock ? "true" : "false",
             std::to_string(profile.recommended_idle_unload_seconds),
             std::to_string(profile.cold_load_microseconds),
             std::to_string(profile.prompt_evaluation_microseconds),
             std::to_string(profile.generation_microseconds),
             std::to_string(profile.prompt_tokens_measured),
             std::to_string(profile.generated_tokens_measured),
             std::to_string(profile.peak_resident_memory_bytes),
             std::to_string(profile.peak_commit_bytes),
             std::to_string(profile.page_faults),
             std::to_string(profile.average_cpu_percent),
             std::to_string(profile.disk_read_bytes),
             std::to_string(profile.disk_write_bytes),
             std::to_string(profile.calibrated_at_epoch_seconds)});
        record_store_->put("tuning_profiles", key, value);
    }
}

std::optional<TuningProfile> TuningProfileStore::find(
    const std::string& host_hash, const std::string& model_sha256,
    const std::string& backend_hash, const std::string& build_id) const {
    const auto key = profile_key(host_hash, model_sha256, backend_hash, build_id);
    for (const auto& profile : profiles_) {
        if (profile_key(profile.host_hash, profile.model_sha256,
                        profile.backend_hash, profile.build_id) == key) {
            return profile;
        }
    }
    return std::nullopt;
}

std::vector<TuningProfile> TuningProfileStore::all() const { return profiles_; }

void TuningProfileStore::restore() {
    for (const auto& item : record_store_->list("tuning_profiles")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 24U) {
            throw std::runtime_error("persisted tuning profile is invalid");
        }
        TuningProfile profile;
        profile.host_hash = fields[0];
        profile.model_sha256 = fields[1];
        profile.backend_hash = fields[2];
        profile.build_id = fields[3];
        profile.profile_name = fields[4];
        profile.recommended_context_length =
            static_cast<unsigned int>(std::stoul(fields[5]));
        profile.recommended_parallel_slots =
            static_cast<unsigned int>(std::stoul(fields[6]));
        profile.recommended_batch_tokens =
            static_cast<unsigned int>(std::stoul(fields[7]));
        profile.recommended_gpu_layers =
            static_cast<unsigned int>(std::stoul(fields[8]));
        profile.recommended_allow_memory_map = fields[9] == "true";
        profile.recommended_allow_memory_lock = fields[10] == "true";
        profile.recommended_idle_unload_seconds =
            static_cast<std::uint32_t>(std::stoul(fields[11]));
        profile.cold_load_microseconds = std::stoull(fields[12]);
        profile.prompt_evaluation_microseconds = std::stoull(fields[13]);
        profile.generation_microseconds = std::stoull(fields[14]);
        profile.prompt_tokens_measured = std::stoull(fields[15]);
        profile.generated_tokens_measured = std::stoull(fields[16]);
        profile.peak_resident_memory_bytes = std::stoull(fields[17]);
        profile.peak_commit_bytes = std::stoull(fields[18]);
        profile.page_faults = std::stoull(fields[19]);
        profile.average_cpu_percent = std::stod(fields[20]);
        profile.disk_read_bytes = std::stoull(fields[21]);
        profile.disk_write_bytes = std::stoull(fields[22]);
        profile.calibrated_at_epoch_seconds = std::stoull(fields[23]);
        if (!valid_sha256(profile.host_hash) ||
            !valid_sha256(profile.model_sha256)) {
            throw std::runtime_error("persisted tuning profile violates policy");
        }
        profiles_.push_back(std::move(profile));
    }
}

CalibrationService::CalibrationService(RunnerSupervisor& inference,
                                       TuningProfileStore& store,
                                       HardwareInfo hardware,
                                       std::string backend_hash,
                                       std::string build_id)
    : inference_(inference),
      store_(store),
      hardware_(std::move(hardware)),
      backend_hash_(std::move(backend_hash)),
      build_id_(std::move(build_id)) {
    if (backend_hash_.empty() || build_id_.empty()) {
        throw std::invalid_argument(
            "calibration requires a non-empty backend hash and build id");
    }
}

std::string CalibrationService::host_hash() const {
    return sha256_hex(hardware_info_json(hardware_));
}

TuningProfile CalibrationService::resolve(
    const std::string& model_sha256, const std::string& requested_profile) const {
    if (const auto found =
            store_.find(host_hash(), model_sha256, backend_hash_, build_id_)) {
        return *found;
    }
    return safe_default_profile(requested_profile);
}

TuningProfile CalibrationService::calibrate(
    const ModelRecord& model, const std::string& requested_profile,
    const unsigned int port, const std::atomic_bool& cancellation) {
    TuningProfile profile = safe_default_profile(requested_profile);
    profile.host_hash = host_hash();
    profile.model_sha256 = model.manifest.model_sha256;
    profile.backend_hash = backend_hash_;
    profile.build_id = build_id_;
    const auto tuning = launch_tuning_from_profile(profile);

    std::atomic_bool sampling{true};
    std::atomic<std::uint64_t> peak_resident{0};
    std::thread memory_sampler([&]() {
        while (sampling.load()) {
            raise_atomic_maximum(peak_resident,
                                 inference_.metrics().resident_memory_bytes);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    });
    const auto stop_sampling = [&]() {
        sampling.store(false);
        if (memory_sampler.joinable()) memory_sampler.join();
    };

    try {
        const auto before = probe_process_resources();
        const auto load_start = std::chrono::steady_clock::now();
        inference_.load(model, profile.recommended_context_length, port, 120U,
                        profile.recommended_parallel_slots, tuning);
        profile.cold_load_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - load_start)
                .count());

        GenerationOptions evaluation_options;
        evaluation_options.max_tokens = 1U;
        evaluation_options.temperature = 0.0;
        evaluation_options.seed = 1U;
        const auto evaluation = inference_.generate(
            padded_prompt(256U), evaluation_options, {}, cancellation);
        if (evaluation.cancelled) {
            throw std::runtime_error("calibration cancelled");
        }
        profile.prompt_evaluation_microseconds = evaluation.elapsed_microseconds;
        profile.prompt_tokens_measured = evaluation.prompt_tokens;

        const auto utilization = probe_system_utilization(200U);

        GenerationOptions generation_options;
        generation_options.max_tokens = 64U;
        generation_options.temperature = 0.0;
        generation_options.seed = 1U;
        const auto generation = inference_.generate(
            "Write one short C++17 comment.", generation_options, {},
            cancellation);
        if (generation.cancelled) {
            throw std::runtime_error("calibration cancelled");
        }
        profile.generation_microseconds = generation.elapsed_microseconds;
        profile.generated_tokens_measured = generation.generated_tokens;

        stop_sampling();
        const auto after = probe_process_resources();
        profile.peak_resident_memory_bytes = peak_resident.load();
        profile.peak_commit_bytes = std::max(before.commit_bytes, after.commit_bytes);
        profile.page_faults = after.page_faults;
        profile.average_cpu_percent = utilization.cpu_percent;
        profile.disk_read_bytes = utilization.disk_read_bytes;
        profile.disk_write_bytes = utilization.disk_write_bytes;
        profile.calibrated_at_epoch_seconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::seconds>(
                std::chrono::system_clock::now().time_since_epoch())
                .count());
        inference_.unload();
    } catch (...) {
        stop_sampling();
        inference_.unload();
        throw;
    }

    store_.save(profile);
    return profile;
}

// String fields quoted here (host_hash/model_sha256/backend_hash are hex
// digests, profile_name/build_id are validated identifiers) never contain
// characters that require JSON escaping, matching CacheManager::to_json's
// existing practice of quoting validated identifiers directly.
std::string tuning_profile_json(const TuningProfile& profile) {
    return "{\"hostHash\":\"" + profile.host_hash + "\"" +
          ",\"modelSha256\":\"" + profile.model_sha256 + "\"" +
          ",\"backendHash\":\"" + profile.backend_hash + "\"" +
          ",\"buildId\":\"" + profile.build_id + "\"" +
          ",\"profileName\":\"" + profile.profile_name + "\"" +
          ",\"recommendedContextLength\":" +
          std::to_string(profile.recommended_context_length) +
          ",\"recommendedParallelSlots\":" +
          std::to_string(profile.recommended_parallel_slots) +
          ",\"recommendedBatchTokens\":" +
          std::to_string(profile.recommended_batch_tokens) +
          ",\"recommendedGpuLayers\":" +
          std::to_string(profile.recommended_gpu_layers) +
          ",\"recommendedAllowMemoryMap\":" +
          (profile.recommended_allow_memory_map ? "true" : "false") +
          ",\"recommendedAllowMemoryLock\":" +
          (profile.recommended_allow_memory_lock ? "true" : "false") +
          ",\"recommendedIdleUnloadSeconds\":" +
          std::to_string(profile.recommended_idle_unload_seconds) +
          ",\"coldLoadMicroseconds\":" +
          std::to_string(profile.cold_load_microseconds) +
          ",\"promptEvaluationMicroseconds\":" +
          std::to_string(profile.prompt_evaluation_microseconds) +
          ",\"generationMicroseconds\":" +
          std::to_string(profile.generation_microseconds) +
          ",\"promptTokensMeasured\":" +
          std::to_string(profile.prompt_tokens_measured) +
          ",\"generatedTokensMeasured\":" +
          std::to_string(profile.generated_tokens_measured) +
          ",\"peakResidentMemoryBytes\":" +
          std::to_string(profile.peak_resident_memory_bytes) +
          ",\"peakCommitBytes\":" + std::to_string(profile.peak_commit_bytes) +
          ",\"pageFaults\":" + std::to_string(profile.page_faults) +
          ",\"averageCpuPercent\":" +
          std::to_string(profile.average_cpu_percent) +
          ",\"diskReadBytes\":" + std::to_string(profile.disk_read_bytes) +
          ",\"diskWriteBytes\":" + std::to_string(profile.disk_write_bytes) +
          ",\"calibratedAtEpochSeconds\":" +
          std::to_string(profile.calibrated_at_epoch_seconds) + "}";
}

}  // namespace masterai
