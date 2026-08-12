// Phase 75: remote/fleet Hardware and Compute telemetry. The listener half
// (run_telemetry_agent) is what ComputeNode's class comment called "an
// agent process on the node this codebase does not build" -- unlike
// HttpServer, which hard-enforces loopback-only binding (see server.cpp's
// State constructor), this binds whatever host an operator points it at,
// since it has to be reachable from the main MasterAI server on a
// different machine. The client half (fetch_remote_telemetry) is the other
// end of the same small, deliberately single-endpoint protocol, called by
// the ComputeNode `.../telemetry` route in server.cpp.
#include "masterai.hpp"

#include <array>
#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

void close_socket(const NativeSocket socket) noexcept {
    if (socket == invalid_socket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

bool send_all(const NativeSocket socket, const std::string& value) {
    std::size_t sent = 0U;
    while (sent < value.size()) {
        const auto chunk = send(socket, value.data() + sent,
                                static_cast<int>(value.size() - sent), 0);
        if (chunk <= 0) return false;
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

// Reads until the peer closes the connection (both sides always send
// `Connection: close`, so end-of-stream is the real end of message -- no
// chunked-transfer support needed for this deliberately tiny protocol).
std::string recv_until_closed(const NativeSocket socket,
                              const std::chrono::milliseconds timeout) {
    std::string raw;
    std::array<char, 8192> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket, &readable);
        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 100000;
#if defined(_WIN32)
        const int selected = select(0, &readable, nullptr, nullptr, &wait);
#else
        const int selected = select(socket + 1, &readable, nullptr, nullptr, &wait);
#endif
        if (selected < 0) break;
        if (selected == 0) {
            if (std::chrono::steady_clock::now() > deadline) {
                throw std::runtime_error("telemetry agent request timed out");
            }
            continue;
        }
        const auto received =
            recv(socket, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) break;
        raw.append(buffer.data(), static_cast<std::size_t>(received));
    }
    return raw;
}

struct ParsedHttpMessage {
    int status{0};
    std::string method;
    std::string target;
    std::string header_block;
    std::string body;
};

// Splits raw HTTP text into headers + body. For a request, status stays 0
// and method/target are filled from the request line; for a response,
// method/target stay empty and status is filled from the status line.
ParsedHttpMessage split_http_message(const std::string& raw, const bool is_request) {
    ParsedHttpMessage message;
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("telemetry agent message had no header terminator");
    }
    message.header_block = raw.substr(0U, header_end);
    message.body = raw.substr(header_end + 4U);
    const auto first_line_end = message.header_block.find("\r\n");
    const std::string first_line =
        message.header_block.substr(0U, first_line_end);
    std::istringstream line(first_line);
    if (is_request) {
        line >> message.method >> message.target;
    } else {
        std::string version;
        line >> version >> message.status;
    }
    return message;
}

std::string find_header(const std::string& header_block, const std::string& name) {
    std::istringstream stream(header_block);
    std::string line;
    std::getline(stream, line);  // discard the request/status line
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string key = line.substr(0U, colon);
        for (char& character : key) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        std::string lower_name = name;
        for (char& character : lower_name) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        if (key == lower_name) {
            std::size_t start = colon + 1U;
            while (start < line.size() && line[start] == ' ') ++start;
            return line.substr(start);
        }
    }
    return {};
}

// Splits `host:port` or `http://host:port` into its parts. Deliberately
// does not support TLS/paths/query strings -- this is a small
// operator-configured fleet protocol, not a general HTTP client.
void parse_agent_url(const std::string& agent_url, std::string& host,
                    std::uint16_t& port) {
    std::string value = agent_url;
    const auto scheme = value.find("://");
    if (scheme != std::string::npos) value = value.substr(scheme + 3U);
    const auto slash = value.find('/');
    if (slash != std::string::npos) value = value.substr(0U, slash);
    const auto colon = value.rfind(':');
    if (colon == std::string::npos) {
        throw std::runtime_error(
            "agent URL must be host:port (got \"" + agent_url + "\")");
    }
    host = value.substr(0U, colon);
    const auto port_text = value.substr(colon + 1U);
    int parsed = 0;
    try {
        parsed = std::stoi(port_text);
    } catch (const std::exception&) {
        parsed = -1;
    }
    if (parsed <= 0 || parsed > 65535) {
        throw std::runtime_error("agent URL has an invalid port: " + port_text);
    }
    if (host.empty()) host = "127.0.0.1";
    port = static_cast<std::uint16_t>(parsed);
}

}  // namespace

