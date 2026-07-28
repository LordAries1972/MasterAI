// MasterAI backend-neutral IDE integration services.
//
// This unit provides secure OS-backed IDE token installation, connection
// profiles, deterministic source diagnostics, and read-only unified-diff
// previews. VS Code/Agent-Coder and Visual Studio consume the same HTTP/MCP
// contracts, so neither integration contains inference-backend-specific logic.

#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cctype>
#include <iostream>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <termios.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

constexpr std::size_t maximum_ide_text_bytes = 1024U * 1024U;
constexpr std::size_t maximum_diagnostics = 500U;
constexpr std::size_t maximum_diff_files = 64U;
constexpr std::size_t maximum_diff_hunks = 4096U;

// Recognizes C++ and Assembly units to enforce the project rule requiring an
// explanatory source-unit comment at the top.
bool requires_source_unit_comment(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    static const std::set<std::string> source_extensions{
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".asm", ".s"};
    return source_extensions.find(extension) != source_extensions.end();
}

// Removes Git's conventional a/ or b/ prefix and optional timestamp from one
// unified-diff header path.
std::string diff_header_path(std::string value) {
    const auto tab = value.find('\t');
    if (tab != std::string::npos) value.erase(tab);
    if (value.rfind("a/", 0U) == 0U || value.rfind("b/", 0U) == 0U) {
        value.erase(0U, 2U);
    }
    return value;
}

// Validates a diff path lexically and canonically against its registered
// project. /dev/null is accepted only as the create/delete sentinel.
void validate_diff_path(const ProjectRecord& project,
                        const std::string& header_path) {
    if (header_path == "/dev/null") return;
    const auto text = diff_header_path(header_path);
    const std::filesystem::path relative(text);
    if (text.empty() || text.size() > 4096U || relative.is_absolute()) {
        throw std::invalid_argument("diff path is outside policy");
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(project.root, error);
    if (error) throw std::invalid_argument("project root is unavailable");
    const auto candidate = (project.root / relative).lexically_normal();
    if (!is_path_within(root, candidate)) {
        throw std::invalid_argument("diff path escapes project");
    }
    const auto existing_parent =
        std::filesystem::weakly_canonical(candidate.parent_path(), error);
    if (error || !is_path_within(root, existing_parent)) {
        throw std::invalid_argument("diff parent escapes project");
    }
    if (std::filesystem::exists(candidate, error) &&
        (error || std::filesystem::is_symlink(candidate))) {
        throw std::invalid_argument("diff path targets a symlink");
    }
}

// Returns one explicitly authorized project or rejects the operation uniformly.
ProjectRecord authorized_project(
    ProjectCatalog& projects, const std::string& project_id,
    const std::set<std::string>& authorized_project_ids) {
    if (authorized_project_ids.find(project_id) ==
        authorized_project_ids.end()) {
        throw std::invalid_argument("IDE project access denied");
    }
    const auto project = projects.find(project_id);
    if (!project) {
        throw std::invalid_argument("IDE project does not exist");
    }
    return *project;
}

}  // namespace

// Captures the live project catalogue used by diagnostic and diff-preview
// authorization checks.
IdeIntegrationService::IdeIntegrationService(ProjectCatalog& projects)
    : projects_(projects) {}

// Describes only implemented, backend-neutral operations so both IDEs can
// render truthful capability controls.
std::string IdeIntegrationService::capabilities_json() const {
    return
        "{\"protocolVersion\":\"1.0\",\"chat\":{"
        "\"endpoint\":\"/api/v1/chats\",\"streaming\":\"ndjson\","
        "\"cancellation\":\"disconnect\"},\"context\":{"
        "\"endpoint\":\"/mcp\",\"protocol\":\"2025-11-25\"},"
        "\"diagnostics\":{\"endpoint\":\"/api/v1/ide/diagnostics\","
        "\"readOnly\":true},\"diffPreview\":{\"endpoint\":"
        "\"/api/v1/ide/diff-preview\",\"appliesChanges\":false},"
        "\"authentication\":{\"type\":\"scopedBearer\","
        "\"storage\":\"nativeOsSecretStore\"}}";
}

