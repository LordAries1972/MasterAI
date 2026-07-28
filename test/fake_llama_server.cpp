#include <array>
#include <cstdint>
#include <cstdlib>
#include <string>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {
#if defined(_WIN32)
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
#endif

void close_socket(const Socket value) {
#if defined(_WIN32)
    closesocket(value);
#else
    close(value);
#endif
}

bool send_all(const Socket socket, const std::string& value) {
    std::size_t sent = 0U;
    while (sent < value.size()) {
        const auto count =
            send(socket, value.data() + sent,
                 static_cast<int>(value.size() - sent), 0);
        if (count <= 0) return false;
        sent += static_cast<std::size_t>(count);
    }
    return true;
}
}  // namespace

int main(int argc, char* argv[]) {
    unsigned int port = 0U;
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::string(argv[index]) == "--port") {
            port = static_cast<unsigned int>(std::stoul(argv[index + 1]));
        }
    }
    if (port < 1024U || port > 65535U) return 2;
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 3;
#endif
    const Socket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == invalid_socket) return 4;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) !=
            0 ||
        listen(listener, 4) != 0) {
        close_socket(listener);
        return 5;
    }
    while (true) {
        const Socket client = accept(listener, nullptr, nullptr);
        if (client == invalid_socket) continue;
        std::string request;
        std::array<char, 4096> buffer{};
        while (request.find("\r\n\r\n") == std::string::npos) {
            const auto count =
                recv(client, buffer.data(), static_cast<int>(buffer.size()), 0);
            if (count <= 0) break;
            request.append(buffer.data(), static_cast<std::size_t>(count));
        }
        std::string body;
        if (request.rfind("GET /health ", 0U) == 0U) {
            body = "{\"status\":\"ok\"}";
        } else if (request.rfind("POST /tokenize ", 0U) == 0U) {
            body = "{\"tokens\":[1,2,3]}";
        } else if (request.rfind("POST /completion ", 0U) == 0U) {
            body =
                "{\"content\":\"return \",\"tokens_evaluated\":3}\n"
                "{\"content\":\"value;\",\"tokens_predicted\":2}\n";
        } else {
            body = "{\"error\":\"not_found\"}";
        }
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\n"
            "Content-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" +
            body;
        send_all(client, response);
        close_socket(client);
    }
}
