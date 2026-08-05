// MasterAI isolated llama.cpp adapter and supervised inference operations.
//
// The adapter validates model launches, confines backend process ownership, and
// exposes bounded tokenization and streaming generation through loopback HTTP.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <optional>
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
#include <cstdlib>
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

// Phase 26: wall-clock epoch seconds used to stamp WarmModelTracker activity
// (idle-timeout accounting), matching the same
// system_clock::now().time_since_epoch() pattern calibration.cpp already
// uses for TuningProfile::calibrated_at_epoch_seconds.
std::uint64_t current_epoch_seconds() noexcept {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
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

// Phase 30A: merges the current process's inherited environment with
// `overrides` (overrides win on name collision, case-insensitively as
// Windows environment blocks require) into the null-string-terminated,
// double-null-terminated wide block CreateProcessW's lpEnvironment expects
// when launched with CREATE_UNICODE_ENVIRONMENT. Returns an empty vector
// when there are no overrides, so the caller can pass nullptr and fully
// reproduce pre-Phase-30A behavior (full, unmodified inheritance) exactly.
std::vector<wchar_t> build_environment_block(
    const std::map<std::string, std::string>& overrides) {
    if (overrides.empty()) return {};
    LPWCH inherited = GetEnvironmentStringsW();
    if (inherited == nullptr) {
        throw std::runtime_error("runner environment could not be read");
    }
    std::map<std::wstring, std::wstring, std::less<>> merged;
    for (const wchar_t* cursor = inherited; *cursor != L'\0';) {
        const std::wstring entry(cursor);
        cursor += entry.size() + 1U;
        const auto separator = entry.find(L'=');
        // Windows environment blocks legitimately contain entries starting
        // with '=' (per-drive current directory pseudo-variables); skip
        // anything where '=' isn't a real name/value separator.
        if (separator == std::wstring::npos || separator == 0U) continue;
        std::wstring name = entry.substr(0U, separator);
        std::transform(name.begin(), name.end(), name.begin(), ::towupper);
        merged[name] = entry.substr(separator + 1U);
    }
    FreeEnvironmentStringsW(inherited);
    for (const auto& [name, value] : overrides) {
        std::wstring wide_name = widen(name);
        std::transform(wide_name.begin(), wide_name.end(), wide_name.begin(),
                       ::towupper);
        merged[wide_name] = widen(value);
    }
    std::vector<wchar_t> block;
    for (const auto& [name, value] : merged) {
        block.insert(block.end(), name.begin(), name.end());
        block.push_back(L'=');
        block.insert(block.end(), value.begin(), value.end());
        block.push_back(L'\0');
    }
    block.push_back(L'\0');
    return block;
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
    const std::atomic_bool* cancellation = nullptr,
    // Watchdog against a runner that accepts the connection and then never
    // answers (e.g. deadlocks building its first inference graph on a cold
    // model): measured from the last byte actually received, not from
    // connection start, so a slow-but-streaming generation never trips it.
    std::chrono::milliseconds stall_timeout = std::chrono::seconds(120)) {
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
            // SSE frames a stream as "data: <payload>" lines plus other
            // field types (":" comments used as keep-alive pings during a
            // slow prompt eval, "event:"/"id:"/"retry:") that are not
            // payload at all. Forwarding every non-empty line regardless of
            // its field name -- as this used to -- hands parse_json() a
            // bare keep-alive comment the moment a runner is slow enough to
            // emit one mid-stream, which fails immediately as invalid JSON.
            // Only actual "data:" lines are payload; everything else is
            // silently dropped, matching the SSE spec.
            const bool is_data_line = line.rfind("data: ", 0U) == 0U;
            if (!is_data_line) continue;
            line.erase(0U, 6U);
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
    auto last_activity = std::chrono::steady_clock::now();
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
        if (selected == 0) {
            if (std::chrono::steady_clock::now() - last_activity > stall_timeout) {
                close_socket(socket_value);
#if defined(_WIN32)
                WSACleanup();
#endif
                throw std::runtime_error("runner IPC stalled: no response received "
                                          "within the stall timeout");
            }
            continue;
        }
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
        last_activity = std::chrono::steady_clock::now();
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
        // Same "data:"-only rule as dispatch_lines() above, applied to
        // whatever's left in the buffer once the connection has closed.
        if (line.rfind("data: ", 0U) == 0U) {
            line.erase(0U, 6U);
            if (!line.empty() && line != "[DONE]") on_line(line);
        }
    }
    return result;
}

}  // namespace

