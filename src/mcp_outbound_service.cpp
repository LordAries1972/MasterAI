// MasterAI durable outbound MCP registry and authorization gateway.
//
// This unit owns approved server records and the user/project/tool policy
// boundary. Platform process and HTTP mechanics remain in mcp_outbound.cpp.
#include "mcp_outbound_internal.hpp"
#include "json.hpp"

#include <stdexcept>

namespace masterai {

using namespace mcp_outbound_internal;

// Restores only valid, currently approved registry entries. Corrupt, legacy
// SSE, missing, or digest-changed records are omitted and therefore disabled.
McpOutboundRegistry::McpOutboundRegistry(RecordStore& records)
    : records_(records) {
    restore();
}

// Validates and pins a new server before persistence. Duplicate identifiers are
// rejected so authority changes require an explicit remove-and-register action.
McpOutboundServer McpOutboundRegistry::register_server(
    McpOutboundServer server) {
    if (servers_.size() >= maximum_registry_servers ||
        servers_.find(server.id) != servers_.end()) {
        throw std::invalid_argument(
            "outbound MCP registry is full or identifier is duplicate");
    }
    server = validate_server(std::move(server), true);
    servers_.emplace(server.id, server);
    persist(server);
    return server;
}

// Removes one exact registry entry and its durable authority record.
void McpOutboundRegistry::remove(const std::string& server_id) {
    if (servers_.erase(server_id) == 0U) {
        throw std::invalid_argument("outbound MCP server does not exist");
    }
    records_.erase("mcp_outbound_servers", server_id);
}

// Returns a copy so callers cannot mutate registry authority in memory.
std::optional<McpOutboundServer> McpOutboundRegistry::find(
    const std::string& server_id) const {
    const auto found = servers_.find(server_id);
    return found == servers_.end()
               ? std::nullopt
               : std::optional<McpOutboundServer>(found->second);
}

// Lists deterministic registry copies ordered by server identifier.
std::vector<McpOutboundServer> McpOutboundRegistry::list() const {
    std::vector<McpOutboundServer> result;
    result.reserve(servers_.size());
    for (const auto& server : servers_) result.push_back(server.second);
    return result;
}

// Reconstructs durable entries and rechecks every executable digest and
// transport policy before returning authority to the live registry.
void McpOutboundRegistry::restore() {
    for (const auto& item : records_.list("mcp_outbound_servers")) {
        try {
            const auto fields = unpack(item.second);
            if (fields.size() != 12U || item.first.empty()) continue;
            McpOutboundServer server;
            server.id = item.first;
            server.transport = parse_transport(fields[0]);
            server.executable = fields[1];
            server.executable_sha256 = fields[2];
            server.arguments = unpack(fields[3]);
            server.working_directory = fields[4];
            server.endpoint = fields[5];
            server.allowed_tools = unpack_set(fields[6]);
            server.allowed_project_ids = unpack_set(fields[7]);
            server.credential_secret_name = fields[8];
            server.enabled = fields[9] == "1";
            server.timeout_seconds = std::stoull(fields[10]);
            const auto limits = unpack(fields[11]);
            if (limits.size() != 2U) continue;
            server.maximum_output_bytes = std::stoull(limits[0]);
            server.memory_limit_mib = std::stoull(limits[1]);
            server = validate_server(std::move(server), false);
            servers_.emplace(server.id, std::move(server));
        } catch (const std::exception&) {
            // Invalid authority records fail closed without blocking startup.
        }
    }
}

// Writes the complete policy record after validation. Credentials are
// referenced by OS-secret name and never serialized into the registry.
void McpOutboundRegistry::persist(const McpOutboundServer& server) {
    records_.put(
        "mcp_outbound_servers", server.id,
        pack({transport_name(server.transport), server.executable.string(),
              server.executable_sha256, pack(server.arguments),
              server.working_directory.string(), server.endpoint,
              pack_set(server.allowed_tools),
              pack_set(server.allowed_project_ids),
              server.credential_secret_name, server.enabled ? "1" : "0",
              std::to_string(server.timeout_seconds),
              pack({std::to_string(server.maximum_output_bytes),
                    std::to_string(server.memory_limit_mib)})}));
}

// Captures registry, OS-secret, and audit services used by every invocation.
McpOutboundGateway::McpOutboundGateway(McpOutboundRegistry& registry,
                                       SecretStore& secrets,
                                       AuditLog& audit)
    : registry_(registry), secrets_(secrets), audit_(audit) {}

// Authorizes, audits, executes, and outcome-audits one outbound call. Approval,
// caller project binding, registry project binding, scope, executable digest,
// argument shape, and cancellation all fail closed before tool authority runs.
McpOutboundResult McpOutboundGateway::invoke(
    const McpOutboundCall& call, std::atomic_bool& cancellation) const {
    const auto server_record = registry_.find(call.server_id);
    const bool project_authorized =
        server_record &&
        ((call.project_id.empty() &&
          server_record->allowed_project_ids.empty()) ||
         (!call.project_id.empty() &&
          call.actor_project_ids.find(call.project_id) !=
              call.actor_project_ids.end() &&
          server_record->allowed_project_ids.find(call.project_id) !=
              server_record->allowed_project_ids.end()));
    const bool authorized =
        server_record && !call.actor_id.empty() && call.user_approved &&
        call.actor_scopes.find("mcp.tools.invoke") != call.actor_scopes.end() &&
        server_record->allowed_tools.find(call.tool) !=
            server_record->allowed_tools.end() &&
        project_authorized;
    if (!authorized) {
        audit_.append("mcp.outbound", call.actor_id.empty() ? "unknown"
                                                            : call.actor_id,
                      "denied", call.server_id + "/" + call.tool);
        return {false, false, {}, "MCP outbound authorization was denied"};
    }
    if (cancellation.load()) {
        audit_.append("mcp.outbound", call.actor_id, "cancelled",
                      call.server_id + "/" + call.tool);
        return {false, true, {}, "MCP outbound call was already cancelled"};
    }
    try {
        const auto arguments = parse_json(call.arguments_json);
        if (!arguments.is_object() ||
            call.arguments_json.size() > 1024U * 1024U ||
            call.arguments_json.find('\0') != std::string::npos) {
            throw std::runtime_error("arguments are outside policy");
        }
    } catch (const std::exception&) {
        audit_.append("mcp.outbound", call.actor_id, "denied",
                      call.server_id + "/" + call.tool +
                          " invalid arguments");
        return {false, false, {}, "MCP tool arguments are invalid"};
    }

    McpOutboundServer server;
    try {
        server = validate_server(*server_record, false);
    } catch (const std::exception&) {
        audit_.append("mcp.outbound", call.actor_id, "denied",
                      call.server_id + "/" + call.tool +
                          " registry revalidation failed");
        return {false, false, {},
                "MCP server failed registry revalidation"};
    }
    audit_.append("mcp.outbound", call.actor_id, "approved",
                  call.server_id + "/" + call.tool);

    McpOutboundResult result;
    if (server.transport == McpTransport::stdio_transport) {
        const std::string payload =
            initialize_request() + "\n" +
            "{\"jsonrpc\":\"2.0\",\"method\":"
            "\"notifications/initialized\",\"params\":{}}\n" +
            tool_request(call) + "\n";
        result = invoke_stdio(server, payload, cancellation);
    } else if (server.transport == McpTransport::streamable_http) {
        result = invoke_http(server, secrets_, call, cancellation);
    } else {
        result = {false, false, {},
                  "legacy SSE is disabled by release policy"};
    }
    audit_.append("mcp.outbound", call.actor_id,
                  result.cancelled
                      ? "cancelled"
                      : (result.succeeded ? "success" : "failed"),
                  call.server_id + "/" + call.tool + " " +
                      result.diagnostic);
    return result;
}

}  // namespace masterai
