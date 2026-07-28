// MasterAI Model Context Protocol outbound registry and client gateway.
//
// This unit keeps outbound authority separate from inbound identities. It
// persists an approved server registry, pins stdio executable digests, enforces
// tool/project/user approval boundaries, launches each stdio call inside native
// process limits, performs bounded loopback Streamable HTTP exchanges, supports
// cancellation/timeouts, and records sanitized decisions in the audit chain.

#include "masterai.hpp"
#include "json.hpp"
#include "mcp_outbound_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace mcp_outbound_internal {

const std::size_t maximum_registry_servers = 256U;
constexpr std::size_t maximum_arguments = 32U;
constexpr std::size_t maximum_argument_bytes = 4096U;
constexpr std::uint64_t maximum_timeout_seconds = 300U;
constexpr std::uint64_t maximum_response_bytes = 16U * 1024U * 1024U;
constexpr std::uint64_t maximum_memory_limit_mib = 16384U;

McpOutboundResult validate_stdio_output(const std::string& output);

// Converts one completed stdio process into the shared transport result,
// preserving the same failure wording and JSON validation on both platforms.
McpOutboundResult finish_stdio_response(const bool exited_successfully,
                                        const std::string& output) {
    if (!exited_successfully) {
        return {false, false, {},
                "MCP stdio server exited with a failure status"};
    }
    return validate_stdio_output(output);
}

// Accepts stable identifiers used in registry keys, tool allow-lists, project
// bindings, and OS-secret names. Separators that could alter persistence or
// HTTP headers are excluded.
bool safe_identifier(const std::string& value) {
    return !value.empty() && value.size() <= 128U &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               const auto byte = static_cast<unsigned char>(character);
               return std::isalnum(byte) != 0 || character == '-' ||
                      character == '_' || character == '.';
           });
}

// Uses a length-prefixed record format so paths and process arguments can
// contain whitespace without invoking a shell or relying on delimiter escaping.
std::string pack(const std::vector<std::string>& fields) {
    std::string result;
    for (const auto& field : fields) {
        result += std::to_string(field.size()) + ":" + field;
    }
    return result;
}

