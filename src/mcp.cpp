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
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

constexpr std::size_t maximum_mcp_message_bytes = 1024U * 1024U;
constexpr std::size_t maximum_mcp_file_bytes = 1024U * 1024U;
constexpr std::size_t maximum_search_files = 512U;
constexpr std::size_t maximum_search_results = 50U;

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

// Restricts search to source-like text formats already approved for MasterAI
// attachments. Binary and ambiguous files are not read through MCP.
bool searchable_extension(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    static const std::set<std::string> approved{
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".asm", ".s", ".md", ".txt", ".json", ".jsonl", ".xml",
        ".yaml", ".yml", ".toml", ".ini", ".cmake", ".py", ".rs",
        ".go", ".java", ".cs", ".pas", ".sql", ".sh", ".ps1"};
    return approved.find(extension) != approved.end();
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
        "{\"name\":\"masterai.models.list\",\"title\":\"List local models\","
        "\"description\":\"List verified local model inventory and readiness.\","
        "\"inputSchema\":{\"type\":\"object\",\"additionalProperties\":false},"
        "\"annotations\":{\"readOnlyHint\":true,\"destructiveHint\":false}}]}";
}

// Materializes the token-visible project subset once for tools and resources.
std::vector<ProjectRecord> authorized_projects(
    ProjectCatalog& projects, const McpIdentity& identity) {
    std::vector<ProjectRecord> result;
    for (const auto& project : projects.list()) {
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

// Appends bounded literal matches from one approved source file.
void append_file_matches(const std::filesystem::path& path,
                         const std::filesystem::path& project_root,
                         const std::string& query, std::string& structured,
                         std::size_t& matches) {
    std::ifstream input(path, std::ios::binary);
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(input, line) &&
           matches < maximum_search_results) {
        ++line_number;
        if (line.find(query) == std::string::npos ||
            !valid_utf8_text(line)) {
            continue;
        }
        std::error_code error;
        const auto relative =
            std::filesystem::relative(path, project_root, error);
        if (error) continue;
        if (matches != 0U) structured += ",";
        structured += "{\"path\":" +
                      json_string(relative.generic_string()) +
                      ",\"line\":" + std::to_string(line_number) +
                      ",\"text\":" + json_string(line) + "}";
        ++matches;
    }
}

// Reads one search candidate only when its type and size are approved.
bool search_candidate(const std::filesystem::directory_entry& entry,
                      const ProjectRecord& project,
                      const std::string& query, std::string& structured,
                      std::size_t& matches, std::error_code& error) {
    if (!entry.is_regular_file(error) ||
        !searchable_extension(entry.path().extension().string())) {
        return false;
    }
    const auto size = entry.file_size(error);
    if (!error && size <= maximum_mcp_file_bytes) {
        append_file_matches(entry.path(), project.root, query, structured,
                            matches);
    }
    return true;
}

// Scans a bounded number of regular source files and returns literal,
// line-oriented matches. Symlink directories are never followed.
std::string search_project(const ProjectRecord& project,
                           const std::string& query) {
    if (query.empty() || query.size() > 256U ||
        !valid_utf8_text(query)) {
        throw std::invalid_argument("search query is outside policy");
    }
    std::string structured{"{\"matches\":["};
    std::size_t visited = 0U;
    std::size_t matches = 0U;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        project.root, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end && visited < maximum_search_files &&
           matches < maximum_search_results) {
        const auto entry = *iterator;
        if (entry.is_symlink(error)) {
            if (entry.is_directory(error)) iterator.disable_recursion_pending();
            iterator.increment(error);
            continue;
        }
        if (search_candidate(entry, project, query, structured, matches,
                             error)) {
            ++visited;
        }
        error.clear();
        iterator.increment(error);
    }
    return structured + "]}";
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

}  // namespace

const char* mcp_protocol_version() noexcept { return "2025-11-25"; }

// Captures stable service dependencies. Project records remain live through the
// referenced catalogue while the model root and memory reserve are immutable.
McpInboundServer::McpInboundServer(ProjectCatalog& projects,
                                   std::filesystem::path models_root,
                                   const std::uint64_t memory_reserve_mib)
    : projects_(projects), models_root_(std::move(models_root)),
      memory_reserve_mib_(memory_reserve_mib) {
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
        if (name == "masterai.project.read_file" ||
            name == "masterai.project.search") {
            const std::set<std::string> allowed =
                name == "masterai.project.read_file"
                    ? std::set<std::string>{"projectId", "path"}
                    : std::set<std::string>{"projectId", "query"};
            if (!has_only_fields(argument_object, allowed) ||
                argument_object.size() != 2U) {
                return rpc_error(id, -32602, "Invalid project tool arguments");
            }
            const auto& project_id =
                arguments.required("projectId").as_string();
            const auto project = projects_.find(project_id);
            if (!project ||
                !authorized(identity, "projects.read", project_id)) {
                return tool_result(id, "Project access denied.",
                                   "{\"error\":\"permission_denied\"}", true);
            }
            try {
                if (name == "masterai.project.read_file") {
                    const auto& path = arguments.required("path").as_string();
                    const auto content = read_project_text_file(
                                             *project, path,
                                             maximum_mcp_file_bytes)
                                             .content;
                    const std::string structured =
                        "{\"projectId\":" + json_string(project_id) +
                        ",\"path\":" + json_string(path) +
                        ",\"content\":" + json_string(content) + "}";
                    return tool_result(id, content, structured);
                }
                const auto value = search_project(
                    *project, arguments.required("query").as_string());
                return tool_result(id, value, value);
            } catch (const std::exception&) {
                return tool_result(id, "Project input was rejected by policy.",
                                   "{\"error\":\"invalid_project_input\"}", true);
            }
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
