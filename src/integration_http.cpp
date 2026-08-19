// MasterAI MCP, API-token, and IDE HTTP integration operations.
//
// This source unit owns only protocol-facing integration routes. The loopback
// HTTP transport remains in server.cpp, while authorization and operation
// parsing stay together here so browser and bearer callers cannot bypass the
// native MCP/IDE policy services.
#include "server_internal.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>

namespace masterai::server_internal {
namespace {

// Returns current epoch seconds for token validation and issuance.
std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Removes sensitive request bytes immediately after strict JSON parsing.
JsonValue take_json_body(Request& request) {
    auto root = parse_json(request.body);
    std::fill(request.body.begin(), request.body.end(), '\0');
    request.body.clear();
    return root;
}

// Clears request bytes on rejected operations without duplicating wipe logic.
void clear_request_body(Request& request) noexcept {
    std::fill(request.body.begin(), request.body.end(), '\0');
    request.body.clear();
}

// Rejects unknown JSON members so new authority cannot appear implicitly.
void require_fields(const JsonValue& root,
                    const std::set<std::string>& permitted) {
    for (const auto& field : root.as_object()) {
        if (permitted.find(field.first) == permitted.end()) {
            throw std::runtime_error("unexpected integration request field");
        }
    }
}

}  // namespace

struct IntegrationHttpController::State final {
    State(UserStore& user_store, ApiTokenStore& token_store,
          ProjectCatalog& project_catalog, McpInboundServer& inbound_server,
          McpOutboundRegistry& registry, McpOutboundGateway& gateway,
          IdeIntegrationService& ide_service, AuditLog& audit_log)
        : users(user_store), api_tokens(token_store),
          projects(project_catalog), inbound(inbound_server),
          outbound_registry(registry), outbound_gateway(gateway),
          ide(ide_service), audit(audit_log) {}

    UserStore& users;
    ApiTokenStore& api_tokens;
    ProjectCatalog& projects;
    McpInboundServer& inbound;
    McpOutboundRegistry& outbound_registry;
    McpOutboundGateway& outbound_gateway;
    IdeIntegrationService& ide;
    AuditLog& audit;
    std::atomic_bool outbound_cancellation{false};

    // Validates and returns the role-permitted scopes in one bounded helper.
    std::optional<std::set<std::string>> permitted_scopes(
        const JsonValue& root, const UserRecord& user) const {
        std::set<std::string> scopes;
        for (const auto& value : root.required("scopes").as_array()) {
            const auto& scope = value.as_string();
            if (!role_allows(user.role, scope) ||
                !scopes.insert(scope).second) {
                return std::nullopt;
            }
        }
        return scopes;
    }

    // Validates explicit project bindings without granting an implicit root.
    std::optional<std::set<std::string>> permitted_projects(
        const JsonValue& root, const UserRecord& user) const {
        std::set<std::string> project_ids;
        const auto* values = root.optional("projects");
        if (values == nullptr) return project_ids;
        for (const auto& value : values->as_array()) {
            const auto& id = value.as_string();
            if (!projects.find(id) || !project_ids.insert(id).second ||
                !role_allows(user.role, "projects.read")) {
                return std::nullopt;
            }
        }
        return project_ids;
    }

