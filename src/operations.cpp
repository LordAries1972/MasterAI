// MasterAI offline backup, restore, rotation, upgrade, and crash recovery.
//
// This source unit extends the existing RecordStore, SecretStore, and AuditLog
// boundaries. It never exports OS-bound secrets, follows project roots, or lets
// the running HTTP service replace its own executable.
#include "masterai.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace masterai {
namespace {

struct ManifestEntry final {
    std::filesystem::path relative_path;
    std::uint64_t size_bytes{0};
    std::string sha256;
};

// Produces a collision-resistant operation directory name without user input.
std::string operation_id() {
    const auto time = std::chrono::high_resolution_clock::now()
                          .time_since_epoch()
                          .count();
    const auto random = secure_random(8U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string suffix(random.size() * 2U, '0');
    for (std::size_t index = 0; index < random.size(); ++index) {
        suffix[index * 2U] = digits[random[index] >> 4U];
        suffix[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    return std::to_string(time) + "-" + suffix;
}

// Rejects operations that would race the script-managed service process.
void require_offline(const std::filesystem::path& runtime_root) {
    if (std::filesystem::exists(runtime_root / "run" / "masterai.pid")) {
        throw std::runtime_error(
            "offline operation refused while the service PID file exists");
    }
}

// Confirms a source is a normal file and not an indirection boundary.
void require_regular_file(const std::filesystem::path& path,
                          const char* description) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error) || error ||
        std::filesystem::is_symlink(path, error)) {
        throw std::invalid_argument(std::string(description) +
                                    " must be a regular non-symlink file");
    }
}

// Copies one file, then verifies its size and SHA-256 at the destination.
ManifestEntry copy_verified(const std::filesystem::path& source,
                            const std::filesystem::path& destination,
                            const std::filesystem::path& manifest_path) {
    require_regular_file(source, "backup source");
    std::filesystem::create_directories(destination.parent_path());
    std::filesystem::copy_file(
        source, destination, std::filesystem::copy_options::overwrite_existing);
    const auto expected_size = std::filesystem::file_size(source);
    const auto expected_hash = sha256_file_hex(source);
    if (std::filesystem::file_size(destination) != expected_size ||
        !constant_time_equal(sha256_file_hex(destination), expected_hash)) {
        throw std::runtime_error("verified file copy failed");
    }
    return {manifest_path, expected_size, expected_hash};
}

// Adds regular non-symlink files from one approved runtime subtree.
void copy_tree(const std::filesystem::path& source_root,
               const std::filesystem::path& target_root,
               const std::filesystem::path& manifest_prefix,
               std::vector<ManifestEntry>& entries) {
    if (!std::filesystem::exists(source_root)) return;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(
             source_root,
             std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end; iterator.increment(error)) {
        if (error) {
            throw std::runtime_error("backup runtime traversal failed");
        }
        if (iterator->is_symlink(error)) {
            if (iterator->is_directory(error)) iterator.disable_recursion_pending();
            continue;
        }
        if (!iterator->is_regular_file(error) || error) continue;
        const auto relative =
            std::filesystem::relative(iterator->path(), source_root, error);
        if (error || relative.empty() || relative.is_absolute()) {
            throw std::runtime_error("backup relative path failed");
        }
        const auto manifest_path = manifest_prefix / relative;
        entries.push_back(copy_verified(
            iterator->path(), target_root / manifest_path, manifest_path));
    }
}

// Serializes a deterministic manifest after sorting by portable path.
std::string serialize_manifest(std::vector<ManifestEntry> entries) {
    std::sort(entries.begin(), entries.end(),
              [](const ManifestEntry& left, const ManifestEntry& right) {
                  return left.relative_path.generic_string() <
                         right.relative_path.generic_string();
              });
    std::string manifest{"masterai-backup-v1\n"};
    for (const auto& entry : entries) {
        const auto path = entry.relative_path.generic_string();
        if (path.find('\t') != std::string::npos ||
            path.find('\n') != std::string::npos) {
            throw std::runtime_error("backup path cannot enter manifest");
        }
        manifest += entry.sha256 + "\t" +
                    std::to_string(entry.size_bytes) + "\t" + path + "\n";
    }
    return manifest;
}

// Writes a complete file and verifies the stream before publication.
void write_file(const std::filesystem::path& path,
                const std::string& content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(content.data(), static_cast<std::streamsize>(content.size()));
    output.flush();
    if (!output) throw std::runtime_error("operations file write failed");
}

// Parses a closed backup manifest and rejects unsafe or duplicate paths.
std::vector<ManifestEntry> parse_manifest(const std::string& manifest) {
    std::istringstream input(manifest);
    std::string line;
    if (!std::getline(input, line) || line != "masterai-backup-v1") {
        throw std::runtime_error("backup manifest revision is unsupported");
    }
    std::set<std::string> paths;
    std::vector<ManifestEntry> entries;
    // One line per entry, so a newline count gives an exact upfront capacity.
    entries.reserve(static_cast<std::size_t>(
        std::count(manifest.begin(), manifest.end(), '\n')));
    while (std::getline(input, line)) {
        if (line.empty()) continue;
        const auto first = line.find('\t');
        const auto second =
            first == std::string::npos ? first : line.find('\t', first + 1U);
        if (first != 64U || second == std::string::npos ||
            line.find('\t', second + 1U) != std::string::npos) {
            throw std::runtime_error("backup manifest entry is malformed");
        }
        ManifestEntry entry;
        entry.sha256 = line.substr(0U, first);
        entry.size_bytes =
            std::stoull(line.substr(first + 1U, second - first - 1U));
        const auto path_text = line.substr(second + 1U);
        entry.relative_path = std::filesystem::path(path_text);
        if (entry.relative_path.empty() ||
            entry.relative_path.is_absolute() ||
            path_text.find('\\') != std::string::npos ||
            !paths.insert(path_text).second) {
            throw std::runtime_error("backup manifest path is unsafe");
        }
        const auto normalized = entry.relative_path.lexically_normal();
        if (normalized != entry.relative_path ||
            normalized.begin() == normalized.end() ||
            *normalized.begin() == "..") {
            throw std::runtime_error("backup manifest path escapes its root");
        }
        entries.push_back(std::move(entry));
    }
    if (entries.empty()) {
        throw std::runtime_error("backup manifest has no files");
    }
    return entries;
}

// Reads a bounded operations metadata file.
std::string read_file(const std::filesystem::path& path,
                      const std::uint64_t maximum_bytes) {
    require_regular_file(path, "operations metadata");
    const auto size = std::filesystem::file_size(path);
    if (size > maximum_bytes) {
        throw std::runtime_error("operations metadata exceeds its limit");
    }
    std::ifstream input(path, std::ios::binary);
    std::string result(static_cast<std::size_t>(size), '\0');
    if (!result.empty()) {
        input.read(result.data(), static_cast<std::streamsize>(result.size()));
    }
    if (!input) throw std::runtime_error("operations metadata read failed");
    return result;
}

// Requires a destination to be absent or an existing empty directory.
void require_clean_directory(const std::filesystem::path& path) {
    if (!std::filesystem::exists(path)) return;
    if (!std::filesystem::is_directory(path) ||
        !std::filesystem::is_empty(path)) {
        throw std::invalid_argument("restore destination must be clean");
    }
}

// Replaces a file through a verified sibling temporary.
void atomic_replace(const std::filesystem::path& source,
                    const std::filesystem::path& destination) {
    const auto temporary =
        destination.parent_path() /
        (destination.filename().string() + ".operations.tmp");
    std::filesystem::copy_file(
        source, temporary, std::filesystem::copy_options::overwrite_existing);
    if (!constant_time_equal(sha256_file_hex(source),
                             sha256_file_hex(temporary))) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic replacement staging failed");
    }
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic executable replacement failed");
    }
