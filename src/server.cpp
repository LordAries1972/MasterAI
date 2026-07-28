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

bool send_all(const NativeSocket socket, const std::string& response) {
    std::size_t sent = 0U;
    while (sent < response.size()) {
        const auto remaining = response.size() - sent;
        const auto chunk =
            send(socket, response.data() + sent, static_cast<int>(remaining), 0);
        if (chunk <= 0) {
            return false;
        }
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

bool send_chunk(const NativeSocket socket, const std::string& value) {
    std::ostringstream size;
    size << std::hex << value.size();
    return send_all(socket, size.str() + "\r\n" + value + "\r\n");
}

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
        attachments = std::make_unique<AttachmentStore>(
            value.runtime_root / "attachments", records);
        benchmarks = std::make_unique<BenchmarkStore>(records);
        if (!value.llama_server_executable.empty()) {
            inference = std::make_unique<RunnerSupervisor>(
                value.llama_server_executable, value.runtime_root);
        }
        if (!value.curl_executable.empty()) {
            downloads = std::make_unique<DownloadManager>(
                value.curl_executable, value.models_root, records);
        }
        indexes = std::make_unique<ProjectIndexService>(
            value.runtime_root / "indexes", *memory);
        workloads = std::make_unique<
            server_internal::WorkloadHttpController>(
            configuration, *projects, *attachments, inference.get(),
            downloads.get(), *benchmarks, *indexes, audit);
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
        while (!request_times.empty() && request_times.front() + 60U <= now) {
            request_times.erase(request_times.begin());
        }
        if (request_times.size() >= configuration.rate_limit_per_minute) {
            return response(429, "Too Many Requests",
                            "{\"error\":\"rate_limit_exceeded\"}",
                            {"Retry-After: 60"});
        }
        request_times.push_back(now);
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
            const bool ready = !users->setup_required() && identity.available();
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
                             "/index/cancel") == 0))) {
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
        if (request.method == "GET" && request.target == "/models") {
            return workloads->model_inventory_page();
        }
        if (request.method == "GET" && request.target == "/app") {
            return application_page(*user);
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
        if (request.method == "GET" && request.target == "/api/v1/chats") {
            return list_chats(*user);
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

    std::string setup(Request& request) {
        if (!users->setup_required()) {
            return response(409, "Conflict", "{\"error\":\"setup_complete\"}");
        }
        try {
            auto root = parse_json(request.body);
            std::fill(request.body.begin(), request.body.end(), '\0');
            request.body.clear();
            if (root.as_object().size() != 4U) {
                throw std::runtime_error("unexpected setup field");
            }
            const auto token = root.required("setupToken").as_string();
            const auto username = root.required("username").as_string();
            auto password = root.required_mutable("password").take_string();
            const auto display_name = root.required("displayName").as_string();
            if (!constant_time_equal(sha256_hex(token), setup_hash)) {
                audit.append("setup.first_admin", "anonymous", "denied",
                             "invalid setup token");
                return response(403, "Forbidden", "{\"error\":\"setup_token_invalid\"}");
            }
            const auto result = OsIdentityProvider().authenticate(username, password);
            if (!result.authenticated()) {
                audit.append("setup.first_admin", username, "denied",
                             result.diagnostic);
                return response(401, "Unauthorized",
                                "{\"error\":\"os_authentication_failed\"}");
            }
            const auto user =
                users->create_first_administrator(result.principal, display_name);
            setup_hash.clear();
            setup_token.clear();
            audit.append("setup.first_admin", user.id, "success",
                         "administrator mapped to OS principal");
            return response(201, "Created", "{\"status\":\"configured\"}");
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
            const auto result = OsIdentityProvider().authenticate(username, password);
            const auto user = result.authenticated()
                                  ? users->find_by_principal(result.principal)
                                  : std::nullopt;
            if (!user || !user->enabled) {
                audit.append("auth.login", username, "denied",
                             "OS authentication or principal mapping failed");
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

    std::string list_chats(const UserRecord& user) const {
        std::string body{"{\"chats\":["};
        bool first = true;
        for (const auto& chat : chats->list_for_owner(user.id)) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(chat.id) +
                    "\",\"projectId\":\"" + json_escape(chat.project_id) +
                    "\",\"modelId\":\"" + json_escape(chat.model_id) +
                    "\",\"messageCount\":" +
                    std::to_string(chat.messages.size()) + "}";
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
        }
        const auto hardware = probe_hardware(configuration.models_root);
        for (const auto& model :
             ModelRegistry(configuration.models_root, hardware,
                           configuration.memory_reserve_mib).scan()) {
            if (model.manifest.id == model_id) {
                inference->load(model, 4096U, configuration.runner_port);
                return;
            }
        }
        throw std::runtime_error("selected chat model was not found");
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
            const auto inference_prompt =
                assemble_inference_prompt(root, *chat, user);
            queries.transition(query_id, QueryStage::ranking,
                               QueryStatus::retrieving);
            queries.transition(query_id, QueryStage::prompt_assembly,
                               QueryStatus::retrieving);
            chats->append(chat_id, ChatRole::user, prompt);
            std::atomic_bool cancellation{false};
            GenerationOptions options;
            MemoryEstimate memory_estimate;
            memory_estimate.runtime_buffer_bytes = 64ULL * 1024ULL * 1024ULL;
            memory_estimate.kv_bytes_per_sequence =
                128ULL * 1024ULL * 1024ULL;
            memory_estimate.transient_bytes =
                static_cast<std::uint64_t>(inference_prompt.size()) * 3U;
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
                inference_prompt, options,
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
                    if (streaming &&
                        !send_chunk(stream_socket,
                                    "{\"type\":\"token\",\"content\":\"" +
                                        json_escape(chunk) + "\"}\n")) {
                        cancellation.store(true);
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
            queries.transition(query_id, QueryStage::persistence,
                               QueryStatus::generating);
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
        } catch (const std::exception&) {
            if (!memory_lease_id.empty()) {
                memory->release(memory_lease_id);
            }
            if (!query_id.empty()) {
                try {
                    queries.finish(query_id, QueryStatus::failed,
                                   "generation failed");
                } catch (const std::exception&) {
                }
            }
            audit.append("chat.generate", user.id, "failed", chat_id);
            if (stream_started) {
                send_chunk(stream_socket,
                           "{\"type\":\"error\",\"error\":\"generation_failed\"}\n");
                send_all(stream_socket, "0\r\n\r\n");
                return {};
            }
            return response(400, "Bad Request",
                            "{\"error\":\"generation_failed\"}");
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
    std::unique_ptr<AttachmentStore> attachments;
    std::unique_ptr<RunnerSupervisor> inference;
    std::unique_ptr<DownloadManager> downloads;
    std::unique_ptr<BenchmarkStore> benchmarks;
    std::unique_ptr<server_internal::WorkloadHttpController> workloads;
    std::unique_ptr<MemoryBudgetManager> memory;
    std::unique_ptr<ProjectIndexService> indexes;
    QueryCoordinator queries;
    std::string setup_token;
    std::string setup_hash;
    std::vector<std::uint64_t> request_times;
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
        const DWORD timeout_ms = configuration_.request_timeout_seconds * 1000U;
        setsockopt(client, SOL_SOCKET, SO_RCVTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
        setsockopt(client, SOL_SOCKET, SO_SNDTIMEO,
                   reinterpret_cast<const char*>(&timeout_ms), sizeof(timeout_ms));
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
            const auto received =
                recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
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
                const auto received =
                    recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
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
        close_socket(client);
    }
    close_socket(listener);
    socket_ = -1;
    std::filesystem::remove(stop_file);
    log(LogLevel::info, "server.stopped", "Graceful stop completed.");
    return true;
}

void HttpServer::stop() noexcept {
    if (socket_ == -1) {
        return;
    }
    const auto listener = static_cast<NativeSocket>(socket_);
#if defined(_WIN32)
    shutdown(listener, SD_BOTH);
#else
    shutdown(listener, SHUT_RDWR);
#endif
    close_socket(listener);
    socket_ = -1;
}

}  // namespace masterai
