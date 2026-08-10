// Converts Parquet bytes to JSON text by spawning the operator-configured
// DuckDB CLI (rule 15's second process-isolated exception, alongside
// llama.cpp). Never vendors or links DuckDB into masterai_core; the helper
// executable is distributed separately and configured by filesystem path in
// settings.json, exactly like llama-server/curl.
#include "masterai.hpp"

#include <array>
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#error "unsupported platform"
#endif

namespace masterai {
namespace {

constexpr std::uint32_t parquet_bridge_timeout_seconds = 60U;
constexpr std::uint64_t parquet_bridge_maximum_output_bytes = 64ULL * 1024ULL * 1024ULL;

std::filesystem::path make_temp_parquet_path() {
    std::random_device seed;
    std::mt19937_64 generator(seed());
    std::uniform_int_distribution<std::uint64_t> distribution;
    return std::filesystem::temp_directory_path() /
           ("masterai-parquet-" + std::to_string(distribution(generator)) + ".parquet");
}

std::string escape_sql_literal(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    for (const char ch : text) {
        if (ch == '\'') result += '\'';
        result += ch;
    }
    return result;
}

#if defined(_WIN32)
std::string quote_windows_argument(const std::string& argument) {
    std::string result = "\"";
    std::size_t backslashes = 0U;
    for (const char ch : argument) {
        if (ch == '\\') {
            ++backslashes;
            continue;
        }
        if (ch == '"') {
            result.append(backslashes * 2U + 1U, '\\');
            backslashes = 0U;
            result += '"';
            continue;
        }
        result.append(backslashes, '\\');
        backslashes = 0U;
        result += ch;
    }
    result.append(backslashes * 2U, '\\');
    result += '"';
    return result;
}

std::string run_and_capture(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            std::uint32_t timeout_seconds,
                            std::uint64_t maximum_output_bytes) {
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE read_handle = nullptr;
    HANDLE write_handle = nullptr;
    if (!CreatePipe(&read_handle, &write_handle, &security, 0)) {
        throw std::runtime_error("failed to create pipe for DuckDB helper process");
    }
    if (!SetHandleInformation(read_handle, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(read_handle);
        CloseHandle(write_handle);
        throw std::runtime_error("failed to configure pipe for DuckDB helper process");
    }

    std::string command_line = quote_windows_argument(executable.string());
    for (const auto& argument : arguments) {
        command_line += ' ';
        command_line += quote_windows_argument(argument);
    }

    STARTUPINFOA startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = write_handle;
    startup.hStdError = write_handle;

    PROCESS_INFORMATION process_info{};
    const BOOL created = CreateProcessA(
        nullptr, command_line.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process_info);
    CloseHandle(write_handle);
    if (!created) {
        CloseHandle(read_handle);
        throw std::runtime_error("failed to launch DuckDB helper process");
    }

    std::string output;
    std::array<char, 8192> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool timed_out = false;
    bool overflowed = false;
    for (;;) {
        DWORD available = 0U;
        if (PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available, nullptr) && available > 0U) {
            DWORD read_bytes = 0U;
            const DWORD to_read = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available));
            if (ReadFile(read_handle, buffer.data(), to_read, &read_bytes, nullptr) && read_bytes > 0U) {
                if (output.size() + read_bytes > maximum_output_bytes) {
                    overflowed = true;
                    TerminateProcess(process_info.hProcess, 1U);
                    break;
                }
                output.append(buffer.data(), read_bytes);
                continue;
            }
        }
        if (WaitForSingleObject(process_info.hProcess, 20U) == WAIT_OBJECT_0) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            TerminateProcess(process_info.hProcess, 1U);
            break;
        }
    }
    WaitForSingleObject(process_info.hProcess, 2000U);

    // The process can exit right after its final write; drain whatever is
    // still sitting in the pipe buffer before the handle is closed below.
    if (!overflowed) {
        for (;;) {
            DWORD available = 0U;
            if (!PeekNamedPipe(read_handle, nullptr, 0, nullptr, &available, nullptr) ||
                available == 0U) {
                break;
            }
            DWORD read_bytes = 0U;
            const DWORD to_read = static_cast<DWORD>(std::min<std::size_t>(buffer.size(), available));
            if (!ReadFile(read_handle, buffer.data(), to_read, &read_bytes, nullptr) ||
                read_bytes == 0U) {
                break;
            }
            if (output.size() + read_bytes > maximum_output_bytes) {
                overflowed = true;
                break;
            }
            output.append(buffer.data(), read_bytes);
        }
    }

    DWORD exit_code = 1U;
    GetExitCodeProcess(process_info.hProcess, &exit_code);
    CloseHandle(read_handle);
    CloseHandle(process_info.hProcess);
    CloseHandle(process_info.hThread);

    if (overflowed) throw std::runtime_error("DuckDB helper output exceeded its bound");
    if (timed_out) throw std::runtime_error("DuckDB helper process timed out");
    if (exit_code != 0U) throw std::runtime_error("DuckDB helper process failed: " + output);
    return output;
}
#elif defined(__linux__)
std::string run_and_capture(const std::filesystem::path& executable,
                            const std::vector<std::string>& arguments,
                            std::uint32_t timeout_seconds,
                            std::uint64_t maximum_output_bytes) {
    int pipe_fds[2];
    if (pipe(pipe_fds) != 0) {
        throw std::runtime_error("failed to create pipe for DuckDB helper process");
    }

    std::vector<std::string> argument_storage{executable.string()};
    argument_storage.insert(argument_storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    argv.reserve(argument_storage.size() + 1U);
    for (auto& argument : argument_storage) argv.push_back(argument.data());
    argv.push_back(nullptr);

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        throw std::runtime_error("failed to fork DuckDB helper process");
    }
    if (pid == 0) {
        dup2(pipe_fds[1], STDOUT_FILENO);
        dup2(pipe_fds[1], STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }
    close(pipe_fds[1]);
    fcntl(pipe_fds[0], F_SETFL, O_NONBLOCK);

    std::string output;
    std::array<char, 8192> buffer{};
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool timed_out = false;
    bool overflowed = false;
    int status = 0;
    for (;;) {
        const ssize_t read_bytes = read(pipe_fds[0], buffer.data(), buffer.size());
        if (read_bytes > 0) {
            if (output.size() + static_cast<std::size_t>(read_bytes) > maximum_output_bytes) {
                overflowed = true;
                kill(pid, SIGKILL);
                waitpid(pid, &status, 0);
                break;
            }
            output.append(buffer.data(), static_cast<std::size_t>(read_bytes));
            continue;
        }
        const pid_t finished = waitpid(pid, &status, WNOHANG);
        if (finished == pid) break;
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    close(pipe_fds[0]);

    if (overflowed) throw std::runtime_error("DuckDB helper output exceeded its bound");
    if (timed_out) throw std::runtime_error("DuckDB helper process timed out");
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        throw std::runtime_error("DuckDB helper process failed: " + output);
    }
    return output;
}
#endif

}  // namespace

