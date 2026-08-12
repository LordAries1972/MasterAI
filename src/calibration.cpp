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
#include <mutex>
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

// Phase 26: ModelLoadMode/PreTouchLevel string forms, used both by
// tuning_profile_json() below and by anything logging/reporting a resolved
// launch decision.
std::string to_string(const ModelLoadMode mode) {
    switch (mode) {
        case ModelLoadMode::streamed: return "streamed";
        case ModelLoadMode::mapped: return "mapped";
        case ModelLoadMode::resident: return "resident";
        case ModelLoadMode::auto_select: return "auto";
    }
    return "unknown";
}

std::string to_string(const PreTouchLevel level) {
    switch (level) {
        case PreTouchLevel::none: return "none";
        case PreTouchLevel::metadata: return "metadata";
        case PreTouchLevel::first_use: return "first_use";
        case PreTouchLevel::layer_window: return "layer_window";
        case PreTouchLevel::full: return "full";
    }
    return "unknown";
}

bool pre_touch_level_backend_actionable(const PreTouchLevel level) noexcept {
    switch (level) {
        case PreTouchLevel::none:
        case PreTouchLevel::metadata:
        case PreTouchLevel::first_use:
        case PreTouchLevel::layer_window:
        case PreTouchLevel::full:
            return true;
    }
    return false;
}

std::optional<std::string> pretouch_gap_reason(const PreTouchLevel level) {
    if (pre_touch_level_backend_actionable(level)) return std::nullopt;
    return "unknown pre-touch level '" + to_string(level) + "'";
}

PreTouchReport pre_touch_model_file(
    const std::filesystem::path& model_file, const PreTouchLevel level,
    const std::atomic_bool& cancellation,
    const std::function<bool()>& should_yield,
    const std::uint64_t maximum_bytes) {
    PreTouchReport report;
    report.level = level;
    report.file_bytes = std::filesystem::file_size(model_file);
    if (level == PreTouchLevel::none || report.file_bytes == 0U) return report;
    const auto started = std::chrono::steady_clock::now();
    constexpr std::uint64_t page_bytes = 4096U;
    constexpr std::uint64_t mapping_window_bytes = 4ULL * 1024ULL * 1024ULL;
    std::uint64_t policy_bytes = 0U;
    if (maximum_bytes > 0U) {
        policy_bytes = std::min(maximum_bytes, report.file_bytes);
    } else if (level == PreTouchLevel::metadata) {
        policy_bytes = std::min<std::uint64_t>(4ULL * 1024ULL * 1024ULL,
                                               report.file_bytes);
    } else if (level == PreTouchLevel::first_use ||
               level == PreTouchLevel::layer_window) {
        policy_bytes = std::min<std::uint64_t>(64ULL * 1024ULL * 1024ULL,
                                               report.file_bytes);
    } else {
        policy_bytes = report.file_bytes;
    }
    std::vector<std::pair<std::uint64_t, std::uint64_t>> ranges;
    if (level == PreTouchLevel::layer_window && policy_bytes < report.file_bytes) {
        const auto windows = std::min<std::uint64_t>(
            8U, std::max<std::uint64_t>(1U, (policy_bytes + page_bytes - 1U) /
                                                page_bytes));
        const auto each = std::min(mapping_window_bytes, policy_bytes / windows);
        for (std::uint64_t index = 0U; index < windows; ++index) {
            const auto offset = windows == 1U
                                    ? 0U
                                    : index * (report.file_bytes - each) /
                                          (windows - 1U);
            const auto length = index + 1U == windows
                                    ? policy_bytes - each * index
                                    : each;
            ranges.emplace_back(offset, length);
        }
    } else {
        for (std::uint64_t offset = 0U; offset < policy_bytes;
             offset += mapping_window_bytes) {
            ranges.emplace_back(
                offset, std::min(mapping_window_bytes, policy_bytes - offset));
        }
    }
    volatile std::uint8_t page_sink = 0U;
    for (const auto& range : ranges) {
        if (cancellation.load(std::memory_order_acquire) ||
            (should_yield && should_yield())) {
            report.cancelled = true;
            break;
        }
        MappedBufferView mapped(model_file, range.first, range.second);
        std::uint64_t range_bytes_touched = 0U;
        for (std::size_t offset = 0U; offset < mapped.size();
             offset += static_cast<std::size_t>(page_bytes)) {
            if (cancellation.load(std::memory_order_acquire)) {
                report.cancelled = true;
                break;
            }
            page_sink = static_cast<std::uint8_t>(page_sink ^ mapped.data()[offset]);
            ++report.pages_touched;
            range_bytes_touched += std::min<std::uint64_t>(
                page_bytes, mapped.size() - offset);
        }
        report.bytes_touched += range_bytes_touched;
        if (report.cancelled) break;
    }
    static_cast<void>(page_sink);
    report.elapsed_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - started).count());
    return report;
}

