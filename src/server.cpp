// MasterAI loopback HTTP control plane and browser/API presentation.
//
// This unit accepts bounded HTTP/1.1 requests, applies host/origin,
// authentication, scope, CSRF, project, and rate-limit policy, then routes to
// native services. Streamable MCP HTTP reuses the same security boundary and
// protocol dispatcher as stdio so IDE and browser behavior remain consistent.
#include "masterai.hpp"
#include "json.hpp"
#include "server_internal.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

using server_internal::json_escape;
using server_internal::json_escape_bytes;
using server_internal::Request;
using server_internal::response;
using server_internal::application_page;
using server_internal::application_script;
using server_internal::login_page;

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

void close_socket(const NativeSocket socket) noexcept {
    if (socket == invalid_socket) {
        return;
    }
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

// Phase 30: raw pointer+length send loop -- the actual socket-write
// primitive. The std::string overload below just forwards into this one,
// so callers that already hold a BufferView/SharedBuffer (no owned
// std::string) can write straight from that view's own backing storage
// with no copy forced on them by this function's signature.
bool send_all(const NativeSocket socket, const char* data, std::size_t size) {
    std::size_t sent = 0U;
    while (sent < size) {
        const auto remaining = size - sent;
        const auto chunk =
            send(socket, data + sent, static_cast<int>(remaining), 0);
        if (chunk <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

bool send_all(const NativeSocket socket, const std::string& response) {
    return send_all(socket, response.data(), response.size());
}

bool send_chunk(const NativeSocket socket, const std::string& value) {
    std::ostringstream size;
    size << std::hex << value.size();
    return send_all(socket, size.str() + "\r\n" + value + "\r\n");
}

// Phase 30: zero-copy chunked-encoding write for the streaming chat-token
// hot path (send_chat_message() below). HTTP/1.1 chunked encoding requires
// the exact payload byte count up front, so the three parts' sizes are
// summed first -- but unlike send_chunk() above, which concatenates
// size+CRLF+value+CRLF into one new std::string before a single
// send_all() call (a full copy of the whole payload, paid on every
// streamed token), this writes `prefix`, `payload` (a BufferView -- e.g.
// straight from json_escape_bytes()'s output moved into a SharedBuffer, see
// the call site) and `suffix` directly from their own backing storage via
// separate send_all() calls. The same bytes reach the socket in the same
// order either way; only the framing copy is gone.
bool send_chunk_parts(const NativeSocket socket, const std::string& prefix,
                      const BufferView& payload, const std::string& suffix) {
    const std::size_t total = prefix.size() + payload.size() + suffix.size();
    std::ostringstream size;
    size << std::hex << total;
    if (!send_all(socket, size.str() + "\r\n")) return false;
    if (!prefix.empty() && !send_all(socket, prefix.data(), prefix.size())) {
        return false;
    }
    if (payload.size() > 0U &&
        !send_all(socket, reinterpret_cast<const char*>(payload.data()),
                  payload.size())) {
        return false;
    }
    if (!suffix.empty() && !send_all(socket, suffix.data(), suffix.size())) {
        return false;
    }
    return send_all(socket, "\r\n");
}

// Distinguishes an invalid one-time setup token from other malformed setup
// requests so setup() and setup_local() can each return the right status.
struct InvalidSetupToken : std::runtime_error {
    InvalidSetupToken() : std::runtime_error("setup_token_invalid") {}
};

struct SetupFields {
    std::string username;
    std::string password;
    std::string display_name;
};

std::string asset_response(const char* content_type, const std::string& body) {
    return "HTTP/1.1 200 OK\r\nContent-Type: " + std::string(content_type) +
           "\r\nContent-Length: " + std::to_string(body.size()) +
           "\r\nConnection: close\r\nCache-Control: no-store\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "Content-Security-Policy: default-src 'none'\r\n\r\n" + body;
}

std::string lower(std::string value) {
    for (char& character : value) {
        if (character >= 'A' && character <= 'Z') {
            character = static_cast<char>(character - 'A' + 'a');
        }
    }
    return value;
}

Request parse_request(const std::string& raw, const std::uint64_t max_body) {
    const auto line_end = raw.find("\r\n");
    if (line_end == std::string::npos || line_end > 4096U) {
        throw std::runtime_error("invalid request line");
    }
    Request result;
    std::istringstream line(raw.substr(0U, line_end));
    std::string version;
    line >> result.method >> result.target >> version;
    if (!line || version != "HTTP/1.1" || result.target.empty() ||
        result.target.size() > 2048U) {
        throw std::runtime_error("invalid request line");
    }
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) throw std::runtime_error("headers incomplete");
    std::size_t position = line_end + 2U;
    while (position < header_end) {
        const auto end = raw.find("\r\n", position);
        const auto colon = raw.find(':', position);
        if (end == std::string::npos || colon == std::string::npos || colon >= end) {
            throw std::runtime_error("invalid request header");
        }
        const auto name = lower(raw.substr(position, colon - position));
        std::size_t value_start = colon + 1U;
        while (value_start < end && raw[value_start] == ' ') ++value_start;
        if (!result.headers.emplace(name, raw.substr(value_start, end - value_start))
                 .second) {
            throw std::runtime_error("duplicate request header");
        }
        position = end + 2U;
    }
    std::uint64_t content_length = 0U;
    const auto length = result.headers.find("content-length");
    if (length != result.headers.end()) {
        content_length = std::stoull(length->second);
    }
    if (content_length > max_body ||
        content_length != raw.size() - header_end - 4U) {
        throw std::runtime_error("request body length is invalid");
    }
    result.body = raw.substr(header_end + 4U);
    return result;
}

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string cookie_token(const Request& request) {
    const auto found = request.headers.find("cookie");
    if (found == request.headers.end()) return {};
    const std::string marker = "masterai_session=";
    const auto start = found->second.find(marker);
    if (start == std::string::npos) return {};
    const auto value_start = start + marker.size();
    const auto end = found->second.find(';', value_start);
    return found->second.substr(value_start, end - value_start);
}

// Phase 16: renders one RetrievalOutcome's disclosure list as the raw JSON
// array embedded into a QueryTrace so /api/v1/queries/{id} lets callers
// inspect exactly which project sources were supplied to the model.
// Phase 17: `cache_hit` is a truthful indicator -- true only when this
// request's evidence came from CacheManager::get() rather than a fresh
// RetrievalPlanner::retrieve() call -- surfaced to the caller through the
// same disclosure object ordinary users already see via
// /api/v1/queries/{id}.
std::string retrieval_disclosure_json(const RetrievalOutcome& outcome,
                                      bool cache_hit) {
    std::string body = "{\"strategy\":" + json_string(outcome.strategy) +
                       ",\"partial\":" +
                       (outcome.partial ? "true" : "false") +
                       ",\"cacheHit\":" + (cache_hit ? "true" : "false") +
                       ",\"diagnostic\":" + json_string(outcome.diagnostic) +
                       ",\"entries\":[";
    bool first = true;
    for (const auto& entry : outcome.disclosure) {
        if (!first) body += ",";
        first = false;
        body += "{\"source\":" + json_string(entry.source) +
                ",\"relativePath\":" + json_string(entry.relative_path) +
                ",\"offset\":" + std::to_string(entry.offset) +
                ",\"indexGeneration\":" +
                std::to_string(entry.index_generation) +
                ",\"score\":" + std::to_string(entry.score) +
                ",\"included\":" + (entry.included ? "true" : "false") +
                ",\"reason\":" + json_string(entry.reason) + "}";
    }
    return body + "]}";
}

// Phase 17: parses the optional {"category":"..."} body accepted by
// POST /api/v1/system/cache/clear. Throws on an unrecognized category name;
// returns no value (clear every category) for an empty body.
std::optional<CacheCategory> parse_cache_clear_category(
    const std::string& body) {
    if (body.empty()) return std::nullopt;
    const auto root = parse_json(body);
    const auto* field = root.optional("category");
    if (field == nullptr) return std::nullopt;
    CacheCategory parsed{};
    if (!cache_category_from_string(field->as_string(), parsed)) {
        throw std::runtime_error("unknown cache category");
    }
    return parsed;
}

// Phase 69: docs/PLAN.md "Machine Learning Abilities" section 37
// (Automated Machine Learning Pipelines). Splits a pipeline's comma-joined
// stage list, trimming whitespace around each entry and dropping empty
// entries so "train, ,evaluate" behaves the same as "train,evaluate".
std::vector<std::string> split_pipeline_stages(const std::string& stages) {
    std::vector<std::string> result;
    std::size_t start = 0U;
    while (start <= stages.size()) {
        const auto comma = stages.find(',', start);
        const auto end = comma == std::string::npos ? stages.size() : comma;
        const auto piece = stages.substr(start, end - start);
        const auto first = piece.find_first_not_of(" \t\r\n");
        const auto last = piece.find_last_not_of(" \t\r\n");
        if (first != std::string::npos) {
            result.push_back(piece.substr(first, last - first + 1U));
        }
        if (comma == std::string::npos) break;
        start = comma + 1U;
    }
    return result;
}

std::string ascii_lower(const std::string& value) {
    std::string result = value;
    for (char& character : result) {
        character = static_cast<char>(
            std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

}  // namespace

class HttpServer::State final {
    // Phase 33 (INTRANET-WORKER slice): HttpServer::run()/stop() start and
    // join worker_listener_thread directly against the private fields
    // below, the same way every other background-thread field in this
    // class is already only ever touched from HttpServer's own methods.
    friend class HttpServer;

public:
    // Opens durable services in dependency order, then wires the MCP dispatcher
    // to the same project catalogue and model root used by the normal API.
    explicit State(const AppConfig& value,
                   std::filesystem::path settings_file_path = {})
        : configuration(value), records(value.runtime_root / "database"),
          audit(value.runtime_root / "audit" / "audit.log"),
          settings_file(std::move(settings_file_path)) {
        // request_times is a self-pruning sliding window capped at the
        // configured rate limit; reserving its steady-state size upfront
        // avoids reallocation churn as it fills on the first hot minute.
        request_times.reserve(value.rate_limit_per_minute);
        records.open();
        advanced_optimizations =
            std::make_unique<AdvancedOptimizationRegistry>(records);
        speculative_pair_evidence =
            std::make_unique<SpeculativeDecodingPairEvidenceStore>(records);
        model_usage = std::make_unique<ModelUsagePredictor>();
        const auto hardware = probe_hardware(value.runtime_root);
        const auto profile =
            value.resource_profile == "minimal"
                ? ResourceProfile::minimal
                : value.resource_profile == "performance"
                      ? ResourceProfile::performance
                      : ResourceProfile::balanced;
        auto memory_policy = MemoryBudgetManager::policy_for(
            profile, hardware,
            value.memory_hard_limit_mib == 0U
                ? 0U
                : value.memory_hard_limit_mib * 1024ULL * 1024ULL);
        memory_policy.minimum_os_reserve_bytes =
            value.memory_reserve_mib * 1024ULL * 1024ULL;
        memory_policy.minimum_free_percent =
            value.minimum_free_ram_percent;
        memory_policy.critical_percent = value.critical_memory_percent;
        memory = std::make_unique<MemoryBudgetManager>(
            memory_policy, hardware);
        auto scheduling_policies = default_scheduling_policies();
        scheduling_policies[SchedulingClass::interactive_chat]
            .concurrency_allowance = memory_policy.maximum_active_inference;
        request_scheduler = std::make_unique<RequestScheduler>(
            std::move(scheduling_policies), 256U);
        // Phase 34: always constructed (cheap, in-memory, no disk/network
        // I/O) even when no administrator has ever selected a mode other
        // than the default `automatic` -- the same "always on, opt-in
        // behavior only" shape PromptSessionManager (Phase 18) already
        // established for a cheap always-present service.
        adaptive_controller = std::make_unique<AdaptiveController>();
        users = std::make_unique<UserStore>(records);
        sessions = std::make_unique<SessionStore>(records);
        api_tokens = std::make_unique<ApiTokenStore>(records);
        secrets = std::make_unique<SecretStore>(
            value.runtime_root / "secrets");
        mcp_outbound_registry =
            std::make_unique<McpOutboundRegistry>(records);
        mcp_outbound_gateway = std::make_unique<McpOutboundGateway>(
            *mcp_outbound_registry, *secrets, audit);
        const auto projects_root = value.runtime_root / "projects";
        std::filesystem::create_directories(projects_root);
        projects = std::make_unique<ProjectCatalog>(projects_root, records);
        mcp = std::make_unique<McpInboundServer>(
            *projects, value.models_root, value.memory_reserve_mib);
        ide = std::make_unique<IdeIntegrationService>(*projects);
        integrations =
            std::make_unique<server_internal::IntegrationHttpController>(
                *users, *api_tokens, *projects, *mcp,
                *mcp_outbound_registry, *mcp_outbound_gateway, *ide, audit);
        chats = std::make_unique<ChatStore>(records);
        // User memory is independent of the selected inference backend: the
        // server captures, persists, recalls, and manages details before a
        // model-specific prompt is assembled.
        user_memories = std::make_unique<UserMemoryStore>(records);
        ml_projects = std::make_unique<MLProjectStore>(records);
        ml_models = std::make_unique<ModelRegistryStore>(records);
        ml_datasets = std::make_unique<DatasetStore>(records);
        ml_subjects = std::make_unique<SubjectPackageStore>(records);
        ml_label_tasks = std::make_unique<LabelTaskStore>(records);
        ml_prep_jobs = std::make_unique<DataPreparationJobStore>(records);
        ml_training_jobs = std::make_unique<TrainingJobStore>(records);
        ml_evaluation_runs = std::make_unique<EvaluationRunStore>(records);
        ml_experiments = std::make_unique<ExperimentStore>(records);
        // Phase 80: real experiment-run results (see ml_engine.cpp).
        ml_experiment_results = std::make_unique<ExperimentResultStore>(records);
        ml_fine_tuning_jobs = std::make_unique<FineTuningJobStore>(records);
        ml_model_builder_configs = std::make_unique<ModelBuilderConfigStore>(records);
        ml_instruction_examples = std::make_unique<InstructionExampleStore>(records);
        // Phase 81: the real instruction-record content store.
        ml_instruction_example_content =
            std::make_unique<InstructionExampleContentStore>(records);
        ml_synthetic_records = std::make_unique<SyntheticRecordStore>(records);
        ml_synthetic_record_content =
            std::make_unique<SyntheticRecordContentStore>(records);
        ml_vector_stores = std::make_unique<VectorStoreStore>(records);
        ml_rag_configs = std::make_unique<RagConfigStore>(records);
        ml_subject_exams = std::make_unique<SubjectExamStore>(records);
        ml_hyperparameter_searches =
            std::make_unique<HyperparameterSearchStore>(records);
        ml_model_optimizations = std::make_unique<ModelOptimizationStore>(records);
        ml_training_checkpoints =
            std::make_unique<TrainingCheckpointStore>(records);
        // Phase 79: real checkpoint weight snapshots (see ml_engine.cpp).
        ml_checkpoint_models = std::make_unique<CheckpointModelStore>(records);
        ml_deployments = std::make_unique<DeploymentStore>(records);
        // Phase 56: real ML execution stores (see ml_engine.cpp).
        ml_dataset_content = std::make_unique<DatasetContentStore>(records);
        ml_trained_models = std::make_unique<TrainedModelStore>(records);
        ml_evaluation_results = std::make_unique<EvaluationResultStore>(records);
        // Phase 73: real LLM LoRA fine-tuning run outcomes (see
        // ml_finetune.cpp).
        ml_llm_finetune_results =
            std::make_unique<FineTuningRunResultStore>(records);
        // Phase 57: real model comparison (see ml_engine.cpp).
        ml_model_comparisons = std::make_unique<ModelComparisonStore>(records);
        ml_comparison_results = std::make_unique<ComparisonResultStore>(records);
        // Phases 58-61: uploaded knowledge sources, their persisted chunks
        // and authored or learned vectors, and the index consumed by RAG.
        ml_knowledge_index = std::make_unique<KnowledgeIndexStore>(
            records, value.parquet_helper_executable,
            value.knowledge_maximum_document_bytes);
        // Phases 62-65: the remaining docs/PLAN.md "Machine Learning
        // Abilities" section-2 interfaces (see each store's class comment
        // in masterai.hpp).
        ml_inference_endpoints = std::make_unique<InferenceEndpointStore>(records);
        ml_compute_nodes = std::make_unique<ComputeNodeStore>(records);
        ml_automation_pipelines = std::make_unique<AutomationPipelineStore>(records);
        ml_safety_governance = std::make_unique<SafetyGovernanceStore>(records);
        attachments = std::make_unique<AttachmentStore>(
            value.runtime_root / "attachments", records);
        benchmarks = std::make_unique<BenchmarkStore>(records);
        // Phase 31: constructed before anything that might want scratch
        // space of its own. classify_storage_tier() reasons from a real
        // measured StorageLatencyProfile of the actual scratch volume (which
        // may sit on a different physical drive than runtime_root), not an
        // assumed tier -- the "model placement recommendations reflect
        // measured storage, not assumption" exit criterion applies to
        // scratch placement the same way. recover_orphans() runs
        // immediately so a directory left behind by a previous crashed
        // process is cleaned up before this run creates any new job.
        {
            const auto scratch_root = resolve_scratch_root(value);
            const auto scratch_storage_class = probe_hardware(scratch_root).storage_class;
            const auto scratch_latency =
                probe_storage_latency(scratch_root, scratch_storage_class);
            scratch_volumes = std::make_unique<ScratchVolumeManager>(
                scratch_root, value.scratch_global_quota_mib * 1024ULL * 1024ULL,
                value.scratch_free_space_reserve_mib * 1024ULL * 1024ULL,
                classify_storage_tier(scratch_latency));
            scratch_volumes->recover_orphans();
        }
        // Phase 31 (Priority B, manifest closure): installed globally so
        // every resolve_durable_path() call site -- in particular
        // LlamaCppAdapter::build_launch_spec's model-file resolution --
        // transparently follows a since-migrated path without this
        // constructor threading a reference through the adapter/runner
        // layers, which have no other reason to know about it.
        durable_file_manifest = std::make_unique<DurableFileManifest>(records);
        install_global_durable_file_manifest(*durable_file_manifest);
        if (!value.llama_server_executable.empty()) {
            inference = std::make_unique<RunnerSupervisor>(
                value.llama_server_executable, value.runtime_root);
            tuning_profiles = std::make_unique<TuningProfileStore>(records);
            calibration = std::make_unique<CalibrationService>(
                *inference, *tuning_profiles, hardware,
                sha256_file_hex(value.llama_server_executable),
                "masterai-0.1.0", value.accelerator_policy);
            // Phase 33 (LOCAL-ONLY slice): additional concurrent local
            // runners, opt-in only -- ConfigurationManager::validate()
            // already rejected a non-empty local_runner_pool without
            // llama_server_executable set, so reaching here with entries
            // present means every precondition already held.
            if (!value.local_runner_pool.empty()) {
                runner_pool = std::make_unique<LocalRunnerPool>(
                    value.local_runner_pool, value.llama_server_executable,
                    value.runtime_root);
            }
        }
        // Phase 33 (INTRANET-WORKER slice): opt-in remote worker routing.
        // ConfigurationManager::validate() already rejected a non-empty
        // intranet_worker_pool without private_ca/client certificate/key
        // set, so reaching here with entries present means every
        // precondition already held.
        if (!value.intranet_worker_pool.empty()) {
            intranet_worker_pool = std::make_unique<IntranetWorkerPool>(
                value.intranet_worker_pool, value.private_ca,
                value.intranet_worker_client_certificate_file,
                value.intranet_worker_client_private_key_file);
        }
        // Phase 33 (INTRANET-WORKER slice): opt-in worker-mode listener --
        // ConfigurationManager::validate() already required
        // llama_server_executable/PKI paths to be set when enabled.
        if (value.worker_mode.enabled) {
            worker_listener = std::make_unique<WorkerListener>(
                value.worker_mode, value.llama_server_executable,
                value.runtime_root);
        }
        if (!value.curl_executable.empty()) {
            downloads = std::make_unique<DownloadManager>(
                value.curl_executable, value.models_root, records);
        }
        indexes = std::make_unique<ProjectIndexService>(
            value.runtime_root / "indexes", *memory);
        // Phase 24: one long-lived planner rather than one per request --
        // its in-flight join table (RetrievalPlanner::inflight_) can only
        // ever join genuinely concurrent duplicate requests if the same
        // instance sees both of them.
        // Phase 30: pass the shared MemoryBudgetManager so the fusion-
        // candidate pool's bytes_reserved() is registered live against
        // MemoryCategory::retrieval_index_cache instead of being invisible.
        retrieval_planner = std::make_unique<RetrievalPlanner>(*indexes, memory.get());
        if (value.watch_project_files) {
            watcher =
                std::make_unique<ProjectWatcher>(*projects, *indexes);
        }
        CachePolicy cache_policy;
        cache_policy.maximum_bytes_per_category =
            value.cache_maximum_bytes_per_category;
        cache = std::make_unique<CacheManager>(
            resolve_page_file_root(value), *memory, cache_policy);
        // Phase 23: wire the tokenization cache into the runner supervisor
        // now that both exist. Safe even when caching is later disabled at
        // the request layer -- CacheManager's own configuration.cache_enabled
        // gate (see retrieve_with_cache() for the analogous Phase 17 gate)
        // is not consulted here because tokenize() currently has no
        // production caller that needs a kill switch; the cache lookup
        // itself is cheap and fails safe (see tokenize()'s catch on a
        // corrupt cached value).
        if (inference != nullptr) {
            inference->set_tokenization_cache(cache.get());
        }
        // Phase 18: always constructed (cheap, in-memory, no disk/network
        // I/O) even when session_reuse_enabled is off -- send_chat_message()
        // is the single gate that decides whether to ever consult it, so
        // flipping the config flag needs no restart-time branching here.
        prompt_sessions = std::make_unique<PromptSessionManager>(
            value.session_reuse_max_slots,
            value.session_reuse_idle_retention_seconds);
        // Phase 30A: same minimal/performance/balanced idle-unload seconds
        // CalibrationService::safe_default_profile() recommends per profile
        // (calibration.cpp) -- one background sweep applies it unattended
        // instead of it only ever taking effect when an administrator
        // happens to run /api/v1/performance/calibrate.
        runner_idle_unload_seconds =
            value.resource_profile == "minimal"
                ? 300U
                : value.resource_profile == "performance" ? 1800U : 600U;
        memory_sweeper = std::make_unique<MemorySweeper>(
            *memory, inference.get(), cache.get(), prompt_sessions.get(),
            runner_idle_unload_seconds,
            [this]() { release_runner_weights_lease(); });
        workloads = std::make_unique<
            server_internal::WorkloadHttpController>(
            configuration, *projects, *attachments, inference.get(),
            downloads.get(), *benchmarks, *indexes, audit, *cache);
        if (users->setup_required()) {
            const auto bytes = secure_random(24U);
            static constexpr char digits[] = "0123456789abcdef";
            setup_token.assign(bytes.size() * 2U, '0');
            for (std::size_t i = 0; i < bytes.size(); ++i) {
                setup_token[i * 2U] = digits[bytes[i] >> 4U];
                setup_token[i * 2U + 1U] = digits[bytes[i] & 0x0fU];
            }
            setup_hash = sha256_hex(setup_token);
            log(LogLevel::warning, "setup.token",
                "One-time first-administrator token: " + setup_token);
        }
    }

    // Phase 77: stops and joins every still-running InferenceEndpoint
    // listener thread before this State (and the sockets/managers those
    // threads call into) is destroyed -- without this, a joinable
    // std::thread member outliving its object calls std::terminate, and a
    // listener left running past shutdown would keep its port bound.
    ~State() {
        std::lock_guard<std::mutex> lock(inference_endpoint_threads_mutex);
        for (auto& entry : inference_endpoint_threads) {
            entry.second.second->store(true);
            entry.second.first.join();
        }
    }

    // Applies connection-level controls before selecting public, browser,
    // Streamable MCP, or authenticated API routes. Unsafe cookie requests also
    // require CSRF, while MCP deliberately accepts bearer credentials only.
    std::string handle(std::string raw,
                       const NativeSocket stream_socket = invalid_socket) {
        const auto now = epoch_seconds();
        // Locked only for this trim/check/push sequence -- a request thread
        // never holds this lock while parsing, routing, or dispatching, so
        // the rate limiter never becomes a serialization bottleneck under
        // concurrent connections.
        {
            std::lock_guard<std::mutex> lock(request_times_mutex);
            while (!request_times.empty() && request_times.front() + 60U <= now) {
                request_times.erase(request_times.begin());
            }
            if (request_times.size() >= configuration.rate_limit_per_minute) {
                return response(429, "Too Many Requests",
                                "{\"error\":\"rate_limit_exceeded\"}",
                                {"Retry-After: 60"});
            }
            request_times.push_back(now);
        }
        Request request;
        try {
            request = parse_request(raw, configuration.max_request_bytes);
            std::fill(raw.begin(), raw.end(), '\0');
            raw.clear();
        } catch (const std::exception&) {
            std::fill(raw.begin(), raw.end(), '\0');
            raw.clear();
            return response(400, "Bad Request", "{\"error\":\"invalid_request\"}");
        }
        const auto host = request.headers.find("host");
        if (host == request.headers.end() ||
            configuration.allowed_hosts.find(
                host->second.substr(0U, host->second.find(':'))) ==
                configuration.allowed_hosts.end()) {
            return response(421, "Misdirected Request", "{\"error\":\"host_rejected\"}");
        }
        const bool unsafe = request.method != "GET" && request.method != "HEAD";
        const auto origin = request.headers.find("origin");
        const std::string same_origin =
            std::string(configuration.tls_mode == "required" ? "https://" :
                                                              "http://") +
            host->second;
        if (origin != request.headers.end() &&
            origin->second != same_origin &&
            configuration.allowed_origins.find(origin->second) ==
                configuration.allowed_origins.end()) {
            return response(403, "Forbidden", "{\"error\":\"origin_rejected\"}");
        }

        if (request.method == "GET" && request.target == "/health/live") {
            return response(200, "OK", "{\"status\":\"live\"}");
        }
        if (request.method == "GET" && request.target == "/") {
            return login_page();
        }
        if (request.method == "GET" && request.target == "/assets/app.js") {
            return asset_response("application/javascript; charset=utf-8",
                                  application_script());
        }
        if (request.method == "GET" && request.target == "/health/ready") {
            const OsIdentityProvider identity;
            // Readiness only depends on the OS identity provider when OS
            // sign-in is actually enabled; the default locally stored
            // password path has no such external dependency.
            const bool auth_path_ready =
                configuration.allow_local_password_accounts ||
                (configuration.allow_os_identity_accounts && identity.available());
            const bool ready = !users->setup_required() && auth_path_ready;
            return response(
                ready ? 200 : 503, ready ? "OK" : "Service Unavailable",
                "{\"status\":\"" + std::string(ready ? "ready" : "not_ready") +
                "\",\"setupRequired\":" +
                std::string(users->setup_required() ? "true" : "false") +
                ",\"osIdentityProviderAvailable\":" +
                std::string(identity.available() ? "true" : "false") + "}");
        }
        if (request.target == "/mcp") {
            return integrations->handle_inbound_mcp(request);
        }
        if (request.method == "POST" && request.target == "/api/v1/setup") {
            return setup(request);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/setup/local") {
            return setup_local(request);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/auth/login") {
            return login(request);
        }

        const auto token = cookie_token(request);
        const auto session = sessions->validate(token, epoch_seconds());
        std::string user_id;
        std::set<std::string> authenticated_scopes;
        std::set<std::string> authenticated_project_ids;
        bool cookie_authenticated = false;
        if (session) {
            user_id = session->user_id;
            cookie_authenticated = true;
        } else {
            const auto authorization = request.headers.find("authorization");
            std::string required_scope;
            if (request.method == "GET" &&
                (request.target == "/api/v1/models" ||
                 request.target == "/models")) {
                required_scope = "models.read";
            } else if (request.method == "GET" &&
                       request.target == "/api/v1/users/me") {
                required_scope = "identity.read";
            } else if (request.target == "/api/v1/users") {
                required_scope = "users.manage";
            } else if (request.target == "/api/v1/admin/config") {
                required_scope = "settings.manage";
            } else if (request.method == "GET" &&
                       request.target.rfind("/api/v1/projects", 0U) == 0U) {
                required_scope = "projects.read";
            } else if (request.method == "GET" &&
                       request.target.rfind("/api/v1/chats", 0U) == 0U) {
                required_scope = "chats.read";
            } else if (request.method == "GET" &&
                       request.target == "/api/v1/memories") {
                required_scope = "chats.read";
            } else if (request.method == "GET" &&
                       request.target.rfind("/api/v1/benchmarks", 0U) == 0U) {
                required_scope = "benchmarks.read";
            } else if (request.method == "GET" &&
                       (request.target == "/api/v1/system/resources" ||
                        request.target == "/api/v1/system/memory" ||
                        request.target.rfind("/api/v1/requests/", 0U) == 0U)) {
                required_scope = "metrics.read";
            } else if (request.method == "POST" &&
                       request.target == "/api/v1/performance/baseline") {
                required_scope = "metrics.write";
            } else if (request.method == "GET" &&
                       request.target ==
                           "/api/v1/performance/advanced-optimizations") {
                required_scope = "metrics.read";
            } else if (request.method == "POST" &&
                       request.target ==
                           "/api/v1/performance/advanced-optimizations") {
                required_scope = "settings.manage";
            } else if (request.method == "POST" &&
                       request.target == "/api/v1/projects") {
                required_scope = "projects.write";
            } else if (request.method == "POST" &&
                       request.target.rfind("/api/v1/projects/", 0U) == 0U &&
                       (request.target.size() >= 14U &&
                        (request.target.compare(
                             request.target.size() - 14U, 14U,
                             "/index/rebuild") == 0 ||
                         request.target.compare(
                             request.target.size() - 13U, 13U,
                             "/index/cancel") == 0 ||
                         request.target.compare(
                             request.target.size() - 13U, 13U,
                             "/index/notify") == 0))) {
                required_scope = "projects.write";
            } else if (request.method == "POST" &&
                       request.target.rfind("/api/v1/chats", 0U) == 0U) {
                required_scope = "chats.write";
            } else if (request.method == "POST" &&
                       request.target.rfind("/api/v1/memories", 0U) == 0U) {
                required_scope = "chats.write";
            } else if (request.method == "POST" &&
                       request.target == "/api/v1/attachments") {
                required_scope = "attachments.write";
            } else if (request.target.rfind("/api/v1/model-downloads", 0U) ==
                       0U) {
                required_scope = "downloads.manage";
            } else if (request.method == "POST" &&
                       request.target.rfind("/api/v1/benchmarks", 0U) == 0U) {
                required_scope =
                    request.target == "/api/v1/benchmarks"
                        ? "benchmarks.run"
                        : "benchmarks.read";
            } else if (request.method == "POST" &&
                       request.target.rfind("/api/v1/models/", 0U) == 0U) {
                required_scope = "models.load";
            } else {
                required_scope = integrations->required_scope(request);
            }
            if (authorization != request.headers.end() &&
                authorization->second.rfind("Bearer ", 0U) == 0U &&
                !required_scope.empty()) {
                const auto api_token = api_tokens->validate(
                    authorization->second.substr(7U), required_scope,
                    epoch_seconds());
                if (api_token) {
                    user_id = api_token->user_id;
                    authenticated_scopes = api_token->scopes;
                    authenticated_project_ids = api_token->project_ids;
                }
            }
        }
        if (user_id.empty()) {
            return response(401, "Unauthorized", "{\"error\":\"authentication_required\"}");
        }
        if (unsafe && cookie_authenticated) {
            const auto csrf = request.headers.find("x-csrf-token");
            if (csrf == request.headers.end() ||
                !constant_time_equal(csrf->second, session->csrf_secret)) {
                return response(403, "Forbidden", "{\"error\":\"csrf_rejected\"}");
            }
        }
        const auto user = users->find_by_id(user_id);
        if (!user || !user->enabled) {
            return response(403, "Forbidden", "{\"error\":\"user_disabled\"}");
        }

        if (request.method == "POST" &&
            request.target == "/api/v1/auth/logout") {
            if (!cookie_authenticated) {
                return response(400, "Bad Request", "{\"error\":\"cookie_session_required\"}");
            }
            sessions->revoke(token);
            audit.append("auth.logout", user->id, "success", "session revoked");
            return response(204, "No Content", "",
                            {"Set-Cookie: masterai_session=; Path=/; HttpOnly; "
                             "SameSite=Strict; Max-Age=0"});
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/auth/refresh") {
            if (!cookie_authenticated) {
                return response(400, "Bad Request", "{\"error\":\"cookie_session_required\"}");
            }
            const auto replacement = sessions->rotate(
                token, epoch_seconds(), configuration.session_minutes * 60U);
            const auto fresh = sessions->validate(replacement, epoch_seconds());
            return response(
                200, "OK",
                "{\"csrfToken\":\"" + fresh->csrf_secret + "\"}",
                {session_cookie(replacement)});
        }
        if (request.method == "GET" && request.target == "/api/v1/users/me") {
            return response(200, "OK",
                "{\"id\":\"" + json_escape(user->id) +
                "\",\"principal\":\"" + json_escape(user->os_principal) +
                "\",\"displayName\":\"" + json_escape(user->display_name) +
                "\",\"role\":\"" + role(user->role) + "\"}");
        }
        if (request.method == "GET" && request.target == "/api/v1/users") {
            return list_users(*user);
        }
        if (request.method == "POST" && request.target == "/api/v1/users") {
            return create_user(request, *user);
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/admin/config") {
            return admin_config_get(*user);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/admin/config") {
            return admin_config_put(request, *user);
        }
        if (request.method == "GET" && request.target == "/api/v1/models") {
            return workloads->model_inventory();
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/models/usage-signals") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK", model_usage_signals_json(
                                           model_usage->snapshot()));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/models/usage-signals") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            try {
                const auto body = parse_json(request.body);
                if (body.as_object().size() != 2U) {
                    throw std::runtime_error("unexpected usage-signal field");
                }
                const auto model_id = body.required("modelId").as_string();
                if (!find_model(model_id).has_value()) {
                    throw std::runtime_error("unknown model id");
                }
                model_usage->set_pinned(
                    model_id, body.required("pinned").as_boolean());
                audit.append("model.usage_pin", user->id, "success", model_id);
                return response(200, "OK", model_usage_signals_json(
                                               model_usage->snapshot()));
            } catch (const std::exception&) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_usage_signal_request\"}");
            }
        }
        // Lets the chat UI show a one-shot "model warmed" confirmation once
        // the runner actually finishes loading, instead of silently going
        // quiet after the "Thinking..." spinner from the first message --
        // and, since warmState is exposed here too, avoid repeating that
        // confirmation on every poll by only reacting to state transitions,
        // not to Ready being merely re-observed. No permission gate beyond
        // being logged in: this is read-only status a chatting user already
        // implicitly knows (which model they picked), not an administrator
        // secret.
        if (request.method == "GET" && request.target == "/api/v1/runner/status") {
            if (inference == nullptr) {
                return response(200, "OK",
                                "{\"modelId\":\"\",\"warmState\":\"Unloaded\"}");
            }
            const auto current = inference->metrics();
            // Phase 30A: requested-vs-actual GPU layers and the unload
            // countdown are administrator-relevant, not a chat secret --
            // this route already has no permission gate beyond being logged
            // in (see the comment above), so both are exposed to every role
            // exactly like modelId/warmState already are.
            std::uint64_t unload_countdown_seconds = 0U;
            if (current.state == RunnerState::ready &&
                current.warm_state == WarmModelState::Ready) {
                const auto elapsed =
                    epoch_seconds() >= current.last_activity_epoch_seconds
                        ? epoch_seconds() - current.last_activity_epoch_seconds
                        : 0U;
                unload_countdown_seconds =
                    elapsed >= runner_idle_unload_seconds
                        ? 0U
                        : runner_idle_unload_seconds - elapsed;
            }
            return response(200, "OK",
                "{\"modelId\":\"" + json_escape(current.model_id) +
                "\",\"warmState\":\"" + to_string(current.warm_state) +
                "\",\"requestedGpuLayers\":" +
                std::to_string(current.requested_gpu_layers) +
                ",\"loadAcceleratorPolicy\":\"" +
                json_escape(current.accelerator_policy) +
                "\",\"unloadCountdownSeconds\":" +
                std::to_string(unload_countdown_seconds) + "}");
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/system/resources") {
            // Phase 30A: appended, not baked into hardware_info_json()
            // itself -- CalibrationService::host_hash() hashes that
            // function's output as a host identity fingerprint, and must
            // stay independent of the administrator's accelerator policy
            // choice. "totalVirtualMemoryMiB"/"availableVirtualMemoryMiB"
            // already report pagefile-inclusive commit capacity (Windows
            // GlobalMemoryStatusEx ullTotalPageFile) -- see
            // src/platform.cpp -- so this only adds the policy itself, not
            // a duplicate probe.
            auto body = hardware_info_json(
                probe_hardware(configuration.runtime_root));
            body.pop_back();
            body += ",\"acceleratorPolicy\":" +
                    ('"' + json_escape(configuration.accelerator_policy) +
                     '"') +
                    "}";
            return response(200, "OK", body);
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/system/memory") {
            // Phase 30A: appended the same way /api/v1/system/resources
            // appends acceleratorPolicy above -- pagefile/swap headroom
            // (already probed for hardware_info_json's virtual-memory
            // fields) and this process's own commit/hard-fault counters
            // round out what an administrator needs to judge whether a
            // cpu_only host is actually staying inside its configured
            // ceiling, without duplicating MemoryBudgetManager's own
            // reserved/category accounting.
            const auto hardware = probe_hardware(configuration.runtime_root);
            const auto process = probe_process_resources();
            auto body = MemoryBudgetManager::to_json(memory->sample());
            body.pop_back();
            body += ",\"totalVirtualMemoryMiB\":" +
                    std::to_string(hardware.total_virtual_memory_mib) +
                    ",\"availableVirtualMemoryMiB\":" +
                    std::to_string(hardware.available_virtual_memory_mib) +
                    ",\"processCommitBytes\":" +
                    std::to_string(process.commit_bytes) +
                    ",\"processPageFaults\":" +
                    std::to_string(process.page_faults) +
                    ",\"runnerIdleUnloadSeconds\":" +
                    std::to_string(runner_idle_unload_seconds) + "}";
            return response(200, "OK", body);
        }
        // "System Report" (Report sidebar): a single consolidated,
        // administrator-only read combining what several narrower endpoints
        // already expose piecemeal (hardware, process resources, RAM
        // pressure, the real Windows pagefile's commit headroom, feature
        // enablement) plus the "PageFile" setting's own usage -- the
        // MasterAI-scoped substitute cache/scratch area CacheManager is
        // rooted at (see resolve_page_file_root()). "Used" there is the sum
        // of every cache category's used_bytes; "capacity" is the sum of
        // their configured ceilings; "drive" figures come from probing the
        // filesystem the page-file root actually lives on, which can differ
        // from the workspace's own runtime/models roots once an
        // administrator points it elsewhere.
        if (request.method == "GET" &&
            request.target == "/api/v1/system/report") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto hardware = probe_hardware(configuration.models_root);
            const auto process = probe_process_resources();
            const auto page_file_root = resolve_page_file_root(configuration);
            const auto page_file_drive = probe_hardware(page_file_root);
            const auto cache_status = cache->status();
            std::uint64_t page_file_used_bytes = 0U;
            std::uint64_t page_file_capacity_bytes = 0U;
            for (const auto& [category, status] : cache_status.categories) {
                static_cast<void>(category);
                page_file_used_bytes += status.used_bytes;
                page_file_capacity_bytes += status.capacity_bytes;
            }
            const std::string body =
                "{\"hardware\":" + hardware_info_json(hardware) +
                ",\"process\":{\"residentMemoryBytes\":" +
                std::to_string(process.resident_memory_bytes) +
                ",\"privateMemoryBytes\":" +
                std::to_string(process.private_memory_bytes) +
                ",\"commitBytes\":" + std::to_string(process.commit_bytes) +
                ",\"pageFaults\":" + std::to_string(process.page_faults) +
                "}"
                ",\"memory\":" + MemoryBudgetManager::to_json(memory->sample()) +
                ",\"systemPageFile\":{\"totalVirtualMemoryMiB\":" +
                std::to_string(hardware.total_virtual_memory_mib) +
                ",\"availableVirtualMemoryMiB\":" +
                std::to_string(hardware.available_virtual_memory_mib) + "}"
                ",\"pageFile\":{\"location\":\"" +
                json_escape(page_file_root.string()) +
                "\",\"usingDefault\":" +
                (configuration.page_file_root.empty() ? "true" : "false") +
                ",\"driveCapacityMiB\":" +
                std::to_string(page_file_drive.storage_capacity_mib) +
                ",\"driveFreeMiB\":" +
                std::to_string(page_file_drive.free_disk_mib) +
                ",\"usedBytes\":" + std::to_string(page_file_used_bytes) +
                ",\"capacityBytes\":" + std::to_string(page_file_capacity_bytes) +
                ",\"categories\":" + CacheManager::to_json(cache_status) + "}"
                ",\"features\":{\"acceleratorPolicy\":\"" +
                json_escape(configuration.accelerator_policy) +
                "\",\"retrievalEnabled\":" +
                (configuration.retrieval_enabled ? "true" : "false") +
                ",\"cacheEnabled\":" +
                (configuration.cache_enabled ? "true" : "false") +
                ",\"sessionReuseEnabled\":" +
                (configuration.session_reuse_enabled ? "true" : "false") +
                ",\"performanceAutoTune\":" +
                (configuration.performance_auto_tune ? "true" : "false") +
                ",\"watchProjectFiles\":" +
                (configuration.watch_project_files ? "true" : "false") +
                ",\"allowLocalPasswordAccounts\":" +
                (configuration.allow_local_password_accounts ? "true"
                                                              : "false") +
                ",\"allowOsIdentityAccounts\":" +
                (configuration.allow_os_identity_accounts ? "true" : "false") +
                ",\"tlsMode\":\"" + json_escape(configuration.tls_mode) +
                "\"}"
                "}";
            return response(200, "OK", body);
        }
        // Phase 17: administrators see bounded cache use and can trim/clear
        // it; ordinary users see only the per-request cacheHit indicator
        // already carried in retrievalDisclosure (see
        // retrieve_with_cache()/retrieval_disclosure_json()).
        if (request.method == "GET" &&
            request.target == "/api/v1/system/cache") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            CacheManager::to_json(cache->status()));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/system/cache/trim") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            cache->trim();
            return response(200, "OK",
                            CacheManager::to_json(cache->status()));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/system/cache/clear") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            try {
                cache->clear(parse_cache_clear_category(request.body));
                return response(200, "OK",
                                CacheManager::to_json(cache->status()));
            } catch (const std::exception&) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_cache_clear_request\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/requests/", 0U) == 0U &&
            request.target.size() > 17U) {
            const auto trace = queries.find(request.target.substr(17U));
            if (!trace) {
                return response(404, "Not Found",
                                "{\"error\":\"request_metrics_not_found\"}");
            }
            if (trace->user_id != user->id &&
                user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK", QueryCoordinator::to_json(*trace));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/baseline") {
            try {
                const auto root = parse_json(request.body);
                if (root.as_object().size() != 7U) {
                    throw std::runtime_error("unexpected baseline field");
                }
                const auto trace =
                    queries.find(root.required("requestId").as_string());
                if (!trace) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"request_metrics_not_found\"}");
                }
                if (trace->user_id != user->id &&
                    user->role != UserRole::administrator) {
                    return response(
                        403, "Forbidden",
                        "{\"error\":\"permission_denied\"}");
                }
                const auto baseline = make_query_baseline(
                    probe_hardware(configuration.runtime_root),
                    root.required("modelHash").as_string(),
                    root.required("backendHash").as_string(),
                    root.required("buildHash").as_string(),
                    root.required("settingsHash").as_string(),
                    root.required("promptSuiteHash").as_string(),
                    root.required("cold").as_boolean(), *trace);
                return response(200, "OK",
                                query_baseline_json(baseline));
            } catch (const std::exception&) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_baseline_request\"}");
            }
        }
        // Phase 19: adaptive hardware/model calibration routes. Distinct
        // from the Phase 13 /api/v1/performance/baseline route above (a
        // reproducibility baseline for one query trace) -- these read and
        // produce TuningProfile evidence instead.
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/performance/profile/", 0U) == 0U) {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            if (calibration == nullptr) {
                return response(503, "Service Unavailable",
                                "{\"error\":\"inference_not_configured\"}");
            }
            static const std::string prefix = "/api/v1/performance/profile/";
            const auto remainder = request.target.substr(prefix.size());
            const auto slash = remainder.find('/');
            if (slash == std::string::npos || slash == 0U) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_profile_request\"}");
            }
            const auto model = find_model(remainder.substr(0U, slash));
            if (!model) {
                return response(404, "Not Found",
                                "{\"error\":\"model_not_found\"}");
            }
            try {
                const auto profile = calibration->resolve(
                    model->manifest.model_sha256, remainder.substr(slash + 1U));
                return response(200, "OK", tuning_profile_json(profile));
            } catch (const std::exception&) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_profile_request\"}");
            }
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/performance/recommendations") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            std::string body = "{\"profiles\":[";
            bool first = true;
            if (tuning_profiles != nullptr) {
                for (const auto& profile : tuning_profiles->all()) {
                    if (!first) body += ",";
                    first = false;
                    body += tuning_profile_json(profile);
                }
            }
            return response(200, "OK", body + "]}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/calibrate") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            if (calibration == nullptr) {
                return response(503, "Service Unavailable",
                                "{\"error\":\"inference_not_configured\"}");
            }
            try {
                const auto root = parse_json(request.body);
                if (root.as_object().size() != 2U) {
                    throw std::runtime_error("unexpected calibration field");
                }
                const auto model =
                    find_model(root.required("modelId").as_string());
                if (!model) {
                    return response(404, "Not Found",
                                    "{\"error\":\"model_not_found\"}");
                }
                std::atomic_bool cancellation{false};
                const auto profile = calibration->calibrate(
                    *model, root.required("profile").as_string(),
                    configuration.runner_port, cancellation);
                audit.append("performance.calibrate", user->id, "success",
                            model->manifest.id);
                return response(201, "Created", tuning_profile_json(profile));
            } catch (const std::exception&) {
                return response(409, "Conflict",
                                "{\"error\":\"calibration_failed\"}");
            }
        }
        // Phase 20: durable evidence/admission registry. GET is read-only;
        // POST records evidence, explicitly admits a validated implemented
        // capability, or disables it without changing the safe Phase 19
        // profile.
        if (request.method == "GET" &&
            request.target == "/api/v1/performance/advanced-optimizations") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(
                200, "OK",
                "{\"features\":" +
                    advanced_optimization_registry_json(
                        advanced_optimizations->features()) +
                    ",\"safeProfileAvailable\":true}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/advanced-optimizations") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            try {
                const auto root = parse_json(request.body);
                const auto feature = root.required("feature").as_string();
                const auto action = root.required("action").as_string();
                if (action == "record-evidence") {
                    if (root.as_object().size() != 3U) {
                        throw std::invalid_argument("unexpected evidence field");
                    }
                    const auto& value = root.required("evidence");
                    if (value.as_object().size() != 13U) {
                        throw std::invalid_argument("unexpected evidence field");
                    }
                    const auto peak =
                        value.required("peakResidentMemoryBytes").as_integer();
                    if (peak <= 0) {
                        throw std::invalid_argument("invalid peak memory");
                    }
                    AdvancedOptimizationEvidence evidence;
                    evidence.feature_name = feature;
                    evidence.baseline_description =
                        value.required("baselineDescription").as_string();
                    evidence.changed_setting =
                        value.required("changedSetting").as_string();
                    evidence.host_hash = value.required("hostHash").as_string();
                    evidence.model_sha256 =
                        value.required("modelSha256").as_string();
                    evidence.backend_hash =
                        value.required("backendHash").as_string();
                    evidence.time_to_first_token_ms =
                        value.required("timeToFirstTokenMs").as_double();
                    evidence.prompt_throughput_tokens_per_second =
                        value.required("promptThroughputTokensPerSecond")
                            .as_double();
                    evidence.generation_throughput_tokens_per_second =
                        value.required("generationThroughputTokensPerSecond")
                            .as_double();
                    evidence.peak_resident_memory_bytes =
                        static_cast<std::uint64_t>(peak);
                    evidence.quality_notes =
                        value.required("qualityNotes").as_string();
                    evidence.power_thermal_notes =
                        value.required("powerThermalNotes").as_string();
                    evidence.regression_detected =
                        value.required("regressionDetected").as_boolean();
                    evidence.fallback_verified =
                        value.required("fallbackVerified").as_boolean();
                    advanced_optimizations->record_evidence(feature, evidence);
                } else {
                    if (root.as_object().size() != 2U) {
                        throw std::invalid_argument("unexpected action field");
                    }
                    if (action == "admit") {
                        advanced_optimizations->admit(feature);
                    } else if (action == "disable") {
                        advanced_optimizations->disable(feature);
                    } else {
                        throw std::invalid_argument("unknown action");
                    }
                }
                audit.append("performance.advanced-optimization", user->id,
                             "success", feature + ":" + action);
                return response(
                    200, "OK", "{\"features\":" +
                        advanced_optimization_registry_json(
                            advanced_optimizations->features()) +
                        ",\"safeProfileAvailable\":true}");
            } catch (const std::exception&) {
                audit.append("performance.advanced-optimization", user->id,
                             "denied", "invalid-or-unadmitted");
                return response(
                    400, "Bad Request",
                    "{\"error\":\"advanced_optimization_rejected\"}");
            }
        }
        // Phase 32: durable, administrator-submitted measured acceptance
        // rate for one (target, draft) model pair -- separate from the
        // Phase 20 advanced-optimizations route above, which only ever
        // records a one-time global before/after admission figure, never a
        // per-pair acceptance rate. GET lists every recorded pair; POST
        // records one. Nothing here enables speculative decoding by itself
        // -- ensure_model_loaded() only ever consults a recorded pair after
        // "speculative_decoding" has separately been admitted.
        if (request.method == "GET" &&
            request.target == "/api/v1/performance/speculative-pairs") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            using PairRecord = SpeculativeDecodingPairEvidenceStore::PairRecord;
            return response(200, "OK",
                            "{\"pairs\":" +
                                speculative_decoding_pairs_json(
                                    speculative_pair_evidence != nullptr
                                        ? speculative_pair_evidence->all()
                                        : std::vector<PairRecord>{}) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/speculative-pairs") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            try {
                if (speculative_pair_evidence == nullptr) {
                    throw std::runtime_error(
                        "speculative-decoding pair evidence store is "
                        "unavailable");
                }
                const auto root = parse_json(request.body);
                if (root.as_object().size() != 3U) {
                    throw std::invalid_argument("unexpected evidence field");
                }
                const auto target_sha256 =
                    root.required("targetModelSha256").as_string();
                const auto draft_sha256 =
                    root.required("draftModelSha256").as_string();
                const auto acceptance_rate =
                    root.required("acceptanceRate").as_double();
                speculative_pair_evidence->record(target_sha256, draft_sha256,
                                                  acceptance_rate);
                audit.append("performance.speculative-pair", user->id,
                             "success", target_sha256 + ":" + draft_sha256);
                return response(200, "OK",
                                "{\"pairs\":" +
                                    speculative_decoding_pairs_json(
                                        speculative_pair_evidence->all()) +
                                    "}");
            } catch (const std::exception&) {
                audit.append("performance.speculative-pair", user->id,
                             "denied", "invalid-evidence");
                return response(400, "Bad Request",
                                "{\"error\":\"speculative_pair_rejected\"}");
            }
        }
        // Phase 31: storage tiering visibility. GET /api/v1/system/storage
        // reports the measured tier/filesystem-integrity evidence and
        // resulting placement recommendation for models_root (the "active
        // model/index storage" the plan's exit criterion is about), plus a
        // separate, non-conflated memory accounting snapshot -- following
        // the same "administrator-only, evidence not assumption" shape as
        // /api/v1/system/resources and the Phase 19/20 performance routes
        // above.
        if (request.method == "GET" &&
            request.target == "/api/v1/system/storage") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto storage_class = probe_hardware(configuration.models_root).storage_class;
            const auto latency =
                probe_storage_latency(configuration.models_root, storage_class);
            const auto filesystem =
                probe_filesystem_integrity_flags(configuration.models_root);
            const auto placement = recommend_storage_placement(latency, filesystem);
            const auto memory_accounting = probe_memory_accounting(
                100U, 0U,
                inference != nullptr ? inference->metrics().resident_memory_bytes : 0U);
            std::string concerns = "[";
            for (std::size_t index = 0U; index < placement.concerns.size(); ++index) {
                if (index != 0U) concerns += ",";
                concerns += json_string(placement.concerns[index]);
            }
            concerns += "]";
            return response(
                200, "OK",
                "{\"modelsRoot\":{\"storageClass\":" + json_string(storage_class) +
                    ",\"measuredReadLatencyMicroseconds\":" +
                    std::to_string(latency.measured_read_latency_us) +
                    ",\"tier\":" + json_string(to_string(placement.recommended_tier)) +
                    ",\"acceptableForActiveModelStorage\":" +
                    (placement.acceptable_for_active_model_storage ? "true" : "false") +
                    ",\"concerns\":" + concerns +
                    ",\"filesystem\":{\"detectionAvailable\":" +
                    (filesystem.detection_available ? "true" : "false") +
                    ",\"volumeFilesystem\":" + json_string(filesystem.volume_filesystem) +
                    ",\"compressed\":" + (filesystem.compressed ? "true" : "false") +
                    ",\"encrypted\":" + (filesystem.encrypted ? "true" : "false") +
                    ",\"deduplicated\":" + (filesystem.deduplicated ? "true" : "false") +
                    ",\"virtualDisk\":" + (filesystem.virtual_disk ? "true" : "false") +
                    ",\"networkRedirected\":" +
                    (filesystem.network_redirected ? "true" : "false") + "}},\n"
                "\"memoryAccounting\":{\"physicalTotalBytes\":" +
                    std::to_string(memory_accounting.physical_total_bytes) +
                    ",\"physicalAvailableBytes\":" +
                    std::to_string(memory_accounting.physical_available_bytes) +
                    ",\"committedBytes\":" +
                    std::to_string(memory_accounting.committed_bytes) +
                    ",\"commitLimitBytes\":" +
                    std::to_string(memory_accounting.commit_limit_bytes) +
                    ",\"pagefileUsedBytes\":" +
                    std::to_string(memory_accounting.pagefile_used_bytes) +
                    ",\"hardPageFaultRatePerSecond\":" +
                    std::to_string(memory_accounting.hard_page_fault_rate_per_second) +
                    ",\"modelMappedBytes\":" +
                    std::to_string(memory_accounting.model_mapped_bytes) +
                    ",\"modelResidentBytes\":" +
                    std::to_string(memory_accounting.model_resident_bytes) + "}}");
        }
        // Phase 33 (LOCAL-ONLY slice): local runner pool visibility --
        // which runners are configured, their live RunnerMetrics-derived
        // state, capabilities, project authorization, and health --
        // following the same "administrator-only" shape as
        // /api/v1/system/storage above. Returns an empty runners array
        // (not an error) when no pool is configured, so this route is
        // always safe for an admin UI to poll regardless of deployment
        // mode.
        if (request.method == "GET" &&
            request.target == "/api/v1/runner/pool") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto entries = runner_pool != nullptr
                                     ? runner_pool->status()
                                     : std::vector<RunnerPoolEntrySnapshot>{};
            return response(200, "OK", LocalRunnerPool::status_json(entries));
        }
        // Phase 33 (INTRANET-WORKER slice): remote worker pool visibility,
        // the same administrator-only shape as GET /api/v1/runner/pool
        // above. Returns an empty workers array (not an error) when no
        // intranet worker pool is configured.
        if (request.method == "GET" && request.target == "/api/v1/worker/pool") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto entries =
                intranet_worker_pool != nullptr
                    ? intranet_worker_pool->status()
                    : std::vector<RunnerPoolEntrySnapshot>{};
            return response(200, "OK", IntranetWorkerPool::status_json(entries));
        }
        // Phase 33 (INTRANET-WORKER slice): re-runs mutual-TLS status/
        // model-digest verification against every configured intranet
        // worker on demand, rather than only ever on an internal timer --
        // an administrator can immediately confirm a newly approved worker
        // is reachable and reporting the expected model.
        if (request.method == "POST" &&
            request.target == "/api/v1/worker/pool/refresh") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            if (intranet_worker_pool == nullptr) {
                return response(200, "OK", "{\"refreshed\":false}");
            }
            std::map<std::string, std::string> known_model_sha256_by_id;
            const auto hardware = probe_hardware(configuration.models_root);
            for (const auto& model :
                ModelRegistry(configuration.models_root, hardware,
                              configuration.memory_reserve_mib)
                    .scan()) {
                known_model_sha256_by_id[model.manifest.id] =
                    model.manifest.model_sha256;
            }
            intranet_worker_pool->refresh_all(known_model_sha256_by_id);
            audit.append("worker.pool.refresh", user->id, "success", "");
            return response(200, "OK", "{\"refreshed\":true}");
        }
        // Phase 33 (INTRANET-WORKER slice): initializes this control
        // plane's own private worker CA. Administrator-only, and refuses to
        // overwrite an already-existing CA (see
        // initialize_private_certificate_authority()'s own guard) --
        // replacing a CA silently would invalidate every already-issued
        // worker certificate without warning.
        if (request.method == "POST" &&
            request.target == "/api/v1/system/pki/initialize") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            if (configuration.private_ca.certificate_file.empty() ||
                configuration.private_ca.private_key_file.empty()) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"privateCa is not configured in settings\"}");
            }
            try {
                initialize_private_certificate_authority(configuration.private_ca);
            } catch (const std::exception& failure) {
                return response(400, "Bad Request",
                                "{\"error\":" + json_string(failure.what()) +
                                    "}");
            }
            audit.append("pki.ca.initialize", user->id, "success", "");
            return response(200, "OK", "{\"initialized\":true}");
        }
        // Phase 33 (INTRANET-WORKER slice): signs one worker certificate
        // against this control plane's private CA. Administrator-only.
        // Returns the certificate AND private key exactly once -- this
        // control plane does not retain the private key itself (see
        // IssuedWorkerCertificate's class comment) -- the response body is
        // therefore the only copy an administrator will ever see and must
        // be copied to the physical worker machine immediately.
        if (request.method == "POST" &&
            request.target == "/api/v1/system/pki/workers") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            if (configuration.private_ca.certificate_file.empty() ||
                configuration.private_ca.private_key_file.empty()) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"privateCa is not configured in settings\"}");
            }
            std::string common_name;
            try {
                const auto payload = parse_json(request.body);
                common_name = payload.required("commonName").as_string();
            } catch (const std::exception&) {
                return response(400, "Bad Request",
                                "{\"error\":\"commonName is required\"}");
            }
            try {
                const auto issued = issue_worker_certificate(
                    configuration.private_ca, common_name);
                audit.append("pki.worker.issue", user->id, "success", common_name);
                return response(
                    200, "OK",
                    "{\"certificatePem\":" +
                        json_string(issued.certificate_pem) +
                        ",\"privateKeyPem\":" +
                        json_string(issued.private_key_pem) +
                        ",\"sha256Fingerprint\":" +
                        json_string(issued.sha256_fingerprint) + "}");
            } catch (const std::exception& failure) {
                return response(400, "Bad Request",
                                "{\"error\":" + json_string(failure.what()) +
                                    "}");
            }
        }
        // Phase 34: runs one AdaptiveController::evaluate() cycle against
        // live MemoryBudgetManager/RequestScheduler/CacheManager signals
        // and returns the resulting report -- see AdaptiveController's
        // class comment in masterai.hpp for exactly which named knobs this
        // can apply live versus only ever recommend. Administrator-only,
        // and safe to poll repeatedly: evaluate() itself is what enforces
        // dwell time/cooldown/max-changes-per-interval, not the caller.
        if (request.method == "GET" &&
            request.target == "/api/v1/performance/adaptive") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            AdaptiveSignalSnapshot signals;
            signals.memory = memory->sample();
            signals.scheduler = request_scheduler->status();
            signals.cache = cache->status();
            const auto evaluate_now = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            const auto report =
                adaptive_controller->evaluate(signals, *memory, evaluate_now);
            return response(200, "OK", AdaptiveController::to_json(report));
        }
        // Phase 34: administrator mode selection -- one of the named
        // PerformanceMode values from masterai.hpp, case-sensitive,
        // matching to_string(PerformanceMode)'s own spelling exactly.
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/adaptive/mode") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto payload = parse_json(request.body);
            const auto requested = payload.required("mode").as_string();
            static const std::map<std::string, PerformanceMode> modes = {
                {"minimal_memory", PerformanceMode::minimal_memory},
                {"balanced", PerformanceMode::balanced},
                {"lowest_latency", PerformanceMode::lowest_latency},
                {"maximum_throughput", PerformanceMode::maximum_throughput},
                {"battery_saver", PerformanceMode::battery_saver},
                {"quiet_thermal_conservative",
                 PerformanceMode::quiet_thermal_conservative},
                {"administrator_custom", PerformanceMode::administrator_custom},
                {"automatic", PerformanceMode::automatic}};
            const auto found = modes.find(requested);
            if (found == modes.end()) {
                return response(400, "Bad Request",
                                "{\"error\":\"unknown performance mode\"}");
            }
            adaptive_controller->set_mode(found->second);
            audit.append("performance.adaptive.mode", user->id, "success", requested);
            return response(200, "OK", "{\"mode\":" + json_string(requested) + "}");
        }
        // Phase 34: administrator-configured ceilings -- see
        // PerformanceCeilings in masterai.hpp for the field list. Every
        // field is optional in the request body; an omitted field keeps
        // its current value rather than resetting to PerformanceCeilings'
        // struct default.
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/adaptive/ceilings") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto payload = parse_json(request.body);
            auto ceilings = adaptive_controller->ceilings();
            if (payload.optional("maxInferenceConcurrency") != nullptr) {
                ceilings.max_inference_concurrency = static_cast<std::uint32_t>(
                    payload.required("maxInferenceConcurrency").as_integer());
            }
            if (payload.optional("maxQueuedInference") != nullptr) {
                ceilings.max_queued_inference = static_cast<std::uint32_t>(
                    payload.required("maxQueuedInference").as_integer());
            }
            if (payload.optional("maxIndexWorkers") != nullptr) {
                ceilings.max_index_workers = static_cast<std::uint32_t>(
                    payload.required("maxIndexWorkers").as_integer());
            }
            if (payload.optional("maxContextTokens") != nullptr) {
                ceilings.max_context_tokens = static_cast<std::uint32_t>(
                    payload.required("maxContextTokens").as_integer());
            }
            if (payload.optional("minIdleUnloadSeconds") != nullptr) {
                ceilings.min_idle_unload_seconds = static_cast<std::uint32_t>(
                    payload.required("minIdleUnloadSeconds").as_integer());
            }
            if (payload.optional("maxIdleUnloadSeconds") != nullptr) {
                ceilings.max_idle_unload_seconds = static_cast<std::uint32_t>(
                    payload.required("maxIdleUnloadSeconds").as_integer());
            }
            if (payload.optional("maxStepPercent") != nullptr) {
                ceilings.max_step_percent = static_cast<unsigned int>(
                    payload.required("maxStepPercent").as_integer());
            }
            if (payload.optional("maxChangesPerInterval") != nullptr) {
                ceilings.max_changes_per_interval = static_cast<std::uint32_t>(
                    payload.required("maxChangesPerInterval").as_integer());
            }
            if (payload.optional("intervalSeconds") != nullptr) {
                ceilings.interval_seconds = static_cast<std::uint32_t>(
                    payload.required("intervalSeconds").as_integer());
            }
            if (payload.optional("minimumDwellSeconds") != nullptr) {
                ceilings.minimum_dwell_seconds = static_cast<std::uint32_t>(
                    payload.required("minimumDwellSeconds").as_integer());
            }
            adaptive_controller->set_ceilings(ceilings);
            audit.append("performance.adaptive.ceilings", user->id, "success", "");
            return response(200, "OK", "{\"updated\":true}");
        }
        // Phase 34: the plan's "failed recommendations revert to the last
        // safe profile" exit criterion, exposed as an explicit
        // administrator action rather than only ever automatic.
        if (request.method == "POST" &&
            request.target == "/api/v1/performance/adaptive/rollback") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            adaptive_controller->rollback(*memory);
            audit.append("performance.adaptive.rollback", user->id, "success", "");
            return response(200, "OK", "{\"rolledBack\":true}");
        }
        // Phase 31: ScratchVolumeManager visibility/administration.
        // POST cleanup re-runs recover_orphans() on demand (the same
        // journal-driven sweep the constructor already ran once at
        // startup) so an administrator can reclaim orphaned scratch without
        // restarting the server.
        if (request.method == "GET" &&
            request.target == "/api/v1/system/scratch") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            scratch_volume_manager_status_json(*scratch_volumes));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/system/scratch/cleanup") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto removed = scratch_volumes->recover_orphans();
            audit.append("system.scratch.cleanup", user->id, "success",
                        std::to_string(removed));
            return response(200, "OK",
                            "{\"orphansRemoved\":" + std::to_string(removed) + "}");
        }
        // Phase 31 (Priority B): tier-migration tooling. Relocates an
        // already-published durable file (e.g. a GGUF model sitting on Tier
        // C) to a different tier's directory, verified byte-for-byte via
        // migrate_durable_file()'s SHA-256 check before the original is ever
        // removed, and recorded in durable_file_manifest so any reader still
        // holding the old path (LlamaCppAdapter::build_launch_spec resolving
        // a Model Registry entry's model file, in particular) transparently
        // finds it at its new location via resolve_durable_path() -- closing
        // the earlier "no central manifest" gap.
        if (request.method == "GET" &&
            request.target == "/api/v1/system/storage/manifest") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            durable_file_manifest_json(*durable_file_manifest));
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/system/storage/migrate") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            try {
                auto root = parse_json(request.body);
                const auto source_path_text = root.required("sourcePath").as_string();
                const auto destination_directory_text =
                    root.required("destinationDirectory").as_string();
                const auto* data_class_value = root.optional("dataClass");
                const auto data_class_text =
                    data_class_value ? data_class_value->as_string() : std::string{"gguf_model"};
                DurableDataClass data_class;
                if (data_class_text == "reconstructable_scratch") {
                    data_class = DurableDataClass::reconstructable_scratch;
                } else if (data_class_text == "gguf_model") {
                    data_class = DurableDataClass::gguf_model;
                } else if (data_class_text == "durable_chat") {
                    data_class = DurableDataClass::durable_chat;
                } else if (data_class_text == "audit_record") {
                    data_class = DurableDataClass::audit_record;
                } else if (data_class_text == "user_database") {
                    data_class = DurableDataClass::user_database;
                } else if (data_class_text == "resumable_download") {
                    data_class = DurableDataClass::resumable_download;
                } else if (data_class_text == "backup") {
                    data_class = DurableDataClass::backup;
                } else if (data_class_text == "security_record") {
                    data_class = DurableDataClass::security_record;
                } else if (data_class_text == "index_generation_sole_copy") {
                    data_class = DurableDataClass::index_generation_sole_copy;
                } else {
                    return response(400, "Bad Request",
                                    "{\"error\":\"invalid_data_class\"}");
                }
                const auto result = migrate_durable_file(
                    source_path_text, destination_directory_text, data_class,
                    *durable_file_manifest);
                audit.append("system.storage.migrate", user->id, "success",
                             source_path_text + " -> " + result.destination_path.string());
                return response(
                    200, "OK",
                    "{\"destinationPath\":" +
                        json_string(result.destination_path.string()) +
                        ",\"bytesMigrated\":" + std::to_string(result.bytes_migrated) +
                        ",\"sha256\":" + json_string(result.sha256_hex) +
                        ",\"recordedInManifest\":true}");
            } catch (const std::exception& error) {
                audit.append("system.storage.migrate", user->id, "denied", error.what());
                return response(400, "Bad Request",
                                "{\"error\":\"migration_failed\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        // Machine Learning foundation phase: Dashboard is the only real
        // interface behind this route so far -- see MachineLearningRegistry's
        // class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/dashboard") {
            if (auto denied = forbidden_unless(user->role, "ml.dashboard.view")) return *denied;
            return response(200, "OK",
                            machine_learning_dashboard_json(
                                machine_learning.dashboard(
                                    *ml_projects, *ml_models,
                                    *ml_training_jobs)));
        }
        // Phase 38: Machine Learning Projects (docs/PLAN.md "Machine
        // Learning Abilities" section 5), scoped to identity/intent/status
        // fields -- see MLProjectStore's class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/projects") {
            if (auto denied = forbidden_unless(user->role, "ml.projects.view")) return *denied;
            return response(200, "OK",
                            "{\"projects\":" +
                                ml_projects_json(ml_projects->list()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/projects") {
            if (auto denied = forbidden_unless(user->role, "ml.projects.create")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto description = text_field("description");
                const auto objective = text_field("objective");
                const auto subject_domain = text_field("subjectDomain");
                const auto model_task = text_field("modelTask");
                const auto project = ml_projects->create(
                    user->id, name, description, objective, subject_domain,
                    model_task);
                audit.append("ml.project.create", user->id, "success",
                             project.id);
                return response(201, "Created", ml_project_json(project));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_project\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/projects/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.projects.delete")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 7U);
            if (!ml_projects->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_project_not_found\"}");
            }
            audit.append("ml.project.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 39: Model Registry (docs/PLAN.md "Machine Learning
        // Abilities" section 7), scoped to identity/provenance/lifecycle
        // fields -- see ModelRegistryStore's class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/models") {
            if (auto denied = forbidden_unless(user->role, "ml.models.view")) return *denied;
            return response(200, "OK",
                            "{\"models\":" +
                                model_registry_entries_json(ml_models->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/models") {
            if (auto denied = forbidden_unless(user->role, "ml.models.import")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto entry = ml_models->create(
                    user->id, name, text_field("displayName"),
                    text_field("version"), text_field("family"),
                    text_field("task"), text_field("format"),
                    text_field("source"), text_field("license"));
                audit.append("ml.model.import", user->id, "success", entry.id);
                return response(201, "Created", model_registry_entry_json(entry));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_model\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/models/", 0U) == 0U &&
            request.target.size() > 6U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/state") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.models.approve")) return *denied;
            const auto id = request.target.substr(
                18U, request.target.size() - 18U - 6U);
            try {
                auto root = parse_json(request.body);
                const auto state =
                    parse_model_registry_state(root.required("state").as_string());
                if (!ml_models->set_state(id, state)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_model_not_found\"}");
                }
                audit.append("ml.model.state", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_model_state\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/models/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.models.delete")) return *denied;
            const auto id = request.target.substr(
                18U, request.target.size() - 18U - 7U);
            if (!ml_models->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_not_found\"}");
            }
            // Phase 56: the learned-weight artifact dies with its registry
            // entry.
            ml_trained_models->remove(id);
            audit.append("ml.model.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 56: real trained-model surface. GET /artifact reports what
        // was actually learned (method, schema, classes, trained-at); POST
        // /predict runs a live prediction through the persisted weights.
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/models/", 0U) == 0U &&
            request.target.size() > 9U &&
            request.target.compare(request.target.size() - 9U, 9U,
                                   "/artifact") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.models.view")) return *denied;
            const auto id = request.target.substr(
                18U, request.target.size() - 18U - 9U);
            const auto model = ml_trained_models->find(id);
            if (!model) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_not_trained\"}");
            }
            return response(200, "OK",
                            trained_tabular_model_summary_json(*model));
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/models/", 0U) == 0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/predict") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.models.view")) return *denied;
            const auto id = request.target.substr(
                18U, request.target.size() - 18U - 8U);
            const auto model = ml_trained_models->find(id);
            if (!model) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_not_trained\"}");
            }
            try {
                auto root = parse_json(request.body);
                // Features arrive as an object keyed by column name so a
                // caller never has to know the model's internal ordering.
                const auto& object = root.required("features").as_object();
                std::vector<double> features;
                features.reserve(model->feature_names.size());
                for (const auto& name : model->feature_names) {
                    const auto found = object.find(name);
                    if (found == object.end()) {
                        throw std::runtime_error("missing feature \"" + name +
                                                 "\"");
                    }
                    features.push_back(found->second.as_double());
                }
                const auto prediction = predict_tabular(*model, features);
                return response(200, "OK",
                                tabular_prediction_json(prediction, *model));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_prediction\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 39: Dataset Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 10), scoped to identity/provenance/approval
        // fields -- see DatasetStore's class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/datasets") {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.view")) return *denied;
            return response(200, "OK",
                            "{\"datasets\":" +
                                datasets_json(ml_datasets->list()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/datasets") {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.import")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto dataset = ml_datasets->create(
                    user->id, name, text_field("description"),
                    text_field("subjectArea"), text_field("source"),
                    text_field("license"), text_field("dataFormat"));
                audit.append("ml.dataset.import", user->id, "success",
                             dataset.id);
                return response(201, "Created", dataset_json(dataset));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_dataset\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/datasets/", 0U) == 0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/approve") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.approve")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 8U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_dataset_approval_status(
                    root.required("status").as_string());
                if (!ml_datasets->set_approval_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_dataset_not_found\"}");
                }
                audit.append("ml.dataset.approve", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_dataset_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/datasets/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.delete")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 7U);
            if (!ml_datasets->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_dataset_not_found\"}");
            }
            // Phase 56: uploaded CSV content dies with its dataset entry.
            ml_dataset_content->remove(id);
            audit.append("ml.dataset.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 56: real dataset content. POST uploads CSV bytes against a
        // registered dataset (validated by a full parse before anything is
        // stored); GET reports the parsed profile (row count, feature
        // columns, task, classes). This is what makes a Dataset trainable
        // instead of being a name in a list.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/datasets/", 0U) == 0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/content") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.import")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 8U);
            if (!ml_datasets->find(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_dataset_not_found\"}");
            }
            try {
                auto root = parse_json(request.body);
                const auto csv = root.required("csv").as_string();
                const auto* target = root.optional("targetColumn");
                const auto target_column =
                    target ? target->as_string() : std::string{};
                // Parse before storing so bad content is rejected now, not
                // at training time.
                const auto parsed = parse_tabular_csv(
                    csv, target_column,
                    configuration.tabular_dataset_maximum_csv_bytes);
                ml_dataset_content->put(id, csv, target_column);
                audit.append("ml.dataset.content", user->id, "success", id);
                return response(200, "OK",
                                tabular_dataset_profile_json(id, parsed));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_dataset_content\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/datasets/", 0U) == 0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/content") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.datasets.view")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 8U);
            const auto content = ml_dataset_content->find(id);
            if (!content) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_dataset_content_not_found\"}");
            }
            try {
                const auto parsed =
                    parse_tabular_csv(content->csv, content->target_column);
                return response(200, "OK",
                                tabular_dataset_profile_json(id, parsed));
            } catch (const std::exception& error) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_content_unreadable\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 40: Subject Knowledge Manager (docs/PLAN.md "Machine
        // Learning Abilities" section 12), scoped to identity/scope/
        // ownership/review-status fields -- see SubjectPackageStore's class
        // comment in masterai.hpp for the fields deferred to the later
        // Knowledge Ingestion Pipeline phase.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/subjects") {
            if (auto denied = forbidden_unless(user->role, "ml.subjects.view")) return *denied;
            return response(200, "OK",
                            "{\"subjects\":" +
                                subject_packages_json(ml_subjects->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/subjects") {
            if (auto denied = forbidden_unless(user->role, "ml.subjects.create")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto package = ml_subjects->create(
                    user->id, name, text_field("description"),
                    text_field("scope"), text_field("targetAudience"));
                audit.append("ml.subject.create", user->id, "success",
                             package.id);
                return response(201, "Created", subject_package_json(package));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_subject\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/subjects/", 0U) == 0U &&
            request.target.size() > 14U &&
            request.target.compare(request.target.size() - 14U, 14U,
                                   "/review-status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.subjects.review")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 14U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_subject_review_status(
                    root.required("status").as_string());
                if (!ml_subjects->set_review_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_subject_not_found\"}");
                }
                audit.append("ml.subject.review", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_subject_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/subjects/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.subjects.delete")) return *denied;
            const auto id = request.target.substr(
                20U, request.target.size() - 20U - 7U);
            const auto knowledge_documents =
                ml_knowledge_index->list_documents();
            if (std::any_of(
                    knowledge_documents.begin(), knowledge_documents.end(),
                    [&id](const KnowledgeDocument& document) {
                        return document.subject_id == id;
                    })) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_subject_has_knowledge_documents\","
                    "\"detail\":\"delete the subject's ingested knowledge "
                    "files first\"}");
            }
            if (!ml_subjects->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_subject_not_found\"}");
            }
            audit.append("ml.subject.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 41: Data Labeling (docs/PLAN.md "Machine Learning
        // Abilities" section 14), scoped to identity/target-dataset/
        // label-mode/assignment/status fields -- see LabelTaskStore's class
        // comment in masterai.hpp for the fields deferred to the phase that
        // creates actual label records.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/label-tasks") {
            if (auto denied = forbidden_unless(user->role, "ml.labels.view")) return *denied;
            return response(200, "OK",
                            "{\"labelTasks\":" +
                                label_tasks_json(ml_label_tasks->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/label-tasks") {
            if (auto denied = forbidden_unless(user->role, "ml.labels.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto task = ml_label_tasks->create(
                    user->id, dataset_id, name, text_field("description"),
                    text_field("labelMode"), text_field("assigneeId"));
                audit.append("ml.label_task.create", user->id, "success",
                             task.id);
                return response(201, "Created", label_task_json(task));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_label_task\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/label-tasks/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.labels.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status =
                    parse_label_task_status(root.required("status").as_string());
                if (!ml_label_tasks->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_label_task_not_found\"}");
                }
                audit.append("ml.label_task.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_label_task_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/label-tasks/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.labels.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            if (!ml_label_tasks->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_label_task_not_found\"}");
            }
            audit.append("ml.label_task.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 41: Data Preparation (docs/PLAN.md "Machine Learning
        // Abilities" section 15), scoped to identity/target-dataset/
        // operation/status fields -- see DataPreparationJobStore's class
        // comment in masterai.hpp for the pipeline-step composition,
        // logging, and reproducibility record deferred to the phase that
        // actually executes a pipeline.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/prep-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.dataprep.view")) return *denied;
            return response(200, "OK",
                            "{\"prepJobs\":" +
                                data_preparation_jobs_json(
                                    ml_prep_jobs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/prep-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.dataprep.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto operation = root.required("operation").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto job = ml_prep_jobs->create(
                    user->id, dataset_id, name, text_field("description"),
                    operation);
                audit.append("ml.prep_job.create", user->id, "success",
                             job.id);
                return response(201, "Created", data_preparation_job_json(job));
            } catch (const std::exception& error) {
                return response(400, "Bad Request",
                                "{\"error\":\"invalid_ml_prep_job\",\"detail\":\"" +
                                    json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/prep-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.dataprep.manage")) return *denied;
            const auto id = request.target.substr(
                21U, request.target.size() - 21U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_data_preparation_job_status(
                    root.required("status").as_string());
                if (!ml_prep_jobs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_prep_job_not_found\"}");
                }
                audit.append("ml.prep_job.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_prep_job_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/prep-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.dataprep.manage")) return *denied;
            const auto id = request.target.substr(
                21U, request.target.size() - 21U - 7U);
            if (!ml_prep_jobs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_prep_job_not_found\"}");
            }
            audit.append("ml.prep_job.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 42: Training Jobs (docs/PLAN.md "Machine Learning
        // Abilities" section 16), scoped to identity/target-project/target-
        // model/target-dataset/training-method/status fields -- see
        // TrainingJobStore's class comment in masterai.hpp for the compute/
        // hyperparameter/scheduling fields deferred to the phase that
        // actually executes a training run.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/training-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.training.view")) return *denied;
            return response(200, "OK",
                            "{\"trainingJobs\":" +
                                training_jobs_json(ml_training_jobs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/training-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.training.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto project_id = root.required("projectId").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto job = ml_training_jobs->create(
                    user->id, project_id, text_field("modelId"), dataset_id,
                    name, text_field("description"),
                    text_field("trainingType"));
                audit.append("ml.training_job.create", user->id, "success",
                             job.id);
                return response(201, "Created", training_job_json(job));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_training_job\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/training-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.training.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_training_job_status(
                    root.required("status").as_string());
                if (!ml_training_jobs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_training_job_not_found\"}");
                }
                audit.append("ml.training_job.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_training_job_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/training-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.training.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            if (!ml_training_jobs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_training_job_not_found\"}");
            }
            audit.append("ml.training_job.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 78 (this pass): live per-step training progress -- see
        // TrainingProgressTracker's class comment in masterai.hpp. Read-only,
        // so it uses the same view scope the job-listing GET route uses
        // rather than the .manage scope the mutating routes above require.
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/training-jobs/", 0U) == 0U &&
            request.target.size() > 14U &&
            request.target.compare(request.target.size() - 14U, 14U,
                                   "/live-progress") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.training.view")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 14U);
            return response(200, "OK",
                            training_progress_snapshot_json(
                                ml_training_progress.snapshot(id)));
        }
        // Phase 56: the real training executor. POST .../run trains the
        // job's dataset content by gradient descent right now: the job
        // moves through queued -> preparing -> running for real, the loss
        // curve comes from actual optimization steps, checkpoints record
        // genuinely measured losses, the learned weights persist as a
        // reloadable artifact, and the model registry entry (created here
        // if the job named none) lands in the evaluation state. Runs
        // synchronously: the accepted dataset sizes (<= 8 MiB CSV) train in
        // well under a request timeout.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/training-jobs/", 0U) == 0U &&
            request.target.size() > 4U &&
            request.target.compare(request.target.size() - 4U, 4U,
                                   "/run") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.training.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 4U);
            const auto job = ml_training_jobs->find(id);
            if (!job) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_training_job_not_found\"}");
            }
            const auto content = ml_dataset_content->find(job->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the job's dataset first\"}");
            }
            TabularTrainingOptions options;
            try {
                if (!request.body.empty()) {
                    auto root = parse_json(request.body);
                    const auto integer_field = [&root](const char* field,
                                                       const std::uint32_t fallback) {
                        const auto* value = root.optional(field);
                        return value ? static_cast<std::uint32_t>(value->as_integer())
                                     : fallback;
                    };
                    options.epochs = integer_field("epochs", options.epochs);
                    options.seed = integer_field("seed", options.seed);
                    options.checkpoint_interval = integer_field(
                        "checkpointInterval", options.checkpoint_interval);
                    if (const auto* rate = root.optional("learningRate")) {
                        options.learning_rate = rate->as_double();
                    }
                    if (const auto* fraction = root.optional("testFraction")) {
                        options.test_fraction = fraction->as_double();
                    }
                }
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_training_options\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
            try {
                const auto result =
                    execute_training_job(*job, *content, options, user->id);
                audit.append("ml.training_job.run", user->id, "success", id);
                return response(200, "OK",
                                tabular_training_report_json(result.report,
                                                             result.model));
            } catch (const std::exception& error) {
                audit.append("ml.training_job.run", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_training_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 43: Evaluation Lab (docs/PLAN.md "Machine Learning
        // Abilities" section 23), scoped to identity/target-model/target-
        // dataset/category/status fields -- see EvaluationRunStore's class
        // comment in masterai.hpp for the benchmark-set/human-evaluation/
        // pairwise-comparison/numeric-score fields deferred to the phase
        // that actually executes an evaluation.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/evaluation-runs") {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.view")) return *denied;
            return response(200, "OK",
                            "{\"evaluationRuns\":" +
                                evaluation_runs_json(ml_evaluation_runs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/evaluation-runs") {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto run = ml_evaluation_runs->create(
                    user->id, model_id, dataset_id, name,
                    text_field("description"), text_field("category"));
                audit.append("ml.evaluation_run.create", user->id, "success",
                             run.id);
                return response(201, "Created", evaluation_run_json(run));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_evaluation_run\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/evaluation-runs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.manage")) return *denied;
            const auto id = request.target.substr(
                27U, request.target.size() - 27U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_evaluation_run_status(
                    root.required("status").as_string());
                if (!ml_evaluation_runs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_evaluation_run_not_found\"}");
                }
                audit.append("ml.evaluation_run.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_evaluation_run_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/evaluation-runs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.manage")) return *denied;
            const auto id = request.target.substr(
                27U, request.target.size() - 27U - 7U);
            if (!ml_evaluation_runs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_evaluation_run_not_found\"}");
            }
            // Phase 56: an executed run's stored metrics die with it.
            ml_evaluation_results->remove(id);
            audit.append("ml.evaluation_run.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 56: the real evaluation harness. POST .../run scores the
        // run's trained model against the run's dataset content right now
        // and stores the genuine metrics (accuracy/precision/recall/F1 and
        // confusion matrix, or MSE/MAE/R-squared); GET .../result returns
        // them later. This is the "actual numeric score" Evaluation Lab
        // was originally scoped down without.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/evaluation-runs/", 0U) == 0U &&
            request.target.size() > 4U &&
            request.target.compare(request.target.size() - 4U, 4U,
                                   "/run") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.manage")) return *denied;
            const auto id = request.target.substr(
                27U, request.target.size() - 27U - 4U);
            const auto run = ml_evaluation_runs->find(id);
            if (!run) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_evaluation_run_not_found\"}");
            }
            const auto model = ml_trained_models->find(run->model_id);
            if (!model) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_model_not_trained\",\"detail\":"
                    "\"run a training job for this model first\"}");
            }
            const auto content = ml_dataset_content->find(run->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the run's dataset first\"}");
            }
            try {
                const auto metrics_json =
                    execute_evaluation_run(*run, *model, *content);
                audit.append("ml.evaluation_run.run", user->id, "success", id);
                return response(200, "OK",
                                "{\"runId\":\"" + json_escape(id) +
                                    "\",\"metrics\":" + metrics_json + "}");
            } catch (const std::exception& error) {
                audit.append("ml.evaluation_run.run", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_evaluation_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/evaluation-runs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/result") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.evaluation.view")) return *denied;
            const auto id = request.target.substr(
                27U, request.target.size() - 27U - 7U);
            const auto metrics_json = ml_evaluation_results->find(id);
            if (!metrics_json) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_evaluation_result_not_found\"}");
            }
            return response(200, "OK",
                            "{\"runId\":\"" + json_escape(id) +
                                "\",\"metrics\":" + *metrics_json + "}");
        }
        // Phase 44/80: Experiment Tracking (docs/PLAN.md "Machine Learning
        // Abilities" section 25). Phase 80 made this a REAL executor phase
        // like Phase 56/57/70 before it -- POST .../run genuinely trains
        // the experiment's dataset content, computes real training/
        // validation/evaluation metrics and captures real checkpoints (see
        // execute_experiment_run() above), GET .../result recalls them,
        // and POST .../compare builds a genuine side-by-side diff of two
        // or more experiments (see experiments_comparison_json() in
        // ml_engine.cpp).
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/experiments") {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.view")) return *denied;
            return response(200, "OK",
                            "{\"experiments\":" +
                                experiments_json(ml_experiments->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/experiments") {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto project_id = root.required("projectId").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto seed_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? static_cast<std::uint32_t>(value->as_integer())
                                 : std::uint32_t{0};
                };
                const auto experiment = ml_experiments->create(
                    user->id, project_id, model_id, text_field("datasetId"),
                    name, text_field("description"),
                    text_field("hyperparametersJson"),
                    seed_field("randomSeed"), text_field("sourceCodeVersion"),
                    text_field("configurationVersion"),
                    text_field("containerVersion"), text_field("tags"),
                    text_field("notes"));
                audit.append("ml.experiment.create", user->id, "success",
                             experiment.id);
                return response(201, "Created", experiment_json(experiment));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_experiment\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/experiments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_experiment_status(
                    root.required("status").as_string());
                if (!ml_experiments->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_experiment_not_found\"}");
                }
                audit.append("ml.experiment.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_experiment_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/experiments/", 0U) == 0U &&
            request.target.size() > 6U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/notes") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 6U);
            try {
                auto root = parse_json(request.body);
                const auto existing = ml_experiments->find(id);
                if (!existing) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_experiment_not_found\"}");
                }
                const auto text_field = [&root](const char* field,
                                                const std::string& fallback) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : fallback;
                };
                const auto* seed_value = root.optional("randomSeed");
                const auto seed = seed_value
                                      ? static_cast<std::uint32_t>(
                                            seed_value->as_integer())
                                      : existing->random_seed;
                ml_experiments->update_metadata(
                    id, text_field("hyperparametersJson",
                                  existing->hyperparameters_json),
                    seed,
                    text_field("sourceCodeVersion",
                              existing->source_code_version),
                    text_field("configurationVersion",
                              existing->configuration_version),
                    text_field("containerVersion", existing->container_version),
                    text_field("tags", existing->tags),
                    text_field("notes", existing->notes));
                audit.append("ml.experiment.notes", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_experiment_notes\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 80: the real executor. Runs synchronously, same latency
        // profile as Training Jobs' own .../run -- the accepted dataset
        // sizes train in well under a request timeout.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/experiments/", 0U) == 0U &&
            request.target.size() > 4U &&
            request.target.compare(request.target.size() - 4U, 4U,
                                   "/run") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 4U);
            const auto experiment = ml_experiments->find(id);
            if (!experiment) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_experiment_not_found\"}");
            }
            const auto content = ml_dataset_content->find(experiment->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the experiment's dataset first\"}");
            }
            TabularTrainingOptions options;
            if (experiment->random_seed != 0U) {
                options.seed = experiment->random_seed;
            }
            try {
                if (!request.body.empty()) {
                    auto root = parse_json(request.body);
                    const auto integer_field = [&root](const char* field,
                                                       const std::uint32_t fallback) {
                        const auto* value = root.optional(field);
                        return value ? static_cast<std::uint32_t>(value->as_integer())
                                     : fallback;
                    };
                    options.epochs = integer_field("epochs", options.epochs);
                    options.seed = integer_field("seed", options.seed);
                    options.checkpoint_interval = integer_field(
                        "checkpointInterval", options.checkpoint_interval);
                    if (const auto* rate = root.optional("learningRate")) {
                        options.learning_rate = rate->as_double();
                    }
                    if (const auto* fraction = root.optional("testFraction")) {
                        options.test_fraction = fraction->as_double();
                    }
                }
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_experiment_options\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
            try {
                execute_experiment_run(*experiment, *content, options, user->id);
                audit.append("ml.experiment.run", user->id, "success", id);
                const auto result_json = ml_experiment_results->find(id);
                return response(200, "OK",
                                "{\"experimentId\":\"" + json_escape(id) +
                                    "\",\"result\":" +
                                    (result_json ? *result_json : "null") +
                                    "}");
            } catch (const std::exception& error) {
                audit.append("ml.experiment.run", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_experiment_run_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/experiments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/result") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.view")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            const auto result_json = ml_experiment_results->find(id);
            if (!result_json) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_experiment_no_result\"}");
            }
            return response(200, "OK",
                            "{\"experimentId\":\"" + json_escape(id) +
                                "\",\"result\":" + *result_json + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/experiments/compare") {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.view")) return *denied;
            try {
                auto root = parse_json(request.body);
                std::vector<std::string> ids;
                for (const auto& value : root.required("experimentIds").as_array()) {
                    ids.push_back(value.as_string());
                }
                const auto result = experiments_comparison_json(
                    ids,
                    [this](const std::string& experiment_id) {
                        return ml_experiments->find(experiment_id);
                    },
                    [this](const std::string& experiment_id) {
                        return ml_experiment_results->find(experiment_id);
                    });
                audit.append("ml.experiment.compare", user->id, "success",
                             std::to_string(ids.size()) + " experiments");
                return response(200, "OK", result);
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_experiment_compare\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/experiments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.experiments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            if (!ml_experiments->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_experiment_not_found\"}");
            }
            ml_experiment_results->remove(id);
            audit.append("ml.experiment.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 45: Fine-Tuning Interface (docs/PLAN.md "Machine Learning
        // Abilities" section 18), scoped to identity/target-project/target-
        // model/target-dataset/method/status fields -- see
        // FineTuningJobStore's class comment in masterai.hpp for the
        // adapter-method/hyperparameter/checkpoint/output-model fields
        // deferred to the phase that actually executes a fine-tuning run.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/fine-tuning-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.view")) return *denied;
            return response(200, "OK",
                            "{\"fineTuningJobs\":" +
                                fine_tuning_jobs_json(ml_fine_tuning_jobs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/fine-tuning-jobs") {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto job = ml_fine_tuning_jobs->create(
                    user->id, text_field("projectId"), model_id, dataset_id,
                    name, text_field("description"), text_field("method"));
                audit.append("ml.fine_tuning_job.create", user->id, "success",
                             job.id);
                return response(201, "Created", fine_tuning_job_json(job));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_fine_tuning_job\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/fine-tuning-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.manage")) return *denied;
            const auto id = request.target.substr(
                28U, request.target.size() - 28U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_fine_tuning_job_status(
                    root.required("status").as_string());
                if (!ml_fine_tuning_jobs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_fine_tuning_job_not_found\"}");
                }
                audit.append("ml.fine_tuning_job.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_fine_tuning_job_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/fine-tuning-jobs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.manage")) return *denied;
            const auto id = request.target.substr(
                28U, request.target.size() - 28U - 7U);
            if (!ml_fine_tuning_jobs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_fine_tuning_job_not_found\"}");
            }
            audit.append("ml.fine_tuning_job.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 70: the real fine-tuning executor. POST .../run adapts the
        // job's already-trained base model to the job's dataset by warm-
        // started gradient descent (see train_tabular_model's warm_start
        // parameter and execute_fine_tuning_job above): the job moves
        // through queued -> preparing -> running for real, the resulting
        // weights genuinely continue from the base model rather than
        // starting from zero, and the adapted model is registered as a new
        // Model Registry entry awaiting Evaluation Lab review, leaving the
        // base model untouched.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/fine-tuning-jobs/", 0U) == 0U &&
            request.target.size() > 4U &&
            request.target.compare(request.target.size() - 4U, 4U,
                                   "/run") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.manage")) return *denied;
            const auto id = request.target.substr(
                28U, request.target.size() - 28U - 4U);
            const auto job = ml_fine_tuning_jobs->find(id);
            if (!job) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_fine_tuning_job_not_found\"}");
            }
            // Phase 73: a "llm:"-prefixed method (e.g. "llm:code assistant")
            // routes to the real LLM LoRA executor instead of Phase 70's
            // tabular warm-start path -- a free-text convention matching
            // FineTuningJob.method's own "free text, not a closed enum"
            // design rather than a second type field. Runs on a detached
            // background thread (run_llm_fine_tuning_job above) and returns
            // 202 immediately, since a real llama.cpp finetune run can take
            // far longer than the HTTP request timeout.
            if (job->method.size() >= 4U &&
                ascii_lower(job->method.substr(0U, 4U)) == "llm:") {
                const auto base_entry = ml_models->find(job->model_id);
                if (!base_entry) {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_fine_tuning_base_model_not_found\"}");
                }
                if (base_entry->source.empty() ||
                    !std::filesystem::is_regular_file(base_entry->source)) {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_fine_tuning_base_model_file_missing\","
                        "\"detail\":\"Model Registry entry " +
                            json_escape(base_entry->id) +
                            "'s source field is not a real, existing file "
                            "path (\\\"" +
                            json_escape(base_entry->source) +
                            "\\\"); set it to the base GGUF's real path "
                            "first\"}");
                }
                const auto content = ml_dataset_content->find(job->dataset_id);
                if (!content) {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                        "\"upload CSV content to the job's dataset first\"}");
                }
                LlmFineTuneOptions llm_options;
                try {
                    if (!request.body.empty()) {
                        auto root = parse_json(request.body);
                        if (const auto* epochs = root.optional("epochs")) {
                            llm_options.epochs = static_cast<std::uint32_t>(
                                epochs->as_integer());
                        }
                        if (const auto* rate = root.optional("learningRate")) {
                            llm_options.learning_rate = rate->as_double();
                        }
                        if (const auto* extra =
                                root.optional("extraFinetuneArguments")) {
                            llm_options.extra_finetune_arguments =
                                extra->as_string();
                        }
                        if (const auto* extra =
                                root.optional("extraExportLoraArguments")) {
                            llm_options.extra_export_lora_arguments =
                                extra->as_string();
                        }
                    }
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_llm_fine_tuning_options\","
                        "\"detail\":\"" + json_escape(error.what()) + "\"}");
                }
                ml_fine_tuning_jobs->set_status(id, FineTuningJobStatus::queued);
                const FineTuningJob job_copy = *job;
                const ModelRegistryEntry base_entry_copy = *base_entry;
                const DatasetContentStore::Content content_copy = *content;
                const std::string acting_user_id = user->id;
                std::thread([this, job_copy, base_entry_copy, content_copy,
                            llm_options, acting_user_id]() {
                    run_llm_fine_tuning_job(job_copy, base_entry_copy,
                                            content_copy, llm_options,
                                            acting_user_id);
                }).detach();
                audit.append("ml.fine_tuning_job.llm_run", user->id, "started",
                            id);
                return response(
                    202, "Accepted",
                    "{\"status\":\"queued\",\"jobId\":\"" + json_escape(id) +
                        "\",\"detail\":\"LLM fine-tuning is running in the "
                        "background; poll GET /api/v1/ml/fine-tuning-jobs/" +
                        json_escape(id) + "/llm-result for the outcome\"}");
            }
            const auto base_model = ml_trained_models->find(job->model_id);
            if (!base_model) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_fine_tuning_base_model_not_trained\",\"detail\":"
                    "\"the job's base model has no trained weights yet; "
                    "train it first\"}");
            }
            const auto content = ml_dataset_content->find(job->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the job's dataset first\"}");
            }
            TabularTrainingOptions options;
            try {
                if (!request.body.empty()) {
                    auto root = parse_json(request.body);
                    const auto integer_field = [&root](const char* field,
                                                       const std::uint32_t fallback) {
                        const auto* value = root.optional(field);
                        return value ? static_cast<std::uint32_t>(value->as_integer())
                                     : fallback;
                    };
                    options.epochs = integer_field("epochs", options.epochs);
                    options.seed = integer_field("seed", options.seed);
                    options.checkpoint_interval = integer_field(
                        "checkpointInterval", options.checkpoint_interval);
                    if (const auto* rate = root.optional("learningRate")) {
                        options.learning_rate = rate->as_double();
                    }
                    if (const auto* fraction = root.optional("testFraction")) {
                        options.test_fraction = fraction->as_double();
                    }
                }
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_fine_tuning_options\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
            try {
                const auto result = execute_fine_tuning_job(
                    *job, *base_model, *content, options, user->id);
                audit.append("ml.fine_tuning_job.run", user->id, "success", id);
                return response(200, "OK",
                                tabular_training_report_json(result.report,
                                                             result.model));
            } catch (const std::exception& error) {
                audit.append("ml.fine_tuning_job.run", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_fine_tuning_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 73: polling endpoint for a background LLM LoRA fine-tuning
        // run started by the "llm:"-prefixed branch of POST .../run above.
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/fine-tuning-jobs/", 0U) == 0U &&
            request.target.size() > 11U &&
            request.target.compare(request.target.size() - 11U, 11U,
                                   "/llm-result") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.finetuning.view")) return *denied;
            const auto id = request.target.substr(
                28U, request.target.size() - 28U - 11U);
            const auto result_json = ml_llm_finetune_results->find(id);
            if (!result_json) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_llm_fine_tuning_result_not_found\",\"detail\":"
                    "\"no LLM fine-tuning run has completed for this job yet\"}");
            }
            return response(200, "OK", *result_json);
        }
        // Phase 46: Model Builder Interface (docs/PLAN.md "Machine Learning
        // Abilities" section 9) at full surface: identity/target-project/
        // base-model/source-type/status fields at creation, plus the
        // complete build-settings list (architecture, layers, tokenizer,
        // optimiser, scheduling, reproducibility -- ModelBuilderSettings in
        // masterai.hpp) applied through the /configure endpoint below.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/model-builder-configs") {
            if (auto denied = forbidden_unless(user->role, "ml.modelbuilder.view")) return *denied;
            return response(200, "OK",
                            "{\"modelBuilderConfigs\":" +
                                model_builder_configs_json(ml_model_builder_configs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/model-builder-configs") {
            if (auto denied = forbidden_unless(user->role, "ml.modelbuilder.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto source_type = root.required("sourceType").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto config = ml_model_builder_configs->create(
                    user->id, text_field("projectId"), text_field("baseModelId"),
                    name, text_field("description"), source_type);
                audit.append("ml.model_builder_config.create", user->id, "success",
                             config.id);
                return response(201, "Created", model_builder_config_json(config));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_builder_config\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-builder-configs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.modelbuilder.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_model_builder_config_status(
                    root.required("status").as_string());
                if (!ml_model_builder_configs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_model_builder_config_not_found\"}");
                }
                audit.append("ml.model_builder_config.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_builder_config_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Full section 9 surface: replace a configuration's build settings.
        // Fields absent from the request keep their current values (the web
        // UI pre-fills the form from the stored settings, but an API caller
        // may send only the fields it wants to change).
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-builder-configs/", 0U) == 0U &&
            request.target.size() > 10U &&
            request.target.compare(request.target.size() - 10U, 10U,
                                   "/configure") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.modelbuilder.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 10U);
            const auto existing = ml_model_builder_configs->find(id);
            if (!existing) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_builder_config_not_found\"}");
            }
            try {
                auto root = parse_json(request.body);
                auto settings = existing->settings;
                const auto text = [&root](const char* field, std::string& out) {
                    if (const auto* value = root.optional(field)) {
                        out = value->as_string();
                    }
                };
                const auto count = [&root](const char* field,
                                           std::uint64_t& out) {
                    if (const auto* value = root.optional(field)) {
                        const auto number = value->as_integer();
                        if (number < 0) {
                            throw std::invalid_argument(
                                std::string(field) + " must not be negative");
                        }
                        out = static_cast<std::uint64_t>(number);
                    }
                };
                const auto fraction = [&root](const char* field, double& out) {
                    if (const auto* value = root.optional(field)) {
                        out = value->as_double();
                    }
                };
                const auto flag = [&root](const char* field, bool& out) {
                    if (const auto* value = root.optional(field)) {
                        out = value->as_boolean();
                    }
                };
                text("configurationMode", settings.configuration_mode);
                text("architecture", settings.architecture);
                text("layerConfiguration", settings.layer_configuration);
                count("hiddenDimensions", settings.hidden_dimensions);
                text("attentionConfiguration",
                     settings.attention_configuration);
                text("vocabularyTokenizer", settings.vocabulary_tokenizer);
                count("sequenceLength", settings.sequence_length);
                text("activationFunctions", settings.activation_functions);
                fraction("dropout", settings.dropout);
                text("initialisationStrategy",
                     settings.initialisation_strategy);
                text("lossFunction", settings.loss_function);
                text("optimiser", settings.optimiser);
                text("learningRateScheduler",
                     settings.learning_rate_scheduler);
                count("batchSize", settings.batch_size);
                count("epochCount", settings.epoch_count);
                count("gradientAccumulation", settings.gradient_accumulation);
                fraction("gradientClipping", settings.gradient_clipping);
                flag("mixedPrecision", settings.mixed_precision);
                count("checkpointFrequency", settings.checkpoint_frequency);
                count("validationFrequency", settings.validation_frequency);
                flag("earlyStopping", settings.early_stopping);
                count("randomSeed", settings.random_seed);
                text("reproducibilitySettings",
                     settings.reproducibility_settings);
                text("distributedTrainingSettings",
                     settings.distributed_training_settings);
                if (!ml_model_builder_configs->configure(id, settings)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_model_builder_config_not_found\"}");
                }
                audit.append("ml.model_builder_config.configure", user->id,
                             "success", id);
                return response(200, "OK",
                                model_builder_config_json(
                                    *ml_model_builder_configs->find(id)));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_builder_config_settings\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-builder-configs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.modelbuilder.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 7U);
            if (!ml_model_builder_configs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_builder_config_not_found\"}");
            }
            audit.append("ml.model_builder_config.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 47/81: Prompt and Instruction Training (docs/PLAN.md
        // "Machine Learning Abilities" section 19). Phase 81 made this a
        // real content phase: InstructionExampleContentStore now holds the
        // full instruction record (system/user instruction, context,
        // expected/rejected response, tool calls/results, output format,
        // difficulty, safety classification) InstructionExampleStore's own
        // class comment deferred, .../generate and .../test genuinely
        // invoke a model via execute_rag_generation, and .../duplicates,
        // .../contradictions, and .../validate run real (if heuristic --
        // see their own doc comments in masterai.hpp) detection/validation
        // logic instead of leaving those admin operations unimplemented.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/instruction-examples") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.view")) return *denied;
            return response(200, "OK",
                            "{\"instructionExamples\":" +
                                instruction_examples_json(
                                    ml_instruction_examples->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/instruction-examples") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto example = ml_instruction_examples->create(
                    user->id, dataset_id, name, text_field("description"),
                    text_field("subjectClassification"));
                audit.append("ml.instruction_example.create", user->id,
                             "success", example.id);
                return response(201, "Created",
                                instruction_example_json(example));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 81: bulk create -- one InstructionExample + content record
        // per entry. All-or-nothing is not required (a large import batch
        // should not lose every good entry over one bad one), so each
        // entry reports its own success/failure rather than the endpoint
        // fabricating a false all-succeeded result.
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/instruction-examples/import") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto dataset_id = root.required("datasetId").as_string();
                std::string results = "[";
                bool first_result = true;
                for (const auto& entry : root.required("examples").as_array()) {
                    if (!first_result) results += ",";
                    first_result = false;
                    try {
                        const auto text_field = [&entry](const char* field) {
                            const auto* value = entry.optional(field);
                            return value ? value->as_string() : std::string{};
                        };
                        const auto example = ml_instruction_examples->create(
                            user->id, dataset_id,
                            entry.required("name").as_string(),
                            text_field("description"),
                            text_field("subjectClassification"));
                        if (const auto* content_value = entry.optional("content")) {
                            const auto& content_field =
                                [content_value](const char* field) {
                                    const auto* value =
                                        content_value->optional(field);
                                    return value ? value->as_string()
                                                 : std::string{};
                                };
                            InstructionExampleContent content;
                            content.system_instruction =
                                content_field("systemInstruction");
                            content.user_instruction =
                                content_field("userInstruction");
                            content.context = content_field("context");
                            content.expected_response =
                                content_field("expectedResponse");
                            content.rejected_response =
                                content_field("rejectedResponse");
                            content.tool_calls_json =
                                content_field("toolCallsJson");
                            content.tool_results_json =
                                content_field("toolResultsJson");
                            content.required_output_format =
                                content_field("requiredOutputFormat");
                            content.difficulty = content_field("difficulty");
                            content.safety_classification =
                                content_field("safetyClassification");
                            ml_instruction_example_content->put(example.id,
                                                                content);
                        }
                        audit.append("ml.instruction_example.import", user->id,
                                     "success", example.id);
                        results += "{\"success\":true,\"id\":\"" +
                                   json_escape(example.id) + "\"}";
                    } catch (const std::exception& entry_error) {
                        results += "{\"success\":false,\"error\":\"" +
                                   json_escape(entry_error.what()) + "\"}";
                    }
                }
                results += "]";
                return response(200, "OK", "{\"results\":" + results + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_import\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 81: generates a real draft example. Composes a prompt from
        // the given system/user instruction and context, invokes the model
        // via execute_rag_generation, and creates a new example (status
        // draft) with the real generated text as expected_response --
        // section 19's "generated training examples must require approval
        // before entering an approved dataset" is enforced by the
        // .../status handler below refusing "approved" without content
        // (which every generated example already has) and by draft simply
        // starting there like every manually-created example does.
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/instruction-examples/generate") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto dataset_id = root.required("datasetId").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto system_instruction = text_field("systemInstruction");
                const auto user_instruction = text_field("userInstruction");
                const auto context = text_field("context");
                std::string prompt;
                if (!system_instruction.empty()) prompt += system_instruction + "\n\n";
                if (!context.empty()) prompt += "Context: " + context + "\n\n";
                prompt += user_instruction;
                const auto generated = execute_rag_generation(model_id, prompt);
                auto example_name = text_field("name");
                if (example_name.empty()) example_name = "generated-example";
                const auto example = ml_instruction_examples->create(
                    user->id, dataset_id, example_name,
                    text_field("description"),
                    text_field("subjectClassification"));
                InstructionExampleContent content;
                content.system_instruction = system_instruction;
                content.user_instruction = user_instruction;
                content.context = context;
                content.expected_response = generated.text;
                content.required_output_format = text_field("requiredOutputFormat");
                content.difficulty = text_field("difficulty");
                content.safety_classification = text_field("safetyClassification");
                ml_instruction_example_content->put(example.id, content);
                audit.append("ml.instruction_example.generate", user->id,
                             "success", example.id);
                return response(
                    201, "Created",
                    "{\"example\":" + instruction_example_json(example) +
                        ",\"content\":" +
                        instruction_example_content_json(content) + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_generate\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_instruction_example_status(
                    root.required("status").as_string());
                // Phase 81: enforces section 19's "generated training
                // examples must require approval before entering an
                // approved dataset" -- concretely, no example (generated
                // or manual) can reach `approved` without a real content
                // record for a reviewer to have actually approved.
                if (status == InstructionExampleStatus::approved &&
                    !ml_instruction_example_content->find(id)) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"ml_instruction_example_not_reviewable\","
                        "\"detail\":\"an example needs content before it can "
                        "be approved\"}");
                }
                if (!ml_instruction_examples->set_status(id, status)) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"ml_instruction_example_not_found\"}");
                }
                audit.append("ml.instruction_example.status", user->id,
                             "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/content") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 8U);
            if (!ml_instruction_examples->find(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_instruction_example_not_found\"}");
            }
            try {
                auto root = parse_json(request.body);
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                InstructionExampleContent content;
                content.system_instruction = text_field("systemInstruction");
                content.user_instruction = text_field("userInstruction");
                content.context = text_field("context");
                content.expected_response = text_field("expectedResponse");
                content.rejected_response = text_field("rejectedResponse");
                content.tool_calls_json = text_field("toolCallsJson");
                content.tool_results_json = text_field("toolResultsJson");
                content.required_output_format =
                    text_field("requiredOutputFormat");
                content.difficulty = text_field("difficulty");
                content.safety_classification =
                    text_field("safetyClassification");
                ml_instruction_example_content->put(id, content);
                audit.append("ml.instruction_example.content", user->id,
                             "success", id);
                return response(200, "OK",
                                instruction_example_content_json(content));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_content\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/content") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.view")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 8U);
            const auto content = ml_instruction_example_content->find(id);
            if (!content) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_instruction_example_no_content\"}");
            }
            return response(200, "OK", instruction_example_content_json(*content));
        }
        // Phase 81: real fan-out probe -- not persisted, since it is a
        // test of the existing content against other models, not new
        // content of its own.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 5U &&
            request.target.compare(request.target.size() - 5U, 5U,
                                   "/test") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 5U);
            const auto content = ml_instruction_example_content->find(id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_instruction_example_no_content\"}");
            }
            try {
                auto root = parse_json(request.body);
                std::string prompt;
                if (!content->system_instruction.empty()) {
                    prompt += content->system_instruction + "\n\n";
                }
                if (!content->context.empty()) {
                    prompt += "Context: " + content->context + "\n\n";
                }
                prompt += content->user_instruction;
                std::string results = "[";
                bool first_result = true;
                for (const auto& value : root.required("modelIds").as_array()) {
                    if (!first_result) results += ",";
                    first_result = false;
                    const auto model_id = value.as_string();
                    try {
                        const auto started = std::chrono::steady_clock::now();
                        const auto generated =
                            execute_rag_generation(model_id, prompt);
                        const auto elapsed =
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - started);
                        results += "{\"modelId\":\"" + json_escape(model_id) +
                                   "\",\"response\":\"" +
                                   json_escape(generated.text) +
                                   "\",\"elapsedMicroseconds\":" +
                                   std::to_string(elapsed.count()) + "}";
                    } catch (const std::exception& model_error) {
                        results += "{\"modelId\":\"" + json_escape(model_id) +
                                   "\",\"error\":\"" +
                                   json_escape(model_error.what()) + "\"}";
                    }
                }
                results += "]";
                audit.append("ml.instruction_example.test", user->id,
                             "success", id);
                return response(200, "OK", "{\"results\":" + results + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_test\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 9U &&
            request.target.compare(request.target.size() - 9U, 9U,
                                   "/validate") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.view")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 9U);
            const auto content = ml_instruction_example_content->find(id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_instruction_example_no_content\"}");
            }
            std::string error_detail;
            const bool valid = validate_structured_output(*content, error_detail);
            audit.append("ml.instruction_example.validate", user->id,
                         valid ? "success" : "failure", id);
            return response(200, "OK",
                            "{\"valid\":" + std::string(valid ? "true" : "false") +
                                ",\"detail\":\"" + json_escape(error_detail) +
                                "\"}");
        }
        // Phase 81: "detect duplicated examples"/"detect contradictory
        // instructions" -- both take a POST body rather than a query
        // string, matching /api/v1/ml/experiments/compare's own precedent
        // (this codebase has no query-string parsing utility anywhere
        // else, so this doesn't invent one for two endpoints).
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/instruction-examples/duplicates") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.view")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto dataset_id = root.required("datasetId").as_string();
                std::vector<InstructionExample> in_dataset;
                for (const auto& example : ml_instruction_examples->list()) {
                    if (example.dataset_id == dataset_id) {
                        in_dataset.push_back(example);
                    }
                }
                const auto pairs = detect_duplicate_instruction_examples(
                    in_dataset, [this](const std::string& example_id) {
                        return ml_instruction_example_content->find(example_id);
                    });
                std::string body = "[";
                bool first_pair = true;
                for (const auto& pair : pairs) {
                    if (!first_pair) body += ",";
                    first_pair = false;
                    body += "{\"firstId\":\"" + json_escape(pair.first) +
                            "\",\"secondId\":\"" + json_escape(pair.second) + "\"}";
                }
                body += "]";
                return response(200, "OK", "{\"duplicates\":" + body + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_duplicates\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target ==
                "/api/v1/ml/instruction-examples/contradictions") {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.view")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto dataset_id = root.required("datasetId").as_string();
                std::vector<InstructionExample> in_dataset;
                for (const auto& example : ml_instruction_examples->list()) {
                    if (example.dataset_id == dataset_id) {
                        in_dataset.push_back(example);
                    }
                }
                const auto pairs = detect_contradictory_instruction_examples(
                    in_dataset, [this](const std::string& example_id) {
                        return ml_instruction_example_content->find(example_id);
                    });
                std::string body = "[";
                bool first_pair = true;
                for (const auto& pair : pairs) {
                    if (!first_pair) body += ",";
                    first_pair = false;
                    body += "{\"firstId\":\"" + json_escape(pair.first) +
                            "\",\"secondId\":\"" + json_escape(pair.second) + "\"}";
                }
                body += "]";
                return response(200, "OK", "{\"contradictions\":" + body + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_instruction_example_contradictions\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/instruction-examples/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.instructions.manage")) return *denied;
            const auto id = request.target.substr(
                33U, request.target.size() - 33U - 7U);
            if (!ml_instruction_examples->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_instruction_example_not_found\"}");
            }
            ml_instruction_example_content->remove(id);
            audit.append("ml.instruction_example.delete", user->id, "success",
                         id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 48: Synthetic Data Generation (docs/PLAN.md "Machine
        // Learning Abilities" section 20), scoped to identity/target-
        // dataset/generation-technique/status fields -- see
        // SyntheticRecordStore's class comment in masterai.hpp for the
        // generator-model/generator-version/prompt/generation-settings/
        // confidence-score/original-source-linkage fields deferred to the
        // phase that actually creates generated records.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/synthetic-records") {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.view")) return *denied;
            return response(200, "OK",
                            "{\"syntheticRecords\":" +
                                synthetic_records_json(
                                    ml_synthetic_records->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/synthetic-records") {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto record = ml_synthetic_records->create(
                    user->id, dataset_id, name, text_field("description"),
                    text_field("generationTechnique"));
                audit.append("ml.synthetic_record.create", user->id,
                             "success", record.id);
                return response(201, "Created", synthetic_record_json(record));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_synthetic_record\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/synthetic-records/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.manage")) return *denied;
            const auto id = request.target.substr(
                30U, request.target.size() - 30U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_synthetic_record_status(
                    root.required("status").as_string());
                if (!ml_synthetic_records->set_status(id, status)) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"ml_synthetic_record_not_found\"}");
                }
                audit.append("ml.synthetic_record.status", user->id,
                             "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_synthetic_record_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/synthetic-records/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.manage")) return *denied;
            const auto id = request.target.substr(
                30U, request.target.size() - 30U - 7U);
            if (!ml_synthetic_records->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_synthetic_record_not_found\"}");
            }
            ml_synthetic_record_content->remove(id);
            audit.append("ml.synthetic_record.delete", user->id, "success",
                         id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Deployment Manager/Inference Endpoints/Synthetic Data completion
        // phase: the real generation executor the class comment above used
        // to defer. Composes a technique-specific prompt (falling back to a
        // generic template for any technique not in this fixed list, since
        // section 20's operations are open-ended) and invokes the same
        // execute_rag_generation path the instruction-example generator
        // above uses, then stores the real result via
        // SyntheticRecordContentStore.
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/synthetic-records/generate") {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto dataset_id = root.required("datasetId").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto technique = text_field("generationTechnique");
                const auto source_text = text_field("sourceText");
                static const std::map<std::string, std::string> technique_templates = {
                    {"alternative_questions",
                     "Generate three alternative phrasings of the following "
                     "question, one per line, preserving its exact meaning:\n"},
                    {"paraphrase",
                     "Rewrite the following text with the same meaning but "
                     "different wording:\n"},
                    {"example",
                     "Write a new, realistic example consistent with the "
                     "following:\n"},
                    {"counterexample",
                     "Write a counterexample that contradicts or breaks the "
                     "following:\n"},
                    {"difficult_case",
                     "Write an unusually difficult or ambiguous case similar "
                     "to the following:\n"},
                    {"malformed_input",
                     "Write a malformed or invalid variant of the following "
                     "input, the kind a robust system must reject cleanly:\n"},
                    {"edge_case",
                     "Write an edge case (a boundary or unusual condition) "
                     "related to the following:\n"},
                    {"balanced_class_sample",
                     "Write a new sample belonging to the minority class "
                     "implied by the following, to help balance a dataset:\n"},
                    {"code_sample",
                     "Write a short code sample consistent with the "
                     "following description:\n"},
                    {"unit_test_case",
                     "Write a unit test case (input and expected output) for "
                     "the following:\n"},
                    {"simulated_conversation",
                     "Write a short simulated multi-turn conversation "
                     "consistent with the following:\n"},
                    {"image_variation",
                     "Write a text description of a plausible visual "
                     "variation of the following:\n"},
                    {"tabular_record",
                     "Write one new tabular data record (as comma-separated "
                     "values) consistent with the following schema/example:\n"},
                };
                std::string prompt;
                const auto template_it = technique_templates.find(technique);
                if (template_it != technique_templates.end()) {
                    prompt = template_it->second + source_text;
                } else {
                    prompt = "Generate a " +
                             (technique.empty() ? std::string("synthetic record")
                                                 : technique) +
                             " based on the following:\n" + source_text;
                }
                const auto generated = execute_rag_generation(model_id, prompt);
                auto name = text_field("name");
                if (name.empty()) name = "generated-" + (technique.empty() ? "synthetic-record" : technique);
                const auto record = ml_synthetic_records->create(
                    user->id, dataset_id, name, text_field("description"),
                    technique);
                SyntheticRecordContent content;
                content.generator_model = model_id;
                if (const auto entry = ml_models->find(model_id)) {
                    content.generator_version = entry->version;
                }
                content.prompt = prompt;
                content.generation_settings = technique;
                content.generated_text = generated.text;
                content.confidence_score =
                    (!generated.cancelled && !generated.text.empty()) ? 1.0 : 0.0;
                content.source_record_id = text_field("sourceRecordId");
                ml_synthetic_record_content->put(record.id, content);
                audit.append("ml.synthetic_record.generate", user->id,
                             "success", record.id);
                return response(
                    201, "Created",
                    "{\"record\":" + synthetic_record_json(record) +
                        ",\"content\":" +
                        synthetic_record_content_json(content) + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_synthetic_record_generate\","
                    "\"detail\":\"" + json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/synthetic-records/", 0U) ==
                0U &&
            request.target.size() > 8U &&
            request.target.compare(request.target.size() - 8U, 8U,
                                   "/content") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.syntheticdata.view")) return *denied;
            const auto id = request.target.substr(
                30U, request.target.size() - 30U - 8U);
            const auto content = ml_synthetic_record_content->find(id);
            if (!content) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_synthetic_record_content_not_found\"}");
            }
            return response(200, "OK",
                            "{\"content\":" +
                                synthetic_record_content_json(*content) + "}");
        }
        // Phase 49: Embeddings and Vector Stores (docs/PLAN.md "Machine
        // Learning Abilities" section 21), scoped to identity/embedding-
        // model/distance-metric/status fields -- see VectorStoreStore's
        // class comment in masterai.hpp for the document-import/chunking/
        // indexing fields deferred to the phase that actually generates
        // embeddings.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/vector-stores") {
            if (auto denied = forbidden_unless(user->role, "ml.vectorstores.view")) return *denied;
            return response(200, "OK",
                            "{\"vectorStores\":" +
                                vector_stores_json(ml_vector_stores->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/vector-stores") {
            if (auto denied = forbidden_unless(user->role, "ml.vectorstores.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto store = ml_vector_stores->create(
                    user->id, name, text_field("description"),
                    text_field("embeddingModel"), text_field("distanceMetric"));
                audit.append("ml.vector_store.create", user->id, "success",
                             store.id);
                return response(201, "Created", vector_store_json(store));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_vector_store\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/vector-stores/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.vectorstores.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_vector_store_status(
                    root.required("status").as_string());
                if (!ml_vector_stores->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_vector_store_not_found\"}");
                }
                audit.append("ml.vector_store.status", user->id, "success",
                             id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_vector_store_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/vector-stores/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.vectorstores.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            if (!ml_knowledge_index->chunks_for_store(id).empty()) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_vector_store_has_indexed_documents\","
                    "\"detail\":\"delete the vector store's ingested "
                    "knowledge files first\"}");
            }
            if (!ml_vector_stores->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_vector_store_not_found\"}");
            }
            audit.append("ml.vector_store.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 50: Retrieval-Augmented Generation (docs/PLAN.md "Machine
        // Learning Abilities" section 22), scoped to identity/search-
        // strategy/vector-store-reference/status fields -- see
        // RagConfigStore's class comment in masterai.hpp for the
        // retrieval-testing/reranking/citation fields deferred to the
        // phase that actually generates retrieval results.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/rag-configs") {
            if (auto denied = forbidden_unless(user->role, "ml.ragconfigs.view")) return *denied;
            return response(200, "OK",
                            "{\"ragConfigs\":" +
                                rag_configs_json(ml_rag_configs->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/rag-configs") {
            if (auto denied = forbidden_unless(user->role, "ml.ragconfigs.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto config = ml_rag_configs->create(
                    user->id, name, text_field("description"),
                    text_field("searchStrategy"), text_field("vectorStoreId"));
                audit.append("ml.rag_config.create", user->id, "success",
                             config.id);
                return response(201, "Created", rag_config_json(config));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_rag_config\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/rag-configs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.ragconfigs.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_rag_config_status(
                    root.required("status").as_string());
                if (!ml_rag_configs->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_rag_config_not_found\"}");
                }
                audit.append("ml.rag_config.status", user->id, "success",
                             id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_rag_config_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/rag-configs/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.ragconfigs.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            if (!ml_rag_configs->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_rag_config_not_found\"}");
            }
            audit.append("ml.rag_config.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phases 58-59: real knowledge-file ingestion and durable vector
        // indexing. Browser file bytes arrive as bounded JSON text; the
        // server validates both referenced records before hashing/chunking.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/knowledge-documents") {
            if (auto denied = forbidden_unless(user->role, "ml.knowledge.view")) return *denied;
            return response(
                200, "OK", "{\"knowledgeDocuments\":" +
                    knowledge_documents_json(
                        ml_knowledge_index->list_documents()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/knowledge-documents") {
            if (auto denied = forbidden_unless(user->role, "ml.knowledge.manage")) return *denied;
            try {
                // A whole knowledge document travels as a JSON string field
                // (base64 for Parquet), so it can legitimately exceed the
                // 1 MiB default every other JSON body is capped at.
                // configuration.max_request_bytes already bounds the raw
                // HTTP body at parse_request() time (server.cpp), so
                // reusing it here removes the redundant, smaller, hardcoded
                // ceiling without introducing a second setting to keep in
                // sync with it.
                auto root =
                    parse_json(request.body, configuration.max_request_bytes);
                const auto subject_id = root.required("subjectId").as_string();
                const auto vector_store_id =
                    root.required("vectorStoreId").as_string();
                if (!ml_subjects->find(subject_id)) {
                    return response(409, "Conflict",
                                    "{\"error\":\"ml_subject_not_found\"}");
                }
                const auto vector_store =
                    ml_vector_stores->find(vector_store_id);
                if (!vector_store) {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_vector_store_not_found\"}");
                }
                if (vector_store->distance_metric != "cosine") {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_vector_store_executor_unsupported\","
                        "\"detail\":\"the local executor requires cosine distance\"}");
                }
                const auto vectorize = knowledge_vectorizer(
                    vector_store->embedding_model);
                const auto file_name = root.required("fileName").as_string();
                const auto media_type = root.required("mediaType").as_string();
                auto content = root.required("content").as_string();
                // Parquet is binary and cannot travel as raw JSON text like
                // the other supported media types, so the browser sends it
                // base64-encoded; every other media type is used as-is.
                if (is_parquet_knowledge_upload(media_type, file_name)) {
                    content = base64_decode(content);
                }
                const auto document = ml_knowledge_index->ingest(
                    user->id, subject_id, vector_store_id, file_name, media_type,
                    content, vector_store->embedding_model, vectorize);
                audit.append("ml.knowledge.ingest", user->id, "success",
                             document.id);
                return response(201, "Created",
                                knowledge_document_json(document));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_knowledge_document\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/knowledge-documents/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.knowledge.manage")) return *denied;
            const std::string prefix = "/api/v1/ml/knowledge-documents/";
            const auto id = request.target.substr(
                prefix.size(), request.target.size() - prefix.size() - 7U);
            if (!ml_knowledge_index->remove_document(id)) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_knowledge_document_not_found\"}");
            }
            audit.append("ml.knowledge.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/vector-stores/", 0U) == 0U &&
            request.target.size() > 6U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/index") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.vectorstores.view")) return *denied;
            const std::string prefix = "/api/v1/ml/vector-stores/";
            const auto id = request.target.substr(
                prefix.size(), request.target.size() - prefix.size() - 6U);
            if (!ml_vector_stores->find(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_vector_store_not_found\"}");
            }
            return response(200, "OK", knowledge_index_profile_json(
                id, ml_knowledge_index->chunks_for_store(id)));
        }
        // Phase 60: execute one approved RAG configuration against its real
        // populated index and return ranked chunks plus citation-ready
        // context. Phase 76 adds an optional real generated answer
        // (request field "generate":true plus "modelId") via
        // execute_rag_generation, grounded in this same retrieved context
        // -- never fabricated. Without "generate", the response is
        // unchanged from Phase 60: context only, no generated-model
        // answer.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/rag-configs/", 0U) == 0U &&
            request.target.size() > 6U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/query") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.ragconfigs.manage")) return *denied;
            const std::string prefix = "/api/v1/ml/rag-configs/";
            const auto id = request.target.substr(
                prefix.size(), request.target.size() - prefix.size() - 6U);
            const auto config = ml_rag_configs->find(id);
            if (!config) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_rag_config_not_found\"}");
            }
            if (config->status != RagConfigStatus::approved) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_rag_config_not_approved\",\"detail\":"
                    "\"approve the RAG configuration before querying it\"}");
            }
            const auto vector_store =
                ml_vector_stores->find(config->vector_store_id);
            if (!vector_store || vector_store->status != VectorStoreStatus::approved) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_vector_store_not_approved\",\"detail\":"
                    "\"the RAG configuration needs an approved vector store\"}");
            }
            if (vector_store->distance_metric != "cosine") {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_vector_store_executor_unsupported\","
                    "\"detail\":\"the local executor requires cosine distance\"}");
            }
            try {
                auto root = parse_json(request.body);
                std::size_t top_k = 5U;
                if (const auto* value = root.optional("topK")) {
                    const auto requested = value->as_integer();
                    if (requested < 0) {
                        throw std::invalid_argument("topK cannot be negative");
                    }
                    top_k = static_cast<std::size_t>(requested);
                }
                const auto vectorize = knowledge_vectorizer(
                    vector_store->embedding_model);
                const auto query_text = root.required("query").as_string();
                const auto result = retrieve_knowledge(
                    *ml_knowledge_index, config->vector_store_id,
                    config->search_strategy, query_text, top_k,
                    vector_store->embedding_model, vectorize);
                const auto result_json = rag_retrieval_result_json(result);
                audit.append("ml.rag.query", user->id, "success", id);
                // Phase 76: optional real answer generation, grounded in
                // the same retrieved context above -- reuses the exact
                // inference path (execute_rag_generation ->
                // assemble_chat_prompt -> inference->generate) the chat
                // handler uses, so this never duplicates or diverges from
                // it. Default (no "generate" field, or false) keeps the
                // pre-Phase-76 context-only response unchanged for every
                // existing caller.
                const auto* generate_field = root.optional("generate");
                if (generate_field != nullptr && generate_field->as_boolean()) {
                    const auto* model_id_field = root.optional("modelId");
                    if (model_id_field == nullptr) {
                        return response(
                            400, "Bad Request",
                            "{\"error\":\"ml_rag_generate_requires_model\","
                            "\"detail\":\"set modelId to the id of a ready "
                            "model to generate a real answer\"}");
                    }
                    const auto model_id = model_id_field->as_string();
                    try {
                        // Rebuild the same citation-prefixed context text
                        // rag_retrieval_result_json() reports as "context",
                        // since RagRetrievalResult itself only holds the
                        // ranked chunks, not a pre-joined string.
                        std::string generation_prompt;
                        for (const auto& item : result.chunks) {
                            const std::string citation =
                                "[" + item.chunk.file_name + "#chunk-" +
                                std::to_string(item.chunk.chunk_index + 1U) +
                                "]";
                            if (!generation_prompt.empty()) {
                                generation_prompt += "\n\n";
                            }
                            generation_prompt += citation + "\n" + item.chunk.text;
                        }
                        generation_prompt += "\n\n[Question]\n" + query_text;
                        const auto generated =
                            execute_rag_generation(model_id, generation_prompt);
                        audit.append("ml.rag.generate", user->id, "success", id);
                        return response(
                            200, "OK",
                            "{\"ragConfigId\":\"" + json_escape(id) +
                                "\",\"result\":" + result_json +
                                ",\"answer\":{\"content\":\"" +
                                json_escape(generated.text) +
                                "\",\"modelId\":\"" + json_escape(model_id) +
                                "\",\"promptTokens\":" +
                                std::to_string(generated.prompt_tokens) +
                                ",\"generatedTokens\":" +
                                std::to_string(generated.generated_tokens) +
                                ",\"elapsedMicroseconds\":" +
                                std::to_string(generated.elapsed_microseconds) +
                                "}}");
                    } catch (const std::exception& error) {
                        audit.append("ml.rag.generate", user->id, "failure", id);
                        return response(
                            409, "Conflict",
                            "{\"error\":\"ml_rag_generate_failed\",\"detail\":\"" +
                                json_escape(error.what()) + "\"}");
                    }
                }
                return response(200, "OK",
                                "{\"ragConfigId\":\"" + json_escape(id) +
                                    "\",\"result\":" + result_json + "}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"ml_rag_query_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 51: Subject Examination System (docs/PLAN.md "Machine
        // Learning Abilities" section 24), scoped to identity/subject-
        // reference/question-format/status fields -- see SubjectExamStore's
        // class comment in masterai.hpp for the question-bank/scoring
        // fields deferred to the phase that actually administers exams.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/subject-exams") {
            if (auto denied = forbidden_unless(user->role, "ml.subjectexams.view")) return *denied;
            return response(200, "OK",
                            "{\"subjectExams\":" +
                                subject_exams_json(ml_subject_exams->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/subject-exams") {
            if (auto denied = forbidden_unless(user->role, "ml.subjectexams.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto subject_id = root.required("subjectId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto exam = ml_subject_exams->create(
                    user->id, subject_id, name, text_field("description"),
                    text_field("questionFormat"));
                audit.append("ml.subject_exam.create", user->id, "success",
                             exam.id);
                return response(201, "Created", subject_exam_json(exam));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_subject_exam\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/subject-exams/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.subjectexams.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_subject_exam_status(
                    root.required("status").as_string());
                if (!ml_subject_exams->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_subject_exam_not_found\"}");
                }
                audit.append("ml.subject_exam.status", user->id, "success",
                             id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_subject_exam_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/subject-exams/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.subjectexams.manage")) return *denied;
            const auto id = request.target.substr(
                25U, request.target.size() - 25U - 7U);
            if (!ml_subject_exams->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_subject_exam_not_found\"}");
            }
            audit.append("ml.subject_exam.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 52: Hyperparameter Optimization (docs/PLAN.md "Machine
        // Learning Abilities" section 26), scoped to identity/training-job-
        // reference/strategy/status fields -- see HyperparameterSearchStore's
        // class comment in masterai.hpp for the search-space/trial-history
        // fields deferred to the phase that actually runs searches.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/hyperparameter-searches") {
            if (auto denied = forbidden_unless(user->role, "ml.hyperparams.view")) return *denied;
            return response(
                200, "OK",
                "{\"hyperparameterSearches\":" +
                    hyperparameter_searches_json(
                        ml_hyperparameter_searches->list()) +
                    "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/hyperparameter-searches") {
            if (auto denied = forbidden_unless(user->role, "ml.hyperparams.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto training_job_id =
                    root.required("trainingJobId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto search = ml_hyperparameter_searches->create(
                    user->id, training_job_id, name,
                    text_field("description"), text_field("strategy"));
                audit.append("ml.hyperparameter_search.create", user->id,
                             "success", search.id);
                return response(201, "Created",
                                hyperparameter_search_json(search));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_hyperparameter_search\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/hyperparameter-searches/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.hyperparams.manage")) return *denied;
            const auto id = request.target.substr(
                35U, request.target.size() - 35U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_hyperparameter_search_status(
                    root.required("status").as_string());
                if (!ml_hyperparameter_searches->set_status(id, status)) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"ml_hyperparameter_search_not_found\"}");
                }
                audit.append("ml.hyperparameter_search.status", user->id,
                             "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_hyperparameter_search_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/hyperparameter-searches/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.hyperparams.manage")) return *denied;
            const auto id = request.target.substr(
                35U, request.target.size() - 35U - 7U);
            if (!ml_hyperparameter_searches->remove(id)) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_hyperparameter_search_not_found\"}");
            }
            audit.append("ml.hyperparameter_search.delete", user->id,
                         "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 53: Model Optimization (docs/PLAN.md "Machine Learning
        // Abilities" section 28), scoped to identity/model-reference/
        // operation/status fields -- see ModelOptimizationStore's class
        // comment in masterai.hpp for the before/after-comparison fields
        // deferred to the phase that actually optimizes models.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/model-optimizations") {
            if (auto denied = forbidden_unless(user->role, "ml.modelopts.view")) return *denied;
            return response(200, "OK",
                            "{\"modelOptimizations\":" +
                                model_optimizations_json(
                                    ml_model_optimizations->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/model-optimizations") {
            if (auto denied = forbidden_unless(user->role, "ml.modelopts.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto run = ml_model_optimizations->create(
                    user->id, model_id, name, text_field("description"),
                    text_field("operation"));
                audit.append("ml.model_optimization.create", user->id,
                             "success", run.id);
                return response(201, "Created", model_optimization_json(run));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_optimization\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-optimizations/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.modelopts.manage")) return *denied;
            const auto id = request.target.substr(
                31U, request.target.size() - 31U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_model_optimization_status(
                    root.required("status").as_string());
                if (!ml_model_optimizations->set_status(id, status)) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"ml_model_optimization_not_found\"}");
                }
                audit.append("ml.model_optimization.status", user->id,
                             "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_optimization_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-optimizations/", 0U) ==
                0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.modelopts.manage")) return *denied;
            const auto id = request.target.substr(
                31U, request.target.size() - 31U - 7U);
            if (!ml_model_optimizations->remove(id)) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_model_optimization_not_found\"}");
            }
            audit.append("ml.model_optimization.delete", user->id, "success",
                         id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 54: Checkpoint Management (docs/PLAN.md "Machine Learning
        // Abilities" section 33), scoped to identity/training-job-reference/
        // capture-reason/retention-status fields. Phase 79 closed the
        // remaining step/epoch/resume gap: execute_training_job and
        // execute_fine_tuning_job now capture a real weight snapshot at each
        // checkpoint (see checkpoint_snapshot()/CheckpointModelStore), and
        // the POST .../resume route below continues training from one.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/checkpoints") {
            if (auto denied = forbidden_unless(user->role, "ml.checkpoints.view")) return *denied;
            return response(200, "OK",
                            "{\"checkpoints\":" +
                                training_checkpoints_json(
                                    ml_training_checkpoints->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/checkpoints") {
            if (auto denied = forbidden_unless(user->role, "ml.checkpoints.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto training_job_id =
                    root.required("trainingJobId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto checkpoint = ml_training_checkpoints->create(
                    user->id, training_job_id, name,
                    text_field("description"), text_field("captureReason"));
                audit.append("ml.checkpoint.create", user->id, "success",
                             checkpoint.id);
                return response(201, "Created",
                                training_checkpoint_json(checkpoint));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_checkpoint\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/checkpoints/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.checkpoints.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_training_checkpoint_status(
                    root.required("status").as_string());
                if (!ml_training_checkpoints->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_checkpoint_not_found\"}");
                }
                audit.append("ml.checkpoint.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_checkpoint_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/checkpoints/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.checkpoints.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            if (!ml_training_checkpoints->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_checkpoint_not_found\"}");
            }
            audit.append("ml.checkpoint.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 79: resumes real gradient descent from a genuinely captured
        // checkpoint snapshot -- mirrors the training-jobs .../run route's
        // request/response shape, gated on the same ml.training.manage
        // permission since this performs a real training operation.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/checkpoints/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/resume") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.training.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            const auto checkpoint = ml_training_checkpoints->find(id);
            if (!checkpoint) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_checkpoint_not_found\"}");
            }
            if (!checkpoint->has_snapshot) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_checkpoint_has_no_snapshot\",\"detail\":"
                    "\"this checkpoint has no captured weight snapshot to "
                    "resume from\"}");
            }
            const auto job = ml_training_jobs->find(checkpoint->training_job_id);
            if (!job) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_checkpoint_training_job_not_found\"}");
            }
            const auto content = ml_dataset_content->find(job->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the job's dataset first\"}");
            }
            TabularTrainingOptions options;
            try {
                if (!request.body.empty()) {
                    auto root = parse_json(request.body);
                    const auto integer_field = [&root](const char* field,
                                                       const std::uint32_t fallback) {
                        const auto* value = root.optional(field);
                        return value ? static_cast<std::uint32_t>(value->as_integer())
                                     : fallback;
                    };
                    options.epochs = integer_field("epochs", options.epochs);
                    options.seed = integer_field("seed", options.seed);
                    options.checkpoint_interval = integer_field(
                        "checkpointInterval", options.checkpoint_interval);
                    if (const auto* rate = root.optional("learningRate")) {
                        options.learning_rate = rate->as_double();
                    }
                    if (const auto* fraction = root.optional("testFraction")) {
                        options.test_fraction = fraction->as_double();
                    }
                }
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_training_options\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
            try {
                const auto result = execute_checkpoint_resume(
                    *checkpoint, *job, *content, options, user->id);
                audit.append("ml.checkpoint.resume", user->id, "success", id);
                return response(200, "OK",
                                tabular_training_report_json(result.report,
                                                             result.model));
            } catch (const std::exception& error) {
                audit.append("ml.checkpoint.resume", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_resume_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        // Phase 55: Deployment Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 34), scoped to identity/model-reference/
        // environment/strategy/status fields -- see DeploymentStore's class
        // comment in masterai.hpp. The Deployment Manager/Inference
        // Endpoints/Synthetic Data completion phase adds the real .../deploy
        // and .../rollback actions below, giving this module its own
        // deploy/health/rollback path instead of relying solely on
        // Automation Pipelines' stage executor.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/deployments") {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.view")) return *denied;
            return response(200, "OK",
                            "{\"deployments\":" +
                                deployments_json(ml_deployments->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/deployments") {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto model_id = root.required("modelId").as_string();
                const auto text_field = [&root](const char* field) {
                    const auto* value = root.optional(field);
                    return value ? value->as_string() : std::string{};
                };
                const auto deployment = ml_deployments->create(
                    user->id, model_id, name, text_field("description"),
                    text_field("environment"), text_field("strategy"));
                audit.append("ml.deployment.create", user->id, "success",
                             deployment.id);
                return response(201, "Created", deployment_json(deployment));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_deployment\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/deployments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_deployment_status(
                    root.required("status").as_string());
                if (!ml_deployments->set_status(id, status)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_deployment_not_found\"}");
                }
                audit.append("ml.deployment.status", user->id, "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_deployment_status\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/deployments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/deploy") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            const auto deployment = ml_deployments->find(id);
            if (!deployment) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_deployment_not_found\"}");
            }
            // Same approved-ModelCard gate AutomationPipeline's
            // run_safety_tests_stage enforces below, so a deployment
            // approved through this module's own API is held to the
            // identical bar as one approved through a pipeline run.
            bool has_approved_card = false;
            for (const auto& card : ml_safety_governance->list_model_cards()) {
                if (card.model_id == deployment->model_id &&
                    card.status == SafetyPolicyStatus::approved) {
                    has_approved_card = true;
                    break;
                }
            }
            if (!has_approved_card) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_deployment_no_approved_model_card\","
                    "\"detail\":\"no approved model card exists for model " +
                        json_escape(deployment->model_id) +
                        " (create and approve one in Safety and "
                        "Governance)\"}");
            }
            const bool trained_weights_present =
                static_cast<bool>(ml_trained_models->find(deployment->model_id));
            if (!ml_deployments->deploy(id, true, trained_weights_present)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_deployment_not_found\"}");
            }
            audit.append("ml.deployment.deploy", user->id, "success", id);
            return response(200, "OK",
                            "{\"deployment\":" +
                                deployment_json(*ml_deployments->find(id)) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/deployments/", 0U) == 0U &&
            request.target.size() > 9U &&
            request.target.compare(request.target.size() - 9U, 9U,
                                   "/rollback") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 9U);
            const auto restored_id = ml_deployments->rollback(id);
            if (restored_id.empty()) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"ml_deployment_rollback_unavailable\","
                    "\"detail\":\"deployment " + json_escape(id) +
                        " does not exist or has no previous deployment to "
                        "roll back to\"}");
            }
            audit.append("ml.deployment.rollback", user->id, "success", id);
            return response(
                200, "OK",
                "{\"activeDeploymentId\":\"" + json_escape(restored_id) +
                    "\"}");
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/deployments/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.deployments.manage")) return *denied;
            const auto id = request.target.substr(
                23U, request.target.size() - 23U - 7U);
            if (!ml_deployments->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_deployment_not_found\"}");
            }
            audit.append("ml.deployment.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 62: Inference Endpoints (docs/PLAN.md "Machine Learning
        // Abilities" section 35) -- see InferenceEndpoint's class comment
        // in masterai.hpp for the fields this scoped-down registry defers.
        {
            static constexpr std::string_view endpoints_prefix =
                "/api/v1/ml/inference-endpoints/";
            if (request.method == "GET" &&
                request.target == "/api/v1/ml/inference-endpoints") {
                if (auto denied = forbidden_unless(user->role, "ml.endpoints.view")) return *denied;
                return response(200, "OK",
                                "{\"inferenceEndpoints\":" +
                                    inference_endpoints_json(ml_inference_endpoints->list()) +
                                    "}");
            }
            if (request.method == "POST" &&
                request.target == "/api/v1/ml/inference-endpoints") {
                if (auto denied = forbidden_unless(user->role, "ml.endpoints.manage")) return *denied;
                try {
                    auto root = parse_json(request.body);
                    const auto name = root.required("name").as_string();
                    const auto model_id = root.required("modelId").as_string();
                    const auto text_field = [&root](const char* field) {
                        const auto* value = root.optional(field);
                        return value ? value->as_string() : std::string{};
                    };
                    const auto* port_value = root.optional("port");
                    const auto* rate_limit_value =
                        root.optional("rateLimitPerMinute");
                    const auto auth_token = text_field("authToken");
                    const auto endpoint = ml_inference_endpoints->create(
                        user->id, name, model_id, text_field("runtime"),
                        text_field("host"),
                        static_cast<std::uint16_t>(
                            port_value ? port_value->as_integer() : 0),
                        text_field("protocol"),
                        text_field("authenticationMethod"),
                        static_cast<std::uint32_t>(
                            rate_limit_value ? rate_limit_value->as_integer() : 0),
                        auth_token);
                    // Endpoint only ever persists the token's hash; the
                    // plaintext this route just received is the only copy.
                    if (!auth_token.empty()) {
                        secrets->set("inference-endpoint:" + endpoint.id, auth_token);
                    }
                    audit.append("ml.endpoint.create", user->id, "success",
                                 endpoint.id);
                    return response(201, "Created", inference_endpoint_json(endpoint));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_endpoint\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(endpoints_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/status") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.endpoints.manage")) return *denied;
                const auto id = request.target.substr(
                    endpoints_prefix.size(),
                    request.target.size() - endpoints_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto status = parse_inference_endpoint_status(
                        root.required("status").as_string());
                    if (!ml_inference_endpoints->set_status(id, status)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_endpoint_not_found\"}");
                    }
                    // Phase 77: a real listener thread's lifecycle now
                    // follows the status transition, not just the database
                    // row -- `active` starts it (or restarts it, so an
                    // authToken/rateLimit change on an already-active
                    // endpoint takes effect without a manual disable/
                    // re-enable), anything else stops it.
                    {
                        std::lock_guard<std::mutex> lock(
                            inference_endpoint_threads_mutex);
                        const auto existing = inference_endpoint_threads.find(id);
                        if (existing != inference_endpoint_threads.end()) {
                            existing->second.second->store(true);
                            existing->second.first.join();
                            inference_endpoint_threads.erase(existing);
                        }
                        if (status == InferenceEndpointStatus::active) {
                            const auto endpoint = ml_inference_endpoints->find(id);
                            const auto secret =
                                secrets->get("inference-endpoint:" + id);
                            std::string expected_authorization;
                            if (endpoint->authentication_method != "none" &&
                                !endpoint->authentication_method.empty() &&
                                secret) {
                                expected_authorization = "Bearer " + *secret;
                            }
                            auto stop_flag = std::make_shared<std::atomic_bool>(false);
                            std::thread worker(
                                [this, endpoint = *endpoint, expected_authorization,
                                stop_flag]() {
                                    run_inference_endpoint(
                                        endpoint, expected_authorization, stop_flag);
                                });
                            inference_endpoint_threads.emplace(
                                id, std::make_pair(std::move(worker), stop_flag));
                        }
                    }
                    audit.append("ml.endpoint.status", user->id, "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_endpoint_status\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            // Phase 77 (this pass): per-endpoint tool/safety policy
            // configuration -- see InferenceEndpoint's policy fields'
            // comment in masterai.hpp. Distinct from /status above: this
            // never touches the listener thread's lifecycle, only the
            // policy `run_inference_endpoint` reads at the top of each
            // request loop (so a live endpoint picks up a policy change on
            // its very next request, no restart required).
            if (request.method == "POST" &&
                request.target.rfind(endpoints_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/policy") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.endpoints.manage")) return *denied;
                const auto id = request.target.substr(
                    endpoints_prefix.size(),
                    request.target.size() - endpoints_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto bool_field = [&root](const char* field, bool fallback) {
                        const auto* value = root.optional(field);
                        return value ? value->as_boolean() : fallback;
                    };
                    const auto* safety_policy_id_value = root.optional("safetyPolicyId");
                    const auto* confidence_value =
                        root.optional("modelClassifierConfidenceFloor");
                    const auto content_scan_enabled =
                        bool_field("contentScanEnabled", true);
                    const auto block_on_scan_finding =
                        bool_field("blockOnScanFinding", true);
                    const auto block_answer_on_scan_finding =
                        bool_field("blockAnswerOnScanFinding", false);
                    const auto safety_policy_id =
                        safety_policy_id_value ? safety_policy_id_value->as_string()
                                               : std::string{};
                    const auto model_classifier_enabled =
                        bool_field("modelClassifierEnabled", false);
                    const auto confidence_floor =
                        confidence_value ? confidence_value->as_double() : 0.5;
                    if (!safety_policy_id.empty() &&
                        !ml_safety_governance->find_policy(safety_policy_id)) {
                        return response(400, "Bad Request",
                                        "{\"error\":\"safety_policy_not_found\"}");
                    }
                    if (!ml_inference_endpoints->set_policy(
                            id, content_scan_enabled, block_on_scan_finding,
                            block_answer_on_scan_finding, safety_policy_id,
                            model_classifier_enabled, confidence_floor)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_endpoint_not_found\"}");
                    }
                    audit.append("ml.endpoint.policy", user->id, "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_endpoint_policy\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(endpoints_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/delete") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.endpoints.manage")) return *denied;
                const auto id = request.target.substr(
                    endpoints_prefix.size(),
                    request.target.size() - endpoints_prefix.size() - 7U);
                if (!ml_inference_endpoints->remove(id)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_endpoint_not_found\"}");
                }
                {
                    std::lock_guard<std::mutex> lock(inference_endpoint_threads_mutex);
                    const auto existing = inference_endpoint_threads.find(id);
                    if (existing != inference_endpoint_threads.end()) {
                        existing->second.second->store(true);
                        existing->second.first.join();
                        inference_endpoint_threads.erase(existing);
                    }
                }
                secrets->erase("inference-endpoint:" + id);
                audit.append("ml.endpoint.delete", user->id, "success", id);
                return response(200, "OK", "{\"deleted\":true}");
            }
        }
        // Phase 63: Hardware and Compute (docs/PLAN.md "Machine Learning
        // Abilities" section 30) -- see ComputeNode's class comment in
        // masterai.hpp for the live-telemetry fields this scoped-down
        // registry defers.
        {
            static constexpr std::string_view nodes_prefix =
                "/api/v1/ml/compute-nodes/";
            if (request.method == "GET" &&
                request.target == "/api/v1/ml/compute-nodes") {
                if (auto denied = forbidden_unless(user->role, "ml.hardware.view")) return *denied;
                return response(200, "OK",
                                "{\"computeNodes\":" +
                                    compute_nodes_json(ml_compute_nodes->list()) + "}");
            }
            if (request.method == "POST" &&
                request.target == "/api/v1/ml/compute-nodes") {
                if (auto denied = forbidden_unless(user->role, "ml.hardware.manage")) return *denied;
                try {
                    auto root = parse_json(request.body);
                    const auto name = root.required("name").as_string();
                    const auto text_field = [&root](const char* field) {
                        const auto* value = root.optional(field);
                        return value ? value->as_string() : std::string{};
                    };
                    const auto* memory_value = root.optional("memoryMib");
                    const auto* is_local_value = root.optional("isLocal");
                    const auto node = ml_compute_nodes->create(
                        user->id, name, text_field("address"),
                        text_field("operatingSystem"),
                        text_field("cpuDescription"),
                        text_field("gpuDescription"),
                        memory_value ? static_cast<std::uint64_t>(
                                           memory_value->as_integer())
                                    : 0ULL,
                        is_local_value ? is_local_value->as_boolean() : false,
                        // Phase 75: a non-local node names a real
                        // `masterai telemetry-agent` process's address and
                        // shared secret so .../telemetry can poll it for
                        // real, instead of remaining permanently 400.
                        text_field("agentUrl"), text_field("agentSharedSecret"));
                    // ComputeNode only ever persists the secret's hash; the
                    // plaintext this route just received is the only copy,
                    // so it goes into the encrypted-at-rest SecretStore
                    // here, keyed by the node's new id, for
                    // .../telemetry to retrieve later.
                    const auto agent_secret = text_field("agentSharedSecret");
                    if (!agent_secret.empty()) {
                        secrets->set("telemetry-agent:" + node.id, agent_secret);
                    }
                    audit.append("ml.compute_node.create", user->id, "success",
                                 node.id);
                    return response(201, "Created", compute_node_json(node));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_compute_node\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(nodes_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/status") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.hardware.manage")) return *denied;
                const auto id = request.target.substr(
                    nodes_prefix.size(),
                    request.target.size() - nodes_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto status = parse_compute_node_status(
                        root.required("status").as_string());
                    if (!ml_compute_nodes->set_status(id, status)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_compute_node_not_found\"}");
                    }
                    audit.append("ml.compute_node.status", user->id, "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_compute_node_status\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(nodes_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/delete") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.hardware.manage")) return *denied;
                const auto id = request.target.substr(
                    nodes_prefix.size(),
                    request.target.size() - nodes_prefix.size() - 7U);
                if (!ml_compute_nodes->remove(id)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_compute_node_not_found\"}");
                }
                secrets->erase("telemetry-agent:" + id);
                audit.append("ml.compute_node.delete", user->id, "success", id);
                return response(200, "OK", "{\"deleted\":true}");
            }
            // Phase 67: live telemetry for a node flagged `is_local` -- see
            // ComputeNode's class comment in masterai.hpp. Only the host
            // this MasterAI process is already running on can be probed
            // in-process; a remote node still has no agent to poll, so it
            // 400s instead of fabricating numbers.
            if (request.method == "GET" &&
                request.target.rfind(nodes_prefix, 0U) == 0U &&
                request.target.size() > 10U &&
                request.target.compare(request.target.size() - 10U, 10U,
                                       "/telemetry") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.hardware.view")) return *denied;
                const auto id = request.target.substr(
                    nodes_prefix.size(),
                    request.target.size() - nodes_prefix.size() - 10U);
                const auto node = ml_compute_nodes->find(id);
                if (!node) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_compute_node_not_found\"}");
                }
                const auto probed_at_epoch_seconds = std::to_string(
                    static_cast<std::uint64_t>(
                        std::chrono::duration_cast<std::chrono::seconds>(
                            std::chrono::system_clock::now().time_since_epoch())
                            .count()));
                if (node->is_local) {
                    const auto hardware =
                        probe_hardware(configuration.models_root);
                    return response(
                        200, "OK",
                        "{\"telemetry\":" + hardware_info_json(hardware) +
                            ",\"probedAtEpochSeconds\":" +
                            probed_at_epoch_seconds + "}");
                }
                // Phase 75: a non-local node with a configured agent_url is
                // polled for real over run_telemetry_agent's protocol --
                // `secrets` (the same encrypted-at-rest SecretStore
                // `ml.compute_node.create` above wrote to) holds the
                // plaintext, since ComputeNode itself only ever persists
                // its hash.
                if (node->agent_url.empty()) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"ml_compute_node_not_local\",\"detail\":"
                        "\"live telemetry requires a node flagged as the "
                        "local host, or one with an agentUrl configured\"}");
                }
                const auto secret = secrets->get("telemetry-agent:" + id);
                if (!secret) {
                    return response(
                        409, "Conflict",
                        "{\"error\":\"ml_compute_node_agent_secret_missing\","
                        "\"detail\":\"this node's shared secret was not "
                        "found; re-create the node with agentSharedSecret "
                        "set\"}");
                }
                try {
                    const auto telemetry_json =
                        fetch_remote_telemetry(node->agent_url, *secret);
                    return response(
                        200, "OK",
                        "{\"telemetry\":" + telemetry_json +
                            ",\"probedAtEpochSeconds\":" +
                            probed_at_epoch_seconds + "}");
                } catch (const std::exception& error) {
                    return response(
                        502, "Bad Gateway",
                        "{\"error\":\"ml_compute_node_agent_unreachable\","
                        "\"detail\":\"" + json_escape(error.what()) + "\"}");
                }
            }
        }
        // Phase 64/69: Automation Pipelines (docs/PLAN.md "Machine Learning
        // Abilities" section 37) -- see AutomationPipeline's class comment
        // in masterai.hpp for which stages POST .../run genuinely executes
        // and why the rest are honestly recorded as skipped.
        {
            static constexpr std::string_view pipelines_prefix =
                "/api/v1/ml/automation-pipelines/";
            if (request.method == "GET" &&
                request.target == "/api/v1/ml/automation-pipelines") {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.view")) return *denied;
                return response(200, "OK",
                                "{\"automationPipelines\":" +
                                    automation_pipelines_json(ml_automation_pipelines->list()) +
                                    "}");
            }
            if (request.method == "POST" &&
                request.target == "/api/v1/ml/automation-pipelines") {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.manage")) return *denied;
                try {
                    auto root = parse_json(request.body);
                    const auto name = root.required("name").as_string();
                    const auto text_field = [&root](const char* field) {
                        const auto* value = root.optional(field);
                        return value ? value->as_string() : std::string{};
                    };
                    const auto pipeline = ml_automation_pipelines->create(
                        user->id, name, text_field("projectId"),
                        text_field("description"), text_field("stages"),
                        text_field("datasetId"), text_field("modelId"));
                    audit.append("ml.pipeline.create", user->id, "success",
                                 pipeline.id);
                    return response(201, "Created", automation_pipeline_json(pipeline));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_pipeline\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(pipelines_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/status") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.manage")) return *denied;
                const auto id = request.target.substr(
                    pipelines_prefix.size(),
                    request.target.size() - pipelines_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto status = parse_automation_pipeline_status(
                        root.required("status").as_string());
                    if (!ml_automation_pipelines->set_status(id, status)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_pipeline_not_found\"}");
                    }
                    audit.append("ml.pipeline.status", user->id, "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_pipeline_status\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(pipelines_prefix, 0U) == 0U &&
                request.target.size() > 4U &&
                request.target.compare(request.target.size() - 4U, 4U,
                                       "/run") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.manage")) return *denied;
                const auto id = request.target.substr(
                    pipelines_prefix.size(),
                    request.target.size() - pipelines_prefix.size() - 4U);
                const auto pipeline = ml_automation_pipelines->find(id);
                if (!pipeline) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_pipeline_not_found\"}");
                }
                const auto stage_names = split_pipeline_stages(pipeline->stages);
                AutomationPipelineRun run;
                try {
                    run = ml_automation_pipelines->begin_run(
                        user->id, id,
                        static_cast<std::uint32_t>(stage_names.size()));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_pipeline_run\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
                // Phase 71: execution moves to a detached background thread
                // so this request returns immediately with the run's id and
                // a `running` status; run_automation_pipeline() below
                // updates the same run record as each stage finishes, so
                // GET .../runs (already polled by the web UI) reports real,
                // live progress instead of blocking until every stage ends.
                // pipeline/stage_names are copied by value into the thread
                // since `request`/`user` are request-thread-local and must
                // not be touched once this handler returns.
                const AutomationPipeline pipeline_copy = *pipeline;
                const std::string run_id = run.id;
                const std::string acting_user_id = user->id;
                std::thread(
                    [this, pipeline_copy, run_id, acting_user_id, stage_names]() {
                        try {
                            run_automation_pipeline(pipeline_copy, run_id,
                                                    acting_user_id, stage_names);
                        } catch (const std::exception& error) {
                            ml_automation_pipelines->finish_run(
                                run_id, AutomationPipelineRunStatus::failed,
                                std::string("pipeline run crashed: ") +
                                    error.what());
                        } catch (...) {
                            ml_automation_pipelines->finish_run(
                                run_id, AutomationPipelineRunStatus::failed,
                                "pipeline run crashed with an unknown exception");
                        }
                    })
                    .detach();
                audit.append("ml.pipeline.run", user->id, "started", id);
                return response(202, "Accepted", automation_pipeline_run_json(run));
            }
            if (request.method == "GET" &&
                request.target.rfind(pipelines_prefix, 0U) == 0U &&
                request.target.size() > 5U &&
                request.target.compare(request.target.size() - 5U, 5U,
                                       "/runs") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.view")) return *denied;
                const auto id = request.target.substr(
                    pipelines_prefix.size(),
                    request.target.size() - pipelines_prefix.size() - 5U);
                return response(200, "OK",
                                "{\"pipelineRuns\":" +
                                    automation_pipeline_runs_json(
                                        ml_automation_pipelines->runs_for(id)) +
                                    "}");
            }
            if (request.method == "POST" &&
                request.target.rfind(pipelines_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/delete") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.pipelines.manage")) return *denied;
                const auto id = request.target.substr(
                    pipelines_prefix.size(),
                    request.target.size() - pipelines_prefix.size() - 7U);
                if (!ml_automation_pipelines->remove(id)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_pipeline_not_found\"}");
                }
                audit.append("ml.pipeline.delete", user->id, "success", id);
                return response(200, "OK", "{\"deleted\":true}");
            }
        }
        // Phase 65: Safety and Governance (docs/PLAN.md "Machine Learning
        // Abilities" section 40) -- see SafetyPolicy/ModelCard's class
        // comment in masterai.hpp for the content-scanning fields this
        // scoped-down registry defers.
        {
            static constexpr std::string_view policies_prefix =
                "/api/v1/ml/safety-policies/";
            static constexpr std::string_view cards_prefix =
                "/api/v1/ml/model-cards/";
            if (request.method == "GET" &&
                request.target == "/api/v1/ml/safety-policies") {
                if (auto denied = forbidden_unless(user->role, "ml.safety.view")) return *denied;
                return response(200, "OK",
                                "{\"safetyPolicies\":" +
                                    safety_policies_json(ml_safety_governance->list_policies()) +
                                    "}");
            }
            if (request.method == "POST" &&
                request.target == "/api/v1/ml/safety-policies") {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                try {
                    auto root = parse_json(request.body);
                    const auto name = root.required("name").as_string();
                    const auto text_field = [&root](const char* field) {
                        const auto* value = root.optional(field);
                        return value ? value->as_string() : std::string{};
                    };
                    const auto policy = ml_safety_governance->create_policy(
                        user->id, name, text_field("scope"),
                        text_field("restrictedDataCategories"));
                    audit.append("ml.safety_policy.create", user->id,
                                 "success", policy.id);
                    return response(201, "Created", safety_policy_json(policy));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_safety_policy\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(policies_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/status") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                const auto id = request.target.substr(
                    policies_prefix.size(),
                    request.target.size() - policies_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto status = parse_safety_policy_status(
                        root.required("status").as_string());
                    if (!ml_safety_governance->set_policy_status(id, status)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_safety_policy_not_found\"}");
                    }
                    audit.append("ml.safety_policy.status", user->id,
                                 "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_safety_policy_status\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(policies_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/delete") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                const auto id = request.target.substr(
                    policies_prefix.size(),
                    request.target.size() - policies_prefix.size() - 7U);
                if (!ml_safety_governance->remove_policy(id)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_safety_policy_not_found\"}");
                }
                audit.append("ml.safety_policy.delete", user->id, "success", id);
                return response(200, "OK", "{\"deleted\":true}");
            }
            // Phase 74: real heuristic content scanning (see
            // scan_content_for_risks in masterai.hpp/ml_safety_scan.cpp) --
            // a read-only diagnostic, not a mutation, so it uses the same
            // ml.safety.view scope GET routes above use.
            if (request.method == "POST" &&
                request.target.rfind(policies_prefix, 0U) == 0U &&
                request.target.size() > 5U &&
                request.target.compare(request.target.size() - 5U, 5U,
                                       "/scan") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.safety.view")) return *denied;
                const auto id = request.target.substr(
                    policies_prefix.size(),
                    request.target.size() - policies_prefix.size() - 5U);
                const auto policy = ml_safety_governance->find_policy(id);
                if (!policy) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_safety_policy_not_found\"}");
                }
                try {
                    auto root = parse_json(request.body);
                    const auto text = root.required("text").as_string();
                    const auto report = scan_content_for_risks(text, &*policy);
                    std::string body = content_scan_report_json(report);
                    // Phase 74 (this pass): optional real ML-classifier pass
                    // (scan_content_with_model_classifier) layered on top of
                    // the heuristic report above, for bias/hallucination/
                    // subtler-harmful-content categories keyword matching
                    // structurally cannot catch. Opt-in via modelId because
                    // it costs a real generation call, unlike the
                    // always-on heuristic scan above.
                    const auto* model_id_value = root.optional("modelId");
                    if (model_id_value != nullptr) {
                        const auto model_id = model_id_value->as_string();
                        const auto classifier = scan_content_with_model_classifier(
                            text, [&](const std::string& prompt) {
                                return execute_rag_generation(model_id, prompt).text;
                            });
                        body.pop_back();  // drop closing '}'
                        body += ",\"modelClassifier\":" +
                               model_classifier_report_json(classifier) + "}";
                    }
                    return response(200, "OK", body);
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_safety_scan_request\","
                        "\"detail\":\"" + json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "GET" &&
                request.target == "/api/v1/ml/model-cards") {
                if (auto denied = forbidden_unless(user->role, "ml.safety.view")) return *denied;
                return response(200, "OK",
                                "{\"modelCards\":" +
                                    model_cards_json(ml_safety_governance->list_model_cards()) +
                                    "}");
            }
            if (request.method == "POST" &&
                request.target == "/api/v1/ml/model-cards") {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                try {
                    auto root = parse_json(request.body);
                    const auto model_id = root.required("modelId").as_string();
                    const auto purpose = root.required("purpose").as_string();
                    const auto text_field = [&root](const char* field) {
                        const auto* value = root.optional(field);
                        return value ? value->as_string() : std::string{};
                    };
                    const auto card = ml_safety_governance->create_model_card(
                        user->id, model_id, purpose,
                        text_field("intendedUse"), text_field("prohibitedUse"),
                        text_field("trainingDataReference"),
                        text_field("evaluationResults"),
                        text_field("knownLimitations"), text_field("license"));
                    audit.append("ml.model_card.create", user->id, "success",
                                 card.id);
                    return response(201, "Created", model_card_json(card));
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_model_card\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(cards_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/status") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                const auto id = request.target.substr(
                    cards_prefix.size(),
                    request.target.size() - cards_prefix.size() - 7U);
                try {
                    auto root = parse_json(request.body);
                    const auto status = parse_safety_policy_status(
                        root.required("status").as_string());
                    if (!ml_safety_governance->set_model_card_status(id, status)) {
                        return response(404, "Not Found",
                                        "{\"error\":\"ml_model_card_not_found\"}");
                    }
                    audit.append("ml.model_card.status", user->id, "success", id);
                    return response(200, "OK", "{\"updated\":true}");
                } catch (const std::exception& error) {
                    return response(
                        400, "Bad Request",
                        "{\"error\":\"invalid_ml_model_card_status\",\"detail\":\"" +
                            json_escape(error.what()) + "\"}");
                }
            }
            if (request.method == "POST" &&
                request.target.rfind(cards_prefix, 0U) == 0U &&
                request.target.size() > 7U &&
                request.target.compare(request.target.size() - 7U, 7U,
                                       "/delete") == 0) {
                if (auto denied = forbidden_unless(user->role, "ml.safety.manage")) return *denied;
                const auto id = request.target.substr(
                    cards_prefix.size(),
                    request.target.size() - cards_prefix.size() - 7U);
                if (!ml_safety_governance->remove_model_card(id)) {
                    return response(404, "Not Found",
                                    "{\"error\":\"ml_model_card_not_found\"}");
                }
                audit.append("ml.model_card.delete", user->id, "success", id);
                return response(200, "OK", "{\"deleted\":true}");
            }
        }
        // Phase 66: Audit Logs (docs/PLAN.md "Machine Learning Abilities"
        // section 43) -- a read-only surface over the AuditLog every ml.*
        // mutation above already writes to (see AuditLog::recent()'s class
        // comment in masterai.hpp). Scoped to "ml." events; the general
        // audit trail (login, chat, project actions, ...) is not an ML
        // administration concern.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/audit-logs") {
            if (auto denied = forbidden_unless(user->role, "ml.auditlogs.view")) return *denied;
            return response(200, "OK",
                            "{\"auditLogs\":" +
                                audit_log_entries_json(audit.recent(200U, "ml.")) +
                                "}");
        }
        // Phase 68/78: Monitoring and Diagnostics (docs/PLAN.md "Machine
        // Learning Abilities" section 44). A read-only aggregation over
        // data other real phases already measured -- no fabricated
        // numbers. Deliberately still does NOT report per-step gradient
        // norm/learning-rate curves (the tabular trainer has no iterative
        // training loop to sample), cache-hit rate (no KV-cache-hit
        // instrumentation exists), safety-filter rate, tool-call success,
        // retrieval latency, or temperature/network activity (no sensor
        // access this codebase has built) -- those all remain planned.
        // What is real: live local-host CPU/RAM/GPU/disk via the same
        // probe_hardware() Phase 67 uses, real training-job status counts,
        // each completed evaluation run's genuinely measured metrics
        // (Phase 56/57's EvaluationResultStore), real prompt/generation
        // throughput from actual BenchmarkStore runs, and -- as of Phase
        // 78 -- real live per-request latency percentiles/queue depth/
        // throughput (InferenceMetricsStore, fed by every real generation
        // call site: chat, RAG generation, inference endpoints).
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/monitoring") {
            if (auto denied = forbidden_unless(user->role, "ml.monitoring.view")) return *denied;
            return response(200, "OK", build_ml_monitoring_json());
        }
        // Phase 57: Model Comparison (docs/PLAN.md "Machine Learning
        // Abilities" section 27) -- a real executor like Phase 56's
        // training/evaluation endpoints. POST .../run evaluates both trained
        // artifacts against the comparison's shared benchmark dataset right
        // now and stores the genuine side-by-side result (both metric sets,
        // primary-metric delta, winner); GET .../result recalls it later.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/model-comparisons") {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.view")) return *denied;
            return response(
                200, "OK",
                "{\"modelComparisons\":" +
                    model_comparisons_json(ml_model_comparisons->list()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/model-comparisons") {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.manage")) return *denied;
            try {
                auto root = parse_json(request.body);
                const auto name = root.required("name").as_string();
                const auto baseline_model_id =
                    root.required("baselineModelId").as_string();
                const auto candidate_model_id =
                    root.required("candidateModelId").as_string();
                const auto dataset_id = root.required("datasetId").as_string();
                const auto* description = root.optional("description");
                const auto comparison = ml_model_comparisons->create(
                    user->id, baseline_model_id, candidate_model_id,
                    dataset_id, name,
                    description ? description->as_string() : std::string{});
                audit.append("ml.model_comparison.create", user->id,
                             "success", comparison.id);
                return response(201, "Created",
                                model_comparison_json(comparison));
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_comparison\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-comparisons/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/status") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.manage")) return *denied;
            const auto id = request.target.substr(
                29U, request.target.size() - 29U - 7U);
            try {
                auto root = parse_json(request.body);
                const auto status = parse_model_comparison_status(
                    root.required("status").as_string());
                if (!ml_model_comparisons->set_status(id, status)) {
                    return response(
                        404, "Not Found",
                        "{\"error\":\"ml_model_comparison_not_found\"}");
                }
                audit.append("ml.model_comparison.status", user->id,
                             "success", id);
                return response(200, "OK", "{\"updated\":true}");
            } catch (const std::exception& error) {
                return response(
                    400, "Bad Request",
                    "{\"error\":\"invalid_ml_model_comparison_status\","
                    "\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-comparisons/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.manage")) return *denied;
            const auto id = request.target.substr(
                29U, request.target.size() - 29U - 7U);
            if (!ml_model_comparisons->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_comparison_not_found\"}");
            }
            // An executed comparison's stored result dies with it, exactly
            // like an evaluation run's stored metrics.
            ml_comparison_results->remove(id);
            audit.append("ml.model_comparison.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/ml/model-comparisons/", 0U) == 0U &&
            request.target.size() > 4U &&
            request.target.compare(request.target.size() - 4U, 4U,
                                   "/run") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.manage")) return *denied;
            const auto id = request.target.substr(
                29U, request.target.size() - 29U - 4U);
            const auto comparison = ml_model_comparisons->find(id);
            if (!comparison) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_comparison_not_found\"}");
            }
            const auto baseline =
                ml_trained_models->find(comparison->baseline_model_id);
            const auto candidate =
                ml_trained_models->find(comparison->candidate_model_id);
            if (!baseline || !candidate) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_model_not_trained\",\"detail\":"
                    "\"both compared models need a trained artifact; run "
                    "their training jobs first\"}");
            }
            const auto content =
                ml_dataset_content->find(comparison->dataset_id);
            if (!content) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_dataset_has_no_content\",\"detail\":"
                    "\"upload CSV content to the comparison's benchmark "
                    "dataset first\"}");
            }
            ml_model_comparisons->set_status(id, ModelComparisonStatus::running);
            try {
                const auto data =
                    parse_tabular_csv(content->csv, content->target_column);
                const auto baseline_metrics =
                    evaluate_tabular_model(*baseline, data);
                const auto candidate_metrics =
                    evaluate_tabular_model(*candidate, data);
                const auto result_json = tabular_model_comparison_json(
                    *baseline, baseline_metrics, *candidate, candidate_metrics);
                ml_comparison_results->put(id, result_json);
                ml_model_comparisons->set_status(
                    id, ModelComparisonStatus::completed);
                audit.append("ml.model_comparison.run", user->id, "success",
                             id);
                return response(200, "OK",
                                "{\"comparisonId\":\"" + json_escape(id) +
                                    "\",\"result\":" + result_json + "}");
            } catch (const std::exception& error) {
                ml_model_comparisons->set_status(id,
                                                 ModelComparisonStatus::failed);
                audit.append("ml.model_comparison.run", user->id, "failure",
                             id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_model_comparison_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/ml/model-comparisons/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/result") == 0) {
            if (auto denied = forbidden_unless(user->role, "ml.comparisons.view")) return *denied;
            const auto id = request.target.substr(
                29U, request.target.size() - 29U - 7U);
            const auto result_json = ml_comparison_results->find(id);
            if (!result_json) {
                return response(
                    404, "Not Found",
                    "{\"error\":\"ml_model_comparison_result_not_found\"}");
            }
            return response(200, "OK",
                            "{\"comparisonId\":\"" + json_escape(id) +
                                "\",\"result\":" + *result_json + "}");
        }
        if (request.method == "GET" && request.target == "/models") {
            return workloads->model_inventory_page();
        }
        if (request.method == "GET" && request.target.rfind("/app", 0U) == 0U) {
            const bool can_manage_settings =
                user->role == UserRole::administrator ||
                user->role == UserRole::developer;
            const bool is_administrator = user->role == UserRole::administrator;
            const std::string target = request.target;
            // Each workspace section is served at its own URL -- switching
            // sections is a normal page navigation, not a client-side panel
            // swap. A role that can't reach a section (typed directly or
            // via a stale bookmark) is redirected to chat rather than shown
            // a page whose underlying API calls would 403 anyway.
            if (target == "/app") {
                return application_page(*user, "chat");
            }
            if (target.rfind("/app/chat/", 0U) == 0U) {
                return application_page(*user, "chat",
                                        target.substr(10U));
            }
            if (target == "/app/projects") {
                return can_manage_settings
                           ? application_page(*user, "projects")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/models/inventory") {
                return can_manage_settings
                           ? application_page(*user, "models-inventory")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/models/download") {
                return can_manage_settings
                           ? application_page(*user, "models-download")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/models/downloads") {
                // Active downloads now lives at the bottom of the download
                // page itself; keep the old URL working as a redirect.
                return response(302, "Found", "", {"Location: /app/models/download"});
            }
            if (target == "/app/models/benchmarks") {
                return can_manage_settings
                           ? application_page(*user, "models-benchmarks")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml") {
                return is_administrator
                           ? application_page(*user, "ml-dashboard")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/projects") {
                return is_administrator
                           ? application_page(*user, "ml-projects")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/models") {
                return is_administrator
                           ? application_page(*user, "ml-models")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/datasets") {
                return is_administrator
                           ? application_page(*user, "ml-datasets")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/subjects") {
                return is_administrator
                           ? application_page(*user, "ml-subjects")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/label-tasks") {
                return is_administrator
                           ? application_page(*user, "ml-label-tasks")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/prep-jobs") {
                return is_administrator
                           ? application_page(*user, "ml-prep-jobs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/training-jobs") {
                return is_administrator
                           ? application_page(*user, "ml-training-jobs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/evaluation-runs") {
                return is_administrator
                           ? application_page(*user, "ml-evaluation-runs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/experiments") {
                return is_administrator
                           ? application_page(*user, "ml-experiments")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/fine-tuning-jobs") {
                return is_administrator
                           ? application_page(*user, "ml-fine-tuning-jobs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/model-builder-configs") {
                return is_administrator
                           ? application_page(*user, "ml-model-builder-configs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/instruction-examples") {
                return is_administrator
                           ? application_page(*user, "ml-instruction-examples")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/synthetic-records") {
                return is_administrator
                           ? application_page(*user, "ml-synthetic-records")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/vector-stores") {
                return is_administrator
                           ? application_page(*user, "ml-vector-stores")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/rag-configs") {
                return is_administrator
                           ? application_page(*user, "ml-rag-configs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/subject-exams") {
                return is_administrator
                           ? application_page(*user, "ml-subject-exams")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/hyperparameter-searches") {
                return is_administrator
                           ? application_page(*user, "ml-hyperparameter-searches")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/model-optimizations") {
                return is_administrator
                           ? application_page(*user, "ml-model-optimizations")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/checkpoints") {
                return is_administrator
                           ? application_page(*user, "ml-checkpoints")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/deployments") {
                return is_administrator
                           ? application_page(*user, "ml-deployments")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/model-comparisons") {
                return is_administrator
                           ? application_page(*user, "ml-model-comparisons")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/inference-endpoints") {
                return is_administrator
                           ? application_page(*user, "ml-inference-endpoints")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/compute-nodes") {
                return is_administrator
                           ? application_page(*user, "ml-compute-nodes")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/automation-pipelines") {
                return is_administrator
                           ? application_page(*user, "ml-automation-pipelines")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/safety-governance") {
                return is_administrator
                           ? application_page(*user, "ml-safety-governance")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/audit-logs") {
                return is_administrator
                           ? application_page(*user, "ml-audit-logs")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/settings") {
                return is_administrator
                           ? application_page(*user, "ml-settings")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/ml/monitoring") {
                return is_administrator
                           ? application_page(*user, "ml-monitoring")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/report/system") {
                return is_administrator
                           ? application_page(*user, "report-system")
                           : response(302, "Found", "", {"Location: /app"});
            }
            // Phase 35: Performance administration sidebar -- one
            // consolidated page (Overview/Adaptive Controller, Local Runner
            // Pool, Intranet Worker Pool, Memory, Caches, Storage, tier-
            // migration manifest, Scheduling, Advanced Optimizations,
            // Calibration) rather than the plan's full thirteen-route
            // enumeration; see the Phase 35 status note in docs/PLAN.md for
            // the honest scope this condenses (Query Traces, Runner
            // Configuration, Model Comparison, Benchmarks, and Regression
            // History remain deferred -- no dedicated telemetry route
            // exists for those yet). Every figure it renders comes from the
            // same GET routes already exposed by Phase 13/14/17/19/20/31/
            // 33/34, not a separately maintained display-only value.
            if (target == "/app/performance") {
                return is_administrator
                           ? application_page(*user, "performance")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/settings/config") {
                return is_administrator
                           ? application_page(*user, "settings-config")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/admin/create") {
                return is_administrator
                           ? application_page(*user, "admin-create")
                           : response(302, "Found", "", {"Location: /app"});
            }
            if (target == "/app/admin/users") {
                return is_administrator
                           ? application_page(*user, "admin-users")
                           : response(302, "Found", "", {"Location: /app"});
            }
            return response(404, "Not Found", "{\"error\":\"not_found\"}");
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/projects") {
            return workloads->list_projects();
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/projects") {
            if (auto denied = forbidden_unless(user->role, "projects.write")) return *denied;
            return workloads->create_project(request, *user);
        }
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/projects/", 0U) == 0U &&
            request.target.size() >= 6U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/index") == 0) {
            return workloads->index_status(
                request, cookie_authenticated, authenticated_project_ids);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/projects/", 0U) == 0U &&
            request.target.size() >= 14U &&
            request.target.compare(request.target.size() - 14U, 14U,
                                   "/index/rebuild") == 0) {
            return workloads->rebuild_index(
                request, *user, cookie_authenticated,
                authenticated_project_ids);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/projects/", 0U) == 0U &&
            request.target.size() >= 13U &&
            request.target.compare(request.target.size() - 13U, 13U,
                                   "/index/cancel") == 0) {
            return workloads->cancel_index(
                request, *user, cookie_authenticated,
                authenticated_project_ids);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/projects/", 0U) == 0U &&
            request.target.size() >= 13U &&
            request.target.compare(request.target.size() - 13U, 13U,
                                   "/index/notify") == 0) {
            return workloads->notify_index(
                request, *user, cookie_authenticated,
                authenticated_project_ids);
        }
        if (request.method == "GET" && request.target == "/api/v1/chats") {
            return list_chats(*user);
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/system/scheduler") {
            if (user->role != UserRole::administrator) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK", RequestScheduler::to_json(
                                           request_scheduler->status()));
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/memories") {
            return list_user_memories(*user);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/memories") {
            if (auto denied = forbidden_unless(user->role, "chats.write")) {
                return *denied;
            }
            return create_user_memory(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/memories/", 0U) == 0U &&
            request.target.size() > 24U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "chats.write")) {
                return *denied;
            }
            return delete_user_memory(request, *user);
        }
        // Single-chat message history, added for the sidebar's chat-history
        // panel: matches "/api/v1/chats/{id}" exactly (no further path
        // segments), so it never collides with the "/messages" POST route
        // below.
        if (request.method == "GET" &&
            request.target.rfind("/api/v1/chats/", 0U) == 0U &&
            request.target.size() > 14U &&
            request.target.find('/', 14U) == std::string::npos) {
            return get_chat_messages(request, *user);
        }
        if (request.method == "POST" && request.target == "/api/v1/chats") {
            if (auto denied = forbidden_unless(user->role, "chats.write")) return *denied;
            return create_chat(request, *user);
        }
        // Client-driven warm trigger -- see the comment on warm_runner()
        // above for why this replaced get_chat_messages() firing it as a
        // side effect of merely opening a chat.
        if (request.method == "POST" && request.target == "/api/v1/runner/warm") {
            return warm_runner(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/chats/", 0U) == 0U &&
            request.target.size() > 24U &&
            request.target.compare(request.target.size() - 9U, 9U,
                                   "/messages") == 0) {
            return send_chat_message(request, *user, stream_socket);
        }
        // Lets the composer's model picker stay usable after the first
        // message instead of locking to whatever model the chat started
        // with -- only future messages use the new model; past history is
        // unaffected.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/chats/", 0U) == 0U &&
            request.target.size() > 20U &&
            request.target.compare(request.target.size() - 6U, 6U,
                                   "/model") == 0) {
            if (auto denied = forbidden_unless(user->role, "chats.write")) return *denied;
            return set_chat_model(request, *user);
        }
        // Deletes a chat and its full message history. A POST-with-suffix
        // action (matching /model, /messages above) rather than the DELETE
        // verb, so it falls under the same "/api/v1/chats" POST scope check
        // above without adding a new HTTP method this server has never had
        // to support anywhere else.
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/chats/", 0U) == 0U &&
            request.target.size() > 21U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/delete") == 0) {
            if (auto denied = forbidden_unless(user->role, "chats.write")) return *denied;
            return delete_chat(request, *user);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/attachments") {
            if (auto denied = forbidden_unless(user->role, "attachments.write")) return *denied;
            return workloads->create_attachment(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/models/", 0U) == 0U &&
            request.target.compare(request.target.size() - 5U, 5U, "/load") == 0) {
            return workloads->load_model(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/models/", 0U) == 0U &&
            request.target.compare(request.target.size() - 7U, 7U,
                                   "/unload") == 0) {
            return workloads->unload_model(*user);
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/model-downloads") {
            return workloads->list_downloads();
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/model-downloads") {
            return workloads->create_download(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/model-downloads/", 0U) == 0U &&
            request.target.compare(request.target.size() - 4U, 4U, "/run") == 0) {
            return workloads->run_download(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/model-downloads/", 0U) == 0U &&
            request.target.size() > 6U &&
            request.target.compare(request.target.size() - 6U, 6U, "/pause") == 0) {
            return workloads->pause_download(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/model-downloads/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U, "/cancel") == 0) {
            return workloads->cancel_download(request, *user);
        }
        if (request.method == "POST" &&
            request.target.rfind("/api/v1/model-downloads/", 0U) == 0U &&
            request.target.size() > 7U &&
            request.target.compare(request.target.size() - 7U, 7U, "/remove") == 0) {
            return workloads->remove_download(request, *user);
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/benchmarks") {
            return workloads->list_benchmarks();
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/benchmarks/recommend") {
            return workloads->recommend_benchmark(request);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/benchmarks") {
            return workloads->run_benchmark(request, *user);
        }
        if (auto integration_response =
                integrations->handle_authenticated(
                    request, *user, cookie_authenticated,
                    authenticated_scopes, authenticated_project_ids)) {
            return std::move(*integration_response);
        }
        return response(404, "Not Found", "{\"error\":\"not_found\"}");
    }

    // Called from HttpServer::stop() (outside this class's private section)
    // so a graceful shutdown doesn't have to wait out an in-flight
    // download's full transfer.
    void cancel_all_downloads() noexcept {
        if (downloads) downloads->cancel_all();
    }

private:
    std::string session_cookie(const std::string& token) const {
        return "Set-Cookie: masterai_session=" + token +
               "; Path=/; HttpOnly; SameSite=Strict" +
               (configuration.tls_mode == "required" ? "; Secure" : "");
    }

    static std::string role(const UserRole value) {
        if (value == UserRole::administrator) return "administrator";
        if (value == UserRole::developer) return "developer";
        return "viewer";
    }

    static UserRole parse_role_value(const std::string& value) {
        if (value == "administrator") return UserRole::administrator;
        if (value == "developer") return UserRole::developer;
        if (value == "viewer") return UserRole::viewer;
        throw std::runtime_error("requested role is invalid");
    }

    // Shared by every role-gated route below: returns the 403 response to
    // return immediately if `role` lacks `permission`, or nullopt to
    // continue handling the request.
    static std::optional<std::string> forbidden_unless(const UserRole role,
                                                        const char* permission) {
        if (role_allows(role, permission)) return std::nullopt;
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }

    // Phase 79: builds the TrainedTabularModel that gets persisted for a
    // real mid-training checkpoint snapshot -- `live_model` already carries
    // this epoch's genuine weights plus the fixed feature/target/class
    // schema for the run, so this only needs to attach the two identity
    // fields CheckpointModelStore's key/lookup convention requires (see its
    // class comment in masterai.hpp): `model_id` repurposed to the owning
    // checkpoint id, `training_job_id` left as the real training job id.
    static TrainedTabularModel checkpoint_snapshot(
        const std::string& checkpoint_id, const std::string& training_job_id,
        const TrainedTabularModel& live_model) {
        TrainedTabularModel snapshot = live_model;
        snapshot.model_id = checkpoint_id;
        snapshot.training_job_id = training_job_id;
        return snapshot;
    }

    // Phase 56/69: the real training executor's core, factored out so both
    // the single-job POST .../training-jobs/{id}/run handler and the
    // automation-pipelines "Train model" stage (Phase 69) run the exact
    // same real training path. Moves the job through queued/preparing/
    // running for real, trains genuine weights, registers/attaches the
    // resulting model, records checkpoints, and leaves the job awaiting
    // evaluation. Throws std::runtime_error (after moving the job to
    // failed) if the CSV or the optimizer itself fails; callers already own
    // the not-found/no-content checks that precede this.
    struct TrainingExecution {
        TabularTrainingReport report;
        TrainedTabularModel model;
        std::string model_id;
    };
    TrainingExecution execute_training_job(
        const TrainingJob& job, const DatasetContentStore::Content& content,
        const TabularTrainingOptions& options, const std::string& user_id) {
        ml_training_jobs->set_status(job.id, TrainingJobStatus::queued);
        ml_training_jobs->set_status(job.id, TrainingJobStatus::preparing);
        TrainedTabularModel model;
        TabularTrainingReport report;
        try {
            const auto data =
                parse_tabular_csv(content.csv, content.target_column);
            ml_training_jobs->set_status(job.id, TrainingJobStatus::running);
            // Phase 78 (this pass): real live progress -- begin() marks this
            // job in-flight for any concurrent GET .../live-progress poll on
            // another connection thread, on_epoch feeds it a genuine
            // (epoch, loss) pair after every real gradient-descent step, and
            // end() (via the RAII-style guard below) always clears the
            // in-flight entry on every exit path, success or failure, so a
            // finished/failed job never reports stale "still running" state.
            ml_training_progress.begin(job.id, options.epochs);
            struct ProgressGuard {
                TrainingProgressTracker& tracker;
                const std::string& job_id;
                ~ProgressGuard() { tracker.end(job_id); }
            } progress_guard{ml_training_progress, job.id};
            // Phase 79: the checkpoint epoch stride is computed up front
            // (options.epochs is already known) instead of after the run,
            // so on_epoch can capture a real weight snapshot the instant
            // training crosses each stride boundary -- capped at ten
            // checkpoints so a 10000-epoch run doesn't flood the checkpoint
            // list, same cap the old post-hoc logic used.
            std::size_t checkpoint_stride = options.checkpoint_interval;
            if (checkpoint_stride > 0U && options.epochs > 0U) {
                if (options.epochs / checkpoint_stride > 10U) {
                    checkpoint_stride = options.epochs / 10U;
                }
            }
            report = train_tabular_model(
                data, options, model, nullptr,
                [this, &job, &user_id, checkpoint_stride](
                    const std::uint32_t epoch, const double loss,
                    const TrainedTabularModel& live_model) {
                    ml_training_progress.update(job.id, epoch, loss);
                    const std::uint32_t epoch_number = epoch + 1U;
                    if (checkpoint_stride > 0U &&
                        epoch_number % checkpoint_stride == 0U) {
                        char loss_text[32];
                        std::snprintf(loss_text, sizeof(loss_text), "%.6g", loss);
                        const auto checkpoint = ml_training_checkpoints->create(
                            user_id, job.id,
                            job.name + " epoch " + std::to_string(epoch_number),
                            "captured by the Phase 79 training executor",
                            "epoch " + std::to_string(epoch_number) +
                                ", training loss " + loss_text,
                            epoch_number, /*has_snapshot=*/true);
                        ml_checkpoint_models->put(
                            checkpoint_snapshot(checkpoint.id, job.id, live_model));
                    }
                });
        } catch (const std::exception&) {
            ml_training_jobs->set_status(job.id, TrainingJobStatus::failed);
            throw;
        }
        std::string model_id = job.model_id;
        if (model_id.empty() || !ml_models->find(model_id)) {
            const auto entry = ml_models->create(
                user_id, job.name + "-model", job.name + " (trained)",
                "1", "masterai-tabular",
                model.classification ? "classification" : "regression",
                "masterai-tabular-v1", "training-job:" + job.id, "");
            model_id = entry.id;
        }
        model.model_id = model_id;
        model.training_job_id = job.id;
        ml_trained_models->put(model);
        // Trained models wait for Evaluation Lab review before approval,
        // matching the dashboard's models-awaiting-evaluation count.
        ml_models->set_state(model_id, ModelRegistryState::evaluation);
        ml_training_jobs->set_status(job.id,
                                     TrainingJobStatus::awaiting_evaluation);
        return {std::move(report), std::move(model), model_id};
    }

    // Phase 80: the real Experiment Tracking executor -- section 25's
    // "training/validation/evaluation metrics, checkpoints, hardware,
    // runtime" become genuine numbers instead of deferred fields. Trains
    // the experiment's dataset content exactly like execute_training_job
    // above (checkpoints captured the same stride-capped-at-10 way, via
    // the same TrainingCheckpointStore/CheckpointModelStore pair -- the
    // checkpoint's "training_job_id" field holds this experiment's id
    // here, the same field reused across a conceptually different run
    // kind FineTuningJob already reuses TrainingJob's status enum for),
    // then runs an *independent* evaluate_tabular_model pass against the
    // full dataset for "evaluation metrics" distinct from the held-out
    // "validation metrics" train_tabular_model already computed. Moves the
    // experiment through running -> completed/failed for real, with
    // genuine started/completed timestamps and (on failure) a real
    // exception message as the failure reason -- never a queued/never-run
    // experiment silently reporting success.
    void execute_experiment_run(const Experiment& experiment,
                                const DatasetContentStore::Content& content,
                                const TabularTrainingOptions& options,
                                const std::string& user_id) {
        ml_experiments->mark_started(experiment.id);
        const auto run_started = std::chrono::steady_clock::now();
        TrainedTabularModel model;
        TabularTrainingReport report;
        std::vector<std::string> checkpoint_ids;
        try {
            const auto data =
                parse_tabular_csv(content.csv, content.target_column);
            std::size_t checkpoint_stride = options.checkpoint_interval;
            if (checkpoint_stride > 0U && options.epochs > 0U) {
                if (options.epochs / checkpoint_stride > 10U) {
                    checkpoint_stride = options.epochs / 10U;
                }
            }
            report = train_tabular_model(
                data, options, model, nullptr,
                [this, &experiment, &user_id, checkpoint_stride,
                 &checkpoint_ids](const std::uint32_t epoch, const double loss,
                                  const TrainedTabularModel& live_model) {
                    (void)loss;
                    const std::uint32_t epoch_number = epoch + 1U;
                    if (checkpoint_stride > 0U &&
                        epoch_number % checkpoint_stride == 0U) {
                        char loss_text[32];
                        std::snprintf(loss_text, sizeof(loss_text), "%.6g", loss);
                        const auto checkpoint = ml_training_checkpoints->create(
                            user_id, experiment.id,
                            experiment.name + " epoch " +
                                std::to_string(epoch_number),
                            "captured by the Phase 80 experiment executor",
                            "epoch " + std::to_string(epoch_number) +
                                ", training loss " + loss_text,
                            epoch_number, /*has_snapshot=*/true);
                        ml_checkpoint_models->put(checkpoint_snapshot(
                            checkpoint.id, experiment.id, live_model));
                        checkpoint_ids.push_back(checkpoint.id);
                    }
                });
            const auto evaluation_metrics = evaluate_tabular_model(model, data);
            const auto hardware = probe_hardware(configuration.models_root);
            const auto runtime = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - run_started);
            std::string model_id = experiment.model_id;
            if (model_id.empty() || !ml_models->find(model_id)) {
                const auto entry = ml_models->create(
                    user_id, experiment.name + "-model",
                    experiment.name + " (trained)", "1", "masterai-tabular",
                    model.classification ? "classification" : "regression",
                    "masterai-tabular-v1", "experiment:" + experiment.id, "");
                model_id = entry.id;
            }
            model.model_id = model_id;
            model.training_job_id = experiment.id;
            ml_trained_models->put(model);
            ml_models->set_state(model_id, ModelRegistryState::evaluation);
            const auto result_json = experiment_result_json(
                report, evaluation_metrics, hardware,
                static_cast<std::uint64_t>(runtime.count()), checkpoint_ids,
                "trained " + std::to_string(report.loss_history.size()) +
                    " epochs, final loss " +
                    std::to_string(report.final_loss),
                model_id);
            ml_experiment_results->put(experiment.id, result_json);
            ml_experiments->mark_completed(experiment.id,
                                           ExperimentStatus::completed, "");
        } catch (const std::exception& error) {
            ml_experiments->mark_completed(experiment.id,
                                           ExperimentStatus::failed,
                                           error.what());
            throw;
        }
    }

    // Phase 70: the real fine-tuning executor. Unlike execute_training_job
    // above (which may start a model from zero-initialized weights),
    // fine-tuning always requires an already-trained base model -- callers
    // check ml_trained_models->find(job.model_id) first -- and
    // train_tabular_model's warm_start parameter continues gradient
    // descent from that base model's weights on the job's dataset, so the
    // resulting model is genuinely adapted rather than freshly trained.
    // The adapted model is registered as a new Model Registry entry (the
    // base model is left untouched, matching Model Comparison's
    // baseline/candidate pattern) and, like a training job, lands in the
    // evaluation state awaiting Evaluation Lab review. Throws
    // std::runtime_error (after moving the job to failed) if the CSV, the
    // base/dataset schema match, or the optimizer itself fails.
    TrainingExecution execute_fine_tuning_job(
        const FineTuningJob& job, const TrainedTabularModel& base_model,
        const DatasetContentStore::Content& content,
        const TabularTrainingOptions& options, const std::string& user_id) {
        ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::queued);
        ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::preparing);
        TrainedTabularModel model;
        TabularTrainingReport report;
        try {
            const auto data =
                parse_tabular_csv(content.csv, content.target_column);
            ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::running);
            // Phase 79: same up-front stride + real weight-snapshot capture
            // execute_training_job uses above -- see its comment for why the
            // stride is computed before the run instead of after.
            std::size_t checkpoint_stride = options.checkpoint_interval;
            if (checkpoint_stride > 0U && options.epochs > 0U) {
                if (options.epochs / checkpoint_stride > 10U) {
                    checkpoint_stride = options.epochs / 10U;
                }
            }
            report = train_tabular_model(
                data, options, model, &base_model,
                [this, &job, &user_id, checkpoint_stride](
                    const std::uint32_t epoch, const double loss,
                    const TrainedTabularModel& live_model) {
                    const std::uint32_t epoch_number = epoch + 1U;
                    if (checkpoint_stride > 0U &&
                        epoch_number % checkpoint_stride == 0U) {
                        char loss_text[32];
                        std::snprintf(loss_text, sizeof(loss_text), "%.6g", loss);
                        const auto checkpoint = ml_training_checkpoints->create(
                            user_id, job.id,
                            job.name + " epoch " + std::to_string(epoch_number),
                            "captured by the Phase 79 fine-tuning executor",
                            "epoch " + std::to_string(epoch_number) +
                                ", training loss " + loss_text,
                            epoch_number, /*has_snapshot=*/true);
                        ml_checkpoint_models->put(
                            checkpoint_snapshot(checkpoint.id, job.id, live_model));
                    }
                });
        } catch (const std::exception&) {
            ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::failed);
            throw;
        }
        const auto entry = ml_models->create(
            user_id, job.name + "-model", job.name + " (fine-tuned)", "1",
            "masterai-tabular",
            model.classification ? "classification" : "regression",
            "masterai-tabular-v1",
            "fine-tuning-job:" + job.id + " base-model:" + job.model_id, "");
        const std::string model_id = entry.id;
        model.model_id = model_id;
        model.training_job_id = job.id;
        ml_trained_models->put(model);
        ml_models->set_state(model_id, ModelRegistryState::evaluation);
        ml_fine_tuning_jobs->set_status(job.id,
                                        FineTuningJobStatus::awaiting_evaluation);
        return {std::move(report), std::move(model), model_id};
    }

    // Phase 79: the real checkpoint-resume executor, closing Checkpoint
    // Management's (section 33, Phase 54) last deferred gap -- "the resume
    // ... operation" its own class comment named as future work. Continues
    // gradient descent from a genuinely captured checkpoint snapshot via
    // the same warm_start mechanism Phase 70's fine-tuning executor above
    // uses to adapt a base model, on the same dataset the owning training
    // job trained on. Like fine-tuning, the result is registered as a new
    // Model Registry entry (the checkpoint and the model it was captured
    // from are left untouched) and lands in the evaluation state. Throws
    // std::runtime_error if the checkpoint has no real snapshot, its
    // owning training job can no longer be found, or the optimizer itself
    // fails.
    TrainingExecution execute_checkpoint_resume(
        const TrainingCheckpoint& checkpoint, const TrainingJob& job,
        const DatasetContentStore::Content& content,
        const TabularTrainingOptions& options, const std::string& user_id) {
        const auto snapshot = ml_checkpoint_models->find(checkpoint.id);
        if (!snapshot) {
            throw std::runtime_error(
                "checkpoint has no captured weight snapshot to resume from");
        }
        const auto data = parse_tabular_csv(content.csv, content.target_column);
        TrainedTabularModel model;
        std::size_t checkpoint_stride = options.checkpoint_interval;
        if (checkpoint_stride > 0U && options.epochs > 0U) {
            if (options.epochs / checkpoint_stride > 10U) {
                checkpoint_stride = options.epochs / 10U;
            }
        }
        const auto report = train_tabular_model(
            data, options, model, &(*snapshot),
            [this, &job, &user_id, checkpoint_stride](
                const std::uint32_t epoch, const double loss,
                const TrainedTabularModel& live_model) {
                const std::uint32_t epoch_number = epoch + 1U;
                if (checkpoint_stride > 0U &&
                    epoch_number % checkpoint_stride == 0U) {
                    char loss_text[32];
                    std::snprintf(loss_text, sizeof(loss_text), "%.6g", loss);
                    const auto resumed_checkpoint = ml_training_checkpoints->create(
                        user_id, job.id,
                        job.name + " resumed epoch " + std::to_string(epoch_number),
                        "captured by the Phase 79 checkpoint-resume executor",
                        "epoch " + std::to_string(epoch_number) +
                            ", training loss " + loss_text,
                        epoch_number, /*has_snapshot=*/true);
                    ml_checkpoint_models->put(checkpoint_snapshot(
                        resumed_checkpoint.id, job.id, live_model));
                }
            });
        const auto entry = ml_models->create(
            user_id, job.name + "-model-resumed", job.name + " (resumed)",
            "1", "masterai-tabular",
            model.classification ? "classification" : "regression",
            "masterai-tabular-v1", "checkpoint:" + checkpoint.id, "");
        const std::string model_id = entry.id;
        model.model_id = model_id;
        model.training_job_id = job.id;
        ml_trained_models->put(model);
        ml_models->set_state(model_id, ModelRegistryState::evaluation);
        return {std::move(report), std::move(model), model_id};
    }

    // Phase 73: the real LLM LoRA fine-tuning executor. Runs on a detached
    // background thread (see the "llm:"-prefixed branch of POST
    // .../fine-tuning-jobs/{id}/run below) because a genuine llama.cpp
    // finetune run can take far longer than the HTTP request timeout, so
    // `job`/`base_model_entry`/`content`/`user_id` all arrive already
    // copied by value and this must not touch `request`/`user`. Writes its
    // outcome to ml_llm_finetune_results (JSON), the only way a caller
    // polling GET .../llm-result learns whether/how it finished; never
    // reports success without a real merged GGUF file on disk.
    void run_llm_fine_tuning_job(const FineTuningJob& job,
                                 const ModelRegistryEntry& base_model_entry,
                                 const DatasetContentStore::Content& content,
                                 const LlmFineTuneOptions& options,
                                 const std::string& user_id) {
        ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::preparing);
        const std::atomic_bool no_cancellation{false};
        const auto work_directory =
            configuration.runtime_root / "ml-finetune" / job.id;
        try {
            const auto training_text_path =
                write_llm_finetune_training_text(
                    content.csv, work_directory / "training.txt");
            ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::running);
            const auto result = run_llama_lora_finetune(
                configuration, base_model_entry.source, training_text_path,
                work_directory, options, no_cancellation);
            const auto merged_entry = ml_models->create(
                user_id, job.name + "-model", job.name + " (LoRA fine-tuned)",
                "1", base_model_entry.family, base_model_entry.task,
                "gguf-lora-merged", result.merged_gguf.string(), "");
            ml_models->set_state(merged_entry.id, ModelRegistryState::evaluation);
            const std::string result_json =
                "{\"status\":\"completed\",\"jobId\":\"" + json_escape(job.id) +
                "\",\"modelId\":\"" + json_escape(merged_entry.id) +
                "\",\"mergedModelPath\":\"" +
                json_escape(result.merged_gguf.string()) +
                "\",\"finetuneLogTail\":\"" +
                json_escape(result.finetune_log_tail) +
                "\",\"exportLoraLogTail\":\"" +
                json_escape(result.export_lora_log_tail) + "\"}";
            ml_llm_finetune_results->put(job.id, result_json);
            ml_fine_tuning_jobs->set_status(
                job.id, FineTuningJobStatus::awaiting_evaluation);
            audit.append("ml.fine_tuning_job.llm_run", user_id, "success", job.id);
        } catch (const std::exception& error) {
            const std::string result_json =
                "{\"status\":\"failed\",\"jobId\":\"" + json_escape(job.id) +
                "\",\"error\":\"" + json_escape(error.what()) + "\"}";
            ml_llm_finetune_results->put(job.id, result_json);
            ml_fine_tuning_jobs->set_status(job.id, FineTuningJobStatus::failed);
            audit.append("ml.fine_tuning_job.llm_run", user_id, "failure", job.id);
        }
    }

    // Phase 56/69: the real evaluation harness's core, factored out the
    // same way as execute_training_job above -- shared by the single-run
    // POST .../evaluation-runs/{id}/run handler and the automation-
    // pipelines "Evaluate model" stage. Returns the genuine metrics JSON;
    // throws std::runtime_error (after moving the run to failed) on CSV/
    // schema mismatch.
    std::string execute_evaluation_run(
        const EvaluationRun& run, const TrainedTabularModel& model,
        const DatasetContentStore::Content& content) {
        ml_evaluation_runs->set_status(run.id, EvaluationRunStatus::running);
        try {
            const auto data =
                parse_tabular_csv(content.csv, content.target_column);
            const auto metrics = evaluate_tabular_model(model, data);
            const auto metrics_json = tabular_evaluation_metrics_json(metrics);
            ml_evaluation_results->put(run.id, metrics_json);
            ml_evaluation_runs->set_status(run.id,
                                           EvaluationRunStatus::completed);
            return metrics_json;
        } catch (const std::exception&) {
            ml_evaluation_runs->set_status(run.id, EvaluationRunStatus::failed);
            throw;
        }
    }

    // Phase 69/71/72: executes every recognized stage of one Automation
    // Pipeline run, in the order the pipeline names them, persisting live
    // progress via AutomationPipelineStore::append_stage_result() before
    // and after each stage so a caller polling GET .../runs sees the run
    // advance stage by stage rather than only its final result. Runs on the
    // detached background thread the POST .../run route above spawns, so
    // `pipeline`/`stage_names` arrive already copied by value and nothing
    // here may touch `request`/`user`. "Train model" and "Evaluate model"
    // reuse Phase 56's tabular engine via execute_training_job/
    // execute_evaluation_run above; "Validate data" (parse_tabular_csv),
    // "Validate model" (a trained-weights lookup), "Safety tests" (an
    // approved ModelCard lookup), "Request approval"/"Deploy staging"/
    // "Deploy production" (a real Deployment record created/approved for
    // that environment -- Deployment Manager's own documented scope is an
    // approval workflow, not live traffic serving), "Rollback" (that
    // deployment's approval revoked), "Monitor" (Phase 68's real
    // monitoring snapshot), "Import data" (a real content-presence check),
    // "Clean data" (real blank/duplicate row removal via clean_tabular_csv,
    // persisted back to the dataset), "Split data" (real train/holdout
    // counts via split_tabular_csv, which reuses the trainer's own split
    // formula), "Optimize" (a real ModelOptimizationRun that actually
    // prunes the current model's learned weights via
    // prune_tabular_model), and "Staging tests" (a real evaluation run,
    // reusing execute_evaluation_run) are all genuine, in-process calls to
    // existing or Phase-72-added executors -- not fabricated results.
    // "Label data" is the one remaining honestly-skipped stage: it looks
    // for a real completed LabelTaskStore entry targeting the dataset and
    // reports "skipped" when none exists, since this codebase still has no
    // automated labeler.
    void run_automation_pipeline(const AutomationPipeline& pipeline,
                                 const std::string& run_id,
                                 const std::string& user_id,
                                 const std::vector<std::string>& stage_names) {
        std::string current_model_id = pipeline.model_id;
        std::string last_deployment_id;
        bool any_failed = false;
        std::vector<std::string> stage_result_fragments;
        const auto stage_results_json = [&stage_result_fragments]() {
            std::string body = "[";
            bool first = true;
            for (const auto& fragment : stage_result_fragments) {
                if (!first) body += ",";
                first = false;
                body += fragment;
            }
            return body + "]";
        };
        // Each stage lambda returns {status, detail} and updates
        // current_model_id/last_deployment_id/any_failed as a side effect,
        // so the dispatch loop below stays a flat if-chain.
        const auto run_train_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured for training"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            const auto job = ml_training_jobs->create(
                user_id, pipeline.project_id, current_model_id,
                pipeline.dataset_id, pipeline.name + " (pipeline run)",
                "created by automation pipeline run", "tabular");
            try {
                const auto result = execute_training_job(
                    job, *content, TabularTrainingOptions{}, user_id);
                current_model_id = result.model_id;
                audit.append("ml.training_job.run", user_id, "success", job.id);
                return {"completed", "trained job " + job.id + "; model " +
                                          current_model_id};
            } catch (const std::exception& error) {
                any_failed = true;
                audit.append("ml.training_job.run", user_id, "failure", job.id);
                return {"failed", error.what()};
            }
        };
        const auto run_evaluate_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no trained model available to evaluate (run a "
                        "Train model stage first, or set the pipeline's "
                        "model)"};
            }
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured for evaluation"};
            }
            const auto model = ml_trained_models->find(current_model_id);
            if (!model) {
                any_failed = true;
                return {"failed", "model has not been trained yet"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            const auto eval_run = ml_evaluation_runs->create(
                user_id, current_model_id, pipeline.dataset_id,
                pipeline.name + " (pipeline run)",
                "created by automation pipeline run", "");
            try {
                execute_evaluation_run(eval_run, *model, *content);
                audit.append("ml.evaluation_run.run", user_id, "success",
                            eval_run.id);
                return {"completed",
                        "evaluation run " + eval_run.id +
                            " completed; see GET /api/v1/ml/evaluation-runs/" +
                            eval_run.id + "/result for metrics"};
            } catch (const std::exception& error) {
                any_failed = true;
                audit.append("ml.evaluation_run.run", user_id, "failure",
                            eval_run.id);
                return {"failed", error.what()};
            }
        };
        const auto run_validate_data_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured to validate"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            try {
                const auto data =
                    parse_tabular_csv(content->csv, content->target_column);
                return {"completed",
                        "dataset has " + std::to_string(data.features.size()) +
                            " valid row(s) and " +
                            std::to_string(data.feature_names.size()) +
                            " feature column(s)"};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_validate_model_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no model available to validate (run a Train model "
                        "stage first, or set the pipeline's model)"};
            }
            if (!ml_trained_models->find(current_model_id)) {
                any_failed = true;
                return {"failed", "model " + current_model_id +
                                       " has not been trained yet"};
            }
            return {"completed",
                    "model " + current_model_id + " has trained weights"};
        };
        const auto run_safety_tests_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no model available for safety review (run a Train "
                        "model stage first, or set the pipeline's model)"};
            }
            bool has_approved_card = false;
            std::string approved_card_id;
            for (const auto& card : ml_safety_governance->list_model_cards()) {
                if (card.model_id == current_model_id &&
                    card.status == SafetyPolicyStatus::approved) {
                    has_approved_card = true;
                    approved_card_id = card.id;
                    break;
                }
            }
            if (!has_approved_card) {
                any_failed = true;
                return {"failed", "no approved model card exists for model " +
                                       current_model_id +
                                       " (create and approve one in Safety "
                                       "and Governance)"};
            }
            // Phase 74: also runs a real content scan (see
            // scan_content_for_risks) over the pipeline's dataset content,
            // so "Safety tests" catches secrets/prompt-injection phrasing
            // in the training data, not only the model-card approval check
            // above.
            std::string classifier_note;
            if (!pipeline.dataset_id.empty()) {
                const auto content = ml_dataset_content->find(pipeline.dataset_id);
                if (content) {
                    const auto report = scan_content_for_risks(content->csv);
                    if (!report.clean()) {
                        any_failed = true;
                        std::string detail =
                            "approved model card " + approved_card_id +
                            " covers model " + current_model_id +
                            ", but the dataset's content scan found " +
                            std::to_string(report.findings.size()) +
                            " issue(s): ";
                        for (std::size_t index = 0;
                            index < report.findings.size(); ++index) {
                            if (index > 0U) detail += "; ";
                            detail += report.findings[index].detail;
                        }
                        return {"failed", detail};
                    }
                    // Phase 74 (this pass): a best-effort real ML-classifier
                    // pass (scan_content_with_model_classifier) for bias/
                    // hallucination/subtler-harmful-content categories the
                    // heuristic scan above cannot catch. Deliberately
                    // informational, not a stage-failing check: an
                    // unavailable/misconfigured model must never block a
                    // pipeline the heuristic scan already passed, since this
                    // is the same judge model the pipeline's own dataset was
                    // trained against, not an independent, always-on gate.
                    const auto classifier = scan_content_with_model_classifier(
                        content->csv.substr(0U, 4000U),
                        [&](const std::string& prompt) {
                            return execute_rag_generation(current_model_id, prompt).text;
                        });
                    if (classifier.available && !classifier.findings.empty()) {
                        classifier_note = "; model classifier flagged " +
                                          std::to_string(classifier.findings.size()) +
                                          " potential issue(s)";
                    } else if (!classifier.available) {
                        classifier_note = "; model classifier unavailable (" +
                                          classifier.diagnostic + ")";
                    }
                }
            }
            return {"completed", "approved model card " + approved_card_id +
                                      " covers model " + current_model_id +
                                      "; dataset content scan found no "
                                      "issues" + classifier_note};
        };
        const auto run_request_approval_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no model available to request deployment approval "
                        "for"};
            }
            try {
                const auto deployment = ml_deployments->create(
                    user_id, current_model_id,
                    pipeline.name + " (pipeline deployment request)",
                    "created by automation pipeline run", "requested",
                    "automated-pipeline");
                last_deployment_id = deployment.id;
                audit.append("ml.deployment.create", user_id, "success",
                            deployment.id);
                return {"completed", "deployment request " + deployment.id +
                                          " created in pending status"};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_deploy_stage =
            [&](const std::string& environment)
            -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed", "no model available to deploy"};
            }
            try {
                if (last_deployment_id.empty()) {
                    const auto deployment = ml_deployments->create(
                        user_id, current_model_id,
                        pipeline.name + " (" + environment + ")",
                        "created by automation pipeline run", environment,
                        "automated-pipeline");
                    last_deployment_id = deployment.id;
                    audit.append("ml.deployment.create", user_id, "success",
                                deployment.id);
                }
                if (!ml_deployments->set_status(last_deployment_id,
                                                DeploymentStatus::approved)) {
                    any_failed = true;
                    return {"failed", "deployment " + last_deployment_id +
                                           " no longer exists"};
                }
                audit.append("ml.deployment.status", user_id, "success",
                            last_deployment_id);
                return {"completed", "deployment " + last_deployment_id +
                                          " approved for " + environment};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_rollback_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (last_deployment_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no deployment recorded by this run to roll back "
                        "(run a Request approval or Deploy stage first)"};
            }
            if (!ml_deployments->set_status(last_deployment_id,
                                            DeploymentStatus::rejected)) {
                any_failed = true;
                return {"failed", "deployment " + last_deployment_id +
                                       " no longer exists"};
            }
            audit.append("ml.deployment.status", user_id, "success",
                        last_deployment_id);
            return {"completed", "deployment " + last_deployment_id +
                                      " approval revoked (rolled back)"};
        };
        const auto run_monitor_stage =
            [&]() -> std::pair<std::string, std::string> {
            return {"completed", build_ml_monitoring_json()};
        };
        // Phase 72: the six stages Phase 71 left "skipped" gain real
        // executors. "Import data" checks the dataset's uploaded content
        // actually exists (distinct from "Validate data", which parses its
        // structure). "Clean data" runs clean_tabular_csv (ml_engine.cpp)
        // and persists the cleaned CSV back over the dataset's content, so
        // later Train/Evaluate stages see the cleaned rows. "Label data"
        // looks for a real completed LabelTaskStore entry targeting this
        // dataset -- honestly still "skipped" when none exists, since this
        // codebase has no automated labeler. "Split data" runs
        // split_tabular_csv, which reuses train_tabular_model's own
        // deterministic split formula so the reported counts are exactly
        // what a Train model stage will use. "Optimize" creates a real
        // ModelOptimizationRun (operation "pruning"), runs
        // prune_tabular_model against the current model's real learned
        // weights, and persists the pruned weights back. "Staging tests"
        // reuses execute_evaluation_run (the same function "Evaluate
        // model" and the Evaluation Lab page call) against the pipeline's
        // dataset.
        const auto run_import_data_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured to import"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset " + pipeline.dataset_id +
                                       " has no uploaded content to import"};
            }
            return {"completed",
                    "dataset " + pipeline.dataset_id + " has " +
                        std::to_string(content->csv.size()) +
                        " byte(s) of uploaded content"};
        };
        const auto run_clean_data_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured to clean"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            try {
                const auto report = clean_tabular_csv(content->csv);
                ml_dataset_content->put(pipeline.dataset_id, report.csv,
                                        content->target_column);
                return {"completed",
                        std::to_string(report.rows_before) +
                            " row(s) before, " +
                            std::to_string(report.rows_after) +
                            " after; removed " +
                            std::to_string(report.blank_rows_removed) +
                            " blank and " +
                            std::to_string(report.duplicate_rows_removed) +
                            " duplicate row(s)"};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_label_data_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured for labeling"};
            }
            for (const auto& task : ml_label_tasks->list()) {
                if (task.dataset_id == pipeline.dataset_id &&
                    task.status == LabelTaskStatus::completed) {
                    return {"completed", "labeling task " + task.id +
                                              " is complete for this dataset"};
                }
            }
            // No completed labeling task exists yet: run the real heuristic
            // auto-labeler (auto_label_tabular_dataset, ml_engine.cpp)
            // rather than reporting "skipped" -- persists the filled-in CSV
            // back over the dataset's content and records a completed
            // LabelTaskStore entry so a later run of this same stage (or a
            // human reviewer opening the Labeling page) sees real, honestly
            // attributed work, not a placeholder.
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content to label"};
            }
            try {
                const auto report =
                    auto_label_tabular_dataset(content->csv, content->target_column);
                ml_dataset_content->put(pipeline.dataset_id, report.csv,
                                        content->target_column);
                const auto task = ml_label_tasks->create(
                    user_id, pipeline.dataset_id,
                    pipeline.name + " (automated labeling)",
                    report.method == "existing_labels_validated"
                        ? "all " + std::to_string(report.rows_total) +
                              " row(s) already had a target value; validated, "
                              "none invented"
                        : "heuristic quantile-binning against column \"" +
                              report.source_column + "\" (thresholds " +
                              std::to_string(report.low_medium_threshold) + " / " +
                              std::to_string(report.medium_high_threshold) + ")",
                    "automated-heuristic", user_id);
                ml_label_tasks->set_status(task.id, LabelTaskStatus::completed);
                audit.append("ml.label_task.create", user_id, "success", task.id);
                return {"completed",
                        "labeling task " + task.id + " (" + report.method +
                            "): " + std::to_string(report.rows_already_labeled) +
                            " already labeled, " +
                            std::to_string(report.rows_labeled) +
                            " labeled by the heuristic"};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_split_data_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed",
                        "pipeline has no dataset configured to split"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            try {
                const auto report =
                    split_tabular_csv(content->csv, content->target_column);
                return {"completed",
                        std::to_string(report.total_rows) + " row(s): " +
                            std::to_string(report.train_rows) + " train, " +
                            std::to_string(report.holdout_rows) + " holdout"};
            } catch (const std::exception& error) {
                any_failed = true;
                return {"failed", error.what()};
            }
        };
        const auto run_optimize_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed", "no model available to optimize"};
            }
            auto model = ml_trained_models->find(current_model_id);
            if (!model) {
                any_failed = true;
                return {"failed", "model " + current_model_id +
                                       " has not been trained yet"};
            }
            const auto run = ml_model_optimizations->create(
                user_id, current_model_id,
                pipeline.name + " (pipeline pruning)",
                "created by automation pipeline run", "pruning");
            try {
                const auto prune = prune_tabular_model(*model, 1e-3);
                ml_trained_models->put(*model);
                ml_model_optimizations->set_status(
                    run.id, ModelOptimizationStatus::completed);
                audit.append("ml.model_optimization.run", user_id, "success",
                            run.id);
                return {"completed",
                        "optimization run " + run.id + " pruned " +
                            std::to_string(prune.weights_pruned) + " of " +
                            std::to_string(prune.weights_total) +
                            " weight(s) below magnitude 0.001"};
            } catch (const std::exception& error) {
                any_failed = true;
                ml_model_optimizations->set_status(
                    run.id, ModelOptimizationStatus::failed);
                audit.append("ml.model_optimization.run", user_id, "failure",
                            run.id);
                return {"failed", error.what()};
            }
        };
        const auto run_staging_tests_stage =
            [&]() -> std::pair<std::string, std::string> {
            if (current_model_id.empty()) {
                any_failed = true;
                return {"failed",
                        "no model available for staging tests (run a Train "
                        "model stage first, or set the pipeline's model)"};
            }
            if (pipeline.dataset_id.empty()) {
                any_failed = true;
                return {"failed", "pipeline has no dataset configured for "
                                   "staging tests"};
            }
            const auto model = ml_trained_models->find(current_model_id);
            if (!model) {
                any_failed = true;
                return {"failed", "model " + current_model_id +
                                       " has not been trained yet"};
            }
            const auto content = ml_dataset_content->find(pipeline.dataset_id);
            if (!content) {
                any_failed = true;
                return {"failed", "dataset has no uploaded content"};
            }
            const auto eval_run = ml_evaluation_runs->create(
                user_id, current_model_id, pipeline.dataset_id,
                pipeline.name + " (pipeline staging tests)",
                "created by automation pipeline run", "");
            try {
                execute_evaluation_run(eval_run, *model, *content);
                audit.append("ml.evaluation_run.run", user_id, "success",
                            eval_run.id);
                return {"completed",
                        "staging evaluation run " + eval_run.id +
                            " completed; see GET "
                            "/api/v1/ml/evaluation-runs/" +
                            eval_run.id + "/result for metrics"};
            } catch (const std::exception& error) {
                any_failed = true;
                audit.append("ml.evaluation_run.run", user_id, "failure",
                            eval_run.id);
                return {"failed", error.what()};
            }
        };
        std::uint32_t completed = 0;
        for (const auto& stage : stage_names) {
            const auto lower = ascii_lower(stage);
            // Mark this stage as the one currently running before executing
            // it, so a poller sees "Running: <stage>" rather than the run
            // appearing frozen on whichever stage finished last.
            ml_automation_pipelines->append_stage_result(
                run_id, stage_results_json(), completed, "Running: " + stage);
            std::pair<std::string, std::string> outcome;
            if (lower == "train model") {
                outcome = run_train_stage();
            } else if (lower == "evaluate model") {
                outcome = run_evaluate_stage();
            } else if (lower == "validate data") {
                outcome = run_validate_data_stage();
            } else if (lower == "validate model") {
                outcome = run_validate_model_stage();
            } else if (lower == "safety tests") {
                outcome = run_safety_tests_stage();
            } else if (lower == "request approval") {
                outcome = run_request_approval_stage();
            } else if (lower == "deploy staging") {
                outcome = run_deploy_stage("staging");
            } else if (lower == "deploy production") {
                outcome = run_deploy_stage("production");
            } else if (lower == "rollback") {
                outcome = run_rollback_stage();
            } else if (lower == "monitor") {
                outcome = run_monitor_stage();
            } else if (lower == "import data") {
                outcome = run_import_data_stage();
            } else if (lower == "clean data") {
                outcome = run_clean_data_stage();
            } else if (lower == "label data") {
                outcome = run_label_data_stage();
            } else if (lower == "split data") {
                outcome = run_split_data_stage();
            } else if (lower == "optimize") {
                outcome = run_optimize_stage();
            } else if (lower == "staging tests") {
                outcome = run_staging_tests_stage();
            } else {
                outcome = {"skipped",
                           "no automated executor exists for this stage yet"};
            }
            stage_result_fragments.push_back(
                "{\"stage\":\"" + json_escape(stage) + "\",\"status\":\"" +
                outcome.first + "\",\"detail\":\"" + json_escape(outcome.second) +
                "\"}");
            ++completed;
            ml_automation_pipelines->append_stage_result(
                run_id, stage_results_json(), completed, "");
        }
        const auto overall = any_failed ? AutomationPipelineRunStatus::failed
                                         : AutomationPipelineRunStatus::completed;
        ml_automation_pipelines->finish_run(
            run_id, overall,
            any_failed
                ? "One or more pipeline stages failed; see stageResults."
                : "Pipeline run completed; see stageResults for the outcome "
                  "of each stage.");
        audit.append("ml.pipeline.run", user_id,
                     any_failed ? "failure" : "success", pipeline.id);
    }

    // Parses a first-admin setup body (setupToken/username/password/
    // displayName), wipes the raw JSON, and checks the one-time token.
    // Shared by setup() and setup_local(), which differ only in how they
    // verify/store the resulting credential.
    SetupFields parse_setup_fields(Request& request) {
        auto root = parse_json(request.body);
        std::fill(request.body.begin(), request.body.end(), '\0');
        request.body.clear();
        if (root.as_object().size() != 4U) {
            throw std::runtime_error("unexpected setup field");
        }
        const auto token = root.required("setupToken").as_string();
        auto username = root.required("username").as_string();
        auto password = root.required_mutable("password").take_string();
        auto display_name = root.required("displayName").as_string();
        {
            // Locked so the compare can never interleave with another
            // thread's setup_hash/setup_token clear (see setup()/
            // setup_local()), closing a TOCTOU window where two concurrent
            // requests both pass validation before either clears the token.
            std::lock_guard<std::mutex> lock(setup_mutex);
            if (!constant_time_equal(sha256_hex(token), setup_hash)) {
                std::fill(password.begin(), password.end(), '\0');
                throw InvalidSetupToken();
            }
        }
        return {std::move(username), std::move(password), std::move(display_name)};
    }

    // Shared by setup() and setup_local(): both parse the setup body, run
    // their own credential-specific body, and must fail the same way on a
    // stale/invalid setup token or any other malformed-request exception
    // (wiping the raw request body either way so a password never lingers
    // in memory longer than needed).
    template <typename Body>
    std::string run_setup_request(Request& request, Body&& body) {
        try {
            auto fields = parse_setup_fields(request);
            return body(fields);
        } catch (const InvalidSetupToken&) {
            audit.append("setup.first_admin", "anonymous", "denied",
                         "invalid setup token");
            return response(403, "Forbidden", "{\"error\":\"setup_token_invalid\"}");
        } catch (const std::exception&) {
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            return response(400, "Bad Request", "{\"error\":\"invalid_setup_request\"}");
        }
    }

    std::string setup(Request& request) {
        if (!configuration.allow_os_identity_accounts) {
            return response(403, "Forbidden", "{\"error\":\"os_accounts_disabled\"}");
        }
        if (!users->setup_required()) {
            return response(409, "Conflict", "{\"error\":\"setup_complete\"}");
        }
        return run_setup_request(request, [&](SetupFields& fields) {
            const auto result = OsIdentityProvider().authenticate(
                fields.username, fields.password);
            if (!result.authenticated()) {
                audit.append("setup.first_admin", fields.username, "denied",
                             result.diagnostic);
                return response(401, "Unauthorized",
                                "{\"error\":\"os_authentication_failed\"}");
            }
            const auto user = users->create_first_administrator(
                result.principal, fields.display_name);
            {
                std::lock_guard<std::mutex> lock(setup_mutex);
                setup_hash.clear();
                setup_token.clear();
            }
            audit.append("setup.first_admin", user.id, "success",
                         "administrator mapped to OS principal");
            return response(201, "Created", "{\"status\":\"configured\"}");
        });
    }

    // Creates the first administrator as a MasterAI-hashed local account
    // instead of an OS-verified one. Only reachable when the operator has
    // explicitly opted in via allowLocalPasswordAccounts (e.g. accounts with
    // no OS password, such as Windows Hello PIN-only sign-in).
    std::string setup_local(Request& request) {
        if (!configuration.allow_local_password_accounts) {
            return response(403, "Forbidden", "{\"error\":\"local_accounts_disabled\"}");
        }
        if (!users->setup_required()) {
            return response(409, "Conflict", "{\"error\":\"setup_complete\"}");
        }
        return run_setup_request(request, [&](SetupFields& fields) {
            if (fields.password.size() < 8U) {
                std::fill(fields.password.begin(), fields.password.end(), '\0');
                return response(400, "Bad Request", "{\"error\":\"password_too_short\"}");
            }
            const auto hash = hash_password(fields.password);
            const auto user = users->create_first_administrator(
                fields.username, fields.display_name, hash);
            {
                std::lock_guard<std::mutex> lock(setup_mutex);
                setup_hash.clear();
                setup_token.clear();
            }
            audit.append("setup.first_admin", user.id, "success",
                         "administrator created with a local password");
            return response(201, "Created", "{\"status\":\"configured\"}");
        });
    }

    std::string login(Request& request) {
        if (users->setup_required()) {
            return response(503, "Service Unavailable", "{\"error\":\"setup_required\"}");
        }
        try {
            auto root = parse_json(request.body);
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            if (root.as_object().size() != 2U) {
                throw std::runtime_error("unexpected login field");
            }
            const auto username = root.required("username").as_string();
            auto password = root.required_mutable("password").take_string();
            // Accounts with a stored local password hash are verified
            // locally; every other account only falls through to the OS when
            // OS-identity sign-in is explicitly enabled -- otherwise there is
            // nothing to authenticate against and this must not shell out to
            // the OS at all.
            const auto local_candidate = users->find_by_principal(username);
            std::optional<UserRecord> user;
            if (local_candidate && !local_candidate->password_hash.empty()) {
                if (verify_password(password, local_candidate->password_hash)) {
                    user = local_candidate;
                }
            } else if (configuration.allow_os_identity_accounts) {
                const auto result = OsIdentityProvider().authenticate(username, password);
                if (result.authenticated()) {
                    user = users->find_by_principal(result.principal);
                }
            }
            std::fill(password.begin(), password.end(), '\0');
            if (!user || !user->enabled) {
                audit.append("auth.login", username, "denied",
                             "authentication or principal mapping failed");
                return response(401, "Unauthorized",
                                "{\"error\":\"invalid_credentials\"}");
            }
            const auto token = sessions->create(
                user->id, epoch_seconds(), configuration.session_minutes * 60U);
            const auto session = sessions->validate(token, epoch_seconds());
            audit.append("auth.login", user->id, "success", "session issued");
            return response(200, "OK",
                            "{\"csrfToken\":\"" + session->csrf_secret + "\"}",
                            {session_cookie(token)});
        } catch (const std::exception&) {
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            return response(400, "Bad Request", "{\"error\":\"invalid_login_request\"}");
        }
    }

    std::string list_users(const UserRecord& admin) const {
        if (auto denied = forbidden_unless(admin.role, "users.manage")) return *denied;
        std::string body{"{\"users\":["};
        bool first = true;
        for (const auto& item : users->all()) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(item.id) + "\",\"username\":\"" +
                    json_escape(item.os_principal) + "\",\"displayName\":\"" +
                    json_escape(item.display_name) + "\",\"role\":\"" +
                    role(item.role) + "\",\"enabled\":" +
                    (item.enabled ? "true" : "false") + ",\"accountType\":\"" +
                    (item.password_hash.empty() ? "os" : "local") + "\"}";
        }
        return response(200, "OK", body + "]}");
    }

    // Administrator-only creation of an additional locally stored password
    // account. Distinct from setup_local(), which only ever creates the
    // first administrator.
    std::string create_user(Request& request, const UserRecord& admin) {
        if (!configuration.allow_local_password_accounts) {
            return response(403, "Forbidden", "{\"error\":\"local_accounts_disabled\"}");
        }
        if (auto denied = forbidden_unless(admin.role, "users.manage")) return *denied;
        try {
            auto root = parse_json(request.body);
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            if (root.as_object().size() != 4U) {
                throw std::runtime_error("unexpected user field");
            }
            const auto username = root.required("username").as_string();
            const auto display_name = root.required("displayName").as_string();
            const auto requested_role = parse_role_value(root.required("role").as_string());
            auto password = root.required_mutable("password").take_string();
            if (password.size() < 8U) {
                std::fill(password.begin(), password.end(), '\0');
                return response(400, "Bad Request", "{\"error\":\"password_too_short\"}");
            }
            const auto hash = hash_password(password);
            const auto created = users->add(username, display_name, requested_role, hash);
            // Phase 17: a new user changes what "authorized" means, so every
            // cached entry -- however it was keyed -- must stop being
            // reachable.
            cache->invalidate_policy();
            audit.append("users.create", admin.id, "success", created.id);
            return response(201, "Created", "{\"id\":\"" + created.id + "\"}");
        } catch (const std::exception&) {
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            return response(400, "Bad Request", "{\"error\":\"invalid_user_request\"}");
        }
    }

    // Phase 30A: administrator-only view of the live settings.json this
    // process started from (the same document `masterai configure` writes),
    // so the accelerator policy and other local-configuration knobs the rest
    // of Phase 30A added can be read and changed from the web UI instead of
    // requiring shell access to the host. Never exposed to developer/viewer
    // roles -- see the Admin/Settings sidebar in web_ui.cpp.
    std::string admin_config_get(const UserRecord& admin) const {
        if (auto denied = forbidden_unless(admin.role, "settings.manage")) return *denied;
        if (settings_file.empty()) {
            return response(503, "Service Unavailable",
                            "{\"error\":\"admin_configuration_unavailable\"}");
        }
        return response(200, "OK", ConfigurationManager::serialize(configuration));
    }

    // Accepts a full settings.json-shaped document (the exact shape
    // admin_config_get() above returns), validates and durably persists it
    // via the same ConfigurationManager::save_atomic() `masterai configure`
    // uses, then applies the subset of fields every live request path
    // already re-reads straight from `configuration` (accelerator policy,
    // reply/context length, retrieval and cache toggles, session reuse,
    // rate/body limits, allow-lists, local/OS sign-in toggles, the runner
    // generation stall timeout) so those take effect immediately. Every
    // other field -- host/port/TLS, storage roots,
    // memory ceilings baked into MemoryBudgetManager at construction, the
    // runner executable/port, and anything else only ever read once at
    // server startup -- is saved for the next restart and reported back in
    // "restartRequired" rather than silently pretended to be live.
    std::string admin_config_put(Request& request, const UserRecord& admin) {
        if (auto denied = forbidden_unless(admin.role, "settings.manage")) return *denied;
        if (settings_file.empty()) {
            return response(503, "Service Unavailable",
                            "{\"error\":\"admin_configuration_unavailable\"}");
        }
        AppConfig incoming;
        try {
            const auto pending_path = settings_file.string() + ".pending";
            {
                std::ofstream output(pending_path,
                                     std::ios::binary | std::ios::trunc);
                output.write(
                    request.body.data(),
                    static_cast<std::streamsize>(request.body.size()));
                output.flush();
                if (!output) {
                    throw std::runtime_error(
                        "could not stage the submitted configuration");
                }
            }
            try {
                incoming = ConfigurationManager::load(pending_path);
            } catch (...) {
                std::filesystem::remove(pending_path);
                throw;
            }
            std::filesystem::remove(pending_path);
        } catch (const std::exception& error) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_configuration\",\"detail\":\"" +
                                json_escape(error.what()) + "\"}");
        }
        ConfigurationManager::save_atomic(incoming, settings_file);
        const AppConfig previous = configuration;
        configuration.accelerator_policy = incoming.accelerator_policy;
        configuration.chat_context_length = incoming.chat_context_length;
        configuration.chat_max_reply_tokens = incoming.chat_max_reply_tokens;
        configuration.retrieval_enabled = incoming.retrieval_enabled;
        configuration.retrieval_deadline_milliseconds =
            incoming.retrieval_deadline_milliseconds;
        configuration.retrieval_maximum_context_bytes =
            incoming.retrieval_maximum_context_bytes;
        configuration.retrieval_maximum_chunks_per_source =
            incoming.retrieval_maximum_chunks_per_source;
        configuration.retrieval_maximum_total_chunks =
            incoming.retrieval_maximum_total_chunks;
        configuration.cache_enabled = incoming.cache_enabled;
        configuration.session_reuse_enabled = incoming.session_reuse_enabled;
        configuration.performance_auto_tune = incoming.performance_auto_tune;
        configuration.allow_local_password_accounts =
            incoming.allow_local_password_accounts;
        configuration.allow_os_identity_accounts =
            incoming.allow_os_identity_accounts;
        configuration.rate_limit_per_minute = incoming.rate_limit_per_minute;
        configuration.max_request_bytes = incoming.max_request_bytes;
        configuration.allowed_hosts = incoming.allowed_hosts;
        configuration.allowed_origins = incoming.allowed_origins;
        configuration.runner_stall_timeout_seconds =
            incoming.runner_stall_timeout_seconds;
        // Phase 30A: an accelerator-policy change must retire the stale
        // CalibrationService immediately -- otherwise resolve()/calibrate()
        // would keep validating against the policy this process started
        // with (see CalibrationService::accelerator_policy_, fixed at
        // construction) even though ensure_model_loaded() itself now reads
        // the new configuration.accelerator_policy on every load, which
        // would surface as a confusing build_launch_spec() rejection instead
        // of the new policy actually taking effect.
        if (inference != nullptr && tuning_profiles != nullptr &&
            previous.accelerator_policy != configuration.accelerator_policy) {
            const auto hardware = probe_hardware(configuration.runtime_root);
            calibration = std::make_unique<CalibrationService>(
                *inference, *tuning_profiles, hardware,
                sha256_file_hex(configuration.llama_server_executable),
                "masterai-0.1.0", configuration.accelerator_policy);
        }
        const bool restart_required =
            previous.host != incoming.host || previous.port != incoming.port ||
            previous.tls_mode != incoming.tls_mode ||
            previous.tls_certificate_file != incoming.tls_certificate_file ||
            previous.tls_private_key_file != incoming.tls_private_key_file ||
            previous.allow_intranet != incoming.allow_intranet ||
            previous.runtime_root != incoming.runtime_root ||
            previous.models_root != incoming.models_root ||
            previous.page_file_root != incoming.page_file_root ||
            previous.memory_reserve_mib != incoming.memory_reserve_mib ||
            previous.memory_hard_limit_mib != incoming.memory_hard_limit_mib ||
            previous.minimum_free_ram_percent !=
                incoming.minimum_free_ram_percent ||
            previous.critical_memory_percent !=
                incoming.critical_memory_percent ||
            previous.resource_profile != incoming.resource_profile ||
            previous.llama_server_executable !=
                incoming.llama_server_executable ||
            previous.runner_port != incoming.runner_port ||
            previous.runner_startup_timeout_seconds !=
                incoming.runner_startup_timeout_seconds ||
            previous.curl_executable != incoming.curl_executable ||
            previous.watch_project_files != incoming.watch_project_files ||
            previous.session_reuse_max_slots !=
                incoming.session_reuse_max_slots ||
            previous.session_reuse_idle_retention_seconds !=
                incoming.session_reuse_idle_retention_seconds ||
            previous.max_concurrent_connections !=
                incoming.max_concurrent_connections ||
            previous.request_timeout_seconds !=
                incoming.request_timeout_seconds ||
            previous.cache_maximum_bytes_per_category !=
                incoming.cache_maximum_bytes_per_category ||
            previous.session_minutes != incoming.session_minutes;
        audit.append("admin.config.update", admin.id, "success",
                    settings_file.string());
        return response(
            200, "OK",
            "{\"configuration\":" + ConfigurationManager::serialize(configuration) +
                ",\"restartRequired\":" +
                (restart_required ? "true" : "false") + "}");
    }

    std::string list_chats(const UserRecord& user) const {
        std::string body{"{\"chats\":["};
        bool first = true;
        for (const auto& chat : chats->list_for_owner(user.id)) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(chat.id) +
                    "\",\"projectId\":\"" + json_escape(chat.project_id) +
                    "\",\"modelId\":\"" + json_escape(chat.model_id) +
                    "\",\"title\":\"" + json_escape(chat.title) +
                    "\",\"createdAtEpochSeconds\":" +
                    std::to_string(chat.created_at_epoch_seconds) +
                    ",\"messageCount\":" +
                    std::to_string(chat.messages.size()) + "}";
        }
        return response(200, "OK", body + "]}");
    }

    // Lists only the authenticated user's remembered details. Memory reuses
    // the established chats.read/chats.write policy because it is part of
    // that user's chat state rather than an administrator-owned resource.
    std::string list_user_memories(const UserRecord& user) const {
        std::string body{"{\"memories\":["};
        bool first = true;
        for (const auto& record : user_memories->list_for_owner(user.id)) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(record.id) +
                    "\",\"content\":\"" + json_escape(record.content) +
                    "\",\"source\":\"" + json_escape(record.source) +
                    "\",\"createdAtEpochSeconds\":" +
                    std::to_string(record.created_at_epoch_seconds) + "}";
        }
        return response(200, "OK", body + "]}");
    }

    // Adds a manual detail outside the chat-command path so the browser can
    // offer a small, inspectable memory manager without duplicating storage
    // or normalization rules in JavaScript.
    std::string create_user_memory(Request& request, const UserRecord& user) {
        try {
            const auto root = parse_json(request.body);
            if (root.as_object().size() != 1U) {
                throw std::runtime_error("unexpected memory field");
            }
            const auto record = user_memories->add(
                user.id, root.required("content").as_string(), "manual");
            audit.append("chat.memory_create", user.id, "success", record.id);
            return response(
                201, "Created",
                "{\"id\":\"" + json_escape(record.id) +
                    "\",\"content\":\"" + json_escape(record.content) +
                    "\",\"source\":\"" + json_escape(record.source) +
                    "\",\"createdAtEpochSeconds\":" +
                    std::to_string(record.created_at_epoch_seconds) + "}");
        } catch (const std::exception&) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_memory_request\"}");
        }
    }

    std::string delete_user_memory(Request& request, const UserRecord& user) {
        const std::string prefix{"/api/v1/memories/"};
        const auto memory_id = request.target.substr(
            prefix.size(), request.target.size() - prefix.size() - 7U);
        if (!user_memories->remove(memory_id, user.id)) {
            return response(404, "Not Found",
                            "{\"error\":\"memory_not_found\"}");
        }
        audit.append("chat.memory_delete", user.id, "success", memory_id);
        return response(200, "OK", "{\"deleted\":true}");
    }

    // Full message history for one chat, used by the sidebar to render a
    // conversation when the operator selects it from the chat-history list.
    // ChatStore already keeps every message in memory (see send_chat_message
    // below, which is the only prior caller of find_for_owner()); this just
    // exposes it over GET.
    std::string get_chat_messages(Request& request, const UserRecord& user) const {
        const std::string prefix{"/api/v1/chats/"};
        const auto chat_id = request.target.substr(prefix.size());
        const auto chat = chats->find_for_owner(chat_id, user.id);
        if (!chat) {
            return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
        }
        const auto role_name = [](const ChatRole role) {
            switch (role) {
                case ChatRole::user: return "user";
                case ChatRole::assistant: return "assistant";
                case ChatRole::system: return "system";
            }
            return "user";
        };
        std::string body{"{\"id\":\"" + json_escape(chat->id) +
                         "\",\"projectId\":\"" + json_escape(chat->project_id) +
                         "\",\"modelId\":\"" + json_escape(chat->model_id) +
                         "\",\"messages\":["};
        bool first = true;
        for (const auto& message : chat->messages) {
            if (!first) body += ",";
            first = false;
            body += "{\"role\":\"" + std::string(role_name(message.role)) +
                    "\",\"content\":\"" + json_escape(message.content) +
                    "\",\"createdAtEpochSeconds\":" +
                    std::to_string(message.created_at_epoch_seconds) +
                    ",\"tokenCount\":" +
                    std::to_string(message.token_count) + "}";
        }
        // Deliberately NOT warm_model_async() here: this fires mid-page-load
        // (openChat() is called from load(), before the browser has
        // finished painting the chat history this very response carries, or
        // the models dropdown from the request racing it), so starting a
        // multi-gigabyte cold load right now competes with the page still
        // rendering. The client instead calls POST /api/v1/runner/warm of
        // its own accord once the whole page -- history, model list,
        // sidebar -- is actually up on screen.
        return response(200, "OK", body + "]}");
    }

    // Explicit, client-driven counterpart to the warm_model_async() calls in
    // create_chat()/set_chat_model() below (which fire from a direct user
    // action, so warming immediately is correct there). This one exists so
    // the web UI can defer warming a chat's model until its own load()
    // sequence has actually finished rendering everything -- see the
    // comment on get_chat_messages() above for why that GET no longer
    // triggers it itself.
    std::string warm_runner(Request& request, const UserRecord&) const {
        try {
            const auto root = parse_json(request.body);
            const auto model_id = root.required("modelId").as_string();
            if (find_model(model_id)) warm_model_async(model_id);
        } catch (const std::exception&) {
            // Best-effort: a malformed body or unknown model just means
            // nothing gets warmed, not a request failure the UI needs to
            // surface -- the first real message still loads it synchronously.
        }
        return response(202, "Accepted", "{}");
    }

    std::string create_chat(Request& request, const UserRecord& user) {
        try {
            const auto root = parse_json(request.body);
            if (root.as_object().size() != 2U) {
                throw std::runtime_error("unexpected chat field");
            }
            const auto project_id = root.required("projectId").as_string();
            const auto model_id = root.required("modelId").as_string();
            if (!projects->find(project_id)) {
                return response(404, "Not Found",
                                "{\"error\":\"project_not_found\"}");
            }
            if (!is_model_ready(model_id)) {
                return response(409, "Conflict",
                                "{\"error\":\"model_not_ready\"}");
            }
            // Phase 61 performs the one durable-memory read at conversation
            // start. The bounded snapshot is stored with the chat and reused
            // by every turn; later memory changes apply to newly created
            // chats instead of repeatedly changing an active conversation.
            const auto memory_context =
                user_memories->recall_context(user.id, 4096U);
            const auto chat = chats->create(
                user.id, project_id, model_id, memory_context);
            audit.append("chat.create", user.id, "success", chat.id);
            warm_model_async(model_id);
            return response(201, "Created",
                            "{\"id\":\"" + json_escape(chat.id) + "\"}");
        } catch (const std::exception&) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_chat_request\"}");
        }
    }

    // Switches the model a chat's future messages generate against. Kept
    // separate from create_chat() -- this route can't create a new chat, and
    // it looks the target chat up by ownership (chats->set_model()) rather
    // than trusting a client-supplied id blindly.
    std::string set_chat_model(Request& request, const UserRecord& user) {
        const std::string prefix{"/api/v1/chats/"};
        const auto chat_id = request.target.substr(
            prefix.size(), request.target.size() - prefix.size() - 6U);
        try {
            const auto root = parse_json(request.body);
            const auto model_id = root.required("modelId").as_string();
            const auto chat = chats->find_for_owner(chat_id, user.id);
            if (!chat) {
                return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
            }
            if (!is_model_ready(model_id)) {
                return response(409, "Conflict",
                                "{\"error\":\"model_not_ready\"}");
            }
            if (!chats->set_model(chat_id, user.id, model_id)) {
                return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
            }
            audit.append("chat.model_change", user.id, "success", chat_id);
            warm_model_async(model_id);
            return response(200, "OK", "{\"modelId\":\"" + json_escape(model_id) + "\"}");
        } catch (const std::exception&) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_chat_request\"}");
        }
    }

    // Deletes a chat and its full message history. Ownership is re-checked
    // by chats->remove() itself (not just trusted from the URL), matching
    // find_for_owner()'s pattern everywhere else in this file.
    std::string delete_chat(Request& request, const UserRecord& user) {
        const std::string prefix{"/api/v1/chats/"};
        const auto chat_id = request.target.substr(
            prefix.size(), request.target.size() - prefix.size() - 7U);
        if (!chats->remove(chat_id, user.id)) {
            return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
        }
        prompt_sessions->release(chat_id);
        audit.append("chat.delete", user.id, "success", chat_id);
        return response(200, "OK", "{\"id\":\"" + json_escape(chat_id) + "\"}");
    }

    // Scans the model registry once for a single manifest id -- shared by
    // ensure_model_loaded() (which needs the full record to launch the
    // runner) and the chat-template lookup below (which only needs the
    // architecture string), so both stay consistent about what "the chat's
    // model" resolves to.
    std::optional<ModelRecord> find_model(const std::string& model_id) const {
        const auto hardware = probe_hardware(configuration.models_root);
        for (auto& model :
             ModelRegistry(configuration.models_root, hardware,
                           configuration.memory_reserve_mib).scan()) {
            if (model.manifest.id == model_id) return model;
        }
        return std::nullopt;
    }

    // Phase 61 resolves VectorStore::embedding_model into one of exactly two
    // executor paths. The authored method stays dependency-free; any other
    // value must be the id of a verified GGUF in the dedicated embeddings
    // category and is executed only by the existing loopback llama.cpp
    // supervisor. The lambda rechecks the loaded model for every chunk so a
    // chat request cannot silently make vectors with a different model.
    KnowledgeEmbeddingFunction knowledge_vectorizer(
        const std::string& embedding_model) const {
        if (embedding_model == "authored_hashing_vectorizer_v1") return {};
        if (inference == nullptr) {
            throw std::runtime_error(
                "the learned embedding backend is not configured");
        }
        const auto model = find_model(embedding_model);
        if (!model || model->state != ModelState::ready ||
            model->manifest.category != "embeddings-code-search") {
            throw std::runtime_error(
                "embedding model must be a verified ready model in the "
                "embeddings-code-search category");
        }
        return [this, embedding_model](const std::string& text) {
            // Phase 33 (LOCAL-ONLY slice): prefer a dedicated pool runner
            // whose capabilities include "embedding" (e.g. a runner set
            // aside so bulk knowledge-indexing embedding calls do not
            // compete with the default runner's own chat generations);
            // falls back to the default `inference` supervisor exactly as
            // before whenever no pool is configured or the pool runner
            // fails. Embedding responses are never partial (one call
            // either returns one complete vector or throws), so this
            // fallback is always semantically safe -- no idempotency check
            // is needed here the way generate() requires one.
            const auto active =
                select_and_warm_pool_runner(embedding_model, std::string{},
                                            "embedding");
            EmbeddingResult result;
            if (!active.empty()) {
                try {
                    result = runner_pool->embed(active, text);
                } catch (...) {
                    ensure_model_loaded(embedding_model);
                    result = inference->embed(text);
                }
            } else {
                ensure_model_loaded(embedding_model);
                result = inference->embed(text);
            }
            if (result.model_id != embedding_model) {
                throw std::runtime_error(
                    "embedding runner model changed during execution");
            }
            return result.values;
        };
    }

    // Shared by create_chat() and set_chat_model(): both must reject a
    // chat-model assignment unless the target model is actually Ready,
    // so neither route accepts a model id that would fail to load.
    bool is_model_ready(const std::string& model_id) const {
        const auto model = find_model(model_id);
        return model && model->state == ModelState::ready;
    }

    // Phase 68: the Machine Learning monitoring aggregation -- a live
    // probe_hardware() snapshot, real training-job status counts, every
    // completed evaluation run's genuinely measured metrics, and real
    // prompt/generation throughput from actual BenchmarkStore runs.
    // Phase 71 factored this out of the GET /api/v1/ml/monitoring route so
    // an Automation Pipeline "Monitor" stage can attach the identical,
    // genuinely measured snapshot to its stage result instead of a second
    // copy of the same query logic. Phase 78 adds real per-request
    // latency/queue-depth/throughput telemetry (ml_inference_metrics,
    // fed by every real generation call site) -- still deliberately
    // missing: live per-step training curves (the tabular trainer's runs
    // are synchronous and complete before there is a meaningful "live"
    // window to sample one from) and cache-hit rate (no KV-cache-hit
    // instrumentation exists in the inference adapter to report on).
    std::string build_ml_monitoring_json() const {
        const auto hardware = probe_hardware(configuration.models_root);
        std::map<std::string, std::uint64_t> job_counts;
        for (const auto& job : ml_training_jobs->list()) {
            ++job_counts[training_job_status_name(job.status)];
        }
        std::string job_counts_json = "{";
        {
            bool first = true;
            for (const auto& entry : job_counts) {
                if (!first) job_counts_json += ",";
                first = false;
                job_counts_json += "\"" + json_escape(entry.first) +
                                   "\":" + std::to_string(entry.second);
            }
        }
        job_counts_json += "}";
        std::string evaluations_json = "[";
        {
            bool first = true;
            for (const auto& run : ml_evaluation_runs->list()) {
                if (run.status != EvaluationRunStatus::completed) continue;
                const auto metrics_json = ml_evaluation_results->find(run.id);
                if (!metrics_json) continue;
                if (!first) evaluations_json += ",";
                first = false;
                evaluations_json +=
                    "{\"runId\":\"" + json_escape(run.id) +
                    "\",\"name\":\"" + json_escape(run.name) +
                    "\",\"modelId\":\"" + json_escape(run.model_id) +
                    "\",\"datasetId\":\"" + json_escape(run.dataset_id) +
                    "\",\"metrics\":" + *metrics_json + "}";
            }
        }
        evaluations_json += "]";
        std::string benchmarks_json = "[";
        {
            bool first = true;
            for (const auto& record : benchmarks->all()) {
                if (!first) benchmarks_json += ",";
                first = false;
                const double elapsed_seconds =
                    static_cast<double>(record.elapsed_microseconds) /
                    1'000'000.0;
                const double prompt_tokens_per_second =
                    elapsed_seconds > 0.0
                        ? static_cast<double>(record.prompt_tokens) /
                              elapsed_seconds
                        : 0.0;
                const double generation_tokens_per_second =
                    elapsed_seconds > 0.0
                        ? static_cast<double>(record.generated_tokens) /
                              elapsed_seconds
                        : 0.0;
                const char* profile_name = "quick";
                switch (record.profile) {
                    case BenchmarkProfile::quick: profile_name = "quick"; break;
                    case BenchmarkProfile::standard: profile_name = "standard"; break;
                    case BenchmarkProfile::extended: profile_name = "extended"; break;
                }
                benchmarks_json +=
                    "{\"modelId\":\"" + json_escape(record.model_id) +
                    "\",\"profile\":\"" + profile_name +
                    "\",\"promptTokensPerSecond\":" +
                    std::to_string(prompt_tokens_per_second) +
                    ",\"generationTokensPerSecond\":" +
                    std::to_string(generation_tokens_per_second) +
                    ",\"passedCases\":" +
                    std::to_string(record.passed_cases) +
                    ",\"totalCases\":" +
                    std::to_string(record.total_cases) + "}";
            }
        }
        benchmarks_json += "]";
        // Phase 78: real per-request latency/queue-depth telemetry, fed by
        // every real generation call site (chat, RAG generation, inference
        // endpoints) -- see InferenceMetricsStore's class comment.
        const auto inference_metrics_snapshot = ml_inference_metrics.snapshot();
        // Phase 78 (this pass): every tabular training job currently
        // in-flight, each with its own real live epoch/loss, not a single
        // "is anything training" boolean -- see TrainingProgressTracker's
        // class comment.
        std::string live_training_json = "[";
        {
            bool first = true;
            for (const auto& job_id : ml_training_progress.active_job_ids()) {
                if (!first) live_training_json += ",";
                first = false;
                const auto snapshot = ml_training_progress.snapshot(job_id);
                live_training_json += "{\"jobId\":" + json_string(job_id) +
                                      ",\"progress\":" +
                                      training_progress_snapshot_json(snapshot) + "}";
            }
        }
        live_training_json += "]";
        return "{\"systemResources\":" + hardware_info_json(hardware) +
               ",\"trainingJobCounts\":" + job_counts_json +
               ",\"evaluationMetrics\":" + evaluations_json +
               ",\"inferenceBenchmarks\":" + benchmarks_json +
               ",\"inferenceRequests\":" +
               inference_metrics_json(inference_metrics_snapshot) +
               ",\"liveTrainingProgress\":" + live_training_json + "}";
    }

    // Best-effort pre-warm: starts the same load ensure_model_loaded() would
    // do on the first chat message, but fires the moment a chat is opened,
    // created, or its model changes, so the runner is already cold-loading
    // by the time the user finishes typing instead of only starting once
    // they hit send. Detached and exception-swallowing on purpose -- this is
    // strictly a head start, never the thing a caller depends on for
    // correctness: send_chat_message()'s own ensure_model_loaded() call
    // still runs synchronously and is what actually surfaces a real load
    // failure to the user.
    void warm_model_async(const std::string& model_id) const {
        if (inference == nullptr) return;
        bool expected = false;
        if (!model_warm_in_progress.compare_exchange_strong(
                expected, true, std::memory_order_acq_rel)) {
            return;
        }
        std::thread([this, model_id]() {
            try {
                ensure_model_loaded(model_id);
            } catch (const std::exception& warm_exception) {
                // Best-effort warmup: send_chat_message()'s own synchronous
                // ensure_model_loaded() call is what actually surfaces a real
                // load failure to the user, so this is only logged, not
                // rethrown.
                std::cerr << "warm_model_async: failed to warm model '"
                          << model_id << "': " << warm_exception.what()
                          << std::endl;
            } catch (...) {
                std::cerr << "warm_model_async: failed to warm model '"
                          << model_id << "': unknown exception" << std::endl;
            }
            model_warm_in_progress.store(false, std::memory_order_release);
        }).detach();
    }

    // Releases whatever runner_weights lease admit_runner_weights() last
    // granted, if any. Safe to call even when nothing is currently held
    // (MemoryBudgetManager::release() is itself a no-op on an unknown/empty
    // lease id) -- every caller below invokes this unconditionally rather
    // than tracking whether a lease is outstanding.
    void release_runner_weights_lease() const {
        std::lock_guard<std::mutex> lock(runner_admission_mutex);
        if (!runner_weights_lease_id.empty()) {
            memory->release(runner_weights_lease_id);
            runner_weights_lease_id.clear();
        }
    }

    // Phase 30A CPU-only admission estimator (docs/PLAN.md Phase 30A,
    // deliverable 2 / implementation-order item 2): a model that would push
    // resident/committed memory past the configured ceiling or OS reserve is
    // rejected -- with a concrete, actionable reason -- before the runner
    // process is ever started, rather than discovered only after llama.cpp
    // has already mapped/loaded gigabytes of weights. This does not add a
    // second memory authority: it is one more MemoryCategory::runner_weights
    // reservation against the same MemoryBudgetManager every other category
    // (compute_buffers, kv_cache, ...) already reserves against, held for as
    // long as this model stays loaded and released by
    // release_runner_weights_lease() above on every unload path.
    void admit_runner_weights(const ModelRecord& model,
                              unsigned int parallel_slots,
                              const ModelRecord* speculative_draft = nullptr) const {
        const auto hardware = probe_hardware(configuration.models_root);
        const auto suitability = assess_model(
            model.manifest, hardware, configuration.memory_reserve_mib);
        if (suitability.rating == Suitability::unsupported) {
            throw std::runtime_error(
                "model rejected before load: " + suitability.reason);
        }
        MemoryEstimate estimate;
        estimate.weights_bytes = model.manifest.model_size_bytes;
        // Phase 32: when a draft model is about to be launched alongside
        // this target (dual-model speculative decoding), its weights are a
        // second, fully separate resident allocation in the same runner
        // process -- added here rather than estimated separately so the one
        // real MemoryBudgetManager reservation below is the actual combined-
        // memory-fit check, not a second parallel heuristic that could
        // disagree with it.
        if (speculative_draft != nullptr) {
            estimate.weights_bytes += speculative_draft->manifest.model_size_bytes;
        }
        // Backend graph/compute-buffer and tokenizer-state overhead: a
        // conservative flat heuristic (same order of magnitude as the 64 MiB
        // per-request compute_buffers estimate in send_chat_message(), just
        // scaled up for the larger one-time backend init allocation) pending
        // the real-model measurement docs/PLAN.md Phase 30A's benchmark
        // matrix deliverable calls for.
        estimate.runtime_buffer_bytes = 256ULL * 1024ULL * 1024ULL;
        // Matches the KV-per-slot figure send_chat_message() already
        // estimates for MemoryCategory::compute_buffers, since llama.cpp
        // reserves this much KV cache for every --parallel slot at
        // model-load time regardless of how many slots later see traffic.
        estimate.kv_bytes_per_sequence = 128ULL * 1024ULL * 1024ULL;
        estimate.sequences = parallel_slots;
        estimate.safety_margin_bytes = 32ULL * 1024ULL * 1024ULL;
        std::lock_guard<std::mutex> lock(runner_admission_mutex);
        if (!runner_weights_lease_id.empty()) {
            memory->release(runner_weights_lease_id);
            runner_weights_lease_id.clear();
        }
        const auto admission =
            memory->reserve(MemoryCategory::runner_weights, estimate, true);
        if (!admission.admitted) {
            std::string message = admission.diagnostic;
            if (!suitability.reason.empty()) {
                message += " (" + suitability.reason + ")";
            }
            throw std::runtime_error(message);
        }
        runner_weights_lease_id = admission.lease_id;
    }

    void ensure_model_loaded(const std::string& model_id) const {
        // A background pre-warm and the first foreground message can reach
        // this boundary at the same time. The first caller owns the bounded
        // load; later callers wait here, then re-read the final runner state
        // instead of rejecting RunnerState::starting as "not available".
        // This also prevents two load attempts from racing the single runner
        // and its one authoritative memory lease.
        std::lock_guard<std::mutex> load_lock(model_load_mutex);
        const auto current = inference->metrics();
        if ((current.state == RunnerState::ready ||
             current.state == RunnerState::busy) &&
            current.model_id == model_id) {
            return;
        }
        if (current.state != RunnerState::unloaded &&
            current.state != RunnerState::failed &&
            current.state != RunnerState::ready) {
            throw std::runtime_error("inference runner is not available");
        }
        if (current.state == RunnerState::ready ||
            current.state == RunnerState::failed) {
            inference->unload();
            // Every slot's KV cache lives inside the runner process that
            // just went away, so any session state referencing it is
            // meaningless (and its slot ids may now collide with a
            // differently-loaded model's slots).
            prompt_sessions->reset();
            release_runner_weights_lease();
        }
        if (const auto model = find_model(model_id)) {
            // A live chat load always asks for a GPU-offload recommendation
            // (see select_gpu_layers()) even with no persisted calibration
            // profile for this host/model pair yet -- resolve() computes
            // recommended_gpu_layers from the model's real size and the
            // host's detected VRAM regardless of whether a full calibration
            // run has ever measured anything else, so a compatible GPU gets
            // used for model weights from the very first load rather than
            // only after an administrator runs /api/v1/performance/calibrate.
            LaunchTuning tuning;
            if (calibration != nullptr) {
                const auto profile = calibration->resolve(
                    model->manifest.model_sha256, "balanced", nullptr, 0U,
                    model->manifest.model_size_bytes,
                    model->manifest.required_gpu_backend);
                tuning = launch_tuning_from_profile(profile);
            }
            tuning.continuous_batching =
                advanced_optimizations != nullptr &&
                advanced_optimizations->is_enabled("continuous_batching");
            const unsigned int parallel_slots =
                configuration.session_reuse_enabled
                    ? configuration.session_reuse_max_slots
                    : 1U;
            // Phase 32: the real dual-model (draft + target) launch path.
            // Gated by AdvancedOptimizationRegistry exactly like
            // continuous_batching above -- an administrator must have
            // already recorded real evidence and admitted
            // "speculative_decoding" (see optimization_registry.cpp) before
            // this ever does anything, so nothing self-enables.
            std::optional<ModelRecord> speculative_draft;
            if (advanced_optimizations != nullptr &&
                advanced_optimizations->is_enabled("speculative_decoding")) {
                std::vector<ModelRecord> ready_models;
                for (auto& candidate :
                     ModelRegistry(configuration.models_root,
                                   probe_hardware(configuration.models_root),
                                   configuration.memory_reserve_mib)
                         .scan()) {
                    if (candidate.state == ModelState::ready) {
                        ready_models.push_back(std::move(candidate));
                    }
                }
                speculative_draft = select_speculative_draft_candidate(
                    model->manifest, ready_models);
                if (speculative_draft.has_value()) {
                    SpeculativeDecodingRequestContext context;
                    context.measured_acceptance_rate =
                        speculative_pair_evidence != nullptr
                            ? speculative_pair_evidence->lookup(
                                  model->manifest.model_sha256,
                                  speculative_draft->manifest.model_sha256)
                            : std::nullopt;
                    context.combined_memory_estimate_bytes =
                        model->manifest.model_size_bytes +
                        speculative_draft->manifest.model_size_bytes;
                    context.available_memory_bytes =
                        memory->sample().available_physical_bytes;
                    // This decision runs once at model-load time, before any
                    // specific chat message's own sampling/max_tokens are
                    // known (a chat session's runner is loaded once, not
                    // re-launched per message -- see LaunchTuning::
                    // speculative_draft_model_file's class comment).
                    // configuration.chat_max_reply_tokens stands in for "a
                    // normal-length reply". sampling_supported_by_
                    // speculative_verification is accurately true, not a
                    // placeholder: llama.cpp's own speculative-decoding
                    // verification is the general rejection-sampling
                    // algorithm, valid for any temperature/top-p/top-k/
                    // repeat-penalty sampling (every live chat request's
                    // actual settings), and GenerationOptions (masterai.hpp)
                    // exposes no grammar/logit-bias field that could make a
                    // request incompatible -- see that field's own class
                    // comment for why this is not restricted to greedy
                    // decoding.
                    context.requested_max_tokens =
                        configuration.chat_max_reply_tokens;
                    context.draft_runner_queued = false;
                    context.sampling_supported_by_speculative_verification = true;
                    if (!decide_speculative_decoding_for_request(context)
                             .enabled) {
                        speculative_draft.reset();
                    }
                }
            }
            if (speculative_draft.has_value()) {
                tuning.speculative_draft_model_file =
                    speculative_draft->directory /
                    speculative_draft->manifest.model_file;
                tuning.speculative_draft_gpu_layers = tuning.gpu_layers;
            }
            admit_runner_weights(
                *model, parallel_slots,
                speculative_draft.has_value() ? &*speculative_draft : nullptr);
            try {
                inference->load(*model, configuration.chat_context_length,
                                configuration.runner_port,
                                configuration.runner_startup_timeout_seconds,
                                parallel_slots, tuning,
                                configuration.accelerator_policy);
            } catch (...) {
                release_runner_weights_lease();
                throw;
            }
            return;
        }
        throw std::runtime_error("selected chat model was not found");
    }

    // Phase 33 (LOCAL-ONLY slice): resolves which local runner should serve
    // one request and, if a pool is configured and a suitable runner is
    // found, warms it. Returns "" to mean "use the default single-runner
    // `inference` supervisor instead" -- the only possible outcome when
    // runner_pool is null/empty, so single-runner behavior is completely
    // unaffected when multi-runner is not configured. Never throws: any
    // failure to select/warm a pool runner is treated the same as "no pool
    // runner was suitable" rather than failing the whole request, which is
    // the load-time half of the "runner failure does not crash the control
    // plane" exit criterion (the generate-time half is
    // LocalRunnerPool::generate()'s RunnerGenerationFailure handling above).
    std::string select_and_warm_pool_runner(
        const std::string& model_id, const std::string& project_id,
        const std::string& required_capability = "generation") const {
        if (runner_pool == nullptr || runner_pool->empty()) return {};
        RunnerSelectionSignals signals;
        signals.model_id = model_id;
        signals.required_capability = required_capability;
        signals.project_id = project_id;
        const auto selected = runner_pool->select_runner(signals);
        if (!selected.has_value()) return {};
        const auto model = find_model(model_id);
        if (!model) return {};
        try {
            runner_pool->ensure_model_loaded(
                *selected, *model, configuration.chat_context_length,
                configuration.runner_startup_timeout_seconds);
            return *selected;
        } catch (...) {
            return {};
        }
    }

    // Phase 33 (LOCAL-ONLY slice): the resident-memory figure Phase 13's
    // query trace reports should always describe whichever runner is
    // actually serving the request -- the pool runner named by
    // `active_runner_id`, or the default `inference` supervisor when that
    // is empty (the only case in a single-runner deployment).
    RunnerMetrics active_runner_metrics(const std::string& active_runner_id) const {
        if (!active_runner_id.empty() && runner_pool != nullptr) {
            return runner_pool->metrics(active_runner_id);
        }
        return inference->metrics();
    }

    // Instruction-tuned models expect their own turn-delimiting markup
    // around each role's text; feeding them a bare user string with no
    // wrapper (as the /completion endpoint would otherwise receive it)
    // leaves them free-completing a document instead of answering, which
    // reads as the model ignoring or misunderstanding the message. Each
    // entry here mirrors the turn format the corresponding architecture
    // was instruction-tuned with; `stop_sequence` is where that format
    // marks the end of a turn, so generation halts there instead of
    // running on into a hallucinated next turn.
    struct ChatTemplate {
        std::string system_prefix, system_suffix;
        std::string user_prefix, user_suffix;
        std::string assistant_prefix, assistant_suffix;
        std::string generation_prompt;
        std::string stop_sequence;
    };

    // True for architectures whose chat template/tokenizer actually reacts
    // to a reasoning directive placed in the conversation text: Qwen's
    // documented "/think" and "/no_think" turn suffixes, and gpt-oss's
    // harmony-style "Reasoning effort: <level>" convention (see
    // apply_reasoning_directive() below -- this project does not implement
    // the full Harmony format, see chat_template_for_architecture(), so this
    // is an approximation of it, not a faithful reproduction). Every other
    // architecture (llama, gemma, phi3, yi, and anything unrecognized) has
    // no such hook, so effort/thinking are applied as ordinary sampling
    // presets instead (see apply_sampling_preset()).
    static bool architecture_supports_reasoning_directives(
        const std::string& architecture) {
        std::string lower = architecture;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        return lower.find("qwen") != std::string::npos ||
               lower.find("gpt-oss") != std::string::npos;
    }

    // Folds the user's chosen effort/thinking preference into the latest
    // turn's own text for a reasoning-capable architecture (see
    // architecture_supports_reasoning_directives()) -- there is no separate
    // system-message hook in assemble_chat_prompt()'s template tables, and
    // both conventions this approximates are themselves ordinarily placed
    // in-conversation, not in a dedicated system role. Only ever called for
    // an architecture that passed the check above.
    static std::string apply_reasoning_directive(
        const std::string& architecture, const std::string& effort,
        const std::string& thinking, const std::string& content) {
        std::string lower = architecture;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) {
                           return static_cast<char>(std::tolower(c));
                       });
        if (lower.find("gpt-oss") != std::string::npos) {
            std::string header = "Reasoning effort: " + effort + ".";
            header += (thinking == "on")
                          ? " Show your reasoning before the final answer."
                          : " Respond directly without showing your "
                            "reasoning.";
            return header + "\n\n" + content;
        }
        // Qwen family: the effort level becomes a depth hint alongside the
        // model's own documented /think /no_think turn suffix.
        std::string depth =
            effort == "high"
                ? "Think through this thoroughly, step by step."
                : (effort == "low" ? "Keep your reasoning brief."
                                   : "Think it through at a normal depth.");
        return content + (thinking == "on" ? (" " + depth + " /think")
                                            : " /no_think");
    }

    // Effort/thinking fallback for every architecture that has no reasoning
    // hook of its own (see architecture_supports_reasoning_directives()):
    // adjusts ordinary sampling instead, so the two settings still do
    // something honest rather than silently no-op-ing. "medium" effort
    // leaves GenerationOptions' own defaults untouched.
    static void apply_sampling_preset(GenerationOptions& options,
                                      const std::string& effort,
                                      const std::string& thinking) {
        if (effort == "low") {
            options.temperature = 0.15;
            options.top_p = 0.85;
        } else if (effort == "high") {
            options.temperature = 0.6;
            options.top_p = 0.97;
        }
        if (thinking == "on") {
            options.temperature = std::max(0.0, options.temperature - 0.05);
            options.max_tokens = static_cast<unsigned int>(std::min<std::uint64_t>(
                static_cast<std::uint64_t>(options.max_tokens) * 5ULL / 4ULL,
                32768ULL));
        }
    }

    ChatTemplate chat_template_for_architecture(
        const std::string& architecture) const {
        if (architecture == "phi3") {
            return {"<|system|>\n", "<|end|>\n", "<|user|>\n", "<|end|>\n",
                    "<|assistant|>\n", "<|end|>\n", "<|assistant|>\n",
                    "<|end|>"};
        }
        if (architecture == "llama") {
            return {"<|start_header_id|>system<|end_header_id|>\n\n",
                    "<|eot_id|>", "<|start_header_id|>user<|end_header_id|>\n\n",
                    "<|eot_id|>",
                    "<|start_header_id|>assistant<|end_header_id|>\n\n",
                    "<|eot_id|>",
                    "<|start_header_id|>assistant<|end_header_id|>\n\n",
                    "<|eot_id|>"};
        }
        if (architecture == "gemma") {
            // Gemma has no separate system role; a system message is folded
            // into the following user turn instead of dropped. This branch
            // also covers CodeGemma (registered under the same "gemma"
            // architecture), whose extra fill-in-the-middle /
            // document-boundary tokens (e.g. <|file_separator|>) are stopped
            // on separately -- see extra_stop_sequences below.
            return {"", "\n\n", "<start_of_turn>user\n", "<end_of_turn>\n",
                    "<start_of_turn>model\n", "<end_of_turn>\n",
                    "<start_of_turn>model\n", "<end_of_turn>"};
        }
        // ChatML, used as-is by qwen2/yi and as a reasonable default for any
        // other or unrecognized architecture (including gpt-oss, whose full
        // Harmony format this does not attempt to reproduce).
        return {"<|im_start|>system\n", "<|im_end|>\n", "<|im_start|>user\n",
                "<|im_end|>\n", "<|im_start|>assistant\n", "<|im_end|>\n",
                "<|im_start|>assistant\n", "<|im_end|>"};
    }

    // Wraps the chat's prior turns plus the latest user message (already
    // carrying any attachment/retrieval text -- see assemble_inference_prompt
    // and retrieve_with_cache) in the target model's own instruction format,
    // and reports the stop sequence that format uses to end a turn so the
    // caller can bound generation with it.
    std::string assemble_chat_prompt(const std::string& architecture,
                                     const std::vector<ChatMessage>& history,
                                     const std::string& latest_user_content,
                                     std::string& stop_sequence) const {
        const auto tmpl = chat_template_for_architecture(architecture);
        stop_sequence = tmpl.stop_sequence;
        // Phase 23: adapt this file's local ChatTemplate literal table into
        // the masterai.hpp ChatWrapTemplate shape so the process-lifetime
        // compiled-template cache and segmented assembly in
        // prompt_assembly.cpp can be reused here without duplicating the
        // per-architecture literal tables or the wrapping logic. The
        // resulting prompt is byte-for-byte identical to the old repeated
        // std::string += concatenation this replaces (see
        // test_phase_twentythree_segmented_assembly_byte_identical) --
        // materialize_prompt() is the single point where the segmented
        // representation is joined back into one contiguous string, right
        // before it needs to leave this function.
        const ChatWrapTemplate wrap{
            tmpl.system_prefix,    tmpl.system_suffix, tmpl.user_prefix,
            tmpl.user_suffix,      tmpl.assistant_prefix,
            tmpl.assistant_suffix, tmpl.generation_prompt, tmpl.stop_sequence};
        const auto& plan = compiled_chat_template(architecture, wrap);
        const auto segments =
            assemble_chat_prompt_segments(plan, history, latest_user_content);
        return materialize_prompt(segments);
    }

    // Phase 76: real, single-turn, non-streaming generation for the RAG
    // query route's optional `generate:true` mode -- reuses exactly the
    // same prompt-assembly (assemble_chat_prompt) and inference call
    // (inference->generate) the chat handler below uses, and honors the
    // identical memory-admission/scheduler-admission contract (reserve/
    // admit before generating, release/complete or cancel on every exit
    // path) so a RAG answer cannot bypass the concurrency/memory limits
    // the chat path is careful to enforce. Deliberately does not carry
    // chat history, session-slot reuse, or query-lifecycle tracking --
    // those are chat-specific concerns this one-shot call has no need of.
    // Throws std::runtime_error (a real error, e.g. "inference backend not
    // configured" or a memory/scheduler admission failure) rather than
    // ever fabricating an answer.
    GenerationResult execute_rag_generation(const std::string& model_id,
                                            const std::string& prompt_text) {
        if (inference == nullptr) {
            throw std::runtime_error("inference backend is not configured");
        }
        const auto model = find_model(model_id);
        if (!model) {
            throw std::runtime_error("model \"" + model_id + "\" was not found");
        }
        ensure_model_loaded(model_id);
        std::string stop_sequence;
        const auto generation_prompt = assemble_chat_prompt(
            model->manifest.architecture, {}, prompt_text, stop_sequence);
        GenerationOptions options;
        options.max_tokens = configuration.chat_max_reply_tokens;
        if (!stop_sequence.empty()) options.stop_sequences.push_back(stop_sequence);
        MemoryEstimate memory_estimate;
        memory_estimate.runtime_buffer_bytes = 64ULL * 1024ULL * 1024ULL;
        memory_estimate.kv_bytes_per_sequence = 128ULL * 1024ULL * 1024ULL;
        memory_estimate.transient_bytes =
            static_cast<std::uint64_t>(generation_prompt.size()) * 3U;
        memory_estimate.safety_margin_bytes = 32ULL * 1024ULL * 1024ULL;
        const auto admission =
            memory->reserve(MemoryCategory::compute_buffers, memory_estimate, true);
        if (!admission.admitted) {
            throw std::runtime_error(admission.diagnostic);
        }
        std::string memory_lease_id = admission.lease_id;
        const auto scheduling = request_scheduler->admit(
            SchedulingClass::interactive_chat,
            memory_estimate.runtime_buffer_bytes +
                memory_estimate.kv_bytes_per_sequence +
                memory_estimate.transient_bytes +
                memory_estimate.safety_margin_bytes);
        if (!scheduling.admitted || !scheduling.ticket.has_value()) {
            memory->release(memory_lease_id);
            throw std::runtime_error("inference queue admission failed: " +
                                     scheduling.reason);
        }
        const auto scheduler_ticket = *scheduling.ticket;
        model_usage->increment_waiting(model_id);
        // Phase 78: real per-request telemetry -- begin_request() marks
        // this request as queued/in-flight from admission through
        // completion; end_request() always fires exactly once, with the
        // real wall-clock latency, on every exit path below.
        ml_inference_metrics.begin_request();
        const auto request_started = std::chrono::steady_clock::now();
        const auto record_end = [&]() {
            const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - request_started);
            ml_inference_metrics.end_request(static_cast<std::uint64_t>(
                elapsed.count()));
        };
        const std::atomic_bool no_cancellation{false};
        bool scheduler_running = false;
        try {
            if (!request_scheduler->wait_until_ready(scheduler_ticket,
                                                     no_cancellation)) {
                throw std::runtime_error(
                    "inference request was cancelled or expired while queued");
            }
            model_usage->decrement_waiting(model_id);
            scheduler_running = true;
            const auto generated = inference->generate(
                generation_prompt, options, [](const std::string&) {},
                no_cancellation);
            memory->release(memory_lease_id);
            request_scheduler->complete(scheduler_ticket);
            model_usage->record_use(model_id, epoch_seconds());
            record_end();
            return generated;
        } catch (...) {
            memory->release(memory_lease_id);
            if (scheduler_running) {
                request_scheduler->complete(scheduler_ticket);
            } else {
                model_usage->decrement_waiting(model_id);
                request_scheduler->cancel(scheduler_ticket);
            }
            record_end();
            throw;
        }
    }

    // Phase 77: real network listener for one `active` InferenceEndpoint --
    // closes the "does not open a real network listener, enforce the rate
    // limit, or apply the safety/tool policy" gap InferenceEndpoint's class
    // comment named. Runs on its own detached-from-the-caller background
    // thread (started/stopped by the endpoint status route below), binds
    // endpoint.host:endpoint.port with its own small accept loop (the same
    // hand-rolled pattern HttpServer::run uses for the main listener --
    // this codebase's socket layer is not a singleton object, so a second
    // instance is a direct copy of that pattern, not new architecture),
    // and serves exactly one route: `POST /v1/completions` with a
    // `{"prompt":"..."}` body. Every request is authenticated (a Bearer
    // token check against expected_authorization when the endpoint's
    // authentication_method is not "none"), rate-limited (a real in-memory,
    // per-minute counter local to this thread -- resets on restart, capped
    // at endpoint.rate_limit_per_minute), and content-scanned (Phase 74's
    // scan_content_for_risks, run over both the incoming prompt and the
    // generated answer) before/after the exact same execute_rag_generation
    // path Phase 76's RAG route uses -- no duplicated inference logic.
    void run_inference_endpoint(const InferenceEndpoint endpoint,
                                const std::string expected_authorization,
                                const std::shared_ptr<std::atomic_bool> stop_flag) {
#if defined(_WIN32)
        WSADATA wsa_data{};
        WSAStartup(MAKEWORD(2, 2), &wsa_data);
#endif
        const NativeSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == invalid_socket) {
            log(LogLevel::error, "ml.endpoint.listen_failed", endpoint.id);
            return;
        }
        const int reuse = 1;
        setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                  reinterpret_cast<const char*>(&reuse), sizeof(reuse));
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(endpoint.port);
        if (endpoint.host.empty() || endpoint.host == "0.0.0.0") {
            address.sin_addr.s_addr = INADDR_ANY;
        } else {
            // inet_pton() is the non-deprecated replacement for inet_addr().
            inet_pton(AF_INET, endpoint.host.c_str(), &address.sin_addr);
        }
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 16) != 0) {
            log(LogLevel::error, "ml.endpoint.bind_failed",
               endpoint.id + " " + endpoint.host + ":" +
                   std::to_string(endpoint.port));
            close_socket(listener);
            return;
        }
        log(LogLevel::info, "ml.endpoint.listening",
           endpoint.id + " " + endpoint.host + ":" +
               std::to_string(endpoint.port));
        std::uint64_t rate_window_minute = 0U;
        std::uint32_t requests_this_window = 0U;
        while (!stop_flag->load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            timeval wait{};
            wait.tv_sec = 0;
            wait.tv_usec = 200000;
#if defined(_WIN32)
            const int selected = select(0, &readable, nullptr, nullptr, &wait);
#else
            const int selected = select(listener + 1, &readable, nullptr, nullptr, &wait);
#endif
            if (selected <= 0) continue;
            const NativeSocket client = accept(listener, nullptr, nullptr);
            if (client == invalid_socket) continue;
            try {
                std::string raw;
                std::array<char, 8192> buffer{};
                while (raw.size() < 16384U &&
                      raw.find("\r\n\r\n") == std::string::npos) {
                    const auto received =
                        recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
                    if (received <= 0) break;
                    raw.append(buffer.data(), static_cast<std::size_t>(received));
                }
                const auto header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    const std::string headers = lower(raw.substr(0U, header_end));
                    const std::string marker = "\r\ncontent-length:";
                    const auto position = headers.find(marker);
                    std::uint64_t content_length = 0U;
                    if (position != std::string::npos) {
                        const auto start =
                            headers.find_first_not_of(' ', position + marker.size());
                        const auto end = headers.find("\r\n", start);
                        content_length = std::stoull(headers.substr(start, end - start));
                    }
                    while (content_length <= configuration.max_request_bytes &&
                          raw.size() < header_end + 4U + content_length) {
                        const auto received = recv(client, buffer.data(),
                                                   static_cast<int>(buffer.size()), 0);
                        if (received <= 0) break;
                        raw.append(buffer.data(), static_cast<std::size_t>(received));
                    }
                }
                const auto request = parse_request(raw, configuration.max_request_bytes);
                const auto minute = epoch_seconds() / 60U;
                if (minute != rate_window_minute) {
                    rate_window_minute = minute;
                    requests_this_window = 0U;
                }
                std::string response_body;
                std::string status_line;
                const auto authorization = request.headers.count("authorization")
                                               ? request.headers.at("authorization")
                                               : std::string{};
                if (!expected_authorization.empty() &&
                    !constant_time_equal(authorization, expected_authorization)) {
                    status_line = "HTTP/1.1 401 Unauthorized";
                    response_body = "{\"error\":\"unauthorized\"}";
                } else if (request.method != "POST" ||
                          request.target != "/v1/completions") {
                    status_line = "HTTP/1.1 404 Not Found";
                    response_body = "{\"error\":\"not_found\"}";
                } else if (endpoint.rate_limit_per_minute > 0U &&
                          ++requests_this_window > endpoint.rate_limit_per_minute) {
                    status_line = "HTTP/1.1 429 Too Many Requests";
                    response_body = "{\"error\":\"rate_limited\"}";
                } else {
                    try {
                        auto root = parse_json(request.body);
                        const auto prompt_text = root.required("prompt").as_string();
                        // Phase 77 (this pass): the fixed, unconditional
                        // scan every endpoint used to run is now this
                        // endpoint's own configured policy -- re-read fresh
                        // from the store on every request (not captured
                        // once at thread start) so an administrator's
                        // policy change takes effect on the very next
                        // request without a listener restart.
                        const auto live_endpoint = ml_inference_endpoints->find(endpoint.id);
                        const auto policy_endpoint = live_endpoint ? *live_endpoint : endpoint;
                        std::optional<SafetyPolicy> safety_policy;
                        if (!policy_endpoint.safety_policy_id.empty()) {
                            safety_policy = ml_safety_governance->find_policy(
                                policy_endpoint.safety_policy_id);
                        }
                        ContentScanReport prompt_scan;
                        if (policy_endpoint.content_scan_enabled) {
                            prompt_scan = scan_content_for_risks(
                                prompt_text, safety_policy ? &*safety_policy : nullptr);
                        }
                        if (!prompt_scan.clean() && policy_endpoint.block_on_scan_finding) {
                            status_line = "HTTP/1.1 400 Bad Request";
                            response_body =
                                "{\"error\":\"content_scan_flagged_prompt\","
                                "\"scan\":" + content_scan_report_json(prompt_scan) + "}";
                        } else {
                            const auto generated =
                                execute_rag_generation(endpoint.model_id, prompt_text);
                            ContentScanReport answer_scan;
                            if (policy_endpoint.content_scan_enabled) {
                                answer_scan = scan_content_for_risks(
                                    generated.text, safety_policy ? &*safety_policy : nullptr);
                            }
                            if (!answer_scan.clean() &&
                                policy_endpoint.block_answer_on_scan_finding) {
                                status_line = "HTTP/1.1 400 Bad Request";
                                response_body =
                                    "{\"error\":\"content_scan_flagged_response\","
                                    "\"scan\":" + content_scan_report_json(answer_scan) + "}";
                                audit.append("ml.endpoint.request", endpoint.owner_id,
                                            "denied", endpoint.id);
                            } else {
                                std::string classifier_json;
                                if (policy_endpoint.model_classifier_enabled) {
                                    const auto classifier = scan_content_with_model_classifier(
                                        generated.text,
                                        [&](const std::string& prompt) {
                                            return execute_rag_generation(
                                                       endpoint.model_id, prompt)
                                                .text;
                                        },
                                        policy_endpoint.model_classifier_confidence_floor);
                                    classifier_json = ",\"modelClassifier\":" +
                                                      model_classifier_report_json(classifier);
                                }
                                status_line = "HTTP/1.1 200 OK";
                                response_body =
                                    "{\"content\":\"" + json_escape(generated.text) +
                                    "\",\"promptTokens\":" +
                                    std::to_string(generated.prompt_tokens) +
                                    ",\"generatedTokens\":" +
                                    std::to_string(generated.generated_tokens) +
                                    ",\"elapsedMicroseconds\":" +
                                    std::to_string(generated.elapsed_microseconds) +
                                    ",\"contentScan\":" +
                                    content_scan_report_json(answer_scan) +
                                    classifier_json + "}";
                                audit.append("ml.endpoint.request", endpoint.owner_id,
                                            "success", endpoint.id);
                            }
                        }
                    } catch (const std::exception& error) {
                        status_line = "HTTP/1.1 409 Conflict";
                        response_body =
                            "{\"error\":\"ml_endpoint_generation_failed\","
                            "\"detail\":\"" + json_escape(error.what()) + "\"}";
                        audit.append("ml.endpoint.request", endpoint.owner_id,
                                    "failure", endpoint.id);
                    }
                }
                const std::string wire = status_line +
                    "\r\nContent-Type: application/json\r\nContent-Length: " +
                    std::to_string(response_body.size()) +
                    "\r\nConnection: close\r\n\r\n" + response_body;
                send_all(client, wire);
            } catch (const std::exception& error) {
                log(LogLevel::warning, "ml.endpoint.request_failed", error.what());
            }
            close_socket(client);
        }
        close_socket(listener);
        log(LogLevel::info, "ml.endpoint.stopped", endpoint.id);
    }

    // Builds bounded chat context from attachments owned by the same user and
    // project, keeping attachment loops out of the streaming operation.
    std::string assemble_inference_prompt(
        const JsonValue& root, const ChatRecord& chat,
        const UserRecord& user) const {
        // Bound matches send_chat_message()'s own parse of this same body:
        // content, attachmentIds, and the optional effort/thinking fields.
        // Only content/attachmentIds are read here -- effort/thinking are
        // applied separately, against the model's architecture, once it's
        // been resolved (see send_chat_message()).
        if (root.as_object().size() < 1U ||
            root.as_object().size() > 4U) {
            throw std::runtime_error("unexpected message field");
        }
        std::string result = root.required("content").as_string();
        const auto* attachment_ids = root.optional("attachmentIds");
        if (attachment_ids == nullptr) return result;
        if (attachment_ids->as_array().size() > 8U) {
            throw std::runtime_error("too many message attachments");
        }
        for (const auto& value : attachment_ids->as_array()) {
            const auto attachment =
                attachments->find_for_owner(value.as_string(), user.id);
            if (!attachment || attachment->project_id != chat.project_id) {
                throw std::runtime_error(
                    "attachment is outside the chat project");
            }
            result += "\n\n[Attachment: " + attachment->filename + "]\n" +
                      attachments->read_text_for_owner(attachment->id,
                                                       user.id);
            if (result.size() > configuration.max_request_bytes) {
                throw std::runtime_error(
                    "assembled chat context exceeds policy");
            }
        }
        return result;
    }

    // Phase 17: serves a cached RetrievalOutcome when one is reachable under
    // the current policy/index generation, otherwise runs RetrievalPlanner
    // and stores the result (skipped for partial outcomes, which are not
    // representative of a settled generation). Sets `cache_hit` truthfully
    // for the caller's disclosure.
    RetrievalOutcome retrieve_with_cache(const ProjectRecord& project,
                                         const UserRecord& user,
                                         const std::string& inference_prompt,
                                         bool& cache_hit) {
        const auto index_status = indexes->status(project.id);
        CacheKey cache_key;
        cache_key.user_id = user.id;
        cache_key.project_id = project.id;
        cache_key.policy_generation = cache->current_policy_generation();
        cache_key.canonical_identity = inference_prompt;
        cache_key.version_tag = "retrieval-v1";
        cache_key.index_generation =
            index_status ? index_status->index.generation : 0U;
        cache_hit = false;
        if (configuration.cache_enabled) {
            if (const auto cached =
                    cache->get(CacheCategory::retrieval_result, cache_key)) {
                cache_hit = true;
                return deserialize_retrieval_outcome(*cached);
            }
        }
        RetrievalRequest retrieval_request;
        retrieval_request.project = project;
        // Phase 24: identity + policy generation are part of the in-flight
        // join key, so two different users' concurrent requests -- or the
        // same user's requests spanning a policy change -- can never join.
        retrieval_request.requester_id = user.id;
        retrieval_request.policy_generation = cache_key.policy_generation;
        retrieval_request.query_text = inference_prompt;
        retrieval_request.deadline = std::chrono::milliseconds(
            configuration.retrieval_deadline_milliseconds);
        retrieval_request.maximum_context_bytes =
            configuration.retrieval_maximum_context_bytes;
        retrieval_request.maximum_chunks_per_source =
            configuration.retrieval_maximum_chunks_per_source;
        retrieval_request.maximum_total_chunks =
            configuration.retrieval_maximum_total_chunks;
        auto retrieved = retrieval_planner->retrieve(retrieval_request);
        if (configuration.cache_enabled && !retrieved.partial) {
            cache->put(CacheCategory::retrieval_result, cache_key,
                      serialize_retrieval_outcome(retrieved));
        }
        return retrieved;
    }

    std::string send_chat_message(Request& request, const UserRecord& user,
                                  const NativeSocket stream_socket) {
        const std::string prefix{"/api/v1/chats/"};
        const auto chat_id = request.target.substr(
            prefix.size(), request.target.size() - prefix.size() - 9U);
        auto chat = chats->find_for_owner(chat_id, user.id);
        if (!chat) {
            return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
        }

        // Chats created before Phase 61 have no memory snapshot. Initialize
        // such a chat once on its first post-upgrade turn, persist even an
        // empty result, then use only the cached snapshot thereafter.
        if (!chat->memory_context_initialized) {
            const auto memory_context =
                user_memories->recall_context(user.id, 4096U);
            static_cast<void>(chats->initialize_memory_context(
                chat_id, user.id, memory_context));
            chat = chats->find_for_owner(chat_id, user.id);
            if (!chat) {
                return response(404, "Not Found",
                                "{\"error\":\"chat_not_found\"}");
            }
        }

        // Parse and validate the user's message before touching the runner.
        // A whole-message "save to memory:" command is a server operation,
        // so it remains fast and works even when no inference backend is
        // configured or the selected model is currently unloaded.
        JsonValue root;
        std::string prompt;
        // Effort/thinking are an optional per-message reasoning preference
        // the composer's model settings panel sends alongside content (see
        // application_script()'s streamMessage()) -- default to "medium"/
        // "off" for any older client or direct API caller that omits them,
        // so this remains backward compatible.
        std::string effort = "medium";
        std::string thinking = "off";
        try {
            root = parse_json(request.body);
            if (root.as_object().size() < 1U ||
                root.as_object().size() > 4U) {
                throw std::runtime_error("unexpected message field");
            }
            prompt = root.required("content").as_string();
            if (prompt.empty()) {
                throw std::runtime_error("empty message");
            }
            if (const auto* effort_field = root.optional("effort")) {
                effort = effort_field->as_string();
                if (effort != "low" && effort != "medium" && effort != "high") {
                    throw std::runtime_error("invalid effort");
                }
            }
            if (const auto* thinking_field = root.optional("thinking")) {
                thinking = thinking_field->as_string();
                if (thinking != "off" && thinking != "on") {
                    throw std::runtime_error("invalid thinking level");
                }
            }
        } catch (const std::exception&) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_chat_message\"}");
        }

        const auto explicit_memory = extract_memory_directive(prompt);
        if (explicit_memory) {
            const auto record =
                user_memories->add(user.id, *explicit_memory, "manual");
            audit.append("chat.memory_create", user.id, "success", record.id);
            if (memory_directive_is_whole_message(prompt)) {
                const std::string confirmation =
                    "Saved to memory: " + record.content;
                chats->append(chat_id, ChatRole::user, prompt);
                chats->append(chat_id, ChatRole::assistant, confirmation);
                if (stream_socket != invalid_socket) {
                    const std::string header =
                        "HTTP/1.1 200 OK\r\n"
                        "Content-Type: application/x-ndjson; charset=utf-8\r\n"
                        "Transfer-Encoding: chunked\r\nConnection: close\r\n"
                        "Cache-Control: no-store\r\n"
                        "X-Content-Type-Options: nosniff\r\n"
                        "X-Frame-Options: DENY\r\n"
                        "Referrer-Policy: no-referrer\r\n\r\n";
                    if (!send_all(stream_socket, header)) return {};
                    send_chunk(stream_socket,
                               "{\"type\":\"token\",\"content\":\"" +
                                   json_escape(confirmation) + "\"}\n");
                    send_chunk(stream_socket,
                               "{\"type\":\"complete\",\"promptTokens\":0,"
                               "\"generatedTokens\":0,\"memorySaved\":true}\n");
                    send_all(stream_socket, "0\r\n\r\n");
                    return {};
                }
                return response(200, "OK",
                                "{\"content\":\"" +
                                    json_escape(confirmation) +
                                    "\",\"promptTokens\":0,"
                                    "\"generatedTokens\":0,"
                                    "\"memorySaved\":true}");
            }
        } else {
            // Automatic capture is deliberately deterministic and bounded;
            // it records only recognized self-disclosure phrasing and never
            // asks a model to decide what should be persisted.
            for (const auto& detail : extract_automatic_memories(prompt)) {
                const auto record = user_memories->add(user.id, detail, "auto");
                audit.append("chat.memory_auto_capture", user.id, "success",
                             record.id);
            }
        }

        if (inference == nullptr) {
            return response(503, "Service Unavailable",
                            "{\"error\":\"inference_backend_not_configured\"}");
        }
        bool stream_started = false;
        std::string query_id;
        std::string memory_lease_id;
        std::optional<ScheduledTicket> scheduler_ticket;
        bool scheduler_running = false;
        // Phase 33 (LOCAL-ONLY slice): "" means the default single-runner
        // `inference` supervisor -- the only value this ever holds unless
        // runner_pool is configured AND select_and_warm_pool_runner()
        // actually found and warmed a suitable pool runner below.
        std::string active_runner_id;
        bool model_waiting_recorded = false;
        // Phase 78: set the moment this request is actually admitted into
        // the scheduler (see scheduler_ticket assignment below); used only
        // to time the failure path below, since the success path already
        // has generated.elapsed_microseconds as its real measured latency.
        auto request_started = std::chrono::steady_clock::now();
        std::atomic_bool cancellation{false};
        // Tokens are streamed to the client as they arrive, so a client
        // watching the reply has already seen this text by the time
        // anything below can fail (a dropped runner connection, a policy
        // limit, etc.) -- the catch block below persists whatever made it
        // this far rather than silently discarding a reply the user already
        // read on screen.
        std::string streamed_text;
        try {
            query_id = queries.begin(user.id, chat->project_id, chat->model_id);
            queries.transition(query_id, QueryStage::authentication,
                               QueryStatus::accepted);
            // Phase 33 (LOCAL-ONLY slice): try an opt-in pool runner first;
            // falls back to the always-available default `inference` path
            // (unchanged from every pre-Phase-33 build) whenever no pool is
            // configured, no runner qualifies, or the selected runner fails
            // to load the model.
            active_runner_id =
                select_and_warm_pool_runner(chat->model_id, chat->project_id);
            if (active_runner_id.empty()) {
                ensure_model_loaded(chat->model_id);
            }
            queries.record_runner(query_id, active_runner_id.empty()
                                                 ? std::string("inference")
                                                 : active_runner_id);
            queries.transition(query_id, QueryStage::normalization,
                               QueryStatus::accepted);
            queries.transition(query_id, QueryStage::classification,
                               QueryStatus::accepted);
            queries.transition(query_id, QueryStage::retrieval_planning,
                               QueryStatus::retrieving);
            queries.transition(query_id, QueryStage::retrieval,
                               QueryStatus::retrieving);
            // Phase 16: retrieve a small, ranked, explainable evidence set
            // from the project's own Phase 15 index -- bounded by a request
            // deadline and a hard byte/chunk budget -- instead of ever
            // injecting the whole project. The chat's own project has
            // already been re-authorized for this user just above via
            // chats->find_for_owner(), so a membership/policy change is
            // reflected on the very next message.
            auto inference_prompt = assemble_inference_prompt(root, *chat, user);
            // Saved details are user-provided reference data, not trusted
            // system instructions. Keep them inside the current user turn,
            // clearly delimited and under a strict byte budget, so every
            // model can use relevant facts without granting persisted text
            // a higher instruction priority than the live request.
            if (!chat->memory_context.empty()) {
                inference_prompt = chat->memory_context +
                                   "\n[Current user message]\n" +
                                   inference_prompt;
                if (inference_prompt.size() > configuration.max_request_bytes) {
                    throw std::runtime_error(
                        "assembled chat context exceeds policy");
                }
            }
            if (configuration.retrieval_enabled && !chat->project_id.empty()) {
                if (const auto project = projects->find(chat->project_id)) {
                    // Phase 17: a repeated question against an unchanged
                    // index generation and policy state is served from
                    // CacheManager instead of rerunning RetrievalPlanner --
                    // see retrieve_with_cache(). A file change or a
                    // membership/policy change makes the previous entry
                    // unreachable automatically because the cache key embeds
                    // the current index/policy generation.
                    bool cache_hit = false;
                    const auto retrieved = retrieve_with_cache(
                        *project, user, inference_prompt, cache_hit);
                    inference_prompt += retrieved.context_text;
                    if (inference_prompt.size() > configuration.max_request_bytes) {
                        throw std::runtime_error(
                            "assembled chat context exceeds policy");
                    }
                    queries.record_retrieval(
                        query_id, retrieved.partial,
                        retrieval_disclosure_json(retrieved, cache_hit));
                    // Phase 24: surface the deterministic classification and
                    // any declared-but-disabled strategy reasons on the same
                    // trace ordinary users already see via
                    // /api/v1/queries/{id}.
                    queries.record_classification(query_id,
                                                  retrieved.classification);
                    queries.record_retrieval_strategy_skips(
                        query_id, retrieved.disabled_strategy_reasons);
                }
            }
            queries.transition(query_id, QueryStage::ranking,
                               QueryStatus::retrieving);
            queries.transition(query_id, QueryStage::prompt_assembly,
                               QueryStatus::retrieving);
            // The chat record was looked up before this turn's user message
            // was appended below, so chat->messages here is exactly the
            // prior history -- wrapping it plus this turn's content in the
            // model's own instruction format is what keeps an instruct model
            // (see chat_template_for_architecture()) from free-completing an
            // unrelated document instead of answering.
            const auto model = find_model(chat->model_id);
            // Effort/thinking (see the composer's model settings panel):
            // a reasoning-capable architecture gets a real directive folded
            // into this turn's own text before the chat template wraps it;
            // every other architecture instead gets a sampling-preset
            // adjustment below, once GenerationOptions exists.
            const bool reasoning_capable =
                model.has_value() &&
                architecture_supports_reasoning_directives(
                    model->manifest.architecture);
            if (reasoning_capable) {
                inference_prompt = apply_reasoning_directive(
                    model->manifest.architecture, effort, thinking,
                    inference_prompt);
            }
            std::string stop_sequence;
            const auto generation_prompt = assemble_chat_prompt(
                model ? model->manifest.architecture : std::string(),
                chat->messages, inference_prompt, stop_sequence);
            chats->append(chat_id, ChatRole::user, prompt);
            GenerationOptions options;
            options.max_tokens = configuration.chat_max_reply_tokens;
            if (!reasoning_capable) {
                apply_sampling_preset(options, effort, thinking);
            }
            if (!stop_sequence.empty()) {
                options.stop_sequences.push_back(stop_sequence);
            }
            // Fit the reply budget inside the model's context window. The
            // configured chat_max_reply_tokens can exceed what is actually
            // left after the prompt (history + attachments + chat template),
            // and letting the runner discover that mid-generation either
            // fails the request outright (large attachments read as an
            // opaque "system error") or triggers llama.cpp context shifting,
            // which discards the oldest tokens -- including the chat
            // template's own structure -- and reliably degrades small
            // instruct models into repeating themselves until max_tokens.
            // Counting the prompt up front turns both into either a clear,
            // actionable error or a correctly bounded generation.
            try {
                const auto prompt_token_count =
                    inference->tokenize(generation_prompt);
                // Small allowance for special/BOS tokens the plain /tokenize
                // count may not reflect exactly.
                constexpr std::uint64_t kContextMarginTokens = 32U;
                // Below this many tokens of remaining room a reply cannot
                // say anything useful -- treat it as an overflow instead.
                constexpr std::uint64_t kMinimumReplyTokens = 16U;
                const std::uint64_t context_tokens =
                    configuration.chat_context_length;
                if (prompt_token_count + kContextMarginTokens +
                        kMinimumReplyTokens >
                    context_tokens) {
                    throw std::runtime_error(
                        "this message needs about " +
                        std::to_string(prompt_token_count) +
                        " prompt tokens but the model's context window is " +
                        std::to_string(context_tokens) +
                        " -- remove or shorten an attachment, start a new "
                        "chat, or raise inference.chatContextLength in "
                        "Settings");
                }
                const auto available = static_cast<unsigned int>(
                    context_tokens - prompt_token_count -
                    kContextMarginTokens);
                if (options.max_tokens > available) {
                    options.max_tokens = available;
                }
            } catch (const std::runtime_error&) {
                throw;
            } catch (const std::exception&) {
                // A failed token count (runner still warming, transient HTTP
                // error) falls back to the configured budget rather than
                // failing the whole turn -- the runner itself still enforces
                // its context limit as the backstop.
            }
            // Phase 18: every field here must still match exactly, and the
            // new prompt must literally extend the slot's last prompt,
            // before PromptSessionManager will ever grant reuse -- a model
            // switch, a project reindex, or an edited/resubmitted earlier
            // turn each change one of these and correctly force a fresh,
            // uncached request instead of feeding the runner a stale KV
            // cache. Disabled entirely (session_reuse_enabled defaults to
            // false) until a same-host repeated-turn benchmark records the
            // Phase 18 exit-criterion evidence.
            SessionFingerprint fingerprint;
            SessionDecision session_decision;
            if (configuration.session_reuse_enabled) {
                if (model) fingerprint.model_sha256 = model->manifest.model_sha256;
                fingerprint.backend_executable =
                    configuration.llama_server_executable.string();
                fingerprint.architecture =
                    model ? model->manifest.architecture : std::string();
                fingerprint.context_length = configuration.chat_context_length;
                // Phase 19: bind reuse to the calibration profile name so a
                // profile switch (which can change GPU-layer/batch/mmap
                // launch tuning) invalidates any cached KV slot instead of
                // reusing one loaded under different settings.
                fingerprint.settings_fingerprint =
                    configuration.resource_profile;
                if (!chat->project_id.empty()) {
                    if (const auto index_status =
                            indexes->status(chat->project_id)) {
                        fingerprint.project_index_generation =
                            index_status->index.generation;
                    }
                }
                session_decision = prompt_sessions->try_reuse(
                    chat_id, fingerprint, generation_prompt);
                // Phase 78 (this pass): the real KV-cache-hit-rate telemetry
                // the Phase 78 gap note said this adapter had no
                // instrumentation for -- this is the actual reuse decision
                // every generation on this path already makes, recorded
                // only while session reuse is enabled (when it is disabled,
                // "miss" would not reflect a genuine cache decision at all).
                ml_inference_metrics.record_cache_decision(session_decision.reuse);
                options.cache_prompt = session_decision.reuse;
                if (session_decision.reuse) {
                    options.slot_id = session_decision.slot_id;
                }
            }
            if (model && model->manifest.architecture == "gemma") {
                // CodeGemma checkpoints share the "gemma" architecture tag
                // but were trained with extra code-infill / document
                // boundary special tokens that the base Gemma chat template
                // doesn't know about. Left unstopped, the model can emit
                // these as literal text in a chat reply (e.g.
                // "<|file_separator|>"), so they're stopped on here too.
                options.stop_sequences.push_back("<|file_separator|>");
                options.stop_sequences.push_back("<|fim_prefix|>");
                options.stop_sequences.push_back("<|fim_suffix|>");
                options.stop_sequences.push_back("<|fim_middle|>");
                // Defense against a degraded/mismatched tokenizer: if the
                // model's own "<end_of_turn>" special token isn't being
                // produced as a real stop token (seen with GGUF conversions
                // llama.cpp logs "control-looking token ... was not
                // control-type" for), it can instead free-run past the end
                // of its own reply and hallucinate a fake next turn as
                // literal text, e.g. "<start_of_turn>model\n...", repeating
                // until max_tokens. Stopping the instant that literal header
                // text appears bounds the damage to one hallucinated turn
                // instead of an unbounded loop.
                options.stop_sequences.push_back("<start_of_turn>");
            }
            MemoryEstimate memory_estimate;
            memory_estimate.runtime_buffer_bytes = 64ULL * 1024ULL * 1024ULL;
            // Phase 18: llama.cpp reserves KV cache for every --parallel
            // slot at model-load time (see ensure_model_loaded()), not just
            // for slots currently tracked as reusable, so admission must
            // account for the full configured slot count whenever session
            // reuse is enabled -- one sequence's worth otherwise.
            memory_estimate.kv_bytes_per_sequence =
                128ULL * 1024ULL * 1024ULL *
                static_cast<std::uint64_t>(configuration.session_reuse_enabled
                                               ? configuration.session_reuse_max_slots
                                               : 1U);
            memory_estimate.transient_bytes =
                static_cast<std::uint64_t>(generation_prompt.size()) * 3U;
            memory_estimate.safety_margin_bytes = 32ULL * 1024ULL * 1024ULL;
            const auto admission = memory->reserve(
                MemoryCategory::compute_buffers, memory_estimate, true);
            if (!admission.admitted) {
                throw std::runtime_error(admission.diagnostic);
            }
            memory_lease_id = admission.lease_id;
            const auto scheduling = request_scheduler->admit(
                SchedulingClass::interactive_chat,
                memory_estimate.runtime_buffer_bytes +
                    memory_estimate.kv_bytes_per_sequence +
                    memory_estimate.transient_bytes +
                    memory_estimate.safety_margin_bytes);
            if (!scheduling.admitted || !scheduling.ticket.has_value()) {
                throw std::runtime_error(
                    "inference queue admission failed: " + scheduling.reason);
            }
            scheduler_ticket = scheduling.ticket;
            // Phase 78: real per-request telemetry begins the moment this
            // request actually enters the queue.
            ml_inference_metrics.begin_request();
            request_started = std::chrono::steady_clock::now();
            model_usage->increment_waiting(chat->model_id);
            model_waiting_recorded = true;
            const bool streaming = stream_socket != invalid_socket;
            if (streaming) {
                const std::string header =
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/x-ndjson; charset=utf-8\r\n"
                    "Transfer-Encoding: chunked\r\nConnection: close\r\n"
                    "Cache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n"
                    "X-Frame-Options: DENY\r\nReferrer-Policy: no-referrer\r\n\r\n";
                stream_started = send_all(stream_socket, header);
                if (!stream_started) cancellation.store(true);
                if (stream_started) {
                    send_chunk(stream_socket,
                               "{\"type\":\"status\",\"status\":\"accepted\","
                               "\"requestId\":\"" +
                                   json_escape(query_id) + "\"}\n");
                    send_chunk(stream_socket,
                               "{\"type\":\"status\",\"status\":\"retrieving\"}\n");
                }
            }
            queries.transition(query_id, QueryStage::runner_queue,
                               QueryStatus::queued);
            if (streaming && stream_started) {
                send_chunk(stream_socket,
                           "{\"type\":\"status\",\"status\":\"queued\"}\n");
            }
            if (!request_scheduler->wait_until_ready(*scheduler_ticket,
                                                     cancellation)) {
                throw std::runtime_error(
                    "inference request was cancelled or expired while queued");
            }
            model_usage->decrement_waiting(chat->model_id);
            model_waiting_recorded = false;
            scheduler_running = true;
            // Phase 33 (LOCAL-ONLY slice): observe whichever runner is
            // actually about to serve this request -- active_runner_id is
            // "" (meaning the default `inference` supervisor) unless a
            // configured pool already warmed a specific runner for it above.
            queries.observe_resources(
                query_id,
                active_runner_metrics(active_runner_id).resident_memory_bytes);
            queries.transition(query_id, QueryStage::prompt_evaluation,
                               QueryStatus::evaluating_prompt);
            if (streaming && stream_started) {
                send_chunk(
                    stream_socket,
                    "{\"type\":\"status\",\"status\":\"evaluating_prompt\"}\n");
            }
            const auto generation_started = std::chrono::steady_clock::now();
            bool first_token = true;
            std::uint64_t time_to_first_token = 0U;
            // Named (rather than inline at the call site) so the identical
            // callback can be reused by both the primary generate() call and
            // the safe same-request fallback to the default runner below.
            const std::function<void(const std::string&)> on_chunk =
                [&](const std::string& chunk) {
                    if (first_token) {
                        first_token = false;
                        time_to_first_token = static_cast<std::uint64_t>(
                            std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() -
                                generation_started)
                                .count());
                        queries.transition(query_id, QueryStage::generation,
                                           QueryStatus::generating);
                        if (streaming && stream_started) {
                            send_chunk(
                                stream_socket,
                                "{\"type\":\"status\",\"status\":\"generating\"}\n");
                        }
                    }
                    streamed_text += chunk;
                    if (streaming) {
                        // Phase 30: escape straight into a byte vector and
                        // move it (no copy -- SharedBuffer's vector
                        // constructor takes ownership) into a SharedBuffer,
                        // then hand a BufferView over it to
                        // send_chunk_parts(), which writes the JSON envelope
                        // prefix/escaped-payload/suffix directly to the
                        // socket instead of concatenating them into one
                        // throwaway std::string first (the old
                        // "prefix + json_escape(chunk) + suffix" line above
                        // built two intermediate strings, then send_chunk()
                        // built a third to add chunked-encoding framing --
                        // all three copies of the full payload are gone).
                        SharedBuffer escaped_buffer(json_escape_bytes(chunk));
                        BufferView escaped_view(escaped_buffer, 0U,
                                                escaped_buffer.size());
                        if (!send_chunk_parts(stream_socket,
                                              "{\"type\":\"token\",\"content\":\"",
                                              escaped_view, "\"}\n")) {
                            cancellation.store(true);
                        }
                    }
                };
            GenerationResult generated;
            // Phase 33 (intranet-worker slice, this pass): a healthy remote
            // worker verified to hold the requested model is tried before
            // falling back further to the default local runner, closing the
            // "chat-generation dispatch does not yet automatically fail over
            // onto a remote worker" gap the original pass left open. Applies
            // the identical retry_is_semantically_safe() rule the local-pool
            // fallback below already uses: only when nothing from this
            // attempt has reached the client yet, since persistence only
            // happens after generate() returns successfully, further below.
            // A remote-worker failure itself (already recorded against that
            // worker's own health tracking by IntranetWorkerPool::generate)
            // is swallowed here rather than failing the whole request --
            // the caller's next fallback still gets a chance.
            const auto try_remote_worker_failover =
                [&](const bool any_bytes_emitted)
                    -> std::optional<GenerationResult> {
                if (intranet_worker_pool == nullptr) return std::nullopt;
                if (!retry_is_semantically_safe(any_bytes_emitted, false)) {
                    return std::nullopt;
                }
                RunnerSelectionSignals signals;
                signals.model_id = chat->model_id;
                signals.required_capability = "generation";
                const auto worker_id = intranet_worker_pool->select_worker(signals);
                if (!worker_id.has_value()) return std::nullopt;
                try {
                    auto result = intranet_worker_pool->generate(
                        *worker_id, generation_prompt, options, on_chunk,
                        cancellation, configuration.runner_stall_timeout_seconds);
                    active_runner_id.clear();
                    queries.record_runner(query_id, "intranet-worker:" + *worker_id);
                    return result;
                } catch (const std::exception&) {
                    return std::nullopt;
                }
            };
            if (active_runner_id.empty()) {
                try {
                    generated = inference->generate(
                        generation_prompt, options, on_chunk, cancellation,
                        configuration.runner_stall_timeout_seconds);
                } catch (const std::exception&) {
                    if (auto remote =
                            try_remote_worker_failover(!streamed_text.empty())) {
                        generated = std::move(*remote);
                    } else {
                        throw;
                    }
                }
            } else {
                try {
                    generated = runner_pool->generate(
                        active_runner_id, generation_prompt, options, on_chunk,
                        cancellation, configuration.runner_stall_timeout_seconds);
                } catch (const RunnerGenerationFailure& failure) {
                    // Phase 33 (LOCAL-ONLY slice) exit criterion: a runner
                    // failure must not crash the control plane, and a retry
                    // must only happen when it cannot duplicate output the
                    // client already received -- nothing has been persisted
                    // from this request yet (persistence only happens after
                    // generate() returns successfully, further below), so
                    // the only thing that matters is whether any byte of
                    // this attempt already reached the client's on_chunk.
                    if (!retry_is_semantically_safe(failure.any_bytes_emitted,
                                                    false)) {
                        throw;
                    }
                    if (auto remote = try_remote_worker_failover(
                            failure.any_bytes_emitted)) {
                        generated = std::move(*remote);
                    } else {
                        ensure_model_loaded(chat->model_id);
                        generated = inference->generate(
                            generation_prompt, options, on_chunk, cancellation,
                            configuration.runner_stall_timeout_seconds);
                        // The default runner ended up serving this request,
                        // not the pool runner originally selected -- keep
                        // the trace identity honest about who actually
                        // generated the reply.
                        active_runner_id.clear();
                        queries.record_runner(query_id, "inference");
                    }
                }
            }
            if (first_token) {
                queries.transition(query_id, QueryStage::generation,
                                   QueryStatus::generating);
            }
            queries.record_inference(query_id, generated,
                                     time_to_first_token);
            queries.observe_resources(
                query_id,
                active_runner_metrics(active_runner_id).resident_memory_bytes);
            if (configuration.session_reuse_enabled) {
                // A cancelled turn leaves the runner's KV state for that
                // slot describing an incomplete reply -- never record it as
                // reusable. A completed turn becomes the new prefix the
                // *next* turn is checked against.
                if (generated.cancelled) {
                    prompt_sessions->release(chat_id);
                } else {
                    prompt_sessions->record(
                        chat_id, fingerprint, generation_prompt,
                        session_decision.reuse
                            ? std::optional<unsigned int>(session_decision.slot_id)
                            : std::nullopt,
                        generated.prompt_tokens);
                }
            }
            queries.transition(query_id, QueryStage::persistence,
                               QueryStatus::generating);
            if (!generated.cancelled && generated.text.empty()) {
                // The runner reported success and streamed zero content --
                // seen with models whose GGUF conversion is missing/broken
                // tokenizer metadata (llama.cpp logs "missing pre-tokenizer
                // type" / "control-looking token ... was not control-type"
                // for these), where every generated token decodes to an
                // empty piece. ChatStore::append would reject this with an
                // opaque "outside policy" error; name the real cause instead
                // so it doesn't read as a generic crash.
                throw std::runtime_error(
                    "the model produced an empty reply -- its GGUF file's "
                    "tokenizer metadata may be broken/incomplete (check the "
                    "runner log for a pre-tokenizer warning); try a "
                    "different quantization or source for this model");
            }
            // Persist the runner-reported token figures with the transcript:
            // the reply's generated count on the assistant message, and the
            // evaluated prompt count back-filled onto this turn's user
            // message (appended above, before these numbers existed). The
            // web UI shows them in each bubble's title row.
            chats->append(chat_id, ChatRole::assistant, generated.text,
                          generated.generated_tokens);
            chats->set_last_message_tokens(chat_id, ChatRole::user,
                                           generated.prompt_tokens);
            queries.transition(query_id, QueryStage::release,
                               QueryStatus::generating);
            queries.finish(
                query_id, generated.cancelled ? QueryStatus::cancelled
                                              : QueryStatus::completed);
            memory->release(memory_lease_id);
            memory_lease_id.clear();
            request_scheduler->complete(*scheduler_ticket);
            scheduler_running = false;
            model_usage->record_use(chat->model_id, epoch_seconds());
            ml_inference_metrics.end_request(generated.elapsed_microseconds);
            audit.append("chat.generate", user.id, "success", chat_id);
            if (streaming) {
                send_chunk(
                    stream_socket,
                    "{\"type\":\"complete\",\"promptTokens\":" +
                        std::to_string(generated.prompt_tokens) +
                        ",\"generatedTokens\":" +
                        std::to_string(generated.generated_tokens) +
                        ",\"elapsedMicroseconds\":" +
                        std::to_string(generated.elapsed_microseconds) +
                        ",\"requestId\":\"" + json_escape(query_id) + "\"" +
                        ",\"cancelled\":" +
                        std::string(generated.cancelled ? "true" : "false") +
                        "}\n");
                send_all(stream_socket, "0\r\n\r\n");
                return {};
            }
            return response(
                200, "OK",
                "{\"content\":\"" + json_escape(generated.text) +
                    "\",\"promptTokens\":" +
                    std::to_string(generated.prompt_tokens) +
                    ",\"generatedTokens\":" +
                    std::to_string(generated.generated_tokens) +
                    ",\"elapsedMicroseconds\":" +
                    std::to_string(generated.elapsed_microseconds) +
                    ",\"requestId\":\"" + json_escape(query_id) + "\"}");
        } catch (const std::exception& generation_exception) {
            if (!memory_lease_id.empty()) {
                memory->release(memory_lease_id);
            }
            if (model_waiting_recorded) {
                model_usage->decrement_waiting(chat->model_id);
                model_waiting_recorded = false;
            }
            if (scheduler_ticket.has_value()) {
                if (scheduler_running) {
                    request_scheduler->complete(*scheduler_ticket);
                } else {
                    request_scheduler->cancel(*scheduler_ticket);
                }
                scheduler_running = false;
                // Phase 78: this request was actually admitted (begin_request()
                // ran above), so its failure must still be recorded, timed
                // from the same request_started admission moment.
                const auto elapsed = std::chrono::duration_cast<
                    std::chrono::microseconds>(
                    std::chrono::steady_clock::now() - request_started);
                ml_inference_metrics.end_request(
                    static_cast<std::uint64_t>(elapsed.count()));
            }
            if (configuration.session_reuse_enabled) {
                prompt_sessions->release(chat_id);
            }
            // Keep the real failure reason (e.what()) instead of discarding
            // it: this is what previously made every failure -- a genuine
            // runner crash, a degraded/garbled model producing zero-length
            // completions that trip ChatStore::append's non-empty policy,
            // cancellation races, etc. -- show the same generic message,
            // making the actual cause undiagnosable from the UI alone.
            const std::string failure_reason = generation_exception.what();
            if (!query_id.empty()) {
                try {
                    queries.finish(query_id, QueryStatus::failed,
                                   failure_reason);
                } catch (const std::exception& finish_exception) {
                    // Already handling a generation failure; logging instead
                    // of rethrowing keeps the original failure_reason as the
                    // one surfaced to the client.
                    std::cerr << "chat.generate: failed to mark query '"
                              << query_id
                              << "' failed: " << finish_exception.what()
                              << std::endl;
                }
            }
            // The generate() call throwing loses its own copy of whatever it
            // had produced so far, but the client already rendered every
            // token this callback saw -- save that copy so the reply
            // surviving in the transcript matches what the user read, rather
            // than reopening the chat to find it silently gone.
            if (!streamed_text.empty()) {
                try {
                    chats->append(chat_id, ChatRole::assistant, streamed_text);
                } catch (const std::exception& append_exception) {
                    std::cerr << "chat.generate: failed to persist partial "
                                 "streamed reply for chat '"
                              << chat_id << "': " << append_exception.what()
                              << std::endl;
                }
            }
            audit.append("chat.generate", user.id,
                         "failed: " + failure_reason, chat_id);
            if (stream_started) {
                send_chunk(stream_socket,
                           "{\"type\":\"error\",\"error\":\"generation_failed\","
                           "\"detail\":\"" + json_escape(failure_reason) +
                           "\"}\n");
                send_all(stream_socket, "0\r\n\r\n");
                return {};
            }
            return response(400, "Bad Request",
                            "{\"error\":\"generation_failed\",\"detail\":\"" +
                                json_escape(failure_reason) + "\"}");
        }
    }

    AppConfig configuration;
    RecordStore records;
    AuditLog audit;
    // Phase 75: holds each non-local ComputeNode's real telemetry-agent
    // shared secret, keyed by node id, so the .../telemetry route can
    // actually present it to the agent on every poll. ComputeNode itself
    // only ever persists the secret's hash (agent_shared_secret_hash,
    // matching the setup-token hash convention), since that record is
    // returned to callers of GET .../compute-nodes; this is the one place
    // the plaintext lives, encrypted at rest the same way SecretStore
    // already protects the IDE MCP token in main.cpp.
    // Phase 78: real per-request latency/queue-depth telemetry, fed by
    // every real generation call site (chat below, execute_rag_generation)
    // and surfaced by build_ml_monitoring_json().
    InferenceMetricsStore ml_inference_metrics;
    // Phase 78 (this pass): live per-step tabular-training progress -- see
    // TrainingProgressTracker's class comment in masterai.hpp. Populated by
    // execute_training_job()'s on_epoch callback while a run is executing
    // on its own request-handling thread, read by GET /api/v1/ml/training-
    // jobs/{id}/live-progress and build_ml_monitoring_json() from any other
    // connection thread.
    TrainingProgressTracker ml_training_progress;
    // Phase 77: one background listener thread per `active` InferenceEndpoint
    // (see run_inference_endpoint), keyed by endpoint id. The
    // shared_ptr<atomic_bool> is that thread's own stop flag; flipping it
    // and joining is how a status change away from `active` (or delete)
    // really stops the listener, not just the database record.
    std::map<std::string, std::pair<std::thread, std::shared_ptr<std::atomic_bool>>>
        inference_endpoint_threads;
    std::mutex inference_endpoint_threads_mutex;
    // Phase 30A: empty unless the caller (HttpServer's two-argument
    // constructor, wired from main.cpp's `serve` command) supplied the real
    // settings.json path -- see the admin configuration API below, which
    // refuses to run without it rather than guessing a path.
    const std::filesystem::path settings_file;
    std::unique_ptr<UserStore> users;
    std::unique_ptr<SessionStore> sessions;
    std::unique_ptr<ApiTokenStore> api_tokens;
    std::unique_ptr<SecretStore> secrets;
    std::unique_ptr<ProjectCatalog> projects;
    std::unique_ptr<McpInboundServer> mcp;
    std::unique_ptr<IdeIntegrationService> ide;
    std::unique_ptr<McpOutboundRegistry> mcp_outbound_registry;
    std::unique_ptr<McpOutboundGateway> mcp_outbound_gateway;
    std::unique_ptr<server_internal::IntegrationHttpController> integrations;
    std::unique_ptr<ChatStore> chats;
    std::unique_ptr<UserMemoryStore> user_memories;
    std::unique_ptr<MLProjectStore> ml_projects;
    std::unique_ptr<ModelRegistryStore> ml_models;
    std::unique_ptr<DatasetStore> ml_datasets;
    std::unique_ptr<SubjectPackageStore> ml_subjects;
    std::unique_ptr<LabelTaskStore> ml_label_tasks;
    std::unique_ptr<DataPreparationJobStore> ml_prep_jobs;
    std::unique_ptr<TrainingJobStore> ml_training_jobs;
    std::unique_ptr<EvaluationRunStore> ml_evaluation_runs;
    std::unique_ptr<ExperimentStore> ml_experiments;
    std::unique_ptr<ExperimentResultStore> ml_experiment_results;
    std::unique_ptr<FineTuningJobStore> ml_fine_tuning_jobs;
    std::unique_ptr<ModelBuilderConfigStore> ml_model_builder_configs;
    std::unique_ptr<InstructionExampleStore> ml_instruction_examples;
    std::unique_ptr<InstructionExampleContentStore> ml_instruction_example_content;
    std::unique_ptr<SyntheticRecordStore> ml_synthetic_records;
    std::unique_ptr<SyntheticRecordContentStore> ml_synthetic_record_content;
    std::unique_ptr<VectorStoreStore> ml_vector_stores;
    std::unique_ptr<RagConfigStore> ml_rag_configs;
    std::unique_ptr<SubjectExamStore> ml_subject_exams;
    std::unique_ptr<HyperparameterSearchStore> ml_hyperparameter_searches;
    std::unique_ptr<ModelOptimizationStore> ml_model_optimizations;
    std::unique_ptr<TrainingCheckpointStore> ml_training_checkpoints;
    std::unique_ptr<CheckpointModelStore> ml_checkpoint_models;
    std::unique_ptr<DeploymentStore> ml_deployments;
    // Phase 56: the real ML execution layer's stores -- uploaded dataset
    // content, learned weight artifacts, and executed evaluation results.
    std::unique_ptr<DatasetContentStore> ml_dataset_content;
    std::unique_ptr<TrainedModelStore> ml_trained_models;
    std::unique_ptr<EvaluationResultStore> ml_evaluation_results;
    std::unique_ptr<FineTuningRunResultStore> ml_llm_finetune_results;
    std::unique_ptr<ModelComparisonStore> ml_model_comparisons;
    std::unique_ptr<ComparisonResultStore> ml_comparison_results;
    std::unique_ptr<KnowledgeIndexStore> ml_knowledge_index;
    // Phases 62-65: the remaining docs/PLAN.md "Machine Learning
    // Abilities" section-2 interfaces.
    std::unique_ptr<InferenceEndpointStore> ml_inference_endpoints;
    std::unique_ptr<ComputeNodeStore> ml_compute_nodes;
    std::unique_ptr<AutomationPipelineStore> ml_automation_pipelines;
    std::unique_ptr<SafetyGovernanceStore> ml_safety_governance;
    std::unique_ptr<AttachmentStore> attachments;
    std::unique_ptr<RunnerSupervisor> inference;
    // Phase 33 (LOCAL-ONLY slice): opt-in local multi-runner pool. Null
    // (the default) whenever configuration.local_runner_pool is empty --
    // every request path below falls back to `inference` above exactly as
    // it did before this phase, so single-runner mode is never required to
    // change. See LocalRunnerPool's class comment in masterai.hpp.
    std::unique_ptr<LocalRunnerPool> runner_pool;
    // Phase 33 (INTRANET-WORKER slice): opt-in remote worker routing. Null
    // (the default) whenever configuration.intranet_worker_pool is empty --
    // consulted only as a fallback after runner_pool/`inference` in
    // select_and_warm_pool_runner() below, never in place of them.
    std::unique_ptr<IntranetWorkerPool> intranet_worker_pool;
    // Phase 33 (INTRANET-WORKER slice): this machine's own worker-mode
    // listener. Null unless configuration.worker_mode.enabled -- see
    // WorkerListener's class comment. Runs on worker_listener_thread below,
    // started in HttpServer::run() and stopped alongside every other
    // background loop.
    std::unique_ptr<WorkerListener> worker_listener;
    std::thread worker_listener_thread;
    std::unique_ptr<DownloadManager> downloads;
    std::unique_ptr<BenchmarkStore> benchmarks;
    std::unique_ptr<server_internal::WorkloadHttpController> workloads;
    std::unique_ptr<MemoryBudgetManager> memory;
    // Phase 25: the single live admission/fairness/backpressure authority
    // for inference work. Its interactive concurrency allowance is derived
    // from MemoryPolicy::maximum_active_inference in the constructor.
    std::unique_ptr<RequestScheduler> request_scheduler;
    // Phase 34: always present -- see the constructor comment above.
    std::unique_ptr<AdaptiveController> adaptive_controller;
    // Serializes the complete single-runner load/unload decision. A browser
    // message arriving during best-effort pre-warm waits for that same
    // bounded load rather than observing `starting` and failing immediately.
    mutable std::mutex model_load_mutex;
    // Coalesces repeated page-open/chat-create/model-change warm requests;
    // foreground ensure_model_loaded() calls still wait on model_load_mutex.
    mutable std::atomic_bool model_warm_in_progress{false};
    // Phase 30A: guards the runner weight lease itself. Kept separate from
    // model_load_mutex because release paths outside load also use it.
    mutable std::mutex runner_admission_mutex;
    mutable std::string runner_weights_lease_id;
    std::unique_ptr<ProjectIndexService> indexes;
    // Phase 24: constructed once alongside `indexes` (see the constructor)
    // so its in-flight join table actually sees concurrent requests.
    std::unique_ptr<RetrievalPlanner> retrieval_planner;
    // Declared after `indexes` so it is destroyed first: the watcher thread
    // must stop calling indexes->request_update() before indexes itself is
    // torn down.
    std::unique_ptr<ProjectWatcher> watcher;
    std::unique_ptr<CacheManager> cache;
    std::unique_ptr<PromptSessionManager> prompt_sessions;
    // Phase 30A: declared (and therefore destroyed) before `memory`,
    // `inference`, `cache`, and `prompt_sessions` themselves so its
    // background thread always stops -- see ~MemorySweeper()'s join --
    // before any object it might still be calling into is torn down.
    std::unique_ptr<MemorySweeper> memory_sweeper;
    std::uint32_t runner_idle_unload_seconds{600U};
    // Phase 19: nullable exactly like `inference`/`downloads` above --
    // calibration requires a real RunnerSupervisor to load a model against,
    // so it only exists when inference.llamaServerExecutable is configured.
    std::unique_ptr<TuningProfileStore> tuning_profiles;
    std::unique_ptr<CalibrationService> calibration;
    // Phase 31: bounded/quota-enforced/crash-recoverable scratch storage.
    // Always constructed (unlike calibration/inference above, which are
    // conditional on an external llama-server executable) -- scratch space
    // is needed regardless of whether real inference is configured. Its own
    // destructor calls shutdown_cleanup(), so no explicit call is needed in
    // ~State() above; ordinary member-destruction order handles it.
    std::unique_ptr<ScratchVolumeManager> scratch_volumes;
    // Phase 31 (Priority B, manifest closure): durable, journaled record of
    // every migrate_durable_file() move, installed process-wide via
    // install_global_durable_file_manifest() immediately after construction
    // so resolve_durable_path() call sites (LlamaCppAdapter::build_launch_spec,
    // pre-touch, ...) work without threading a reference through every layer
    // between here and there. Declared after records (which it wraps) and
    // before scratch_volumes' users that might resolve through it.
    std::unique_ptr<DurableFileManifest> durable_file_manifest;
    // Phase 20: durable, administrator-controlled evidence/admission state.
    // Declared after records, which outlives it; constructed after open().
    std::unique_ptr<AdvancedOptimizationRegistry> advanced_optimizations;
    // Phase 32: durable, administrator-submitted per-(target,draft)-pair
    // measured acceptance rate. Declared after records for the same reason
    // advanced_optimizations is above.
    std::unique_ptr<SpeculativeDecodingPairEvidenceStore> speculative_pair_evidence;
    // Phase 26: live, bounded, administrator-inspectable model recency/pin/
    // preference/waiting evidence. It records only real server events.
    std::unique_ptr<ModelUsagePredictor> model_usage;
    // Machine Learning foundation phase: same shape as
    // advanced_optimizations above -- always constructible, no persistence,
    // administrator-only. See docs/PLAN.md "Machine Learning Abilities".
    MachineLearningRegistry machine_learning;
    QueryCoordinator queries;
    std::string setup_token;
    std::string setup_hash;
    // Guards the one-time setup token's check-and-clear as a single atomic
    // unit (see parse_setup_fields()/setup()/setup_local()).
    std::mutex setup_mutex;
    std::vector<std::uint64_t> request_times;
    // Guards request_times only -- released before any auth/routing work, so
    // it never becomes a bottleneck under concurrent connections.
    std::mutex request_times_mutex;
};

HttpServer::HttpServer(std::string host, const std::uint16_t port)
    : HttpServer([&host, port]() {
          auto config = ConfigurationManager::safe_defaults();
          config.host = std::move(host);
          config.port = port;
          return config;
      }()) {}

HttpServer::HttpServer(AppConfig configuration)
    : HttpServer(std::move(configuration), std::filesystem::path{}) {}

HttpServer::HttpServer(AppConfig configuration,
                       std::filesystem::path settings_file)
    : host_(configuration.host), port_(configuration.port),
      configuration_(std::move(configuration)) {
    ConfigurationManager::validate(configuration_);
    if (host_ != "127.0.0.1") {
        throw std::invalid_argument(
            "this native listener release permits only IPv4 loopback; intranet "
            "deployment requires the documented TLS reverse-proxy boundary");
    }
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("WSAStartup failed");
    }
#endif
    state_ = std::make_unique<State>(configuration_, std::move(settings_file));
}

HttpServer::~HttpServer() {
    stop();
#if defined(_WIN32)
    WSACleanup();
#endif
}

bool HttpServer::run(std::atomic_bool& stop_requested) {
    const auto stop_file = configuration_.runtime_root / "run" / "stop.request";
    std::filesystem::create_directories(stop_file.parent_path());
    std::filesystem::remove(stop_file);
    const NativeSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalid_socket) {
        log(LogLevel::error, "server.socket_failed", "Could not create listener.");
        return false;
    }
#if defined(_WIN32)
    // A freshly created socket is inheritable by default, and downloads.cpp
    // launches curl.exe with bInheritHandles=TRUE (to hand it a log file
    // handle). Without this, curl.exe would silently inherit a duplicate of
    // the listening socket too, and keep the port bound even after this
    // process exits, so a later restart fails with server.bind_failed until
    // that orphaned child is found and killed by hand.
    SetHandleInformation(reinterpret_cast<HANDLE>(listener), HANDLE_FLAG_INHERIT, 0);
#endif
    socket_ = static_cast<std::intptr_t>(listener);

    int exclusive = 1;
#if defined(_WIN32)
    setsockopt(listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
               reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
#else
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR, &exclusive, sizeof(exclusive));
#endif

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port_);
    if (inet_pton(AF_INET, host_.c_str(), &address.sin_addr) != 1 ||
        bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 16) != 0) {
        log(LogLevel::error, "server.bind_failed", "Loopback listener could not start.");
        close_socket(listener);
        socket_ = -1;
        return false;
    }

    log(LogLevel::info, "server.started", host_ + ":" + std::to_string(port_));
    // Phase 33 (INTRANET-WORKER slice): the worker-mode listener (if
    // configured) runs its own accept loop on its own thread -- it is a
    // deliberately separate, narrower listener from the loopback
    // administrator socket above (see WorkerListener's class comment), not
    // a route registered on this one.
    if (state_->worker_listener != nullptr) {
        state_->worker_listener_thread =
            std::thread([this, &stop_requested] {
                try {
                    state_->worker_listener->run(stop_requested);
                } catch (const std::exception& failure) {
                    log(LogLevel::error, "worker_listener.failed", failure.what());
                }
            });
    }
    while (!stop_requested.load() && !std::filesystem::exists(stop_file)) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 250000;
        const int selected =
#if defined(_WIN32)
            select(0, &readable, nullptr, nullptr, &timeout);
#else
            select(listener + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (selected <= 0) {
            continue;
        }

        sockaddr_in peer{};
#if defined(_WIN32)
        int peer_size = sizeof(peer);
#else
        socklen_t peer_size = sizeof(peer);
#endif
        const NativeSocket client =
            accept(listener, reinterpret_cast<sockaddr*>(&peer), &peer_size);
        if (client == invalid_socket) {
            continue;
        }
#if defined(_WIN32)
        // Same reasoning as the listener above: this connection's own worker
        // thread may run a model download and launch curl.exe on it, which
        // must not inherit a handle to this client socket either.
        SetHandleInformation(reinterpret_cast<HANDLE>(client), HANDLE_FLAG_INHERIT, 0);
#endif

        // Reap any worker threads that have already finished before deciding
        // whether there's room for another one, so workers_ never grows
        // without bound across the server's lifetime.
        {
            std::lock_guard<std::mutex> lock(workers_mutex_);
            for (auto it = workers_.begin(); it != workers_.end();) {
                if (it->second->load()) {
                    it->first.join();
                    it = workers_.erase(it);
                } else {
                    ++it;
                }
            }
        }

        // A long-running request (most notably a model download) must never
        // block any other connection -- including the frontend's own
        // progress-poll requests, which is what previously produced a
        // stalled progress bar and, once enough polls queued up past the
        // listen backlog, a network error in the browser. Each accepted
        // connection therefore gets its own worker thread, bounded by
        // max_concurrent_connections so a runaway/duplicated client can't
        // grow the thread count without limit; past the cap, reject
        // immediately rather than queuing.
        if (active_connections_.load() >= configuration_.max_concurrent_connections) {
            send_all(client, response(503, "Service Unavailable",
                                      "{\"error\":\"server_busy\"}",
                                      {"Retry-After: 1"}));
            close_socket(client);
            continue;
        }

        ++active_connections_;
        auto finished = std::make_shared<std::atomic_bool>(false);
        std::thread worker([this, client, finished]() {
            // Decrements active_connections_ and marks this worker reapable
            // on every exit path, including an unexpected exception escaping
            // handle() -- request routes already catch their own errors, so
            // this is a defensive backstop, not an expected path.
            struct WorkerGuard {
                std::atomic<std::uint32_t>& active;
                std::shared_ptr<std::atomic_bool> done;
                ~WorkerGuard() {
                    --active;
                    done->store(true);
                }
            } guard{active_connections_, finished};

            try {
#if defined(_WIN32)
                const DWORD timeout_ms =
                    configuration_.request_timeout_seconds * 1000U;
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                           reinterpret_cast<const char*>(&timeout_ms),
                           sizeof(timeout_ms));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                           reinterpret_cast<const char*>(&timeout_ms),
                           sizeof(timeout_ms));
#else
                timeval client_timeout{};
                client_timeout.tv_sec =
                    static_cast<long>(configuration_.request_timeout_seconds);
                setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &client_timeout,
                           sizeof(client_timeout));
                setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &client_timeout,
                           sizeof(client_timeout));
#endif

                std::string request;
                std::array<char, 8192> buffer{};
                while (request.size() < 16384U &&
                       request.find("\r\n\r\n") == std::string::npos) {
                    const auto received = recv(
                        client, buffer.data(), static_cast<int>(buffer.size()), 0);
                    if (received <= 0) {
                        break;
                    }
                    request.append(buffer.data(), static_cast<std::size_t>(received));
                }
                const auto header_end = request.find("\r\n\r\n");
                bool invalid_length = false;
                std::uint64_t content_length = 0U;
                if (header_end != std::string::npos) {
                    const std::string headers = lower(request.substr(0U, header_end));
                    const std::string marker = "\r\ncontent-length:";
                    const auto position = headers.find(marker);
                    if (position != std::string::npos) {
                        const auto start = headers.find_first_not_of(
                            ' ', position + marker.size());
                        const auto end = headers.find("\r\n", start);
                        try {
                            content_length = std::stoull(
                                headers.substr(start, end - start));
                        } catch (const std::exception&) {
                            invalid_length = true;
                        }
                    }
                    while (!invalid_length &&
                           content_length <= configuration_.max_request_bytes &&
                           request.size() < header_end + 4U + content_length) {
                        const auto received = recv(
                            client, buffer.data(), static_cast<int>(buffer.size()), 0);
                        if (received <= 0) break;
                        request.append(buffer.data(), static_cast<std::size_t>(received));
                    }
                }
                if (request.size() >= 16384U && header_end == std::string::npos) {
                    send_all(client, response(431, "Request Header Fields Too Large",
                                              "{\"error\":\"headers_too_large\"}"));
                } else if (invalid_length ||
                           content_length > configuration_.max_request_bytes) {
                    send_all(client, response(413, "Payload Too Large",
                                              "{\"error\":\"body_too_large\"}"));
                } else {
                    const auto outgoing = state_->handle(request, client);
                    if (!outgoing.empty()) send_all(client, outgoing);
                }
            } catch (const std::exception& exception) {
                log(LogLevel::error, "server.connection_failed", exception.what());
            }
            close_socket(client);
        });
        {
            std::lock_guard<std::mutex> lock(workers_mutex_);
            workers_.emplace_back(std::move(worker), std::move(finished));
        }
    }
    // Phase 33 (INTRANET-WORKER slice): the worker-listener thread started
    // above only watches `stop_requested`, not the stop_file this loop also
    // watches -- force it true here so a stop_file-triggered shutdown stops
    // the worker listener too, not just the main loopback listener.
    stop_requested.store(true);
    if (state_->worker_listener_thread.joinable()) {
        state_->worker_listener_thread.join();
    }
    // stop() closes the listener (if not already closed by an external
    // stop() call) and joins/detaches every in-flight worker thread. No
    // request thread may still be running once this function returns,
    // because ~HttpServer() destroys state_ immediately afterward.
    stop();
    std::filesystem::remove(stop_file);
    log(LogLevel::info, "server.stopped", "Graceful stop completed.");
    return true;
}

void HttpServer::stop() noexcept {
    if (socket_ != -1) {
        const auto listener = static_cast<NativeSocket>(socket_);
#if defined(_WIN32)
        shutdown(listener, SD_BOTH);
#else
        shutdown(listener, SHUT_RDWR);
#endif
        close_socket(listener);
        socket_ = -1;
    }

    // Signal every in-flight download to stop immediately, rather than
    // letting shutdown wait out its full transfer: without this, a large
    // in-progress download could keep this call (and stop.ps1's 30-second
    // wait for the process to exit) blocked for as long as the transfer
    // itself takes.
    if (state_) {
        state_->cancel_all_downloads();
    }

    // Wait for in-flight request-handling threads to finish naturally (the
    // download signalled above still has to notice, terminate its curl
    // child, and unwind, which is normally well within a few seconds) up to
    // a generous bound, then detach any stragglers so this call -- and the
    // state_ destruction that follows it in ~HttpServer() -- is never
    // blocked indefinitely by a stuck request. Detaching here is only safe
    // because the process is exiting.
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::minutes(5);
    while (active_connections_.load() > 0U &&
           std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    std::lock_guard<std::mutex> lock(workers_mutex_);
    for (auto& worker : workers_) {
        if (!worker.first.joinable()) continue;
        if (worker.second->load()) {
            worker.first.join();
        } else {
            worker.first.detach();
        }
    }
    workers_.clear();
}

}  // namespace masterai
