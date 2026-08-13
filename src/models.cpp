#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

const std::set<std::string> allowed_categories{
    "general-programming", "code-completion", "code-review",
    "debugging", "documentation", "embeddings-code-search",
    "conversation"};
const std::set<std::string> allowed_licenses{
    "Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause",
    "CC-BY-4.0", "Llama-3.1", "Llama-3.2", "Gemma"};

bool is_safe_identifier(const std::string& value) {
    if (value.empty() || value.size() > 96U || value.front() == '.' ||
        value.back() == '.') {
        return false;
    }
    return std::all_of(value.begin(), value.end(), [](const char character) {
        return (character >= 'a' && character <= 'z') ||
               (character >= 'A' && character <= 'Z') ||
               (character >= '0' && character <= '9') ||
               character == '-' || character == '_' || character == '.';
    });
}

std::uint64_t positive_number(const JsonValue& object, const std::string& key) {
    const auto value = object.required(key).as_integer();
    if (value <= 0) {
        throw std::runtime_error(key + " must be positive");
    }
    return static_cast<std::uint64_t>(value);
}

void require_only(const JsonValue& object, const std::set<std::string>& fields,
                  const std::string& location) {
    for (const auto& item : object.as_object()) {
        if (fields.find(item.first) == fields.end()) {
            throw std::runtime_error("unknown manifest field: " +
                                     location + item.first);
        }
    }
}

bool valid_sha256(const std::string& digest) {
    return digest.size() == 64U &&
           std::all_of(digest.begin(), digest.end(), [](const char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}

// Shared by ModelRegistry::scan() and ::verify(): both walk the same
// category/model directory structure and skip the same things (a
// non-directory or symlinked category, a category outside
// allowed_categories, a non-directory or symlinked model directory, and a
// model directory with no manifest.json -- create_download() always writes
// one before a transfer starts, so a directory without one was never a
// tracked model at all). What each does with a surviving model directory
// differs, so only the walk itself is shared.
void for_each_model_directory(
    const std::filesystem::path& root,
    const std::function<void(const std::string& category,
                             const std::filesystem::directory_entry& model_entry)>&
        visit) {
    for (const auto& category_entry : std::filesystem::directory_iterator(root)) {
        if (!category_entry.is_directory() || category_entry.is_symlink()) {
            continue;
        }
        const std::string category = category_entry.path().filename().string();
        if (allowed_categories.find(category) == allowed_categories.end()) {
            continue;
        }
        for (const auto& model_entry :
             std::filesystem::directory_iterator(category_entry.path())) {
            if (!model_entry.is_directory() || model_entry.is_symlink() ||
                !std::filesystem::is_regular_file(model_entry.path() /
                                                   "manifest.json")) {
                continue;
            }
            visit(category, model_entry);
        }
    }
}

std::string read_bounded_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > 1024U * 1024U) {
        throw std::runtime_error("manifest size is invalid");
    }
    // Phase 21: opt-in async read (via the process-lifetime reader cached
    // per storage root) with automatic blocking fallback inside
    // read_file_bytes() -- identical bytes/behavior either way, so manifest
    // loading is unaffected when async storage isn't available.
    auto* reader = global_async_file_reader(path.parent_path());
    std::string content = read_file_bytes(reader, path, 0U, size);
    if (content.size() != size) {
        throw std::runtime_error("manifest read failed");
    }
    return content;
}

// Thrown by load_manifest() when a manifest parses and validates cleanly but
// its model file hasn't landed on disk yet -- carries the already-parsed
// manifest so scan() can still populate the inventory row's name/category
// instead of leaving it blank the way a genuinely invalid manifest would.
class ModelFileIncomplete final : public std::runtime_error {
public:
    ModelFileIncomplete(ModelManifest manifest_value, const std::string& message)
        : std::runtime_error(message), manifest_(std::move(manifest_value)) {}
    const ModelManifest& manifest() const noexcept { return manifest_; }

private:
    ModelManifest manifest_;
};