// Phase 26: WarmModelState/WarmModelTracker -- see masterai.hpp for the
// full rationale. to_string(), the baseline translation table, and the
// legal-transition graph live here (next to RunnerSupervisor, the class
// that actually drives them) rather than in calibration.cpp/models.cpp.
std::string to_string(const WarmModelState state) {
    switch (state) {
        case WarmModelState::Cold: return "Cold";
        case WarmModelState::LoadingMetadata: return "LoadingMetadata";
        case WarmModelState::MappingWeights: return "MappingWeights";
        case WarmModelState::InitialisingBackend: return "InitialisingBackend";
        case WarmModelState::Warming: return "Warming";
        case WarmModelState::Ready: return "Ready";
        case WarmModelState::Busy: return "Busy";
        case WarmModelState::Idle: return "Idle";
        case WarmModelState::Draining: return "Draining";
        case WarmModelState::Evicting: return "Evicting";
        case WarmModelState::Unloaded: return "Unloaded";
        case WarmModelState::Failed: return "Failed";
    }
    return "Unknown";
}

// The regression-safe baseline table required by the plan: every
// RunnerState maps to exactly one WarmModelState. "starting" maps to the
// generic MappingWeights rather than any of its finer sub-phases -- those
// (LoadingMetadata/InitialisingBackend/Warming) are only ever reached via
// RunnerSupervisor's explicit WarmModelTracker::enter() calls at the exact
// points in load() that know which sub-phase is actually happening; a
// caller that only ever calls observe_runner_state() (as this table
// implies) still gets a legal, if coarser, walk through the state graph.
WarmModelState translate_runner_state(const RunnerState state) noexcept {
    switch (state) {
        case RunnerState::unloaded: return WarmModelState::Unloaded;
        case RunnerState::starting: return WarmModelState::MappingWeights;
        case RunnerState::ready: return WarmModelState::Ready;
        case RunnerState::busy: return WarmModelState::Busy;
        case RunnerState::stopping: return WarmModelState::Draining;
        case RunnerState::failed: return WarmModelState::Failed;
    }
    return WarmModelState::Failed;
}

namespace {

// Phase 26: the legal WarmModelState graph. Expressed as an explicit edge
// list (rather than a switch of allowed-next-sets) so the whole graph is
// visible in one place for review/testing.
bool warm_state_edge(const WarmModelState from, const WarmModelState to) noexcept {
    using W = WarmModelState;
    static const std::pair<W, W> edges[] = {
        {W::Cold, W::LoadingMetadata}, {W::Cold, W::Failed},
        {W::LoadingMetadata, W::MappingWeights}, {W::LoadingMetadata, W::Failed},
        {W::MappingWeights, W::InitialisingBackend}, {W::MappingWeights, W::Failed},
        {W::InitialisingBackend, W::Warming}, {W::InitialisingBackend, W::Failed},
        {W::Warming, W::Ready}, {W::Warming, W::Failed},
        {W::Ready, W::Busy}, {W::Ready, W::Idle}, {W::Ready, W::Draining},
        {W::Ready, W::Failed},
        {W::Busy, W::Ready}, {W::Busy, W::Draining}, {W::Busy, W::Failed},
        {W::Idle, W::Busy}, {W::Idle, W::Ready}, {W::Idle, W::Draining},
        {W::Idle, W::Failed},
        {W::Draining, W::Evicting}, {W::Draining, W::Unloaded},
        {W::Evicting, W::Unloaded},
        {W::Unloaded, W::LoadingMetadata},
        {W::Failed, W::LoadingMetadata}, {W::Failed, W::Unloaded},
    };
    for (const auto& edge : edges) {
        if (edge.first == from && edge.second == to) return true;
    }
    return false;
}

}  // namespace

bool warm_state_transition_allowed(const WarmModelState from,
                                   const WarmModelState to) noexcept {
    if (from == to) return true;  // self-transition: always a legal no-op
    return warm_state_edge(from, to);
}

