// MasterAI model, project, attachment, download, and benchmark HTTP operations.
//
// This focused controller keeps non-streaming workload routes out of the core
// socket/authentication server while reusing the same native service instances.
#include "server_internal.hpp"
#include "json.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <stdexcept>

namespace masterai::server_internal {
namespace {

// Converts durable model state into its stable API spelling.
std::string model_state(const ModelState state) {
    switch (state) {
        case ModelState::discovered: return "discovered";
        case ModelState::valid: return "valid";
        case ModelState::invalid: return "invalid";
        case ModelState::unverified: return "unverified";
        case ModelState::ready: return "ready";
        case ModelState::loading: return "loading";
        case ModelState::loaded: return "loaded";
        case ModelState::failed: return "failed";
        case ModelState::quarantined: return "quarantined";
        case ModelState::downloading: return "downloading";
    }
    return "unknown";
}

// Converts durable download state into its stable API spelling.
std::string download_state(const DownloadState state) {
    switch (state) {
        case DownloadState::queued: return "queued";
        case DownloadState::transferring: return "transferring";
        case DownloadState::paused: return "paused";
        case DownloadState::verifying: return "verifying";
        case DownloadState::complete: return "complete";
        case DownloadState::failed: return "failed";
        case DownloadState::quarantined: return "quarantined";
        case DownloadState::cancelled: return "cancelled";
    }
    return "unknown";
}

// Converts benchmark profile state into its stable API spelling.
std::string benchmark_profile(const BenchmarkProfile value) {
    if (value == BenchmarkProfile::standard) return "standard";
    if (value == BenchmarkProfile::extended) return "extended";
    return "quick";
}

// Parses the closed benchmark-profile vocabulary shared by both routes.
BenchmarkProfile parse_benchmark_profile(const std::string& profile) {
    if (profile == "quick") return BenchmarkProfile::quick;
    if (profile == "standard") return BenchmarkProfile::standard;
    if (profile == "extended") return BenchmarkProfile::extended;
    throw std::runtime_error("invalid benchmark profile");
}

// Returns the canonical bounded response when native hardware probing fails.
std::string hardware_probe_failure() {
    return response(503, "Service Unavailable",
                    "{\"error\":\"hardware_probe_failed\"}");
}

// Extracts exactly one project identifier from a closed index-route suffix.
std::optional<std::string> index_project_id(const Request& request,
                                            const std::string& suffix) {
    const std::string prefix{"/api/v1/projects/"};
    if (request.target.rfind(prefix, 0U) != 0U ||
        request.target.size() <= prefix.size() + suffix.size() ||
        request.target.compare(request.target.size() - suffix.size(),
                               suffix.size(), suffix) != 0) {
        return std::nullopt;
    }
    const auto id = request.target.substr(
        prefix.size(),
        request.target.size() - prefix.size() - suffix.size());
    if (id.empty() || id.find('/') != std::string::npos ||
        std::filesystem::path(id).filename().string() != id) {
        return std::nullopt;
    }
    return id;
}

// Enforces explicit bearer-token project bindings; cookie sessions retain
// their role-authorized catalogue access.
bool index_project_authorized(
    const std::string& project_id, const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) {
    return cookie_authenticated ||
           authenticated_project_ids.find(project_id) !=
               authenticated_project_ids.end();
}

struct IndexProjectResolution {
    std::optional<ProjectRecord> project;
    std::string failure_response;

