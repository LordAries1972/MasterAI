// MasterAI private HTTP routing contracts shared by focused server source units.
//
// This header keeps transport parsing in server.cpp while allowing MCP and IDE
// HTTP operations to live in integration_http.cpp. It is not installed as part
// of the public API and contains no independent authorization policy.
#pragma once

#include "masterai.hpp"

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace masterai::server_internal {

struct Request final {
    std::string method;
    std::string target;
    std::map<std::string, std::string> headers;
    std::string body;
};

// Builds the one canonical JSON HTTP response envelope used by every handler.
std::string response(int status, const char* reason, const std::string& body,
                     const std::vector<std::string>& headers = {});

// Escapes untrusted text before it is embedded in an existing JSON envelope.
std::string json_escape(const std::string& value);

// Builds the one canonical hardened HTML response envelope.
std::string html_response(const std::string& body);

// Escapes untrusted text before insertion into a static HTML document.
std::string html_escape(const std::string& value);

// Returns the dependency-free browser client served by the loopback UI.
std::string application_script();

// Returns the OS-account sign-in document.
std::string login_page();

// Returns the authenticated programming workspace document.
std::string application_page(const UserRecord& user);

class IntegrationHttpController final {
public:
    IntegrationHttpController(UserStore& users, ApiTokenStore& api_tokens,
                              ProjectCatalog& projects,
                              McpInboundServer& inbound,
                              McpOutboundRegistry& outbound_registry,
                              McpOutboundGateway& outbound_gateway,
                              IdeIntegrationService& ide, AuditLog& audit);
    ~IntegrationHttpController();

    IntegrationHttpController(const IntegrationHttpController&) = delete;
    IntegrationHttpController& operator=(const IntegrationHttpController&) =
        delete;

    // Selects the minimum bearer scope for MCP and IDE control-plane routes.
    std::string required_scope(const Request& request) const;

    // Handles bearer-only Streamable HTTP MCP before browser authentication.
    std::string handle_inbound_mcp(Request& request);

    // Handles an authenticated MCP/IDE route, or returns no value when the
    // request belongs to another control-plane service.
    std::optional<std::string> handle_authenticated(
        Request& request, const UserRecord& user, bool cookie_authenticated,
        std::set<std::string> authenticated_scopes,
        std::set<std::string> authenticated_project_ids);

private:
    struct State;
    std::unique_ptr<State> state_;
};

class WorkloadHttpController final {
public:
    WorkloadHttpController(const AppConfig& configuration,
                           ProjectCatalog& projects,
                           AttachmentStore& attachments,
                           RunnerSupervisor* inference,
                           DownloadManager* downloads,
                           BenchmarkStore& benchmarks,
                           ProjectIndexService& indexes, AuditLog& audit);
    ~WorkloadHttpController();

    WorkloadHttpController(const WorkloadHttpController&) = delete;
    WorkloadHttpController& operator=(const WorkloadHttpController&) = delete;

    std::string model_inventory() const;
    std::string model_inventory_page() const;
    std::string list_projects() const;
    std::string create_project(Request& request, const UserRecord& user);
    std::string index_status(
        Request& request, bool cookie_authenticated,
        const std::set<std::string>& authenticated_project_ids) const;
    std::string rebuild_index(
        Request& request, const UserRecord& user, bool cookie_authenticated,
        const std::set<std::string>& authenticated_project_ids);
    std::string cancel_index(
        Request& request, const UserRecord& user, bool cookie_authenticated,
        const std::set<std::string>& authenticated_project_ids);
    std::string notify_index(
        Request& request, const UserRecord& user, bool cookie_authenticated,
        const std::set<std::string>& authenticated_project_ids);
    std::string create_attachment(Request& request, const UserRecord& user);
    std::string load_model(Request& request, const UserRecord& user);
    std::string unload_model(const UserRecord& user);
    std::string list_downloads() const;
    std::string create_download(Request& request, const UserRecord& user);
    std::string run_download(Request& request, const UserRecord& user);
    std::string list_benchmarks() const;
    std::string recommend_benchmark(Request& request) const;
    std::string run_benchmark(Request& request, const UserRecord& user);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace masterai::server_internal
