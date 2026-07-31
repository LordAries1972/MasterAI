// MasterAI isolated llama.cpp adapter and supervised inference operations.
//
// The adapter validates model launches, confines backend process ownership, and
// exposes bounded tokenization and streaming generation through loopback HTTP.
#include "masterai.hpp"
#include "json.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <processthreadsapi.h>
#include <psapi.h>
#elif defined(__linux__)
#include <arpa/inet.h>
#include <csignal>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
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

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result.push_back(digits[character >> 4U]);
                    result.push_back(digits[character & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    return result;
}

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) throw std::runtime_error("path is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), result.data(),
                            size) != size) {
        throw std::runtime_error("UTF-8 path conversion failed");
    }
    return result;
}

std::wstring quote_windows_argument(const std::string& argument) {
    const auto input = widen(argument);
    if (input.find_first_of(L" \t\n\v\"") == std::wstring::npos) return input;
    std::wstring output(1U, L'"');
    std::size_t slashes = 0U;
    for (const wchar_t character : input) {
        if (character == L'\\') {
            ++slashes;
        } else if (character == L'"') {
            output.append(slashes * 2U + 1U, L'\\');
            output.push_back(character);
            slashes = 0U;
        } else {
            output.append(slashes, L'\\');
            slashes = 0U;
            output.push_back(character);
        }
    }
    output.append(slashes * 2U, L'\\');
    output.push_back(L'"');
    return output;
}
#endif

bool send_all(const NativeSocket socket, const std::string& value) {
    std::size_t sent = 0U;
    while (sent < value.size()) {
        const auto chunk =
            send(socket, value.data() + sent,
                 static_cast<int>(value.size() - sent), 0);
        if (chunk <= 0) return false;
        sent += static_cast<std::size_t>(chunk);
    }
    return true;
}

struct HttpResult {
    int status{0};
    std::string body;
};

HttpResult local_http(
    const unsigned int port, const std::string& method,
    const std::string& target, const std::string& body,
    const std::function<void(const std::string&)>& on_line,
    const std::atomic_bool* cancellation = nullptr) {
#if defined(_WIN32)
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) {
        throw std::runtime_error("runner IPC socket initialization failed");
    }
#endif
    const NativeSocket socket_value = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_value == invalid_socket) {
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("runner IPC socket creation failed");
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<std::uint16_t>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (connect(socket_value, reinterpret_cast<sockaddr*>(&address),
                sizeof(address)) != 0) {
        close_socket(socket_value);
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("runner IPC connection failed");
    }
    const std::string request =
        method + " " + target + " HTTP/1.1\r\nHost: 127.0.0.1\r\n"
        "Content-Type: application/json\r\nAccept: application/json\r\n"
        "Connection: close\r\nContent-Length: " +
        std::to_string(body.size()) + "\r\n\r\n" + body;
    if (!send_all(socket_value, request)) {
        close_socket(socket_value);
#if defined(_WIN32)
        WSACleanup();
#endif
        throw std::runtime_error("runner IPC request failed");
    }

    std::string raw;
    std::string wire_body;
    std::string line_buffer;
    HttpResult result;
    bool headers_parsed = false;
    bool chunked = false;
    bool final_chunk = false;
    const auto dispatch_lines = [&]() {
        std::size_t line_end = 0U;
        while ((line_end = line_buffer.find('\n')) != std::string::npos) {
            std::string line = line_buffer.substr(0U, line_end);
            line_buffer.erase(0U, line_end + 1U);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.rfind("data: ", 0U) == 0U) line.erase(0U, 6U);
            if (on_line && !line.empty() && line != "[DONE]") on_line(line);
        }
    };
    const auto consume_body = [&]() {
        if (!headers_parsed) return;
        if (!chunked) {
            result.body += wire_body;
            line_buffer += wire_body;
            wire_body.clear();
            dispatch_lines();
            return;
        }
        while (!final_chunk) {
            const auto size_end = wire_body.find("\r\n");
            if (size_end == std::string::npos) return;
            std::uint64_t size = 0U;
            std::istringstream size_text(wire_body.substr(0U, size_end));
            size_text >> std::hex >> size;
            if (!size_text) {
                throw std::runtime_error("runner IPC chunk size was invalid");
            }
            if (size == 0U) {
                final_chunk = true;
                wire_body.erase(0U, size_end + 2U);
                return;
            }
            const auto data_start = size_end + 2U;
            if (size > wire_body.size() - data_start ||
                wire_body.size() - data_start <
                    static_cast<std::size_t>(size) + 2U) {
                return;
            }
            const auto data =
                wire_body.substr(data_start, static_cast<std::size_t>(size));
            result.body += data;
            line_buffer += data;
            wire_body.erase(0U, data_start + static_cast<std::size_t>(size) + 2U);
            dispatch_lines();
        }
    };
    std::array<char, 8192> buffer{};
    while (true) {
        if (cancellation != nullptr && cancellation->load()) break;
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket_value, &readable);
        timeval timeout{};
        timeout.tv_sec = 0;
        timeout.tv_usec = 100000;
        const int selected =