void WarmModelTracker::observe_runner_state(const RunnerState state) {
    enter(translate_runner_state(state));
}

void WarmModelTracker::enter(const WarmModelState next) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!warm_state_transition_allowed(state_, next)) {
        throw std::logic_error(
            "illegal warm-model state transition from " + to_string(state_) +
            " to " + to_string(next));
    }
    state_ = next;
}

WarmModelState WarmModelTracker::current() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return state_;
}

void WarmModelTracker::record_activity(
    const std::uint64_t now_epoch_seconds) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    last_activity_epoch_seconds_ = now_epoch_seconds;
}

std::uint64_t WarmModelTracker::last_activity_epoch_seconds() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return last_activity_epoch_seconds_;
}

bool WarmModelTracker::apply_idle_timeout(
    const std::uint64_t now_epoch_seconds,
    const std::uint32_t idle_unload_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (state_ != WarmModelState::Ready) return false;
    if (now_epoch_seconds < last_activity_epoch_seconds_) return false;
    if (now_epoch_seconds - last_activity_epoch_seconds_ < idle_unload_seconds) {
        return false;
    }
    // Ready -> Idle is a legal edge (see warm_state_edge above); bypass
    // enter()'s own locking since this method already holds mutex_.
    state_ = WarmModelState::Idle;
    return true;
}