// Scans one authorized source unit for locally deterministic presentation and
// maintainability issues. It never compiles, edits, or sends source to a model.
std::string IdeIntegrationService::diagnostics_json(
    const std::string& project_id, const std::string& relative_path,
    const std::set<std::string>& authorized_project_ids) const {
    const auto project =
        authorized_project(projects_, project_id, authorized_project_ids);
    const auto source = read_project_text_file(
        project, relative_path, maximum_ide_text_bytes);
    std::string diagnostics{"{\"projectId\":" + json_string(project_id) +
                            ",\"path\":" + json_string(relative_path) +
                            ",\"diagnostics\":["};
    std::size_t count = 0U;
    auto add = [&](const std::size_t line, const std::string& code,
                   const std::string& severity,
                   const std::string& message) {
        if (count >= maximum_diagnostics) return;
        if (count != 0U) diagnostics += ",";
        diagnostics += "{\"line\":" + std::to_string(line) +
                       ",\"code\":" + json_string(code) +
                       ",\"severity\":" + json_string(severity) +
                       ",\"message\":" + json_string(message) + "}";
        ++count;
    };

    std::size_t content_start = 0U;
    if (source.content.size() >= 3U &&
        static_cast<unsigned char>(source.content[0]) == 0xefU &&
        static_cast<unsigned char>(source.content[1]) == 0xbbU &&
        static_cast<unsigned char>(source.content[2]) == 0xbfU) {
        content_start = 3U;
    }
    while (content_start < source.content.size() &&
           std::isspace(static_cast<unsigned char>(
               source.content[content_start])) != 0) {
        ++content_start;
    }
    if (requires_source_unit_comment(
            source.canonical_path.extension().string()) &&
        source.content.compare(content_start, 2U, "//") != 0 &&
        source.content.compare(content_start, 2U, "/*") != 0) {
        add(1U, "source-unit-comment-missing", "warning",
            "Add a top-of-unit explanation of responsibility and boundaries.");
    }

    std::istringstream lines(source.content);
    std::string line;
    std::size_t line_number = 0U;
    while (std::getline(lines, line) && count < maximum_diagnostics) {
        ++line_number;
        if (!line.empty() &&
            (line.back() == ' ' || line.back() == '\t')) {
            add(line_number, "trailing-whitespace", "information",
                "Remove trailing whitespace.");
        }
        if (line.size() > 160U) {
            add(line_number, "line-too-long", "information",
                "Line exceeds 160 bytes and may be difficult to review.");
        }
        if (line.find("TODO") != std::string::npos ||
            line.find("FIXME") != std::string::npos) {
            add(line_number, "unfinished-marker", "information",
                "Review the unfinished-work marker before release.");
        }
    }
    return diagnostics + "],\"truncated\":" +
           std::string(count >= maximum_diagnostics ? "true" : "false") +
           "}";
}

// Parses and validates a bounded text-only unified diff without writing any
// file. The result gives IDEs a trustworthy file/hunk/add/delete summary for
// human approval.
std::string IdeIntegrationService::diff_preview_json(
    const std::string& project_id, const std::string& unified_diff,
    const std::set<std::string>& authorized_project_ids) const {
    const auto project =
        authorized_project(projects_, project_id, authorized_project_ids);
    if (unified_diff.empty() ||
        unified_diff.size() > maximum_ide_text_bytes ||
        unified_diff.find('\0') != std::string::npos ||
        !valid_utf8_text(unified_diff) ||
        unified_diff.find("GIT binary patch") != std::string::npos) {
        throw std::invalid_argument("IDE diff is outside text policy");
    }

    std::istringstream input(unified_diff);
    std::string line;
    std::string old_path;
    std::size_t files = 0U;
    std::size_t hunks = 0U;
    std::size_t additions = 0U;
    std::size_t deletions = 0U;
    while (std::getline(input, line)) {
        if (line.rfind("--- ", 0U) == 0U) {
            old_path = line.substr(4U);
            validate_diff_path(project, old_path);
        } else if (line.rfind("+++ ", 0U) == 0U) {
            if (old_path.empty()) {
                throw std::invalid_argument("diff new path lacks old path");
            }
            validate_diff_path(project, line.substr(4U));
            ++files;
            old_path.clear();
            if (files > maximum_diff_files) {
                throw std::invalid_argument("diff touches too many files");
            }
        } else if (line.rfind("@@ ", 0U) == 0U ||
                   line.rfind("@@", 0U) == 0U) {
            ++hunks;
            if (hunks > maximum_diff_hunks) {
                throw std::invalid_argument("diff has too many hunks");
            }
        } else if (!line.empty() && line[0] == '+' &&
                   line.rfind("+++", 0U) != 0U) {
            ++additions;
        } else if (!line.empty() && line[0] == '-' &&
                   line.rfind("---", 0U) != 0U) {
            ++deletions;
        }
    }
    if (files == 0U || hunks == 0U || !old_path.empty()) {
        throw std::invalid_argument("unified diff is incomplete");
    }
    return "{\"projectId\":" + json_string(project_id) +
           ",\"files\":" + std::to_string(files) +
           ",\"hunks\":" + std::to_string(hunks) +
           ",\"additions\":" + std::to_string(additions) +
           ",\"deletions\":" + std::to_string(deletions) +
           ",\"appliesChanges\":false,\"requiresApproval\":true}";
}

// Returns the stable profile identifier used in secret aliases and generated
// connection metadata.
std::string ide_client_name(const IdeClientKind client) {
    return client == IdeClientKind::vscode ? "vscode" : "visual-studio";
}

// Parses only the two Phase 10 client profiles.
IdeClientKind parse_ide_client(const std::string& value) {
    if (value == "vscode") return IdeClientKind::vscode;
    if (value == "visual-studio") return IdeClientKind::visual_studio;
    throw std::invalid_argument("IDE client must be vscode or visual-studio");
}