ModelManifest load_manifest(
    const std::filesystem::path& model_directory,
    const std::string& expected_category, const bool verify_hash,
    const std::function<void(std::uint64_t, std::uint64_t)>& hash_progress =
        {}) {
    const JsonValue root = parse_json(read_bounded_file(model_directory / "manifest.json"));
    require_only(root, {"schemaVersion", "id", "displayName", "category",
                        "model", "requirements", "files", "backends",
                        "provenance", "license", "hardware"}, "");
    ModelManifest manifest;
    manifest.schema_version =
        static_cast<int>(root.required("schemaVersion").as_integer());
    manifest.id = root.required("id").as_string();
    manifest.display_name = root.required("displayName").as_string();
    manifest.category = root.required("category").as_string();

    const auto& model = root.required("model");
    require_only(model, {"format", "architecture", "quantization"}, "model.");
    manifest.format = model.required("format").as_string();
    manifest.architecture = model.required("architecture").as_string();
    manifest.quantization = model.required("quantization").as_string();

    const auto& requirements = root.required("requirements");
    require_only(requirements,
                 {"minimumRamMiB", "recommendedRamMiB", "estimatedDiskMiB"},
                 "requirements.");
    manifest.minimum_ram_mib = positive_number(requirements, "minimumRamMiB");
    manifest.recommended_ram_mib = positive_number(requirements, "recommendedRamMiB");
    manifest.estimated_disk_mib = positive_number(requirements, "estimatedDiskMiB");

    const auto& files = root.required("files").as_array();
    if (files.size() != 1U) {
        throw std::runtime_error("release-1 manifest requires exactly one model file");
    }
    require_only(files.front(), {"path", "sizeBytes", "sha256"}, "files[].");
    manifest.model_file = files.front().required("path").as_string();
    manifest.model_size_bytes = positive_number(files.front(), "sizeBytes");
    manifest.model_sha256 = files.front().required("sha256").as_string();

    const auto& backends = root.required("backends").as_array();
    if (backends.size() != 1U) {
        throw std::runtime_error("release-1 manifest requires exactly one backend");
    }
    manifest.backend = backends.front().as_string();

    const auto& provenance = root.required("provenance");
    require_only(provenance, {"sourceUrl", "revision"}, "provenance.");
    manifest.source_url = provenance.required("sourceUrl").as_string();
    manifest.source_revision = provenance.required("revision").as_string();

    const auto& license = root.required("license");
    require_only(license, {"spdx", "accepted"}, "license.");
    manifest.license_id = license.required("spdx").as_string();
    manifest.license_accepted = license.required("accepted").as_boolean();

    const auto& hardware = root.required("hardware");
    require_only(hardware, {"cpuFeatures", "gpuBackend"}, "hardware.");
    for (const auto& feature : hardware.required("cpuFeatures").as_array()) {
        const auto& name = feature.as_string();
        if ((name != "sse4.2" && name != "avx" && name != "avx2" &&
             name != "neon") ||
            !manifest.required_cpu_features.insert(name).second) {
            throw std::runtime_error("duplicate required CPU feature");
        }
    }
    manifest.required_gpu_backend = hardware.required("gpuBackend").as_string();

    if (manifest.schema_version != 1 || !is_safe_identifier(manifest.id) ||
        manifest.id != model_directory.filename().string() ||
        manifest.category != expected_category ||
        allowed_categories.find(manifest.category) == allowed_categories.end() ||
        manifest.display_name.empty() || manifest.display_name.size() > 160U ||
        !is_safe_identifier(manifest.architecture) ||
        !is_safe_identifier(manifest.quantization) ||
        std::filesystem::path(manifest.model_file).is_absolute() ||
        std::filesystem::path(manifest.model_file).filename() !=
            std::filesystem::path(manifest.model_file) ||
        manifest.format != "gguf" || manifest.backend != "llama-cpp" ||
        manifest.recommended_ram_mib < manifest.minimum_ram_mib ||
        manifest.estimated_disk_mib <
            (manifest.model_size_bytes + 1024U * 1024U - 1U) /
                (1024U * 1024U) ||
        !valid_sha256(manifest.model_sha256) ||
        !is_safe_identifier(manifest.source_revision) ||
        (manifest.required_gpu_backend != "" &&
         manifest.required_gpu_backend != "cuda" &&
         manifest.required_gpu_backend != "vulkan" &&
         manifest.required_gpu_backend != "hip") ||
        (manifest.source_url.rfind("https://huggingface.co/", 0U) != 0U &&
         manifest.source_url.rfind("https://github.com/", 0U) != 0U &&
         manifest.source_url.rfind("https://modelscope.cn/", 0U) != 0U) ||
        allowed_licenses.find(manifest.license_id) == allowed_licenses.end() ||
        !manifest.license_accepted) {
        throw std::runtime_error("manifest identity, category, format, or backend is invalid");
    }

    const std::filesystem::path model_file = model_directory / manifest.model_file;
    if (!is_path_within(model_directory, model_file)) {
        throw std::runtime_error("model file escapes its model directory");
    }
    // A queued/in-progress download writes this manifest before the transfer
    // starts (see create_download in workload_http.cpp), so a model file that
    // simply hasn't arrived yet is an expected, temporary condition -- not a
    // corrupt or tampered manifest. Report it distinctly (ModelState::downloading,
    // carrying the manifest we already parsed) so the inventory can still show
    // the model's name instead of a blank "invalid" row.
    if (!std::filesystem::exists(model_file)) {
        throw ModelFileIncomplete(manifest, "model file has not finished downloading yet");
    }
    if (!std::filesystem::is_regular_file(model_file) ||
        std::filesystem::is_symlink(model_file)) {
        throw std::runtime_error("model file is not a regular file");
    }
    if (std::filesystem::file_size(model_file) != manifest.model_size_bytes) {
        throw std::runtime_error("model file size does not match its manifest");
    }
    // Hashing a multi-gigabyte GGUF file is far too slow to repeat on every
    // inventory/chat/download page load (see ModelRegistry::scan(), which
    // passes verify_hash=false and instead consults the on-disk verification
    // cache). Only the `masterai verify-models` CLI path -- ModelRegistry::
    // verify() -- asks for the real hash here.
    if (verify_hash &&
        !constant_time_equal(sha256_file_hex(model_file, hash_progress),
                             manifest.model_sha256)) {
        throw std::runtime_error("model file SHA-256 does not match its manifest");
    }
    return manifest;
}

