// Phase 31: storage tiering, virtual drives, and scratch-volume management.
//
// This unit owns the pure tier-classification/placement-recommendation logic
// and ScratchVolumeManager, the bounded/quota-enforced/crash-recoverable
// scratch-storage service. See masterai.hpp's Phase 31 block for the public
// contract and the rationale behind each piece; OS-level filesystem/memory
// probing (probe_filesystem_integrity_flags/probe_memory_accounting) lives in
// platform.cpp alongside every other probe_*() function, not here.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <mutex>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace masterai {
namespace {

// Same identifier policy as storage.cpp's safe_name(): short, filesystem-
// and journal-line safe, no path separators or control characters.
bool safe_job_id(const std::string& value) {
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

// DurableFileManifest's own copies of this codebase's standard packed-record
// helpers (ml.cpp, downloads.cpp, ... each keep a local copy rather than
// sharing one across translation units -- see masterai.hpp).
std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

std::string random_id() {
    const auto random = secure_random(16U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string id(random.size() * 2U, '0');
    for (std::size_t index = 0; index < random.size(); ++index) {
        id[index * 2U] = digits[(random[index] >> 4U) & 0x0fU];
        id[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    return id;
}

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
            throw std::runtime_error("persisted storage migration record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted storage migration record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

// Canonical form used as the manifest's map key so the same file referenced
// via two different-but-equivalent path spellings still resolves as one
// entry. Falls back to the raw string (rather than throwing) for a path that
// does not currently exist -- e.g. resolving an already-migrated original
// path, which by definition no longer exists at its old location.
std::string canonical_key(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.string() : canonical.string();
}

std::mutex g_durable_manifest_mutex;
DurableFileManifest* g_durable_manifest = nullptr;

}  // namespace

std::string to_string(const StorageTier tier) {
    switch (tier) {
        case StorageTier::fast_local: return "fast_local";
        case StorageTier::local_ssd: return "local_ssd";
        case StorageTier::local_hdd: return "local_hdd";
        case StorageTier::removable_or_network: return "removable_or_network";
        case StorageTier::ram_backed: return "ram_backed";
    }
    return "unknown";
}

std::string to_string(const DurableDataClass data_class) {
    switch (data_class) {
        case DurableDataClass::reconstructable_scratch: return "reconstructable_scratch";
        case DurableDataClass::gguf_model: return "gguf_model";
        case DurableDataClass::durable_chat: return "durable_chat";
        case DurableDataClass::audit_record: return "audit_record";
        case DurableDataClass::user_database: return "user_database";
        case DurableDataClass::resumable_download: return "resumable_download";
        case DurableDataClass::backup: return "backup";
        case DurableDataClass::security_record: return "security_record";
        case DurableDataClass::index_generation_sole_copy: return "index_generation_sole_copy";
    }
    return "unknown";
}

// The hard prohibition (docs/PLAN.md Phase 31): only ephemeral,
// reconstructable scratch may ever sit on RAM-backed storage. Every other
// DurableDataClass names data that would simply be gone -- not degraded,
// gone -- across a reboot or a RAM-disk driver crash, so it is never
// eligible regardless of how small or how "cache-like" a caller believes a
// particular instance of it to be.
bool durable_data_class_allows_ram_tier(const DurableDataClass data_class) noexcept {
    return data_class == DurableDataClass::reconstructable_scratch;
}

// Uses the same latency thresholds select_load_mode()/adaptive_queue_depth()
// already rely on elsewhere (async_storage.cpp/calibration.cpp) so a given
// measured profile is classified consistently everywhere in the codebase,
// not just here.
StorageTier classify_storage_tier(const StorageLatencyProfile& profile) {
    if (profile.storage_class == "ram") return StorageTier::ram_backed;
    if (profile.storage_class == "network" || profile.storage_class == "removable" ||
        profile.storage_class == "optical") {
        return StorageTier::removable_or_network;
    }
    // NVMe-shaped: very low measured random-read latency.
    if (profile.measured_read_latency_us > 0.0 &&
        profile.measured_read_latency_us < 200.0) {
        return StorageTier::fast_local;
    }
    // HDD-shaped: high measured latency, or probing marked the device
    // non-fan-out-safe (the conservative fallback when probing itself
    // failed -- see probe_storage_latency()'s own comment in
    // async_storage.cpp).
    if (profile.sequential || profile.measured_read_latency_us >= 3000.0) {
        return StorageTier::local_hdd;
    }
    // Everything else (moderate SSD-shaped latency, or an unmeasured "fixed"
    // device on Linux where probe_storage_class() only reports "local"):
    // Tier B, not Tier A -- fast-local is reserved for the clearly-NVMe-
    // shaped measurement above.
    return StorageTier::local_ssd;
}

StoragePlacementRecommendation recommend_storage_placement(
    const StorageLatencyProfile& latency, const FilesystemIntegrityFlags& filesystem) {
    StoragePlacementRecommendation result;
    result.recommended_tier = classify_storage_tier(latency);
    if (!filesystem.detection_available) {
        result.concerns.emplace_back(
            "filesystem integrity could not be measured on this platform/path");
    } else {
        if (filesystem.network_redirected) {
            result.concerns.emplace_back("network-redirected volume");
        }
        if (filesystem.virtual_disk) {
            result.concerns.emplace_back("virtual disk (e.g. mounted VHD/VHDX) volume");
        }
        if (filesystem.compressed) {
            result.concerns.emplace_back("filesystem compression is enabled on this path");
        }
        if (filesystem.encrypted) {
            result.concerns.emplace_back("filesystem/file encryption is enabled on this path");
        }
        if (filesystem.deduplicated) {
            result.concerns.emplace_back("deduplication reparse point detected on this path");
        }
    }
    // Active model/index storage is never recommended on removable/network
    // or RAM-backed tiers (the former is import-export only by default per
    // docs/PLAN.md, the latter is the hard RAM-tier prohibition itself), nor
    // on a measured network-redirected or virtual-disk volume even if the OS
    // otherwise reports it as a plain fixed drive letter.
    result.acceptable_for_active_model_storage =
        result.recommended_tier != StorageTier::removable_or_network &&
        result.recommended_tier != StorageTier::ram_backed &&
        !(filesystem.detection_available &&
          (filesystem.network_redirected || filesystem.virtual_disk));
    return result;
}

// The Phase 31 exit criterion "a model is rejected or downgraded when
// projected active pages exceed safe physical capacity even if commit
// capacity remains" -- deliberately reasons only about physical_available_
// bytes, never commit_limit_bytes/pagefile_used_bytes, so a host with a huge
// pagefile cannot make this function believe an oversized model's resident
// working set is safe.
bool projected_resident_exceeds_safe_physical_capacity(
    const MemoryAccountingSnapshot& snapshot,
    const std::uint64_t projected_resident_bytes,
    const std::uint64_t os_reserve_bytes) noexcept {
    const auto safe_available =
        snapshot.physical_available_bytes > os_reserve_bytes
            ? snapshot.physical_available_bytes - os_reserve_bytes
            : 0ULL;
    return projected_resident_bytes > safe_available;
}

// ---------------------------------------------------------------------
// ScratchVolumeManager
// ---------------------------------------------------------------------

ScratchVolumeManager::ScratchVolumeManager(std::filesystem::path root,
                                           const std::uint64_t global_quota_bytes,
                                           const std::uint64_t free_space_reserve_bytes,
                                           const StorageTier preferred_tier)
    : root_(std::move(root)), global_quota_bytes_(global_quota_bytes),
      free_space_reserve_bytes_(free_space_reserve_bytes),
      preferred_tier_(preferred_tier) {
    if (root_.empty() || global_quota_bytes_ == 0U) {
        throw std::invalid_argument("scratch volume manager configuration is invalid");
    }
    std::filesystem::create_directories(root_);
}

ScratchVolumeManager::~ScratchVolumeManager() { shutdown_cleanup(); }

void ScratchVolumeManager::append_journal(const std::string& operation,
                                          const std::string& job_id,
                                          const std::filesystem::path& directory) {
    // Plain, unhashed lines -- unlike RecordStore's journal (storage.cpp),
    // this is a crash-recovery hint for ephemeral data, not a tamper-evident
    // durable record, so the extra hash-chain machinery would add cost
    // without adding anything this use actually needs.
    std::ofstream journal(root_ / "scratch.journal", std::ios::binary | std::ios::app);
    journal << operation << "\t" << job_id << "\t" << directory.string() << "\n";
    journal.flush();
    // A failed journal append is not itself fatal to the scratch operation
    // it accompanies -- recover_orphans() on the next startup will simply
    // fail to know about this job's directory if it crashes before the
    // matching "E" record, which only means that directory might have to be
    // found by a future manual/administrator cleanup instead. It must never
    // block or fail the caller's actual scratch write.
}

std::size_t ScratchVolumeManager::recover_orphans() {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto journal_path = root_ / "scratch.journal";
    std::map<std::string, std::filesystem::path> open_jobs;
    if (std::filesystem::exists(journal_path)) {
        std::ifstream journal(journal_path, std::ios::binary);
        std::string line;
        while (std::getline(journal, line)) {
            const auto first_tab = line.find('\t');
            if (first_tab == std::string::npos) continue;
            const auto second_tab = line.find('\t', first_tab + 1U);
            const auto operation = line.substr(0U, first_tab);
            const auto job_id = second_tab == std::string::npos
                                    ? line.substr(first_tab + 1U)
                                    : line.substr(first_tab + 1U, second_tab - first_tab - 1U);
            if (operation == "B" && second_tab != std::string::npos) {
                open_jobs[job_id] = line.substr(second_tab + 1U);
            } else if (operation == "E") {
                open_jobs.erase(job_id);
            }
        }
    }
    std::size_t removed = 0U;
    for (const auto& job : open_jobs) {
        std::error_code error;
        // Only ever remove a directory this journal itself created, and
        // only if it is genuinely still inside our own scratch root --
        // is_path_within() guards against a corrupted/tampered journal line
        // pointing recover_orphans() at an arbitrary path outside scratch.
        if (!is_path_within(root_, job.second)) continue;
        if (std::filesystem::exists(job.second, error)) {
            std::filesystem::remove_all(job.second, error);
            if (!error) ++removed;
        }
    }
    // Every orphan named by the previous run's journal has now been
    // resolved one way or another; start this run's journal fresh so a
    // second recover_orphans() call (or a future crash before any new job
    // starts) does not repeatedly reprocess the same already-handled lines.
    std::error_code truncate_error;
    std::filesystem::remove(journal_path, truncate_error);
    return removed;
}

bool ScratchVolumeManager::free_space_available(const std::uint64_t additional_bytes) const {
    std::error_code error;
    const auto space = std::filesystem::space(root_, error);
    if (error) return false;  // Cannot measure: fail closed, not open.
    if (space.available < free_space_reserve_bytes_) return false;
    return space.available - free_space_reserve_bytes_ >= additional_bytes;
}

std::filesystem::path ScratchVolumeManager::begin_job(
    const std::string& job_id, const std::uint64_t quota_bytes,
    const DurableDataClass data_class) {
    if (!safe_job_id(job_id) || quota_bytes == 0U) {
        throw std::invalid_argument("scratch job id or quota is invalid");
    }
    if (preferred_tier_ == StorageTier::ram_backed &&
        !durable_data_class_allows_ram_tier(data_class)) {
        // The hard prohibition: this scratch root is asserted to sit on a
        // RAM-backed volume, so only reconstructable scratch may ever be
        // admitted onto it -- see durable_data_class_allows_ram_tier().
        throw std::invalid_argument(
            "durable data class '" + to_string(data_class) +
            "' is not permitted on RAM-backed (Tier R) scratch storage");
    }
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.find(job_id) != jobs_.end()) {
        throw std::invalid_argument("scratch job id is already active");
    }
    // global_reserved_bytes_ never exceeds global_quota_bytes_ (every
    // admission path re-checks this same invariant), so the subtraction
    // below cannot underflow.
    if (quota_bytes > global_quota_bytes_ - global_reserved_bytes_) {
        throw std::invalid_argument("scratch job quota exceeds remaining global quota");
    }
    if (!free_space_available(0U)) {
        throw std::invalid_argument(
            "volume free space is already below the configured scratch reserve");
    }
    const auto directory = root_ / job_id;
    std::filesystem::create_directories(directory);
    append_journal("B", job_id, directory);
    jobs_.emplace(job_id, JobState{directory, quota_bytes, 0U});
    return directory;
}

bool ScratchVolumeManager::reserve(const std::string& job_id,
                                   const std::uint64_t additional_bytes) {
    if (additional_bytes == 0U) return true;
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(job_id);
    if (found == jobs_.end()) return false;
    auto& job = found->second;
    if (additional_bytes > job.quota_bytes - job.reserved_bytes) return false;
    if (additional_bytes > global_quota_bytes_ - global_reserved_bytes_) return false;
    if (!free_space_available(additional_bytes)) return false;
    job.reserved_bytes += additional_bytes;
    global_reserved_bytes_ += additional_bytes;
    return true;
}

std::filesystem::path ScratchVolumeManager::publish(
    const std::string& job_id, const std::filesystem::path& scratch_source,
    const std::filesystem::path& durable_destination,
    const DurableDataClass data_class) {
    std::filesystem::path job_directory;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end()) {
            throw std::invalid_argument("scratch job id is not active");
        }
        job_directory = found->second.directory;
    }
    if (!is_path_within(job_directory, scratch_source)) {
        throw std::invalid_argument(
            "publish() source must be inside the job's own scratch directory");
    }
    if (!std::filesystem::is_regular_file(scratch_source)) {
        throw std::invalid_argument("publish() source is not a readable regular file");
    }
    // The hard prohibition again, this time against the *destination*: a
    // caller may not use publish() to quietly write durable data back into
    // this manager's own (potentially RAM-backed) scratch root.
    if (is_path_within(root_, durable_destination) &&
        !durable_data_class_allows_ram_tier(data_class)) {
        throw std::invalid_argument(
            "durable data class '" + to_string(data_class) +
            "' may not be published into ephemeral scratch storage");
    }
    const auto destination_parent = durable_destination.parent_path();
    if (!destination_parent.empty()) {
        std::filesystem::create_directories(destination_parent);
    }
    // Atomic publication: stage a full copy as a `.tmp` sibling of the real
    // destination (same directory => same volume, so the rename below is a
    // single filesystem-journalled operation), then replace the destination
    // in one shot -- mirroring RecordStore::checkpoint()'s existing
    // temp-write-then-atomic-rename pattern (storage.cpp) so
    // durable_destination is always either fully absent/unchanged or fully
    // written, never partially observable by a concurrent reader.
    const auto temporary = durable_destination.string() + ".scratch-publish.tmp";
    std::error_code copy_error;
    std::filesystem::copy_file(
        scratch_source, temporary,
        std::filesystem::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic publish staging copy failed");
    }
#if defined(_WIN32)
    if (MoveFileExW(std::filesystem::path(temporary).c_str(), durable_destination.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic publish replacement failed");
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, durable_destination, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("atomic publish replacement failed");
    }
#endif
    return durable_destination;
}

void ScratchVolumeManager::end_job(const std::string& job_id) noexcept {
    std::filesystem::path directory;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto found = jobs_.find(job_id);
        if (found == jobs_.end()) return;  // Already ended, or never began: a no-op.
        directory = found->second.directory;
        global_reserved_bytes_ -= std::min(global_reserved_bytes_, found->second.reserved_bytes);
        jobs_.erase(found);
    }
    std::error_code error;
    std::filesystem::remove_all(directory, error);
    try {
        append_journal("E", job_id, directory);
    } catch (const std::exception&) {
        // Journal append is best-effort on the shutdown/end path too -- see
        // the comment in append_journal() above.
    }
}

void ScratchVolumeManager::shutdown_cleanup() noexcept {
    for (const auto& job_id : active_job_ids()) {
        end_job(job_id);
    }
}

std::vector<std::string> ScratchVolumeManager::active_job_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(jobs_.size());
    for (const auto& job : jobs_) ids.push_back(job.first);
    return ids;
}

std::uint64_t ScratchVolumeManager::global_quota_bytes() const noexcept {
    return global_quota_bytes_;
}

std::uint64_t ScratchVolumeManager::global_reserved_bytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return global_reserved_bytes_;
}

