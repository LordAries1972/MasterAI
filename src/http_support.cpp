// MasterAI shared private HTTP response and JSON text helpers.
//
// Keeping these operations in one source unit prevents each focused HTTP
// controller from growing its own subtly different response or escaping code.
#include "server_internal.hpp"

namespace masterai::server_internal {
namespace {

// Replaces transport-unsafe controls consistently for HTML text/attributes,
// where a raw newline or tab in a value carries no meaning worth preserving.
bool append_normalized_control(const char character, std::string& output) {
    if (character != '\r' && character != '\n' &&
        static_cast<unsigned char>(character) >= 0x20U) {
        return false;
    }
    output.push_back(' ');
    return true;
}

// Escapes a control character for safe embedding in a JSON string, using the
// short escapes JSON defines for the common ones and \u00XX for the rest.
// Newlines and tabs must survive round-trips (chat message formatting, code
// blocks) rather than being collapsed to a single space.
void append_json_control_escape(const char character, std::string& output) {
    switch (character) {
        case '\n': output += "\\n"; return;
        case '\r': output += "\\r"; return;
        case '\t': output += "\\t"; return;
        case '\b': output += "\\b"; return;
        case '\f': output += "\\f"; return;
        default: break;
    }
    static constexpr char digits[] = "0123456789abcdef";
    const auto value = static_cast<unsigned char>(character);
    output += "\\u00";
    output.push_back(digits[(value >> 4U) & 0x0fU]);
    output.push_back(digits[value & 0x0fU]);
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

// Escapes embedded JSON text, preserving newlines/tabs via proper JSON
// escapes instead of collapsing them to spaces -- required for chat message
// content to round-trip with its original line breaks and indentation.
std::string json_escape(const std::string& value) {
    std::string output;
    for (const char character : value) {
        if (static_cast<unsigned char>(character) < 0x20U) {
            append_json_control_escape(character, output);
            continue;
        }
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