#else
    std::filesystem::rename(temporary, destination);
#endif
}

// Splits one receipt line with an exact field count.
std::vector<std::string> split_tabs(const std::string& line) {
    std::vector<std::string> fields;
    // Field count is exactly the tab count plus one; reserve avoids growth.
    fields.reserve(
        static_cast<std::size_t>(std::count(line.begin(), line.end(), '\t')) +
        1U);
    std::size_t start = 0U;
    while (true) {
        const auto end = line.find('\t', start);
        fields.push_back(line.substr(start, end - start));
        if (end == std::string::npos) break;
        start = end + 1U;
    }
    return fields;
}

}  // namespace

// Captures the one runtime root used by all offline lifecycle operations.
OperationsManager::OperationsManager(std::filesystem::path runtime_root)
    : runtime_root_(std::move(runtime_root)) {
    if (runtime_root_.empty()) {
        throw std::invalid_argument("operations runtime root is required");
    }
}

// Checkpoints existing records and publishes a manifest-verified backup.
BackupReport OperationsManager::create_backup(
    const std::filesystem::path& settings_path,
    const std::filesystem::path& backup_root) const {
    require_offline(runtime_root_);
    static_cast<void>(ConfigurationManager::load(settings_path));
    RecordStore records(runtime_root_ / "database");
    records.open();
    records.checkpoint();

    const auto id = operation_id();
    const auto staging = backup_root / (".backup-" + id + ".tmp");
    const auto target = backup_root / ("backup-" + id);
    if (std::filesystem::exists(staging) ||
        std::filesystem::exists(target)) {
        throw std::runtime_error("backup operation directory already exists");
    }
    std::filesystem::create_directories(staging);
    std::vector<ManifestEntry> entries;
    entries.push_back(copy_verified(
        settings_path, staging / "settings" / "settings.json",
        "settings/settings.json"));
    for (const auto* name : {"database", "audit", "attachments"}) {
        copy_tree(runtime_root_ / name, staging, std::filesystem::path("runtime") /
                      name, entries);
    }
    const auto manifest = serialize_manifest(entries);
    write_file(staging / "manifest.tsv", manifest);
    write_file(staging / "manifest.sha256", sha256_hex(manifest) + "\n");
    std::filesystem::create_directories(backup_root);
    std::filesystem::rename(staging, target);

    BackupReport report;
    report.backup_path = target;
    report.file_count = static_cast<std::uint64_t>(entries.size());
    for (const auto& entry : entries) {
        report.total_bytes += entry.size_bytes;
    }
    report.manifest_sha256 = sha256_hex(manifest);
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.backup", "offline-operator", "success",
                target.filename().string());
    return report;
}

