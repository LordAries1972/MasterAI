// MasterAI command-line control plane.
//
// This unit parses administrative commands, loads validated configuration, and
// delegates work to native C++17 services. It keeps credentials out of command
// arguments and exposes MCP stdio as a clean protocol-only stdout process for
// IDE hosts such as Agent-Coder.
#include "masterai.hpp"
#include "operations_cli.hpp"
#include "performance_cli.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

std::atomic_bool stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true);
}

const char* model_state_name(const masterai::ModelState state) noexcept {
    switch (state) {
        case masterai::ModelState::discovered: return "discovered";
        case masterai::ModelState::valid: return "valid";
        case masterai::ModelState::invalid: return "invalid";
        case masterai::ModelState::unverified: return "unverified";
        case masterai::ModelState::ready: return "ready";
        case masterai::ModelState::loading: return "loading";
        case masterai::ModelState::loaded: return "loaded";
        case masterai::ModelState::failed: return "failed";
        case masterai::ModelState::quarantined: return "quarantined";
    }
    return "unknown";
}

// Prints only supported commands and their exact argument contracts so scripts
// and operators are not directed toward planned or backend-specific behavior.
void show_usage() {
    std::cout
        << "MasterAI development foundation through Phase 12 (ISO C++17)\n"
        << "Usage:\n"
        << "  masterai configure [settings-file]\n"
        << "  masterai serve [settings-file]\n"
        << "  masterai probe [storage-root]\n"
        << "  masterai scan-models [models-root]\n"
        << "  masterai download-model <settings> <url> <revision> <sha256> "
           "<category> <model-id> <filename> --accept-license\n"
        << "  masterai benchmark-model <settings> <model-id> "
           "<quick|standard|extended>\n"
        << "  masterai mcp-stdio [settings-file] "
           "[vscode|visual-studio]\n"
        << "  masterai ide-token-store <settings-file> "
           "<vscode|visual-studio>\n"
        << "  masterai ide-profile <settings-file> "
           "<vscode|visual-studio>\n"
        << "  masterai backup <settings-file>\n"
        << "  masterai restore-backup <backup> <settings-destination> "
           "<runtime-destination>\n"
        << "  masterai rotate-secret <settings> <secret-alias>\n"
        << "  masterai rotate-logs <settings> <maximum-bytes> "
           "<retained-files>\n"
        << "  masterai recover <settings>\n"
        << "  masterai runtime-root <settings>\n"
        << "  masterai upgrade <settings> <active> <candidate> "
           "<rollback-root>\n"
        << "  masterai rollback <settings> <active> <receipt>\n"
        << "  masterai performance-probe [iterations]\n"
        << "  masterai security-status [runtime-root]\n";
}

std::map<std::string, std::string> supported_environment() {
    std::map<std::string, std::string> values;
    for (const auto* name : {"MASTERAI_HOST", "MASTERAI_PORT",
                             "MASTERAI_ALLOW_INTRANET",
                             "MASTERAI_RUNTIME_ROOT", "MASTERAI_MODELS_ROOT",
                             "MASTERAI_LLAMA_SERVER", "MASTERAI_CURL"}) {
        const char* value = std::getenv(name);
        if (value != nullptr) values.emplace(name, value);
    }
    return values;
}

std::string prompt(const std::string& label, const std::string& fallback) {
    std::cout << label << " [" << fallback << "]: " << std::flush;
    std::string value;
    if (!std::getline(std::cin, value)) {
        throw std::runtime_error("configuration wizard input ended");
    }
    return value.empty() ? fallback : value;
}

