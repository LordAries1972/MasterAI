// Implementation for run_curl_process() -- see curl_process.hpp. Moved out
// of downloads.cpp verbatim (Phase 103) so research.cpp can reuse the exact
// same subprocess launch/wait/cancel machinery instead of duplicating it.
#include "curl_process.hpp"

#include <chrono>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__) || defined(__APPLE__)
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows, Linux, and macOS (Apple Silicon)."
#endif

namespace masterai {
namespace {

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) throw std::runtime_error("curl argument is not valid UTF-8");
    std::wstring result(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                        static_cast<int>(value.size()), result.data(), size);
    return result;
}

std::wstring quote(const std::string& argument) {
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

}  // namespace

int run_curl_process(const std::filesystem::path& executable,
                     const std::vector<std::string>& arguments,
                     const std::filesystem::path& log_path,
                     const std::atomic_bool& cancellation,
                     const std::function<void()>& progress) {
    std::filesystem::create_directories(log_path.parent_path());
#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(widen(log_path.string()).c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("curl process log could not be opened");
    }
    std::wstring command = quote(executable.string());
    for (const auto& argument : arguments) command += L" " + quote(argument);
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdOutput = log;
    startup.hStdError = log;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION process{};
    const auto application = widen(executable.string());
    const BOOL created = CreateProcessW(
        application.c_str(), mutable_command.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW, nullptr, nullptr, &startup, &process);
    CloseHandle(log);
    if (!created) throw std::runtime_error("curl process launch failed");
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 100U) == WAIT_TIMEOUT) {
        if (progress) progress();
        if (cancellation.load()) {
            TerminateProcess(process.hProcess, 2U);
            break;
        }
    }
    DWORD code = 1U;
    GetExitCodeProcess(process.hProcess, &code);
    CloseHandle(process.hProcess);
    return static_cast<int>(code);
#else
    const int log = open(log_path.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
    if (log < 0) throw std::runtime_error("curl process log could not be opened");
    const pid_t child = fork();
    if (child < 0) {
        close(log);
        throw std::runtime_error("curl process fork failed");
    }
    if (child == 0) {
        if (dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) {
            _exit(126);
        }
        close(log);
        std::vector<std::string> values{executable.string()};
        values.insert(values.end(), arguments.begin(), arguments.end());
        std::vector<char*> pointers;
        for (auto& value : values) pointers.push_back(value.data());
        pointers.push_back(nullptr);
        execv(executable.c_str(), pointers.data());
        _exit(127);
    }
    close(log);
    int status = 0;
    while (waitpid(child, &status, WNOHANG) == 0) {
        if (progress) progress();
        if (cancellation.load()) {
            kill(child, SIGTERM);
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            if (waitpid(child, &status, WNOHANG) == 0) kill(child, SIGKILL);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

}  // namespace masterai
