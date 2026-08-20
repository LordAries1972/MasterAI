// MasterAI shared native test fixtures and assertions.
//
// The validation source units use these helpers so temporary-workspace,
// executable-fixture, file-writing, and assertion behavior are not duplicated.
#pragma once

#include <array>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace masterai_test {

// Fails the active test with its operation-specific evidence.
inline void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory final {
public:
    // Creates an isolated per-test directory under the operating-system temp.
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("masterai-test-" +
                 std::to_string(
                     std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count()));
        std::filesystem::create_directories(path_);
    }

    // Removes the isolated tree without masking an active test failure.
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

// Writes a deterministic binary-safe text fixture and verifies completion.
inline void write_text(const std::filesystem::path& path,
                       const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("test fixture write failed");
    }
}

// Reads back a fixture written by write_text(), the counterpart callers use
// to assert on a tool call's actual on-disk effect rather than only its
// reported result text.
inline std::string read_text(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)),
                     std::istreambuf_iterator<char>());
    if (!input && !input.eof()) {
        throw std::runtime_error("test fixture read failed");
    }
    return text;
}

// Resolves the current test binary used as a harmless executable fixture.
inline std::filesystem::path test_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_tests.exe";
#else
    return std::filesystem::current_path() / "masterai_tests";
#endif
}

// Resolves the platform-specific fake llama service used by runner and
// download tests; callers remain responsible for checking fixture presence.
inline std::filesystem::path fake_llama_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_fake_llama.exe";
#else
    return std::filesystem::current_path() / "masterai_fake_llama";
#endif
}

// Resolves the minimal curl stand-in used to exercise DownloadManager::run()
// without a real network transfer; see test/fake_curl.cpp.
inline std::filesystem::path fake_curl_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_fake_curl.exe";
#else
    return std::filesystem::current_path() / "masterai_fake_curl";
#endif
}

// Resolves the minimal DuckDB CLI stand-in used to exercise
// parquet_bytes_to_json() and KnowledgeIndexStore's Parquet ingestion path
// without a real DuckDB binary or a real Parquet file; see
// test/fake_duckdb.cpp.
inline std::filesystem::path fake_duckdb_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_fake_duckdb.exe";
#else
    return std::filesystem::current_path() / "masterai_fake_duckdb";
#endif
}

// Phase 86: a minimal loopback HTTP client, mirroring the raw-socket style
// test/fake_llama_server.cpp already uses on the *server* side, so
// server.cpp's real request dispatcher (State::handle(), a private nested
// class with no test-only seam) can be exercised end-to-end -- connect,
// authenticate, and route -- exactly the way a real browser or the
// Agent-Coder VS Code extension would, rather than only unit-testing the
// pieces reachable without a live socket. Sends "Connection: close" so the
// server (see server.cpp's keep_alive_eligible logic) always tears the
// connection down after exactly one response, which is what lets this
// helper simply read until the peer closes rather than parsing
// Content-Length/chunked framing itself.
struct HttpResponse {
    int status{0};
    std::string body;
};

// Issues one HTTP/1.1 request to 127.0.0.1:port and returns its status code
// and body. `extra_headers` (e.g. "Authorization: Bearer ...") each get
// "\r\n" appended automatically; pass {} for none. Throws if the connection
// itself could not be established -- a real request/response exchange (even
// an error status) is a success from this helper's own point of view.
inline HttpResponse http_request(const unsigned short port,
                                 const std::string& method,
                                 const std::string& path,
                                 const std::vector<std::string>& extra_headers = {},
                                 const std::string& body = {}) {
#if defined(_WIN32)
    using Socket = SOCKET;
    constexpr Socket invalid = INVALID_SOCKET;
#else
    using Socket = int;
    constexpr Socket invalid = -1;
#endif
    const Socket sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock == invalid) {
        throw std::runtime_error("http_request: socket() failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
    if (connect(sock, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
        0) {
#if defined(_WIN32)
        closesocket(sock);
#else
        close(sock);
#endif
        throw std::runtime_error("http_request: connect() failed");
    }
    std::string request = method + " " + path + " HTTP/1.1\r\n";
    request += "Host: 127.0.0.1:" + std::to_string(port) + "\r\n";
    request += "Connection: close\r\n";
    if (!body.empty()) {
        request += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        request += "Content-Type: application/json\r\n";
    }
    for (const auto& header : extra_headers) request += header + "\r\n";
    request += "\r\n";
    request += body;
    std::size_t sent = 0U;
    while (sent < request.size()) {
        const auto count =
            send(sock, request.data() + sent,
                static_cast<int>(request.size() - sent), 0);
        if (count <= 0) break;
        sent += static_cast<std::size_t>(count);
    }
    std::string raw;
    std::array<char, 4096> buffer{};
    while (true) {
        const auto count =
            recv(sock, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (count <= 0) break;
        raw.append(buffer.data(), static_cast<std::size_t>(count));
    }
#if defined(_WIN32)
    closesocket(sock);
#else
    close(sock);
#endif
    HttpResponse result;
    const auto status_start = raw.find(' ');
    if (status_start != std::string::npos) {
        result.status = std::atoi(raw.c_str() + status_start + 1U);
    }
    const auto separator = raw.find("\r\n\r\n");
    result.body =
        separator == std::string::npos ? std::string{} : raw.substr(separator + 4U);
    return result;
}

}  // namespace masterai_test