// Phase 26: cooperative-cancellation background warm-up runner. See the
// declaration in masterai.hpp for the full rationale -- deliberately
// synchronous (a caller wanting "background" wraps this in its own thread)
// so it stays trivially testable.
WarmupOutcome run_cancellable_warmup(const std::function<bool()>& step,
                                     WarmupCancellationToken& token,
                                     const MemoryBudgetManager& memory,
                                     const std::size_t maximum_steps,
                                     const std::function<bool()>& should_yield) {
    for (std::size_t iteration = 0U; iteration < maximum_steps; ++iteration) {
        if (token.is_cancelled()) return WarmupOutcome::cancelled;
        if (!memory.permits_background_work()) {
            return WarmupOutcome::skipped_low_memory;
        }
        // The caller supplies its platform-calibrated composite pressure
        // signal (interactive demand, thermal state, or rising storage
        // latency). Keeping those probes outside this primitive avoids
        // hidden globals and makes every yield decision deterministic.
        if (should_yield && should_yield()) {
            return WarmupOutcome::skipped_system_pressure;
        }
        if (step()) return WarmupOutcome::completed;
    }
    // Exhausting the step budget without `step` ever reporting "done" still
    // counts as completed (not cancelled/skipped) -- the caller's `step`
    // owns what "done" means; this is just a runaway-loop backstop.
    return WarmupOutcome::completed;
}

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
        auto environment_block = build_environment_block(specification.environment);
        const DWORD creation_flags =
            CREATE_NO_WINDOW |
            (environment_block.empty() ? 0U
                                       : static_cast<DWORD>(
                                             CREATE_UNICODE_ENVIRONMENT));
        const BOOL created = CreateProcessW(
            application.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
            creation_flags,
            environment_block.empty() ? nullptr : environment_block.data(),
            working.c_str(), &startup, &process);
        CloseHandle(log);
        if (!created) throw std::runtime_error("runner process launch failed");
        CloseHandle(process.hThread);
        handle_ = process.hProcess;
        process_id_ = process.dwProcessId;
        // Model warm-up (weight mmap/IO plus backend init) is CPU/IO-heavy
        // enough on a cold load to starve the rest of the OS scheduler if it
        // runs at the same NORMAL_PRIORITY_CLASS as everything else on the
        // box -- that starvation is what surfaced as the whole system (not
        // just this app) freezing while a model warmed up. Dropping the
        // runner one notch below normal keeps it a background-friendly
        // neighbor without materially slowing the load itself. Best-effort:
        // a failure here just leaves the process at its inherited default
        // priority, which is what every prior build already did.
        SetPriorityClass(handle_, BELOW_NORMAL_PRIORITY_CLASS);
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
            // Same reasoning as the Windows BELOW_NORMAL_PRIORITY_CLASS call
            // above: a cold model load is CPU/IO-heavy enough to starve the
            // rest of the system at the default niceness. Best-effort --
            // ignore failure (e.g. if the OS clamps it) and keep launching.
            [[maybe_unused]] const int nice_result = nice(5);
            // Phase 30A: applied only in the forked child, so this never
            // mutates the parent (control-plane) process's environment.
            for (const auto& [name, value] : specification.environment) {
                setenv(name.c_str(), value.c_str(), 1);
            }
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

    // Phase 26: return value reports whether the process had to be forced
    // down (TerminateProcess/SIGKILL) rather than exiting within the grace
    // period -- used by RunnerSupervisor::unload() to distinguish the
    // WarmModelState::Draining (graceful) vs Evicting (forced) sub-phase.
    // Purely additive: no existing caller inspected the old void return.
    bool stop(const std::uint32_t grace_seconds) noexcept {
        if (!running()) {
            close_handle();
            return false;
        }
        bool forced = false;
#if defined(_WIN32)
        if (WaitForSingleObject(handle_, grace_seconds * 1000U) == WAIT_TIMEOUT) {
            TerminateProcess(handle_, 1U);
            WaitForSingleObject(handle_, 2000U);
            forced = true;
        }
#else
        kill(static_cast<pid_t>(process_id_), SIGTERM);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(grace_seconds);
        int status = 0;
        bool exited = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (waitpid(static_cast<pid_t>(process_id_), &status, WNOHANG) > 0) {
                exited = true;
                process_id_ = 0U;
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        if (!exited) {
            kill(static_cast<pid_t>(process_id_), SIGKILL);
            waitpid(static_cast<pid_t>(process_id_), &status, 0);
            forced = true;
        } else {
            close_handle();
            return forced;
        }
#endif
        close_handle();
        return forced;
    }

    std::uint64_t id() const noexcept { return process_id_; }

    // Lets load() tell "the backend crashed on startup" apart from "the
    // backend is still loading weights" -- both look identical from the
    // readiness-poll loop's perspective (no 200 from /health yet), but only
    // the first one is a lost cause worth reporting distinctly instead of
    // under the same generic "runner readiness timed out" message. Returns
    // nullopt while the process is still running or its exit status is not
    // (yet) obtainable.
    std::optional<int> exit_code() const noexcept {
#if defined(_WIN32)
        if (handle_ == nullptr) return std::nullopt;
        DWORD code = 0U;
        if (!GetExitCodeProcess(handle_, &code) || code == STILL_ACTIVE) {
            return std::nullopt;
        }
        return static_cast<int>(code);
#else
        if (process_id_ == 0U) return std::nullopt;
        int status = 0;
        const pid_t result = waitpid(static_cast<pid_t>(process_id_), &status, WNOHANG);
        if (result <= 0) return std::nullopt;
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
#endif
    }

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
                            const LaunchTuning& tuning,
                            const std::string& accelerator_policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.state != RunnerState::unloaded &&
        metrics_.state != RunnerState::failed) {
        throw std::logic_error("a runner is already loaded");
    }
    metrics_ = {};
    metrics_.state = RunnerState::starting;
    metrics_.model_id = model.manifest.id;
    metrics_.requested_gpu_layers = tuning.gpu_layers;
    metrics_.accelerator_policy = accelerator_policy;
    port_ = port;
    // Phase 26: LoadingMetadata -- manifest/model record already validated
    // by the caller, launch spec about to be built. Legal from Cold
    // (first-ever load), Unloaded (reload after a clean unload), or Failed
    // (retry after a previous failed load) -- see warm_state_edge().
    warm_tracker_.enter(WarmModelState::LoadingMetadata);
    // Phase 23: captured for tokenize()'s cache key -- see
    // set_tokenization_cache(). The model's own already-verified content
    // digest doubles as the tokenizer/vocabulary fingerprint, since a given
    // model file always tokenizes the same text the same way.
    model_sha256_ = model.manifest.model_sha256;
    active_generations_ = 0U;
    maximum_parallel_generations_ =
        tuning.continuous_batching ? parallel_slots : 1U;
    try {
        const auto spec = adapter_.build_launch_spec(
            model, context_length, port, parallel_slots, tuning,
            accelerator_policy);
        // MasterAI-owned selective pre-touch for levels llama-server cannot
        // express as flags. Full remains --mlock, avoiding a duplicate full
        // scan immediately before the backend pins the same pages.
        if (tuning.pre_touch == PreTouchLevel::metadata ||
            tuning.pre_touch == PreTouchLevel::first_use ||
            tuning.pre_touch == PreTouchLevel::layer_window) {
            std::atomic_bool not_cancelled{false};
            static_cast<void>(pre_touch_model_file(
                model.directory / model.manifest.model_file,
                tuning.pre_touch, not_cancelled));
        }
        // Phase 26: MappingWeights -- the backend process is about to start
        // reading/mapping the model file per LaunchTuning::load_mode.
        warm_tracker_.enter(WarmModelState::MappingWeights);
        const auto log_path =
            runtime_root_ / "logs" / ("runner-" + model.manifest.id + ".log");
        process_->start(spec, log_path);
        metrics_.process_id = process_->id();
        // Phase 26: InitialisingBackend -- process is up, waiting on its own
        // readiness probe below.
        warm_tracker_.enter(WarmModelState::InitialisingBackend);
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(startup_timeout_seconds);
        bool ready = false;
        bool crashed = false;
        while (std::chrono::steady_clock::now() < deadline) {
            if (!process_->running()) {
                crashed = true;
                break;
            }
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
        if (!ready) {
            // A crash reported as the same generic "timed out" message as a
            // model that's still cold-loading was a real diagnosability gap
            // -- an operator would burn the whole startup_timeout_seconds
            // window waiting on a process that had already exited seconds
            // in. Report which one actually happened, with the exit code
            // and log path so the real cause (bad launch flags, missing
            // GPU library, OOM) is visible without guessing.
            if (crashed) {
                const auto exit_code = process_->exit_code();
                throw std::runtime_error(
                    "runner process exited before becoming ready (exit code " +
                    (exit_code.has_value() ? std::to_string(*exit_code) : "unknown") +
                    ") -- see " + log_path.string());
            }
            throw std::runtime_error(
                "runner readiness timed out after " +
                std::to_string(startup_timeout_seconds) +
                "s -- backend process is still running but never answered "
                "/health; see " + log_path.string());
        }
        // Phase 26: Warming -- backend answered /health but has not yet
        // actually served a request; Ready is only entered below, once this
        // load() call itself is about to hand the runner back as usable.
        warm_tracker_.enter(WarmModelState::Warming);
        metrics_.state = RunnerState::ready;
        metrics_.diagnostic.clear();
        warm_tracker_.enter(WarmModelState::Ready);
        warm_tracker_.record_activity(current_epoch_seconds());
    } catch (const std::exception& exception) {
        process_->stop(1U);
        metrics_.state = RunnerState::failed;
        metrics_.diagnostic = exception.what();
        warm_tracker_.enter(WarmModelState::Failed);
        throw;
    }
}

