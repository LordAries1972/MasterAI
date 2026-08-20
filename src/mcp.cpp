// MasterAI Model Context Protocol inbound implementation.
//
// This unit implements the protocol revision pinned by the project over a
// transport-neutral JSON-RPC dispatcher and newline-delimited stdio runner.
// It exposes a deliberately small programming tool/resource surface, maps
// bearer-token scopes and project bindings into every operation, and keeps
// filesystem reads bounded, UTF-8-only, and contained within registered roots.

#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>

namespace masterai {
namespace {

constexpr std::size_t maximum_mcp_message_bytes = 1024U * 1024U;

// Preserves a valid JSON-RPC string or integer identifier in the response.
// Notifications have no identifier and therefore use null only for errors.
std::string request_id(const JsonValue* value) {
    if (value == nullptr || value->type() == JsonValue::Type::null_value) {
        return "null";
    }
    if (value->type() == JsonValue::Type::string) {
        return json_string(value->as_string());
    }
    if (value->type() == JsonValue::Type::number) {
        return std::to_string(value->as_integer());
    }
    throw std::runtime_error("JSON-RPC id must be a string, integer, or null");
}

// Produces a standards-shaped protocol error without exposing exception text
// or local paths to the client.
std::string rpc_error(const std::string& id, const int code,
                      const std::string& message) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
           ",\"error\":{\"code\":" + std::to_string(code) +
           ",\"message\":" + json_string(message) + "}}";
}

// Wraps tool output in MCP text content and structured JSON. The duplicated
// text form keeps compatibility with hosts that do not consume structuredContent.
std::string tool_result(const std::string& id, const std::string& text,
                        const std::string& structured_json,
                        const bool is_error = false) {
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
           ",\"result\":{\"content\":[{\"type\":\"text\",\"text\":" +
           json_string(text) + "}],\"structuredContent\":" + structured_json +
           (is_error ? ",\"isError\":true" : "") + "}}";
}

// Enforces exact argument schemas. MCP tool input remains deny-by-default when
// clients send misspelled, obsolete, or attacker-controlled extra fields.
bool has_only_fields(const JsonValue::Object& object,
                     const std::set<std::string>& allowed) {
    return std::all_of(
        object.begin(), object.end(), [&](const auto& item) {
            return allowed.find(item.first) != allowed.end();
        });
}

// Confirms that a call carries both its named scope and, for project tools, an
// explicit binding to the requested registered project.
bool authorized(const McpIdentity& identity, const std::string& scope,
                const std::string& project_id = {}) {
    if (identity.user_id.empty() ||
        identity.scopes.find(scope) == identity.scopes.end()) {
        return false;
    }
    return project_id.empty() ||
           identity.project_ids.find(project_id) != identity.project_ids.end();
}

// Builds the programming-focused tool catalogue with closed JSON schemas and
// read-only/destructive annotations that IDE hosts can present accurately.
std::string tools_catalogue() {
    return
        "{\"tools\":["
        "{\"name\":\"masterai.projects.list\",\"title\":\"List authorized "
        "projects\",\"description\":\"List only projects explicitly bound to "
        "this client token.\",\"inputSchema\":{\"type\":\"object\","
        "\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,"
        "\"destructiveHint\":false}},"
        "{\"name\":\"masterai.project.read_file\",\"title\":\"Read project "
        "file\",\"description\":\"Read one bounded UTF-8 source file from an "
        "authorized project.\",\"inputSchema\":{\"type\":\"object\","
        "\"properties\":{\"projectId\":{\"type\":\"string\"},\"path\":{"
        "\"type\":\"string\"}},\"required\":[\"projectId\",\"path\"],"
        "\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,"
        "\"destructiveHint\":false}},"
        "{\"name\":\"masterai.project.search\",\"title\":\"Search project "
        "text\",\"description\":\"Find literal text in bounded source files "
        "inside an authorized project.\",\"inputSchema\":{\"type\":\"object\","
        "\"properties\":{\"projectId\":{\"type\":\"string\"},\"query\":{"
        "\"type\":\"string\"}},\"required\":[\"projectId\",\"query\"],"
        "\"additionalProperties\":false},\"annotations\":{\"readOnlyHint\":true,"
        "\"destructiveHint\":false}},"
        "{\"name\":\"masterai.project.list_directory\",\"title\":\"List "
        "project directory\",\"description\":\"List one bounded directory's "
        "immediate entries inside an authorized project.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"projectId\":{"
        "\"type\":\"string\"},\"path\":{\"type\":\"string\"}},\"required\":["
        "\"projectId\"],\"additionalProperties\":false},\"annotations\":{"
        "\"readOnlyHint\":true,\"destructiveHint\":false}},"
        "{\"name\":\"masterai.project.write_file\",\"title\":\"Write "
        "project file\",\"description\":\"Create or overwrite one bounded "
        "UTF-8 file inside an authorized project. Blanking an existing "
        "file's content is classified high-risk and refused through MCP; "
        "complete that specific call from the MasterAI web chat instead, "
        "where it pauses for a human Approve/Deny decision.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"projectId\":{"
        "\"type\":\"string\"},\"path\":{\"type\":\"string\"},\"content\":{"
        "\"type\":\"string\"}},\"required\":[\"projectId\",\"path\","
        "\"content\"],\"additionalProperties\":false},\"annotations\":{"
        "\"readOnlyHint\":false,\"destructiveHint\":false}},"
        "{\"name\":\"masterai.project.delete_file\",\"title\":\"Delete "
        "project file\",\"description\":\"Delete one file inside an "
        "authorized project. Always classified high-risk and always "
        "refused through MCP; complete it from the MasterAI web chat "
        "instead, where it pauses for a human Approve/Deny decision.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"projectId\":{"
        "\"type\":\"string\"},\"path\":{\"type\":\"string\"}},\"required\":["
        "\"projectId\",\"path\"],\"additionalProperties\":false},"
        "\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":true}},"
        "{\"name\":\"masterai.project.run_command\",\"title\":\"Run "
        "allow-listed command\",\"description\":\"Run one admin-approved "
        "external executable bounded to an authorized project. A command "
        "matching the destructive-pattern table is classified high-risk "
        "and always refused through MCP; complete it from the MasterAI web "
        "chat instead, where it pauses for a human Approve/Deny decision.\","
        "\"inputSchema\":{\"type\":\"object\",\"properties\":{\"projectId\":{"
        "\"type\":\"string\"},\"executable\":{\"type\":\"string\"},"
        "\"arguments\":{\"type\":\"array\",\"items\":{\"type\":\"string\"}}},"
        "\"required\":[\"projectId\",\"executable\"],\"additionalProperties\":"
        "false},\"annotations\":{\"readOnlyHint\":false,\"destructiveHint\":"
        "true}},"
        "{\"name\":\"masterai.models.list\",\"title\":\"List local models\","
        "\"description\":\"List verified local model inventory and readiness.\","
        "\"inputSchema\":{\"type\":\"object\",\"additionalProperties\":false},"
        "\"annotations\":{\"readOnlyHint\":true,\"destructiveHint\":false}}]}";
}