// Reconstructs one complete length-prefixed record and rejects truncation,
// invalid lengths, and trailing ambiguity.
std::vector<std::string> unpack(const std::string& value) {
    std::vector<std::string> fields;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto colon = value.find(':', position);
        if (colon == std::string::npos || colon == position) {
            throw std::runtime_error("outbound MCP record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("outbound MCP record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

// Serializes an identifier set into a nested length-prefixed value, preserving
// deterministic order from std::set.
std::string pack_set(const std::set<std::string>& values) {
    return pack(std::vector<std::string>(values.begin(), values.end()));
}

// Validates every restored set member before granting it authority.
std::set<std::string> unpack_set(const std::string& value) {
    std::set<std::string> result;
    for (const auto& item : unpack(value)) {
        if (!safe_identifier(item) || !result.insert(item).second) {
            throw std::runtime_error("outbound MCP identifier set is invalid");
        }
    }
    return result;
}

// Converts transport values to durable names and keeps legacy SSE explicit.
std::string transport_name(const McpTransport transport) {
    if (transport == McpTransport::stdio_transport) return "stdio";
    if (transport == McpTransport::streamable_http) return "streamable-http";
    return "legacy-sse";
}

// Restores only known transport names. Legacy SSE is parsed so old records can
// be recognized, then rejected by current release policy.
McpTransport parse_transport(const std::string& value) {
    if (value == "stdio") return McpTransport::stdio_transport;
    if (value == "streamable-http") return McpTransport::streamable_http;
    if (value == "legacy-sse") return McpTransport::legacy_sse;
    throw std::runtime_error("outbound MCP transport is invalid");
}

// Performs transport-independent registry validation and returns a canonical,
// digest-pinned copy. Revalidation before every call catches executable swaps.
McpOutboundServer validate_server(McpOutboundServer server,
                                  const bool pin_digest) {
    if (!safe_identifier(server.id) || !server.enabled ||
        server.allowed_tools.empty() ||
        server.allowed_tools.size() > 256U ||
        server.timeout_seconds == 0U ||
        server.timeout_seconds > maximum_timeout_seconds ||
        server.maximum_output_bytes == 0U ||
        server.maximum_output_bytes > maximum_response_bytes ||
        server.memory_limit_mib < 64U ||
        server.memory_limit_mib > maximum_memory_limit_mib ||
        server.transport == McpTransport::legacy_sse) {
        throw std::invalid_argument("outbound MCP server policy is invalid");
    }
    for (const auto& tool : server.allowed_tools) {
        if (!safe_identifier(tool)) {
            throw std::invalid_argument("outbound MCP tool name is invalid");
        }
    }
    for (const auto& project : server.allowed_project_ids) {
        if (!safe_identifier(project)) {
            throw std::invalid_argument(
                "outbound MCP project binding is invalid");
        }
    }
    if (!server.credential_secret_name.empty() &&
        !safe_identifier(server.credential_secret_name)) {
        throw std::invalid_argument(
            "outbound MCP credential reference is invalid");
    }
    if (server.arguments.size() > maximum_arguments) {
        throw std::invalid_argument("too many outbound MCP arguments");
    }
    for (const auto& argument : server.arguments) {
        if (argument.size() > maximum_argument_bytes ||
            argument.find('\0') != std::string::npos ||
            argument.find('\r') != std::string::npos ||
            argument.find('\n') != std::string::npos) {
            throw std::invalid_argument(
                "outbound MCP process argument is invalid");
        }
    }

    if (server.transport == McpTransport::stdio_transport) {
        if (server.executable.empty()) {
            throw std::invalid_argument(
                "outbound MCP stdio executable is required");
        }
        std::error_code error;
        const auto canonical =
            std::filesystem::weakly_canonical(server.executable, error);
        if (error || !std::filesystem::is_regular_file(canonical) ||
            std::filesystem::is_symlink(server.executable)) {
            throw std::invalid_argument(
                "outbound MCP executable is not an approved regular file");
        }
        server.executable = canonical;
        if (server.working_directory.empty()) {
            server.working_directory = canonical.parent_path();
        }
        server.working_directory =
            std::filesystem::weakly_canonical(server.working_directory, error);
        if (error ||
            !std::filesystem::is_directory(server.working_directory) ||
            std::filesystem::is_symlink(server.working_directory)) {
            throw std::invalid_argument(
                "outbound MCP working directory is invalid");
        }
        const auto digest = sha256_file_hex(canonical);
        if (pin_digest || server.executable_sha256.empty()) {
            server.executable_sha256 = digest;
        } else if (!constant_time_equal(server.executable_sha256, digest)) {
            throw std::invalid_argument(
                "outbound MCP executable digest changed");
        }
        if (!server.endpoint.empty()) {
            throw std::invalid_argument(
                "stdio MCP server cannot define an HTTP endpoint");
        }
    } else {
        if (!server.executable.empty() || !server.arguments.empty() ||
            !server.executable_sha256.empty() ||
            !server.working_directory.empty()) {
            throw std::invalid_argument(
                "HTTP MCP server cannot define process fields");
        }
        const bool loopback =
            server.endpoint.rfind("http://127.0.0.1:", 0U) == 0U ||
            server.endpoint.rfind("http://[::1]:", 0U) == 0U;
        if (!loopback &&
            server.endpoint.rfind("https://", 0U) != 0U) {
            throw std::invalid_argument(
                "outbound MCP HTTP requires TLS or explicit loopback");
        }
        if (server.endpoint.size() > 2048U ||
            server.endpoint.find('\r') != std::string::npos ||
            server.endpoint.find('\n') != std::string::npos) {
            throw std::invalid_argument("outbound MCP endpoint is invalid");
        }
    }
    return server;
}

// Builds a JSON-RPC initialize request using the project-pinned MCP revision.
std::string initialize_request() {
    return "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
           "\"params\":{\"protocolVersion\":\"" +
           std::string(mcp_protocol_version()) +
           "\",\"capabilities\":{},\"clientInfo\":{\"name\":"
           "\"MasterAI-outbound\",\"version\":\"0.1.0\","
           "\"description\":\"Native C++17 isolated MCP client\"}}}";
}

// Builds a strict tool request after the caller has parsed arguments as an
// object. The raw object can be inserted safely because it came from JsonValue.
std::string tool_request(const McpOutboundCall& call) {
    std::string escaped_tool;
    escaped_tool.reserve(call.tool.size() + 2U);
    escaped_tool.push_back('"');
    escaped_tool += call.tool;
    escaped_tool.push_back('"');
    return "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\","
           "\"params\":{\"name\":" +
           escaped_tool + ",\"arguments\":" + call.arguments_json + "}}";
}

// Splits newline-delimited stdio output and validates that initialization and
// tool response IDs arrived in sequence. Unrelated stdout is a protocol error.
McpOutboundResult validate_stdio_output(const std::string& output) {
    std::istringstream lines(output);
    std::string line;
    bool initialized = false;
    while (std::getline(lines, line)) {
        if (line.empty()) continue;
        try {
            const auto root = parse_json(line);
            if (root.required("jsonrpc").as_string() != "2.0") {
                return {false, false, {}, "MCP response version is invalid"};
            }
            const auto* id = root.optional("id");
            if (id == nullptr ||
                id->type() != JsonValue::Type::number) {
                return {false, false, {},
                        "MCP response identifier is invalid"};
            }
            if (id->as_integer() == 1) {
                const auto& result = root.required("result");
                if (result.required("protocolVersion").as_string() !=
                    mcp_protocol_version()) {
                    return {false, false, {},
                            "MCP server negotiated an unsupported revision"};
                }
                initialized = true;
            } else if (id->as_integer() == 2 && initialized) {
                if (root.optional("result") == nullptr &&
                    root.optional("error") == nullptr) {
                    return {false, false, {},
                            "MCP tool response has no result or error"};
                }
                return {root.optional("result") != nullptr, false, line,
                        root.optional("result") != nullptr
                            ? "MCP tool call completed"
                            : "MCP tool call returned a protocol error"};
            } else {
                return {false, false, {},
                        "MCP response order or identifier is invalid"};
            }
        } catch (const std::exception&) {
            return {false, false, {}, "MCP response is malformed"};
        }
    }
    return {false, false, {}, "MCP server response was incomplete"};
}

#if defined(_WIN32)

// Converts bounded UTF-8 process values to UTF-16 without accepting malformed
// byte sequences.
std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("MCP process text is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), &result[0],
                            required) != required) {
        throw std::runtime_error("MCP process text conversion failed");
    }
    return result;
}

// Quotes one Windows command-line argument according to CommandLineToArgvW
// rules. CreateProcess receives the executable separately and never invokes a
// command shell.
std::wstring quote_argument(const std::wstring& argument) {
    if (argument.find_first_of(L" \t\"") == std::wstring::npos) {
        return argument;
    }
    std::wstring result{L"\""};
    std::size_t slashes = 0U;
    for (const wchar_t character : argument) {
        if (character == L'\\') {
            ++slashes;
            continue;
        }
        if (character == L'"') {
            result.append(slashes * 2U + 1U, L'\\');
            result.push_back(L'"');
        } else {
            result.append(slashes, L'\\');
            result.push_back(character);
        }
        slashes = 0U;
    }
    result.append(slashes * 2U, L'\\');
    result.push_back(L'"');
    return result;
}

// Owns a Windows handle and closes it on every error/cancellation path.
class UniqueHandle final {
public:
    UniqueHandle() = default;
    explicit UniqueHandle(HANDLE value) : value_(value) {}
    ~UniqueHandle() {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
    }
    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    HANDLE get() const noexcept { return value_; }
    HANDLE release() noexcept {
        const auto result = value_;
        value_ = nullptr;
        return result;
    }
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }

private:
    HANDLE value_{nullptr};
};