void RunnerSupervisor::unload(const std::uint32_t grace_seconds) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    if (metrics_.state == RunnerState::unloaded) return;
    metrics_.state = RunnerState::stopping;
    // Phase 26: unload() only ever observes warm_tracker_ in Ready, Busy, or
    // Failed here -- load() holds this same mutex_ for its entire body, so
    // unload() can never interleave mid-load and see one of the
    // LoadingMetadata/MappingWeights/InitialisingBackend/Warming sub-phases.
    const bool was_failed = warm_tracker_.current() == WarmModelState::Failed;
    if (!was_failed) {
        // Draining -- graceful unload requested, process still up. Legal
        // from both Ready and Busy (see warm_state_edge()).
        warm_tracker_.enter(WarmModelState::Draining);
    }
    const bool forced = process_->stop(grace_seconds);
    // Phase 26: Evicting -- the graceful stop above timed out and had to
    // force-terminate the process. Skipped when the load itself had already
    // failed (Failed -> Unloaded is its own direct legal edge; there is
    // nothing "graceful" to distinguish from "forced" for a process that
    // never became a working runner). Recorded before the final Unloaded
    // transition so a caller polling metrics() mid-unload can observe it,
    // even though by the time this function returns the state has already
    // moved on to Unloaded.
    if (forced && !was_failed) warm_tracker_.enter(WarmModelState::Evicting);
    warm_tracker_.enter(WarmModelState::Unloaded);
    metrics_ = {};
    active_generations_ = 0U;
    maximum_parallel_generations_ = 1U;
}

