// MasterAI agentic chat tool support (docs/PLAN.md Phase 84).
//
// This unit owns three independent pieces the chat tool loop (server.cpp)
// and the MCP inbound tool dispatch (mcp.cpp) both depend on:
//   - AllowedCommandStore: the durable admin allow-list of executables a
//     `run_command` tool call may invoke, persisted the same way
//     McpOutboundRegistry persists McpOutboundServer records
//     (mcp_outbound_service.cpp) -- a RecordStore-backed map with its own
//     length-prefixed pack/unpack, deliberately not sharing
//     mcp_outbound_internal.hpp, which its own header comment scopes to
//     "the durable registry/service unit and the platform transport unit"
//     of outbound MCP specifically.
//   - run_sandboxed_process(): one argv-array external process, contained
//     the same way invoke_stdio() (mcp_outbound.cpp) already contains an
//     outbound MCP stdio server -- a Windows Job Object capping memory and
//     process count, or Linux rlimit+no-new-privileges -- bounded by a
//     wall-clock timeout, a combined output byte cap, and a cancellation
//     flag polled during the wait loop. Never invokes a shell: the
//     executable and every argument are passed as a literal argv array, so
//     no argument can be interpreted as shell syntax.
//   - classify_tool_call_risk(): the fixed destructive-pattern table that
//     decides whether a tool call may run immediately or must pause for an
//     explicit human Approve/Deny click, independent of (and never
//     weakened by) the admin allow-list above.
//   - PendingToolApprovalStore: the durable record of a tool call currently
//     paused on that human decision.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <thread>

#if defined(_WIN32)
#include <windows.h>
#else
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#endif