// Verifies all source files before copying into clean staging destinations.
RestoreReport OperationsManager::restore_backup(
    const std::filesystem::path& backup_path,
    const std::filesystem::path& settings_destination,
    const std::filesystem::path& runtime_destination) {
    require_regular_file(backup_path / "manifest.tsv", "backup manifest");
    require_regular_file(backup_path / "manifest.sha256",
                         "backup manifest digest");
    const auto manifest = read_file(backup_path / "manifest.tsv", 4U * 1024U *
                                                                1024U);
    auto expected_manifest_hash =
        read_file(backup_path / "manifest.sha256", 128U);
    while (!expected_manifest_hash.empty() &&
           (expected_manifest_hash.back() == '\n' ||
            expected_manifest_hash.back() == '\r')) {
        expected_manifest_hash.pop_back();
    }
    if (!constant_time_equal(sha256_hex(manifest),
                             expected_manifest_hash)) {
        throw std::runtime_error("backup manifest digest mismatch");
    }
    const auto entries = parse_manifest(manifest);
    for (const auto& entry : entries) {
        const auto source = backup_path / entry.relative_path;
        if (!is_path_within(backup_path, source)) {
            throw std::runtime_error("backup entry escaped its root");
        }
        require_regular_file(source, "backup entry");
        if (std::filesystem::file_size(source) != entry.size_bytes ||
            !constant_time_equal(sha256_file_hex(source), entry.sha256)) {
            throw std::runtime_error("backup entry verification failed");
        }
    }
    if (std::filesystem::exists(settings_destination)) {
        throw std::invalid_argument(
            "restore settings destination must not exist");
    }
    require_clean_directory(runtime_destination);
    const auto staging = runtime_destination.parent_path() /
                         (runtime_destination.filename().string() +
                          ".restore-" + operation_id() + ".tmp");
    std::filesystem::create_directories(staging);
    std::uint64_t restored_files = 0U;
    std::uint64_t restored_bytes = 0U;
    for (const auto& entry : entries) {
        const auto relative = entry.relative_path.generic_string();
        if (relative == "settings/settings.json") {
            continue;
        }
        static const std::string prefix{"runtime/"};
        if (relative.rfind(prefix, 0U) != 0U) {
            throw std::runtime_error("backup contains an unknown root");
        }
        const auto destination =
            staging / std::filesystem::path(relative.substr(prefix.size()));
        static_cast<void>(copy_verified(
            backup_path / entry.relative_path, destination,
            entry.relative_path));
        ++restored_files;
        restored_bytes += entry.size_bytes;
    }
    if (std::filesystem::exists(runtime_destination)) {
        std::filesystem::remove(runtime_destination);
    }
    std::filesystem::rename(staging, runtime_destination);
    auto restored_configuration = ConfigurationManager::load(
        backup_path / "settings" / "settings.json");
    restored_configuration.runtime_root = runtime_destination;
    ConfigurationManager::save_atomic(restored_configuration,
                                      settings_destination);
    RecordStore validation(runtime_destination / "database");
    validation.open();
    validation.checkpoint();
    AuditLog(runtime_destination / "audit" / "audit.log")
        .append("operations.restore", "offline-operator", "success",
                backup_path.filename().string());
    return {runtime_destination, settings_destination,
            restored_files + 1U,
            restored_bytes +
                std::filesystem::file_size(settings_destination)};
}