// Returns the configured port while the runner can accept the requested
// operation. A generation reserves one calibrated parallel slot; ordinary
// launches retain the historical exclusive limit of one.
unsigned int RunnerSupervisor::ready_port(const bool mark_busy) {
    std::lock_guard<std::mutex> lock(mutex_);
    const bool concurrent_ready =
        metrics_.state == RunnerState::busy &&
        maximum_parallel_generations_ > 1U;
    if (metrics_.state != RunnerState::ready && !concurrent_ready) {
        throw std::logic_error("runner is not ready");
    }
    if (mark_busy) {
        if (active_generations_ >= maximum_parallel_generations_) {
            throw std::logic_error("runner parallel generation limit reached");
        }
        const bool first_generation = active_generations_++ == 0U;
        metrics_.state = RunnerState::busy;
        // Phase 26: Busy is legal from both Ready and Idle (a request can
        // arrive after the idle-timeout sweep already marked the model
        // Idle), so no branching is needed here -- see warm_state_edge().
        if (first_generation) warm_tracker_.enter(WarmModelState::Busy);
        warm_tracker_.record_activity(current_epoch_seconds());
    }
    return port_;
}

bool RunnerSupervisor::apply_idle_timeout(
    const std::uint64_t now_epoch_seconds,
    const std::uint32_t idle_unload_seconds) {
    return warm_tracker_.apply_idle_timeout(now_epoch_seconds,
                                            idle_unload_seconds);
}

void RunnerSupervisor::set_tokenization_cache(
    CacheManager* cache, std::string special_token_policy) {
    std::lock_guard<std::mutex> lock(mutex_);
    tokenization_cache_ = cache;
    special_token_policy_ = std::move(special_token_policy);
}

// Tokenizes one policy-bounded prompt through the ready isolated runner.
//
// Phase 23: when a tokenization cache has been configured (see
// set_tokenization_cache()), this first checks CacheManager for an entry
// keyed by (content hash, model vocabulary fingerprint, special-token
// policy) and only falls back to the runner's own /tokenize HTTP round
// trip on a miss, storing the result afterward. Immutable prompt fragments
// (system instructions, chat wrappers, repeated file chunks/conversation
// prefixes) are exactly the callers this benefits: identical bytes tokenize
// to an identical count for a fixed model/policy, every time.
std::uint64_t RunnerSupervisor::tokenize(const std::string& text) {
    if (text.empty() || text.size() > 16U * 1024U * 1024U) {
        throw std::invalid_argument("tokenization input is outside policy");
    }
    CacheManager* cache = nullptr;
    std::string model_sha256;
    std::string special_token_policy;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cache = tokenization_cache_;
        model_sha256 = model_sha256_;
        special_token_policy = special_token_policy_;
    }
    CacheKey cache_key;
    if (cache != nullptr) {
        // Tokenization results carry no user/project-scoped confidentiality
        // beyond the content itself (a token count reveals nothing a cache
        // hit/miss timing side channel doesn't already), so this cache is
        // intentionally shared process-wide (empty user_id/project_id)
        // rather than partitioned -- what does partition it is the content
        // digest plus the model/policy version tag, so two different texts
        // or two different models/policies can never collide.
        cache_key.policy_generation = cache->current_policy_generation();
        cache_key.canonical_identity = "tokenize";
        cache_key.content_digest = sha256_hex(text);
        cache_key.version_tag =
            "tokenize-v1:" + model_sha256 + ":" + special_token_policy;
        if (const auto cached = cache->get(CacheCategory::tokenization, cache_key)) {
            try {
                return std::stoull(*cached);
            } catch (const std::exception&) {
                // A corrupt/unparsable cached value falls back to a fresh
                // runner call rather than propagating a parse error --
                // matches this codebase's "cache corruption falls back to
                // safe uncached operation" convention (see cache.cpp).
            }
        }
    }
    const auto port = ready_port(false);
    const auto result = local_http(
        port, "POST", "/tokenize",
        "{\"content\":\"" + json_escape(text) + "\"}", {});
    if (result.status != 200) {
        throw std::runtime_error("runner tokenization request failed");
    }
    const auto root = parse_json(result.body);
    const auto token_count = static_cast<std::uint64_t>(
        root.required("tokens").as_array().size());
    if (cache != nullptr) {
        cache->put(CacheCategory::tokenization, cache_key,
                  std::to_string(token_count));
    }
    return token_count;
}