namespace masterai {
namespace {

// Same length-prefixed record shape mcp_outbound_service.cpp uses for its
// own registry, kept as a private copy here rather than reused across the
// module boundary (see this file's header comment).
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
            throw std::runtime_error("tool registry record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("tool registry record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string pack_set(const std::set<std::string>& values) {
    return pack(std::vector<std::string>(values.begin(), values.end()));
}

std::set<std::string> unpack_set(const std::string& value) {
    std::set<std::string> result(unpack(value).begin(), unpack(value).end());
    return result;
}

// CommandOs <-> single-character persistence tag, kept distinct from the
// HTTP layer's "windows"/"linux"/"both" JSON strings (server.cpp) so this
// file's on-disk record shape never has to change if the wire format does.
std::string os_to_tag(CommandOs os) {
    switch (os) {
        case CommandOs::windows: return "w";
        case CommandOs::linux: return "l";
        case CommandOs::both: default: return "b";
    }
}

CommandOs os_from_tag(const std::string& tag) {
    if (tag == "w") return CommandOs::windows;
    if (tag == "l") return CommandOs::linux;
    return CommandOs::both;
}

// Matches the random_id() pattern every other durable-record unit in this
// codebase defines locally for itself (see e.g. server.cpp's
// generate_tool_approval_id()) rather than sharing one across module
// boundaries.
std::string generate_allowed_command_id() {
    const auto random = secure_random(16U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string id(random.size() * 2U, '0');
    for (std::size_t index = 0U; index < random.size(); ++index) {
        id[index * 2U] = digits[random[index] >> 4U];
        id[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    return id;
}

// Case-insensitive ASCII lowering, used only to compare executable names on
// Windows (where "Git.exe" and "git.exe" name the same allow-list entry).
std::string to_lower_ascii(const std::string& value) {
    std::string result = value;
    std::transform(result.begin(), result.end(), result.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

}  // namespace

// ---------------------------------------------------------------------------
// AllowedCommandStore
// ---------------------------------------------------------------------------

AllowedCommandStore::AllowedCommandStore(RecordStore& records)
    : records_(records) {
    restore();
    ensure_default_catalog();
}

void AllowedCommandStore::restore() {
    for (const auto& item : records_.list("chat_tool_allowed_commands")) {
        try {
            const auto fields = unpack(item.second);
            // 5 fields is the pre-OS-column record shape written before
            // this admin allow-list gained an "os" field; treat missing os
            // as "both" rather than rejecting every record written by an
            // older build.
            if ((fields.size() != 5U && fields.size() != 6U) ||
                item.first.empty()) {
                continue;
            }
            AllowedCommandRecord command;
            command.id = item.first;
            command.executable = fields[0];
            command.description = fields[1];
            command.risk_default =
                fields[2] == "1" ? ChatToolRisk::high_risk : ChatToolRisk::safe;
            command.allowed_project_ids = unpack_set(fields[3]);
            command.enabled = fields[4] == "1";
            command.os = fields.size() == 6U ? os_from_tag(fields[5])
                                              : CommandOs::both;
            commands_.emplace(command.id, std::move(command));
        } catch (const std::exception&) {
            // A corrupt allow-list entry fails closed (simply omitted)
            // rather than blocking every other entry or startup itself.
        }
    }
}

void AllowedCommandStore::persist(const AllowedCommandRecord& command) {
    records_.put(
        "chat_tool_allowed_commands", command.id,
        pack({command.executable, command.description,
              command.risk_default == ChatToolRisk::high_risk ? "1" : "0",
              pack_set(command.allowed_project_ids),
              command.enabled ? "1" : "0", os_to_tag(command.os)}));
}

AllowedCommandRecord AllowedCommandStore::register_command(
    AllowedCommandRecord command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command.executable.empty()) {
        throw std::invalid_argument("allowed-command executable is required");
    }
    if (command.id.empty()) {
        throw std::invalid_argument("allowed-command id is required");
    }
    commands_[command.id] = command;
    persist(command);
    return command;
}

AllowedCommandRecord AllowedCommandStore::update_command(
    AllowedCommandRecord command) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (command.executable.empty()) {
        throw std::invalid_argument("allowed-command executable is required");
    }
    if (commands_.find(command.id) == commands_.end()) {
        throw std::invalid_argument("allowed command does not exist");
    }
    commands_[command.id] = command;
    persist(command);
    return command;
}

namespace {

// One entry in the built-in catalog seeded by ensure_default_catalog():
// executable, human-readable description, which OS(es) it applies to, and
// its baseline risk classification. Every seeded entry starts enabled, so
// run_command works against the whole catalog immediately after install;
// an admin reviews the list from the allowed-commands admin page
// (server.cpp/web_ui.cpp) and disables (via Edit) anything not warranted
// for this install rather than opting each one in individually.
struct CatalogEntry {
    const char* executable;
    const char* description;
    CommandOs os;
    ChatToolRisk risk;
};

constexpr CatalogEntry kDefaultCommandCatalog[] = {
    // --- Windows shells and core cmd.exe/PowerShell utilities ---
    {"cmd.exe", "Windows command shell -- full shell access", CommandOs::windows, ChatToolRisk::high_risk},
    {"powershell.exe", "Windows PowerShell -- full shell access", CommandOs::windows, ChatToolRisk::high_risk},
    {"pwsh.exe", "PowerShell 7+ -- full shell access", CommandOs::windows, ChatToolRisk::high_risk},
    {"dir", "List directory contents", CommandOs::windows, ChatToolRisk::safe},
    {"type", "Print a file's contents", CommandOs::windows, ChatToolRisk::safe},
    {"copy", "Copy files", CommandOs::windows, ChatToolRisk::safe},
    {"xcopy", "Copy files/directory trees", CommandOs::windows, ChatToolRisk::safe},
    {"robocopy", "Robust file/directory copy", CommandOs::windows, ChatToolRisk::safe},
    {"move", "Move/rename files", CommandOs::windows, ChatToolRisk::safe},
    {"ren", "Rename files", CommandOs::windows, ChatToolRisk::safe},
    {"del", "Delete files", CommandOs::windows, ChatToolRisk::high_risk},
    {"erase", "Delete files", CommandOs::windows, ChatToolRisk::high_risk},
    {"rmdir", "Remove a directory tree", CommandOs::windows, ChatToolRisk::high_risk},
    {"rd", "Remove a directory tree", CommandOs::windows, ChatToolRisk::high_risk},
    {"mkdir", "Create a directory", CommandOs::windows, ChatToolRisk::safe},
    {"md", "Create a directory", CommandOs::windows, ChatToolRisk::safe},
    {"findstr", "Search text using patterns", CommandOs::windows, ChatToolRisk::safe},
    {"where", "Locate an executable on PATH", CommandOs::windows, ChatToolRisk::safe},
    {"more", "Page through text output", CommandOs::windows, ChatToolRisk::safe},
    {"fc", "Compare two files", CommandOs::windows, ChatToolRisk::safe},
    {"attrib", "View/change file attributes", CommandOs::windows, ChatToolRisk::safe},
    {"tasklist", "List running processes", CommandOs::windows, ChatToolRisk::safe},
    {"taskkill", "Terminate a process", CommandOs::windows, ChatToolRisk::high_risk},
    {"ipconfig", "Show network configuration", CommandOs::windows, ChatToolRisk::safe},
    {"netstat", "Show network connections", CommandOs::windows, ChatToolRisk::safe},
    {"systeminfo", "Show system configuration summary", CommandOs::windows, ChatToolRisk::safe},
    {"whoami", "Show the current user identity", CommandOs::windows, ChatToolRisk::safe},
    {"hostname", "Show the machine's hostname", CommandOs::windows, ChatToolRisk::safe},
    {"powercfg", "View/change power settings", CommandOs::windows, ChatToolRisk::safe},
    {"format", "Format a disk volume -- destroys its contents", CommandOs::windows, ChatToolRisk::high_risk},
    {"diskpart", "Partition/format disks", CommandOs::windows, ChatToolRisk::high_risk},
    {"reg", "Read/write the Windows registry", CommandOs::windows, ChatToolRisk::high_risk},
    {"sc", "Control Windows services", CommandOs::windows, ChatToolRisk::high_risk},
    {"net", "Manage users, shares and network services", CommandOs::windows, ChatToolRisk::high_risk},
    {"shutdown", "Shut down or restart the machine", CommandOs::windows, ChatToolRisk::high_risk},
    {"wmic", "WMI command-line management", CommandOs::windows, ChatToolRisk::high_risk},
    {"certutil", "Certificate/encoding utility", CommandOs::windows, ChatToolRisk::high_risk},
    {"msbuild.exe", "Build .NET/C++ projects and solutions", CommandOs::windows, ChatToolRisk::safe},
    {"nuget.exe", "NuGet package manager", CommandOs::windows, ChatToolRisk::safe},

    // --- Linux/POSIX shells and core coreutils ---
    {"bash", "Bourne Again shell -- full shell access", CommandOs::linux, ChatToolRisk::high_risk},
    {"sh", "POSIX shell -- full shell access", CommandOs::linux, ChatToolRisk::high_risk},
    {"zsh", "Z shell -- full shell access", CommandOs::linux, ChatToolRisk::high_risk},
    {"ls", "List directory contents", CommandOs::linux, ChatToolRisk::safe},
    {"cat", "Print a file's contents", CommandOs::linux, ChatToolRisk::safe},
    {"cp", "Copy files", CommandOs::linux, ChatToolRisk::safe},
    {"mv", "Move/rename files", CommandOs::linux, ChatToolRisk::safe},
    {"rm", "Delete files", CommandOs::linux, ChatToolRisk::high_risk},
    {"rmdir", "Remove an empty directory", CommandOs::linux, ChatToolRisk::high_risk},
    {"mkdir", "Create a directory", CommandOs::linux, ChatToolRisk::safe},
    {"touch", "Create/update a file's timestamp", CommandOs::linux, ChatToolRisk::safe},
    {"chmod", "Change file permissions", CommandOs::linux, ChatToolRisk::high_risk},
    {"chown", "Change file ownership", CommandOs::linux, ChatToolRisk::high_risk},
    {"ln", "Create links between files", CommandOs::linux, ChatToolRisk::safe},
    {"grep", "Search text using patterns", CommandOs::linux, ChatToolRisk::safe},
    {"find", "Search for files/directories", CommandOs::linux, ChatToolRisk::safe},
    {"sed", "Stream text editor", CommandOs::linux, ChatToolRisk::safe},
    {"awk", "Pattern-directed text processing", CommandOs::linux, ChatToolRisk::safe},
    {"head", "Print the first lines of a file", CommandOs::linux, ChatToolRisk::safe},
    {"tail", "Print the last lines of a file", CommandOs::linux, ChatToolRisk::safe},
    {"wc", "Count lines/words/bytes", CommandOs::linux, ChatToolRisk::safe},
    {"sort", "Sort lines of text", CommandOs::linux, ChatToolRisk::safe},
    {"uniq", "Filter repeated lines", CommandOs::linux, ChatToolRisk::safe},
    {"diff", "Compare two files", CommandOs::linux, ChatToolRisk::safe},
    {"tar", "Archive/extract files", CommandOs::linux, ChatToolRisk::safe},
    {"gzip", "Compress a file", CommandOs::linux, ChatToolRisk::safe},
    {"gunzip", "Decompress a .gz file", CommandOs::linux, ChatToolRisk::safe},
    {"stat", "Show file/filesystem status", CommandOs::linux, ChatToolRisk::safe},
    {"file", "Identify a file's type", CommandOs::linux, ChatToolRisk::safe},
    {"readlink", "Print a symlink's target", CommandOs::linux, ChatToolRisk::safe},
    {"ps", "List running processes", CommandOs::linux, ChatToolRisk::safe},
    {"top", "Show live process/resource usage", CommandOs::linux, ChatToolRisk::safe},
    {"kill", "Send a signal to a process", CommandOs::linux, ChatToolRisk::high_risk},
    {"killall", "Send a signal to processes by name", CommandOs::linux, ChatToolRisk::high_risk},
    {"df", "Show disk space usage", CommandOs::linux, ChatToolRisk::safe},
    {"du", "Show directory/file space usage", CommandOs::linux, ChatToolRisk::safe},
    {"free", "Show memory usage", CommandOs::linux, ChatToolRisk::safe},
    {"uname", "Show system/kernel information", CommandOs::linux, ChatToolRisk::safe},
    {"env", "Show/run with environment variables", CommandOs::linux, ChatToolRisk::safe},
    {"ifconfig", "Show/configure network interfaces", CommandOs::linux, ChatToolRisk::safe},
    {"ip", "Show/configure network interfaces and routes", CommandOs::linux, ChatToolRisk::safe},
    {"ss", "Show socket statistics", CommandOs::linux, ChatToolRisk::safe},
    {"systemctl", "Control systemd services", CommandOs::linux, ChatToolRisk::high_risk},
    {"service", "Control system services", CommandOs::linux, ChatToolRisk::high_risk},
    {"sudo", "Run a command as another user -- privilege escalation", CommandOs::linux, ChatToolRisk::high_risk},
    {"su", "Switch user", CommandOs::linux, ChatToolRisk::high_risk},
    {"useradd", "Create a user account", CommandOs::linux, ChatToolRisk::high_risk},
    {"userdel", "Delete a user account", CommandOs::linux, ChatToolRisk::high_risk},
    {"passwd", "Change a user's password", CommandOs::linux, ChatToolRisk::high_risk},
    {"apt", "Debian/Ubuntu package manager", CommandOs::linux, ChatToolRisk::high_risk},
    {"apt-get", "Debian/Ubuntu package manager", CommandOs::linux, ChatToolRisk::high_risk},
    {"dpkg", "Debian package tool", CommandOs::linux, ChatToolRisk::high_risk},
    {"yum", "RHEL/CentOS package manager", CommandOs::linux, ChatToolRisk::high_risk},
    {"dnf", "Fedora package manager", CommandOs::linux, ChatToolRisk::high_risk},
    {"mount", "Mount a filesystem", CommandOs::linux, ChatToolRisk::high_risk},
    {"umount", "Unmount a filesystem", CommandOs::linux, ChatToolRisk::high_risk},
    {"chroot", "Change the apparent root filesystem", CommandOs::linux, ChatToolRisk::high_risk},
    {"crontab", "Manage scheduled cron jobs", CommandOs::linux, ChatToolRisk::high_risk},
    {"man", "Show a command's manual page", CommandOs::linux, ChatToolRisk::safe},
    {"which", "Locate an executable on PATH", CommandOs::linux, ChatToolRisk::safe},

    // --- Cross-platform developer tooling ---
    {"git", "Git version control", CommandOs::both, ChatToolRisk::safe},
    {"python", "Python interpreter", CommandOs::both, ChatToolRisk::safe},
    {"python3", "Python 3 interpreter", CommandOs::both, ChatToolRisk::safe},
    {"pip", "Python package installer", CommandOs::both, ChatToolRisk::safe},
    {"pip3", "Python 3 package installer", CommandOs::both, ChatToolRisk::safe},
    {"node", "Node.js runtime", CommandOs::both, ChatToolRisk::safe},
    {"npm", "Node package manager", CommandOs::both, ChatToolRisk::safe},
    {"npx", "Run a Node package binary", CommandOs::both, ChatToolRisk::safe},
    {"yarn", "Yarn package manager", CommandOs::both, ChatToolRisk::safe},
    {"pnpm", "pnpm package manager", CommandOs::both, ChatToolRisk::safe},
    {"docker", "Container build/run engine", CommandOs::both, ChatToolRisk::high_risk},
    {"docker-compose", "Multi-container orchestration", CommandOs::both, ChatToolRisk::high_risk},
    {"cmake", "Cross-platform build system generator", CommandOs::both, ChatToolRisk::safe},
    {"ninja", "Fast build-file executor", CommandOs::both, ChatToolRisk::safe},
    {"make", "Build automation tool", CommandOs::both, ChatToolRisk::safe},
    {"gcc", "GNU C compiler", CommandOs::both, ChatToolRisk::safe},
    {"g++", "GNU C++ compiler", CommandOs::both, ChatToolRisk::safe},
    {"clang", "LLVM C compiler", CommandOs::both, ChatToolRisk::safe},
    {"clang++", "LLVM C++ compiler", CommandOs::both, ChatToolRisk::safe},
    {"curl", "Transfer data with a URL", CommandOs::both, ChatToolRisk::safe},
    {"wget", "Download files from the web", CommandOs::both, ChatToolRisk::safe},
    {"ping", "Test network reachability", CommandOs::both, ChatToolRisk::safe},
    {"ssh", "Remote shell access to another host", CommandOs::both, ChatToolRisk::high_risk},
    {"scp", "Copy files over SSH", CommandOs::both, ChatToolRisk::high_risk},
    {"rsync", "Sync files/directories", CommandOs::both, ChatToolRisk::safe},
    {"java", "Java runtime", CommandOs::both, ChatToolRisk::safe},
    {"javac", "Java compiler", CommandOs::both, ChatToolRisk::safe},
    {"go", "Go compiler/toolchain", CommandOs::both, ChatToolRisk::safe},
    {"cargo", "Rust package manager/build tool", CommandOs::both, ChatToolRisk::safe},
    {"rustc", "Rust compiler", CommandOs::both, ChatToolRisk::safe},
    {"dotnet", ".NET SDK/CLI", CommandOs::both, ChatToolRisk::safe},
    {"ctest", "CMake test driver", CommandOs::both, ChatToolRisk::safe},
    {"gdb", "GNU debugger", CommandOs::both, ChatToolRisk::safe},
};

}  // namespace

void AllowedCommandStore::ensure_default_catalog() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& entry : kDefaultCommandCatalog) {
        const auto wanted = to_lower_ascii(entry.executable);
        bool already_present = false;
        for (const auto& [id, command] : commands_) {
            static_cast<void>(id);
            if (to_lower_ascii(command.executable) == wanted) {
                already_present = true;
                break;
            }
        }
        if (already_present) continue;
        AllowedCommandRecord command;
        command.id = generate_allowed_command_id();
        command.executable = entry.executable;
        command.description = entry.description;
        command.risk_default = entry.risk;
        command.os = entry.os;
        command.enabled = true;
        commands_.emplace(command.id, command);
        persist(command);
    }
}

void AllowedCommandStore::remove(const std::string& command_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (commands_.erase(command_id) == 0U) {
        throw std::invalid_argument("allowed command does not exist");
    }
    records_.erase("chat_tool_allowed_commands", command_id);
}

std::optional<AllowedCommandRecord> AllowedCommandStore::find_by_executable(
    const std::string& executable) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto wanted = to_lower_ascii(executable);
    for (const auto& [id, command] : commands_) {
        static_cast<void>(id);
        if (!command.enabled) continue;
        if (to_lower_ascii(command.executable) == wanted) return command;
    }
    return std::nullopt;
}

std::vector<AllowedCommandRecord> AllowedCommandStore::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AllowedCommandRecord> result;
    result.reserve(commands_.size());
    for (const auto& item : commands_) result.push_back(item.second);
    return result;
}

// ---------------------------------------------------------------------------
// PendingToolApprovalStore
// ---------------------------------------------------------------------------

PendingToolApprovalStore::PendingToolApprovalStore(RecordStore& records)
    : records_(records) {}

PendingToolApproval PendingToolApprovalStore::create(
    PendingToolApproval approval) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.put("chat_tool_pending_approvals", approval.id,
                 pack({approval.chat_id, approval.user_id, approval.tool_name,
                       approval.arguments_json, approval.reason,
                       std::to_string(approval.created_epoch_seconds)}));
    return approval;
}

