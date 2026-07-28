#include "masterai.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <csignal>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

std::string pack(const std::vector<std::string>& fields) {
    std::string result;
    for (const auto& field : fields) {
        result += std::to_string(field.size()) + ":" + field;
    }
    return result;
}

std::vector<std::string> unpack(const std::string& value) {
    std::vector<std::string> fields;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto colon = value.find(':', position);
        if (colon == std::string::npos || colon == position) {
            throw std::runtime_error("persisted download record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted download record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string state_name(const DownloadState state) {
    switch (state) {
        case DownloadState::queued: return "queued";
        case DownloadState::transferring: return "transferring";
        case DownloadState::paused: return "paused";
        case DownloadState::verifying: return "verifying";
        case DownloadState::complete: return "complete";
        case DownloadState::failed: return "failed";
        case DownloadState::quarantined: return "quarantined";
        case DownloadState::cancelled: return "cancelled";
    }
    throw std::runtime_error("download state is invalid");
}

DownloadState parse_state(const std::string& value) {
    if (value == "queued") return DownloadState::queued;
    if (value == "transferring" || value == "paused") return DownloadState::paused;
    if (value == "verifying") return DownloadState::paused;
    if (value == "complete") return DownloadState::complete;
    if (value == "failed") return DownloadState::failed;
    if (value == "quarantined") return DownloadState::quarantined;
    if (value == "cancelled") return DownloadState::cancelled;
    throw std::runtime_error("persisted download state is invalid");
}

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(),
                                         static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) throw std::runtime_error("download path is not valid UTF-8");
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

int run_curl(const std::filesystem::path& executable,
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
        throw std::runtime_error("download log could not be opened");
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
    if (!created) throw std::runtime_error("download transport launch failed");
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
    if (log < 0) throw std::runtime_error("download log could not be opened");
    const pid_t child = fork();
    if (child < 0) {
        close(log);
        throw std::runtime_error("download transport fork failed");
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

}  // namespace

DownloadManager::DownloadManager(std::filesystem::path approved_curl,
                                 std::filesystem::path download_root,
                                 RecordStore& records)
    : approved_curl_(std::move(approved_curl)),
      download_root_(std::move(download_root)), records_(records) {
    if (!std::filesystem::is_regular_file(approved_curl_) ||
        std::filesystem::is_symlink(approved_curl_) || download_root_.empty()) {
        throw std::invalid_argument(
            "download transport must be an approved regular file");
    }
    std::filesystem::create_directories(download_root_);
    restore();
}

DownloadJob DownloadManager::create(const DownloadRequest& request) {
    DownloadJob job(request);
    if (!is_path_within(download_root_, request.destination) ||
        request.source_url.find(request.immutable_revision) == std::string::npos) {
        throw std::invalid_argument(
            "download destination or immutable source revision violates policy");
    }
    if (jobs_.size() >= 1000U) {
        throw std::runtime_error("download job capacity reached");
    }
    persist(job);
    jobs_.emplace(job.id(), job);
    return job;
}

DownloadJob DownloadManager::run(const std::string& id,
                                 const std::atomic_bool& cancellation) {
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) throw std::invalid_argument("download job not found");
    auto& job = found->second;
    const auto destination = job.request().destination;
    const auto partial = std::filesystem::path(destination.string() + ".part");
    std::filesystem::create_directories(destination.parent_path());
    const std::uint64_t existing =
        std::filesystem::is_regular_file(partial)
            ? static_cast<std::uint64_t>(std::filesystem::file_size(partial))
            : 0U;
    job.begin(existing);
    persist(job);
    try {
        const std::vector<std::string> arguments{
            "--proto", "=https", "--tlsv1.2", "--fail", "--location",
            "--silent", "--show-error", "--continue-at", "-", "--output",
            partial.string(), job.request().source_url};
        const int exit_code = run_curl(
            approved_curl_, arguments,
            download_root_ / "logs" / ("download-" + id + ".log"),
            cancellation, [&]() {
                std::error_code error;
                const auto size = std::filesystem::file_size(partial, error);
                if (!error && size > job.completed_bytes()) {
                    job.record_progress(static_cast<std::uint64_t>(size));
                    persist(job);
                }
            });
        if (cancellation.load()) {
            job.cancel();
            persist(job);
            return job;
        }
        if (exit_code != 0 || !std::filesystem::is_regular_file(partial)) {
            job.fail("download transport failed with exit code " +
                     std::to_string(exit_code));
            persist(job);
            return job;
        }
        job.record_progress(
            static_cast<std::uint64_t>(std::filesystem::file_size(partial)));
        job.begin_verification();
        persist(job);
        const auto actual = sha256_file_hex(partial);
        job.complete(actual);
        if (job.state() == DownloadState::complete) {
            if (std::filesystem::exists(destination)) {
                throw std::runtime_error("verified destination already exists");
            }
            std::filesystem::rename(partial, destination);
        } else {
            const auto quarantine =
                std::filesystem::path(partial.string() + ".quarantine." + id);
            std::filesystem::rename(partial, quarantine);
        }
    } catch (const std::exception& exception) {
        if (job.state() != DownloadState::complete &&
            job.state() != DownloadState::cancelled &&
            job.state() != DownloadState::quarantined) {
            job.fail(exception.what());
        }
    }
    persist(job);
    return job;
}

std::optional<DownloadJob> DownloadManager::find(const std::string& id) const {
    const auto found = jobs_.find(id);
    return found == jobs_.end() ? std::nullopt
                                : std::optional<DownloadJob>(found->second);
}

std::vector<DownloadJob> DownloadManager::list() const {
    std::vector<DownloadJob> result;
    result.reserve(jobs_.size());
    for (const auto& item : jobs_) result.push_back(item.second);
    return result;
}

void DownloadManager::cancel(const std::string& id) {
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) throw std::invalid_argument("download job not found");
    found->second.cancel();
    persist(found->second);
}

void DownloadManager::persist(const DownloadJob& job) {
    const auto& request = job.request();
    records_.put(
        "downloads", job.id(),
        pack({request.source_url, request.immutable_revision,
              request.expected_sha256, request.destination.string(),
              request.license_accepted ? "1" : "0", state_name(job.state()),
              std::to_string(job.completed_bytes()), job.diagnostic(),
              std::to_string(request.minimum_ram_mib),
              std::to_string(request.recommended_ram_mib)}));
}

void DownloadManager::restore() {
    for (const auto& item : records_.list("downloads")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 10U) {
            throw std::runtime_error("persisted download job is invalid");
        }
        DownloadRequest request{
            fields[0], fields[1], fields[2], fields[3], fields[4] == "1",
            std::stoull(fields[8]), std::stoull(fields[9])};
        if (!is_path_within(download_root_, request.destination)) {
            throw std::runtime_error(
                "persisted download destination violates path policy");
        }
        jobs_.emplace(
            item.first,
            DownloadJob(item.first, request, parse_state(fields[5]),
                        std::stoull(fields[6]), fields[7]));
    }
}

}  // namespace masterai