    // Issues a scoped bearer token and binds project access explicitly.
    std::string create_api_token(Request& request, const UserRecord& user) {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() < 2U ||
                root.as_object().size() > 3U) {
                throw std::runtime_error("unexpected API token field count");
            }
            require_fields(root, {"scopes", "expiresMinutes", "projects"});
            const auto scopes = permitted_scopes(root, user);
            if (!scopes) {
                return response(403, "Forbidden",
                                "{\"error\":\"scope_not_permitted\"}");
            }
            const auto minutes = root.required("expiresMinutes").as_integer();
            if (scopes->empty() || minutes <= 0 || minutes > 43200) {
                throw std::runtime_error("API token lifetime is invalid");
            }
            const auto project_ids = permitted_projects(root, user);
            if (!project_ids) {
                return response(
                    403, "Forbidden",
                    "{\"error\":\"project_binding_not_permitted\"}");
            }
            auto token = api_tokens.create(
                user.id, *scopes, epoch_seconds(),
                static_cast<std::uint64_t>(minutes) * 60U, *project_ids);
            audit.append("token.create", user.id, "success",
                         "scoped client token issued");
            const auto result =
                response(201, "Created", "{\"token\":\"" + token + "\"}");
            std::fill(token.begin(), token.end(), '\0');
            return result;
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_token_request\"}");
        }
    }

    // Revokes one submitted bearer token and erases its plaintext bytes.
    std::string revoke_api_token(Request& request, const UserRecord& user) {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() != 1U) {
                throw std::runtime_error("unexpected revocation field count");
            }
            auto token = root.required_mutable("token").take_string();
            api_tokens.revoke(token);
            std::fill(token.begin(), token.end(), '\0');
            audit.append("token.revoke", user.id, "success",
                         "client token revoked");
            return response(204, "No Content", "");
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_revocation_request\"}");
        }
    }

    // Lists outbound MCP registrations without exposing stored credentials.
    std::string list_outbound_servers() const {
        std::string body{"{\"servers\":["};
        bool first = true;
        for (const auto& server : outbound_registry.list()) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(server.id) +
                    "\",\"transport\":\"" +
                    std::string(
                        server.transport == McpTransport::stdio_transport
                            ? "stdio"
                            : "streamable-http") +
                    "\",\"enabled\":" +
                    std::string(server.enabled ? "true" : "false") +
                    ",\"endpoint\":\"" + json_escape(server.endpoint) +
                    "\",\"executable\":\"" +
                    json_escape(server.executable.string()) +
                    "\",\"executableSha256\":\"" +
                    json_escape(server.executable_sha256) + "\"}";
        }
        return response(200, "OK", body + "]}");
    }

    // Parses and registers one explicitly constrained outbound MCP server.
    std::string register_outbound_server(Request& request,
                                         const UserRecord& user) {
        try {
            auto root = take_json_body(request);
            require_fields(
                root,
                {"id", "transport", "executable", "arguments",
                 "workingDirectory", "endpoint", "allowedTools",
                 "allowedProjects", "credentialSecretName", "timeoutSeconds",
                 "maximumOutputBytes", "memoryLimitMiB"});
            McpOutboundServer server;
            server.id = root.required("id").as_string();
            const auto transport = root.required("transport").as_string();
            if (transport == "stdio") {
                server.transport = McpTransport::stdio_transport;
            } else if (transport == "streamable-http") {
                server.transport = McpTransport::streamable_http;
            } else {
                throw std::runtime_error("unsupported MCP transport");
            }
            if (const auto* value = root.optional("executable")) {
                server.executable = value->as_string();
            }
            if (const auto* values = root.optional("arguments")) {
                for (const auto& value : values->as_array()) {
                    server.arguments.push_back(value.as_string());
                }
            }
            if (const auto* value = root.optional("workingDirectory")) {
                server.working_directory = value->as_string();
            }
            if (const auto* value = root.optional("endpoint")) {
                server.endpoint = value->as_string();
            }
            for (const auto& value :
                 root.required("allowedTools").as_array()) {
                if (!server.allowed_tools.insert(value.as_string()).second) {
                    throw std::runtime_error("duplicate outbound MCP tool");
                }
            }
            if (const auto* values = root.optional("allowedProjects")) {
                for (const auto& value : values->as_array()) {
                    const auto& id = value.as_string();
                    if (!projects.find(id) ||
                        !server.allowed_project_ids.insert(id).second) {
                        throw std::runtime_error("invalid outbound project");
                    }
                }
            }
            if (const auto* value = root.optional("credentialSecretName")) {
                server.credential_secret_name = value->as_string();
            }
            if (const auto* value = root.optional("timeoutSeconds")) {
                server.timeout_seconds =
                    static_cast<std::uint64_t>(value->as_integer());
            }
            if (const auto* value = root.optional("maximumOutputBytes")) {
                server.maximum_output_bytes =
                    static_cast<std::uint64_t>(value->as_integer());
            }
            if (const auto* value = root.optional("memoryLimitMiB")) {
                server.memory_limit_mib =
                    static_cast<std::uint64_t>(value->as_integer());
            }
            server.enabled = true;
            const auto registered =
                outbound_registry.register_server(std::move(server));
            audit.append("mcp.registry.add", user.id, "success",
                         registered.id);
            return response(
                201, "Created",
                "{\"id\":\"" + json_escape(registered.id) +
                    "\",\"executableSha256\":\"" +
                    json_escape(registered.executable_sha256) + "\"}");
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(
                400, "Bad Request",
                "{\"error\":\"invalid_mcp_server_registration\"}");
        }
    }

    // Removes a selected registration so its tool authority ends immediately.
    std::string remove_outbound_server(Request& request,
                                       const UserRecord& user) {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() != 1U) {
                throw std::runtime_error("unexpected removal field count");
            }
            const auto id = root.required("id").as_string();
            outbound_registry.remove(id);
            audit.append("mcp.registry.remove", user.id, "success", id);
            return response(204, "No Content", "");
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_mcp_server_removal\"}");
        }
    }

    // Invokes one user-approved MCP tool with caller scopes and project bounds.
    std::string call_outbound(
        Request& request, const UserRecord& user,
        const std::set<std::string>& authenticated_scopes,
        const std::set<std::string>& authenticated_project_ids) {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() != 5U) {
                throw std::runtime_error("unexpected outbound call field count");
            }
            require_fields(root, {"serverId", "tool", "argumentsJson",
                                  "projectId", "approved"});
            McpOutboundCall call;
            call.server_id = root.required("serverId").as_string();
            call.tool = root.required("tool").as_string();
            call.arguments_json =
                root.required("argumentsJson").as_string();
            call.project_id = root.required("projectId").as_string();
            call.user_approved = root.required("approved").as_boolean();
            call.actor_id = user.id;
            call.actor_scopes = authenticated_scopes;
            call.actor_project_ids = authenticated_project_ids;
            outbound_cancellation.store(false);
            const auto result =
                outbound_gateway.invoke(call, outbound_cancellation);
            if (result.cancelled) {
                return response(409, "Conflict",
                                "{\"error\":\"mcp_call_cancelled\"}");
            }
            if (!result.succeeded) {
                return response(
                    409, "Conflict",
                    "{\"error\":\"mcp_call_failed\",\"diagnostic\":\"" +
                        json_escape(result.diagnostic) + "\"}");
            }
            return response(200, "OK", result.response_json);
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_mcp_call\"}");
        }
    }

    // Runs deterministic read-only diagnostics for one authorized project file.
    std::string ide_diagnostics(
        Request& request,
        const std::set<std::string>& authenticated_project_ids) const {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() != 2U) {
                throw std::runtime_error("unexpected diagnostic field count");
            }
            require_fields(root, {"projectId", "path"});
            return response(
                200, "OK",
                ide.diagnostics_json(
                    root.required("projectId").as_string(),
                    root.required("path").as_string(),
                    authenticated_project_ids));
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(
                400, "Bad Request",
                "{\"error\":\"invalid_ide_diagnostic_request\"}");
        }
    }

    // Previews a bounded unified diff without modifying project files.
    std::string ide_diff_preview(
        Request& request,
        const std::set<std::string>& authenticated_project_ids) const {
        try {
            auto root = take_json_body(request);
            if (root.as_object().size() != 2U) {
                throw std::runtime_error("unexpected diff field count");
            }
            require_fields(root, {"projectId", "unifiedDiff"});
            return response(
                200, "OK",
                ide.diff_preview_json(
                    root.required("projectId").as_string(),
                    root.required("unifiedDiff").as_string(),
                    authenticated_project_ids));
        } catch (const std::exception&) {
            clear_request_body(request);
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_ide_diff_preview\"}");
        }
    }
};