std::string fetch_remote_telemetry(const std::string& agent_url,
                                   const std::string& shared_secret,
                                   const std::uint32_t timeout_seconds) {
    std::string host;
    std::uint16_t port = 0U;
    parse_agent_url(agent_url, host, port);
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("telemetry agent socket initialization failed");
    }
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    const auto resolve_status =
        getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &resolved);
    if (resolve_status != 0 || resolved == nullptr) {
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("telemetry agent host could not be resolved: " + host);
    }
    NativeSocket socket_value = invalid_socket;
    for (auto* candidate = resolved; candidate != nullptr;
        candidate = candidate->ai_next) {
        socket_value = socket(candidate->ai_family, candidate->ai_socktype,
                              candidate->ai_protocol);
        if (socket_value == invalid_socket) continue;
        if (connect(socket_value, candidate->ai_addr,
                   static_cast<int>(candidate->ai_addrlen)) == 0) {
            break;
        }
        close_socket(socket_value);
        socket_value = invalid_socket;
    }
    freeaddrinfo(resolved);
    if (socket_value == invalid_socket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("telemetry agent connection failed: " + agent_url);
    }
    const std::string request =
        "GET /telemetry HTTP/1.1\r\nHost: " + host +
        "\r\nAuthorization: Bearer " + shared_secret +
        "\r\nConnection: close\r\n\r\n";
    if (!send_all(socket_value, request)) {
        close_socket(socket_value);
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("telemetry agent request failed: " + agent_url);
    }
    std::string raw;
    try {
        raw = recv_until_closed(socket_value,
                                std::chrono::seconds(timeout_seconds));
    } catch (...) {
        close_socket(socket_value);
#if defined(_WIN32)
        WSACleanup();
#endif
        throw;
    }
    close_socket(socket_value);
#if defined(_WIN32)
    WSACleanup();
#endif
    if (raw.find("\r\n\r\n") == std::string::npos) {
        throw std::runtime_error("telemetry agent sent an empty/invalid response");
    }
    const auto message = split_http_message(raw, false);
    if (message.status != 200) {
        throw std::runtime_error("telemetry agent returned HTTP " +
                                 std::to_string(message.status) + ": " +
                                 message.body);
    }
    return message.body;
}

void run_telemetry_agent(const std::string& host, const std::uint16_t port,
                         const std::string& shared_secret,
                         const std::filesystem::path& storage_root,
                         std::atomic_bool& stop_requested) {
    if (shared_secret.empty()) {
        throw std::runtime_error("telemetry agent requires a non-empty shared secret");
    }
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("telemetry agent socket initialization failed");
    }
#endif
    const NativeSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalid_socket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("telemetry agent listener socket creation failed");
    }
    const int reuse = 1;
    setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
              reinterpret_cast<const char*>(&reuse), sizeof(reuse));
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr =
        host.empty() || host == "0.0.0.0" ? INADDR_ANY : inet_addr(host.c_str());
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(listener, 16) != 0) {
        close_socket(listener);
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("telemetry agent could not bind/listen on " +
                                 host + ":" + std::to_string(port));
    }
    log(LogLevel::info, "telemetry_agent.listening",
       host + ":" + std::to_string(port));
    const std::string expected_authorization = "Bearer " + shared_secret;
    while (!stop_requested.load()) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(listener, &readable);
        timeval wait{};
        wait.tv_sec = 0;
        wait.tv_usec = 200000;
#if defined(_WIN32)
        const int selected = select(0, &readable, nullptr, nullptr, &wait);
#else
        const int selected = select(listener + 1, &readable, nullptr, nullptr, &wait);
#endif
        if (selected <= 0) continue;
        const NativeSocket connection = accept(listener, nullptr, nullptr);
        if (connection == invalid_socket) continue;
        try {
            const auto raw = recv_until_closed(connection, std::chrono::seconds(10));
            if (raw.find("\r\n\r\n") == std::string::npos) {
                close_socket(connection);
                continue;
            }
            const auto message = split_http_message(raw, true);
            const auto authorization = find_header(message.header_block, "Authorization");
            std::string response_body;
            std::string status_line;
            if (!constant_time_equal(authorization, expected_authorization)) {
                status_line = "HTTP/1.1 401 Unauthorized";
                response_body = "{\"error\":\"unauthorized\"}";
            } else if (message.method != "GET" || message.target != "/telemetry") {
                status_line = "HTTP/1.1 404 Not Found";
                response_body = "{\"error\":\"not_found\"}";
            } else {
                status_line = "HTTP/1.1 200 OK";
                response_body =
                    hardware_info_json(probe_hardware(storage_root));
            }
            const std::string response = status_line +
                "\r\nContent-Type: application/json\r\nContent-Length: " +
                std::to_string(response_body.size()) +
                "\r\nConnection: close\r\n\r\n" + response_body;
            send_all(connection, response);
        } catch (const std::exception& error) {
            log(LogLevel::warning, "telemetry_agent.request_failed", error.what());
        }
        close_socket(connection);
    }
    close_socket(listener);
#if defined(_WIN32)
    WSACleanup();
#endif
}

}  // namespace masterai