std::optional<PendingToolApproval> PendingToolApprovalStore::find(
    const std::string& approval_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto raw = records_.get("chat_tool_pending_approvals", approval_id);
    if (!raw) return std::nullopt;
    try {
        const auto fields = unpack(*raw);
        if (fields.size() != 6U) return std::nullopt;
        PendingToolApproval approval;
        approval.id = approval_id;
        approval.chat_id = fields[0];
        approval.user_id = fields[1];
        approval.tool_name = fields[2];
        approval.arguments_json = fields[3];
        approval.reason = fields[4];
        approval.created_epoch_seconds = std::stoull(fields[5]);
        return approval;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

void PendingToolApprovalStore::remove(const std::string& approval_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    records_.erase("chat_tool_pending_approvals", approval_id);
}

// ---------------------------------------------------------------------------
// classify_tool_call_risk
// ---------------------------------------------------------------------------

namespace {

// One fixed, case-insensitive substring per destructive pattern. Matched
// against the whole "executable arg1 arg2 ..." command line so multi-token
// patterns ("reset --hard") are caught without a full argv-aware parser.
// Deliberately conservative (a superset of what's actually destructive is
// fine here -- it only ever costs an extra confirmation click, never a
// silent skip), since this table is the one thing standing between the
// model and an irreversible local change.
const char* const kDestructivePatterns[] = {
    "rm ", "rm.exe", "rmdir", "del ", "del.exe", "erase ", "format ",
    "diskpart", "mkfs", "drop table", "drop database", "truncate table",
    "reset --hard", "clean -f", "clean -fd", "push --force",
    "push -f", "shutdown", "taskkill", "reg delete", "unregister-object",
    "remove-item", "del /", "rd /",
};

bool contains_ci(const std::string& haystack, const std::string& needle) {
    const auto lower_haystack = to_lower_ascii(haystack);
    const auto lower_needle = to_lower_ascii(needle);
    return lower_haystack.find(lower_needle) != std::string::npos;
}

}  // namespace

ChatToolRisk classify_tool_call_risk(const std::string& tool_name,
                                     const JsonValue& arguments) {
    // delete_file is always high_risk: there is no safe wording of "remove
    // this file" that should ever run without a human looking at it first.
    if (tool_name == "delete_file") return ChatToolRisk::high_risk;

    // write_file is high_risk only when it would blank out an existing
    // file (an empty/near-empty replacement content for a path that
    // already has real content is indistinguishable from an accidental
    // delete-via-overwrite); an ordinary edit stays safe so "confirm/
    // implement" can actually apply changes without extra friction.
    if (tool_name == "write_file") {
        const auto* content = arguments.optional("content");
        const auto* path = arguments.optional("path");
        if (content != nullptr && path != nullptr &&
            content->as_string().empty()) {
            return ChatToolRisk::high_risk;
        }
        return ChatToolRisk::safe;
    }

    if (tool_name == "run_command") {
        std::string command_line;
        if (const auto* executable = arguments.optional("executable")) {
            command_line += executable->as_string();
        }
        if (const auto* args = arguments.optional("arguments")) {
            for (const auto& argument : args->as_array()) {
                command_line += " " + argument.as_string();
            }
        }
        for (const char* pattern : kDestructivePatterns) {
            if (contains_ci(command_line, pattern)) return ChatToolRisk::high_risk;
        }
        return ChatToolRisk::safe;
    }

    // read_file/search/list_directory never mutate anything.
    return ChatToolRisk::safe;
}

// ---------------------------------------------------------------------------
// run_sandboxed_process
// ---------------------------------------------------------------------------

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string& value) {
    if (value.empty()) return {};
    const int required = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (required <= 0) {
        throw std::runtime_error("tool process text is not valid UTF-8");
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                            static_cast<int>(value.size()), &result[0],
                            required) != required) {
        throw std::runtime_error("tool process text conversion failed");
    }
    return result;
}