// Stores service references in a private state so server internals stay private.
IntegrationHttpController::IntegrationHttpController(
    UserStore& users, ApiTokenStore& api_tokens, ProjectCatalog& projects,
    McpInboundServer& inbound, McpOutboundRegistry& outbound_registry,
    McpOutboundGateway& outbound_gateway, IdeIntegrationService& ide,
    AuditLog& audit)
    : state_(std::make_unique<State>(
          users, api_tokens, projects, inbound, outbound_registry,
          outbound_gateway, ide, audit)) {}

IntegrationHttpController::~IntegrationHttpController() = default;

// Maps only recognized integration endpoints to bearer scopes.
std::string IntegrationHttpController::required_scope(
    const Request& request) const {
    if (request.method == "GET" &&
        request.target == "/api/v1/mcp/outbound/servers") {
        return "mcp.tools.invoke";
    }
    if (request.method == "POST" &&
        request.target == "/api/v1/mcp/outbound/calls") {
        return "mcp.tools.invoke";
    }
    if (request.method == "POST" &&
        request.target.rfind("/api/v1/mcp/outbound/servers", 0U) == 0U) {
        return "settings.manage";
    }
    if ((request.method == "GET" || request.method == "POST") &&
        request.target.rfind("/api/v1/ide/", 0U) == 0U) {
        return "ide.connect";
    }
    return {};
}

