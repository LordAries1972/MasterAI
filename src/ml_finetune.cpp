// Phase 73: real LLM LoRA fine-tuning support. This file does not implement
// transformer backpropagation itself -- it converts a dataset into the text
// format llama.cpp's finetune tooling expects and supervises that tooling as
// an external process (administrator-vendored, same manual-placement
// convention as llama-server.exe -- see README.md), the same isolation
// approach inference.cpp already uses for the inference runner and
// downloads.cpp already uses for curl. Nothing in here fabricates a result:
// a missing executable, a missing base-model file, or a non-zero exit from
// either tool is a real, surfaced error, never a silent fallback.
#include "masterai.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

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

// One CSV line -> fields, honoring double-quoted fields with "" escapes.
// Deliberately line-scoped, matching ml_engine.cpp's split_csv_line -- a
// multi-line quoted training example is not supported.
std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1U < line.size() && line[index + 1U] == '"') {
                    field += '"';
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                field += character;
            }
        } else if (character == '"' && field.empty()) {
            quoted = true;
        } else if (character == ',') {
            fields.push_back(field);
            field.clear();
        } else {
            field += character;
        }
    }
    fields.push_back(field);
    return fields;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0U;
    std::size_t end = value.size();
    while (begin < end &&
          (value[begin] == ' ' || value[begin] == '\t' || value[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (value[end - 1U] == ' ' || value[end - 1U] == '\t' ||
                           value[end - 1U] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

std::size_t find_column(const std::vector<std::string>& header,
                        const std::vector<std::string>& candidate_names) {
    for (std::size_t column = 0; column < header.size(); ++column) {
        std::string lower = header[column];
        for (char& character : lower) {
            character = static_cast<char>(
                std::tolower(static_cast<unsigned char>(character)));
        }
        for (const auto& candidate : candidate_names) {
            if (lower == candidate) return column;
        }
    }
    return header.size();
}

#if defined(_WIN32)
std::wstring widen(const std::string& value) {
    if (value.empty()) return {};
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                         value.data(), static_cast<int>(value.size()),
                                         nullptr, 0);
    if (size <= 0) throw std::runtime_error("llama.cpp tool path is not valid UTF-8");
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

// Runs one external tool synchronously (the calling thread is always
// server.cpp's detached background thread for an LLM fine-tuning run, never
// the HTTP request thread -- see run_llm_fine_tuning_job), capturing its
// combined stdout+stderr to log_path and returning its exit code. Mirrors
// downloads.cpp's run_curl, which this is deliberately kept parallel to
// rather than sharing, since the two files supervise different families of
// external tool with different lifecycle needs.
int run_llama_tool(const std::filesystem::path& executable,
                   const std::vector<std::string>& arguments,
                   const std::filesystem::path& log_path,
                   const std::atomic_bool& cancellation) {
    std::filesystem::create_directories(log_path.parent_path());
#if defined(_WIN32)
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);
    attributes.bInheritHandle = TRUE;
    HANDLE log = CreateFileW(widen(log_path.string()).c_str(), FILE_APPEND_DATA,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, &attributes,
                             OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (log == INVALID_HANDLE_VALUE) {
        throw std::runtime_error("llama.cpp tool log could not be opened");
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
    if (!created) throw std::runtime_error("llama.cpp tool launch failed");
    CloseHandle(process.hThread);
    while (WaitForSingleObject(process.hProcess, 200U) == WAIT_TIMEOUT) {
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
    if (log < 0) throw std::runtime_error("llama.cpp tool log could not be opened");
    const pid_t child = fork();
    if (child < 0) {
        close(log);
        throw std::runtime_error("llama.cpp tool fork failed");
    }
    if (child == 0) {
        if (dup2(log, STDOUT_FILENO) < 0 || dup2(log, STDERR_FILENO) < 0) _exit(126);
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
        if (cancellation.load()) kill(child, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 1;
#endif
}

// Last few KiB of a log file, for surfacing an external tool's own error
// output rather than a generic "failed" message.
std::string log_tail(const std::filesystem::path& log_path, const std::size_t max_bytes = 4096U) {
    std::ifstream stream(log_path, std::ios::binary);
    if (!stream) return {};
    stream.seekg(0, std::ios::end);
    const auto size = static_cast<std::size_t>(stream.tellg());
    const auto start = size > max_bytes ? size - max_bytes : 0U;
    stream.seekg(static_cast<std::streamoff>(start));
    std::ostringstream buffer;
    buffer << stream.rdbuf();
    return buffer.str();
}

}  // namespace

std::filesystem::path write_llm_finetune_training_text(
    const std::string& csv, const std::filesystem::path& output_path) {
    std::vector<std::string> lines;
    std::size_t position = 0U;
    while (position <= csv.size()) {
        const auto newline = csv.find('\n', position);
        const auto end = newline == std::string::npos ? csv.size() : newline;
        std::string line = csv.substr(position, end - position);
        if (!trim(line).empty()) lines.push_back(std::move(line));
        if (newline == std::string::npos) break;
        position = newline + 1U;
    }
    if (lines.size() < 2U) {
        throw std::runtime_error(
            "dataset needs a header row and at least one data row");
    }
    auto header = split_csv_line(lines[0]);
    for (auto& name : header) name = trim(name);
    const auto instruction_column =
        find_column(header, {"instruction", "prompt"});
    if (instruction_column == header.size()) {
        throw std::runtime_error(
            "dataset has no \"instruction\" or \"prompt\" column; LLM "
            "fine-tuning needs a column naming what to ask the model");
    }
    const auto response_column =
        find_column(header, {"response", "output", "completion"});
    if (response_column == header.size()) {
        throw std::runtime_error(
            "dataset has no \"response\", \"output\", or \"completion\" "
            "column; LLM fine-tuning needs a column naming the answer to "
            "train toward");
    }
    std::filesystem::create_directories(output_path.parent_path());
    std::ofstream out(output_path, std::ios::binary | std::ios::trunc);
    if (!out) {
        throw std::runtime_error("could not create fine-tuning training text file");
    }
    std::size_t written_rows = 0U;
    for (std::size_t row = 1; row < lines.size(); ++row) {
        const auto cells = split_csv_line(lines[row]);
        if (cells.size() != header.size()) continue;
        const auto instruction = trim(cells[instruction_column]);
        const auto response = trim(cells[response_column]);
        if (instruction.empty() || response.empty()) continue;
        out << "### Instruction:\n" << instruction << "\n### Response:\n"
            << response << "\n\n";
        ++written_rows;
    }
    if (written_rows == 0U) {
        throw std::runtime_error(
            "no row had both an instruction and a response after trimming");
    }
    return output_path;
}

LlmFineTuneResult run_llama_lora_finetune(
    const AppConfig& configuration, const std::filesystem::path& base_model_gguf,
    const std::filesystem::path& training_text_path,
    const std::filesystem::path& output_directory,
    const LlmFineTuneOptions& options, const std::atomic_bool& cancellation) {
    if (configuration.llama_finetune_executable.empty() ||
        !std::filesystem::is_regular_file(configuration.llama_finetune_executable)) {
        throw std::runtime_error(
            "llama-finetune is not configured (set llama_finetune_executable "
            "to a real llama.cpp finetune tool -- see README.md)");
    }
    if (configuration.llama_export_lora_executable.empty() ||
        !std::filesystem::is_regular_file(
            configuration.llama_export_lora_executable)) {
        throw std::runtime_error(
            "llama-export-lora is not configured (set "
            "llama_export_lora_executable to a real llama.cpp export-lora "
            "tool -- see README.md)");
    }
    if (!std::filesystem::is_regular_file(base_model_gguf)) {
        throw std::runtime_error("base model GGUF file does not exist: " +
                                 base_model_gguf.string());
    }
    std::filesystem::create_directories(output_directory);
    LlmFineTuneResult result;
    result.adapter_gguf = output_directory / "adapter.gguf";
    result.merged_gguf = output_directory / "merged.gguf";
    const auto finetune_log = output_directory / "finetune.log";
    const auto export_lora_log = output_directory / "export-lora.log";

    // Default flags match llama.cpp's historical `finetune` example CLI
    // (--model-base, --train-data, --lora-out, --epochs, --adam-alpha).
    // That interface has changed across llama.cpp releases, so
    // extra_finetune_arguments lets an administrator override/append
    // whatever their specific vendored build actually expects rather than
    // this codebase guessing wrong and silently producing nothing useful.
    std::vector<std::string> finetune_arguments{
        "--model-base", base_model_gguf.string(), "--train-data",
        training_text_path.string(), "--lora-out", result.adapter_gguf.string(),
        "--epochs", std::to_string(options.epochs), "--adam-alpha",
        std::to_string(options.learning_rate)};
    if (!options.extra_finetune_arguments.empty()) {
        std::istringstream stream(options.extra_finetune_arguments);
        std::string token;
        while (stream >> token) finetune_arguments.push_back(token);
    }
    const auto finetune_exit_code =
        run_llama_tool(configuration.llama_finetune_executable, finetune_arguments,
                       finetune_log, cancellation);
    result.finetune_log_tail = log_tail(finetune_log);
    if (finetune_exit_code != 0 ||
        !std::filesystem::is_regular_file(result.adapter_gguf)) {
        throw std::runtime_error(
            "llama-finetune exited with code " +
            std::to_string(finetune_exit_code) + "; log tail: " +
            result.finetune_log_tail);
    }
    if (cancellation.load()) {
        throw std::runtime_error("LLM fine-tuning run was canceled");
    }

    // Default flags match llama.cpp's historical `export-lora` example CLI
    // (--model-base, --lora, --model-out); same version caveat as above.
    std::vector<std::string> export_lora_arguments{
        "--model-base", base_model_gguf.string(), "--lora",
        result.adapter_gguf.string(), "--model-out", result.merged_gguf.string()};
    if (!options.extra_export_lora_arguments.empty()) {
        std::istringstream stream(options.extra_export_lora_arguments);
        std::string token;
        while (stream >> token) export_lora_arguments.push_back(token);
    }
    const auto export_lora_exit_code = run_llama_tool(
        configuration.llama_export_lora_executable, export_lora_arguments,
        export_lora_log, cancellation);
    result.export_lora_log_tail = log_tail(export_lora_log);
    if (export_lora_exit_code != 0 ||
        !std::filesystem::is_regular_file(result.merged_gguf)) {
        throw std::runtime_error(
            "llama-export-lora exited with code " +
            std::to_string(export_lora_exit_code) + "; log tail: " +
            result.export_lora_log_tail);
    }
    return result;
}

FineTuningRunResultStore::FineTuningRunResultStore(RecordStore& records)
    : records_(&records) {}

void FineTuningRunResultStore::put(const std::string& job_id,
                                   const std::string& result_json) {
    records_->put("ml_llm_finetune_results", job_id, result_json);
}

std::optional<std::string> FineTuningRunResultStore::find(
    const std::string& job_id) const {
    return records_->get("ml_llm_finetune_results", job_id);
}

bool FineTuningRunResultStore::remove(const std::string& job_id) {
    if (!records_->get("ml_llm_finetune_results", job_id)) return false;
    records_->erase("ml_llm_finetune_results", job_id);
    return true;
}

}  // namespace masterai