// Escapes embedded JSON text for the manifest/cache strings built below.
// Kept local (rather than shared with the HTTP layer's own json_escape)
// because this file has no dependency on server_internal.hpp and the CLI
// caller has none at all -- duplicated the same way allowed_categories/
// allowed_licenses already are here versus workload_http.cpp.
std::string manifest_json_escape(const std::string& value) {
    std::string output;
    for (const char character : value) {
        if (character == '"' || character == '\\') output.push_back('\\');
        output.push_back(character);
    }
    return output;
}

// Name of the cache file (see VerificationCache below) that records which
// model files have already had their SHA-256 confirmed against their
// manifest, so ModelRegistry::scan() never has to hash gigabytes of model
// data just to answer a page load.
constexpr const char* kVerificationCacheFile = ".verified-cache.json";

struct VerifiedEntry {
    std::string sha256;
    std::uint64_t size_bytes{0};
};

// Reads the verification cache written by ModelRegistry::verify(). Absent,
// empty, or corrupt cache files are treated as "nothing verified yet" --
// every model simply reports as unverified until an operator runs
// `masterai verify-models`, rather than failing scan() outright.
std::map<std::string, VerifiedEntry> read_verification_cache(
    const std::filesystem::path& model_root) {
    std::map<std::string, VerifiedEntry> cache;
    const auto path = model_root / kVerificationCacheFile;
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) {
        return cache;
    }
    try {
        const JsonValue root = parse_json(read_bounded_file(path));
        for (const auto& item : root.required("models").as_object()) {
            VerifiedEntry entry;
            entry.sha256 = item.second.required("sha256").as_string();
            entry.size_bytes = positive_number(item.second, "sizeBytes");
            cache.emplace(item.first, std::move(entry));
        }
    } catch (const std::exception&) {
        // A hand-edited or truncated cache file is no different from a
        // missing one: everything just re-reports as unverified until the
        // next `masterai verify-models` run rewrites it cleanly.
        return {};
    }
    return cache;
}

void write_verification_cache(
    const std::filesystem::path& model_root,
    const std::map<std::string, VerifiedEntry>& cache) {
    std::string body{"{\"schemaVersion\":1,\"models\":{"};
    bool first = true;
    for (const auto& [id, entry] : cache) {
        if (!first) body += ",";
        first = false;
        body += "\"" + manifest_json_escape(id) + "\":{\"sha256\":\"" +
                manifest_json_escape(entry.sha256) + "\",\"sizeBytes\":" +
                std::to_string(entry.size_bytes) + "}";
    }
    body += "}}";
    const auto path = model_root / kVerificationCacheFile;
    const auto temporary =
        std::filesystem::path(path.string() + ".tmp");
    {
        std::ofstream stream(temporary, std::ios::binary | std::ios::trunc);
        stream << body;
        if (!stream.good()) {
            throw std::runtime_error("verification cache could not be written");
        }
    }
    std::error_code error;
    std::filesystem::rename(temporary, path, error);
    if (error) {
        throw std::runtime_error("verification cache could not be committed");
    }
}

}  // namespace