// Materializes the token-visible project subset once for tools and resources.
std::vector<ProjectRecord> authorized_projects(
    ProjectCatalog& projects, const McpIdentity& identity) {
    const auto catalog = projects.list();
    std::vector<ProjectRecord> result;
    // Upper-bounded by the full catalog size, so reserving it avoids
    // reallocation growth while filtering down to the token's subset.
    result.reserve(catalog.size());
    for (const auto& project : catalog) {
        if (identity.project_ids.find(project.id) !=
            identity.project_ids.end()) {
            result.push_back(project);
        }
    }
    return result;
}

// Lists only token-bound projects, avoiding disclosure of unrelated workspace
// names even when they share the same MasterAI instance.
std::string list_projects(ProjectCatalog& projects,
                          const McpIdentity& identity) {
    std::string result{"{\"projects\":["};
    bool first = true;
    for (const auto& project : authorized_projects(projects, identity)) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":" + json_string(project.id) +
                  ",\"displayName\":" + json_string(project.display_name) + "}";
    }
    return result + "]}";
}

// Returns project metadata resources for the authenticated token. File content
// remains behind tools/call so every requested path receives an explicit check.
std::string list_resources(ProjectCatalog& projects,
                           const McpIdentity& identity) {
    std::string result{"{\"resources\":["};
    bool first = true;
    for (const auto& project : authorized_projects(projects, identity)) {
        if (!first) result += ",";
        first = false;
        result += "{\"uri\":\"masterai://project/" +
                  project.id + "\",\"name\":" +
                  json_string(project.display_name) +
                  ",\"description\":\"Authorized MasterAI project metadata\","
                  "\"mimeType\":\"application/json\"}";
    }
    return result + "]}";
}