    explicit operator bool() const noexcept { return project.has_value(); }
};

// Resolves one index route through the shared security boundary. The request
// and suffix select an identifier, authentication bindings constrain access,
// and the catalogue supplies the authoritative project or a stable failure.
IndexProjectResolution resolve_index_project(
    const Request& request, const std::string& suffix,
    const ProjectCatalog& projects, const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) {
    const auto id = index_project_id(request, suffix);
    if (!id) {
        return {std::nullopt,
                response(400, "Bad Request",
                         "{\"error\":\"invalid_index_route\"}")};
    }
    if (!index_project_authorized(
            *id, cookie_authenticated, authenticated_project_ids)) {
        return {std::nullopt,
                response(403, "Forbidden",
                         "{\"error\":\"project_binding_required\"}")};
    }
    const auto project = projects.find(*id);
    if (!project) {
        return {std::nullopt,
                response(404, "Not Found",
                         "{\"error\":\"project_not_found\"}")};
    }
    return {project, {}};
}

// Converts worker state into the stable public API vocabulary.
std::string index_job_state(const IndexJobState state) {
    switch (state) {
        case IndexJobState::absent: return "not-built";
        case IndexJobState::queued: return "queued";
        case IndexJobState::running: return "running";
        case IndexJobState::ready: return "ready";
        case IndexJobState::cancelled: return "cancelled";
        case IndexJobState::failed: return "failed";
    }
    return "failed";
}

std::string index_trigger(const IndexTrigger trigger) {
    switch (trigger) {
        case IndexTrigger::manual: return "manual";
        case IndexTrigger::save: return "save";
        case IndexTrigger::watcher: return "watcher";
        case IndexTrigger::branch_switch: return "branch-switch";
        case IndexTrigger::periodic: return "periodic";
    }
    return "manual";
}

// Parses the closed trigger vocabulary accepted from a live editor, file
// watcher, or version-control adapter. Manual is excluded here because manual
// rebuilds have their own dedicated route and admission rule.
std::optional<IndexTrigger> parse_index_trigger(const std::string& value) {
    if (value == "save") return IndexTrigger::save;
    if (value == "watcher") return IndexTrigger::watcher;
    if (value == "branch-switch") return IndexTrigger::branch_switch;
    if (value == "periodic") return IndexTrigger::periodic;
    return std::nullopt;
}

// Serializes only bounded progress and diagnostics, never indexed content.
std::string index_status_json(const std::string& project_id,
                              const IndexServiceStatus& status) {
    return "{\"projectId\":\"" + json_escape(project_id) +
           "\",\"state\":\"" + index_job_state(status.state) +
           "\",\"trigger\":\"" + index_trigger(status.trigger) +
           "\",\"queuePosition\":" +
           std::to_string(status.queue_position) +
           ",\"generation\":" +
           std::to_string(status.index.generation) +
           ",\"filesDiscovered\":" +
           std::to_string(status.index.files_discovered) +
           ",\"filesIndexed\":" +
           std::to_string(status.index.files_indexed) +
           ",\"filesUnchanged\":" +
           std::to_string(status.index.files_unchanged) +
           ",\"chunks\":" + std::to_string(status.index.chunks) +
           ",\"diskBytes\":" + std::to_string(status.index.disk_bytes) +
           ",\"partial\":" +
           std::string(status.index.partial ? "true" : "false") +
           ",\"cancelled\":" +
           std::string(status.index.cancelled ? "true" : "false") +
           ",\"diagnostic\":\"" +
           json_escape(status.index.diagnostic) + "\"}";
}

}  // namespace

struct WorkloadHttpController::State final {
    State(const AppConfig& app_configuration, ProjectCatalog& project_catalog,
          AttachmentStore& attachment_store,
          RunnerSupervisor* inference_service,
          DownloadManager* download_service, BenchmarkStore& benchmark_store,
          ProjectIndexService& index_service, AuditLog& audit_log,
          CacheManager& cache_manager)
        : configuration(app_configuration), projects(project_catalog),
          attachments(attachment_store), inference(inference_service),
          downloads(download_service), benchmarks(benchmark_store),
          indexes(index_service), audit(audit_log), cache(cache_manager) {}

    const AppConfig& configuration;
    ProjectCatalog& projects;
    AttachmentStore& attachments;
    RunnerSupervisor* inference;
    DownloadManager* downloads;
    BenchmarkStore& benchmarks;
    ProjectIndexService& indexes;
    AuditLog& audit;
    CacheManager& cache;

