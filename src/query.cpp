// MasterAI end-to-end query measurement and reproducible baseline service.
//
// This unit owns monotonic Phase 13 trace state only. Pipeline callers publish
// real transitions and resource observations; this service never guesses
// progress, performs retrieval, or changes inference scheduling policy.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <deque>
#include <limits>
#include <stdexcept>
#include <utility>

namespace masterai {
namespace {

using Clock = std::chrono::steady_clock;

std::uint64_t microseconds_since_epoch(const Clock::time_point value) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            value.time_since_epoch())
            .count());
}

const char* stage_name(const QueryStage stage) noexcept {
    switch (stage) {
        case QueryStage::admission: return "admission";
        case QueryStage::authentication: return "authentication";
        case QueryStage::normalization: return "normalization";
        case QueryStage::classification: return "classification";
        case QueryStage::retrieval_planning: return "retrieval_planning";
        case QueryStage::retrieval: return "retrieval";
        case QueryStage::ranking: return "ranking";
        case QueryStage::prompt_assembly: return "prompt_assembly";
        case QueryStage::runner_queue: return "runner_queue";
        case QueryStage::prompt_evaluation: return "prompt_evaluation";
        case QueryStage::generation: return "generation";
        case QueryStage::persistence: return "persistence";
        case QueryStage::release: return "release";
    }
    return "unknown";
}

const char* status_name(const QueryStatus status) noexcept {
    switch (status) {
        case QueryStatus::accepted: return "accepted";
        case QueryStatus::retrieving: return "retrieving";
        case QueryStatus::queued: return "queued";
        case QueryStatus::evaluating_prompt: return "evaluating_prompt";
        case QueryStatus::generating: return "generating";
        case QueryStatus::completed: return "completed";
        case QueryStatus::cancelled: return "cancelled";
        case QueryStatus::failed: return "failed";
    }
    return "failed";
}

void close_active_stage(QueryTrace& trace, const std::uint64_t now) {
    if (trace.stages.empty() || trace.stages.back().elapsed_microseconds != 0U) {
        return;
    }
    trace.stages.back().elapsed_microseconds =
        std::max<std::uint64_t>(1U, now - trace.stages.back().started_microseconds);
    if (trace.stages.back().stage == QueryStage::runner_queue) {
        trace.queue_wait_microseconds = trace.stages.back().elapsed_microseconds;
    }
}

std::string join_json_strings(const std::set<std::string>& values) {
    std::string result{"["};
    bool first = true;
    for (const auto& value : values) {
        if (!first) result += ',';
        first = false;
        result += json_string(value);
    }
    return result + "]";
}

std::string join_json_strings(const std::vector<std::string>& values) {
    return join_json_strings(std::set<std::string>(values.begin(), values.end()));
}

}  // namespace

class QueryCoordinator::State final {
public:
    explicit State(const std::size_t maximum) : maximum_traces(maximum) {
        if (maximum == 0U || maximum > 65536U) {
            throw std::invalid_argument(
                "maximum retained query traces must be between 1 and 65536");
        }
    }

    QueryTrace& required(const std::string& id) {
        const auto found = traces.find(id);
        if (found == traces.end()) {
            throw std::runtime_error("query trace was not found");
        }
        return found->second;
    }

    std::size_t maximum_traces;
    mutable std::mutex mutex;
    std::map<std::string, QueryTrace> traces;
    std::deque<std::string> order;
};

QueryCoordinator::QueryCoordinator(const std::size_t maximum_retained_traces)
    : state_(std::make_unique<State>(maximum_retained_traces)) {}

QueryCoordinator::~QueryCoordinator() = default;

// Allocates an opaque trace identity, captures the admission resource floor,
// and evicts only the oldest completed retained trace when the bound is full.
std::string QueryCoordinator::begin(const std::string& user_id,
                                    const std::string& project_id,
                                    const std::string& model_id) {
    const auto random = secure_random(16U);
    static constexpr char hex[] = "0123456789abcdef";
    std::string id(random.size() * 2U, '0');
    for (std::size_t index = 0U; index < random.size(); ++index) {
        id[index * 2U] = hex[random[index] >> 4U];
        id[index * 2U + 1U] = hex[random[index] & 0x0fU];
    }
    QueryTrace trace;
    trace.id = id;
    trace.user_id = user_id;
    trace.project_id = project_id;
    trace.model_id = model_id;
    trace.control_plane_start = probe_process_resources();
    trace.control_plane_peak = trace.control_plane_start;
    trace.stages.push_back(
        {QueryStage::admission, microseconds_since_epoch(Clock::now()), 0U, false});

    std::lock_guard<std::mutex> lock(state_->mutex);
    while (state_->order.size() >= state_->maximum_traces) {
        state_->traces.erase(state_->order.front());
        state_->order.pop_front();
    }
    state_->order.push_back(id);
    state_->traces.emplace(id, std::move(trace));
    return id;
}