void record_verified_model(const std::filesystem::path& model_root,
                           const std::string& model_id,
                           const std::string& sha256_hex,
                           std::uint64_t size_bytes) {
    std::error_code root_error;
    const auto root = std::filesystem::weakly_canonical(model_root, root_error);
    if (root_error || !std::filesystem::is_directory(root)) return;
    auto cache = read_verification_cache(root);
    cache[model_id] = VerifiedEntry{sha256_hex, size_bytes};
    write_verification_cache(root, cache);
}

void write_model_manifest(const std::filesystem::path& model_directory,
                          const std::string& model_id,
                          const std::string& display_name,
                          const std::string& category,
                          const std::string& architecture,
                          const std::string& quantization,
                          const std::uint64_t minimum_ram_mib,
                          const std::uint64_t recommended_ram_mib,
                          const std::string& filename,
                          const std::uint64_t size_bytes,
                          const std::string& sha256,
                          const std::string& source_url,
                          const std::string& revision,
                          const std::string& license_spdx) {
    std::filesystem::create_directories(model_directory);
    std::ofstream manifest_stream(model_directory / "manifest.json",
                                  std::ios::binary | std::ios::trunc);
    manifest_stream <<
        "{\"schemaVersion\":1,\"id\":\"" + manifest_json_escape(model_id) +
        "\",\"displayName\":\"" + manifest_json_escape(display_name) +
        "\",\"category\":\"" + manifest_json_escape(category) +
        "\",\"model\":{\"format\":\"gguf\",\"architecture\":\"" +
        manifest_json_escape(architecture) + "\",\"quantization\":\"" +
        manifest_json_escape(quantization) +
        "\"},\"requirements\":{\"minimumRamMiB\":" +
        std::to_string(minimum_ram_mib) + ",\"recommendedRamMiB\":" +
        std::to_string(recommended_ram_mib) + ",\"estimatedDiskMiB\":" +
        std::to_string((size_bytes + 1024ULL * 1024ULL - 1ULL) /
                       (1024ULL * 1024ULL)) +
        "},\"files\":[{\"path\":\"" + manifest_json_escape(filename) +
        "\",\"sizeBytes\":" + std::to_string(size_bytes) +
        ",\"sha256\":\"" + manifest_json_escape(sha256) +
        "\"}],\"backends\":[\"llama-cpp\"],\"provenance\":{\"sourceUrl\":\"" +
        manifest_json_escape(source_url) + "\",\"revision\":\"" +
        manifest_json_escape(revision) +
        "\"},\"license\":{\"spdx\":\"" + manifest_json_escape(license_spdx) +
        "\",\"accepted\":true},\"hardware\":{\"cpuFeatures\":[],"
        "\"gpuBackend\":\"\"}}";
    if (!manifest_stream.good()) {
        throw std::runtime_error("manifest could not be written");
    }
}

ModelRegistry::ModelRegistry(std::filesystem::path model_root,
                             HardwareInfo hardware,
                             const std::uint64_t memory_reserve_mib)
    : model_root_(std::move(model_root)), hardware_(std::move(hardware)),
      memory_reserve_mib_(memory_reserve_mib) {}