// Validates Streamable HTTP headers and bearer identity before MCP dispatch.
std::string IntegrationHttpController::handle_inbound_mcp(Request& request) {
    if (request.method == "GET") {
        return response(405, "Method Not Allowed",
                        "{\"error\":\"mcp_sse_not_enabled\"}",
                        {"Allow: POST"});
    }
    if (request.method != "POST") {
        return response(405, "Method Not Allowed",
                        "{\"error\":\"method_not_allowed\"}",
                        {"Allow: POST"});
    }
    // The Streamable HTTP transport spec requires the MCP-Protocol-Version
    // header on every request *after* negotiation, but the client cannot
    // know the negotiated version before its first "initialize" call
    // completes -- that negotiation happens via the JSON-RPC body's own
    // protocolVersion field (McpInboundServer::handle, src/mcp.cpp), not
    // this header. Enforcing the header on "initialize" itself rejects
    // every spec-compliant external client on first contact (confirmed
    // 2026-08-18 against the official @modelcontextprotocol/inspector,
    // which -- correctly per spec -- omits this header on its first
    // request). So this header is only required starting with the second
    // request of a session.
    bool is_initialize_call = false;
    try {
        const auto peek = parse_json(request.body);
        const auto* method = peek.optional("method");
        is_initialize_call =
            method != nullptr && method->as_string() == "initialize";
    } catch (const std::exception&) {
        // Malformed JSON is rejected below by McpInboundServer::handle
        // itself with a proper JSON-RPC parse error; fall through to the
        // ordinary header requirement rather than misreport it here.
    }
    const auto protocol = request.headers.find("mcp-protocol-version");
    if (!is_initialize_call &&
        (protocol == request.headers.end() ||
         protocol->second != mcp_protocol_version())) {
        return response(400, "Bad Request",
                        "{\"error\":\"unsupported_mcp_protocol\"}");
    }
    const auto content_type = request.headers.find("content-type");
    const auto accept = request.headers.find("accept");
    if (content_type == request.headers.end() ||
        content_type->second.find("application/json") == std::string::npos ||
        accept == request.headers.end() ||
        accept->second.find("application/json") == std::string::npos) {
        return response(406, "Not Acceptable",
                        "{\"error\":\"mcp_media_type_required\"}");
    }
    const auto authorization = request.headers.find("authorization");
    if (authorization == request.headers.end() ||
        authorization->second.rfind("Bearer ", 0U) != 0U) {
        return response(401, "Unauthorized",
                        "{\"error\":\"mcp_bearer_required\"}");
    }
    const auto token = state_->api_tokens.validate(
        authorization->second.substr(7U), "mcp.connect", epoch_seconds());
    if (!token) {
        return response(403, "Forbidden",
                        "{\"error\":\"mcp_token_rejected\"}");
    }
    const auto user = state_->users.find_by_id(token->user_id);
    if (!user || !user->enabled) {
        return response(403, "Forbidden", "{\"error\":\"user_disabled\"}");
    }
    std::atomic_bool cancellation{false};
    const McpIdentity identity{token->user_id, token->scopes,
                               token->project_ids};
    const auto result =
        state_->inbound.handle(request.body, identity, cancellation);
    state_->audit.append("mcp.inbound", token->user_id, "processed",
                         result.empty() ? "notification" : "request");
    return result.empty() ? response(202, "Accepted", "")
                          : response(200, "OK", result);
}