    // Probes hardware once and applies the existing model registry policy.
    std::vector<ModelRecord> scan_models() const {
        return ModelRegistry(configuration.models_root,
                             probe_hardware(configuration.models_root),
                             configuration.memory_reserve_mib)
            .scan();
    }
};

// Captures existing workload services without creating parallel ownership.
WorkloadHttpController::WorkloadHttpController(
    const AppConfig& configuration, ProjectCatalog& projects,
    AttachmentStore& attachments, RunnerSupervisor* inference,
    DownloadManager* downloads, BenchmarkStore& benchmarks,
    ProjectIndexService& indexes, AuditLog& audit, CacheManager& cache)
    : state_(std::make_unique<State>(
          configuration, projects, attachments, inference, downloads,
          benchmarks, indexes, audit, cache)) {}

WorkloadHttpController::~WorkloadHttpController() = default;

// Returns the verified model inventory as bounded JSON.
std::string WorkloadHttpController::model_inventory() const {
    try {
        std::string body{"{\"models\":["};
        bool first = true;
        for (const auto& model : state_->scan_models()) {
            if (!first) body += ",";
            first = false;
            body += "{\"id\":\"" + json_escape(model.manifest.id) +
                    "\",\"displayName\":\"" +
                    json_escape(model.manifest.display_name) +
                    "\",\"category\":\"" +
                    json_escape(model.manifest.category) +
                    "\",\"state\":\"" + model_state(model.state) +
                    "\",\"diagnostic\":\"" +
                    json_escape(model.diagnostic) +
                    "\",\"minimumRamMiB\":" +
                    std::to_string(model.manifest.minimum_ram_mib) +
                    ",\"recommendedRamMiB\":" +
                    std::to_string(model.manifest.recommended_ram_mib) + "}";
        }
        return response(200, "OK", body + "]}");
    } catch (const std::exception&) {
        return hardware_probe_failure();
    }
}

// Returns the same inventory as an escaped read-only HTML table.
std::string WorkloadHttpController::model_inventory_page() const {
    try {
        std::string body =
            "<!doctype html><html><head><meta charset=\"utf-8\">"
            "<title>MasterAI Models</title><style>body{font:16px system-ui;"
            "max-width:1100px;margin:2rem auto;padding:0 1rem}table{width:100%;"
            "border-collapse:collapse}th,td{padding:.65rem;border-bottom:1px "
            "solid #ccc;text-align:left}.ready{color:#087830}</style></head>"
            "<body><h1>Model inventory</h1><p>Only models marked Ready are "
            "eligible to load.</p><table><thead><tr><th>Model</th><th>Category"
            "</th><th>State</th><th>Verification</th></tr></thead><tbody>";
        for (const auto& model : state_->scan_models()) {
            body += "<tr><td>" + html_escape(model.manifest.display_name) +
                    "</td><td>" + html_escape(model.manifest.category) +
                    "</td><td class=\"" +
                    std::string(model.state == ModelState::ready ? "ready" : "") +
                    "\">" + model_state(model.state) + "</td><td>" +
                    html_escape(model.diagnostic) + "</td></tr>";
        }
        return html_response(body + "</tbody></table></body></html>");
    } catch (const std::exception&) {
        return hardware_probe_failure();
    }
}

// Lists registered project records through the canonical catalogue.
std::string WorkloadHttpController::list_projects() const {
    std::string body{"{\"projects\":["};
    bool first = true;
    for (const auto& project : state_->projects.list()) {
        if (!first) body += ",";
        first = false;
        body += "{\"id\":\"" + json_escape(project.id) +
                "\",\"displayName\":\"" + json_escape(project.display_name) +
                "\",\"root\":\"" + json_escape(project.root.string()) + "\"}";
    }
    return response(200, "OK", body + "]}");
}

// Creates one managed project beneath the configured runtime project root.
std::string WorkloadHttpController::create_project(
    Request& request, const UserRecord& user) {
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 2U) {
            throw std::runtime_error("unexpected project field");
        }
        const auto id = root.required("id").as_string();
        const auto path = state_->configuration.runtime_root / "projects" / id;
        std::filesystem::create_directories(path);
        const auto project = state_->projects.add(
            id, root.required("displayName").as_string(), path);
        // Phase 17: a new project changes what "authorized" means, so every
        // cached entry -- however it was keyed -- must stop being reachable.
        state_->cache.invalidate_policy();
        state_->audit.append("project.create", user.id, "success", project.id);
        return response(201, "Created",
                        "{\"id\":\"" + json_escape(project.id) + "\"}");
    } catch (const std::exception&) {
        return response(400, "Bad Request",
                        "{\"error\":\"invalid_project_request\"}");
    }
}