std::vector<ModelRecord> ModelRegistry::scan() const {
    std::vector<ModelRecord> records;
    std::error_code root_error;
    const auto root = std::filesystem::weakly_canonical(model_root_, root_error);
    if (root_error || !std::filesystem::is_directory(root)) {
        return records;
    }
    // Read once per scan() call rather than per model: this is a single
    // small JSON file, versus the gigabytes of hashing it lets every model
    // below skip.
    const auto verified = read_verification_cache(root);

    for_each_model_directory(root, [&](const std::string& category,
                                       const std::filesystem::directory_entry&
                                           model_entry) {
            ModelRecord record;
            record.directory = model_entry.path();
            try {
                // verify_hash=false: never hash the model file here. Whether
                // it's trusted as verified comes entirely from the cache
                // lookup below, populated only by `masterai verify-models`.
                record.manifest =
                    load_manifest(record.directory, category, /*verify_hash=*/false);
                const auto cached = verified.find(record.manifest.id);
                const bool is_verified =
                    cached != verified.end() &&
                    cached->second.sha256 == record.manifest.model_sha256 &&
                    cached->second.size_bytes == record.manifest.model_size_bytes;
                if (!is_verified) {
                    record.state = ModelState::unverified;
                    record.diagnostic =
                        "This model has not been hash-verified yet. Run "
                        "'.\\scripts\\rehash.ps1' to verify it and make it "
                        "available.";
                    records.push_back(std::move(record));
                    return;
                }
                bool features_available = true;
                for (const auto& feature : record.manifest.required_cpu_features) {
                    if (hardware_.cpu_features.find(feature) ==
                        hardware_.cpu_features.end()) {
                        features_available = false;
                    }
                }
                if (!record.manifest.required_gpu_backend.empty() &&
                    std::find(hardware_.gpu_backends.begin(),
                              hardware_.gpu_backends.end(),
                              record.manifest.required_gpu_backend) ==
                        hardware_.gpu_backends.end()) {
                    features_available = false;
                }
                const auto suitability =
                    assess_model(record.manifest, hardware_, memory_reserve_mib_);
                // A RAM shortfall (memory_risk) is left selectable rather than
                // hidden: it's an estimate against *current* available RAM,
                // which can change (other apps closing, a reboot), and the
                // chat UI shows the recommended RAM against the model's name
                // so the user can judge it themselves instead of us deciding
                // for them. Only genuine incompatibilities -- an unsupported
                // backend/format or missing required CPU/GPU features --
                // still block the model outright, since those can't be
                // worked around by freeing memory.
                if (!features_available ||
                    suitability.rating == Suitability::unsupported) {
                    record.state = ModelState::valid;
                    record.diagnostic =
                        features_available ? suitability.reason
                                           : "Required CPU/GPU features are unavailable.";
                } else {
                    record.state = ModelState::ready;
                    record.diagnostic =
                        suitability.rating == Suitability::memory_risk
                            ? "Warning: " + suitability.reason +
                                  " It may load slowly or fail depending on "
                                  "what else is running."
                            : "Manifest, license, provenance, size, digest, "
                              "and hardware suitability checks passed.";
                }
            } catch (const ModelFileIncomplete& incomplete) {
                record.manifest = incomplete.manifest();
                record.state = ModelState::downloading;
                record.diagnostic = incomplete.what();
            } catch (const std::exception& exception) {
                record.state = ModelState::invalid;
                record.diagnostic = exception.what();
            }
            records.push_back(std::move(record));
        });
    return records;
}

std::size_t ModelRegistry::verify(
    const std::function<void(const std::string&, bool, const std::string&)>&
        progress,
    const std::function<void(const std::string&, std::uint64_t,
                             std::uint64_t)>& hash_progress) const {
    std::error_code root_error;
    const auto root = std::filesystem::weakly_canonical(model_root_, root_error);
    if (root_error || !std::filesystem::is_directory(root)) {
        return 0U;
    }
    // Start from what's already verified so a partial/interrupted run, or
    // one where only some models changed on disk, doesn't discard hashes
    // that are still perfectly valid.
    auto cache = read_verification_cache(root);
    std::size_t verified_count = 0U;
    // Every model id actually seen on disk this run -- anything left in
    // `cache` afterward belongs to a model directory that no longer exists
    // (removed, renamed, or its manifest deleted) and is pruned below rather
    // than lingering forever as a stale entry no scan() lookup can ever
    // match again anyway.
    std::set<std::string> seen_ids;

    for_each_model_directory(root, [&](const std::string& category,
                                       const std::filesystem::directory_entry&
                                           model_entry) {
            const auto directory = model_entry.path();
            const std::string id = directory.filename().string();
            seen_ids.insert(id);
            try {
                // Cheap pre-check (no hashing): parses the manifest and
                // confirms the file exists with the declared size (see
                // load_manifest's verify_hash=false path). If the id is
                // already in the cache with the exact same declared
                // sha256/size, the file was hash-verified on a prior run and
                // nothing about its manifest has changed since -- re-hashing
                // it again would just re-confirm the same fact at the cost
                // of reading the whole (often multi-gigabyte) file. Only a
                // changed manifest (redownload/replacement under the same
                // id) or a first-time id actually needs the real hash below.
                const auto cheap_manifest =
                    load_manifest(directory, category, /*verify_hash=*/false);
                const auto already_verified = cache.find(id);
                if (already_verified != cache.end() &&
                    already_verified->second.sha256 ==
                        cheap_manifest.model_sha256 &&
                    already_verified->second.size_bytes ==
                        cheap_manifest.model_size_bytes) {
                    if (progress) progress(id, true, "");
                    return;
                }
                // Reported at each 10% step reached, and only for files
                // large enough (256 MiB+) that hashing them takes long
                // enough for interim progress to matter -- small manifests
                // finish before a percentage would even be useful.
                std::uint64_t last_reported_step = 0U;
                const auto hash_progress_for_file =
                    [&](const std::uint64_t bytes_hashed,
                        const std::uint64_t total_bytes) {
                        if (!hash_progress ||
                            total_bytes < (256U * 1024U * 1024U)) {
                            return;
                        }
                        const auto step = (bytes_hashed * 10U) / total_bytes;
                        if (step <= last_reported_step && bytes_hashed != total_bytes) {
                            return;
                        }
                        last_reported_step = step;
                        hash_progress(id, bytes_hashed, total_bytes);
                    };
                // verify_hash=true: this is the one place a model file's
                // SHA-256 actually gets computed -- only reached for a new
                // or changed model, per the cache short-circuit above.
                const auto manifest = load_manifest(
                    directory, category, /*verify_hash=*/true,
                    hash_progress_for_file);
                cache[manifest.id] =
                    VerifiedEntry{manifest.model_sha256, manifest.model_size_bytes};
                ++verified_count;
                if (progress) progress(manifest.id, true, "");
            } catch (const ModelFileIncomplete&) {
                // Not yet downloaded -- nothing to verify, and not an error;
                // leave any prior cache entry (there shouldn't be one) alone
                // and say nothing rather than reporting a false failure.
            } catch (const std::exception& exception) {
                cache.erase(id);
                if (progress) progress(id, false, exception.what());
            }
        });
    // Drop any cache entry for a model id that no longer exists on disk
    // (directory removed/renamed, or its manifest.json deleted) instead of
    // letting it linger indefinitely -- it can never be matched by scan()
    // again anyway once its manifest is gone.
    for (auto entry = cache.begin(); entry != cache.end();) {
        entry = seen_ids.find(entry->first) == seen_ids.end()
                    ? cache.erase(entry)
                    : std::next(entry);
    }
    write_verification_cache(root, cache);
    return verified_count;
}