std::wstring quote_argument(const std::wstring& argument) {
    if (!argument.empty() &&
        argument.find_first_of(L" \t\"") == std::wstring::npos) {
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
    void reset(HANDLE value = nullptr) noexcept {
        if (value_ != nullptr && value_ != INVALID_HANDLE_VALUE) {
            CloseHandle(value_);
        }
        value_ = value;
    }

private:
    HANDLE value_{nullptr};
};

}  // namespace

ToolProcessResult run_sandboxed_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory,
    const std::uint64_t timeout_seconds,
    const std::uint64_t maximum_output_bytes, std::atomic_bool& cancellation) {
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
        return {false, false, false, -1, {}, {}, "sandbox pipe creation failed"};
    }
    UniqueHandle child_input_read(child_input_read_raw);
    UniqueHandle parent_input_write(parent_input_write_raw);
    UniqueHandle parent_output_read(parent_output_read_raw);
    UniqueHandle child_output_write(child_output_write_raw);
    SetHandleInformation(parent_input_write.get(), HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(parent_output_read.get(), HANDLE_FLAG_INHERIT, 0);
    // stdout and stderr are merged into the single output pipe above (the
    // child's stderr is duplicated onto the same write handle) -- callers
    // that need them separated can still tell success from failure via the
    // real exit code this function returns.
    HANDLE child_error_write = child_output_write.get();

    UniqueHandle job(CreateJobObjectW(nullptr, nullptr));
    if (job.get() == nullptr) {
        return {false, false, false, -1, {}, {}, "sandbox job object creation failed"};
    }
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE |
        JOB_OBJECT_LIMIT_ACTIVE_PROCESS | JOB_OBJECT_LIMIT_PROCESS_MEMORY;
    limits.BasicLimitInformation.ActiveProcessLimit = 1U;
    // 512 MiB is a fixed, generous ceiling for a short-lived tool
    // invocation (a real build/test command, not a long-running server) --
    // not caller-configurable, so no tool call can widen its own sandbox.
    limits.ProcessMemoryLimit = static_cast<SIZE_T>(512ULL * 1024ULL * 1024ULL);
    if (!SetInformationJobObject(job.get(), JobObjectExtendedLimitInformation,
                                 &limits, sizeof(limits))) {
        return {false, false, false, -1, {}, {}, "sandbox job object policy failed"};
    }

    std::wstring command = quote_argument(wide(executable.string()));
    for (const auto& argument : arguments) {
        command.push_back(L' ');
        command += quote_argument(wide(argument));
    }
    std::vector<wchar_t> mutable_command(command.begin(), command.end());
    mutable_command.push_back(L'\0');

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = child_input_read.get();
    startup.hStdOutput = child_output_write.get();
    startup.hStdError = child_error_write;
    PROCESS_INFORMATION process{};
    const auto executable_wide = wide(executable.string());
    const auto working_wide = wide(working_directory.string());
    // BELOW_NORMAL_PRIORITY_CLASS mirrors the same reasoning as the model
    // runner process (see inference.cpp): a model-issued tool call can be a
    // real build/test/git command with no bound on how CPU-heavy it gets,
    // and running it at the default priority is what previously let a single
    // tool call stall the rest of the OS's scheduler, not just this app.
    if (!CreateProcessW(executable_wide.c_str(), mutable_command.data(),
                        nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW | CREATE_SUSPENDED |
                            BELOW_NORMAL_PRIORITY_CLASS,
                        nullptr, working_wide.c_str(), &startup, &process)) {
        return {false, false, false, -1, {}, {}, "tool process launch failed"};
    }
    UniqueHandle process_handle(process.hProcess);
    UniqueHandle thread_handle(process.hThread);
    if (!AssignProcessToJobObject(job.get(), process_handle.get()) ||
        ResumeThread(thread_handle.get()) == static_cast<DWORD>(-1)) {
        TerminateProcess(process_handle.get(), 1U);
        return {false, false, false, -1, {}, {}, "tool sandbox assignment failed"};
    }
    child_input_read.reset();
    child_output_write.reset();
    // The child holds its own duplicate of the shared output handle; the
    // parent's copy of that same handle value was already closed above via
    // child_output_write.reset() (child_error_write pointed at it).

    // No stdin payload: every tool call this codebase makes is expressed
    // entirely as command-line arguments, so stdin is closed immediately
    // rather than left open (which would otherwise hang a child that reads
    // until EOF).
    parent_input_write.reset();

    std::string output;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    bool exited = false;
    bool timed_out = false;
    bool cancelled = false;
    while (!exited) {
        if (cancellation.load()) {
            TerminateJobObject(job.get(), 2U);
            cancelled = true;
            break;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            TerminateJobObject(job.get(), 3U);
            timed_out = true;
            break;
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
                std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (ReadFile(parent_output_read.get(), buffer.data(), wanted,
                        &received, nullptr) &&
                received > 0U) {
                if (output.size() + received > maximum_output_bytes) {
                    TerminateJobObject(job.get(), 4U);
                    return {false, false, false, -1, std::move(output), {},
                            "tool output exceeded its bound"};
                }
                output.append(buffer.data(), received);
            }
        }
        exited = WaitForSingleObject(process_handle.get(), 10U) == WAIT_OBJECT_0;
    }
    if (cancelled) {
        return {false, true, false, -1, std::move(output), {},
                "tool call was cancelled"};
    }
    if (timed_out) {
        return {false, false, true, -1, std::move(output), {},
                "tool call timed out"};
    }
    for (;;) {
        std::array<char, 8192U> buffer{};
        DWORD received = 0U;
        if (!ReadFile(parent_output_read.get(), buffer.data(),
                      static_cast<DWORD>(buffer.size()), &received, nullptr) ||
            received == 0U) {
            break;
        }
        if (output.size() + received > maximum_output_bytes) {
            return {false, false, false, -1, std::move(output), {},
                    "tool output exceeded its bound"};
        }
        output.append(buffer.data(), received);
    }
    DWORD exit_code = 1U;
    GetExitCodeProcess(process_handle.get(), &exit_code);
    return {exit_code == 0U, false, false, static_cast<int>(exit_code),
            std::move(output), {}, exit_code == 0U ? "" : "tool exited with a failure status"};
}

