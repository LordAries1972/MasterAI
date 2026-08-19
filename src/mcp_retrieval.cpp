// Phase 24: mcp_resource retrieval adapter. Consumes the existing outbound
// MCP client plumbing (McpOutboundRegistry/McpOutboundGateway, mcp_outbound.cpp)
// rather than inventing a second MCP client -- this adapter performs no
// authorization decision of its own; McpOutboundGateway::invoke() already
// enforces the registry's allow-list/project-boundary/approval rules on
// every call, exactly as the class-level comment on McpOutboundGateway
// requires.
#include "json.hpp"
#include "masterai.hpp"

#include <algorithm>
#include <cctype>

namespace masterai {
namespace {

// retrieval.cpp's to_lower_copy() has internal linkage (anonymous
// namespace), so this adapter carries its own copy rather than reaching
// across translation units for it.
std::string ascii_lower(const std::string& text) {
    std::string result = text;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return result;
}

// Cheap lexical relevance check: a resource is considered worth reading only
// when its uri or name contains one of the query's identifier-shaped tokens.
// Keeps this adapter from blindly reading every resource on every server for
// every query.
bool resource_matches_query(const std::string& uri, const std::string& name,
                            const std::vector<std::string>& query_tokens) {
    if (query_tokens.empty()) return false;
    const auto haystack = ascii_lower(uri + " " + name);
    return std::any_of(query_tokens.begin(), query_tokens.end(),
                       [&](const std::string& token) {
                           return haystack.find(ascii_lower(token)) !=
                                  std::string::npos;
                       });
}

// optional()/as_string() reads a string field, tolerating a missing key or a
// parse tree shaped differently than expected -- this adapter degrades to
// "no evidence from this resource" rather than throwing on a malformed or
// unexpected upstream response.
std::string optional_field(const JsonValue& value, const std::string& key,
                           const std::string& fallback) {
    const auto* found = value.optional(key);
    if (found == nullptr || found->type() != JsonValue::Type::string) {
        return fallback;
    }
    return found->as_string();
}

}  // namespace

// Lists resources on every outbound server enabled and authorized for
// `project_id`, reads the ones whose uri/name lexically match `query_tokens`,
// and returns each as one synthetic chunk (relative_path =
// "mcp://<server_id>/<resource_uri>"). Best-effort: a server that fails to
// list or read (offline, misconfigured, denied) is skipped, never surfaced
// as a retrieval error -- the same "declared but no evidence" treatment
// every other strategy gives an empty result.
std::vector<IndexChunk> mcp_resource_search(
    McpOutboundRegistry& registry, McpOutboundGateway& gateway,
    const std::string& requester_id, const std::set<std::string>& requester_scopes,
    const std::string& project_id, const std::vector<std::string>& query_tokens,
    const std::size_t maximum_results, std::atomic_bool& cancellation) {
    std::vector<IndexChunk> results;
    if (maximum_results == 0U) return results;
    for (const auto& server : registry.list()) {
        if (results.size() >= maximum_results) break;
        if (!server.enabled) continue;
        if (!server.allowed_project_ids.empty() &&
            server.allowed_project_ids.find(project_id) ==
                server.allowed_project_ids.end()) {
            continue;
        }

        McpOutboundCall list_call;
        list_call.server_id = server.id;
        list_call.tool = "resources/list";
        list_call.actor_id = requester_id;
        list_call.actor_scopes = requester_scopes;
        list_call.actor_project_ids = {project_id};
        list_call.project_id = project_id;
        list_call.user_approved = true;
        const auto list_result = gateway.invoke(list_call, cancellation);
        if (!list_result.succeeded) continue;

        std::vector<JsonValue> resources;
        try {
            const auto parsed = parse_json(list_result.response_json);
            const auto* found = parsed.optional("resources");
            if (found == nullptr || found->type() != JsonValue::Type::array) {
                continue;
            }
            resources = found->as_array();
        } catch (const std::exception&) {
            continue;
        }

        for (const auto& resource : resources) {
            if (results.size() >= maximum_results) break;
            if (resource.type() != JsonValue::Type::object) continue;
            const auto uri = optional_field(resource, "uri", "");
            const auto name = optional_field(resource, "name", uri);
            if (uri.empty() ||
                !resource_matches_query(uri, name, query_tokens)) {
                continue;
            }

            McpOutboundCall read_call;
            read_call.server_id = server.id;
            read_call.tool = "resources/read";
            read_call.arguments_json = "{\"uri\":" + json_string(uri) + "}";
            read_call.actor_id = requester_id;
            read_call.actor_scopes = requester_scopes;
            read_call.actor_project_ids = {project_id};
            read_call.project_id = project_id;
            read_call.user_approved = true;
            const auto read_result = gateway.invoke(read_call, cancellation);
            if (!read_result.succeeded) continue;

            std::string text = read_result.response_json;
            try {
                const auto read_parsed = parse_json(read_result.response_json);
                const auto* contents = read_parsed.optional("contents");
                if (contents != nullptr &&
                    contents->type() == JsonValue::Type::array &&
                    !contents->as_array().empty()) {
                    text = optional_field(contents->as_array().front(), "text",
                                          read_result.response_json);
                }
            } catch (const std::exception&) {
                // Not JSON (or not the expected shape) -- fall back to the
                // raw response text captured above rather than dropping it.
            }
            if (text.empty()) continue;

            IndexChunk chunk;
            chunk.relative_path = "mcp://" + server.id + "/" + uri;
            chunk.language = "mcp-resource";
            chunk.offset = 0U;
            chunk.text = text;
            chunk.digest = sha256_hex(text);
            chunk.id = sha256_hex("mcp-resource:" + chunk.relative_path + ":" +
                                  chunk.digest);
            results.push_back(std::move(chunk));
        }
    }
    return results;
}

}  // namespace masterai