SuitabilityResult assess_model(const ModelManifest& manifest,
                               const HardwareInfo& hardware,
                               const std::uint64_t reserve_mib) {
    SuitabilityResult result;
    result.available_ram_mib = hardware.available_ram_mib;
    if (manifest.backend != "llama-cpp" || manifest.format != "gguf") {
        result.reason = "The required backend or model format is unsupported.";
        return result;
    }
    if (hardware.available_ram_mib <= reserve_mib) {
        result.rating = Suitability::memory_risk;
        result.reason = "The operating-system memory reserve consumes available RAM.";
        return result;
    }
    const std::uint64_t working_allowance =
        std::max<std::uint64_t>(1024U, manifest.minimum_ram_mib / 5U);
    if (manifest.minimum_ram_mib >
        std::numeric_limits<std::uint64_t>::max() - working_allowance) {
        result.reason = "Manifest memory requirement overflowed validation.";
        return result;
    }
    result.required_ram_mib = manifest.minimum_ram_mib + working_allowance;
    const std::uint64_t usable_ram = hardware.available_ram_mib - reserve_mib;
    if (result.required_ram_mib > usable_ram) {
        result.rating = Suitability::memory_risk;
        result.reason = "Estimated working set breaches available RAM after reserve.";
    } else if (manifest.recommended_ram_mib > usable_ram) {
        result.rating = Suitability::usable;
        result.reason = "Model fits minimum requirements but not the recommended margin.";
    } else {
        result.rating = Suitability::recommended;
        result.reason = "Model fits the recommended RAM requirement and reserve.";
    }
    return result;
}

LlamaCppAdapter::LlamaCppAdapter(std::filesystem::path approved_backend)
    : approved_backend_(std::move(approved_backend)) {}

bool LlamaCppAdapter::available() const {
    return std::filesystem::is_regular_file(approved_backend_) &&
           !std::filesystem::is_symlink(approved_backend_);
}

