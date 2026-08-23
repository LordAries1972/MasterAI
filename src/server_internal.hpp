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

// Phase 30: byte-for-byte identical output to json_escape(), but written
// into a std::vector<std::uint8_t> instead of a std::string so the caller
// can move it straight into a SharedBuffer (SharedBuffer's move constructor
// takes ownership of a std::vector<std::uint8_t> with no copy) and stream it
// to a socket via a BufferView -- see server.cpp's streaming token path,
// which is the one hot copy chain Phase 30 targets this session. Shares its
// escaping rules with json_escape() via one internal template so the two
// can never silently diverge.
std::vector<std::uint8_t> json_escape_bytes(const std::string& value);

// Builds the one canonical hardened HTML response envelope.
std::string html_response(const std::string& body);

// Escapes untrusted text before insertion into a static HTML document.
std::string html_escape(const std::string& value);

// Returns the dependency-free browser client served by the loopback UI.
std::string application_script();

// Returns the OS-account sign-in document.
std::string login_page();

// Returns one authenticated workspace document for a single section --
// "chat", "projects", "models-inventory", "models-download",
// "models-downloads", "models-benchmarks", "admin-create", or "admin-users".
// Each section is served at its own URL (see server.cpp's /app/* routes) so
// switching sections is a normal full-page navigation, not a client-side
// panel swap. chat_id, when non-empty, preloads that conversation's history
// on the chat section. last_chat_title, when non-empty, is the caller's
// most recent chat's real (model-derived) title -- only used by the "chat"
// section's empty-state greeting (see chat_welcome_message's own comment)
// to sometimes ground a returning user back in what they were last doing.
std::string application_page(const UserRecord& user, const std::string& section,
                             const std::string& chat_id = "",
                             const std::string& last_chat_title = "");

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
                           ProjectIndexService& indexes, AuditLog& audit,
                           CacheManager& cache,
                           RequestScheduler& scheduler,
                           PerformanceCertificationStore& certifications);
    ~WorkloadHttpController();

    WorkloadHttpController(const WorkloadHttpController&) = delete;
    WorkloadHttpController& operator=(const WorkloadHttpController&) = delete;

    std::string model_inventory() const;
    std::string model_inventory_page() const;
    // GET /api/v1/model-catalog (Agent-Coder integration): the same curated
    // suggestion list the web download page's PRESETS JS array now renders
    // from (see model_catalog.hpp), exposed as its own JSON route so a
    // non-browser client (e.g. a VS Code extension) can read it without
    // scraping page script. Deliberately independent of model_inventory()
    // above -- that route reports locally scanned/verified state for models
    // already on disk, while this one reports the static, code-shipped list
    // of models available to download in the first place.
    std::string model_catalog() const;
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
    std::string pause_download(Request& request, const UserRecord& user);
    std::string cancel_download(Request& request, const UserRecord& user);
    std::string remove_download(Request& request, const UserRecord& user);
    std::string list_benchmarks() const;
    std::string recommend_benchmark(Request& request) const;
    std::string run_benchmark(Request& request, const UserRecord& user);
    // Phase 36: the full performance benchmark matrix and regression gate.
    // See PerformanceCertificationRunner's class comment (masterai.hpp) for
    // the honest scope this operates under.
    std::string list_certifications() const;
    std::string run_certification(Request& request, const UserRecord& user);
    std::string get_certification_thresholds() const;
    std::string set_certification_thresholds(Request& request,
                                             const UserRecord& user);

private:
    struct State;
    std::unique_ptr<State> state_;
};

}  // namespace masterai::server_internal
