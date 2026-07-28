// MasterAI private outbound MCP validation and transport operations.
//
// This header shares one implementation between the durable registry/service
// unit and the platform transport unit. It is not part of the public API.
#pragma once

#include "masterai.hpp"

#include <atomic>
#include <cstddef>
#include <set>
#include <string>
#include <vector>

namespace masterai::mcp_outbound_internal {

extern const std::size_t maximum_registry_servers;

std::string pack(const std::vector<std::string>& fields);
std::vector<std::string> unpack(const std::string& value);
std::string pack_set(const std::set<std::string>& values);
std::set<std::string> unpack_set(const std::string& value);
std::string transport_name(McpTransport transport);
McpTransport parse_transport(const std::string& value);
McpOutboundServer validate_server(McpOutboundServer server,
                                  bool pin_executable);
std::string initialize_request();
std::string tool_request(const McpOutboundCall& call);
McpOutboundResult invoke_stdio(const McpOutboundServer& server,
                              const std::string& payload,
                              std::atomic_bool& cancellation);
McpOutboundResult invoke_http(const McpOutboundServer& server,
                             SecretStore& secrets,
                             const McpOutboundCall& call,
                             std::atomic_bool& cancellation);

}  // namespace masterai::mcp_outbound_internal