LaunchSpec LlamaCppAdapter::build_launch_spec(const ModelRecord& model,
                                              const unsigned int context_length,
                                              const unsigned int port,
                                              const unsigned int parallel_slots,
                                              const LaunchTuning& tuning,
                                              const std::string& accelerator_policy) const {
    if (!available()) {
        throw std::runtime_error("approved llama.cpp server executable is unavailable");
    }
    const bool cpu_only = accelerator_policy == "cpu_only";
    if (cpu_only && tuning.gpu_layers != 0U) {
        // Fail closed: the administrator's cpu_only choice is never silently
        // overridden. A nonzero gpu_layers reaching this point means a
        // caller bypassed CalibrationService::resolve()/calibrate(), which
        // already force gpu_layers to 0 under cpu_only.
        throw std::runtime_error(
            "refusing to launch a runner with GPU layers requested while "
            "hardware.acceleratorPolicy is cpu_only");
    }
    if (model.state != ModelState::ready) {
        throw std::runtime_error(
            "model must pass digest and suitability verification before launch");
    }
    if (context_length < 256U || context_length > 1048576U ||
        port < 1024U || port > 65535U) {
        throw std::invalid_argument("context length or local port is outside policy");
    }
    if (parallel_slots < 1U || parallel_slots > 64U) {
        throw std::invalid_argument("parallel slot count is outside policy");
    }
    const auto model_file = model.directory / model.manifest.model_file;
    if (!is_path_within(model.directory, model_file)) {
        throw std::runtime_error("model path escaped its approved directory");
    }
    if (std::filesystem::file_size(model_file) !=
            model.manifest.model_size_bytes ||
        !constant_time_equal(sha256_file_hex(model_file),
                             model.manifest.model_sha256)) {
        throw std::runtime_error(
            "model changed after verification; unsafe load was blocked");
    }
    std::vector<std::string> arguments{
        "--model", model_file.string(), "--host", "127.0.0.1",
        "--port", std::to_string(port), "--ctx-size", std::to_string(context_length),
        "--parallel", std::to_string(parallel_slots)};
    if (tuning.continuous_batching) {
        // llama-server performs the actual compatible token-step batching;
        // MasterAI's RequestScheduler remains the admission/fairness layer.
        arguments.emplace_back("--cont-batching");
    }
    // Phase 19: calibrated launch tuning. Every branch below is skipped at
    // its default value, so a caller passing the default LaunchTuning{}
    // reproduces exactly the pre-Phase-19 argument list.
    if (tuning.gpu_layers > 0U) {
        arguments.emplace_back("--n-gpu-layers");
        arguments.emplace_back(std::to_string(tuning.gpu_layers));
    }
    // Phase 26: LaunchTuning::load_mode/pre_touch are advisory metadata that
    // ride alongside allow_memory_map/allow_memory_lock rather than adding
    // their own argument branches here -- CalibrationService::resolve() (see
    // calibration.cpp) already keeps the two in sync (e.g. load_mode ==
    // resident implies allow_memory_map == true and allow_memory_lock ==
    // true), so this function's existing --no-mmap/--mlock logic already
    // fully realizes whatever load_mode/pre_touch a caller resolved to.
    if (!tuning.allow_memory_map) {
        arguments.emplace_back("--no-mmap");
    }
    if (tuning.allow_memory_lock) {
        arguments.emplace_back("--mlock");
    }
    if (tuning.thread_count > 0U) {
        arguments.emplace_back("--threads");
        arguments.emplace_back(std::to_string(tuning.thread_count));
    }
    if (tuning.batch_tokens > 0U) {
        arguments.emplace_back("--batch-size");
        arguments.emplace_back(std::to_string(tuning.batch_tokens));
    }
    if (tuning.ubatch_tokens > 0U) {
        arguments.emplace_back("--ubatch-size");
        arguments.emplace_back(std::to_string(tuning.ubatch_tokens));
    }
    // llama-server's prompt-cache "context checkpoints" each hold a full
    // extra copy of the KV cache, on the *same device* the KV cache lives
    // on (GPU, when gpu_layers > 0). Its own default of 32 checkpoints
    // silently multiplies GPU KV-cache memory 32x on top of what
    // select_gpu_layers() budgeted for (which only accounts for model
    // weights, not this), reliably OOM-crashing the runner on small-VRAM
    // cards even though the model itself fits comfortably. Capping this
    // low keeps the rewind feature usable without blowing the VRAM budget
    // calibration actually reasoned about.
    arguments.emplace_back("--ctx-checkpoints");
    arguments.emplace_back("2");
    // Phase 32: the real dual-model (draft + target) launch path. Empty
    // speculative_draft_model_file reproduces exactly today's single-model
    // launch; every caller that never sets it is unaffected. The caller
    // (server.cpp's ensure_model_loaded) is responsible for having already
    // run check_draft_target_compatibility()/decide_speculative_decoding_
    // for_request() and for only reaching here after AdvancedOptimization
    // Registry has admitted "speculative_decoding" -- this function only
    // ever emits the flags, it never re-derives whether launching together
    // was actually a good idea.
    if (!tuning.speculative_draft_model_file.empty()) {
        if (!std::filesystem::is_regular_file(
                tuning.speculative_draft_model_file)) {
            throw std::runtime_error(
                "speculative decoding draft model file does not exist");
        }
        arguments.emplace_back("--model-draft");
        arguments.emplace_back(tuning.speculative_draft_model_file.string());
        if (tuning.speculative_draft_gpu_layers > 0U) {
            arguments.emplace_back("--gpu-layers-draft");
            arguments.emplace_back(
                std::to_string(tuning.speculative_draft_gpu_layers));
        }
        // llama.cpp's own documented defaults for the draft acceptance
        // window: at most 16 tokens speculated per step, never fewer than
        // 5 -- conservative, well below any context-length concern, and
        // independent of context_length/parallel_slots above.
        arguments.emplace_back("--draft-max");
        arguments.emplace_back("16");
        arguments.emplace_back("--draft-min");
        arguments.emplace_back("5");
    }
    std::map<std::string, std::string> environment;
    if (cpu_only) {
        // Hide every GPU from the runner process at the library level, so a
        // backend that ignores --n-gpu-layers 0 still cannot enumerate or
        // initialize a device. -1/"" are each backend's own documented
        // "no devices visible" sentinel.
        environment["CUDA_VISIBLE_DEVICES"] = "-1";
        environment["HIP_VISIBLE_DEVICES"] = "-1";
        environment["ROCR_VISIBLE_DEVICES"] = "-1";
        environment["GGML_VK_VISIBLE_DEVICES"] = "-1";
        environment["SYCL_VISIBLE_DEVICES"] = "";
    }
    return LaunchSpec{approved_backend_, std::move(arguments), model.directory,
                      std::move(environment)};
}