#if defined(_WIN32)
            select(0, &readable, nullptr, nullptr, &timeout);
#else
            select(socket_value + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (selected == 0) continue;
        if (selected < 0) {
            close_socket(socket_value);
#if defined(_WIN32)
            WSACleanup();
#endif
            throw std::runtime_error("runner IPC wait failed");
        }
        const auto received =
            recv(socket_value, buffer.data(), static_cast<int>(buffer.size()), 0);
        if (received <= 0) break;
        if (!headers_parsed) {
            raw.append(buffer.data(), static_cast<std::size_t>(received));
            const auto header_end = raw.find("\r\n\r\n");
            if (header_end == std::string::npos) {
                if (raw.size() > 64U * 1024U) {
                    throw std::runtime_error("runner IPC headers exceeded policy");
                }
                continue;
            }
            std::istringstream status_line(raw.substr(0U, raw.find("\r\n")));
            std::string version;
            status_line >> version >> result.status;
            if (!status_line || version.rfind("HTTP/", 0U) != 0U) {
                throw std::runtime_error("runner IPC status was invalid");
            }
            const auto headers = raw.substr(0U, header_end);
            chunked =
                headers.find("Transfer-Encoding: chunked") != std::string::npos ||
                headers.find("transfer-encoding: chunked") != std::string::npos;
            wire_body = raw.substr(header_end + 4U);
            raw.clear();
            headers_parsed = true;
        } else {
            wire_body.append(buffer.data(), static_cast<std::size_t>(received));
        }
        consume_body();
    }
    close_socket(socket_value);
#if defined(_WIN32)
    WSACleanup();
#endif
    if (!headers_parsed) {
        throw std::runtime_error("runner IPC response was incomplete");
    }
    consume_body();
    if (on_line && !line_buffer.empty()) {
        auto line = line_buffer;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.rfind("data: ", 0U) == 0U) line.erase(0U, 6U);
        if (!line.empty() && line != "[DONE]") on_line(line);
    }
    return result;
}

}  // namespace

class RunnerSupervisor::Process final {
public:
    ~Process() { stop(2U); }