std::uint64_t ScratchVolumeManager::free_space_reserve_bytes() const noexcept {
    return free_space_reserve_bytes_;
}

StorageTier ScratchVolumeManager::preferred_tier() const noexcept { return preferred_tier_; }

const std::filesystem::path& ScratchVolumeManager::root() const noexcept { return root_; }

std::string scratch_volume_manager_status_json(const ScratchVolumeManager& manager) {
    std::string jobs_json = "[";
    bool first = true;
    for (const auto& job_id : manager.active_job_ids()) {
        if (!first) jobs_json += ",";
        first = false;
        jobs_json += json_string(job_id);
    }
    jobs_json += "]";
    return "{\"root\":" + json_string(manager.root().string()) +
          ",\"preferredTier\":" + json_string(to_string(manager.preferred_tier())) +
          ",\"globalQuotaBytes\":" + std::to_string(manager.global_quota_bytes()) +
          ",\"globalReservedBytes\":" + std::to_string(manager.global_reserved_bytes()) +
          ",\"freeSpaceReserveBytes\":" +
          std::to_string(manager.free_space_reserve_bytes()) +
          ",\"activeJobs\":" + jobs_json + "}";
}

DurableFileManifest::DurableFileManifest(RecordStore& records) : records_(&records) {
    // Rebuild current_location_ from the persisted, append-only migration
    // log. Entries are stored in the order they were recorded, so replaying
    // them in that order naturally reproduces the same chained
    // (source -> latest destination) state record() maintains live.
    for (const auto& item : records_->list("storage_migrations")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 5U) {
            throw std::runtime_error(
                "persisted storage migration record field count is wrong");
        }
        const auto& source_key = fields[0];
        const auto& destination_key = fields[1];
        for (auto& [key, value] : current_location_) {
            if (value == source_key) value = destination_key;
        }
        current_location_[source_key] = destination_key;
    }
}