// Converts current model-registry evidence into an MCP-safe inventory without
// exposing local absolute model paths.
std::string list_models(const std::filesystem::path& models_root,
                        const std::uint64_t memory_reserve_mib) {
    const auto hardware = probe_hardware(models_root);
    const auto models =
        ModelRegistry(models_root, hardware, memory_reserve_mib).scan();
    std::string result{"{\"models\":["};
    bool first = true;
    for (const auto& model : models) {
        if (!first) result += ",";
        first = false;
        result += "{\"id\":" + json_string(model.manifest.id) +
                  ",\"displayName\":" +
                  json_string(model.manifest.display_name) +
                  ",\"category\":" + json_string(model.manifest.category) +
                  ",\"state\":" +
                  std::to_string(static_cast<int>(model.state)) +
                  ",\"diagnostic\":" + json_string(model.diagnostic) + "}";
    }
    return result + "]}";
}

// Dispatches one "masterai.project.<tool>" call to the shared
// execute_chat_tool() (tool_exec.cpp), returning nullopt when `name` does not
// carry the project-tool prefix at all so the caller falls through to its own
// "Unknown tool" response. Split out of McpInboundServer::handle() purely to
// keep that function's nesting shallow -- the six execute_chat_tool() names,
// field schemas, scope, and risk-classification behavior are unchanged.
std::optional<std::string> handle_project_tool_call(
    const std::string& id, const std::string& name, const JsonValue& arguments,
    const JsonValue::Object& argument_object, const McpIdentity& identity,
    ProjectCatalog& projects, AllowedCommandStore& allowed_commands,
    std::atomic_bool& cancellation) {
    static const std::string project_tool_prefix{"masterai.project."};
    if (name.compare(0U, project_tool_prefix.size(), project_tool_prefix) !=
        0) {
        return std::nullopt;
    }
    // The suffix after "masterai.project." is exactly one of the six
    // execute_chat_tool() names (tool_exec.cpp) -- MCP calls the same
    // dispatch function chat's own tool loop calls, so behavior can never
    // drift between the two surfaces (masterai.hpp's execute_chat_tool()
    // comment).
    const auto tool_name = name.substr(project_tool_prefix.size());
    static const std::map<std::string, std::set<std::string>> kAllowedFields{
        {"read_file", {"projectId", "path"}},
        {"list_directory", {"projectId", "path"}},
        {"search", {"projectId", "query"}},
        {"write_file", {"projectId", "path", "content"}},
        {"delete_file", {"projectId", "path"}},
        {"run_command", {"projectId", "executable", "arguments"}},
    };
    static const std::map<std::string, std::set<std::string>> kRequiredFields{
        {"read_file", {"projectId", "path"}},
        {"list_directory", {"projectId"}},
        {"search", {"projectId", "query"}},
        {"write_file", {"projectId", "path", "content"}},
        {"delete_file", {"projectId", "path"}},
        {"run_command", {"projectId", "executable"}},
    };
    const auto allowed_it = kAllowedFields.find(tool_name);
    if (allowed_it == kAllowedFields.end()) {
        return rpc_error(id, -32602, "Unknown tool");
    }
    if (!has_only_fields(argument_object, allowed_it->second)) {
        return rpc_error(id, -32602, "Invalid project tool arguments");
    }
    for (const auto& field : kRequiredFields.at(tool_name)) {
        if (argument_object.find(field) == argument_object.end()) {
            return rpc_error(id, -32602, "Invalid project tool arguments");
        }
    }
    // Everything but read_file/list_directory/search can mutate the project,
    // so it needs the stronger write scope -- the same "projects.write" name
    // integration_http.cpp/workload_http.cpp already gate real project
    // mutation behind for HTTP callers.
    const bool is_write_tool = tool_name == "write_file" ||
                               tool_name == "delete_file" ||
                               tool_name == "run_command";
    const auto& project_id = arguments.required("projectId").as_string();
    const auto project = projects.find(project_id);
    if (!project || !authorized(identity,
                                is_write_tool ? "projects.write"
                                             : "projects.read",
                                project_id)) {
        return tool_result(id, "Project access denied.",
                           "{\"error\":\"permission_denied\"}", true);
    }
    try {
        // classify_tool_call_risk() is the single enforcement point for "a
        // destructive action always needs a human's explicit approval"
        // (masterai.hpp). MCP's tools/call is one synchronous round trip
        // with no Approve/Deny channel of its own yet, so -- unlike the chat
        // tool loop, which pauses on a PendingToolApproval -- a high-risk
        // call is refused outright here rather than silently downgraded to
        // safe or executed without a human decision.
        if (classify_tool_call_risk(tool_name, arguments) ==
            ChatToolRisk::high_risk) {
            return tool_result(
                id,
                "This call was classified high-risk (destructive). MCP has "
                "no interactive approval channel yet, so it was not "
                "executed -- complete it from the MasterAI web chat "
                "instead, where it pauses for a human Approve/Deny "
                "decision.",
                "{\"error\":\"approval_required\"}", true);
        }
        const auto result = execute_chat_tool(
            tool_name, arguments, *project, allowed_commands, cancellation);
        const std::string structured =
            result.structured_json.empty() ? "{}" : result.structured_json;
        return tool_result(id, result.result_text, structured,
                           !result.succeeded);
    } catch (const std::exception&) {
        return tool_result(id, "Project input was rejected by policy.",
                           "{\"error\":\"invalid_project_input\"}", true);
    }
}

}  // namespace