    void start(const LaunchSpec& specification,
               const std::filesystem::path& log_path) {
        if (running()) throw std::logic_error("runner process is already active");
        std::filesystem::create_directories(log_path.parent_path());
#if defined(_WIN32)
        SECURITY_ATTRIBUTES attributes{};
        attributes.nLength = sizeof(attributes);
        attributes.bInheritHandle = TRUE;
        const auto wide_log = widen(log_path.string());
        HANDLE log = CreateFileW(wide_log.c_str(), FILE_APPEND_DATA,
                                 FILE_SHARE_READ | FILE_SHARE_WRITE,
                                 &attributes, OPEN_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        if (log == INVALID_HANDLE_VALUE) {
            throw std::runtime_error("runner log could not be opened");
        }
        auto application = widen(specification.executable.string());
        std::wstring command = quote_windows_argument(
            specification.executable.string());
        for (const auto& argument : specification.arguments) {
            command += L" " + quote_windows_argument(argument);
        }
        std::vector<wchar_t> mutable_command(command.begin(), command.end());
        mutable_command.push_back(L'\0');
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdOutput = log;
        startup.hStdError = log;
        startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        const auto working = widen(specification.working_directory.string());
        const BOOL created = CreateProcessW(
            application.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW, nullptr, working.c_str(), &startup, &process);
        CloseHandle(log);
        if (!created) throw std::runtime_error("runner process launch failed");
        CloseHandle(process.hThread);
        handle_ = process.hProcess;
        process_id_ = process.dwProcessId;
#else
        const int log = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
        if (log < 0) throw std::runtime_error("runner log could not be opened");
        const pid_t child = fork();
        if (child < 0) {
            close(log);
            throw std::runtime_error("runner process fork failed");
        }
        if (child == 0) {
            if (chdir(specification.working_directory.c_str()) != 0 ||
                dup2(log, STDOUT_FILENO) < 0 ||
                dup2(log, STDERR_FILENO) < 0) {
                _exit(126);
            }
            close(log);
            std::vector<std::string> values;
            values.push_back(specification.executable.string());
            values.insert(values.end(), specification.arguments.begin(),
                          specification.arguments.end());
            std::vector<char*> arguments;
            for (auto& value : values) arguments.push_back(value.data());
            arguments.push_back(nullptr);
            execv(specification.executable.c_str(), arguments.data());
            _exit(127);
        }
        close(log);
        process_id_ = static_cast<std::uint64_t>(child);
#endif
    }

    bool running() const noexcept {
#if defined(_WIN32)
        if (handle_ == nullptr) return false;
        return WaitForSingleObject(handle_, 0) == WAIT_TIMEOUT;
#else
        if (process_id_ == 0U) return false;
        return kill(static_cast<pid_t>(process_id_), 0) == 0;
#endif
    }

    void stop(const std::uint32_t grace_seconds) noexcept {
        if (!running()) {
            close_handle();
            return;
        }
#if defined(_WIN32)
        if (WaitForSingleObject(handle_, grace_seconds * 1000U) == WAIT_TIMEOUT) {
            TerminateProcess(handle_, 1U);
            WaitForSingleObject(handle_, 2000U);
        }
#else
        kill(static_cast<pid_t>(process_id_), SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(grace_seconds);
        int status = 0;
        while (std::chrono::steady_clock::now() < deadline) {
            if (waitpid(static_cast<pid_t>(process_id_), &status, WNOHANG) > 0) {
                process_id_ = 0U;
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        kill(static_cast<pid_t>(process_id_), SIGKILL);
        waitpid(static_cast<pid_t>(process_id_), &status, 0);
#endif
        close_handle();
    }

    std::uint64_t id() const noexcept { return process_id_; }

    std::uint64_t resident_memory() const noexcept {
#if defined(_WIN32)
        if (handle_ == nullptr) return 0U;
        PROCESS_MEMORY_COUNTERS counters{};
        return GetProcessMemoryInfo(handle_, &counters, sizeof(counters))
                   ? static_cast<std::uint64_t>(counters.WorkingSetSize)
                   : 0U;
#else
        if (process_id_ == 0U) return 0U;
        std::ifstream input("/proc/" + std::to_string(process_id_) + "/status");
        std::string key;
        while (input >> key) {
            if (key == "VmRSS:") {
                std::uint64_t kib = 0U;
                input >> kib;
                return kib * 1024U;
            }
            std::string remainder;
            std::getline(input, remainder);
        }
        return 0U;
#endif
    }

private:
    void close_handle() noexcept {
#if defined(_WIN32)
        if (handle_ != nullptr) CloseHandle(handle_);
        handle_ = nullptr;
#endif
        process_id_ = 0U;
    }

#if defined(_WIN32)
    HANDLE handle_{nullptr};
#endif
    std::uint64_t process_id_{0};
};

RunnerSupervisor::RunnerSupervisor(std::filesystem::path approved_backend,
                                   std::filesystem::path runtime_root)
    : runtime_root_(std::move(runtime_root)),
      adapter_(std::move(approved_backend)),
      process_(std::make_unique<Process>()) {
    if (runtime_root_.empty()) {
        throw std::invalid_argument("runner runtime root is required");
    }
}

RunnerSupervisor::~RunnerSupervisor() { unload(); }

void RunnerSupervisor::load(const ModelRecord& model,
                            const unsigned int context_length,
                            const unsigned int port,
                            const std::uint32_t startup_timeout_seconds,
                            const unsigned int parallel_slots,
                            const LaunchTuning& tuning) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.state != RunnerState::unloaded &&
        metrics_.state != RunnerState::failed) {
        throw std::logic_error("a runner is already loaded");
    }
    metrics_ = {};
    metrics_.state = RunnerState::starting;
    metrics_.model_id = model.manifest.id;
    port_ = port;
    try {
        process_->start(adapter_.build_launch_spec(model, context_length, port,
                                                    parallel_slots, tuning),
                        runtime_root_ / "logs" /
                            ("runner-" + model.manifest.id + ".log"));
        metrics_.process_id = process_->id();
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(startup_timeout_seconds);
        bool ready = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!process_->running()) break;
            try {
                const auto health =
                    local_http(port_, "GET", "/health", "", {});
                if (health.status == 200) {
                    ready = true;
                    break;
                }
            } catch (const std::exception&) {
                // The runner may still be loading model weights.
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        if (!ready) throw std::runtime_error("runner readiness timed out");
        metrics_.state = RunnerState::ready;
        metrics_.diagnostic.clear();
    } catch (const std::exception& exception) {
        process_->stop(1U);
        metrics_.state = RunnerState::failed;
        metrics_.diagnostic = exception.what();
        throw;
    }
}

void RunnerSupervisor::unload(const std::uint32_t grace_seconds) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.state == RunnerState::unloaded) return;
    metrics_.state = RunnerState::stopping;
    process_->stop(grace_seconds);
    metrics_ = {};
}

