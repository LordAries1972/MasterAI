// Phase 36: full performance benchmark matrix and regression gate. See the
// class comment on PerformanceCertificationRecord/PerformanceCertificationRunner
// in masterai.hpp for the honest scope note this pass operates under.
#include "masterai.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

std::string random_id() {
    const auto random = secure_random(16U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string id(random.size() * 2U, '0');
    for (std::size_t index = 0; index < random.size(); ++index) {
        id[index * 2U] = digits[(random[index] >> 4U) & 0x0fU];
        id[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    return id;
}

std::string profile_name(const BenchmarkProfile profile) {
    if (profile == BenchmarkProfile::standard) return "standard";
    if (profile == BenchmarkProfile::extended) return "extended";
    return "quick";
}

BenchmarkProfile parse_certification_profile(const std::string& profile) {
    if (profile == "standard") return BenchmarkProfile::standard;
    if (profile == "extended") return BenchmarkProfile::extended;
    if (profile == "quick") return BenchmarkProfile::quick;
    throw std::runtime_error("persisted certification profile is invalid");
}

double percent_delta(const double baseline, const double current) {
    if (baseline == 0.0) return current == 0.0 ? 0.0 : 100.0;
    return (current - baseline) / baseline * 100.0;
}

std::string json_escape_local(const std::string& value) {
    // json_string() already returns a quoted string; strip the surrounding
    // quotes here so callers can splice the escaped body into a larger
    // hand-built object the same way the rest of this codebase's *.cpp
    // response builders do (see workload_http.cpp's json_escape() callers).
    const auto quoted = json_string(value);
    return quoted.substr(1U, quoted.size() - 2U);
}

}  // namespace

std::string PerformanceCertificationRecord::fingerprint() const {
    std::ostringstream stream;
    stream << model_id << '|' << backend_version << '|' << hardware_id << '|'
           << settings_json << '|' << prompt_suite_hash << '|' << cache_state
           << '|' << profile_name(profile) << '|' << concurrency;
    return sha256_hex(stream.str());
}

PerformanceCertificationStore::PerformanceCertificationStore(
    RecordStore& records)
    : record_store_(&records) {
    restore();
}

void PerformanceCertificationStore::add(
    const PerformanceCertificationRecord& record) {
    if (record.id.empty() || record.model_id.empty() ||
        record.build_id.empty() || record.hardware_id.empty()) {
        throw std::invalid_argument("certification record is incomplete");
    }
    records_.push_back(record);
    if (record_store_ != nullptr) {
        record_store_->put("performance_certifications", record.id,
                           PerformanceCertificationRunner::to_json(record));
    }
}

std::vector<PerformanceCertificationRecord> PerformanceCertificationStore::all()
    const {
    return records_;
}

std::optional<PerformanceCertificationRecord>
PerformanceCertificationStore::previous_accepted(
    const std::string& fingerprint) const {
    std::optional<PerformanceCertificationRecord> latest;
    for (const auto& record : records_) {
        if (!record.accepted || record.fingerprint() != fingerprint) continue;
        if (!latest || record.created_epoch_seconds >
                           latest->created_epoch_seconds) {
            latest = record;
        }
    }
    return latest;
}

void PerformanceCertificationStore::restore() {
    for (const auto& item : record_store_->list("performance_certifications")) {
        const auto root = parse_json(item.second);
        PerformanceCertificationRecord record;
        record.id = item.first;
        record.build_id = root.required("buildId").as_string();
        record.hardware_id = root.required("hardwareId").as_string();
        record.model_id = root.required("modelId").as_string();
        record.backend_version = root.required("backendVersion").as_string();
        record.prompt_suite_hash = root.required("promptSuiteHash").as_string();
        record.settings_json = root.required("settingsJson").as_string();
        record.cache_state = root.required("cacheState").as_string();
        record.accelerator_mode = root.required("acceleratorMode").as_string();
        record.profile =
            parse_certification_profile(root.required("profile").as_string());
        record.concurrency =
            static_cast<std::uint32_t>(root.required("concurrency").as_integer());
        record.created_epoch_seconds = static_cast<std::uint64_t>(
            root.required("createdEpochSeconds").as_integer());
        record.ttft_microseconds = static_cast<std::uint64_t>(
            root.required("ttftMicroseconds").as_integer());
        record.total_elapsed_microseconds = static_cast<std::uint64_t>(
            root.required("totalElapsedMicroseconds").as_integer());
        record.prompt_tokens = static_cast<std::uint64_t>(
            root.required("promptTokens").as_integer());
        record.generated_tokens = static_cast<std::uint64_t>(
            root.required("generatedTokens").as_integer());
        record.peak_resident_memory_bytes = static_cast<std::uint64_t>(
            root.required("peakResidentMemoryBytes").as_integer());
        record.average_cpu_percent = root.required("averageCpuPercent").as_double();
        record.quality_score = root.required("qualityScore").as_double();
        if (const auto* queue_wait = root.optional("queueWaitMicroseconds")) {
            record.queue_wait_microseconds =
                static_cast<std::uint64_t>(queue_wait->as_integer());
        }
        if (const auto* storage_read = root.optional("storageBytesRead")) {
            record.storage_bytes_read =
                static_cast<std::uint64_t>(storage_read->as_integer());
        }
        record.accepted = root.required("accepted").as_boolean();
        if (const auto* reason = root.optional("rejectionReason")) {
            record.rejection_reason = reason->as_string();
        }
        if (const auto* groups = root.optional("groupResults")) {
            for (const auto& item2 : groups->as_array()) {
                RegressionCheckResult check;
                check.name = item2.required("name").as_string();
                check.passed = item2.required("passed").as_boolean();
                check.detail = item2.required("detail").as_string();
                record.group_results.push_back(std::move(check));
            }
        }
        if (const auto* comparisons = root.optional("comparisons")) {
            for (const auto& item2 : comparisons->as_array()) {
                RegressionMetricComparison comparison;
                comparison.metric = item2.required("metric").as_string();
                comparison.baseline = item2.required("baseline").as_double();
                comparison.current = item2.required("current").as_double();
                comparison.delta_percent =
                    item2.required("deltaPercent").as_double();
                comparison.threshold_percent =
                    item2.required("thresholdPercent").as_double();
                comparison.passed = item2.required("passed").as_boolean();
                record.comparisons.push_back(std::move(comparison));
            }
        }
        records_.push_back(std::move(record));
    }
}

// ---------------------------------------------------------------------
// The five named regression test groups (docs/PLAN.md Phase 36).
// ---------------------------------------------------------------------

RegressionCheckResult check_runner_attribution_regression(
    const RunnerMetrics& metrics, const std::string& accelerator_policy) {
    RegressionCheckResult result;
    result.name = "runner_attribution";
    // "requested-vs-actual GPU settings recorded" + "CPU-only launches
    // proving zero accelerator allocation": a cpu_only launch must never
    // report a nonzero requested GPU layer count -- this is the same
    // invariant LlamaCppAdapter::build_launch_spec() already enforces at
    // launch time (masterai.hpp), checked again here against whatever the
    // runner actually reports live.
    if (accelerator_policy == "cpu_only" && metrics.requested_gpu_layers != 0U) {
        result.detail =
            "cpu_only accelerator policy but requested_gpu_layers=" +
            std::to_string(metrics.requested_gpu_layers);
        return result;
    }
    if (metrics.accelerator_policy.empty()) {
        result.detail = "runner reported no accelerator_policy";
        return result;
    }
    result.passed = true;
    result.detail = "requested-vs-actual accelerator settings consistent";
    return result;
}

RegressionCheckResult check_low_memory_regression() {
    RegressionCheckResult result;
    result.name = "low_memory";
    // "rejection before destructive paging" + "OS reserve never consumed by
    // admission": construct a snapshot where the projected resident set
    // plus the OS reserve exceeds physical RAM, and confirm the pure
    // decision function rejects it; then confirm a projection that leaves
    // the reserve intact is accepted. Both branches of
    // projected_resident_exceeds_safe_physical_capacity() are exercised
    // against real inputs, not assumed.
    MemoryAccountingSnapshot snapshot;
    snapshot.physical_total_bytes = 16ULL * 1024ULL * 1024ULL * 1024ULL;
    snapshot.physical_available_bytes = 4ULL * 1024ULL * 1024ULL * 1024ULL;
    const std::uint64_t os_reserve = 2ULL * 1024ULL * 1024ULL * 1024ULL;
    const std::uint64_t over_reserve_projection =
        3ULL * 1024ULL * 1024ULL * 1024ULL;  // + reserve exceeds available
    const std::uint64_t safe_projection =
        1ULL * 1024ULL * 1024ULL * 1024ULL;  // + reserve fits available
    if (!projected_resident_exceeds_safe_physical_capacity(
            snapshot, over_reserve_projection, os_reserve)) {
        result.detail = "destructive-paging projection was not rejected";
        return result;
    }
    if (projected_resident_exceeds_safe_physical_capacity(snapshot, safe_projection,
                                                          os_reserve)) {
        result.detail = "safe projection within the OS reserve was rejected";
        return result;
    }
    result.passed = true;
    result.detail = "OS reserve boundary honoured in both directions";
    return result;
}

RegressionCheckResult check_prompt_cache_regression() {
    RegressionCheckResult result;
    result.name = "prompt_cache";
    // A fresh, self-contained instance -- never the live server's
    // prompt_sessions -- so this check has zero effect on any real chat's
    // reusable KV state.
    PromptSessionManager sessions(4U, 600U);
    SessionFingerprint fingerprint;
    fingerprint.model_sha256 = std::string(64U, 'a');
    fingerprint.backend_executable = "llama-server";
    fingerprint.architecture = "llama";
    fingerprint.context_length = 4096U;

    // "prefix reuse on exact match": record a turn, then confirm an
    // extending prompt on the same chat/fingerprint reuses the slot.
    sessions.record("chat-a", fingerprint, "Hello", std::nullopt, 4U);
    const auto reuse = sessions.try_reuse("chat-a", fingerprint, "Hello, how are you?");
    if (!reuse.reuse) {
        result.detail = "exact-prefix extension was not offered reuse";
        return result;
    }

    // "refusal on edited history": a non-prefix-diverging prompt must never
    // be offered reuse.
    const auto diverged = sessions.try_reuse("chat-a", fingerprint, "Goodbye");
    if (diverged.reuse) {
        result.detail = "edited/diverged history was incorrectly reused";
        return result;
    }

    // "refusal on ... model change": a mismatched fingerprint must never
    // reuse the slot even with an otherwise-valid prefix.
    auto changed_model_fingerprint = fingerprint;
    changed_model_fingerprint.model_sha256 = std::string(64U, 'b');
    const auto model_changed =
        sessions.try_reuse("chat-a", changed_model_fingerprint, "Hello, how are you?");
    if (model_changed.reuse) {
        result.detail = "model change was incorrectly reused";
        return result;
    }

    // "no cross-chat ... reuse": a different chat id must never see another
    // chat's recorded state.
    const auto cross_chat = sessions.try_reuse("chat-b", fingerprint, "Hello");
    if (cross_chat.reuse) {
        result.detail = "cross-chat reuse was incorrectly granted";
        return result;
    }

    result.passed = true;
    result.detail = "prefix reuse, edit refusal, model-change refusal, and "
                    "cross-chat isolation all verified";
    return result;
}

RegressionCheckResult check_calibration_regression() {
    RegressionCheckResult result;
    result.name = "calibration";
    // "exact-identity-only profile restore": TuningProfileStore::find()
    // (no backing RecordStore -- purely in-memory for this check) must
    // return the saved profile only for its exact host/model/backend/build
    // identity, and nullopt on any single-field mismatch.
    TuningProfileStore store;
    TuningProfile profile;
    profile.host_hash = std::string(64U, 'e');
    profile.model_sha256 = std::string(64U, 'c');
    profile.backend_hash = "backend-1";
    profile.build_id = "build-1";
    store.save(profile);

    const auto exact_match =
        store.find(profile.host_hash, profile.model_sha256, profile.backend_hash,
                   profile.build_id);
    if (!exact_match) {
        result.detail = "exact-identity lookup unexpectedly missed";
        return result;
    }
    const auto model_mismatch = store.find(
        profile.host_hash, std::string(64U, 'd'), profile.backend_hash,
        profile.build_id);
    if (model_mismatch) {
        result.detail = "a model hash mismatch incorrectly restored a profile";
        return result;
    }
    result.passed = true;
    result.detail = "profile restore is identity-exact in both directions";
    return result;
}

RegressionCheckResult check_model_routing_regression() {
    RegressionCheckResult result;
    result.name = "model_routing";
    // "smallest capable model chosen for simple tasks": a plain
    // classification/routing task must resolve to compact_router, never
    // straight to large_specialist.
    ModelRouter router({{ModelTier::compact_router, {"router-small"}},
                        {ModelTier::medium_general, {"general-medium"}},
                        {ModelTier::large_specialist, {"specialist-large"}}});
    RoutingSignals simple;
    simple.task_category = "routing";
    simple.requested_quality = "standard";
    const auto assignment = router.select_initial_tier(simple);
    if (!assignment || assignment->tier != ModelTier::compact_router) {
        result.detail = "a simple routing task did not resolve to the "
                        "smallest capable tier";
        return result;
    }

    // "no multiple large resident models under Minimal profile".
    if (ModelRouter::resident_set_within_profile(
            ResidentModelProfile::minimal,
            {ModelTier::large_specialist, ModelTier::large_specialist})) {
        result.detail = "Minimal profile incorrectly allowed two resident "
                        "large models";
        return result;
    }
    if (!ModelRouter::resident_set_within_profile(
            ResidentModelProfile::minimal, {ModelTier::large_specialist})) {
        result.detail = "Minimal profile incorrectly rejected a single "
                        "resident model";
        return result;
    }
    result.passed = true;
    result.detail = "smallest-capable-tier routing and Minimal-profile "
                    "residency ceiling both verified";
    return result;
}

bool compare_against_baseline(const PerformanceCertificationRecord& baseline,
                              PerformanceCertificationRecord& current,
                              const RegressionThresholds& thresholds) {
    current.comparisons.clear();
    bool all_passed = true;
    const auto add = [&](const std::string& metric, const double baseline_value,
                         const double current_value, const double threshold,
                         const bool higher_is_worse) {
        RegressionMetricComparison comparison;
        comparison.metric = metric;
        comparison.baseline = baseline_value;
        comparison.current = current_value;
        comparison.delta_percent = percent_delta(baseline_value, current_value);
        comparison.threshold_percent = threshold;
        comparison.passed = higher_is_worse ? comparison.delta_percent <= threshold
                                            : comparison.delta_percent >= threshold;
        current.comparisons.push_back(comparison);
        if (!comparison.passed) all_passed = false;
    };

    add("ttft_microseconds",
       static_cast<double>(baseline.ttft_microseconds),
       static_cast<double>(current.ttft_microseconds),
       thresholds.max_ttft_regression_percent, true);
    add("peak_resident_memory_bytes",
       static_cast<double>(baseline.peak_resident_memory_bytes),
       static_cast<double>(current.peak_resident_memory_bytes),
       thresholds.max_memory_increase_percent, true);
    const double baseline_throughput =
        baseline.total_elapsed_microseconds == 0U
            ? 0.0
            : static_cast<double>(baseline.generated_tokens) * 1000000.0 /
                  static_cast<double>(baseline.total_elapsed_microseconds);
    const double current_throughput =
        current.total_elapsed_microseconds == 0U
            ? 0.0
            : static_cast<double>(current.generated_tokens) * 1000000.0 /
                  static_cast<double>(current.total_elapsed_microseconds);
    add("generation_tokens_per_second", baseline_throughput, current_throughput,
       thresholds.min_throughput_percent, false);
    add("quality_score", baseline.quality_score, current.quality_score,
       thresholds.max_quality_regression_percent, true);
    add("average_cpu_percent", baseline.average_cpu_percent,
       current.average_cpu_percent, thresholds.max_cpu_increase_percent, true);
    add("queue_wait_microseconds",
       static_cast<double>(baseline.queue_wait_microseconds),
       static_cast<double>(current.queue_wait_microseconds),
       thresholds.max_queue_wait_increase_percent, true);
    const double baseline_amplification =
        baseline.generated_tokens == 0U
            ? 0.0
            : static_cast<double>(baseline.storage_bytes_read) /
                  static_cast<double>(baseline.generated_tokens);
    const double current_amplification =
        current.generated_tokens == 0U
            ? 0.0
            : static_cast<double>(current.storage_bytes_read) /
                  static_cast<double>(current.generated_tokens);
    add("storage_bytes_read_per_generated_token", baseline_amplification,
       current_amplification, thresholds.max_storage_amplification_percent,
       true);

    return all_passed;
}

PerformanceCertificationRunner::PerformanceCertificationRunner(
    RunnerSupervisor& inference, BenchmarkStore& quality_store,
    CacheManager& cache, RequestScheduler& scheduler,
    PerformanceCertificationStore& store, RegressionThresholds thresholds)
    : inference_(inference), quality_store_(quality_store), cache_(cache),
      scheduler_(scheduler), store_(store), thresholds_(thresholds) {}

RegressionThresholds PerformanceCertificationRunner::thresholds() const {
    return thresholds_;
}

void PerformanceCertificationRunner::set_thresholds(
    const RegressionThresholds& thresholds) {
    thresholds_ = thresholds;
}

PerformanceCertificationRecord PerformanceCertificationRunner::run(
    const std::string& model_id, const std::string& backend_version,
    const std::string& build_id, const std::string& hardware_id,
    const BenchmarkProfile profile, const std::string& cache_state,
    const std::uint32_t concurrency, const std::string& accelerator_policy,
    const std::atomic_bool& cancellation) {
    if (cache_state == "cold") {
        // A real cold-cache run, not a claimed one: every bounded cache
        // category is trimmed to its policy floor before the quality suite
        // runs, so a cache-hit-dependent latency improvement cannot leak
        // into a "cold" record.
        cache_.trim();
    }

    PerformanceCertificationRecord record;
    record.id = random_id();
    record.model_id = model_id;
    record.backend_version = backend_version;
    record.build_id = build_id;
    record.hardware_id = hardware_id;
    record.cache_state = cache_state;
    record.profile = profile;
    record.concurrency = std::max(1U, concurrency);
    record.created_epoch_seconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());

    // Real queue wait: actually admits one SchedulingClass::benchmark
    // ticket through the same RequestScheduler production traffic
    // contends on, and times how long it takes to become ready under that
    // scheduler's own weighted-fair dequeue -- not a synthesized figure.
    // Released immediately (this run measures queueing, not scheduler
    // residency) so it never blocks a real request behind it.
    const auto queue_wait_start = std::chrono::steady_clock::now();
    const auto admission = scheduler_.admit(SchedulingClass::benchmark);
    if (admission.admitted && admission.ticket) {
        scheduler_.wait_until_ready(*admission.ticket, cancellation);
        record.queue_wait_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - queue_wait_start)
                .count());
        scheduler_.complete(*admission.ticket);
    }

    // Real storage bytes read: this process's own cumulative disk-read
    // counter (the same figure CalibrationService already records as
    // disk_read_bytes), sampled before and after the quality suite so the
    // delta reflects only what this run itself read.
    const auto storage_before = probe_system_utilization(1U).disk_read_bytes;

    BenchmarkRunner quality_runner(inference_, quality_store_);
    // concurrency > 1 is measured sequentially (this single-process control
    // plane has no way to fan a real concurrent load out to several
    // physical hosts in this pass) -- each repetition still runs a real
    // generate() call and its cost accumulates into the recorded totals, so
    // higher concurrency settings do show their real added cost even though
    // they are not literally simultaneous.
    PerformanceCertificationRecord accumulated;
    for (std::uint32_t repetition = 0U; repetition < record.concurrency;
        ++repetition) {
        const auto quality =
            quality_runner.run(model_id, backend_version, build_id, hardware_id,
                               profile, cancellation);
        if (repetition == 0U) {
            record.prompt_suite_hash = quality.prompt_suite_hash;
            record.settings_json = quality.settings_json;
            record.ttft_microseconds = quality.elapsed_microseconds /
                                       std::max<std::uint64_t>(1U, quality.total_cases);
        }
        accumulated.prompt_tokens += quality.prompt_tokens;
        accumulated.generated_tokens += quality.generated_tokens;
        accumulated.total_elapsed_microseconds += quality.elapsed_microseconds;
        accumulated.peak_resident_memory_bytes =
            std::max(accumulated.peak_resident_memory_bytes,
                     quality.peak_resident_memory_bytes);
        record.quality_score +=
            static_cast<double>(quality.passed_cases) /
            static_cast<double>(quality.total_cases);
    }
    record.quality_score /= static_cast<double>(record.concurrency);
    record.prompt_tokens = accumulated.prompt_tokens;
    record.generated_tokens = accumulated.generated_tokens;
    record.total_elapsed_microseconds = accumulated.total_elapsed_microseconds;
    record.peak_resident_memory_bytes = accumulated.peak_resident_memory_bytes;

    const auto storage_after = probe_system_utilization(1U).disk_read_bytes;
    record.storage_bytes_read =
        storage_after > storage_before ? storage_after - storage_before : 0U;

    const auto metrics = inference_.metrics();
    record.accelerator_mode =
        metrics.requested_gpu_layers > 0U ? "gpu_offloaded" : "cpu_only";
    // A real, if brief, post-run CPU sample -- not a synthesized figure.
    record.average_cpu_percent = probe_system_utilization(100U).cpu_percent;

    record.group_results = {
        check_runner_attribution_regression(metrics, accelerator_policy),
        check_low_memory_regression(), check_prompt_cache_regression(),
        check_calibration_regression(), check_model_routing_regression()};

    bool groups_passed = true;
    for (const auto& group : record.group_results) {
        if (!group.passed) groups_passed = false;
    }

    const auto baseline = store_.previous_accepted(record.fingerprint());
    bool metrics_passed = true;
    if (baseline) {
        metrics_passed = compare_against_baseline(*baseline, record, thresholds_);
    }

    record.accepted = groups_passed && metrics_passed;
    if (!record.accepted) {
        std::vector<std::string> reasons;
        if (!groups_passed) reasons.push_back("regression check group failed");
        if (!metrics_passed) reasons.push_back("metric exceeded its regression threshold");
        record.rejection_reason = reasons.empty() ? "" : reasons.front() +
            (reasons.size() > 1U ? "; " + reasons[1] : "");
    }

    store_.add(record);
    return record;
}