// Reports one project index without exposing results or cross-project state.
std::string WorkloadHttpController::index_status(
    Request& request, const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) const {
    const auto resolution = resolve_index_project(
        request, "/index", state_->projects, cookie_authenticated,
        authenticated_project_ids);
    if (!resolution) {
        return resolution.failure_response;
    }
    const auto& id = resolution.project->id;
    const auto status = state_->indexes.status(id);
    if (!status) {
        IndexServiceStatus absent;
        absent.index.project_id = id;
        return response(200, "OK", index_status_json(id, absent));
    }
    return response(200, "OK", index_status_json(id, *status));
}

// Enqueues a non-blocking rebuild in the fixed-capacity index service.
std::string WorkloadHttpController::rebuild_index(
    Request& request, const UserRecord& user,
    const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) {
    if (!role_allows(user.role, "projects.write")) {
        return response(403, "Forbidden",
                        "{\"error\":\"permission_denied\"}");
    }
    const auto resolution = resolve_index_project(
        request, "/index/rebuild", state_->projects, cookie_authenticated,
        authenticated_project_ids);
    if (!resolution) {
        return resolution.failure_response;
    }
    const auto& project = *resolution.project;
    if (!state_->indexes.request_rebuild(project)) {
        return response(409, "Conflict",
                        "{\"error\":\"index_rebuild_not_admitted\"}");
    }
    state_->audit.append("index.rebuild", user.id, "accepted", project.id);
    return response(
        202, "Accepted",
        "{\"projectId\":\"" + json_escape(project.id) +
            "\",\"state\":\"queued\"}");
}

// Cooperatively cancels queued or active work without removing a valid index.
std::string WorkloadHttpController::cancel_index(
    Request& request, const UserRecord& user,
    const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) {
    if (!role_allows(user.role, "projects.write")) {
        return response(403, "Forbidden",
                        "{\"error\":\"permission_denied\"}");
    }
    const auto resolution = resolve_index_project(
        request, "/index/cancel", state_->projects, cookie_authenticated,
        authenticated_project_ids);
    if (!resolution) {
        return resolution.failure_response;
    }
    const auto& id = resolution.project->id;
    if (!state_->indexes.cancel(id)) {
        return response(409, "Conflict",
                        "{\"error\":\"index_job_not_active\"}");
    }
    state_->audit.append("index.cancel", user.id, "success", id);
    return response(202, "Accepted",
                    "{\"projectId\":\"" + json_escape(id) +
                        "\",\"state\":\"cancelling\"}");
}

// Accepts one live trigger from an authenticated editor, file-watcher, or
// version-control adapter. Save/watcher triggers must name affected paths so
// only those files are re-read; branch-switch/periodic notifications carry
// no paths because the service always promotes them to a full scan.
std::string WorkloadHttpController::notify_index(
    Request& request, const UserRecord& user, const bool cookie_authenticated,
    const std::set<std::string>& authenticated_project_ids) {
    if (!role_allows(user.role, "projects.write")) {
        return response(403, "Forbidden",
                        "{\"error\":\"permission_denied\"}");
    }
    const auto resolution = resolve_index_project(
        request, "/index/notify", state_->projects, cookie_authenticated,
        authenticated_project_ids);
    if (!resolution) {
        return resolution.failure_response;
    }
    const auto& project = *resolution.project;
    IndexTrigger trigger{};
    std::vector<std::filesystem::path> changed_paths;
    try {
        const auto root = parse_json(request.body);
        const auto parsed = parse_index_trigger(
            root.required("trigger").as_string());
        if (!parsed) {
            return response(400, "Bad Request",
                            "{\"error\":\"invalid_index_trigger\"}");
        }
        trigger = *parsed;
        if (trigger == IndexTrigger::save || trigger == IndexTrigger::watcher) {
            const auto& paths = root.required("paths").as_array();
            // as_array() returns a reference, so the size is known upfront.
            changed_paths.reserve(paths.size());
            for (const auto& path : paths) {
                changed_paths.emplace_back(path.as_string());
            }
        }
    } catch (const std::exception&) {
        return response(400, "Bad Request",
                        "{\"error\":\"invalid_index_notify_request\"}");
    }
    if (!state_->indexes.request_update(project, changed_paths, trigger)) {
        return response(409, "Conflict",
                        "{\"error\":\"index_update_not_admitted\"}");
    }
    state_->audit.append("index.notify", user.id, index_trigger(trigger),
                         project.id);
    return response(
        202, "Accepted",
        "{\"projectId\":\"" + json_escape(project.id) +
            "\",\"state\":\"queued\",\"trigger\":\"" +
            index_trigger(trigger) + "\"}");
}