// Phase 61 learned-embedding boundary. llama.cpp remains an optional,
// process-isolated adapter: MasterAI sends one bounded string over loopback,
// accepts only the documented OpenAI-compatible single-vector envelope, and
// validates every numeric component before returning it to the ML index.
EmbeddingResult RunnerSupervisor::embed(const std::string& text) {
    if (text.empty() || text.size() > 1024U * 1024U) {
        throw std::invalid_argument("embedding input is outside policy");
    }
    const auto port = ready_port(true);
    const auto started = std::chrono::steady_clock::now();
    EmbeddingResult embedded;
    try {
        const auto current = metrics();
        embedded.model_id = current.model_id;
        const auto result = local_http(
            port, "POST", "/v1/embeddings",
            "{\"input\":\"" + json_escape(text) +
                "\",\"model\":\"" + json_escape(embedded.model_id) +
                "\",\"encoding_format\":\"float\"}", {});
        if (result.status != 200) {
            std::string detail = result.body;
            if (detail.size() > 400U) detail.resize(400U);
            throw std::runtime_error(
                "runner embedding request failed (HTTP " +
                std::to_string(result.status) +
                (detail.empty() ? ")" : "): " + detail));
        }
        const auto root = parse_json(result.body);
        const auto& data = root.required("data").as_array();
        if (data.size() != 1U) {
            throw std::runtime_error(
                "runner embedding response must contain exactly one vector");
        }
        const auto& values = data.front().required("embedding").as_array();
        if (values.empty() || values.size() > 8192U) {
            throw std::runtime_error(
                "runner embedding dimensions are outside policy");
        }
        double squared = 0.0;
        embedded.values.reserve(values.size());
        for (const auto& value : values) {
            const double component = value.as_double();
            if (!std::isfinite(component)) {
                throw std::runtime_error(
                    "runner embedding contains a non-finite value");
            }
            embedded.values.push_back(component);
            squared += component * component;
        }
        if (!std::isfinite(squared) || squared <= 0.0) {
            throw std::runtime_error(
                "runner embedding has zero or invalid magnitude");
        }
        const double length = std::sqrt(squared);
        for (auto& value : embedded.values) value /= length;
        embedded.elapsed_microseconds = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now() - started).count());
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_generations_ > 0U) --active_generations_;
        metrics_.state = process_->running()
                             ? (active_generations_ == 0U ? RunnerState::ready
                                                          : RunnerState::busy)
                             : RunnerState::failed;
        if (metrics_.state == RunnerState::ready) {
            warm_tracker_.enter(WarmModelState::Ready);
        } else if (metrics_.state == RunnerState::failed) {
            warm_tracker_.enter(WarmModelState::Failed);
        }
        throw;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_generations_ > 0U) --active_generations_;
        metrics_.state = process_->running()
                             ? (active_generations_ == 0U ? RunnerState::ready
                                                          : RunnerState::busy)
                             : RunnerState::failed;
        if (metrics_.state == RunnerState::ready) {
            warm_tracker_.enter(WarmModelState::Ready);
            warm_tracker_.record_activity(current_epoch_seconds());
            ++metrics_.requests_completed;
        } else {
            warm_tracker_.enter(WarmModelState::Failed);
        }
    }
    return embedded;
}