// Closes the previous stage at the same monotonic instant and begins the next
// stage, preventing gaps caused by separate timing calls.
void QueryCoordinator::transition(const std::string& id,
                                  const QueryStage stage,
                                  const QueryStatus status) {
    const auto now = microseconds_since_epoch(Clock::now());
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& trace = state_->required(id);
    close_active_stage(trace, now);
    trace.status = status;
    trace.stages.push_back({stage, now, 0U, false});
}

// Samples native process memory/page faults and records high-water evidence;
// runner memory stays separately attributed to preserve process isolation.
void QueryCoordinator::observe_resources(
    const std::string& id,
    const std::uint64_t runner_resident_memory_bytes) {
    const auto sample = probe_process_resources();
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& trace = state_->required(id);
    trace.control_plane_peak.resident_memory_bytes =
        std::max(trace.control_plane_peak.resident_memory_bytes,
                 sample.resident_memory_bytes);
    trace.control_plane_peak.private_memory_bytes =
        std::max(trace.control_plane_peak.private_memory_bytes,
                 sample.private_memory_bytes);
    trace.control_plane_peak.commit_bytes =
        std::max(trace.control_plane_peak.commit_bytes, sample.commit_bytes);
    trace.control_plane_peak.page_faults =
        std::max(trace.control_plane_peak.page_faults, sample.page_faults);
    trace.runner_peak_resident_memory_bytes =
        std::max(trace.runner_peak_resident_memory_bytes,
                 runner_resident_memory_bytes);
}

// Records backend-returned token and duration evidence after generation and
// derives rates only when the denominator is nonzero.
void QueryCoordinator::record_inference(
    const std::string& id, const GenerationResult& result,
    const std::uint64_t time_to_first_token_microseconds) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& trace = state_->required(id);
    trace.time_to_first_token_microseconds =
        time_to_first_token_microseconds;
    trace.prompt_tokens = result.prompt_tokens;
    trace.generated_tokens = result.generated_tokens;
    const double seconds =
        static_cast<double>(std::max<std::uint64_t>(
            1U, result.elapsed_microseconds)) /
        1000000.0;
    trace.generation_tokens_per_second =
        static_cast<double>(result.generated_tokens) / seconds;
    trace.prompt_tokens_per_second =
        static_cast<double>(result.prompt_tokens) / seconds;
}

// Closes the final active stage and publishes a terminal state and sanitized
// bounded diagnostic. Cancellation is also attached to the active stage.
void QueryCoordinator::finish(const std::string& id, const QueryStatus status,
                              std::string diagnostic) {
    if (status != QueryStatus::completed &&
        status != QueryStatus::cancelled && status != QueryStatus::failed) {
        throw std::invalid_argument("query finish requires a terminal status");
    }
    const auto now = microseconds_since_epoch(Clock::now());
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& trace = state_->required(id);
    close_active_stage(trace, now);
    if (status == QueryStatus::cancelled && !trace.stages.empty()) {
        trace.stages.back().cancellation_observed = true;
    }
    trace.status = status;
    if (diagnostic.size() > 512U) diagnostic.resize(512U);
    trace.diagnostic = std::move(diagnostic);
}

std::optional<QueryTrace> QueryCoordinator::find(
    const std::string& id) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->traces.find(id);
    if (found == state_->traces.end()) return std::nullopt;
    return found->second;
}

std::vector<QueryTrace> QueryCoordinator::list() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<QueryTrace> result;
    result.reserve(state_->order.size());
    for (const auto& id : state_->order) {
        const auto found = state_->traces.find(id);
        if (found != state_->traces.end()) result.push_back(found->second);
    }
    return result;
}

std::string QueryCoordinator::to_json(const QueryTrace& trace) {
    std::string stages{"["};
    for (std::size_t index = 0U; index < trace.stages.size(); ++index) {
        const auto& stage = trace.stages[index];
        if (index != 0U) stages += ',';
        stages += "{\"name\":" + json_string(stage_name(stage.stage)) +
                  ",\"startedMicroseconds\":" +
                  std::to_string(stage.started_microseconds) +
                  ",\"elapsedMicroseconds\":" +
                  std::to_string(stage.elapsed_microseconds) +
                  ",\"cancellationObserved\":" +
                  (stage.cancellation_observed ? "true" : "false") + "}";
    }
    stages += "]";
    return "{\"id\":" + json_string(trace.id) +
           ",\"userId\":" + json_string(trace.user_id) +
           ",\"projectId\":" + json_string(trace.project_id) +
           ",\"modelId\":" + json_string(trace.model_id) +
           ",\"status\":" + json_string(status_name(trace.status)) +
           ",\"stages\":" + stages +
           ",\"queueWaitMicroseconds\":" +
           std::to_string(trace.queue_wait_microseconds) +
           ",\"timeToFirstTokenMicroseconds\":" +
           std::to_string(trace.time_to_first_token_microseconds) +
           ",\"promptTokens\":" + std::to_string(trace.prompt_tokens) +
           ",\"generatedTokens\":" +
           std::to_string(trace.generated_tokens) +
           ",\"promptTokensPerSecond\":" +
           std::to_string(trace.prompt_tokens_per_second) +
           ",\"generationTokensPerSecond\":" +
           std::to_string(trace.generation_tokens_per_second) +
           ",\"controlPlanePeakResidentBytes\":" +
           std::to_string(trace.control_plane_peak.resident_memory_bytes) +
           ",\"controlPlanePeakPrivateBytes\":" +
           std::to_string(trace.control_plane_peak.private_memory_bytes) +
           ",\"controlPlaneCommitBytes\":" +
           std::to_string(trace.control_plane_peak.commit_bytes) +
           ",\"pageFaults\":" +
           std::to_string(trace.control_plane_peak.page_faults) +
           ",\"runnerPeakResidentBytes\":" +
           std::to_string(trace.runner_peak_resident_memory_bytes) +
           ",\"partialRetrieval\":" +
           (trace.partial_retrieval ? "true" : "false") +
           ",\"diagnostic\":" + json_string(trace.diagnostic) + "}";
}