#else  // Linux

namespace {

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

}  // namespace

ToolProcessResult run_sandboxed_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory,
    const std::uint64_t timeout_seconds,
    const std::uint64_t maximum_output_bytes, std::atomic_bool& cancellation) {
    int input_pipe[2]{-1, -1};
    int output_pipe[2]{-1, -1};
    if (pipe(input_pipe) != 0 || pipe(output_pipe) != 0) {
        if (input_pipe[0] >= 0) close(input_pipe[0]);
        if (input_pipe[1] >= 0) close(input_pipe[1]);
        return {false, false, false, -1, {}, {}, "sandbox pipe creation failed"};
    }
    const pid_t child = fork();
    if (child < 0) {
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        return {false, false, false, -1, {}, {}, "sandbox fork failed"};
    }
    if (child == 0) {
        dup2(input_pipe[0], STDIN_FILENO);
        dup2(output_pipe[1], STDOUT_FILENO);
        dup2(output_pipe[1], STDERR_FILENO);
        close(input_pipe[0]);
        close(input_pipe[1]);
        close(output_pipe[0]);
        close(output_pipe[1]);
        if (chdir(working_directory.c_str()) != 0) _exit(126);
        // Same reasoning as the Windows BELOW_NORMAL_PRIORITY_CLASS above:
        // an unbounded model-issued command should never contend for CPU on
        // equal footing with the rest of the system. Best-effort -- ignore
        // failure and keep launching.
        [[maybe_unused]] const int nice_result = nice(5);
        const rlimit cpu_limit{timeout_seconds + 1U, timeout_seconds + 1U};
        const rlimit file_limit{maximum_output_bytes, maximum_output_bytes};
        const rlimit memory_limit{512ULL * 1024ULL * 1024ULL,
                                  512ULL * 1024ULL * 1024ULL};
        const rlimit process_limit{1U, 1U};
        if (setrlimit(RLIMIT_CPU, &cpu_limit) != 0 ||
            setrlimit(RLIMIT_FSIZE, &file_limit) != 0 ||
            setrlimit(RLIMIT_AS, &memory_limit) != 0 ||
            setrlimit(RLIMIT_NPROC, &process_limit) != 0 ||
            prctl(PR_SET_NO_NEW_PRIVS, 1, 0, 0, 0) != 0) {
            _exit(126);
        }
        std::vector<std::string> values;
        values.push_back(executable.string());
        values.insert(values.end(), arguments.begin(), arguments.end());
        std::vector<char*> argv;
        for (auto& value : values) argv.push_back(&value[0]);
        argv.push_back(nullptr);
        execv(executable.c_str(), argv.data());
        _exit(127);
    }

    close(input_pipe[0]);
    close(output_pipe[1]);
    // No stdin payload -- see the Windows implementation's identical note.
    close(input_pipe[1]);
    const int flags = fcntl(output_pipe[0], F_GETFL, 0);
    fcntl(output_pipe[0], F_SETFL, flags | O_NONBLOCK);
    std::string output;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(timeout_seconds);
    int status = 0;
    for (;;) {
        std::array<char, 8192U> buffer{};
        const auto received = read(output_pipe[0], buffer.data(), buffer.size());
        if (received > 0) {
            if (output.size() + static_cast<std::size_t>(received) >
                maximum_output_bytes) {
                kill(child, SIGKILL);
                waitpid(child, nullptr, 0);
                close(output_pipe[0]);
                return {false, false, false, -1, std::move(output), {},
                        "tool output exceeded its bound"};
            }
            output.append(buffer.data(), static_cast<std::size_t>(received));
        }
        const auto waited = waitpid(child, &status, WNOHANG);
        if (waited == child) break;
        if (cancellation.load()) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(output_pipe[0]);
            return {false, true, false, -1, std::move(output), {},
                    "tool call was cancelled"};
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            kill(child, SIGKILL);
            waitpid(child, nullptr, 0);
            close(output_pipe[0]);
            return {false, false, true, -1, std::move(output), {},
                    "tool call timed out"};
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    for (;;) {
        std::array<char, 8192U> buffer{};
        const auto received = read(output_pipe[0], buffer.data(), buffer.size());
        if (received <= 0) break;
        output.append(buffer.data(), static_cast<std::size_t>(received));
    }
    close(output_pipe[0]);
    const bool succeeded = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    const int exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return {succeeded, false, false, exit_code, std::move(output), {},
            succeeded ? "" : "tool exited with a failure status"};
}

#endif

// ---------------------------------------------------------------------------
// execute_chat_tool
// ---------------------------------------------------------------------------

namespace {

constexpr std::uint64_t kToolReadMaxBytes = 1024ULL * 1024ULL;
constexpr std::size_t kToolSearchMaxFiles = 512U;
constexpr std::size_t kToolSearchMaxResults = 50U;
constexpr std::uint64_t kToolCommandTimeoutSeconds = 30ULL;
constexpr std::uint64_t kToolCommandMaxOutputBytes = 256ULL * 1024ULL;

// Canonicalizes and contains one project-relative path exactly the way
// read_project_text_file() already does for reads (masterai.hpp/
// workflows.cpp) -- reused here by the write/delete/list tools, which need
// the same boundary but aren't reading an existing regular text file.
std::filesystem::path resolve_project_path(const ProjectRecord& project,
                                           const std::string& relative_text) {
    if (relative_text.empty() || relative_text.size() > 4096U) {
        throw std::invalid_argument("tool path is outside policy");
    }
    const std::filesystem::path relative(relative_text);
    if (relative.is_absolute()) {
        throw std::invalid_argument("tool path must be relative");
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(project.root, error);
    if (error) throw std::invalid_argument("project root is unavailable");
    const auto path =
        std::filesystem::weakly_canonical(project.root / relative, error);
    if (error || !is_path_within(root, path)) {
        throw std::invalid_argument("tool path escapes the project root");
    }
    return path;
}

ChatToolCallResult tool_read_file(const JsonValue& arguments,
                                  const ProjectRecord& project) {
    const auto path = arguments.required("path").as_string();
    const auto file = read_project_text_file(project, path, kToolReadMaxBytes);
    return {true, file.content,
            "{\"path\":" + json_string(path) +
                ",\"content\":" + json_string(file.content) + "}"};
}

ChatToolCallResult tool_list_directory(const JsonValue& arguments,
                                       const ProjectRecord& project) {
    std::string relative_text = ".";
    if (const auto* path_field = arguments.optional("path")) {
        relative_text = path_field->as_string();
        if (relative_text.empty()) relative_text = ".";
    }
    const std::filesystem::path relative(relative_text);
    if (relative.is_absolute()) {
        throw std::invalid_argument("tool path must be relative");
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(project.root, error);
    if (error) throw std::invalid_argument("project root is unavailable");
    const auto directory =
        std::filesystem::weakly_canonical(project.root / relative, error);
    if (error || !is_path_within(root, directory) ||
        !std::filesystem::is_directory(directory, error) || error) {
        throw std::invalid_argument("tool directory is unavailable");
    }
    std::string listing_text;
    std::string structured = "{\"entries\":[";
    bool first = true;
    std::size_t count = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(
             directory, std::filesystem::directory_options::skip_permission_denied,
             error)) {
        if (count >= 512U) break;
        std::error_code entry_error;
        if (entry.is_symlink(entry_error)) continue;
        const auto name = entry.path().filename().string();
        const bool is_directory = entry.is_directory(entry_error);
        listing_text += std::string(is_directory ? "[dir]  " : "[file] ") +
                        name + "\n";
        if (!first) structured += ",";
        first = false;
        structured += "{\"name\":" + json_string(name) +
                      ",\"isDirectory\":" + (is_directory ? "true" : "false") +
                      "}";
        ++count;
    }
    structured += "]}";
    return {true, listing_text.empty() ? "(empty directory)" : listing_text,
            structured};
}

// Bounded literal (case-sensitive, single-line) search, mirroring
// mcp.cpp's own search_project() but kept as a private copy here rather
// than reused across that module boundary -- see this file's header
// comment on why AllowedCommandStore's pack/unpack is likewise private.
ChatToolCallResult tool_search(const JsonValue& arguments,
                               const ProjectRecord& project) {
    const auto query = arguments.required("query").as_string();
    if (query.empty() || query.size() > 256U) {
        throw std::invalid_argument("search query is outside policy");
    }
    std::string text_result;
    std::string structured = "{\"matches\":[";
    bool first = true;
    std::size_t visited = 0U;
    std::size_t matches = 0U;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        project.root, std::filesystem::directory_options::skip_permission_denied,
        error);
    const std::filesystem::recursive_directory_iterator end;
    while (!error && iterator != end && visited < kToolSearchMaxFiles &&
           matches < kToolSearchMaxResults) {
        const auto entry = *iterator;
        std::error_code entry_error;
        if (entry.is_symlink(entry_error)) {
            if (entry.is_directory(entry_error)) {
                iterator.disable_recursion_pending();
            }
            iterator.increment(error);
            continue;
        }
        if (entry.is_regular_file(entry_error)) {
            const auto size = entry.file_size(entry_error);
            if (!entry_error && size <= kToolReadMaxBytes) {
                std::ifstream input(entry.path(), std::ios::binary);
                std::string line;
                std::size_t line_number = 0U;
                while (matches < kToolSearchMaxResults &&
                      std::getline(input, line)) {
                    ++line_number;
                    if (line.find(query) == std::string::npos) continue;
                    const auto relative = std::filesystem::relative(
                        entry.path(), project.root, entry_error);
                    if (entry_error) continue;
                    if (!first) structured += ",";
                    first = false;
                    structured += "{\"path\":" +
                                  json_string(relative.generic_string()) +
                                  ",\"line\":" + std::to_string(line_number) +
                                  ",\"text\":" + json_string(line) + "}";
                    text_result += relative.generic_string() + ":" +
                                   std::to_string(line_number) + ": " + line +
                                   "\n";
                    ++matches;
                }
            }
            ++visited;
        }
        iterator.increment(error);
    }
    structured += "]}";
    return {true, text_result.empty() ? "(no matches)" : text_result,
            structured};
}

// Always renders a plain before/after byte-count summary alongside the new
// content rather than a computed unified diff -- a real line-level diff
// algorithm is a reasonable follow-up, but this still satisfies the actual
// requirement (the change is real and fully visible, not silent) without
// it.
ChatToolCallResult tool_write_file(const JsonValue& arguments,
                                   const ProjectRecord& project) {
    const auto path_text = arguments.required("path").as_string();
    const auto content = arguments.required("content").as_string();
    if (content.size() > 4ULL * 1024ULL * 1024ULL) {
        throw std::invalid_argument("tool file content exceeds its limit");
    }
    const auto path = resolve_project_path(project, path_text);
    std::error_code error;
    const bool existed = std::filesystem::is_regular_file(path, error);
    std::uint64_t previous_size = 0ULL;
    if (existed) {
        previous_size = std::filesystem::file_size(path, error);
    }
    std::filesystem::create_directories(path.parent_path(), error);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::invalid_argument("tool file could not be opened for writing");
    }
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    if (!output) {
        throw std::invalid_argument("tool file write failed");
    }
    output.close();
    const std::string summary =
        std::string(existed ? "Updated " : "Created ") + path_text + " (" +
        std::to_string(previous_size) + " -> " +
        std::to_string(content.size()) + " bytes).";
    return {true, summary + "\n\n" + content,
            "{\"path\":" + json_string(path_text) +
                ",\"created\":" + (existed ? "false" : "true") +
                ",\"previousBytes\":" + std::to_string(previous_size) +
                ",\"newBytes\":" + std::to_string(content.size()) + "}"};
}

ChatToolCallResult tool_delete_file(const JsonValue& arguments,
                                    const ProjectRecord& project) {
    const auto path_text = arguments.required("path").as_string();
    const auto path = resolve_project_path(project, path_text);
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error) {
        throw std::invalid_argument("tool file to delete is unavailable");
    }
    if (!std::filesystem::remove(path, error) || error) {
        throw std::invalid_argument("tool file delete failed");
    }
    return {true, "Deleted " + path_text + ".",
            "{\"path\":" + json_string(path_text) + ",\"deleted\":true}"};
}

