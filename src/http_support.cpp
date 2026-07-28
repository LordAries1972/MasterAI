// MasterAI shared private HTTP response and JSON text helpers.
//
// Keeping these operations in one source unit prevents each focused HTTP
// controller from growing its own subtly different response or escaping code.
#include "server_internal.hpp"

namespace masterai::server_internal {
namespace {

// Replaces transport-unsafe controls consistently for JSON and HTML text.
bool append_normalized_control(const char character, std::string& output) {
    if (character != '\r' && character != '\n' &&
        static_cast<unsigned char>(character) >= 0x20U) {
        return false;
    }
    output.push_back(' ');
    return true;
}

}  // namespace

// Builds security-hardened JSON responses with optional protocol headers.
std::string response(const int status, const char* reason,
                     const std::string& body,
                     const std::vector<std::string>& headers) {
    std::string extra;
    for (const auto& header : headers) {
        extra += header + "\r\n";
    }
    return "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n"
           "Content-Type: application/json\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\n"
           "Cache-Control: no-store\r\n"
           "X-Content-Type-Options: nosniff\r\n"
           "X-Frame-Options: DENY\r\n"
           "Content-Security-Policy: default-src 'none'; frame-ancestors 'none'\r\n"
           "Referrer-Policy: no-referrer\r\n" + extra + "\r\n" + body;
}

// Escapes embedded JSON text and normalizes prohibited control characters.
std::string json_escape(const std::string& value) {
    std::string output;
    for (const char character : value) {
        if (append_normalized_control(character, output)) continue;
        if (character == '"' || character == '\\') {
            output.push_back('\\');
        }
        output.push_back(character);
    }
    return output;
}

// Builds hardened HTML responses for every browser-facing controller.
std::string html_response(const std::string& body) {
    return "HTTP/1.1 200 OK\r\n"
           "Content-Type: text/html; charset=utf-8\r\n"
           "Content-Length: " + std::to_string(body.size()) + "\r\n"
           "Connection: close\r\nCache-Control: no-store\r\n"
           "X-Content-Type-Options: nosniff\r\nX-Frame-Options: DENY\r\n"
           "Content-Security-Policy: default-src 'none'; style-src 'unsafe-inline'; "
           "script-src 'self'; connect-src 'self'; "
           "frame-ancestors 'none'\r\nReferrer-Policy: no-referrer\r\n\r\n" +
           body;
}

// Escapes user-controlled text for HTML text and attribute contexts.
std::string html_escape(const std::string& value) {
    std::string output;
    for (const char character : value) {
        if (append_normalized_control(character, output)) continue;
        if (character == '&') output += "&amp;";
        else if (character == '<') output += "&lt;";
        else if (character == '>') output += "&gt;";
        else if (character == '"') output += "&quot;";
        else if (character == '\'') output += "&#39;";
        else output.push_back(character);
    }
    return output;
}

}  // namespace masterai::server_internal