// Phase 26: use-prediction signal recording. See the ModelUsagePredictor
// comment in masterai.hpp -- deliberately plain recorded evidence (recency,
// pin, project-preference count, waiting-request count), never an opaque
// computed ranking.
void ModelUsagePredictor::record_use(const std::string& model_id,
                                     const std::uint64_t now_epoch_seconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = signals_[model_id];
    entry.model_id = model_id;
    entry.last_used_epoch_seconds = now_epoch_seconds;
    ++entry.project_preference_score;
}

void ModelUsagePredictor::set_pinned(const std::string& model_id,
                                     const bool pinned) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = signals_[model_id];
    entry.model_id = model_id;
    entry.pinned = pinned;
}

void ModelUsagePredictor::set_waiting_request_count(
    const std::string& model_id, const std::uint32_t count) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = signals_[model_id];
    entry.model_id = model_id;
    entry.waiting_request_count = count;
}

void ModelUsagePredictor::increment_waiting(const std::string& model_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = signals_[model_id];
    entry.model_id = model_id;
    if (entry.waiting_request_count !=
        std::numeric_limits<std::uint32_t>::max()) {
        ++entry.waiting_request_count;
    }
}

void ModelUsagePredictor::decrement_waiting(const std::string& model_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto& entry = signals_[model_id];
    entry.model_id = model_id;
    if (entry.waiting_request_count > 0U) --entry.waiting_request_count;
}

std::vector<ModelUsageSignals> ModelUsagePredictor::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelUsageSignals> result;
    result.reserve(signals_.size());
    for (const auto& [id, signals] : signals_) result.push_back(signals);
    return result;
}

// Reporting convention shared with tuning_profile_json()/model_state_name()
// elsewhere in this codebase: a small struct plus a _json() free function an
// administrator-facing route or CLI command can print directly, rather than
// a bespoke new reporting surface for use-prediction alone.
std::string model_usage_signals_json(
    const std::vector<ModelUsageSignals>& signals) {
    std::string body = "[";
    bool first = true;
    for (const auto& entry : signals) {
        if (!first) body += ",";
        first = false;
        // model_id is validated elsewhere against is_safe_identifier() (see
        // above), so -- like host_hash/model_sha256 in tuning_profile_json
        // -- it never needs JSON escaping here.
        body += "{\"modelId\":\"" + entry.model_id + "\"" +
               ",\"lastUsedEpochSeconds\":" +
               std::to_string(entry.last_used_epoch_seconds) +
               ",\"pinned\":" + (entry.pinned ? "true" : "false") +
               ",\"projectPreferenceScore\":" +
               std::to_string(entry.project_preference_score) +
               ",\"waitingRequestCount\":" +
               std::to_string(entry.waiting_request_count) + "}";
    }
    body += "]";
    return body;
}

}  // namespace masterai
