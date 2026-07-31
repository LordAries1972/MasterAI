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
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>

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
    explicit State(const AppConfig& value)
        : configuration(value), records(value.runtime_root / "database"),
          audit(value.runtime_root / "audit" / "audit.log") {
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
                "masterai-0.1.0");
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
            value.runtime_root / "cache", *memory, cache_policy);
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
        if (request.method == "GET" && request.target == "/api/v1/models") {
            return workloads->model_inventory();
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/system/resources") {
            return response(
                200, "OK",
                hardware_info_json(
                    probe_hardware(configuration.runtime_root)));
        }
        if (request.method == "GET" &&
            request.target == "/api/v1/system/memory") {
            return response(200, "OK",
                            MemoryBudgetManager::to_json(memory->sample()));
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
            if (!role_allows(user->role, "ml.dashboard.view")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            machine_learning_dashboard_json(
                                machine_learning.dashboard(*ml_projects,
                                                           *ml_models)));
        }
        // Phase 38: Machine Learning Projects (docs/PLAN.md "Machine
        // Learning Abilities" section 5), scoped to identity/intent/status
        // fields -- see MLProjectStore's class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/projects") {
            if (!role_allows(user->role, "ml.projects.view")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            "{\"projects\":" +
                                ml_projects_json(ml_projects->list()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/projects") {
            if (!role_allows(user->role, "ml.projects.create")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
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
            if (!role_allows(user->role, "ml.projects.delete")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto id = request.target.substr(
                21U, request.target.size() - 21U - 7U);
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
            if (!role_allows(user->role, "ml.models.view")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            "{\"models\":" +
                                model_registry_entries_json(ml_models->list()) +
                                "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/models") {
            if (!role_allows(user->role, "ml.models.import")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
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
            if (!role_allows(user->role, "ml.models.approve")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto id = request.target.substr(
                19U, request.target.size() - 19U - 6U);
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
            if (!role_allows(user->role, "ml.models.delete")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto id = request.target.substr(
                19U, request.target.size() - 19U - 7U);
            if (!ml_models->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_model_not_found\"}");
            }
            audit.append("ml.model.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
        }
        // Phase 39: Dataset Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 10), scoped to identity/provenance/approval
        // fields -- see DatasetStore's class comment in masterai.hpp.
        if (request.method == "GET" &&
            request.target == "/api/v1/ml/datasets") {
            if (!role_allows(user->role, "ml.datasets.view")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            return response(200, "OK",
                            "{\"datasets\":" +
                                datasets_json(ml_datasets->list()) + "}");
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/ml/datasets") {
            if (!role_allows(user->role, "ml.datasets.import")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
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
            if (!role_allows(user->role, "ml.datasets.approve")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto id = request.target.substr(
                21U, request.target.size() - 21U - 8U);
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
            if (!role_allows(user->role, "ml.datasets.delete")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
            const auto id = request.target.substr(
                21U, request.target.size() - 21U - 7U);
            if (!ml_datasets->remove(id)) {
                return response(404, "Not Found",
                                "{\"error\":\"ml_dataset_not_found\"}");
            }
            audit.append("ml.dataset.delete", user->id, "success", id);
            return response(200, "OK", "{\"deleted\":true}");
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
            if (!role_allows(user->role, "projects.write")) {
                return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
            }
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
            if (!role_allows(user->role, "chats.write")) {
                return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
            }
            return create_chat(request, *user);
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
            if (!role_allows(user->role, "chats.write")) {
                return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
            }
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
            if (!role_allows(user->role, "chats.write")) {
                return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
            }
            return delete_chat(request, *user);
        }
        if (request.method == "POST" &&
            request.target == "/api/v1/attachments") {
            if (!role_allows(user->role, "attachments.write")) {
                return response(403, "Forbidden",
                                "{\"error\":\"permission_denied\"}");
            }
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

    std::string setup(Request& request) {
        if (!configuration.allow_os_identity_accounts) {
            return response(403, "Forbidden", "{\"error\":\"os_accounts_disabled\"}");
        }
        if (!users->setup_required()) {
            return response(409, "Conflict", "{\"error\":\"setup_complete\"}");
        }
        try {
            auto fields = parse_setup_fields(request);
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
        try {
            auto fields = parse_setup_fields(request);
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
        if (!role_allows(admin.role, "users.manage")) {
            return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
        }
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
        if (!role_allows(admin.role, "users.manage")) {
            return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
        }
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
        return response(200, "OK", body + "]}");
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
            bool model_ready = false;
            const auto hardware = probe_hardware(configuration.models_root);
            for (const auto& model :
                 ModelRegistry(configuration.models_root, hardware,
                               configuration.memory_reserve_mib).scan()) {
                if (model.manifest.id == model_id &&
                    model.state == ModelState::ready) {
                    model_ready = true;
                    break;
                }
            }
            if (!model_ready) {
                return response(409, "Conflict",
                                "{\"error\":\"model_not_ready\"}");
            }
            const auto chat = chats->create(user.id, project_id, model_id);
            audit.append("chat.create", user.id, "success", chat.id);
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
            bool model_ready = false;
            const auto hardware = probe_hardware(configuration.models_root);
            for (const auto& model :
                 ModelRegistry(configuration.models_root, hardware,
                               configuration.memory_reserve_mib).scan()) {
                if (model.manifest.id == model_id &&
                    model.state == ModelState::ready) {
                    model_ready = true;
                    break;
                }
            }
            if (!model_ready) {
                return response(409, "Conflict",
                                "{\"error\":\"model_not_ready\"}");
            }
            if (!chats->set_model(chat_id, user.id, model_id)) {
                return response(404, "Not Found", "{\"error\":\"chat_not_found\"}");
            }
            audit.append("chat.model_change", user.id, "success", chat_id);
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

    void ensure_model_loaded(const std::string& model_id) {
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
        }
        if (const auto model = find_model(model_id)) {
            inference->load(*model, configuration.chat_context_length,
                            configuration.runner_port,
                            configuration.runner_startup_timeout_seconds,
                            configuration.session_reuse_enabled
                                ? configuration.session_reuse_max_slots
                                : 1U);
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
                cancellation);
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
    std::unique_ptr<AttachmentStore> attachments;
    std::unique_ptr<RunnerSupervisor> inference;
    std::unique_ptr<DownloadManager> downloads;
    std::unique_ptr<BenchmarkStore> benchmarks;
    std::unique_ptr<server_internal::WorkloadHttpController> workloads;
    std::unique_ptr<MemoryBudgetManager> memory;
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
    state_ = std::make_unique<State>(configuration_);
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
