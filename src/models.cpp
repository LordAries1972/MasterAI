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
    "debugging", "documentation", "embeddings-code-search"};
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

std::string read_bounded_file(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error || size == 0U || size > 1024U * 1024U) {
        throw std::runtime_error("manifest size is invalid");
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error("manifest could not be opened");
    }
    std::ostringstream content;
    content << stream.rdbuf();
    if (!stream.good() && !stream.eof()) {
        throw std::runtime_error("manifest read failed");
    }
    return content.str();
}

ModelManifest load_manifest(const std::filesystem::path& model_directory,
                            const std::string& expected_category) {
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
         manifest.source_url.rfind("https://github.com/", 0U) != 0U) ||
        allowed_licenses.find(manifest.license_id) == allowed_licenses.end() ||
        !manifest.license_accepted) {
        throw std::runtime_error("manifest identity, category, format, or backend is invalid");
    }

    const std::filesystem::path model_file = model_directory / manifest.model_file;
    if (!is_path_within(model_directory, model_file) ||
        !std::filesystem::is_regular_file(model_file) ||
        std::filesystem::is_symlink(model_file)) {
        throw std::runtime_error("model file is missing or escapes its model directory");
    }
    if (std::filesystem::file_size(model_file) != manifest.model_size_bytes) {
        throw std::runtime_error("model file size does not match its manifest");
    }
    if (!constant_time_equal(sha256_file_hex(model_file), manifest.model_sha256)) {
        throw std::runtime_error("model file SHA-256 does not match its manifest");
    }
    return manifest;
}

}  // namespace

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
            if (!model_entry.is_directory() || model_entry.is_symlink()) {
                continue;
            }
            ModelRecord record;
            record.directory = model_entry.path();
            try {
                record.manifest = load_manifest(record.directory, category);
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
                if (!features_available ||
                    suitability.rating == Suitability::unsupported ||
                    suitability.rating == Suitability::memory_risk) {
                    record.state = ModelState::valid;
                    record.diagnostic =
                        features_available ? suitability.reason
                                           : "Required CPU/GPU features are unavailable.";
                } else {
                    record.state = ModelState::ready;
                    record.diagnostic =
                        "Manifest, license, provenance, size, digest, and hardware "
                        "suitability checks passed.";
                }
            } catch (const std::exception& exception) {
                record.state = ModelState::invalid;
                record.diagnostic = exception.what();
            }
            records.push_back(std::move(record));
        }
    }
    return records;
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
                                              const unsigned int port) const {
    if (!available()) {
        throw std::runtime_error("approved llama.cpp server executable is unavailable");
    }
    if (model.state != ModelState::ready) {
        throw std::runtime_error(
            "model must pass digest and suitability verification before launch");
    }
    if (context_length < 256U || context_length > 1048576U ||
        port < 1024U || port > 65535U) {
        throw std::invalid_argument("context length or local port is outside policy");
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
    return LaunchSpec{
        approved_backend_,
        {"--model", model_file.string(), "--host", "127.0.0.1",
         "--port", std::to_string(port), "--ctx-size", std::to_string(context_length)},
        model.directory};
}

}  // namespace masterai