// Names the OS-protected token without placing credentials in settings,
// command arguments, generated profiles, or workspace files.
std::string ide_secret_name(const IdeClientKind client) {
    return "ide-" + ide_client_name(client) + "-token";
}

// Generates a backend-neutral, secret-free profile. VS Code uses Agent-Coder's
// MCP client; Visual Studio launches the same native bridge/API contract.
std::string ide_connection_profile_json(
    const IdeClientKind client,
    const std::filesystem::path& masterai_executable,
    const std::filesystem::path& settings,
    const AppConfig& configuration) {
    if (masterai_executable.empty() || settings.empty()) {
        throw std::invalid_argument("IDE profile paths are required");
    }
    const auto name = ide_client_name(client);
    const std::string integration =
        client == IdeClientKind::vscode
            ? "Agent-Coder MCP client"
            : "Visual Studio native external-tool bridge";
    return "{\"schemaVersion\":1,\"client\":" + json_string(name) +
           ",\"integration\":" + json_string(integration) +
           ",\"command\":" +
           json_string(masterai_executable.string()) +
           ",\"arguments\":[\"mcp-stdio\"," +
           json_string(settings.string()) + "," + json_string(name) +
           "],\"apiBase\":" +
           json_string("http://127.0.0.1:" +
                       std::to_string(configuration.port) + "/api/v1") +
           ",\"mcpEndpoint\":" +
           json_string("http://127.0.0.1:" +
                       std::to_string(configuration.port) + "/mcp") +
           ",\"secretAlias\":" + json_string(ide_secret_name(client)) +
           ",\"containsCredential\":false,\"capabilities\":[\"chat\","
           "\"context\",\"diagnostics\",\"diff-preview\",\"cancellation\"]}";
}

// Reads one token without terminal echo when stdin is interactive and restores
// the original console state on every normal path.
std::string read_hidden_console_line(const std::string& prompt) {
    std::cerr << prompt << std::flush;
#if defined(_WIN32)
    const HANDLE input = GetStdHandle(STD_INPUT_HANDLE);
    DWORD original = 0U;
    const bool console =
        input != INVALID_HANDLE_VALUE && GetConsoleMode(input, &original) != 0;
    if (console) {
        SetConsoleMode(input, original & ~ENABLE_ECHO_INPUT);
    }
#else
    termios original{};
    const bool console =
        isatty(STDIN_FILENO) != 0 &&
        tcgetattr(STDIN_FILENO, &original) == 0;
    if (console) {
        termios hidden = original;
        hidden.c_lflag &= static_cast<tcflag_t>(~ECHO);
        tcsetattr(STDIN_FILENO, TCSAFLUSH, &hidden);
    }
#endif
    std::string value;
    const bool read = static_cast<bool>(std::getline(std::cin, value));
#if defined(_WIN32)
    if (console) SetConsoleMode(input, original);
#else
    if (console) tcsetattr(STDIN_FILENO, TCSAFLUSH, &original);
#endif
    std::cerr << '\n';
    if (!read) throw std::runtime_error("IDE token input ended");
    return value;
}

// Validates both MCP and IDE scopes, active user identity, and a nonempty
// project binding before committing the bearer token to the native OS store.
// The plaintext buffer is erased before returning.
ApiTokenStore::Token store_ide_token(
    const IdeClientKind client, std::string token, ApiTokenStore& tokens,
    UserStore& users, SecretStore& secrets, AuditLog& audit,
    const std::uint64_t now_epoch_seconds) {
    const auto mcp = tokens.validate(token, "mcp.connect", now_epoch_seconds);
    const auto ide = tokens.validate(token, "ide.connect", now_epoch_seconds);
    if (!mcp || !ide || mcp->user_id != ide->user_id ||
        mcp->project_ids.empty()) {
        std::fill(token.begin(), token.end(), '\0');
        throw std::invalid_argument(
            "IDE token lacks required scopes or project bindings");
    }
    const auto user = users.find_by_id(mcp->user_id);
    if (!user || !user->enabled || !secrets.available()) {
        std::fill(token.begin(), token.end(), '\0');
        throw std::runtime_error(
            "IDE identity or native secret protection is unavailable");
    }
    const auto secret = ide_secret_name(client);
    secrets.set(secret, token);
    std::fill(token.begin(), token.end(), '\0');
    try {
        audit.append("ide.token.store", user->id, "success",
                     ide_client_name(client));
    } catch (...) {
        secrets.erase(secret);
        throw;
    }
    return *mcp;
}

// Removes one local IDE credential and records the operation. Revoking the API
// token itself remains an authenticated server action.
void remove_ide_token(const IdeClientKind client, SecretStore& secrets,
                      AuditLog& audit, const std::string& actor_id) {
    secrets.erase(ide_secret_name(client));
    audit.append("ide.token.remove", actor_id, "success",
                 ide_client_name(client));
}

}  // namespace masterai