void DurableFileManifest::record(const std::filesystem::path& source_path,
                                  const std::filesystem::path& destination_path,
                                  const std::string& sha256_hex,
                                  const DurableDataClass data_class) {
    const auto source_key = canonical_key(source_path);
    const auto destination_key = canonical_key(destination_path);
    const std::lock_guard<std::mutex> lock(mutex_);
    if (records_ != nullptr) {
        records_->put(
            "storage_migrations", random_id(),
            pack({source_key, destination_key, sha256_hex,
                 to_string(data_class), std::to_string(epoch_seconds())}));
    }
    for (auto& [key, value] : current_location_) {
        if (value == source_key) value = destination_key;
    }
    current_location_[source_key] = destination_key;
}

std::filesystem::path DurableFileManifest::resolve(
    const std::filesystem::path& original_path) const {
    if (std::filesystem::exists(original_path)) return original_path;
    auto key = canonical_key(original_path);
    const std::lock_guard<std::mutex> lock(mutex_);
    // Bounded rather than following current_location_ until it stabilises:
    // a manifest corrupted into a cycle must fail safe (return the last
    // hop actually reached) rather than hang.
    for (int hop = 0; hop < 32; ++hop) {
        const auto found = current_location_.find(key);
        if (found == current_location_.end()) break;
        key = found->second;
    }
    return std::filesystem::path(key);
}