// Stores one project-bound UTF-8 attachment through AttachmentStore policy.
std::string WorkloadHttpController::create_attachment(
    Request& request, const UserRecord& user) {
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 3U) {
            throw std::runtime_error("unexpected attachment field");
        }
        const auto project_id = root.required("projectId").as_string();
        if (!state_->projects.find(project_id)) {
            return response(404, "Not Found",
                            "{\"error\":\"project_not_found\"}");
        }
        const auto record = state_->attachments.add_text(
            user.id, project_id, root.required("filename").as_string(),
            root.required("content").as_string());
        state_->audit.append("attachment.create", user.id, "success",
                             record.id);
        return response(201, "Created",
                        "{\"id\":\"" + record.id + "\",\"sha256\":\"" +
                            record.sha256 + "\",\"sizeBytes\":" +
                            std::to_string(record.size_bytes) + "}");
    } catch (const std::exception&) {
        return response(400, "Bad Request",
                        "{\"error\":\"invalid_attachment_request\"}");
    }
}

// Loads one verified model with the caller-selected context length.
std::string WorkloadHttpController::load_model(
    Request& request, const UserRecord& user) {
    if (!role_allows(user.role, "models.load")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    if (state_->inference == nullptr) {
        return response(503, "Service Unavailable",
                        "{\"error\":\"inference_backend_not_configured\"}");
    }
    const std::string prefix{"/api/v1/models/"};
    const auto id = request.target.substr(
        prefix.size(), request.target.size() - prefix.size() - 5U);
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 1U) {
            throw std::runtime_error("unexpected load field");
        }
        for (const auto& model : state_->scan_models()) {
            if (model.manifest.id == id) {
                // Suitability is gated here, at activation, rather than at
                // download time: a model may be fetched on modest hardware
                // and only loaded later, elsewhere or after a hardware
                // upgrade. state == ready already encodes the fresh
                // manifest/digest/hardware suitability pass from scan_models().
                if (model.state != ModelState::ready) {
                    state_->audit.append("model.load", user.id, "denied",
                                         model.diagnostic);
                    return response(
                        409, "Conflict",
                        "{\"error\":\"model_not_ready\",\"reason\":\"" +
                            json_escape(model.diagnostic) + "\"}");
                }
                // Same GPU-offload recommendation ensure_model_loaded() uses
                // for chat auto-loads (see server.cpp): a manual admin load
                // through this endpoint should not be the one path that
                // never touches VRAM for a compatible model/GPU pair.
                LaunchTuning tuning;
                // Phase 30A: this manual-load path built its own tuning
                // directly from select_gpu_layers() rather than going
                // through CalibrationService::resolve(), so it must repeat
                // the cpu_only guard here rather than relying on resolve()
                // to have already applied it.
                tuning.gpu_layers =
                    state_->configuration.accelerator_policy == "cpu_only"
                        ? 0U
                        : select_gpu_layers(
                              probe_hardware(state_->configuration.models_root),
                              model.manifest.required_gpu_backend,
                              model.manifest.model_size_bytes);
                state_->inference->load(
                    model,
                    static_cast<unsigned int>(
                        root.required("contextLength").as_integer()),
                    state_->configuration.runner_port, 30U, 1U, tuning,
                    state_->configuration.accelerator_policy);
                state_->audit.append("model.load", user.id, "success", id);
                return response(200, "OK", "{\"state\":\"ready\"}");
            }
        }
        return response(404, "Not Found", "{\"error\":\"model_not_found\"}");
    } catch (const std::exception&) {
        state_->audit.append("model.load", user.id, "failed", id);
        return response(409, "Conflict", "{\"error\":\"model_load_failed\"}");
    }
}

// Stops the isolated inference runner when the caller has model authority.
std::string WorkloadHttpController::unload_model(const UserRecord& user) {
    if (!role_allows(user.role, "models.load") ||
        state_->inference == nullptr) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    state_->inference->unload();
    state_->audit.append("model.unload", user.id, "success", "runner stopped");
    return response(200, "OK", "{\"state\":\"unloaded\"}");
}