// Phase 26: evidence-based load-mode selection. See the declaration in
// masterai.hpp for the full rationale; this only ever reasons from the
// arguments given, never touches global/process state, so it is trivially
// unit-testable with synthetic evidence.
ModelLoadMode select_load_mode(const StorageLatencyProfile& storage,
                               const std::uint64_t available_ram_bytes,
                               const std::uint64_t model_size_bytes) noexcept {
    // Unknown model size: nothing to reason about size-vs-RAM fit against,
    // so stay with the always-safe pre-Phase-26 default rather than guess.
    if (model_size_bytes == 0U) return ModelLoadMode::mapped;

    const bool slow_storage =
        storage.storage_class == "network" || storage.storage_class == "removable" ||
        storage.storage_class == "optical" ||
        storage.measured_read_latency_us >= 3000.0;  // HDD-shaped, see async_storage.cpp
    const bool fast_storage =
        !slow_storage && storage.measured_read_latency_us > 0.0 &&
        storage.measured_read_latency_us < 200.0;  // NVMe-shaped

    const bool barely_fits = available_ram_bytes >= model_size_bytes;
    if (!barely_fits) {
        // Not enough RAM to comfortably hold the whole model: prefer
        // streamed reads over letting mmap pressure the OS into evicting
        // pages mid-generation.
        return ModelLoadMode::streamed;
    }

    const bool comfortable_fit = available_ram_bytes >= model_size_bytes * 2U;
    if (comfortable_fit && slow_storage) {
        // Plenty of headroom, and re-faulting pages from this storage class
        // would be expensive: pin the model resident up front instead of
        // paying repeated slow-disk page faults during generation.
        return ModelLoadMode::resident;
    }
    if (comfortable_fit && !fast_storage) {
        // Headroom to spare and storage isn't clearly fast (moderate SSD-
        // shaped latency, unmeasured, etc.): resident still avoids
        // uncertain on-demand fault latency without much downside given the
        // available RAM.
        return ModelLoadMode::resident;
    }
    // Enough RAM but not much to spare, or storage is fast enough that
    // demand-paged mmap faults are cheap: the default, lowest-commitment
    // mode.
    return ModelLoadMode::mapped;
}

// See the declaration in masterai.hpp for the full rationale. Fraction of
// detected VRAM actually offered to a model's weights: the remainder is
// deliberately left unallocated as headroom for the backend's own CUDA/HIP/
// Vulkan context overhead plus any KV-cache/activation memory the runner
// will also need on the GPU during generation, neither of which this
// function has evidence about at load-selection time.
constexpr double kUsableVramFraction = 0.7;