std::vector<DurableFileManifest::Entry> DurableFileManifest::entries() const {
    std::vector<Entry> result;
    if (records_ == nullptr) return result;
    for (const auto& item : records_->list("storage_migrations")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 5U) continue;
        Entry entry;
        entry.source_path = fields[0];
        entry.destination_path = fields[1];
        entry.sha256_hex = fields[2];
        entry.data_class = fields[3];
        entry.migrated_at_epoch_seconds = std::stoull(fields[4]);
        result.push_back(std::move(entry));
    }
    return result;
}

std::string durable_file_manifest_json(const DurableFileManifest& manifest) {
    std::string entries_json = "[";
    bool first = true;
    for (const auto& entry : manifest.entries()) {
        if (!first) entries_json += ",";
        first = false;
        entries_json += "{\"sourcePath\":" + json_string(entry.source_path) +
                        ",\"destinationPath\":" + json_string(entry.destination_path) +
                        ",\"sha256\":" + json_string(entry.sha256_hex) +
                        ",\"dataClass\":" + json_string(entry.data_class) +
                        ",\"migratedAtEpochSeconds\":" +
                        std::to_string(entry.migrated_at_epoch_seconds) + "}";
    }
    entries_json += "]";
    return "{\"migrations\":" + entries_json + "}";
}