// Lists durable download jobs without exposing command-line details.
std::string WorkloadHttpController::list_downloads() const {
    if (state_->downloads == nullptr) {
        return response(503, "Service Unavailable",
                        "{\"error\":\"download_transport_not_configured\"}");
    }
    std::string body{"{\"downloads\":["};
    bool first = true;
    for (const auto& job : state_->downloads->list()) {
        if (!first) body += ",";
        first = false;
        // modelId/filename are derived from the destination path rather than
        // stored again -- models_root/category/model_id/filename is the one
        // place that layout is defined (see create_download below).
        const auto& destination = job.request().destination;
        body += "{\"id\":\"" + job.id() + "\",\"state\":\"" +
                download_state(job.state()) + "\",\"completedBytes\":" +
                std::to_string(job.completed_bytes()) +
                ",\"modelId\":\"" +
                json_escape(destination.parent_path().filename().string()) +
                "\",\"filename\":\"" +
                json_escape(destination.filename().string()) +
                "\",\"diagnostic\":\"" + json_escape(job.diagnostic()) + "\"}";
    }
    return response(200, "OK", body + "]}");
}

// Validates hardware, source, license, and destination before queueing.
std::string WorkloadHttpController::create_download(
    Request& request, const UserRecord& user) {
    if (state_->downloads == nullptr ||
        !role_allows(user.role, "downloads.manage")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 14U) {
            throw std::runtime_error("unexpected download field");
        }
        const auto filename = root.required("filename").as_string();
        const auto category = root.required("category").as_string();
        const auto model_id = root.required("modelId").as_string();
        static const std::set<std::string> categories{
            "general-programming", "code-completion", "code-review",
            "debugging", "documentation", "embeddings-code-search",
            "conversation"};
        if (std::filesystem::path(filename).filename().string() != filename ||
            categories.find(category) == categories.end() ||
            std::filesystem::path(model_id).filename().string() != model_id ||
            model_id.empty()) {
            throw std::runtime_error("download destination is invalid");
        }
        const auto minimum = root.required("minimumRamMiB").as_integer();
        const auto recommended =
            root.required("recommendedRamMiB").as_integer();
        if (minimum <= 0 || recommended < minimum) {
            throw std::runtime_error("download RAM recommendation is invalid");
        }
        // The remaining fields exist purely so a manifest.json can be
        // written alongside the model file once it lands -- without one,
        // ModelRegistry::scan() never recognizes a downloaded file as a
        // model at all, so it can never reach Ready and nothing could ever
        // actually chat with it. This mirrors models.cpp's own manifest
        // validation exactly (kept in sync by hand, same as the categories
        // set above) rather than relaxing it for downloads specifically.
        const auto display_name = root.required("displayName").as_string();
        const auto architecture = root.required("architecture").as_string();
        const auto quantization = root.required("quantization").as_string();
        const auto license_spdx = root.required("licenseSpdx").as_string();
        const auto size_bytes = root.required("sizeBytes").as_integer();
        static const std::set<std::string> licenses{
            "Apache-2.0", "MIT", "BSD-2-Clause", "BSD-3-Clause",
            "CC-BY-4.0", "Llama-3.1", "Llama-3.2", "Gemma"};
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
            licenses.find(license_spdx) == licenses.end() ||
            size_bytes <= 0) {
            throw std::runtime_error("download manifest field is invalid");
        }
        // RAM is advisory only at download time (reported below via
        // hardwareRecommendation), never blocking: a model can be fetched
        // now and run later, on this machine or another. ModelRegistry
        // already regates suitability at scan/Ready-promotion time, and the
        // load route rechecks it again before starting a runner, so nothing
        // downloaded here can load without a fresh suitability pass.
        const auto hardware =
            probe_hardware(state_->configuration.runtime_root / "downloads");
        const auto usable =
            hardware.available_ram_mib >
                    state_->configuration.memory_reserve_mib
                ? hardware.available_ram_mib -
                      state_->configuration.memory_reserve_mib
                : 0U;
        const auto source_url = root.required("sourceUrl").as_string();
        const auto revision = root.required("immutableRevision").as_string();
        const auto sha256 = root.required("expectedSha256").as_string();
        const auto license_accepted = root.required("licenseAccepted").as_boolean();
        if (!is_safe_identifier(revision) ||
            (source_url.rfind("https://huggingface.co/", 0U) != 0U &&
             source_url.rfind("https://github.com/", 0U) != 0U &&
             source_url.rfind("https://modelscope.cn/", 0U) != 0U) ||
            !license_accepted) {
            throw std::runtime_error("download provenance is invalid");
        }
        const auto model_directory =
            state_->configuration.models_root / category / model_id;
        // Written now, before the transfer runs, so ModelRegistry::scan()
        // can recognize the file the moment it lands (and, until then,
        // reports this model as invalid/missing rather than not existing at
        // all -- a truthful "downloading" state instead of silence).
        write_model_manifest(model_directory, model_id, display_name, category,
                             architecture, quantization,
                             static_cast<std::uint64_t>(minimum),
                             static_cast<std::uint64_t>(recommended), filename,
                             static_cast<std::uint64_t>(size_bytes), sha256,
                             source_url, revision, license_spdx);
        const auto job = state_->downloads->create(
            {source_url, revision, sha256,
             model_directory / filename, license_accepted,
             static_cast<std::uint64_t>(minimum),
             static_cast<std::uint64_t>(recommended)});
        state_->audit.append("download.create", user.id, "success", job.id());
        return response(
            202, "Accepted",
            "{\"id\":\"" + job.id() +
                "\",\"state\":\"queued\",\"hardwareRecommendation\":\"" +
                (usable >= static_cast<std::uint64_t>(recommended)
                     ? "recommended"
                     : "usable") +
                "\"}");
    } catch (const std::exception&) {
        return response(400, "Bad Request",
                        "{\"error\":\"invalid_download_request\"}");
    }
}