unsigned int select_gpu_layers(const HardwareInfo& hardware,
                               const std::string& required_gpu_backend,
                               const std::uint64_t model_size_bytes) noexcept {
    if (model_size_bytes == 0U) return 0U;
    if (hardware.gpu_backends.empty()) return 0U;
    if (!required_gpu_backend.empty() &&
        std::find(hardware.gpu_backends.begin(), hardware.gpu_backends.end(),
                  required_gpu_backend) == hardware.gpu_backends.end()) {
        return 0U;
    }
    if (hardware.gpu_memory_mib == 0U) return 0U;

    const auto usable_vram_bytes = static_cast<std::uint64_t>(
        static_cast<double>(hardware.gpu_memory_mib) * 1024.0 * 1024.0 *
        kUsableVramFraction);
    if (model_size_bytes > usable_vram_bytes) return 0U;
    return kGpuLayersOffloadAll;
}

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
    // Phase 26: load_mode/pre_touch ride along as advisory metadata -- their
    // *effect* on the actual llama-server launch is already fully expressed
    // through allow_memory_map/allow_memory_lock above (set consistently
    // with them by resolve() below), so build_launch_spec() needs no
    // separate argument branch for these two fields.
    tuning.load_mode = profile.recommended_load_mode;
    tuning.pre_touch = profile.recommended_pre_touch;
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
             std::to_string(profile.calibrated_at_epoch_seconds),
             // Phase 26: appended at the end so a pre-Phase-26 reader would
             // simply never see these fields; restore() below requires
             // exactly the new count, so a genuinely old record instead
             // fails loudly rather than silently truncating (consistent
             // with this store's existing "malformed record throws"
             // policy).
             to_string(profile.recommended_load_mode),
             to_string(profile.recommended_pre_touch),
             // Phase 19 real-hardware-class evidence: appended at the end,
             // same "old readers never see it, restore() requires the new
             // count" convention the Phase 26 fields above already used.
             profile.gpu_telemetry_available ? "true" : "false",
             profile.gpu_vendor,
             std::to_string(profile.average_gpu_utilization_percent),
             std::to_string(profile.peak_gpu_temperature_celsius)});
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

namespace {

// Phase 26: parses to_string(ModelLoadMode)/to_string(PreTouchLevel) back
// into their enum values for restore() below. Throws on anything else
// rather than silently defaulting, matching this store's existing
// "malformed record throws" policy for every other field.
ModelLoadMode parse_load_mode(const std::string& value) {
    if (value == "streamed") return ModelLoadMode::streamed;
    if (value == "mapped") return ModelLoadMode::mapped;
    if (value == "resident") return ModelLoadMode::resident;
    if (value == "auto") return ModelLoadMode::auto_select;
    throw std::runtime_error("persisted tuning profile has an unknown load mode");
}

PreTouchLevel parse_pre_touch_level(const std::string& value) {
    if (value == "none") return PreTouchLevel::none;
    if (value == "metadata") return PreTouchLevel::metadata;
    if (value == "first_use") return PreTouchLevel::first_use;
    if (value == "layer_window") return PreTouchLevel::layer_window;
    if (value == "full") return PreTouchLevel::full;
    throw std::runtime_error(
        "persisted tuning profile has an unknown pre-touch level");
}

}  // namespace