void install_global_durable_file_manifest(DurableFileManifest& manifest) {
    const std::lock_guard<std::mutex> lock(g_durable_manifest_mutex);
    g_durable_manifest = &manifest;
}

std::filesystem::path resolve_durable_path(const std::filesystem::path& original_path) {
    DurableFileManifest* manifest = nullptr;
    {
        const std::lock_guard<std::mutex> lock(g_durable_manifest_mutex);
        manifest = g_durable_manifest;
    }
    if (manifest == nullptr) return original_path;
    return manifest->resolve(original_path);
}

// Phase 31 (Priority B): tier-migration tooling. Unlike publish() above,
// this operates on data that is already durable and already placed, so
// there is no ScratchVolumeManager job/quota involved at all -- it is a
// standalone, verified relocate-in-place operation. Records the move in
// `manifest` (closing the earlier "no central manifest" gap) so any reader
// still holding `source_path` can find the file again via
// resolve_durable_path().
StorageMigrationResult migrate_durable_file(
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination_directory,
    const DurableDataClass data_class, DurableFileManifest& manifest) {
    if (!std::filesystem::is_regular_file(source_path)) {
        throw std::invalid_argument(
            "migrate_durable_file source is not a readable regular file");
    }
    std::filesystem::create_directories(destination_directory);
    const auto destination_path = destination_directory / source_path.filename();
    std::error_code canonical_error;
    if (std::filesystem::weakly_canonical(source_path, canonical_error) ==
        std::filesystem::weakly_canonical(destination_path, canonical_error)) {
        throw std::invalid_argument(
            "migrate_durable_file source and destination resolve to the same file");
    }
    // The same hard RAM-tier prohibition every other Phase 31 entry point
    // enforces: measure (never assume) what tier the destination directory
    // actually sits on before writing anything there.
    const auto destination_storage_class = probe_hardware(destination_directory).storage_class;
    const auto destination_latency =
        probe_storage_latency(destination_directory, destination_storage_class);
    if (classify_storage_tier(destination_latency) == StorageTier::ram_backed &&
        !durable_data_class_allows_ram_tier(data_class)) {
        throw std::invalid_argument(
            "durable data class '" + to_string(data_class) +
            "' may not be migrated onto RAM-backed (Tier R) storage");
    }
    // Digest the original before touching anything, then digest the staged
    // copy after it lands: the two must match before the rename that makes
    // the new location visible is ever attempted, and must still match
    // before the original is removed -- the same "verify before you trust
    // it, never assume" evidence discipline the rest of Phase 31 follows.
    const auto source_digest = sha256_file_hex(source_path);
    const auto temporary = destination_path.string() + ".tier-migrate.tmp";
    std::error_code copy_error;
    std::filesystem::copy_file(
        source_path, temporary,
        std::filesystem::copy_options::overwrite_existing, copy_error);
    if (copy_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("tier migration staging copy failed");
    }
    const auto staged_digest = sha256_file_hex(temporary);
    if (staged_digest != source_digest) {
        std::filesystem::remove(temporary);
        throw std::runtime_error(
            "tier migration checksum verification failed; original left untouched");
    }
    const auto bytes_migrated =
        static_cast<std::uint64_t>(std::filesystem::file_size(source_path));
#if defined(_WIN32)
    if (MoveFileExW(std::filesystem::path(temporary).c_str(), destination_path.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("tier migration replacement failed");
    }
#else
    std::error_code rename_error;
    std::filesystem::rename(temporary, destination_path, rename_error);
    if (rename_error) {
        std::filesystem::remove(temporary);
        throw std::runtime_error("tier migration replacement failed");
    }
#endif
    // Only remove the original once the verified copy is durably in place at
    // the new location. A crash in the narrow window between the rename
    // above and the remove below leaves both copies present rather than data
    // loss -- the safe failure direction -- and is left for an administrator
    // to notice via /api/v1/system/storage, since this is durable data, not
    // scratch, and recover_orphans() deliberately does not touch it.
    std::error_code remove_error;
    std::filesystem::remove(source_path, remove_error);
    manifest.record(source_path, destination_path, staged_digest, data_class);
    return StorageMigrationResult{destination_path, bytes_migrated, staged_digest};
}

}  // namespace masterai