// Returns the configured port only while the runner is ready and optionally
// reserves it for an exclusive generation request.
unsigned int RunnerSupervisor::ready_port(const bool mark_busy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.state != RunnerState::ready) {
        throw std::logic_error("runner is not ready");
    }
    if (mark_busy) metrics_.state = RunnerState::busy;
    return port_;
}

// Tokenizes one policy-bounded prompt through the ready isolated runner.
std::uint64_t RunnerSupervisor::tokenize(const std::string& text) {
    if (text.empty() || text.size() > 16U * 1024U * 1024U) {
        throw std::invalid_argument("tokenization input is outside policy");
    }
    const auto port = ready_port(false);
    const auto result = local_http(
        port, "POST", "/tokenize",
        "{\"content\":\"" + json_escape(text) + "\"}", {});
    if (result.status != 200) {
        throw std::runtime_error("runner tokenization request failed");
    }
    const auto root = parse_json(result.body);
    return static_cast<std::uint64_t>(
        root.required("tokens").as_array().size());
}

GenerationResult RunnerSupervisor::generate(
    const std::string& prompt, const GenerationOptions& options,
    const std::function<void(const std::string&)>& on_chunk,
    const std::atomic_bool& cancellation) {
    if (prompt.empty() || prompt.size() > 16U * 1024U * 1024U ||
        options.max_tokens == 0U || options.max_tokens > 32768U ||
        options.temperature < 0.0 || options.temperature > 2.0 ||
        options.stop_sequences.size() > 16U) {
        throw std::invalid_argument("generation request is outside policy");
    }
    const auto port = ready_port(true);
    GenerationResult generated;
    const auto start = std::chrono::steady_clock::now();
    try {
        std::string stops{"["};
        for (std::size_t i = 0; i < options.stop_sequences.size(); ++i) {
            if (i != 0U) stops += ",";
            stops += "\"" + json_escape(options.stop_sequences[i]) + "\"";
        }
        stops += "]";
        // Phase 18: cache_prompt/id_slot are only ever set by
        // PromptSessionManager after it has verified an exact compatibility
        // fingerprint and a literal byte-prefix match against the slot's
        // last prompt (see masterai.hpp); every other caller leaves them at
        // their safe defaults (false / absent), which is ordinary
        // from-scratch prompt evaluation, unchanged from before Phase 18.
        std::string body =
            "{\"prompt\":\"" + json_escape(prompt) + "\",\"n_predict\":" +
            std::to_string(options.max_tokens) + ",\"temperature\":" +
            std::to_string(options.temperature) + ",\"seed\":" +
            std::to_string(options.seed) + ",\"stop\":" + stops +
            ",\"cache_prompt\":" +
            (options.cache_prompt ? "true" : "false");
        if (options.slot_id.has_value()) {
            body += ",\"id_slot\":" + std::to_string(*options.slot_id);
        }
        body += ",\"stream\":true}";
        const auto result = local_http(
            port, "POST", "/completion", body,
            [&](const std::string& line) {
                const auto event = parse_json(line);
                const auto* content = event.optional("content");
                if (content != nullptr) {
                    const auto& chunk = content->as_string();
                    generated.text += chunk;
                    if (!chunk.empty()) {
                        ++generated.generated_tokens;
                        if (on_chunk) on_chunk(chunk);
                    }
                }
                const auto* tokens_evaluated = event.optional("tokens_evaluated");
                if (tokens_evaluated != nullptr) {
                    generated.prompt_tokens = static_cast<std::uint64_t>(
                        tokens_evaluated->as_integer());
                }
                const auto* tokens_predicted = event.optional("tokens_predicted");
                if (tokens_predicted != nullptr) {
                    generated.generated_tokens = static_cast<std::uint64_t>(
                        tokens_predicted->as_integer());
                }
            },
            &cancellation);
        if (result.status != 200 && !cancellation.load()) {
            throw std::runtime_error("runner generation request failed");
        }
        generated.cancelled = cancellation.load();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.state = process_->running() ? RunnerState::ready
                                             : RunnerState::failed;
        throw;
    }
    generated.elapsed_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        metrics_.state = process_->running() ? RunnerState::ready
                                             : RunnerState::failed;
        if (generated.cancelled) ++metrics_.requests_cancelled;
        else ++metrics_.requests_completed;
    }
    return generated;
}

RunnerMetrics RunnerSupervisor::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = metrics_;
    result.resident_memory_bytes = process_->resident_memory();
    return result;
}

}  // namespace masterai