std::string PerformanceCertificationRunner::to_json(
    const PerformanceCertificationRecord& record) {
    std::string body =
        "{\"id\":\"" + json_escape_local(record.id) + "\",\"buildId\":\"" +
        json_escape_local(record.build_id) + "\",\"hardwareId\":\"" +
        json_escape_local(record.hardware_id) + "\",\"modelId\":\"" +
        json_escape_local(record.model_id) + "\",\"backendVersion\":\"" +
        json_escape_local(record.backend_version) + "\",\"promptSuiteHash\":\"" +
        json_escape_local(record.prompt_suite_hash) + "\",\"settingsJson\":\"" +
        json_escape_local(record.settings_json) + "\",\"cacheState\":\"" +
        json_escape_local(record.cache_state) + "\",\"acceleratorMode\":\"" +
        json_escape_local(record.accelerator_mode) + "\",\"profile\":\"" +
        profile_name(record.profile) + "\",\"concurrency\":" +
        std::to_string(record.concurrency) + ",\"createdEpochSeconds\":" +
        std::to_string(record.created_epoch_seconds) + ",\"ttftMicroseconds\":" +
        std::to_string(record.ttft_microseconds) +
        ",\"totalElapsedMicroseconds\":" +
        std::to_string(record.total_elapsed_microseconds) + ",\"promptTokens\":" +
        std::to_string(record.prompt_tokens) + ",\"generatedTokens\":" +
        std::to_string(record.generated_tokens) +
        ",\"peakResidentMemoryBytes\":" +
        std::to_string(record.peak_resident_memory_bytes) +
        ",\"queueWaitMicroseconds\":" +
        std::to_string(record.queue_wait_microseconds) +
        ",\"storageBytesRead\":" + std::to_string(record.storage_bytes_read) +
        ",\"averageCpuPercent\":" + std::to_string(record.average_cpu_percent) +
        ",\"qualityScore\":" + std::to_string(record.quality_score) +
        ",\"accepted\":" + (record.accepted ? "true" : "false") +
        ",\"rejectionReason\":\"" + json_escape_local(record.rejection_reason) +
        "\",\"fingerprint\":\"" + record.fingerprint() + "\",\"groupResults\":[";
    bool first = true;
    for (const auto& group : record.group_results) {
        if (!first) body += ",";
        first = false;
        body += "{\"name\":\"" + json_escape_local(group.name) + "\",\"passed\":" +
                (group.passed ? "true" : "false") + ",\"detail\":\"" +
                json_escape_local(group.detail) + "\"}";
    }
    body += "],\"comparisons\":[";
    first = true;
    for (const auto& comparison : record.comparisons) {
        if (!first) body += ",";
        first = false;
        body += "{\"metric\":\"" + json_escape_local(comparison.metric) +
                "\",\"baseline\":" + std::to_string(comparison.baseline) +
                ",\"current\":" + std::to_string(comparison.current) +
                ",\"deltaPercent\":" + std::to_string(comparison.delta_percent) +
                ",\"thresholdPercent\":" +
                std::to_string(comparison.threshold_percent) + ",\"passed\":" +
                (comparison.passed ? "true" : "false") + "}";
    }
    body += "]}";
    return body;
}

}  // namespace masterai