void configure(const std::filesystem::path& settings) {
    auto configuration = masterai::ConfigurationManager::safe_defaults();
    if (std::filesystem::exists(settings)) {
        configuration = masterai::ConfigurationManager::load(settings);
        const auto choice = prompt(
            "Configuration exists. Enter U to update, R to reset, or C to cancel",
            "C");
        if (choice == "R" || choice == "r") {
            std::cout << "Reset replaces server settings. Runtime records, models, "
                         "and backups are preserved.\n";
            if (prompt("Type RESET to confirm", "CANCEL") != "RESET") {
                throw std::runtime_error("configuration reset cancelled");
            }
            configuration = masterai::ConfigurationManager::safe_defaults();
        } else if (choice != "U" && choice != "u") {
            throw std::runtime_error("configuration update cancelled");
        }
    }
    std::cout
        << "MasterAI first-run configuration\n"
        << "Release 1 binds directly to 127.0.0.1 only. Authentication remains required.\n";
    configuration.port = static_cast<std::uint16_t>(
        std::stoul(prompt("Loopback port", std::to_string(configuration.port))));
    configuration.runtime_root =
        prompt("Runtime data directory", configuration.runtime_root.string());
    configuration.models_root =
        prompt("Model directory", configuration.models_root.string());
    configuration.llama_server_executable =
        prompt("Approved llama.cpp server executable (blank disables inference)",
               configuration.llama_server_executable.string());
    configuration.curl_executable =
        prompt("Approved curl executable (blank disables downloads)",
               configuration.curl_executable.string());
    configuration.allow_local_password_accounts =
        prompt("Allow locally stored password accounts in addition to OS "
               "sign-in? (y/N)",
               configuration.allow_local_password_accounts ? "y" : "N") == "y";
    masterai::ConfigurationManager::save_atomic(configuration, settings);
    std::cout << "Validated configuration saved to " << settings.string() << "\n";
}

}  // namespace