GenerationResult RunnerSupervisor::generate(
    const std::string& prompt, const GenerationOptions& options,
    const std::function<void(const std::string&)>& on_chunk,
    const std::atomic_bool& cancellation,
    const std::uint32_t stall_timeout_seconds) {
    if (prompt.empty() || prompt.size() > 16U * 1024U * 1024U ||
        options.max_tokens == 0U || options.max_tokens > 32768U ||
        options.temperature < 0.0 || options.temperature > 2.0 ||
        options.repeat_penalty < 0.5 || options.repeat_penalty > 2.0 ||
        options.repeat_last_n > 4096U ||
        options.top_p <= 0.0 || options.top_p > 1.0 ||
        options.top_k > 1000U ||
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
            // Anti-repetition sampling (see GenerationOptions): without an
            // explicit repeat penalty some instruct models (notably the
            // Qwen2.5 family) can loop, restating earlier sentences until
            // max_tokens runs out instead of ending their turn.
            ",\"repeat_penalty\":" + std::to_string(options.repeat_penalty) +
            ",\"repeat_last_n\":" + std::to_string(options.repeat_last_n) +
            ",\"top_p\":" + std::to_string(options.top_p) +
            ",\"top_k\":" + std::to_string(options.top_k) +
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
            &cancellation, std::chrono::seconds(stall_timeout_seconds));
        if (result.status != 200 && !cancellation.load()) {
            // Surface the runner's own error body (truncated -- it can carry
            // a JSON envelope) instead of a bare status: "the request exceeds
            // the available context size" and similar llama.cpp messages are
            // exactly what the user needs to see to fix the problem, and the
            // chat route forwards this text to the browser as `detail`.
            std::string detail = result.body;
            if (detail.size() > 400U) detail.resize(400U);
            throw std::runtime_error(
                "runner generation request failed (HTTP " +
                std::to_string(result.status) +
                (detail.empty() ? ")" : "): " + detail));
        }
        generated.cancelled = cancellation.load();
    } catch (...) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_generations_ > 0U) --active_generations_;
        metrics_.state = process_->running()
                             ? (active_generations_ == 0U ? RunnerState::ready
                                                          : RunnerState::busy)
                             : RunnerState::failed;
        // Phase 26: Busy -> Ready and Busy -> Failed are both legal edges
        // (see warm_state_edge()) -- ready_port(true) above already moved
        // the tracker to Busy before this request started.
        if (metrics_.state == RunnerState::ready) {
            warm_tracker_.enter(WarmModelState::Ready);
        } else if (metrics_.state == RunnerState::failed) {
            warm_tracker_.enter(WarmModelState::Failed);
        }
        throw;
    }
    generated.elapsed_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (active_generations_ > 0U) --active_generations_;
        metrics_.state = process_->running()
                             ? (active_generations_ == 0U ? RunnerState::ready
                                                          : RunnerState::busy)
                             : RunnerState::failed;
        if (metrics_.state == RunnerState::ready) {
            warm_tracker_.enter(WarmModelState::Ready);
        } else if (metrics_.state == RunnerState::failed) {
            warm_tracker_.enter(WarmModelState::Failed);
        }
        if (metrics_.state == RunnerState::ready) {
            warm_tracker_.record_activity(current_epoch_seconds());
        }
        if (generated.cancelled) ++metrics_.requests_cancelled;
        else ++metrics_.requests_completed;
    }
    return generated;
}

RunnerMetrics RunnerSupervisor::metrics() const {
    // load() holds mutex_ for its *entire* body, including the readiness-poll
    // loop that can run for the whole startup_timeout_seconds window on a
    // cold multi-gigabyte model. metrics() backs GET /api/v1/runner/status,
    // which the web UI polls continuously to show warm-up progress, and
    // ensure_model_loaded() calls it on every chat send before deciding
    // whether to (re)load -- a blocking lock() here therefore froze status
    // polling and chat for the whole warm-up, not just the load() caller.
    // try_to_lock lets metrics() return immediately instead: when the lock
    // is free it reports the exact same snapshot as before; when a load()
    // (or unload()/generate()) is in flight it falls back to warm_tracker_
    // alone, which keeps its own independent mutex (see WarmModelTracker in
    // masterai.hpp) and is therefore always safe and fast to read here.
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (lock.owns_lock()) {
        auto result = metrics_;
        result.resident_memory_bytes = process_->resident_memory();
        result.warm_state = warm_tracker_.current();
        result.last_activity_epoch_seconds = warm_tracker_.last_activity_epoch_seconds();
        return result;
    }
    RunnerMetrics result;
    result.state = RunnerState::starting;
    result.warm_state = warm_tracker_.current();
    result.last_activity_epoch_seconds = warm_tracker_.last_activity_epoch_seconds();
    return result;
}

}  // namespace masterai
