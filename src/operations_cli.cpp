// MasterAI offline lifecycle command-line operations.
//
// This source unit performs strict argument parsing and delegates all backup,
// recovery, rotation, upgrade, and rollback behavior to OperationsManager.
#include "operations_cli.hpp"
#include "masterai.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace masterai::operations_cli {
namespace {

// Loads the settings selected by an offline operation.
AppConfig load_settings(const char* path) {
    return ConfigurationManager::load(std::filesystem::path(path));
}

// Requires an exact argument count so trailing values cannot alter authority.
void require_arguments(const int actual, const int expected,
                       const char* usage) {
    if (actual != expected) {
        throw std::invalid_argument(std::string("Usage: ") + usage);
    }
}

}  // namespace

// Keeps the main dispatcher synchronized with every implemented operation.
bool recognizes(const char* command) noexcept {
    const std::string value = command == nullptr ? "" : command;
    return value == "backup" || value == "restore-backup" ||
           value == "rotate-secret" || value == "rotate-logs" ||
           value == "recover" || value == "upgrade" ||
           value == "rollback" || value == "runtime-root" ||
           value == "models-root";
}

// Dispatches one exact offline operation and returns success after its report.
int run(const int argc, char* argv[]) {
    const std::string command = argc >= 2 ? argv[1] : "";
    if (command == "runtime-root") {
        require_arguments(argc, 3, "masterai runtime-root <settings>");
        std::cout << load_settings(argv[2]).runtime_root.string() << "\n";
        return 0;
    }
    // Mirrors runtime-root above -- scripts (rehash.ps1 in particular) need
    // workspace.modelsRoot resolved the same way the server itself resolves
    // it (relative to the settings file's own directory, see
    // ConfigurationManager::load's resolve_workspace_path), not relative to
    // whatever directory a script assumes the project lives in.
    if (command == "models-root") {
        require_arguments(argc, 3, "masterai models-root <settings>");
        std::cout << load_settings(argv[2]).models_root.string() << "\n";
        return 0;
    }
    if (command == "backup") {
        require_arguments(argc, 3, "masterai backup <settings>");
        const auto configuration = load_settings(argv[2]);
        const auto report =
            OperationsManager(configuration.runtime_root)
                .create_backup(argv[2],
                               configuration.runtime_root / "backups");
        std::cout << "Backup: " << report.backup_path.string()
                  << "\nFiles: " << report.file_count
                  << "\nBytes: " << report.total_bytes
                  << "\nManifest SHA-256: " << report.manifest_sha256
                  << "\n";
        return 0;
    }
    if (command == "restore-backup") {
        require_arguments(
            argc, 5,
            "masterai restore-backup <backup> <settings-destination> "
            "<runtime-destination>");
        const auto report = OperationsManager::restore_backup(
            argv[2], argv[3], argv[4]);
        std::cout << "Restored settings: " << report.settings_path.string()
                  << "\nRestored runtime: " << report.runtime_root.string()
                  << "\nFiles: " << report.file_count
                  << "\nBytes: " << report.total_bytes << "\n";
        return 0;
    }
    if (command == "rotate-secret") {
        require_arguments(
            argc, 4,
            "masterai rotate-secret <settings> <secret-alias>");
        const auto configuration = load_settings(argv[2]);
        auto secret = OperationsManager(configuration.runtime_root)
                          .rotate_secret(argv[3], "offline-cli");
        std::fill(secret.begin(), secret.end(), '\0');
        std::cout << "Secret rotated in native OS-protected storage: "
                  << argv[3] << "\n";
        return 0;
    }
    if (command == "rotate-logs") {
        require_arguments(
            argc, 5,
            "masterai rotate-logs <settings> <maximum-bytes> "
            "<retained-files>");
        const auto configuration = load_settings(argv[2]);
        const auto count = OperationsManager(configuration.runtime_root)
                               .rotate_logs(std::stoull(argv[3]),
                                            std::stoull(argv[4]),
                                            "offline-cli");
        std::cout << "Operational logs rotated: " << count << "\n";
        return 0;
    }
    if (command == "recover") {
        require_arguments(argc, 3, "masterai recover <settings>");
        const auto configuration = load_settings(argv[2]);
        const auto report = OperationsManager(configuration.runtime_root)
                                .recover("offline-cli");
        std::cout << "Record store validated: "
                  << (report.record_store_validated ? "yes" : "no")
                  << "\nStale stop request removed: "
                  << (report.stale_stop_request_removed ? "yes" : "no")
                  << "\nStale checkpoint removed: "
                  << (report.stale_checkpoint_removed ? "yes" : "no")
                  << "\n";
        return 0;
    }
    if (command == "upgrade") {
        require_arguments(
            argc, 6,
            "masterai upgrade <settings> <active-executable> "
            "<candidate-executable> <rollback-root>");
        const auto configuration = load_settings(argv[2]);
        const auto report = OperationsManager(configuration.runtime_root)
                                .install_upgrade(
                                    argv[3], argv[4], argv[5],
                                    "offline-cli");
        std::cout << "Upgrade receipt: " << report.receipt_path.string()
                  << "\nPrevious SHA-256: " << report.previous_sha256
                  << "\nInstalled SHA-256: " << report.installed_sha256
                  << "\n";
        return 0;
    }
    if (command == "rollback") {
        require_arguments(
            argc, 5,
            "masterai rollback <settings> <active-executable> <receipt>");
        const auto configuration = load_settings(argv[2]);
        const auto report = OperationsManager(configuration.runtime_root)
                                .rollback_upgrade(
                                    argv[3], argv[4], "offline-cli");
        std::cout << "Rollback receipt: " << report.receipt_path.string()
                  << "\nRemoved SHA-256: " << report.previous_sha256
                  << "\nRestored SHA-256: " << report.installed_sha256
                  << "\n";
        return 0;
    }
    throw std::invalid_argument("unknown operations command");
}

}  // namespace masterai::operations_cli
