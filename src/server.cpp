// MasterAI loopback HTTP control plane and browser/API presentation.
//
// This unit accepts bounded HTTP/1.1 requests, applies host/origin,
// authentication, scope, CSRF, project, and rate-limit policy, then routes to
// native services. Streamable MCP HTTP reuses the same security boundary and
// protocol dispatcher as stdio so IDE and browser behavior remain consistent.
#include "masterai.hpp"
#include "json.hpp"
#include "server_internal.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>

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

}  // namespace

class HttpServer::State final {
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
        ml_projects = std::make_unique<MLProjectStore>(records);
        ml_models = std::make_unique<ModelRegistryStore>(records);
        ml_datasets = std::make_unique<DatasetStore>(records);
        ml_subjects = std::make_unique<SubjectPackageStore>(records);
        ml_label_tasks = std::make_unique<LabelTaskStore>(records);
        ml_prep_jobs = std::make_unique<DataPreparationJobStore>(records);
        ml_training_jobs = std::make_unique<TrainingJobStore>(records);
        ml_evaluation_runs = std::make_unique<EvaluationRunStore>(records);
        ml_experiments = std::make_unique<ExperimentStore>(records);
        ml_fine_tuning_jobs = std::make_unique<FineTuningJobStore>(records);
        ml_model_builder_configs = std::make_unique<ModelBuilderConfigStore>(records);
        ml_instruction_examples = std::make_unique<InstructionExampleStore>(records);
        ml_synthetic_records = std::make_unique<SyntheticRecordStore>(records);
        ml_vector_stores = std::make_unique<VectorStoreStore>(records);
        ml_rag_configs = std::make_unique<RagConfigStore>(records);
        ml_subject_exams = std::make_unique<SubjectExamStore>(records);
        ml_hyperparameter_searches =
            std::make_unique<HyperparameterSearchStore>(records);
        ml_model_optimizations = std::make_unique<ModelOptimizationStore>(records);
        ml_training_checkpoints =
            std::make_unique<TrainingCheckpointStore>(records);
        ml_deployments = std::make_unique<DeploymentStore>(records);
        // Phase 56: real ML execution stores (see ml_engine.cpp).
        ml_dataset_content = std::make_unique<DatasetContentStore>(records);
        ml_trained_models = std::make_unique<TrainedModelStore>(records);
        ml_evaluation_results = std::make_unique<EvaluationResultStore>(records);
        // Phase 57: real model comparison (see ml_engine.cpp).
        ml_model_comparisons = std::make_unique<ModelComparisonStore>(records);
        ml_comparison_results = std::make_unique<ComparisonResultStore>(records);
        attachments = std::make_unique<AttachmentStore>(
            value.runtime_root / "attachments", records);
        benchmarks = std::make_unique<BenchmarkStore>(records);
        if (!value.llama_server_executable.empty()) {
            inference = std::make_unique<RunnerSupervisor>(
                value.llama_server_executable, value.runtime_root);
            tuning_profiles = std::make_unique<TuningProfileStore>(records);
            calibration = std::make_unique<CalibrationService>(
                *inference, *tuning_profiles, hardware,
                sha256_file_hex(value.llama_server_executable),
                "masterai-0.1.0", value.accelerator_policy);
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
        // Phase 20: framework-only listing -- every feature reports
        // enabled=false; see docs/PLAN.md section 25 and
        // AdvancedOptimizationRegistry.
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
                        advanced_optimizations.features()) +
                    "}");
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
                const auto parsed = parse_tabular_csv(csv, target_column);
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
            // Real lifecycle transitions, recorded as they happen.
            ml_training_jobs->set_status(id, TrainingJobStatus::queued);
            ml_training_jobs->set_status(id, TrainingJobStatus::preparing);
            TrainedTabularModel model;
            TabularTrainingReport report;
            try {
                const auto data =
                    parse_tabular_csv(content->csv, content->target_column);
                ml_training_jobs->set_status(id, TrainingJobStatus::running);
                report = train_tabular_model(data, options, model);
            } catch (const std::exception& error) {
                ml_training_jobs->set_status(id, TrainingJobStatus::failed);
                audit.append("ml.training_job.run", user->id, "failure", id);
                return response(
                    409, "Conflict",
                    "{\"error\":\"ml_training_failed\",\"detail\":\"" +
                        json_escape(error.what()) + "\"}");
            }
            // Attach the result to the job's registry entry, or register
            // the newly trained model if the job never named one.
            std::string model_id = job->model_id;
            if (model_id.empty() || !ml_models->find(model_id)) {
                const auto entry = ml_models->create(
                    user->id, job->name + "-model", job->name + " (trained)",
                    "1", "masterai-tabular",
                    model.classification ? "classification" : "regression",
                    "masterai-tabular-v1", "training-job:" + job->id, "");
                model_id = entry.id;
            }
            model.model_id = model_id;
            model.training_job_id = job->id;
            ml_trained_models->put(model);
            // Trained models wait for Evaluation Lab review before approval,
            // matching the dashboard's models-awaiting-evaluation count.
            ml_models->set_state(model_id, ModelRegistryState::evaluation);
            // Checkpoint records carry genuinely measured losses from the
            // finished run, capped at ten so a 10000-epoch run doesn't
            // flood the checkpoint list.
            if (options.checkpoint_interval > 0U && !report.loss_history.empty()) {
                std::size_t stride = options.checkpoint_interval;
                const std::size_t epochs = report.loss_history.size();
                if (epochs / stride > 10U) stride = epochs / 10U;
                for (std::size_t epoch = stride; epoch <= epochs; epoch += stride) {
                    char loss_text[32];
                    std::snprintf(loss_text, sizeof(loss_text), "%.6g",
                                  report.loss_history[epoch - 1U]);
                    ml_training_checkpoints->create(
                        user->id, job->id,
                        job->name + " epoch " + std::to_string(epoch),
                        "captured by the Phase 56 training executor",
                        "epoch " + std::to_string(epoch) + ", training loss " +
                            loss_text);
                }
            }
            ml_training_jobs->set_status(id,
                                         TrainingJobStatus::awaiting_evaluation);
            audit.append("ml.training_job.run", user->id, "success", id);
            return response(200, "OK",
                            tabular_training_report_json(report, model));
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
            ml_evaluation_runs->set_status(id, EvaluationRunStatus::running);
            try {
                const auto data =
                    parse_tabular_csv(content->csv, content->target_column);
                const auto metrics = evaluate_tabular_model(*model, data);
                const auto metrics_json = tabular_evaluation_metrics_json(metrics);
                ml_evaluation_results->put(id, metrics_json);
                ml_evaluation_runs->set_status(id, EvaluationRunStatus::completed);
                audit.append("ml.evaluation_run.run", user->id, "success", id);
                return response(200, "OK",
                                "{\"runId\":\"" + json_escape(id) +
                                    "\",\"metrics\":" + metrics_json + "}");
            } catch (const std::exception& error) {
                ml_evaluation_runs->set_status(id, EvaluationRunStatus::failed);
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
        // Phase 44: Experiment Tracking (docs/PLAN.md "Machine Learning
        // Abilities" section 25), scoped to identity/target-project/target-
        // model/target-dataset/status fields -- see ExperimentStore's class
        // comment in masterai.hpp for the version/hyperparameter/metric/
        // artifact/comparison fields deferred to the phase that actually
        // executes and records a training or evaluation run.
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
                const auto experiment = ml_experiments->create(
                    user->id, project_id, model_id, text_field("datasetId"),
                    name, text_field("description"));
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
        // Phase 46: Model Builder Interface (docs/PLAN.md "Machine Learning
        // Abilities" section 9), scoped to identity/target-project/base-
        // model/source-type/status fields -- see ModelBuilderConfigStore's
        // class comment in masterai.hpp for the architecture/layer/
        // tokenizer/optimiser/scheduling fields deferred to the phase that
        // actually executes a model build.
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
        // Phase 47: Prompt and Instruction Training (docs/PLAN.md "Machine
        // Learning Abilities" section 19), scoped to identity/target-
        // dataset/subject-classification/status fields -- see
        // InstructionExampleStore's class comment in masterai.hpp for the
        // system-instruction/user-instruction/context/expected-response/
        // rejected-response/tool-call/output-format fields deferred to the
        // phase that actually creates example records.
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
            audit.append("ml.synthetic_record.delete", user->id, "success",
                         id);
            return response(200, "OK", "{\"deleted\":true}");
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
        // capture-reason/retention-status fields -- see
        // TrainingCheckpointStore's class comment in masterai.hpp for the
        // step/epoch/hash/resume fields deferred to the phase that actually
        // captures checkpoints.
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
        // Phase 55: Deployment Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 34), scoped to identity/model-reference/
        // environment/strategy/status fields -- see DeploymentStore's class
        // comment in masterai.hpp for the health/rollback fields deferred
        // to the phase that actually promotes models.
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
            if (target == "/app/report/system") {
                return is_administrator
                           ? application_page(*user, "report-system")
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
                    std::to_string(message.created_at_epoch_seconds) + "}";
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
            const auto chat = chats->create(user.id, project_id, model_id);
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

    // Shared by create_chat() and set_chat_model(): both must reject a
    // chat-model assignment unless the target model is actually Ready,
    // so neither route accepts a model id that would fail to load.
    bool is_model_ready(const std::string& model_id) const {
        const auto model = find_model(model_id);
        return model && model->state == ModelState::ready;
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
        std::thread([this, model_id]() {
            try {
                ensure_model_loaded(model_id);
            } catch (const std::exception&) {
            }
        }).detach();
    }

    // Phase 30A deliverable 3: bounded, lock-free admission against
    // MemoryPolicy::maximum_active_inference (compare-and-swap retry loop
    // rather than a mutex since this is checked on every chat request).
    // Returns false without incrementing anything once the configured number
    // of concurrent generations is already in flight -- the cpu_only/minimal
    // profile's "one active request" default becomes a real ceiling instead
    // of a validated-but-unread policy field.
    bool try_admit_inference_slot() const {
        const auto limit = memory->policy().maximum_active_inference;
        auto current = active_inference_count.load(std::memory_order_relaxed);
        while (current < limit) {
            if (active_inference_count.compare_exchange_weak(
                    current, current + 1U, std::memory_order_acq_rel)) {
                return true;
            }
        }
        return false;
    }

    void release_inference_slot() const noexcept {
        active_inference_count.fetch_sub(1U, std::memory_order_acq_rel);
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
                              unsigned int parallel_slots) const {
        const auto hardware = probe_hardware(configuration.models_root);
        const auto suitability = assess_model(
            model.manifest, hardware, configuration.memory_reserve_mib);
        if (suitability.rating == Suitability::unsupported) {
            throw std::runtime_error(
                "model rejected before load: " + suitability.reason);
        }
        MemoryEstimate estimate;
        estimate.weights_bytes = model.manifest.model_size_bytes;
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
        const auto current = inference->metrics();
        if (current.state == RunnerState::ready &&
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
            const unsigned int parallel_slots =
                configuration.session_reuse_enabled
                    ? configuration.session_reuse_max_slots
                    : 1U;
            admit_runner_weights(*model, parallel_slots);
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

    // Builds bounded chat context from attachments owned by the same user and
    // project, keeping attachment loops out of the streaming operation.
    std::string assemble_inference_prompt(
        const JsonValue& root, const ChatRecord& chat,
        const UserRecord& user) const {
        if (root.as_object().size() < 1U ||
            root.as_object().size() > 2U) {
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
        if (inference == nullptr) {
            return response(503, "Service Unavailable",
                            "{\"error\":\"inference_backend_not_configured\"}");
        }
        const std::string prefix{"/api/v1/chats/"};
        const auto chat_id = request.target.substr(
            prefix.size(), request.target.size() - prefix.size() - 9U);
        const auto chat = chats->find_for_owner(chat_id, user.id);
        if (!chat) {
            return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
        }
        bool stream_started = false;
        std::string query_id;
        std::string memory_lease_id;
        bool inference_slot_admitted = false;
        // Tokens are streamed to the client as they arrive, so a client
        // watching the reply has already seen this text by the time
        // anything below can fail (a dropped runner connection, a policy
        // limit, etc.) -- the catch block below persists whatever made it
        // this far rather than silently discarding a reply the user already
        // read on screen.
        std::string streamed_text;
        try {
            // Phase 30A: one of, at most, MemoryPolicy::maximum_active_inference
            // concurrent generations -- checked before touching the runner at
            // all, so a rejection under the cpu_only/minimal one-slot default
            // never contends with, or interrupts, whichever generation is
            // already in flight.
            if (!try_admit_inference_slot()) {
                throw std::runtime_error(
                    "the server is already running its maximum number of "
                    "concurrent inference requests; try again once the "
                    "current reply finishes");
            }
            inference_slot_admitted = true;
            query_id = queries.begin(user.id, chat->project_id, chat->model_id);
            queries.transition(query_id, QueryStage::authentication,
                               QueryStatus::accepted);
            ensure_model_loaded(chat->model_id);
            queries.transition(query_id, QueryStage::normalization,
                               QueryStatus::accepted);
            const auto root = parse_json(request.body);
            const auto prompt = root.required("content").as_string();
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
            std::string stop_sequence;
            const auto generation_prompt = assemble_chat_prompt(
                model ? model->manifest.architecture : std::string(),
                chat->messages, inference_prompt, stop_sequence);
            chats->append(chat_id, ChatRole::user, prompt);
            std::atomic_bool cancellation{false};
            GenerationOptions options;
            options.max_tokens = configuration.chat_max_reply_tokens;
            if (!stop_sequence.empty()) {
                options.stop_sequences.push_back(stop_sequence);
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
            queries.observe_resources(
                query_id, inference->metrics().resident_memory_bytes);
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
            const auto generated = inference->generate(
                generation_prompt, options,
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
                },
                cancellation, configuration.runner_stall_timeout_seconds);
            if (first_token) {
                queries.transition(query_id, QueryStage::generation,
                                   QueryStatus::generating);
            }
            queries.record_inference(query_id, generated,
                                     time_to_first_token);
            queries.observe_resources(
                query_id, inference->metrics().resident_memory_bytes);
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
                            : std::nullopt);
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
            chats->append(chat_id, ChatRole::assistant, generated.text);
            queries.transition(query_id, QueryStage::release,
                               QueryStatus::generating);
            queries.finish(
                query_id, generated.cancelled ? QueryStatus::cancelled
                                              : QueryStatus::completed);
            memory->release(memory_lease_id);
            memory_lease_id.clear();
            release_inference_slot();
            inference_slot_admitted = false;
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
            if (inference_slot_admitted) {
                release_inference_slot();
                inference_slot_admitted = false;
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
                } catch (const std::exception&) {
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
                } catch (const std::exception&) {
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
    std::unique_ptr<MLProjectStore> ml_projects;
    std::unique_ptr<ModelRegistryStore> ml_models;
    std::unique_ptr<DatasetStore> ml_datasets;
    std::unique_ptr<SubjectPackageStore> ml_subjects;
    std::unique_ptr<LabelTaskStore> ml_label_tasks;
    std::unique_ptr<DataPreparationJobStore> ml_prep_jobs;
    std::unique_ptr<TrainingJobStore> ml_training_jobs;
    std::unique_ptr<EvaluationRunStore> ml_evaluation_runs;
    std::unique_ptr<ExperimentStore> ml_experiments;
    std::unique_ptr<FineTuningJobStore> ml_fine_tuning_jobs;
    std::unique_ptr<ModelBuilderConfigStore> ml_model_builder_configs;
    std::unique_ptr<InstructionExampleStore> ml_instruction_examples;
    std::unique_ptr<SyntheticRecordStore> ml_synthetic_records;
    std::unique_ptr<VectorStoreStore> ml_vector_stores;
    std::unique_ptr<RagConfigStore> ml_rag_configs;
    std::unique_ptr<SubjectExamStore> ml_subject_exams;
    std::unique_ptr<HyperparameterSearchStore> ml_hyperparameter_searches;
    std::unique_ptr<ModelOptimizationStore> ml_model_optimizations;
    std::unique_ptr<TrainingCheckpointStore> ml_training_checkpoints;
    std::unique_ptr<DeploymentStore> ml_deployments;
    // Phase 56: the real ML execution layer's stores -- uploaded dataset
    // content, learned weight artifacts, and executed evaluation results.
    std::unique_ptr<DatasetContentStore> ml_dataset_content;
    std::unique_ptr<TrainedModelStore> ml_trained_models;
    std::unique_ptr<EvaluationResultStore> ml_evaluation_results;
    std::unique_ptr<ModelComparisonStore> ml_model_comparisons;
    std::unique_ptr<ComparisonResultStore> ml_comparison_results;
    std::unique_ptr<AttachmentStore> attachments;
    std::unique_ptr<RunnerSupervisor> inference;
    std::unique_ptr<DownloadManager> downloads;
    std::unique_ptr<BenchmarkStore> benchmarks;
    std::unique_ptr<server_internal::WorkloadHttpController> workloads;
    std::unique_ptr<MemoryBudgetManager> memory;
    // Phase 30A: guards runner_weights_lease_id against ensure_model_loaded()
    // being entered concurrently (warm_model_async() detaches a background
    // thread that races the synchronous send_chat_message() call path).
    mutable std::mutex runner_admission_mutex;
    mutable std::string runner_weights_lease_id;
    // Phase 30A deliverable 3 (docs/PLAN.md Phase 30A): enforces
    // MemoryPolicy::maximum_active_inference (1 under the minimal/cpu_only
    // profile) as a real admission gate on send_chat_message() instead of
    // leaving the field validated-but-inert -- see try_admit_inference_slot()
    // / release_inference_slot() below.
    mutable std::atomic<std::uint32_t> active_inference_count{0U};
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
    // Phase 20: framework-only registry, always constructible (no runner
    // dependency, no persistence) -- see docs/PLAN.md section 25.
    AdvancedOptimizationRegistry advanced_optimizations;
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