// Launches one stdio server with a minimal environment, job-object process and
// memory limits, no visible window, bounded pipes, timeout, and cancellation.
McpOutboundResult invoke_stdio(const McpOutboundServer& server,
                              const std::string& payload,
                              std::atomic_bool& cancellation) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE child_input_read_raw = nullptr;
    HANDLE parent_input_write_raw = nullptr;
    HANDLE parent_output_read_raw = nullptr;
    HANDLE child_output_write_raw = nullptr;
    if (!CreatePipe(&child_input_read_raw, &parent_input_write_raw,
                    &attributes, 0) ||
        !CreatePipe(&parent_output_read_raw, &child_output_write_raw,
                    &attributes, 0)) {
        return {false, false, {}, "MCP stdio pipe creation failed"};
    }
    UniqueHandle child_input_read(child_input_read_raw);
    UniqueHandle parent_input_write(parent_input_write_raw);
    UniqueHandle parent_output_read(parent_output_read_raw);
    UniqueHandle child_output_write(child_output_write_raw);
    SetHandleInformation(parent_input_write.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parent_output_read.get(), HANDLE_FLAG_INHERIT, 0);

    UniqueHandle null_error(CreateFileW(
        L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (null_error.get() == INVALID_HANDLE_VALUE || job.get() == nullptr) {
        return {false, false, {}, "MCP sandbox resource creation failed"};
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS |
        JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1U;
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(
        server.memory_limit_mib * 1024U * 1024U);
    if (!SetInformationJobObject(job.get(),
                                 JobObjectExtendedLimitInformation, &limits,
                                 sizeof(limits))) {
        return {false, false, {}, "MCP job-object policy failed"};
    }

    std::wstring command = quote_argument(server.executable.wstring());
    for (const auto& argument : server.arguments) {
        command.push_back(L' ');
        command += quote_argument(wide(argument));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    std::vector<wchar_t> environment{
        L'M', L'A', L'S', L'T', L'E', L'R', L'A', L'I', L'_',
        L'M', L'C', L'P', L'_', L'S', L'A', L'N', L'D', L'B',
        L'O', L'X', L'=', L'1', L'\0', L'\0'};

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input_read.get();
    startup.hStdOutput = child_output_write.get();
    startup.hStdError = null_error.get();
    PROCESS_INFORMATION process{};
    const auto executable = server.executable.wstring();
    const auto working = server.working_directory.wstring();
    if (!CreateProcessW(
            executable.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT,
            environment.data(), working.c_str(), &startup, &process)) {
        return {false, false, {}, "MCP stdio process launch failed"};
    }
    UniqueHandle process_handle(process.hProcess);
    UniqueHandle thread_handle(process.hThread);
    if (!AssignProcessToJobObject(job.get(), process_handle.get()) ||
        ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(process_handle.get(), 1U);
        return {false, false, {}, "MCP stdio sandbox assignment failed"};
    }
    child_input_read.reset();
    child_output_write.reset();

    DWORD written = 0U;
    if (payload.size() > std::numeric_limits<DWORD>::max() ||
        !WriteFile(parent_input_write.get(), payload.data(),
                   static_cast<DWORD>(payload.size()), &written, nullptr) ||
        written != payload.size()) {
        TerminateJobObject(job.get(), 1U);
        return {false, false, {}, "MCP stdio request write failed"};
    }
    parent_input_write.reset();

    std::string output;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(server.timeout_seconds);
    bool exited = false;
    while (!exited) {
        if (cancellation.load()) {
            TerminateJobObject(job.get(), 2U);
            return {false, true, {}, "MCP tool call was cancelled"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateJobObject(job.get(), 3U);
            return {false, false, {}, "MCP tool call timed out"};
        }
        DWORD available = 0U;
        if (!PeekNamedPipe(parent_output_read.get(), nullptr, 0, nullptr,
                           &available, nullptr)) {
            available = 0U;
        }
        if (available > 0U) {
            std::array<char, 8192U> buffer{};
            DWORD received = 0U;
            const DWORD wanted =
                std::min<DWORD>(available,
                                static_cast<DWORD>(buffer.size()));
            if (ReadFile(parent_output_read.get(), buffer.data(), wanted,
                         &received, nullptr) &&
                received > 0U) {
                if (output.size() + received >
                    server.maximum_output_bytes) {
                    TerminateJobObject(job.get(), 4U);
                    return {false, false, {},
                            "MCP stdio response exceeded its bound"};
                }
                output.append(buffer.data(), received);
            }
        }
        exited = WaitForSingleObject(process_handle.get(), 10U) ==
                 WAIT_OBJECT_0;
    }
    for (;;) {
        std::array<char, 8192U> buffer{};
        DWORD received = 0U;
        if (!ReadFile(parent_output_read.get(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &received, nullptr) ||
            received == 0U) {
            break;
        }
        if (output.size() + received > server.maximum_output_bytes) {
            return {false, false, {},
                    "MCP stdio response exceeded its bound"};
        }
        output.append(buffer.data(), received);
    }
    DWORD exit_code = 1U;
    GetExitCodeProcess(process_handle.get(), &exit_code);
    return finish_stdio_response(exit_code == 0U, output);
}

#else

// Writes a complete request pipe on Linux while honoring EINTR and preventing
// SIGPIPE from terminating the MasterAI control process.
bool write_all(const int descriptor, const std::string& value) {
    std::size_t offset = 0U;
    while (offset < value.size()) {
        const auto written =
            write(descriptor, value.data() + offset, value.size() - offset);
        if (written < 0 && errno == EINTR) continue;
        if (written <= 0) return false;
        offset += static_cast<std::size_t>(written);
    }
    return true;
}

// Launches one Linux stdio server with no-new-privileges, a cleared environment,
// process/file/address-space limits, bounded nonblocking output, timeout, and
// cancellation.
McpOutboundResult invoke_stdio(const McpOutboundServer& server,
                              const std::string& payload,
                              std::atomic_bool& cancellation) {
    int input_pipe[2]{-1, -1};
    int output_pipe[2]{-1, -1};
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        if (input_pipe[1] >= 0) close(input_pipe[1]);
        return {false, false, {}, "MCP stdio pipe creation failed"};
    }
    const pid_t child = fork();
    if (child < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return {false, false, {}, "MCP stdio fork failed"};
    }
    if (child == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        const int null_error = open("/dev/null", O_WRONLY);
        if (null_error >= 0) dup2(null_error, STDERR_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (null_error > STDERR_FILENO) close(null_error);
        if (chdir(server.working_directory.c_str()) != 0) _exit(126);
        rlimit cpu_limit{server.timeout_seconds + 1U,
                         server.timeout_seconds + 1U};
        rlimit file_limit{server.maximum_output_bytes,
                          server.maximum_output_bytes};
        rlimit memory_limit{
            server.memory_limit_mib * 1024U * 1024U,
            server.memory_limit_mib * 1024U * 1024U};
        rlimit process_limit{1U, 1U};
        if (setrlimit(RLIMIT_CPU, &cpu_limit) != 0 ||
            setrlimit(RLIMIT_FSIZE, &file_limit) != 0 ||
            setrlimit(RLIMIT_AS, &memory_limit) != 0 ||
            setrlimit(RLIMIT_NPROC, &process_limit) != 0 ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            _exit(126);
        }
        clearenv();
        setenv("MASTERAI_MCP_SANDBOX", "1", 1);
        std::vector<std::string> values;
        values.push_back(server.executable.string());
        values.insert(values.end(), server.arguments.begin(),
                      server.arguments.end());
        std::vector<char*> arguments;
        for (auto& value : values) arguments.push_back(&value[0]);
        arguments.push_back(nullptr);
        execv(server.executable.c_str(), arguments.data());
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);
    const auto previous_signal = std::signal(SIGPIPE, SIG_IGN);
    const bool wrote = write_all(input_pipe[1], payload);
    std::signal(SIGPIPE, previous_signal);
    close(input_pipe[1]);
    if (!wrote) {
        kill(child, SIGKILL);
        waitpid(child, nullptr, 0);
        close(output_pipe[0]);
        return {false, false, {}, "MCP stdio request write failed"};
    }
    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    std::string output;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(server.timeout_seconds);
    int status = 0;
    for (;;) {
        std::array<char, 8192U> buffer{};
        const auto received =
            read(output_pipe[0], buffer.data(), buffer.size());
        if (received > 0) {
            if (output.size() + static_cast<std::size_t>(received) >
                server.maximum_output_bytes) {
                kill(child, SIGKILL);
                waitpid(child, nullptr, 0);
                close(output_pipe[0]);
                return {false, false, {},
                        "MCP stdio response exceeded its bound"};
            }
            output.append(buffer.data(), static_cast<std::size_t>(received));
        }
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (cancellation.load()) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(output_pipe[0]);
            return {false, true, {}, "MCP tool call was cancelled"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(output_pipe[0]);
            return {false, false, {}, "MCP tool call timed out"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (;;) {
        std::array<char, 8192U> buffer{};
        const auto received =
            read(output_pipe[0], buffer.data(), buffer.size());
        if (received <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(received));
    }
    close(output_pipe[0]);
    return finish_stdio_response(
        WIFEXITED(status) && WEXITSTATUS(status) == 0, output);
}

#endif

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

// Closes a transport socket through the platform-native operation.
void close_socket(const NativeSocket socket) noexcept {
    if (socket == invalid_socket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

// Sends a complete bounded HTTP request and stops on cancellation or transport
// failure without retrying a potentially side-effecting tool call.
bool socket_send_all(const NativeSocket socket, const std::string& value,
                     std::atomic_bool& cancellation) {
    std::size_t sent = 0U;
    while (sent < value.size()) {
        if (cancellation.load()) return false;
        const auto amount =
            send(socket, value.data() + sent,
                 static_cast<int>(value.size() - sent), 0);
        if (amount <= 0) return false;
        sent += static_cast<std::size_t>(amount);
    }
    return true;
}

struct LoopbackEndpoint {
    bool ipv6{false};
    std::uint16_t port{0};
    std::string host_header;
    std::string path;
};

// Parses only explicit numeric loopback HTTP endpoints. DNS names are never
// resolved, preventing registry changes from becoming DNS-rebinding authority.
std::optional<LoopbackEndpoint> parse_loopback_endpoint(
    const std::string& endpoint) {
    bool ipv6 = false;
    std::size_t start = 0U;
    std::string host;
    if (endpoint.rfind("http://127.0.0.1:", 0U) == 0U) {
        start = std::strlen("http://127.0.0.1:");
        host = "127.0.0.1";
    } else if (endpoint.rfind("http://[::1]:", 0U) == 0U) {
        start = std::strlen("http://[::1]:");
        host = "[::1]";
        ipv6 = true;
    } else {
        return std::nullopt;
    }
    const auto slash = endpoint.find('/', start);
    const auto port_text = endpoint.substr(
        start, (slash == std::string::npos ? endpoint.size() : slash) - start);
    if (port_text.empty() ||
        !std::all_of(port_text.begin(), port_text.end(),
                     [](const char value) {
                         return value >= '0' && value <= '9';
                     })) {
        return std::nullopt;
    }
    const auto port_value = std::stoul(port_text);
    if (port_value == 0U || port_value > 65535U) return std::nullopt;
    return LoopbackEndpoint{
        ipv6, static_cast<std::uint16_t>(port_value),
        host + ":" + port_text,
        slash == std::string::npos ? "/mcp" : endpoint.substr(slash)};
}

// Performs one stateless Streamable HTTP POST and returns its bounded JSON body.
// HTTPS registry entries remain fail-closed until an approved native TLS
// transport is supplied; loopback HTTP never leaves the host.
McpOutboundResult http_exchange(const McpOutboundServer& server,
                                SecretStore& secrets,
                                const std::string& payload,
                                std::atomic_bool& cancellation,
                                const bool notification) {
    const auto endpoint = parse_loopback_endpoint(server.endpoint);
    if (!endpoint) {
        return {false, false, {},
                "remote HTTPS MCP transport is not configured in this build"};
    }
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        return {false, false, {}, "MCP HTTP socket startup failed"};
    }
#endif
    const int family = endpoint->ipv6 ? AF_INET6 : AF_INET;
    const NativeSocket socket_value = socket(family, SOCK_STREAM, IPPROTO_TCP);
    if (socket_value == invalid_socket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        return {false, false, {}, "MCP HTTP socket creation failed"};
    }
    const auto close_runtime = [&]() {
        close_socket(socket_value);
#if defined(_WIN32)
        WSACleanup();
#endif
    };

#if defined(_WIN32)
    const DWORD timeout = 200U;
    setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
    setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO,
               reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    timeval timeout{};
    timeout.tv_usec = 200000;
    setsockopt(socket_value, SOL_SOCKET, SO_RCVTIMEO, &timeout,
               sizeof(timeout));
    setsockopt(socket_value, SOL_SOCKET, SO_SNDTIMEO, &timeout,
               sizeof(timeout));
#endif

    int connected = -1;
    if (endpoint->ipv6) {
        sockaddr_in6 address{};
        address.sin6_family = AF_INET6;
        address.sin6_port = htons(endpoint->port);
        inet_pton(AF_INET6, "::1", &address.sin6_addr);
        connected = connect(socket_value,
                            reinterpret_cast<sockaddr*>(&address),
                            sizeof(address));
    } else {
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(endpoint->port);
        inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);
        connected = connect(socket_value,
                            reinterpret_cast<sockaddr*>(&address),
                            sizeof(address));
    }
    if (connected != 0) {
        close_runtime();
        return {false, false, {}, "MCP HTTP loopback connection failed"};
    }

    std::string authorization;
    if (!server.credential_secret_name.empty()) {
        auto credential = secrets.get(server.credential_secret_name);
        if (!credential || credential->empty() ||
            credential->find('\r') != std::string::npos ||
            credential->find('\n') != std::string::npos) {
            close_runtime();
            return {false, false, {},
                    "MCP HTTP credential is unavailable"};
        }
        authorization = "Authorization: Bearer " + *credential + "\r\n";
        std::fill(credential->begin(), credential->end(), '\0');
    }
    const std::string request =
        "POST " + endpoint->path + " HTTP/1.1\r\nHost: " +
        endpoint->host_header +
        "\r\nContent-Type: application/json\r\n"
        "Accept: application/json, text/event-stream\r\n"
        "MCP-Protocol-Version: " +
        std::string(mcp_protocol_version()) + "\r\n" + authorization +
        "Content-Length: " + std::to_string(payload.size()) +
        "\r\nConnection: close\r\n\r\n" + payload;
    if (!socket_send_all(socket_value, request, cancellation)) {
        close_runtime();
        return {false, cancellation.load(), {},
                cancellation.load() ? "MCP HTTP call was cancelled"
                                    : "MCP HTTP request write failed"};
    }

    std::string response;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::seconds(server.timeout_seconds);
    for (;;) {
        if (cancellation.load()) {
            close_runtime();
            return {false, true, {}, "MCP HTTP call was cancelled"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            close_runtime();
            return {false, false, {}, "MCP HTTP call timed out"};
        }
        std::array<char, 8192U> buffer{};
        const auto received =
            recv(socket_value, buffer.data(),
                 static_cast<int>(buffer.size()), 0);
        if (received > 0) {
            if (response.size() + static_cast<std::size_t>(received) >
                server.maximum_output_bytes + 16384U) {
                close_runtime();
                return {false, false, {},
                        "MCP HTTP response exceeded its bound"};
            }
            response.append(buffer.data(),
                            static_cast<std::size_t>(received));
            continue;
        }
        if (received == 0) break;
#if defined(_WIN32)
        const auto socket_error = WSAGetLastError();
        if (socket_error == WSAETIMEDOUT ||
            socket_error == WSAEWOULDBLOCK) {
            continue;
        }
#else
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
            continue;
        }
#endif
        close_runtime();
        return {false, false, {}, "MCP HTTP response read failed"};
    }
    close_runtime();

    const auto line_end = response.find("\r\n");
    const auto header_end = response.find("\r\n\r\n");
    if (line_end == std::string::npos || header_end == std::string::npos ||
        response.rfind("HTTP/1.1 ", 0U) != 0U) {
        return {false, false, {}, "MCP HTTP response framing is invalid"};
    }
    const auto status = std::stoi(response.substr(9U, 3U));
    const auto body = response.substr(header_end + 4U);
    if (notification && status == 202) {
        return {true, false, {}, "MCP notification accepted"};
    }
    if (status < 200 || status >= 300 || body.empty()) {
        return {false, false, {},
                "MCP HTTP server returned a non-success status"};
    }
    try {
        static_cast<void>(parse_json(body));
    } catch (const std::exception&) {
        return {false, false, {}, "MCP HTTP response is not valid JSON"};
    }
    return {true, false, body, "MCP HTTP exchange completed"};
}

// Runs the Streamable HTTP initialization, initialized notification, and tool
// call sequence. It never retries the final call, preventing duplicate effects.
McpOutboundResult invoke_http(const McpOutboundServer& server,
                             SecretStore& secrets,
                             const McpOutboundCall& call,
                             std::atomic_bool& cancellation) {
    auto initialized = http_exchange(
        server, secrets, initialize_request(), cancellation, false);
    if (!initialized.succeeded) return initialized;
    try {
        const auto root = parse_json(initialized.response_json);
        if (root.required("result")
                .required("protocolVersion")
                .as_string() != mcp_protocol_version()) {
            return {false, false, {},
                    "MCP HTTP server negotiated an unsupported revision"};
        }
    } catch (const std::exception&) {
        return {false, false, {},
                "MCP HTTP initialize response is malformed"};
    }
    auto notification = http_exchange(
        server, secrets,
        "{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\","
        "\"params\":{}}",
        cancellation, true);
    if (!notification.succeeded) return notification;
    auto result = http_exchange(
        server, secrets, tool_request(call), cancellation, false);
    if (!result.succeeded) return result;
    try {
        const auto root = parse_json(result.response_json);
        if (root.required("jsonrpc").as_string() != "2.0" ||
            root.required("id").as_integer() != 2 ||
            (root.optional("result") == nullptr &&
             root.optional("error") == nullptr)) {
            throw std::runtime_error("invalid tool response");
        }
        result.succeeded = root.optional("result") != nullptr;
        result.diagnostic = result.succeeded
                                ? "MCP HTTP tool call completed"
                                : "MCP HTTP tool call returned a protocol error";
        return result;
    } catch (const std::exception&) {
        return {false, false, {}, "MCP HTTP tool response is malformed"};
    }
}

}  // namespace mcp_outbound_internal

using namespace mcp_outbound_internal;

}  // namespace masterai