const char* mcp_protocol_version() noexcept { return "2025-11-25"; }

// Captures stable service dependencies. Project records remain live through the
// referenced catalogue while the model root and memory reserve are immutable.
McpInboundServer::McpInboundServer(ProjectCatalog& projects,
                                   std::filesystem::path models_root,
                                   const std::uint64_t memory_reserve_mib,
                                   AllowedCommandStore& allowed_commands)
    : projects_(projects), models_root_(std::move(models_root)),
      memory_reserve_mib_(memory_reserve_mib),
      allowed_commands_(allowed_commands) {
    if (models_root_.empty()) {
        throw std::invalid_argument("MCP model root is required");
    }
}

// Parses one bounded JSON-RPC message, performs protocol negotiation and
// per-operation authorization, and returns one single-line response. Invalid
// tool inputs become safe tool errors; malformed protocol messages use JSON-RPC
// errors. Notifications intentionally return an empty string.
std::string McpInboundServer::handle(const std::string& request_json,
                                     const McpIdentity& identity,
                                     std::atomic_bool& cancellation) const {
    if (request_json.empty() ||
        request_json.size() > maximum_mcp_message_bytes ||
        request_json.find('\0') != std::string::npos) {
        return rpc_error("null", -32700, "Parse error");
    }

    JsonValue root;
    try {
        root = parse_json(request_json);
    } catch (const std::exception&) {
        return rpc_error("null", -32700, "Parse error");
    }

    std::string id{"null"};
    try {
        static_cast<void>(root.as_object());
        id = request_id(root.optional("id"));
        if (root.required("jsonrpc").as_string() != "2.0") {
            return rpc_error(id, -32600, "Invalid Request");
        }
        const auto& method = root.required("method").as_string();

        if (method == "notifications/initialized") {
            return {};
        }
        if (method == "notifications/cancelled") {
            const auto* parameters = root.optional("params");
            if (parameters != nullptr &&
                parameters->optional("requestId") != nullptr) {
                cancellation.store(true);
            }
            return {};
        }
        if (root.optional("id") == nullptr) {
            return {};
        }
        if (method == "initialize") {
            const auto& parameters = root.required("params");
            if (parameters.required("protocolVersion").as_string() !=
                mcp_protocol_version()) {
                return rpc_error(id, -32602,
                                 "Unsupported MCP protocol version");
            }
            static_cast<void>(parameters.required("capabilities").as_object());
            static_cast<void>(parameters.required("clientInfo").as_object());
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                   ",\"result\":{\"protocolVersion\":\"" +
                   mcp_protocol_version() +
                   "\",\"capabilities\":{\"tools\":{\"listChanged\":false},"
                   "\"resources\":{\"subscribe\":false,\"listChanged\":false}},"
                   "\"serverInfo\":{\"name\":\"MasterAI\",\"version\":\"0.1.0\","
                   "\"description\":\"Native C++17 local programming AI\"},"
                   "\"instructions\":\"Only token-scoped, project-bound "
                   "capabilities are exposed.\"}}";
        }
        if (method == "ping") {
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":{}}";
        }
        if (method == "tools/list") {
            if (!authorized(identity, "mcp.connect")) {
                return rpc_error(id, -32600, "Client is not authorized");
            }
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                   ",\"result\":" + tools_catalogue() + "}";
        }
        if (method == "resources/list") {
            if (!authorized(identity, "mcp.connect")) {
                return rpc_error(id, -32600, "Client is not authorized");
            }
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                   ",\"result\":" + list_resources(projects_, identity) + "}";
        }
        if (method == "resources/read") {
            const auto& parameters = root.required("params");
            if (!has_only_fields(parameters.as_object(), {"uri"})) {
                return rpc_error(id, -32602, "Invalid resource parameters");
            }
            const std::string prefix{"masterai://project/"};
            const auto& uri = parameters.required("uri").as_string();
            if (uri.compare(0U, prefix.size(), prefix) != 0 ||
                uri.size() == prefix.size()) {
                return rpc_error(id, -32602, "Unknown resource");
            }
            const auto project_id = uri.substr(prefix.size());
            const auto project = projects_.find(project_id);
            if (!project ||
                !authorized(identity, "projects.read", project_id)) {
                return rpc_error(id, -32600, "Resource access denied");
            }
            const std::string metadata =
                "{\"id\":" + json_string(project->id) +
                ",\"displayName\":" + json_string(project->display_name) + "}";
            return "{\"jsonrpc\":\"2.0\",\"id\":" + id +
                   ",\"result\":{\"contents\":[{\"uri\":" + json_string(uri) +
                   ",\"mimeType\":\"application/json\",\"text\":" +
                   json_string(metadata) + "}]}}";
        }
        if (method != "tools/call") {
            return rpc_error(id, -32601, "Method not found");
        }

        const auto& parameters = root.required("params");
        if (!has_only_fields(parameters.as_object(), {"name", "arguments"})) {
            return rpc_error(id, -32602, "Invalid tool call parameters");
        }
        const auto& name = parameters.required("name").as_string();
        const auto& arguments = parameters.required("arguments");
        const auto& argument_object = arguments.as_object();

        if (name == "masterai.projects.list") {
            if (!argument_object.empty()) {
                return rpc_error(id, -32602, "Tool accepts no arguments");
            }
            if (!authorized(identity, "projects.read")) {
                return tool_result(id, "Project access denied.",
                                   "{\"error\":\"permission_denied\"}", true);
            }
            const auto value = list_projects(projects_, identity);
            return tool_result(id, value, value);
        }
        if (name == "masterai.models.list") {
            if (!argument_object.empty()) {
                return rpc_error(id, -32602, "Tool accepts no arguments");
            }
            if (!authorized(identity, "models.read")) {
                return tool_result(id, "Model access denied.",
                                   "{\"error\":\"permission_denied\"}", true);
            }
            const auto value = list_models(models_root_, memory_reserve_mib_);
            return tool_result(id, value, value);
        }
        if (const auto response = handle_project_tool_call(
                id, name, arguments, argument_object, identity, projects_,
                allowed_commands_, cancellation)) {
            return *response;
        }
        return rpc_error(id, -32602, "Unknown tool");
    } catch (const std::exception&) {
        return rpc_error(id, -32602, "Invalid params");
    }
}

// Processes one JSON-RPC message per input line and flushes every response so
// interactive IDE clients do not wait on buffered output. Oversized or invalid
// input is converted into protocol errors, while diagnostics stay on stderr.
int run_mcp_stdio(McpInboundServer& server, const McpIdentity& identity,
                  std::istream& input, std::ostream& output,
                  std::ostream& error, std::atomic_bool& cancellation) {
    std::string line;
    while (!cancellation.load() && std::getline(input, line)) {
        const auto response = server.handle(line, identity, cancellation);
        if (!response.empty()) {
            output << response << '\n';
            output.flush();
            if (!output) {
                error << "MasterAI MCP stdout write failed.\n";
                return 1;
            }
        }
    }
    if (input.bad()) {
        error << "MasterAI MCP stdin read failed.\n";
        return 1;
    }
    return 0;
}

}  // namespace masterai
