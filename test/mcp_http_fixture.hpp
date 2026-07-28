// MasterAI loopback Streamable HTTP MCP conformance fixture.
//
// This test-only unit owns native socket lifecycle and a deterministic
// three-request MCP session so the main validation suite stays phase-focused.
#pragma once

#include <array>
#include <atomic>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace masterai_test {

#if defined(_WIN32)
using TestSocket = SOCKET;
constexpr TestSocket invalid_test_socket = INVALID_SOCKET;
#else
using TestSocket = int;
constexpr TestSocket invalid_test_socket = -1;
#endif

// Closes a fixture listener/client with the matching native socket API.
inline void close_test_socket(const TestSocket socket) noexcept {
    if (socket == invalid_test_socket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

class McpHttpFixture final {
public:
    // Opens an ephemeral numeric-loopback listener and starts the fixture.
    McpHttpFixture() {
#if defined(_WIN32)
        WSADATA data{};
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
            throw std::runtime_error("MCP HTTP fixture socket startup failed");
        }
#endif
        listener_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener_ == invalid_test_socket) {
            throw std::runtime_error("MCP HTTP fixture socket creation failed");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = 0;
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address),
                 sizeof(address)) != 0 ||
            listen(listener_, 4) != 0) {
            close_test_socket(listener_);
            listener_ = invalid_test_socket;
            throw std::runtime_error("MCP HTTP fixture bind failed");
        }
#if defined(_WIN32)
        int length = sizeof(address);
#else
        socklen_t length = sizeof(address);
#endif
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address),
                        &length) != 0) {
            throw std::runtime_error("MCP HTTP fixture port lookup failed");
        }
        port_ = ntohs(address.sin_port);
        worker_ = std::thread([this]() { serve(); });
    }

    // Joins the worker and releases native socket state.
    ~McpHttpFixture() {
        if (worker_.joinable()) worker_.join();
        close_test_socket(listener_);
#if defined(_WIN32)
        WSACleanup();
#endif
    }

    McpHttpFixture(const McpHttpFixture&) = delete;
    McpHttpFixture& operator=(const McpHttpFixture&) = delete;

    std::uint16_t port() const noexcept { return port_; }
    bool valid() const noexcept { return valid_.load(); }

private:
    // Accepts initialize, initialized, and tools/call connections and checks
    // the pinned revision plus required Streamable HTTP media headers.
    void serve() noexcept {
        for (int request_index = 0; request_index < 3; ++request_index) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener_, &readable);
            timeval timeout{};
            timeout.tv_sec = 5;
            const int selected =
#if defined(_WIN32)
                select(0, &readable, nullptr, nullptr, &timeout);
#else
                select(listener_ + 1, &readable, nullptr, nullptr, &timeout);
#endif
            if (selected <= 0) {
                valid_.store(false);
                return;
            }
            sockaddr_in peer{};
#if defined(_WIN32)
            int peer_length = sizeof(peer);
#else
            socklen_t peer_length = sizeof(peer);
#endif
            const auto client = accept(
                listener_, reinterpret_cast<sockaddr*>(&peer), &peer_length);
            if (client == invalid_test_socket) {
                valid_.store(false);
                return;
            }
            std::string request;
            std::array<char, 4096U> buffer{};
            std::size_t content_length = 0U;
            std::size_t header_end = std::string::npos;
            while (request.size() < 128U * 1024U) {
                const auto received = recv(
                    client, buffer.data(), static_cast<int>(buffer.size()), 0);
                if (received <= 0) break;
                request.append(buffer.data(),
                               static_cast<std::size_t>(received));
                header_end = request.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    const auto marker =
                        request.find("\r\nContent-Length: ");
                    if (marker == std::string::npos) break;
                    const auto start =
                        marker + std::strlen("\r\nContent-Length: ");
                    const auto end = request.find("\r\n", start);
                    content_length = static_cast<std::size_t>(
                        std::stoull(request.substr(start, end - start)));
                    if (request.size() >=
                        header_end + 4U + content_length) {
                        break;
                    }
                }
            }
            const bool headers_valid =
                request.find("POST /mcp HTTP/1.1\r\n") == 0U &&
                request.find("Content-Type: application/json\r\n") !=
                    std::string::npos &&
                request.find(
                    "Accept: application/json, text/event-stream\r\n") !=
                    std::string::npos &&
                request.find(
                    "MCP-Protocol-Version: 2025-11-25\r\n") !=
                    std::string::npos;
            if (!headers_valid || header_end == std::string::npos) {
                valid_.store(false);
            }
            const auto body =
                header_end == std::string::npos
                    ? std::string{}
                    : request.substr(header_end + 4U, content_length);
            std::string response_body;
            int status = 200;
            const char* reason = "OK";
            if (request_index == 0) {
                if (body.find("\"method\":\"initialize\"") ==
                    std::string::npos) {
                    valid_.store(false);
                }
                response_body =
                    "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":{"
                    "\"protocolVersion\":\"2025-11-25\","
                    "\"capabilities\":{\"tools\":{}},\"serverInfo\":{"
                    "\"name\":\"HTTP fixture\",\"version\":\"1\"}}}";
            } else if (request_index == 1) {
                if (body.find("\"method\":\"notifications/initialized\"") ==
                    std::string::npos) {
                    valid_.store(false);
                }
                status = 202;
                reason = "Accepted";
            } else {
                if (body.find("\"method\":\"tools/call\"") ==
                    std::string::npos) {
                    valid_.store(false);
                }
                response_body =
                    "{\"jsonrpc\":\"2.0\",\"id\":2,\"result\":{\"content\":[{"
                    "\"type\":\"text\",\"text\":\"loopback echo\"}]}}";
            }
            const std::string response =
                "HTTP/1.1 " + std::to_string(status) + " " + reason +
                "\r\nContent-Type: application/json\r\nContent-Length: " +
                std::to_string(response_body.size()) +
                "\r\nConnection: close\r\n\r\n" + response_body;
            std::size_t sent = 0U;
            while (sent < response.size()) {
                const auto amount =
                    send(client, response.data() + sent,
                         static_cast<int>(response.size() - sent), 0);
                if (amount <= 0) {
                    valid_.store(false);
                    break;
                }
                sent += static_cast<std::size_t>(amount);
            }
            close_test_socket(client);
        }
    }

    TestSocket listener_{invalid_test_socket};
    std::uint16_t port_{0};
    std::atomic_bool valid_{true};
    std::thread worker_;
};

}  // namespace masterai_test