// Routes authenticated integration operations through one policy boundary.
std::optional<std::string> IntegrationHttpController::handle_authenticated(
    Request& request, const UserRecord& user, const bool cookie_authenticated,
    std::set<std::string> authenticated_scopes,
    std::set<std::string> authenticated_project_ids) {
    if (request.method == "POST" && request.target == "/api/v1/tokens") {
        if (!cookie_authenticated ||
            !role_allows(user.role, "tokens.create")) {
            return response(403, "Forbidden",
                            "{\"error\":\"permission_denied\"}");
        }
        return state_->create_api_token(request, user);
    }
    if (request.method == "POST" &&
        request.target == "/api/v1/tokens/revoke") {
        return cookie_authenticated
                   ? state_->revoke_api_token(request, user)
                   : response(403, "Forbidden",
                              "{\"error\":\"permission_denied\"}");
    }
    if (request.target == "/api/v1/mcp/outbound/servers" ||
        request.target == "/api/v1/mcp/outbound/servers/remove") {
        if (!cookie_authenticated ||
            !role_allows(user.role, "settings.manage")) {
            return response(403, "Forbidden",
                            "{\"error\":\"permission_denied\"}");
        }
        if (request.method == "GET") {
            return state_->list_outbound_servers();
        }
        return request.target == "/api/v1/mcp/outbound/servers"
                   ? state_->register_outbound_server(request, user)
                   : state_->remove_outbound_server(request, user);
    }
    if (request.method == "POST" &&
        request.target == "/api/v1/mcp/outbound/calls") {
        if (cookie_authenticated) {
            if (role_allows(user.role, "mcp.tools.invoke")) {
                authenticated_scopes.insert("mcp.tools.invoke");
            }
            if (role_allows(user.role, "projects.read")) {
                for (const auto& project : state_->projects.list()) {
                    authenticated_project_ids.insert(project.id);
                }
            }
        }
        return state_->call_outbound(
            request, user, authenticated_scopes,
            authenticated_project_ids);
    }
    if (request.method == "GET" &&
        request.target == "/api/v1/ide/capabilities") {
        return response(200, "OK", state_->ide.capabilities_json());
    }
    if (request.method == "POST" &&
        (request.target == "/api/v1/ide/diagnostics" ||
         request.target == "/api/v1/ide/diff-preview")) {
        if (cookie_authenticated &&
            role_allows(user.role, "projects.read")) {
            for (const auto& project : state_->projects.list()) {
                authenticated_project_ids.insert(project.id);
            }
        }
        return request.target == "/api/v1/ide/diagnostics"
                   ? state_->ide_diagnostics(
                         request, authenticated_project_ids)
                   : state_->ide_diff_preview(
                         request, authenticated_project_ids);
    }
    return std::nullopt;
}

}  // namespace masterai::server_internal