// Runs one durable download with bounded cancellation state.
std::string WorkloadHttpController::run_download(
    Request& request, const UserRecord& user) {
    if (state_->downloads == nullptr ||
        !role_allows(user.role, "downloads.manage")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    const std::string prefix{"/api/v1/model-downloads/"};
    const auto id = request.target.substr(
        prefix.size(), request.target.size() - prefix.size() - 4U);
    std::atomic_bool cancellation{false};
    try {
        const auto job = state_->downloads->run(id, cancellation);
        state_->audit.append(
            "download.run", user.id,
            job.state() == DownloadState::complete ? "success" : "failed", id);
        return response(200, "OK",
                        "{\"id\":\"" + id + "\",\"state\":\"" +
                            download_state(job.state()) + "\"}");
    } catch (const DownloadAlreadyRunning&) {
        // Distinguished from the generic failure below so the frontend can
        // tell "already downloading, keep polling" apart from a real error.
        return response(409, "Conflict",
                        "{\"error\":\"download_already_running\"}");
    } catch (const std::exception&) {
        return response(409, "Conflict",
                        "{\"error\":\"download_run_failed\"}");
    }
}

// Requests a pause of an in-flight transfer; the run() call already blocked
// on that job's own /run request observes the signal and persists the
// resulting 'paused' state itself once it unwinds.
std::string WorkloadHttpController::pause_download(
    Request& request, const UserRecord& user) {
    if (state_->downloads == nullptr ||
        !role_allows(user.role, "downloads.manage")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    const std::string prefix{"/api/v1/model-downloads/"};
    const auto id = request.target.substr(
        prefix.size(), request.target.size() - prefix.size() - 6U);
    try {
        state_->downloads->pause(id);
        state_->audit.append("download.pause", user.id, "success", id);
        return response(202, "Accepted", "{\"id\":\"" + id + "\"}");
    } catch (const std::exception&) {
        return response(409, "Conflict",
                        "{\"error\":\"download_pause_failed\"}");
    }
}

// Stops a download, whether queued, paused, or actively transferring.
std::string WorkloadHttpController::cancel_download(
    Request& request, const UserRecord& user) {
    if (state_->downloads == nullptr ||
        !role_allows(user.role, "downloads.manage")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    const std::string prefix{"/api/v1/model-downloads/"};
    const auto id = request.target.substr(
        prefix.size(), request.target.size() - prefix.size() - 7U);
    try {
        state_->downloads->cancel(id);
        state_->audit.append("download.cancel", user.id, "success", id);
        return response(202, "Accepted", "{\"id\":\"" + id + "\"}");
    } catch (const std::exception&) {
        return response(409, "Conflict",
                        "{\"error\":\"download_cancel_failed\"}");
    }
}

// Discards a finished/paused/queued download's record and its leftover
// on-disk artifact (a completed model file itself is never touched -- only
// its own .part/.quarantine leftovers are).
std::string WorkloadHttpController::remove_download(
    Request& request, const UserRecord& user) {
    if (state_->downloads == nullptr ||
        !role_allows(user.role, "downloads.manage")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    const std::string prefix{"/api/v1/model-downloads/"};
    const auto id = request.target.substr(
        prefix.size(), request.target.size() - prefix.size() - 7U);
    try {
        state_->downloads->remove(id);
        state_->audit.append("download.remove", user.id, "success", id);
        return response(200, "OK", "{\"id\":\"" + id + "\"}");
    } catch (const std::exception&) {
        return response(409, "Conflict",
                        "{\"error\":\"download_remove_failed\"}");
    }
}

// Lists comparable durable benchmark records with derived throughput.
std::string WorkloadHttpController::list_benchmarks() const {
    std::string body{"{\"benchmarks\":["};
    bool first = true;
    for (const auto& record : state_->benchmarks.all()) {
        if (!first) body += ",";
        first = false;
        body += "{\"modelId\":\"" + json_escape(record.model_id) +
                "\",\"profile\":\"" + benchmark_profile(record.profile) +
                "\",\"promptSuiteHash\":\"" + record.prompt_suite_hash +
                "\",\"passedCases\":" +
                std::to_string(record.passed_cases) +
                ",\"totalCases\":" + std::to_string(record.total_cases) +
                ",\"tokensPerSecond\":" +
                std::to_string(
                    record.elapsed_microseconds == 0U
                        ? 0.0
                        : static_cast<double>(record.generated_tokens) *
                              1000000.0 /
                              static_cast<double>(
                                  record.elapsed_microseconds)) +
                "}";
    }
    return response(200, "OK", body + "]}");
}

// Recommends only a benchmark with matching host, suite, and profile.
std::string WorkloadHttpController::recommend_benchmark(
    Request& request) const {
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 3U) {
            throw std::runtime_error("unexpected recommendation field");
        }
        const auto recommended = state_->benchmarks.recommend(
            root.required("hardwareId").as_string(),
            root.required("promptSuiteHash").as_string(),
            parse_benchmark_profile(root.required("profile").as_string()));
        if (!recommended) {
            return response(404, "Not Found",
                            "{\"error\":\"no_compatible_benchmarks\"}");
        }
        return response(
            200, "OK",
            "{\"modelId\":\"" + json_escape(recommended->model_id) +
                "\",\"passedCases\":" +
                std::to_string(recommended->passed_cases) +
                ",\"totalCases\":" +
                std::to_string(recommended->total_cases) + "}");
    } catch (const std::exception&) {
        return response(400, "Bad Request",
                        "{\"error\":\"invalid_recommendation_request\"}");
    }
}

// Executes the existing reproducible benchmark runner for an authorized user.
std::string WorkloadHttpController::run_benchmark(
    Request& request, const UserRecord& user) {
    if (state_->inference == nullptr ||
        !role_allows(user.role, "benchmarks.run")) {
        return response(403, "Forbidden", "{\"error\":\"permission_denied\"}");
    }
    try {
        const auto root = parse_json(request.body);
        if (root.as_object().size() != 5U) {
            throw std::runtime_error("unexpected benchmark field");
        }
        BenchmarkRunner runner(*state_->inference, state_->benchmarks);
        std::atomic_bool cancellation{false};
        const auto record = runner.run(
            root.required("modelId").as_string(),
            root.required("backendVersion").as_string(),
            root.required("buildId").as_string(),
            root.required("hardwareId").as_string(),
            parse_benchmark_profile(root.required("profile").as_string()),
            cancellation);
        state_->audit.append("benchmark.run", user.id, "success",
                             record.model_id);
        return response(201, "Created",
                        "{\"promptSuiteHash\":\"" +
                            record.prompt_suite_hash +
                            "\",\"passedCases\":" +
                            std::to_string(record.passed_cases) + "}");
    } catch (const std::exception&) {
        return response(409, "Conflict", "{\"error\":\"benchmark_failed\"}");
    }
}

}  // namespace masterai::server_internal