// Replaces a named secret with 256 random bits and audits only its alias.
std::string OperationsManager::rotate_secret(
    const std::string& name, const std::string& actor_id) const {
    require_offline(runtime_root_);
    const auto random = secure_random(32U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string secret(random.size() * 2U, '0');
    for (std::size_t index = 0; index < random.size(); ++index) {
        secret[index * 2U] = digits[random[index] >> 4U];
        secret[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    SecretStore store(runtime_root_ / "secrets");
    store.set(name, secret);
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.secret.rotate", actor_id, "success", name);
    return secret;
}

// Rotates only operational logs; the tamper-evident audit chain is preserved.
std::uint64_t OperationsManager::rotate_logs(
    const std::uint64_t maximum_bytes,
    const std::uint64_t retained_files,
    const std::string& actor_id) const {
    require_offline(runtime_root_);
    if (maximum_bytes < 4096U || retained_files == 0U ||
        retained_files > 32U) {
        throw std::invalid_argument("log rotation bounds are invalid");
    }
    std::uint64_t rotations = 0U;
    for (const auto& active :
         {runtime_root_ / "logs" / "masterai.log",
          runtime_root_ / "run" / "masterai.log"}) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(active, error) || error ||
            std::filesystem::file_size(active, error) <= maximum_bytes ||
            error) {
            continue;
        }
        std::filesystem::remove(
            active.string() + "." + std::to_string(retained_files), error);
        for (std::uint64_t index = retained_files; index > 1U; --index) {
            const std::filesystem::path source =
                active.string() + "." + std::to_string(index - 1U);
            const std::filesystem::path destination =
                active.string() + "." + std::to_string(index);
            if (std::filesystem::exists(source)) {
                std::filesystem::rename(source, destination);
            }
        }
        std::filesystem::rename(active, active.string() + ".1");
        write_file(active, "");
        ++rotations;
    }
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.logs.rotate", actor_id, "success",
                std::to_string(rotations));
    return rotations;
}