// Dispatches one CLI operation and converts all exceptions into sanitized,
// structured logs and a nonzero process status.
int main(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            show_usage();
            return 0;
        }
        const std::string command = argv[1];
        if (masterai::operations_cli::recognizes(argv[1])) {
            return masterai::operations_cli::run(argc, argv);
        }
        if (command == "performance-probe") {
            return masterai::performance_cli::run(argc, argv);
        }
        if (command == "configure") {
            configure(argc >= 3 ? argv[2]
                                : std::filesystem::path("config/settings.json"));
            return 0;
        }
        if (command == "serve") {
            const std::filesystem::path settings =
                argc >= 3 ? argv[2] : std::filesystem::path("config/settings.json");
            if (!std::filesystem::is_regular_file(settings)) {
                throw std::runtime_error(
                    "configuration is missing; run 'masterai configure' from "
                    "an interactive terminal before service startup");
            }
            const auto configuration = masterai::ConfigurationManager::load(
                settings, supported_environment());
            std::signal(SIGINT, handle_signal);
            std::signal(SIGTERM, handle_signal);
            masterai::HttpServer server(configuration);
            return server.run(stop_requested) ? 0 : 1;
        }
        if (command == "mcp-stdio") {
            if (argc > 4) {
                throw std::runtime_error(
                    "mcp-stdio accepts settings and one IDE client");
            }
            const std::filesystem::path settings =
                argc >= 3 ? argv[2]
                          : std::filesystem::path("config/settings.json");
            const auto ide_client = masterai::parse_ide_client(
                argc >= 4 ? argv[3] : "vscode");
            const auto configuration =
                masterai::ConfigurationManager::load(settings);
            masterai::RecordStore records(
                configuration.runtime_root / "database");
            records.open();
            masterai::SecretStore secrets(
                configuration.runtime_root / "secrets");
            auto stored_token =
                secrets.get(masterai::ide_secret_name(ide_client));
            if (!stored_token) {
                throw std::runtime_error(
                    "secure IDE MCP token is missing; run ide-token-store "
                    "before starting stdio");
            }
            masterai::ApiTokenStore api_tokens(records);
            const auto authorized = api_tokens.validate(
                *stored_token, "mcp.connect",
                static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count()));
            std::fill(stored_token->begin(), stored_token->end(), '\0');
            stored_token.reset();
            if (!authorized) {
                throw std::runtime_error(
                    "secure IDE MCP token is expired, revoked, or lacks "
                    "mcp.connect");
            }
            masterai::UserStore users(records);
            const auto user = users.find_by_id(authorized->user_id);
            if (!user || !user->enabled) {
                throw std::runtime_error(
                    "secure IDE MCP identity is disabled or unavailable");
            }
            const auto projects_root =
                configuration.runtime_root / "projects";
            std::filesystem::create_directories(projects_root);
            masterai::ProjectCatalog projects(projects_root, records);
            masterai::McpInboundServer mcp(
                projects, configuration.models_root,
                configuration.memory_reserve_mib);
            std::signal(SIGINT, handle_signal);
            std::signal(SIGTERM, handle_signal);
            return masterai::run_mcp_stdio(
                mcp,
                {authorized->user_id, authorized->scopes,
                 authorized->project_ids},
                std::cin, std::cout, std::cerr, stop_requested);
        }
        if (command == "ide-token-store") {
            if (argc != 4) {
                throw std::runtime_error(
                    "ide-token-store requires settings and IDE client");
            }
            const auto ide_client = masterai::parse_ide_client(argv[3]);
            const auto configuration =
                masterai::ConfigurationManager::load(argv[2]);
            masterai::RecordStore records(
                configuration.runtime_root / "database");
            records.open();
            masterai::ApiTokenStore api_tokens(records);
            masterai::UserStore users(records);
            masterai::SecretStore secrets(
                configuration.runtime_root / "secrets");
            masterai::AuditLog audit(
                configuration.runtime_root / "audit" / "audit.log");
            auto token = masterai::read_hidden_console_line(
                "Paste scoped IDE token (input hidden): ");
            const auto now = static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count());
            const auto authority = masterai::store_ide_token(
                ide_client, std::move(token), api_tokens, users, secrets,
                audit, now);
            std::cout << "Stored " << masterai::ide_client_name(ide_client)
                      << " token in native OS protection for user "
                      << authority.user_id << " with "
                      << authority.project_ids.size()
                      << " project binding(s).\n";
            return 0;
        }
        if (command == "ide-profile") {
            if (argc != 4) {
                throw std::runtime_error(
                    "ide-profile requires settings and IDE client");
            }
            const std::filesystem::path settings(argv[2]);
            const auto configuration =
                masterai::ConfigurationManager::load(settings);
            std::error_code error;
            auto executable =
                std::filesystem::weakly_canonical(argv[0], error);
            if (error) executable = std::filesystem::absolute(argv[0]);
            auto canonical_settings =
                std::filesystem::weakly_canonical(settings, error);
            if (error) canonical_settings = std::filesystem::absolute(settings);
            std::cout << masterai::ide_connection_profile_json(
                             masterai::parse_ide_client(argv[3]), executable,
                             canonical_settings, configuration)
                      << '\n';
            return 0;
        }
        if (command == "probe") {
            const std::filesystem::path root =
                argc >= 3 ? argv[2] : std::filesystem::current_path();
            const auto hardware = masterai::probe_hardware(root);
            std::cout << "{\"platform\":\"" << hardware.platform
                      << "\",\"architecture\":\"" << hardware.architecture
                      << "\",\"logicalCpuCount\":" << hardware.logical_cpu_count
                      << ",\"totalRamMiB\":" << hardware.total_ram_mib
                      << ",\"availableRamMiB\":" << hardware.available_ram_mib
                      << ",\"freeDiskMiB\":" << hardware.free_disk_mib << "}\n";
            return 0;
        }
        if (command == "scan-models") {
            const std::filesystem::path root =
                argc >= 3 ? argv[2] : std::filesystem::path("models");
            const auto hardware = masterai::probe_hardware(root);
            const auto models =
                masterai::ModelRegistry(root, hardware, 2048U).scan();
            for (const auto& model : models) {
                std::cout << model.directory.string() << '\t'
                          << model_state_name(model.state) << '\t'
                          << model.diagnostic << '\n';
            }
            std::cout << "Models discovered: " << models.size() << '\n';
            return 0;
        }
        if (command == "download-model") {
            if (argc != 10 || std::string(argv[9]) != "--accept-license") {
                throw std::runtime_error(
                    "download-model requires settings, URL, immutable revision, "
                    "SHA-256, category, model ID, filename, and --accept-license");
            }
            const auto configuration =
                masterai::ConfigurationManager::load(argv[2]);
            if (configuration.curl_executable.empty()) {
                throw std::runtime_error(
                    "downloads.curlExecutable is not configured");
            }
            const std::filesystem::path category(argv[6]);
            const std::filesystem::path model_id(argv[7]);
            const std::filesystem::path filename(argv[8]);
            static const std::set<std::string> categories{
                "general-programming", "code-completion", "code-review",
                "debugging", "documentation", "embeddings-code-search"};
            if (filename.filename() != filename) {
                throw std::runtime_error("download filename must be a leaf name");
            }
            if (category.filename() != category ||
                model_id.filename() != model_id ||
                categories.find(category.string()) == categories.end()) {
                throw std::runtime_error(
                    "download category or model ID is invalid");
            }
            masterai::RecordStore records(configuration.runtime_root / "database");
            records.open();
            masterai::DownloadManager downloads(
                configuration.curl_executable, configuration.models_root,
                records);
            const auto job = downloads.create(
                {argv[3], argv[4], argv[5],
                 configuration.models_root / category / model_id / filename,
                 true});
            std::cout << "Download job " << job.id()
                      << " queued; existing partial data will be resumed.\n";
            const auto result = downloads.run(job.id(), stop_requested);
            std::cout << "Completed bytes: " << result.completed_bytes()
                      << "\nState: "
                      << (result.state() == masterai::DownloadState::complete
                              ? "complete"
                              : (result.state() ==
                                         masterai::DownloadState::quarantined
                                     ? "quarantined"
                                     : "failed"))
                      << "\n";
            return result.state() == masterai::DownloadState::complete ? 0 : 1;
        }
        if (command == "benchmark-model") {
            if (argc != 5) {
                throw std::runtime_error(
                    "benchmark-model requires settings, model ID, and profile");
            }
            const auto configuration =
                masterai::ConfigurationManager::load(argv[2]);
            if (configuration.llama_server_executable.empty()) {
                throw std::runtime_error(
                    "inference.llamaServerExecutable is not configured");
            }
            const std::string profile_text(argv[4]);
            const auto profile =
                profile_text == "quick"
                    ? masterai::BenchmarkProfile::quick
                    : (profile_text == "standard"
                           ? masterai::BenchmarkProfile::standard
                           : (profile_text == "extended"
                                  ? masterai::BenchmarkProfile::extended
                                  : throw std::runtime_error(
                                        "benchmark profile is invalid")));
            const auto hardware =
                masterai::probe_hardware(configuration.models_root);
            const auto models =
                masterai::ModelRegistry(configuration.models_root, hardware,
                                        configuration.memory_reserve_mib)
                    .scan();
            const auto found = std::find_if(
                models.begin(), models.end(), [&](const auto& model) {
                    return model.manifest.id == argv[3];
                });
            if (found == models.end()) {
                throw std::runtime_error("benchmark model was not found");
            }
            masterai::RunnerSupervisor inference(
                configuration.llama_server_executable,
                configuration.runtime_root);
            inference.load(*found, 4096U, configuration.runner_port);
            masterai::RecordStore records(configuration.runtime_root / "database");
            records.open();
            masterai::BenchmarkStore store(records);
            masterai::BenchmarkRunner runner(inference, store);
            const auto hardware_id = hardware.platform + "-" +
                                     hardware.architecture + "-" +
                                     std::to_string(hardware.logical_cpu_count);
            const auto record = runner.run(
                argv[3], "llama.cpp-b10156", "masterai-0.1.0", hardware_id,
                profile, stop_requested);
            inference.unload();
            std::cout << "Benchmark prompt suite: "
                      << record.prompt_suite_hash
                      << "\nCases passed: " << record.passed_cases << "/"
                      << record.total_cases << "\nGenerated tokens: "
                      << record.generated_tokens << "\nElapsed microseconds: "
                      << record.elapsed_microseconds << "\n";
            return 0;
        }
        if (command == "security-status") {
            const masterai::OsIdentityProvider identity;
            const auto runtime =
                argc >= 3 ? std::filesystem::path(argv[2])
                          : std::filesystem::path("runtime");
            const masterai::SecretStore secrets(runtime / "secrets");
            std::cout << "C++ standard: 17\n"
                      << "Native OS identity provider: "
                      << (identity.available() ? "available" : "unavailable (fail-closed)")
                      << "\nPassword storage: prohibited"
                      << "\nNon-password OS secret store: "
                      << (secrets.available() ? "available" : "unavailable (fail-closed)")
                      << "\nListener policy: loopback-only\n";
            return identity.available() && secrets.available() ? 0 : 2;
        }
        show_usage();
        return 2;
    } catch (const std::exception& exception) {
        masterai::log(masterai::LogLevel::error, "application.failure",
                      exception.what());
        return 1;
    }
}