std::string parquet_bytes_to_json(const std::filesystem::path& helper_executable,
                                  const std::string& parquet_bytes) {
    if (helper_executable.empty()) {
        throw std::runtime_error("no DuckDB helper executable is configured");
    }
    if (!std::filesystem::is_regular_file(helper_executable) ||
        std::filesystem::is_symlink(helper_executable)) {
        throw std::runtime_error("configured DuckDB helper executable is not an approved regular file");
    }

    const auto temp_path = make_temp_parquet_path();
    {
        std::ofstream output(temp_path, std::ios::binary | std::ios::trunc);
        output.write(parquet_bytes.data(), static_cast<std::streamsize>(parquet_bytes.size()));
        if (!output) {
            throw std::runtime_error("failed to stage Parquet bytes for DuckDB conversion");
        }
    }

    struct TempFileGuard {
        std::filesystem::path path;
        ~TempFileGuard() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } guard{temp_path};

    const std::string query =
        "SELECT * FROM read_parquet('" + escape_sql_literal(temp_path.string()) + "')";
    const std::vector<std::string> arguments{"-json", "-c", query};

    return run_and_capture(helper_executable, arguments, parquet_bridge_timeout_seconds,
                           parquet_bridge_maximum_output_bytes);
}

}  // namespace masterai
