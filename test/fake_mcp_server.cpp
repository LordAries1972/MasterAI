// MasterAI isolated outbound-MCP process fixture.
//
// This test executable implements the smallest newline-delimited MCP server
// needed to validate the real child-process client. It verifies that MasterAI
// supplied the sandbox marker, negotiates the pinned revision, accepts an
// initialized notification, and returns a deterministic tool result.

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

// Emits one compact JSON-RPC response and flushes immediately to model an
// interactive stdio server.
void send(const std::string& response) {
    std::cout << response << '\n' << std::flush;
}

}  // namespace

// Reads one JSON-RPC message per line. This fixture intentionally recognizes
// only the methods used by the outbound conformance test.
int main() {
    const char* sandbox = std::getenv("MASTERAI_MCP_SANDBOX");
    if (sandbox == nullptr || std::string(sandbox) != "1") {
        return 3;
    }
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.find("\"method\":\"initialize\"") != std::string::npos) {
            send("{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
                 "\"protocolVersion\":\"2025-11-25\","
                 "\"capabilities\":{\"tools\":{}},\"serverInfo\":{"
                 "\"name\":\"MasterAI fake MCP\",\"version\":\"1\"}}}");
        } else if (line.find("\"method\":\"notifications/initialized\"") !=
                   std::string::npos) {
            continue;
        } else if (line.find("\"method\":\"tools/call\"") !=
                   std::string::npos) {
            send("{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"content\":[{"
                 "\"type\":\"text\",\"text\":\"sandboxed echo\"}],"
                 "\"structuredContent\":{\"sandboxed\":true}}}");
        } else {
            send("{\"jsonrpc\":\"2.0\",\"id\":0,\"error\":{\"code\":-32601,"
                 "\"message\":\"Method not found\"}}");
        }
    }
    return std::cin.bad() ? 2 : 0;
}