// Saves the active executable, verifies the candidate, and atomically installs.
UpgradeReport OperationsManager::install_upgrade(
    const std::filesystem::path& active_executable,
    const std::filesystem::path& candidate_executable,
    const std::filesystem::path& rollback_root,
    const std::string& actor_id) const {
    require_offline(runtime_root_);
    require_regular_file(active_executable, "active executable");
    require_regular_file(candidate_executable, "candidate executable");
    if (std::filesystem::equivalent(active_executable,
                                    candidate_executable)) {
        throw std::invalid_argument("upgrade candidate is already active");
    }
    const auto previous_hash = sha256_file_hex(active_executable);
    const auto installed_hash = sha256_file_hex(candidate_executable);
    if (constant_time_equal(previous_hash, installed_hash)) {
        throw std::invalid_argument("upgrade candidate is unchanged");
    }
    const auto directory = rollback_root / ("upgrade-" + operation_id());
    std::filesystem::create_directories(directory);
    const auto previous = directory / "previous.bin";
    static_cast<void>(copy_verified(active_executable, previous,
                                    "previous.bin"));
    atomic_replace(candidate_executable, active_executable);
    if (!constant_time_equal(sha256_file_hex(active_executable),
                             installed_hash)) {
        atomic_replace(previous, active_executable);
        throw std::runtime_error("installed executable verification failed");
    }
    const auto receipt = directory / "receipt.tsv";
    write_file(receipt, "masterai-upgrade-v1\t" + previous_hash + "\t" +
                            installed_hash + "\tprevious.bin\n");
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.upgrade", actor_id, "success", installed_hash);
    return {receipt, previous_hash, installed_hash};
}

// Verifies receipt, active version, and rollback binary before replacement.
UpgradeReport OperationsManager::rollback_upgrade(
    const std::filesystem::path& active_executable,
    const std::filesystem::path& receipt_path,
    const std::string& actor_id) const {
    require_offline(runtime_root_);
    require_regular_file(active_executable, "active executable");
    const auto fields =
        split_tabs(read_file(receipt_path, 1024U));
    if (fields.size() != 4U || fields[0] != "masterai-upgrade-v1" ||
        fields[1].size() != 64U || fields[2].size() != 64U ||
        fields[3] != "previous.bin\n") {
        throw std::runtime_error("upgrade receipt is malformed");
    }
    const auto previous = receipt_path.parent_path() / "previous.bin";
    require_regular_file(previous, "rollback executable");
    if (!constant_time_equal(sha256_file_hex(active_executable), fields[2]) ||
        !constant_time_equal(sha256_file_hex(previous), fields[1])) {
        throw std::runtime_error("upgrade rollback hashes do not match");
    }
    atomic_replace(previous, active_executable);
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.rollback", actor_id, "success", fields[1]);
    return {receipt_path, fields[2], fields[1]};
}

// Removes known stale control files and reuses RecordStore crash recovery.
RecoveryReport OperationsManager::recover(
    const std::string& actor_id) const {
    require_offline(runtime_root_);
    RecoveryReport report;
    report.stale_stop_request_removed =
        std::filesystem::remove(runtime_root_ / "run" / "stop.request");
    report.stale_checkpoint_removed =
        std::filesystem::remove(
            runtime_root_ / "database" / "records.snapshot.tmp");
    RecordStore records(runtime_root_ / "database");
    records.open();
    records.checkpoint();
    report.record_store_validated = true;
    AuditLog(runtime_root_ / "audit" / "audit.log")
        .append("operations.recover", actor_id, "success",
                report.stale_stop_request_removed ? "stale-stop-cleared"
                                                  : "store-validated");
    return report;
}

}  // namespace masterai