// Hashes every reproducibility identity and measures the trace lookup hot path
// separately so instrumentation overhead is explicit rather than assumed.
QueryBaseline make_query_baseline(
    const HardwareInfo& hardware, std::string model_hash,
    std::string backend_hash, std::string build_hash,
    std::string settings_hash, std::string prompt_suite_hash, const bool cold,
    const QueryTrace& trace, const std::uint64_t instrumentation_iterations) {
    if (instrumentation_iterations == 0U ||
        instrumentation_iterations > 10000000U) {
        throw std::invalid_argument(
            "instrumentation iterations must be between 1 and 10000000");
    }
    const auto host_identity =
        hardware_info_json(hardware);
    const auto begin = Clock::now();
    volatile std::uint64_t sink = 0U;
    for (std::uint64_t index = 0U; index < instrumentation_iterations;
         ++index) {
        sink += trace.stages.size() + trace.id.size();
    }
    const auto elapsed = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            Clock::now() - begin)
            .count());
    static_cast<void>(sink);
    QueryBaseline baseline;
    baseline.host_hash = sha256_hex(host_identity);
    baseline.model_hash = std::move(model_hash);
    baseline.backend_hash = std::move(backend_hash);
    baseline.build_hash = std::move(build_hash);
    baseline.settings_hash = std::move(settings_hash);
    baseline.prompt_suite_hash = std::move(prompt_suite_hash);
    baseline.cold = cold;
    baseline.trace = trace;
    baseline.instrumentation_overhead_nanoseconds =
        std::max<std::uint64_t>(1U,
            elapsed / instrumentation_iterations);
    return baseline;
}

std::string query_baseline_json(const QueryBaseline& baseline) {
    return "{\"hostHash\":" + json_string(baseline.host_hash) +
           ",\"modelHash\":" + json_string(baseline.model_hash) +
           ",\"backendHash\":" + json_string(baseline.backend_hash) +
           ",\"buildHash\":" + json_string(baseline.build_hash) +
           ",\"settingsHash\":" + json_string(baseline.settings_hash) +
           ",\"promptSuiteHash\":" +
           json_string(baseline.prompt_suite_hash) +
           ",\"cold\":" + (baseline.cold ? "true" : "false") +
           ",\"instrumentationOverheadNanoseconds\":" +
           std::to_string(
               baseline.instrumentation_overhead_nanoseconds) +
           ",\"trace\":" + QueryCoordinator::to_json(baseline.trace) + "}";
}

std::string hardware_info_json(const HardwareInfo& hardware) {
    return "{\"platform\":" + json_string(hardware.platform) +
           ",\"architecture\":" + json_string(hardware.architecture) +
           ",\"physicalCpus\":" +
           std::to_string(hardware.physical_cpu_count) +
           ",\"logicalCpus\":" +
           std::to_string(hardware.logical_cpu_count) +
           ",\"numaNodes\":" + std::to_string(hardware.numa_node_count) +
           ",\"totalRamMiB\":" + std::to_string(hardware.total_ram_mib) +
           ",\"availableRamMiB\":" +
           std::to_string(hardware.available_ram_mib) +
           ",\"totalVirtualMemoryMiB\":" +
           std::to_string(hardware.total_virtual_memory_mib) +
           ",\"availableVirtualMemoryMiB\":" +
           std::to_string(hardware.available_virtual_memory_mib) +
           ",\"storageClass\":" + json_string(hardware.storage_class) +
           ",\"storageCapacityMiB\":" +
           std::to_string(hardware.storage_capacity_mib) +
           ",\"storageFreeMiB\":" +
           std::to_string(hardware.free_disk_mib) +
           ",\"gpuMemoryMiB\":" +
           std::to_string(hardware.gpu_memory_mib) +
           ",\"cpuFeatures\":" + join_json_strings(hardware.cpu_features) +
           ",\"gpuBackends\":" + join_json_strings(hardware.gpu_backends) +
           ",\"backendCapabilities\":" +
           join_json_strings(hardware.backend_capabilities) + "}";
}

}  // namespace masterai
