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
#include <iomanip>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

std::atomic_bool stop_requested{false};

void handle_signal(int) {
    stop_requested.store(true);
}

void raise_atomic_maximum(std::atomic<std::uint64_t>& target,
                          const std::uint64_t candidate) {
    std::uint64_t observed = target.load();
    while (candidate > observed &&
          !target.compare_exchange_weak(observed, candidate)) {
    }
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
        case masterai::ModelState::downloading: return "downloading";
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
        << "  masterai verify-models [models-root]\n"
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
        << "  masterai models-root <settings>\n"
        << "  masterai upgrade <settings> <active> <candidate> "
           "<rollback-root>\n"
        << "  masterai rollback <settings> <active> <receipt>\n"
        << "  masterai performance-probe [iterations]\n"
        << "  masterai index-probe <project-root> [index-root]\n"
        << "  masterai calibrate <settings> <model-id> "
           "<auto|minimal|balanced|performance>\n"
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
    // A relative runtimeRoot/modelsRoot resolves against this settings
    // file's own directory (see ConfigurationManager::load), not whatever
    // directory `masterai configure` happens to be run from -- these two
    // levels up keep a fresh config's data siblings of `settings`'s parent
    // directory (e.g. config/settings.json -> ../runtime, ../models) instead
    // of nesting it inside that directory.
    configuration.runtime_root = "../runtime";
    configuration.models_root = "../models";
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
        prompt("Allow locally stored password accounts? (Y/n)",
               configuration.allow_local_password_accounts ? "y" : "n") != "n";
    configuration.allow_os_identity_accounts =
        prompt("Also allow OS-integrated (Windows/PAM) sign-in accounts? "
               "(y/N)",
               configuration.allow_os_identity_accounts ? "y" : "N") == "y";
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
            masterai::HttpServer server(configuration, settings);
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
        if (command == "verify-models") {
            // The one place model files are actually hashed: scan() (used by
            // every inventory/chat/download request) instead trusts the
            // cache this command writes, so a page load never blocks on
            // gigabytes of SHA-256 work. Run this after adding or changing
            // models to confirm them and make them selectable again.
            const std::filesystem::path root =
                argc >= 3 ? argv[2] : std::filesystem::path("models");
            const auto hardware = masterai::probe_hardware(root);
            std::size_t total = 0U;
            std::size_t failed = 0U;
            // Tracks which model's hash progress was last printed so a
            // model's first progress line can announce it's starting
            // (nothing else prints between models otherwise, which reads as
            // the command having hung on a large file).
            std::string hashing_id;
            const auto verified_count = masterai::ModelRegistry(root, hardware, 2048U)
                .verify(
                    [&](const std::string& id, const bool ok,
                        const std::string& message) {
                        ++total;
                        if (ok) {
                            std::cout << "[" << total << "] " << id << " ... OK\n";
                        } else {
                            ++failed;
                            std::cout << "[" << total << "] " << id
                                      << " ... FAILED: " << message << '\n';
                        }
                    },
                    [&](const std::string& id, const std::uint64_t bytes_hashed,
                        const std::uint64_t total_bytes) {
                        if (id != hashing_id) {
                            hashing_id = id;
                            std::cout << "    hashing " << id << " ("
                                      << (total_bytes / (1024U * 1024U))
                                      << " MB)...\n";
                        }
                        const auto percent = total_bytes == 0U
                            ? 100U
                            : static_cast<unsigned int>(
                                  (bytes_hashed * 100U) / total_bytes);
                        std::cout << "    " << id << ": " << percent << "% ("
                                  << (bytes_hashed / (1024U * 1024U)) << " / "
                                  << (total_bytes / (1024U * 1024U)) << " MB)\n";
                    });
            std::cout << "Verified " << verified_count << " model(s), "
                      << failed << " failed, out of " << total
                      << " checked.\n";
            return failed == 0U ? 0 : 1;
        }
        if (command == "index-probe") {
            // Phase 15's representative large-project ceiling measurement:
            // drives the real ProjectIndexer (the same class the running
            // service's worker thread uses) through a full rebuild followed
            // by a one-file incremental update over an actual project tree,
            // and reports elapsed time, disk bytes, and this process's
            // resident-memory delta/peak so the results are directly
            // comparable to the configured memory ceiling.
            if (argc < 3) {
                throw std::runtime_error(
                    "index-probe requires a project root and accepts an "
                    "optional scratch index-root");
            }
            const std::filesystem::path project_root(argv[2]);
            if (!std::filesystem::is_directory(project_root)) {
                throw std::runtime_error("project root is not a directory");
            }
            const std::filesystem::path index_root =
                argc >= 4 ? std::filesystem::path(argv[3])
                          : std::filesystem::temp_directory_path() /
                                "masterai-index-probe";
            std::filesystem::remove_all(index_root);
            const auto hardware = masterai::probe_hardware(project_root);
            auto policy = masterai::MemoryBudgetManager::policy_for(
                masterai::ResourceProfile::balanced, hardware);
            masterai::MemoryBudgetManager memory(policy, hardware);
            masterai::ProjectRecord project{"index-probe", "index-probe",
                                            project_root};
            masterai::ProjectIndexer index(project, index_root, memory);
            std::atomic_bool cancellation{false};

            std::atomic_bool sampling{true};
            std::atomic<std::uint64_t> peak_resident{0};
            const auto before = masterai::probe_process_resources();
            std::thread sampler([&]() {
                while (sampling.load()) {
                    raise_atomic_maximum(
                        peak_resident,
                        masterai::probe_process_resources()
                            .resident_memory_bytes);
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(50));
                }
            });
            const auto start = std::chrono::steady_clock::now();
            const auto rebuild_result = index.rebuild(cancellation, 0U);
            const auto rebuild_elapsed = std::chrono::steady_clock::now() - start;

            std::filesystem::path touched;
            for (const auto& entry :
                 std::filesystem::recursive_directory_iterator(project_root)) {
                if (entry.is_regular_file()) {
                    touched = entry.path();
                    break;
                }
            }
            std::chrono::steady_clock::duration update_elapsed{};
            masterai::IndexStatus update_result;
            if (!touched.empty()) {
                const auto update_start = std::chrono::steady_clock::now();
                update_result = index.update({touched}, cancellation);
                update_elapsed =
                    std::chrono::steady_clock::now() - update_start;
            }
            sampling.store(false);
            sampler.join();
            const auto after = masterai::probe_process_resources();

            const auto rebuild_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    rebuild_elapsed)
                    .count();
            const auto update_ms =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    update_elapsed)
                    .count();
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "filesDiscovered\t" << rebuild_result.files_discovered << '\n'
                      << "filesIndexed\t" << rebuild_result.files_indexed << '\n'
                      << "chunks\t" << rebuild_result.chunks << '\n'
                      << "diskBytes\t" << rebuild_result.disk_bytes << '\n'
                      << "rebuildElapsedMs\t" << rebuild_ms << '\n'
                      << "incrementalUpdateElapsedMs\t" << update_ms << '\n'
                      << "residentBeforeBytes\t" << before.resident_memory_bytes << '\n'
                      << "residentAfterBytes\t" << after.resident_memory_bytes << '\n'
                      << "residentPeakBytes\t" << peak_resident.load() << '\n'
                      << "residentDeltaBytes\t"
                      << (after.resident_memory_bytes >
                                  before.resident_memory_bytes
                              ? after.resident_memory_bytes -
                                    before.resident_memory_bytes
                              : 0U)
                      << '\n'
                      << "memoryHardLimitBytes\t" << policy.hard_limit_bytes
                      << '\n';
            std::filesystem::remove_all(index_root);
            return 0;
        }
        if (command == "download-model") {
            if (argc != 17 || std::string(argv[16]) != "--accept-license") {
                throw std::runtime_error(
                    "download-model requires settings, URL, immutable revision, "
                    "SHA-256, category, model ID, filename, display name, "
                    "architecture, quantization, license SPDX ID, size in bytes, "
                    "minimum RAM MiB, recommended RAM MiB, and --accept-license");
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
            const std::string display_name(argv[9]);
            const std::string architecture(argv[10]);
            const std::string quantization(argv[11]);
            const std::string license_spdx(argv[12]);
            static const std::set<std::string> categories{
                "general-programming", "code-completion", "code-review",
                "debugging", "documentation", "embeddings-code-search",
                "conversation"};
            static const std::set<std::string> licenses{
                "Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause",
                "CC-BY-4.0", "Llama-3.1", "Llama-3.2", "Gemma"};
            if (filename.filename() != filename) {
                throw std::runtime_error("download filename must be a leaf name");
            }
            if (category.filename() != category ||
                model_id.filename() != model_id ||
                categories.find(category.string()) == categories.end()) {
                throw std::runtime_error(
                    "download category or model ID is invalid");
            }
            const auto is_safe_identifier = [](const std::string& value) {
                if (value.empty() || value.size() > 96U || value.front() == '.' ||
                    value.back() == '.') {
                    return false;
                }
                return std::all_of(value.begin(), value.end(), [](const char c) {
                    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                           (c >= '0' && c <= '9') || c == '-' || c == '_' ||
                           c == '.';
                });
            };
            if (display_name.empty() || display_name.size() > 160U ||
                !is_safe_identifier(architecture) ||
                !is_safe_identifier(quantization) ||
                licenses.find(license_spdx) == licenses.end()) {
                throw std::runtime_error(
                    "download display name, architecture, quantization, or "
                    "license SPDX ID is invalid");
            }
            const auto size_bytes = std::stoull(argv[13]);
            const auto minimum_ram_mib = std::stoull(argv[14]);
            const auto recommended_ram_mib = std::stoull(argv[15]);
            if (size_bytes == 0U || minimum_ram_mib == 0U ||
                recommended_ram_mib < minimum_ram_mib) {
                throw std::runtime_error(
                    "download size or RAM recommendation is invalid");
            }
            const auto model_directory =
                configuration.models_root / category / model_id;
            // Written now, before the transfer runs, exactly like the HTTP
            // download route -- without this, ModelRegistry::scan() never
            // recognizes the fetched file as a model at all, so it could
            // never reach Ready and nothing could actually chat with it.
            masterai::write_model_manifest(
                model_directory, model_id.string(), display_name,
                category.string(), architecture, quantization,
                minimum_ram_mib, recommended_ram_mib, filename.string(),
                size_bytes, argv[5], argv[3], argv[4], license_spdx);
            masterai::RecordStore records(configuration.runtime_root / "database");
            records.open();
            masterai::DownloadManager downloads(
                configuration.curl_executable, configuration.models_root,
                records);
            const auto job = downloads.create(
                {argv[3], argv[4], argv[5],
                 model_directory / filename, true, minimum_ram_mib,
                 recommended_ram_mib});
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
        if (command == "calibrate") {
            // Phase 19: real cold-load/prompt-evaluation/generation
            // measurement against a Ready model, mirroring benchmark-model
            // and index-probe's own real-fixture measurement pattern.
            // Persists a TuningProfile keyed by host/model/backend/build
            // identity so a later request for the same identity reuses it
            // instead of re-measuring.
            if (argc != 5) {
                throw std::runtime_error(
                    "calibrate requires settings, model ID, and a profile "
                    "(auto, minimal, balanced, or performance)");
            }
            const auto configuration =
                masterai::ConfigurationManager::load(argv[2]);
            if (configuration.llama_server_executable.empty()) {
                throw std::runtime_error(
                    "inference.llamaServerExecutable is not configured");
            }
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
                throw std::runtime_error("calibration model was not found");
            }
            masterai::RunnerSupervisor inference(
                configuration.llama_server_executable,
                configuration.runtime_root);
            masterai::RecordStore records(configuration.runtime_root / "database");
            records.open();
            masterai::TuningProfileStore store(records);
            const auto backend_hash =
                masterai::sha256_file_hex(configuration.llama_server_executable);
            masterai::CalibrationService calibration(
                inference, store, hardware, backend_hash, "masterai-0.1.0",
                configuration.accelerator_policy);
            const auto profile = calibration.calibrate(
                *found, argv[4], configuration.runner_port, stop_requested);
            std::cout << std::fixed << std::setprecision(2);
            std::cout << "hostHash\t" << profile.host_hash << '\n'
                      << "profileName\t" << profile.profile_name << '\n'
                      << "coldLoadMicroseconds\t"
                      << profile.cold_load_microseconds << '\n'
                      << "promptEvaluationMicroseconds\t"
                      << profile.prompt_evaluation_microseconds << '\n'
                      << "generationMicroseconds\t"
                      << profile.generation_microseconds << '\n'
                      << "peakResidentMemoryBytes\t"
                      << profile.peak_resident_memory_bytes << '\n'
                      << "peakCommitBytes\t" << profile.peak_commit_bytes << '\n'
                      << "pageFaults\t" << profile.page_faults << '\n'
                      << "averageCpuPercent\t" << profile.average_cpu_percent
                      << '\n'
                      << "diskReadBytes\t" << profile.disk_read_bytes << '\n'
                      << "diskWriteBytes\t" << profile.disk_write_bytes << '\n';
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