ChatToolCallResult tool_run_command(const JsonValue& arguments,
                                    const ProjectRecord& project,
                                    AllowedCommandStore& allowed_commands,
                                    std::atomic_bool& cancellation) {
    const auto executable_name = arguments.required("executable").as_string();
    std::vector<std::string> args;
    if (const auto* args_field = arguments.optional("arguments")) {
        for (const auto& item : args_field->as_array()) {
            args.push_back(item.as_string());
        }
    }
    const auto allowed = allowed_commands.find_by_executable(executable_name);
    if (!allowed) {
        return {false,
                "Command '" + executable_name +
                    "' is not on the admin allow-list.",
                "{\"error\":\"not_allowed\"}"};
    }
    if (!allowed->allowed_project_ids.empty() &&
        allowed->allowed_project_ids.find(project.id) ==
            allowed->allowed_project_ids.end()) {
        return {false,
                "Command '" + executable_name +
                    "' is not allowed for this project.",
                "{\"error\":\"project_not_allowed\"}"};
    }
    const auto result = run_sandboxed_process(
        allowed->executable, args, project.root, kToolCommandTimeoutSeconds,
        kToolCommandMaxOutputBytes, cancellation);
    const std::string summary =
        "exit " + std::to_string(result.exit_code) + "\n" +
        result.standard_output;
    return {result.succeeded, summary,
            "{\"exitCode\":" + std::to_string(result.exit_code) +
                ",\"output\":" + json_string(result.standard_output) + "}"};
}

}  // namespace

ChatToolCallResult execute_chat_tool(const std::string& tool_name,
                                     const JsonValue& arguments,
                                     const ProjectRecord& project,
                                     AllowedCommandStore& allowed_commands,
                                     std::atomic_bool& cancellation) {
    try {
        if (tool_name == "read_file") return tool_read_file(arguments, project);
        if (tool_name == "list_directory") {
            return tool_list_directory(arguments, project);
        }
        if (tool_name == "search") return tool_search(arguments, project);
        if (tool_name == "write_file") return tool_write_file(arguments, project);
        if (tool_name == "delete_file") {
            return tool_delete_file(arguments, project);
        }
        if (tool_name == "run_command") {
            return tool_run_command(arguments, project, allowed_commands,
                                    cancellation);
        }
        return {false, "Unknown tool '" + tool_name + "'.",
                "{\"error\":\"unknown_tool\"}"};
    } catch (const std::exception& error) {
        return {false, std::string("Tool call failed: ") + error.what(),
                "{\"error\":\"tool_call_failed\"}"};
    }
}

}  // namespace masterai