void TuningProfileStore::restore() {
    for (const auto& item : record_store_->list("tuning_profiles")) {
        const auto fields = unpack(item.second);
        // Phase 19 (2026-08-13): TuningProfile gained four GPU-telemetry
        // fields, appended after the Phase 26 load-mode/pre-touch pair (26
        // fields -> 30). A hard throw here on any length other than the
        // current schema would mean simply *building* a newer binary while
        // real calibration evidence from an older schema is still on disk
        // takes the whole control plane down at startup -- even though a
        // mismatched schema almost always also carries a stale build_id, so
        // TuningProfileStore::find() would never have matched it anyway.
        // Recognized older schemas (currently just the pre-Phase-19 26-field
        // one) are parsed with their new fields defaulted; anything else is
        // logged and dropped rather than crashing the server. This profile
        // is only ever advisory evidence a fresh calibrate() regenerates --
        // never durable data whose loss would be silent or irreversible.
        if (fields.size() != 30U && fields.size() != 26U) {
            log(LogLevel::warning, "tuning_profile.restore.skipped",
               "unrecognized persisted tuning profile schema (" +
                   std::to_string(fields.size()) + " fields); dropping stale "
                   "record, recalibration will regenerate it");
            continue;
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
        profile.recommended_load_mode = parse_load_mode(fields[24]);
        profile.recommended_pre_touch = parse_pre_touch_level(fields[25]);
        if (fields.size() == 30U) {
            profile.gpu_telemetry_available = fields[26] == "true";
            profile.gpu_vendor = fields[27];
            profile.average_gpu_utilization_percent = std::stod(fields[28]);
            profile.peak_gpu_temperature_celsius = std::stoi(fields[29]);
        }
        // else: pre-Phase-19 26-field record -- GPU fields stay at their
        // struct defaults (gpu_telemetry_available == false), an honest
        // "not measured by this old record" rather than a fabricated value.
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
                                       std::string build_id,
                                       std::string accelerator_policy)
    : inference_(inference),
      store_(store),
      hardware_(std::move(hardware)),
      backend_hash_(std::move(backend_hash)),
      build_id_(std::move(build_id)),
      accelerator_policy_(std::move(accelerator_policy)) {
    if (backend_hash_.empty() || build_id_.empty()) {
        throw std::invalid_argument(
            "calibration requires a non-empty backend hash and build id");
    }
    if (accelerator_policy_ != "auto" && accelerator_policy_ != "cpu_only" &&
        accelerator_policy_ != "gpu_allowed") {
        throw std::invalid_argument("accelerator policy is outside policy");
    }
}

std::string CalibrationService::host_hash() const {
    return sha256_hex(hardware_info_json(hardware_));
}

TuningProfile CalibrationService::resolve(
    const std::string& model_sha256, const std::string& requested_profile,
    const StorageLatencyProfile* storage, const std::uint64_t available_ram_bytes,
    const std::uint64_t model_size_bytes,
    const std::string& required_gpu_backend) const {
    if (const auto found =
            store_.find(host_hash(), model_sha256, backend_hash_, build_id_)) {
        // A persisted profile already reflects a real measurement -- evidence
        // passed to this call never overrides it, only fills the gap when
        // there isn't one yet (see the safe-default branch below). Phase
        // 30A: a persisted profile calibrated under a GPU-permitting policy
        // that recommends a nonzero GPU layer count is rejected outright
        // under cpu_only rather than silently returned with GPU layers
        // zeroed out -- the administrator's cpu_only choice must never be
        // silently overridden by stale evidence.
        if (accelerator_policy_ == "cpu_only" &&
            found->recommended_gpu_layers > 0U) {
            throw std::runtime_error(
                "persisted tuning profile recommends GPU offload but "
                "hardware.acceleratorPolicy is cpu_only; recalibrate or "
                "delete the stale profile before loading this model");
        }
        return *found;
    }
    TuningProfile profile = safe_default_profile(requested_profile);
    profile.recommended_gpu_layers =
        accelerator_policy_ == "cpu_only"
            ? 0U
            : select_gpu_layers(hardware_, required_gpu_backend, model_size_bytes);
    if (storage != nullptr) {
        profile.recommended_load_mode =
            select_load_mode(*storage, available_ram_bytes, model_size_bytes);
        // Phase 26: keep load_mode and the two booleans it is actually
        // realized through (see LaunchTuning/launch_tuning_from_profile)
        // consistent with each other -- "resident" implies both mmap and
        // mlock, "streamed" implies no mmap, and "resident" also means the
        // pre-touch policy is "full" (the one pre-touch level that is
        // itself backend-actionable, via that same mlock flag).
        switch (profile.recommended_load_mode) {
            case ModelLoadMode::resident:
                profile.recommended_allow_memory_map = true;
                profile.recommended_allow_memory_lock = true;
                profile.recommended_pre_touch = PreTouchLevel::full;
                break;
            case ModelLoadMode::streamed:
                profile.recommended_allow_memory_map = false;
                profile.recommended_allow_memory_lock = false;
                profile.recommended_pre_touch = PreTouchLevel::none;
                break;
            case ModelLoadMode::mapped:
            case ModelLoadMode::auto_select:
            default:
                profile.recommended_allow_memory_map = true;
                profile.recommended_allow_memory_lock = false;
                profile.recommended_pre_touch = PreTouchLevel::none;
                break;
        }
    }
    return profile;
}

TuningProfile CalibrationService::calibrate(
    const ModelRecord& model, const std::string& requested_profile,
    const unsigned int port, const std::atomic_bool& cancellation) {
    TuningProfile profile = safe_default_profile(requested_profile);
    profile.host_hash = host_hash();
    profile.model_sha256 = model.manifest.model_sha256;
    profile.backend_hash = backend_hash_;
    profile.build_id = build_id_;
    profile.recommended_gpu_layers =
        accelerator_policy_ == "cpu_only"
            ? 0U
            : select_gpu_layers(hardware_, model.manifest.required_gpu_backend,
                                model.manifest.model_size_bytes);
    const auto tuning = launch_tuning_from_profile(profile);

    std::atomic_bool sampling{true};
    std::atomic<std::uint64_t> peak_resident{0};
    // Phase 19 (real-hardware-class evidence): GPU utilization/temperature
    // sampled at the same cadence and over the same window as the existing
    // resident-memory sampler, via the now-approved vendor SDKs
    // (gpu_vendor.cpp / ADR-0001). A std::mutex guards the small aggregate
    // state below rather than atomics, since averaging utilization across
    // however many GPU devices a host has needs more than a single
    // read-modify-write.
    std::atomic_bool gpu_available{false};
    std::mutex gpu_sample_mutex;
    std::string gpu_vendor_seen;
    double gpu_utilization_sum{0.0};
    std::uint64_t gpu_utilization_samples{0};
    int gpu_peak_temperature{0};
    std::thread memory_sampler([&]() {
        while (sampling.load()) {
            raise_atomic_maximum(peak_resident,
                                 inference_.metrics().resident_memory_bytes);
            for (const auto& telemetry : probe_gpu_vendor_telemetry()) {
                if (!telemetry.available) continue;
                gpu_available.store(true);
                std::lock_guard<std::mutex> lock(gpu_sample_mutex);
                if (gpu_vendor_seen.empty()) gpu_vendor_seen = telemetry.vendor;
                gpu_utilization_sum += static_cast<double>(telemetry.utilization_percent);
                ++gpu_utilization_samples;
                gpu_peak_temperature =
                    std::max(gpu_peak_temperature, telemetry.temperature_celsius);
            }
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
                        profile.recommended_parallel_slots, tuning,
                        accelerator_policy_);
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
        profile.gpu_telemetry_available = gpu_available.load();
        {
            std::lock_guard<std::mutex> lock(gpu_sample_mutex);
            profile.gpu_vendor = gpu_vendor_seen;
            profile.average_gpu_utilization_percent =
                gpu_utilization_samples > 0U
                    ? gpu_utilization_sum / static_cast<double>(gpu_utilization_samples)
                    : 0.0;
            profile.peak_gpu_temperature_celsius = gpu_peak_temperature;
        }
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
          std::to_string(profile.calibrated_at_epoch_seconds) +
          ",\"recommendedLoadMode\":\"" +
          to_string(profile.recommended_load_mode) + "\"" +
          ",\"recommendedPreTouch\":\"" +
          to_string(profile.recommended_pre_touch) + "\"" +
          ",\"gpuTelemetryAvailable\":" +
          (profile.gpu_telemetry_available ? "true" : "false") +
          // gpu_vendor is always "nvidia", "amd", or empty (see
          // GpuVendorTelemetry's comment in masterai.hpp) -- a fixed literal
          // set, never escaped input, matching how host_hash/profile_name
          // above are quoted directly.
          ",\"gpuVendor\":\"" + profile.gpu_vendor + "\"" +
          ",\"averageGpuUtilizationPercent\":" +
          std::to_string(profile.average_gpu_utilization_percent) +
          ",\"peakGpuTemperatureCelsius\":" +
          std::to_string(profile.peak_gpu_temperature_celsius) + "}";
}

}  // namespace masterai
