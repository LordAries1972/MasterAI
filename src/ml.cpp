// Machine Learning foundation phase: administrator-only scaffolding for
// docs/PLAN.md's "Machine Learning Abilities" section. Dashboard, Projects,
// Model Registry, Model Builder, Dataset Manager, Subject Knowledge
// Manager, Data Labeling, Data Preparation, and Training Jobs are real; the
// remaining planned interfaces (Fine-Tuning, Evaluation Lab, ...) are
// listed so an administrator can see the roadmap, but none of them have a
// full backing service yet -- see MachineLearningRegistry's class comment
// in masterai.hpp for why this stays honest rather than fabricating data.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <set>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

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

// Length-prefixed field packing so free-form project text (description,
// objective, ...) can safely contain any byte -- including the delimiter
// itself -- without escaping, matching ChatStore/DownloadManager's own
// persisted-record encoding in workflows.cpp.
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
            throw std::runtime_error("persisted ML project record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted ML project record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

// Escapes free-form project text for JSON output; the interface roster in
// the constructor below is hardcoded literals and doesn't need this.
std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char hex_digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex_digits[(character >> 4U) & 0x0fU];
                    result += hex_digits[character & 0x0fU];
                } else {
                    result += static_cast<char>(character);
                }
        }
    }
    return result;
}

}  // namespace

MachineLearningRegistry::MachineLearningRegistry() {
    interfaces_ = {
        {"dashboard", "Dashboard", "available"},
        {"projects", "Projects", "available"},
        {"model-registry", "Model Registry", "available"},
        // Model Builder reports available now that Phase 46 carries the
        // full section 9 configuration surface (ModelBuilderSettings) and
        // basic/advanced modes, not just the scoped-down identity record.
        {"model-builder", "Model Builder", "available"},
        {"dataset-manager", "Dataset Manager", "available"},
        {"subject-knowledge", "Subject Knowledge Manager", "available"},
        {"data-labeling", "Data Labeling", "available"},
        {"data-preparation", "Data Preparation", "available"},
        {"training-jobs", "Training Jobs", "available"},
        // Phase 70: POST .../run genuinely adapts the job's already-trained
        // base model via train_tabular_model's warm_start parameter (real
        // continued gradient descent on the fine-tuning dataset, not a
        // fresh model that merely reuses the training code path) -- see
        // execute_fine_tuning_job's comment in server.cpp.
        {"fine-tuning", "Fine-Tuning", "available"},
        {"evaluation-lab", "Evaluation Lab", "available"},
        {"experiment-tracking", "Experiment Tracking", "planned"},
        {"prompt-instruction-training", "Prompt and Instruction Training",
         "planned"},
        {"embeddings-vector-stores", "Embeddings and Vector Stores",
         "available"},
        {"retrieval-augmented-generation", "Retrieval-Augmented Generation",
         "available"},
        {"synthetic-data", "Synthetic Data", "planned"},
        {"model-comparison", "Model Comparison", "available"},
        {"deployment-manager", "Deployment Manager", "planned"},
        // Phases 62, 65: identity/lifecycle registries only (no live
        // network listener or content scanner behind them yet), so these
        // stay "planned" like Experiment Tracking/Deployment Manager above
        // -- "available" here means a real executor exists,
        // not merely a list/create/status/delete UI. Automation Pipelines
        // (Phase 64) left this set in Phase 69 once its "Train model"/
        // "Evaluate model" stages gained a real executor -- see below.
        {"inference-endpoints", "Inference Endpoints", "planned"},
        // Phase 67: a node flagged `is_local` now reports genuinely live
        // CPU/RAM/GPU capacity via probe_hardware() on every telemetry
        // request rather than a static description -- see ComputeNode's
        // class comment in masterai.hpp. Remote nodes (no agent process
        // built) still hold only the administrator-entered static record,
        // so this remains "available" for the local host, not for an
        // arbitrary fleet.
        {"hardware-compute", "Hardware and Compute", "available"},
        // Phase 69: POST .../run genuinely executes any "Train model"/
        // "Evaluate model" stages via Phase 56's real tabular engine; every
        // other named stage is honestly recorded as skipped since no
        // executor for it exists in this codebase -- see AutomationPipeline
        // and execute_training_job/execute_evaluation_run's comments.
        {"automation-pipelines", "Automation Pipelines", "available"},
        {"safety-governance", "Safety and Governance", "planned"},
        // Phase 68: GET /api/v1/ml/monitoring is a real read-only
        // aggregation over other phases' already-real data -- live local-
        // host telemetry (Phase 67's probe_hardware()), real training-job
        // status counts, genuinely measured evaluation metrics, and actual
        // benchmark throughput. Deliberately does not report per-step
        // training curves or live per-request inference telemetry -- see
        // the endpoint's own comment in server.cpp for the full boundary.
        {"monitoring-diagnostics", "Monitoring and Diagnostics", "available"},
        // Phase 66: a genuine read over the real AuditLog every ml.*
        // mutation already writes to -- no simulated data behind it.
        {"audit-logs", "Audit Logs", "available"},
        // Genuinely reads/writes real AppConfig fields with real
        // enforcement (the CSV upload cap) -- no simulated data behind it.
        {"ml-settings", "Machine Learning Settings", "available"},
    };
}

MachineLearningDashboard MachineLearningRegistry::dashboard(
    const MLProjectStore& projects, const ModelRegistryStore& models,
    const TrainingJobStore& training_jobs) const {
    MachineLearningDashboard result;
    result.interfaces = interfaces_;
    // Archived projects don't count as "active" -- everything else does,
    // including draft, since a draft project is still a real administrative
    // record, not a placeholder.
    for (const auto& project : projects.list()) {
        if (project.status != MLProjectStatus::archived) {
            ++result.active_projects;
        }
    }
    for (const auto& entry : models.list()) {
        switch (entry.state) {
            case ModelRegistryState::training: ++result.models_training; break;
            case ModelRegistryState::evaluation:
                ++result.models_awaiting_evaluation;
                break;
            case ModelRegistryState::production: ++result.deployed_models; break;
            default: break;
        }
    }
    for (const auto& job : training_jobs.list()) {
        if (job.status == TrainingJobStatus::failed) {
            ++result.failed_training_jobs;
        }
    }
    // models_awaiting_approval still defaults to zero (see the struct's own
    // comment): there is no distinct "awaiting approval" state yet.
    return result;
}

// Every label/status/key above is a hardcoded literal (see the registry
// constructor), never free-form input, so no JSON escaping is required.
std::string machine_learning_dashboard_json(
    const MachineLearningDashboard& dashboard) {
    std::string body =
        "{\"enabled\":" + std::string(dashboard.enabled ? "true" : "false") +
        ",\"phase\":\"" + dashboard.phase + "\"" +
        ",\"activeProjects\":" + std::to_string(dashboard.active_projects) +
        ",\"modelsTraining\":" + std::to_string(dashboard.models_training) +
        ",\"modelsAwaitingEvaluation\":" +
        std::to_string(dashboard.models_awaiting_evaluation) +
        ",\"modelsAwaitingApproval\":" +
        std::to_string(dashboard.models_awaiting_approval) +
        ",\"deployedModels\":" + std::to_string(dashboard.deployed_models) +
        ",\"failedTrainingJobs\":" +
        std::to_string(dashboard.failed_training_jobs) + ",\"interfaces\":[";
    bool first = true;
    for (const auto& interface : dashboard.interfaces) {
        if (!first) body += ",";
        first = false;
        body += "{\"key\":\"" + interface.key + "\",\"label\":\"" +
               interface.label + "\",\"status\":\"" + interface.status + "\"}";
    }
    return body + "]}";
}

std::string ml_project_status_name(const MLProjectStatus status) {
    switch (status) {
        case MLProjectStatus::draft: return "draft";
        case MLProjectStatus::data_collection: return "data_collection";
        case MLProjectStatus::data_preparation: return "data_preparation";
        case MLProjectStatus::ready_for_training: return "ready_for_training";
        case MLProjectStatus::training: return "training";
        case MLProjectStatus::evaluation: return "evaluation";
        case MLProjectStatus::awaiting_approval: return "awaiting_approval";
        case MLProjectStatus::approved: return "approved";
        case MLProjectStatus::deployed: return "deployed";
        case MLProjectStatus::paused: return "paused";
        case MLProjectStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid ML project status");
}

MLProjectStatus parse_ml_project_status(const std::string& status) {
    if (status == "draft") return MLProjectStatus::draft;
    if (status == "data_collection") return MLProjectStatus::data_collection;
    if (status == "data_preparation") return MLProjectStatus::data_preparation;
    if (status == "ready_for_training") return MLProjectStatus::ready_for_training;
    if (status == "training") return MLProjectStatus::training;
    if (status == "evaluation") return MLProjectStatus::evaluation;
    if (status == "awaiting_approval") return MLProjectStatus::awaiting_approval;
    if (status == "approved") return MLProjectStatus::approved;
    if (status == "deployed") return MLProjectStatus::deployed;
    if (status == "paused") return MLProjectStatus::paused;
    if (status == "archived") return MLProjectStatus::archived;
    throw std::runtime_error("stored ML project status is invalid");
}

MLProjectStore::MLProjectStore(RecordStore& records) : records_(&records) {
    restore();
}

void MLProjectStore::restore() {
    for (const auto& item : records_->list("ml_projects")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error("persisted ML project record field count is wrong");
        }
        MLProject project;
        project.id = item.first;
        project.name = fields[0];
        project.description = fields[1];
        project.objective = fields[2];
        project.subject_domain = fields[3];
        project.model_task = fields[4];
        project.owner_id = fields[5];
        project.status = parse_ml_project_status(fields[6]);
        project.created_at_epoch_seconds = std::stoull(fields[7]);
        // updated_at isn't persisted separately (see persist()'s field
        // list) -- restoring it as created_at is the same approximation
        // ChatStore makes for fields it doesn't independently track either.
        project.updated_at_epoch_seconds = project.created_at_epoch_seconds;
        projects_[project.id] = project;
    }
}

void MLProjectStore::persist(const MLProject& project) {
    records_->put(
        "ml_projects", project.id,
        pack({project.name, project.description, project.objective,
             project.subject_domain, project.model_task, project.owner_id,
             ml_project_status_name(project.status),
             std::to_string(project.created_at_epoch_seconds)}));
}

MLProject MLProjectStore::create(const std::string& owner_id,
                                 const std::string& name,
                                 const std::string& description,
                                 const std::string& objective,
                                 const std::string& subject_domain,
                                 const std::string& model_task) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("ML project name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    MLProject project;
    project.id = random_id();
    project.name = name;
    project.description = description;
    project.objective = objective;
    project.subject_domain = subject_domain;
    project.model_task = model_task;
    project.owner_id = owner_id;
    project.status = MLProjectStatus::draft;
    project.created_at_epoch_seconds = epoch_seconds();
    project.updated_at_epoch_seconds = project.created_at_epoch_seconds;
    projects_[project.id] = project;
    if (records_) persist(project);
    return project;
}

std::optional<MLProject> MLProjectStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = projects_.find(id);
    return found != projects_.end() ? std::optional<MLProject>(found->second)
                                    : std::nullopt;
}

std::vector<MLProject> MLProjectStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<MLProject> result;
    result.reserve(projects_.size());
    for (const auto& item : projects_) result.push_back(item.second);
    return result;
}

bool MLProjectStore::set_status(const std::string& id,
                                const MLProjectStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = projects_.find(id);
    if (found == projects_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool MLProjectStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = projects_.find(id);
    if (found == projects_.end()) return false;
    projects_.erase(found);
    if (records_) records_->erase("ml_projects", id);
    return true;
}

std::string ml_project_json(const MLProject& project) {
    return "{\"id\":\"" + json_escape(project.id) + "\",\"name\":\"" +
           json_escape(project.name) + "\",\"description\":\"" +
           json_escape(project.description) + "\",\"objective\":\"" +
           json_escape(project.objective) + "\",\"subjectDomain\":\"" +
           json_escape(project.subject_domain) + "\",\"modelTask\":\"" +
           json_escape(project.model_task) + "\",\"ownerId\":\"" +
           json_escape(project.owner_id) + "\",\"status\":\"" +
           ml_project_status_name(project.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(project.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(project.updated_at_epoch_seconds) + "}";
}

std::string ml_projects_json(const std::vector<MLProject>& projects) {
    std::string body = "[";
    bool first = true;
    for (const auto& project : projects) {
        if (!first) body += ",";
        first = false;
        body += ml_project_json(project);
    }
    return body + "]";
}

std::string model_registry_state_name(const ModelRegistryState state) {
    switch (state) {
        case ModelRegistryState::imported: return "imported";
        case ModelRegistryState::unverified: return "unverified";
        case ModelRegistryState::verified: return "verified";
        case ModelRegistryState::training: return "training";
        case ModelRegistryState::evaluation: return "evaluation";
        case ModelRegistryState::rejected: return "rejected";
        case ModelRegistryState::approved: return "approved";
        case ModelRegistryState::staging: return "staging";
        case ModelRegistryState::production: return "production";
        case ModelRegistryState::deprecated: return "deprecated";
        case ModelRegistryState::archived: return "archived";
        case ModelRegistryState::quarantined: return "quarantined";
    }
    throw std::runtime_error("invalid model registry state");
}

ModelRegistryState parse_model_registry_state(const std::string& state) {
    if (state == "imported") return ModelRegistryState::imported;
    if (state == "unverified") return ModelRegistryState::unverified;
    if (state == "verified") return ModelRegistryState::verified;
    if (state == "training") return ModelRegistryState::training;
    if (state == "evaluation") return ModelRegistryState::evaluation;
    if (state == "rejected") return ModelRegistryState::rejected;
    if (state == "approved") return ModelRegistryState::approved;
    if (state == "staging") return ModelRegistryState::staging;
    if (state == "production") return ModelRegistryState::production;
    if (state == "deprecated") return ModelRegistryState::deprecated;
    if (state == "archived") return ModelRegistryState::archived;
    if (state == "quarantined") return ModelRegistryState::quarantined;
    throw std::runtime_error("stored model registry state is invalid");
}

ModelRegistryStore::ModelRegistryStore(RecordStore& records) : records_(&records) {
    restore();
}

void ModelRegistryStore::restore() {
    for (const auto& item : records_->list("ml_model_registry")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 10U) {
            throw std::runtime_error(
                "persisted model registry record field count is wrong");
        }
        ModelRegistryEntry entry;
        entry.id = item.first;
        entry.name = fields[0];
        entry.display_name = fields[1];
        entry.version = fields[2];
        entry.family = fields[3];
        entry.task = fields[4];
        entry.format = fields[5];
        entry.source = fields[6];
        entry.license = fields[7];
        entry.owner_id = fields[8];
        entry.state = ModelRegistryState::imported;
        entry.created_at_epoch_seconds = 0U;
        entries_[entry.id] = entry;
        // fields[9] (state) and created_at are re-parsed below because the
        // above defaults keep the struct valid even if this loop is later
        // split; see persist() for the authoritative field order.
        entries_[entry.id].state = parse_model_registry_state(fields[9]);
    }
}

void ModelRegistryStore::persist(const ModelRegistryEntry& entry) {
    records_->put(
        "ml_model_registry", entry.id,
        pack({entry.name, entry.display_name, entry.version, entry.family,
             entry.task, entry.format, entry.source, entry.license,
             entry.owner_id, model_registry_state_name(entry.state)}));
}

ModelRegistryEntry ModelRegistryStore::create(
    const std::string& owner_id, const std::string& name,
    const std::string& display_name, const std::string& version,
    const std::string& family, const std::string& task,
    const std::string& format, const std::string& source,
    const std::string& license) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("model registry entry name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ModelRegistryEntry entry;
    entry.id = random_id();
    entry.name = name;
    entry.display_name = display_name;
    entry.version = version;
    entry.family = family;
    entry.task = task;
    entry.format = format;
    entry.source = source;
    entry.license = license;
    entry.owner_id = owner_id;
    entry.state = ModelRegistryState::imported;
    entry.created_at_epoch_seconds = epoch_seconds();
    entry.updated_at_epoch_seconds = entry.created_at_epoch_seconds;
    entries_[entry.id] = entry;
    if (records_) persist(entry);
    return entry;
}

std::optional<ModelRegistryEntry> ModelRegistryStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = entries_.find(id);
    return found != entries_.end() ? std::optional<ModelRegistryEntry>(found->second)
                                   : std::nullopt;
}

std::vector<ModelRegistryEntry> ModelRegistryStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelRegistryEntry> result;
    result.reserve(entries_.size());
    for (const auto& item : entries_) result.push_back(item.second);
    return result;
}

bool ModelRegistryStore::set_state(const std::string& id,
                                   const ModelRegistryState state) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    // Section 7: a model must never reach production merely because
    // training completed -- only an already-approved (or already-staged)
    // entry may move to production.
    if (state == ModelRegistryState::production &&
        found->second.state != ModelRegistryState::approved &&
        found->second.state != ModelRegistryState::staging) {
        throw std::invalid_argument(
            "model registry entry must be approved before production");
    }
    found->second.state = state;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ModelRegistryStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = entries_.find(id);
    if (found == entries_.end()) return false;
    entries_.erase(found);
    if (records_) records_->erase("ml_model_registry", id);
    return true;
}

std::string model_registry_entry_json(const ModelRegistryEntry& entry) {
    return "{\"id\":\"" + json_escape(entry.id) + "\",\"name\":\"" +
           json_escape(entry.name) + "\",\"displayName\":\"" +
           json_escape(entry.display_name) + "\",\"version\":\"" +
           json_escape(entry.version) + "\",\"family\":\"" +
           json_escape(entry.family) + "\",\"task\":\"" +
           json_escape(entry.task) + "\",\"format\":\"" +
           json_escape(entry.format) + "\",\"source\":\"" +
           json_escape(entry.source) + "\",\"license\":\"" +
           json_escape(entry.license) + "\",\"ownerId\":\"" +
           json_escape(entry.owner_id) + "\",\"state\":\"" +
           model_registry_state_name(entry.state) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(entry.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(entry.updated_at_epoch_seconds) + "}";
}

std::string model_registry_entries_json(
    const std::vector<ModelRegistryEntry>& entries) {
    std::string body = "[";
    bool first = true;
    for (const auto& entry : entries) {
        if (!first) body += ",";
        first = false;
        body += model_registry_entry_json(entry);
    }
    return body + "]";
}

std::string dataset_approval_status_name(const DatasetApprovalStatus status) {
    switch (status) {
        case DatasetApprovalStatus::pending: return "pending";
        case DatasetApprovalStatus::approved: return "approved";
        case DatasetApprovalStatus::rejected: return "rejected";
    }
    throw std::runtime_error("invalid dataset approval status");
}

DatasetApprovalStatus parse_dataset_approval_status(const std::string& status) {
    if (status == "pending") return DatasetApprovalStatus::pending;
    if (status == "approved") return DatasetApprovalStatus::approved;
    if (status == "rejected") return DatasetApprovalStatus::rejected;
    throw std::runtime_error("stored dataset approval status is invalid");
}

DatasetStore::DatasetStore(RecordStore& records) : records_(&records) {
    restore();
}

void DatasetStore::restore() {
    for (const auto& item : records_->list("ml_datasets")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error(
                "persisted dataset record field count is wrong");
        }
        Dataset dataset;
        dataset.id = item.first;
        dataset.name = fields[0];
        dataset.description = fields[1];
        dataset.subject_area = fields[2];
        dataset.source = fields[3];
        dataset.license = fields[4];
        dataset.data_format = fields[5];
        dataset.owner_id = fields[6];
        dataset.approval_status = parse_dataset_approval_status(fields[7]);
        datasets_[dataset.id] = dataset;
    }
}

void DatasetStore::persist(const Dataset& dataset) {
    records_->put(
        "ml_datasets", dataset.id,
        pack({dataset.name, dataset.description, dataset.subject_area,
             dataset.source, dataset.license, dataset.data_format,
             dataset.owner_id,
             dataset_approval_status_name(dataset.approval_status)}));
}

Dataset DatasetStore::create(const std::string& owner_id,
                             const std::string& name,
                             const std::string& description,
                             const std::string& subject_area,
                             const std::string& source,
                             const std::string& license,
                             const std::string& data_format) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("dataset name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    Dataset dataset;
    dataset.id = random_id();
    dataset.name = name;
    dataset.description = description;
    dataset.subject_area = subject_area;
    dataset.source = source;
    dataset.license = license;
    dataset.data_format = data_format;
    dataset.owner_id = owner_id;
    dataset.approval_status = DatasetApprovalStatus::pending;
    dataset.created_at_epoch_seconds = epoch_seconds();
    dataset.updated_at_epoch_seconds = dataset.created_at_epoch_seconds;
    datasets_[dataset.id] = dataset;
    if (records_) persist(dataset);
    return dataset;
}

std::optional<Dataset> DatasetStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = datasets_.find(id);
    return found != datasets_.end() ? std::optional<Dataset>(found->second)
                                    : std::nullopt;
}

std::vector<Dataset> DatasetStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Dataset> result;
    result.reserve(datasets_.size());
    for (const auto& item : datasets_) result.push_back(item.second);
    return result;
}

bool DatasetStore::set_approval_status(const std::string& id,
                                       const DatasetApprovalStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = datasets_.find(id);
    if (found == datasets_.end()) return false;
    found->second.approval_status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool DatasetStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = datasets_.find(id);
    if (found == datasets_.end()) return false;
    datasets_.erase(found);
    if (records_) records_->erase("ml_datasets", id);
    return true;
}

std::string dataset_json(const Dataset& dataset) {
    return "{\"id\":\"" + json_escape(dataset.id) + "\",\"name\":\"" +
           json_escape(dataset.name) + "\",\"description\":\"" +
           json_escape(dataset.description) + "\",\"subjectArea\":\"" +
           json_escape(dataset.subject_area) + "\",\"source\":\"" +
           json_escape(dataset.source) + "\",\"license\":\"" +
           json_escape(dataset.license) + "\",\"dataFormat\":\"" +
           json_escape(dataset.data_format) + "\",\"ownerId\":\"" +
           json_escape(dataset.owner_id) + "\",\"approvalStatus\":\"" +
           dataset_approval_status_name(dataset.approval_status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(dataset.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(dataset.updated_at_epoch_seconds) + "}";
}

std::string datasets_json(const std::vector<Dataset>& datasets) {
    std::string body = "[";
    bool first = true;
    for (const auto& dataset : datasets) {
        if (!first) body += ",";
        first = false;
        body += dataset_json(dataset);
    }
    return body + "]";
}

std::string subject_review_status_name(const SubjectReviewStatus status) {
    switch (status) {
        case SubjectReviewStatus::draft: return "draft";
        case SubjectReviewStatus::in_review: return "in_review";
        case SubjectReviewStatus::approved: return "approved";
        case SubjectReviewStatus::needs_revision: return "needs_revision";
        case SubjectReviewStatus::retired: return "retired";
    }
    throw std::runtime_error("invalid subject review status");
}

SubjectReviewStatus parse_subject_review_status(const std::string& status) {
    if (status == "draft") return SubjectReviewStatus::draft;
    if (status == "in_review") return SubjectReviewStatus::in_review;
    if (status == "approved") return SubjectReviewStatus::approved;
    if (status == "needs_revision") return SubjectReviewStatus::needs_revision;
    if (status == "retired") return SubjectReviewStatus::retired;
    throw std::runtime_error("stored subject review status is invalid");
}

SubjectPackageStore::SubjectPackageStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void SubjectPackageStore::restore() {
    for (const auto& item : records_->list("ml_subject_packages")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted subject package record field count is wrong");
        }
        SubjectPackage package;
        package.id = item.first;
        package.name = fields[0];
        package.description = fields[1];
        package.scope = fields[2];
        package.target_audience = fields[3];
        package.owner_id = fields[4];
        package.review_status = parse_subject_review_status(fields[5]);
        packages_[package.id] = package;
    }
}

void SubjectPackageStore::persist(const SubjectPackage& package) {
    records_->put(
        "ml_subject_packages", package.id,
        pack({package.name, package.description, package.scope,
             package.target_audience, package.owner_id,
             subject_review_status_name(package.review_status)}));
}

SubjectPackage SubjectPackageStore::create(const std::string& owner_id,
                                           const std::string& name,
                                           const std::string& description,
                                           const std::string& scope,
                                           const std::string& target_audience) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("subject package name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    SubjectPackage package;
    package.id = random_id();
    package.name = name;
    package.description = description;
    package.scope = scope;
    package.target_audience = target_audience;
    package.owner_id = owner_id;
    package.review_status = SubjectReviewStatus::draft;
    package.created_at_epoch_seconds = epoch_seconds();
    package.updated_at_epoch_seconds = package.created_at_epoch_seconds;
    packages_[package.id] = package;
    if (records_) persist(package);
    return package;
}

std::optional<SubjectPackage> SubjectPackageStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = packages_.find(id);
    return found != packages_.end() ? std::optional<SubjectPackage>(found->second)
                                    : std::nullopt;
}

std::vector<SubjectPackage> SubjectPackageStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SubjectPackage> result;
    result.reserve(packages_.size());
    for (const auto& item : packages_) result.push_back(item.second);
    return result;
}

bool SubjectPackageStore::set_review_status(const std::string& id,
                                            const SubjectReviewStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = packages_.find(id);
    if (found == packages_.end()) return false;
    found->second.review_status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool SubjectPackageStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = packages_.find(id);
    if (found == packages_.end()) return false;
    packages_.erase(found);
    if (records_) records_->erase("ml_subject_packages", id);
    return true;
}

std::string subject_package_json(const SubjectPackage& package) {
    return "{\"id\":\"" + json_escape(package.id) + "\",\"name\":\"" +
           json_escape(package.name) + "\",\"description\":\"" +
           json_escape(package.description) + "\",\"scope\":\"" +
           json_escape(package.scope) + "\",\"targetAudience\":\"" +
           json_escape(package.target_audience) + "\",\"ownerId\":\"" +
           json_escape(package.owner_id) + "\",\"reviewStatus\":\"" +
           subject_review_status_name(package.review_status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(package.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(package.updated_at_epoch_seconds) + "}";
}

std::string subject_packages_json(const std::vector<SubjectPackage>& packages) {
    std::string body = "[";
    bool first = true;
    for (const auto& package : packages) {
        if (!first) body += ",";
        first = false;
        body += subject_package_json(package);
    }
    return body + "]";
}

std::string label_task_status_name(const LabelTaskStatus status) {
    switch (status) {
        case LabelTaskStatus::queued: return "queued";
        case LabelTaskStatus::in_progress: return "in_progress";
        case LabelTaskStatus::in_review: return "in_review";
        case LabelTaskStatus::completed: return "completed";
    }
    throw std::runtime_error("invalid label task status");
}

LabelTaskStatus parse_label_task_status(const std::string& status) {
    if (status == "queued") return LabelTaskStatus::queued;
    if (status == "in_progress") return LabelTaskStatus::in_progress;
    if (status == "in_review") return LabelTaskStatus::in_review;
    if (status == "completed") return LabelTaskStatus::completed;
    throw std::runtime_error("stored label task status is invalid");
}

LabelTaskStore::LabelTaskStore(RecordStore& records) : records_(&records) {
    restore();
}

void LabelTaskStore::restore() {
    for (const auto& item : records_->list("ml_label_tasks")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 7U) {
            throw std::runtime_error(
                "persisted label task record field count is wrong");
        }
        LabelTask task;
        task.id = item.first;
        task.dataset_id = fields[0];
        task.name = fields[1];
        task.description = fields[2];
        task.label_mode = fields[3];
        task.assignee_id = fields[4];
        task.owner_id = fields[5];
        task.status = parse_label_task_status(fields[6]);
        tasks_[task.id] = task;
    }
}

void LabelTaskStore::persist(const LabelTask& task) {
    records_->put(
        "ml_label_tasks", task.id,
        pack({task.dataset_id, task.name, task.description, task.label_mode,
             task.assignee_id, task.owner_id,
             label_task_status_name(task.status)}));
}

LabelTask LabelTaskStore::create(const std::string& owner_id,
                                 const std::string& dataset_id,
                                 const std::string& name,
                                 const std::string& description,
                                 const std::string& label_mode,
                                 const std::string& assignee_id) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("label task name is invalid");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument("label task dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    LabelTask task;
    task.id = random_id();
    task.dataset_id = dataset_id;
    task.name = name;
    task.description = description;
    task.label_mode = label_mode;
    task.assignee_id = assignee_id;
    task.owner_id = owner_id;
    task.status = LabelTaskStatus::queued;
    task.created_at_epoch_seconds = epoch_seconds();
    task.updated_at_epoch_seconds = task.created_at_epoch_seconds;
    tasks_[task.id] = task;
    if (records_) persist(task);
    return task;
}

std::optional<LabelTask> LabelTaskStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tasks_.find(id);
    return found != tasks_.end() ? std::optional<LabelTask>(found->second)
                                 : std::nullopt;
}

std::vector<LabelTask> LabelTaskStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<LabelTask> result;
    result.reserve(tasks_.size());
    for (const auto& item : tasks_) result.push_back(item.second);
    return result;
}

bool LabelTaskStore::set_status(const std::string& id,
                                const LabelTaskStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tasks_.find(id);
    if (found == tasks_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool LabelTaskStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tasks_.find(id);
    if (found == tasks_.end()) return false;
    tasks_.erase(found);
    if (records_) records_->erase("ml_label_tasks", id);
    return true;
}

std::string label_task_json(const LabelTask& task) {
    return "{\"id\":\"" + json_escape(task.id) + "\",\"datasetId\":\"" +
           json_escape(task.dataset_id) + "\",\"name\":\"" +
           json_escape(task.name) + "\",\"description\":\"" +
           json_escape(task.description) + "\",\"labelMode\":\"" +
           json_escape(task.label_mode) + "\",\"assigneeId\":\"" +
           json_escape(task.assignee_id) + "\",\"ownerId\":\"" +
           json_escape(task.owner_id) + "\",\"status\":\"" +
           label_task_status_name(task.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(task.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(task.updated_at_epoch_seconds) + "}";
}

std::string label_tasks_json(const std::vector<LabelTask>& tasks) {
    std::string body = "[";
    bool first = true;
    for (const auto& task : tasks) {
        if (!first) body += ",";
        first = false;
        body += label_task_json(task);
    }
    return body + "]";
}

std::string data_preparation_job_status_name(
    const DataPreparationJobStatus status) {
    switch (status) {
        case DataPreparationJobStatus::pending: return "pending";
        case DataPreparationJobStatus::running: return "running";
        case DataPreparationJobStatus::completed: return "completed";
        case DataPreparationJobStatus::failed: return "failed";
    }
    throw std::runtime_error("invalid data preparation job status");
}

DataPreparationJobStatus parse_data_preparation_job_status(
    const std::string& status) {
    if (status == "pending") return DataPreparationJobStatus::pending;
    if (status == "running") return DataPreparationJobStatus::running;
    if (status == "completed") return DataPreparationJobStatus::completed;
    if (status == "failed") return DataPreparationJobStatus::failed;
    throw std::runtime_error("stored data preparation job status is invalid");
}

DataPreparationJobStore::DataPreparationJobStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void DataPreparationJobStore::restore() {
    for (const auto& item : records_->list("ml_data_preparation_jobs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted data preparation job record field count is "
                "wrong");
        }
        DataPreparationJob job;
        job.id = item.first;
        job.dataset_id = fields[0];
        job.name = fields[1];
        job.description = fields[2];
        job.operation = fields[3];
        job.owner_id = fields[4];
        job.status = parse_data_preparation_job_status(fields[5]);
        jobs_[job.id] = job;
    }
}

void DataPreparationJobStore::persist(const DataPreparationJob& job) {
    records_->put(
        "ml_data_preparation_jobs", job.id,
        pack({job.dataset_id, job.name, job.description, job.operation,
             job.owner_id,
             data_preparation_job_status_name(job.status)}));
}

DataPreparationJob DataPreparationJobStore::create(
    const std::string& owner_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& operation) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("data preparation job name is invalid");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument(
            "data preparation job dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    DataPreparationJob job;
    job.id = random_id();
    job.dataset_id = dataset_id;
    job.name = name;
    job.description = description;
    job.operation = operation;
    job.owner_id = owner_id;
    job.status = DataPreparationJobStatus::pending;
    job.created_at_epoch_seconds = epoch_seconds();
    job.updated_at_epoch_seconds = job.created_at_epoch_seconds;
    jobs_[job.id] = job;
    if (records_) persist(job);
    return job;
}

std::optional<DataPreparationJob> DataPreparationJobStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    return found != jobs_.end() ? std::optional<DataPreparationJob>(found->second)
                                : std::nullopt;
}

std::vector<DataPreparationJob> DataPreparationJobStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DataPreparationJob> result;
    result.reserve(jobs_.size());
    for (const auto& item : jobs_) result.push_back(item.second);
    return result;
}

bool DataPreparationJobStore::set_status(
    const std::string& id, const DataPreparationJobStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool DataPreparationJobStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    jobs_.erase(found);
    if (records_) records_->erase("ml_data_preparation_jobs", id);
    return true;
}

std::string data_preparation_job_json(const DataPreparationJob& job) {
    return "{\"id\":\"" + json_escape(job.id) + "\",\"datasetId\":\"" +
           json_escape(job.dataset_id) + "\",\"name\":\"" +
           json_escape(job.name) + "\",\"description\":\"" +
           json_escape(job.description) + "\",\"operation\":\"" +
           json_escape(job.operation) + "\",\"ownerId\":\"" +
           json_escape(job.owner_id) + "\",\"status\":\"" +
           data_preparation_job_status_name(job.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(job.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(job.updated_at_epoch_seconds) + "}";
}

std::string data_preparation_jobs_json(
    const std::vector<DataPreparationJob>& jobs) {
    std::string body = "[";
    bool first = true;
    for (const auto& job : jobs) {
        if (!first) body += ",";
        first = false;
        body += data_preparation_job_json(job);
    }
    return body + "]";
}

std::string training_job_status_name(const TrainingJobStatus status) {
    switch (status) {
        case TrainingJobStatus::draft: return "draft";
        case TrainingJobStatus::queued: return "queued";
        case TrainingJobStatus::preparing: return "preparing";
        case TrainingJobStatus::running: return "running";
        case TrainingJobStatus::paused: return "paused";
        case TrainingJobStatus::canceling: return "canceling";
        case TrainingJobStatus::canceled: return "canceled";
        case TrainingJobStatus::failed: return "failed";
        case TrainingJobStatus::completed: return "completed";
        case TrainingJobStatus::awaiting_evaluation: return "awaiting_evaluation";
        case TrainingJobStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid training job status");
}

TrainingJobStatus parse_training_job_status(const std::string& status) {
    if (status == "draft") return TrainingJobStatus::draft;
    if (status == "queued") return TrainingJobStatus::queued;
    if (status == "preparing") return TrainingJobStatus::preparing;
    if (status == "running") return TrainingJobStatus::running;
    if (status == "paused") return TrainingJobStatus::paused;
    if (status == "canceling") return TrainingJobStatus::canceling;
    if (status == "canceled") return TrainingJobStatus::canceled;
    if (status == "failed") return TrainingJobStatus::failed;
    if (status == "completed") return TrainingJobStatus::completed;
    if (status == "awaiting_evaluation") return TrainingJobStatus::awaiting_evaluation;
    if (status == "archived") return TrainingJobStatus::archived;
    throw std::runtime_error("stored training job status is invalid");
}

TrainingJobStore::TrainingJobStore(RecordStore& records) : records_(&records) {
    restore();
}

void TrainingJobStore::restore() {
    for (const auto& item : records_->list("ml_training_jobs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error(
                "persisted training job record field count is wrong");
        }
        TrainingJob job;
        job.id = item.first;
        job.project_id = fields[0];
        job.model_id = fields[1];
        job.dataset_id = fields[2];
        job.name = fields[3];
        job.description = fields[4];
        job.training_type = fields[5];
        job.owner_id = fields[6];
        job.status = parse_training_job_status(fields[7]);
        jobs_[job.id] = job;
    }
}

void TrainingJobStore::persist(const TrainingJob& job) {
    records_->put(
        "ml_training_jobs", job.id,
        pack({job.project_id, job.model_id, job.dataset_id, job.name,
             job.description, job.training_type, job.owner_id,
             training_job_status_name(job.status)}));
}

TrainingJob TrainingJobStore::create(
    const std::string& owner_id, const std::string& project_id,
    const std::string& model_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& training_type) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("training job name is invalid");
    }
    if (project_id.empty()) {
        throw std::invalid_argument("training job project id is required");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument("training job dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    TrainingJob job;
    job.id = random_id();
    job.project_id = project_id;
    job.model_id = model_id;
    job.dataset_id = dataset_id;
    job.name = name;
    job.description = description;
    job.training_type = training_type;
    job.owner_id = owner_id;
    job.status = TrainingJobStatus::draft;
    job.created_at_epoch_seconds = epoch_seconds();
    job.updated_at_epoch_seconds = job.created_at_epoch_seconds;
    jobs_[job.id] = job;
    if (records_) persist(job);
    return job;
}

std::optional<TrainingJob> TrainingJobStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    return found != jobs_.end() ? std::optional<TrainingJob>(found->second)
                                : std::nullopt;
}

std::vector<TrainingJob> TrainingJobStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TrainingJob> result;
    result.reserve(jobs_.size());
    for (const auto& item : jobs_) result.push_back(item.second);
    return result;
}

bool TrainingJobStore::set_status(const std::string& id,
                                  const TrainingJobStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool TrainingJobStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    jobs_.erase(found);
    if (records_) records_->erase("ml_training_jobs", id);
    return true;
}

std::string training_job_json(const TrainingJob& job) {
    return "{\"id\":\"" + json_escape(job.id) + "\",\"projectId\":\"" +
           json_escape(job.project_id) + "\",\"modelId\":\"" +
           json_escape(job.model_id) + "\",\"datasetId\":\"" +
           json_escape(job.dataset_id) + "\",\"name\":\"" +
           json_escape(job.name) + "\",\"description\":\"" +
           json_escape(job.description) + "\",\"trainingType\":\"" +
           json_escape(job.training_type) + "\",\"ownerId\":\"" +
           json_escape(job.owner_id) + "\",\"status\":\"" +
           training_job_status_name(job.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(job.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(job.updated_at_epoch_seconds) + "}";
}

std::string training_jobs_json(const std::vector<TrainingJob>& jobs) {
    std::string body = "[";
    bool first = true;
    for (const auto& job : jobs) {
        if (!first) body += ",";
        first = false;
        body += training_job_json(job);
    }
    return body + "]";
}

std::string evaluation_run_status_name(const EvaluationRunStatus status) {
    switch (status) {
        case EvaluationRunStatus::queued: return "queued";
        case EvaluationRunStatus::running: return "running";
        case EvaluationRunStatus::completed: return "completed";
        case EvaluationRunStatus::failed: return "failed";
        case EvaluationRunStatus::canceled: return "canceled";
    }
    throw std::runtime_error("invalid evaluation run status");
}

EvaluationRunStatus parse_evaluation_run_status(const std::string& status) {
    if (status == "queued") return EvaluationRunStatus::queued;
    if (status == "running") return EvaluationRunStatus::running;
    if (status == "completed") return EvaluationRunStatus::completed;
    if (status == "failed") return EvaluationRunStatus::failed;
    if (status == "canceled") return EvaluationRunStatus::canceled;
    throw std::runtime_error("stored evaluation run status is invalid");
}

EvaluationRunStore::EvaluationRunStore(RecordStore& records) : records_(&records) {
    restore();
}

void EvaluationRunStore::restore() {
    for (const auto& item : records_->list("ml_evaluation_runs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 7U) {
            throw std::runtime_error(
                "persisted evaluation run record field count is wrong");
        }
        EvaluationRun run;
        run.id = item.first;
        run.model_id = fields[0];
        run.dataset_id = fields[1];
        run.name = fields[2];
        run.description = fields[3];
        run.category = fields[4];
        run.owner_id = fields[5];
        run.status = parse_evaluation_run_status(fields[6]);
        runs_[run.id] = run;
    }
}

void EvaluationRunStore::persist(const EvaluationRun& run) {
    records_->put(
        "ml_evaluation_runs", run.id,
        pack({run.model_id, run.dataset_id, run.name, run.description,
             run.category, run.owner_id,
             evaluation_run_status_name(run.status)}));
}

EvaluationRun EvaluationRunStore::create(
    const std::string& owner_id, const std::string& model_id,
    const std::string& dataset_id, const std::string& name,
    const std::string& description, const std::string& category) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("evaluation run name is invalid");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("evaluation run model id is required");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument("evaluation run dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    EvaluationRun run;
    run.id = random_id();
    run.model_id = model_id;
    run.dataset_id = dataset_id;
    run.name = name;
    run.description = description;
    run.category = category;
    run.owner_id = owner_id;
    run.status = EvaluationRunStatus::queued;
    run.created_at_epoch_seconds = epoch_seconds();
    run.updated_at_epoch_seconds = run.created_at_epoch_seconds;
    runs_[run.id] = run;
    if (records_) persist(run);
    return run;
}

std::optional<EvaluationRun> EvaluationRunStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    return found != runs_.end() ? std::optional<EvaluationRun>(found->second)
                                : std::nullopt;
}

std::vector<EvaluationRun> EvaluationRunStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<EvaluationRun> result;
    result.reserve(runs_.size());
    for (const auto& item : runs_) result.push_back(item.second);
    return result;
}

bool EvaluationRunStore::set_status(const std::string& id,
                                    const EvaluationRunStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    if (found == runs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool EvaluationRunStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    if (found == runs_.end()) return false;
    runs_.erase(found);
    if (records_) records_->erase("ml_evaluation_runs", id);
    return true;
}

std::string evaluation_run_json(const EvaluationRun& run) {
    return "{\"id\":\"" + json_escape(run.id) + "\",\"modelId\":\"" +
           json_escape(run.model_id) + "\",\"datasetId\":\"" +
           json_escape(run.dataset_id) + "\",\"name\":\"" +
           json_escape(run.name) + "\",\"description\":\"" +
           json_escape(run.description) + "\",\"category\":\"" +
           json_escape(run.category) + "\",\"ownerId\":\"" +
           json_escape(run.owner_id) + "\",\"status\":\"" +
           evaluation_run_status_name(run.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(run.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(run.updated_at_epoch_seconds) + "}";
}

std::string evaluation_runs_json(const std::vector<EvaluationRun>& runs) {
    std::string body = "[";
    bool first = true;
    for (const auto& run : runs) {
        if (!first) body += ",";
        first = false;
        body += evaluation_run_json(run);
    }
    return body + "]";
}

std::string experiment_status_name(const ExperimentStatus status) {
    switch (status) {
        case ExperimentStatus::queued: return "queued";
        case ExperimentStatus::running: return "running";
        case ExperimentStatus::completed: return "completed";
        case ExperimentStatus::failed: return "failed";
        case ExperimentStatus::canceled: return "canceled";
    }
    throw std::runtime_error("invalid experiment status");
}

ExperimentStatus parse_experiment_status(const std::string& status) {
    if (status == "queued") return ExperimentStatus::queued;
    if (status == "running") return ExperimentStatus::running;
    if (status == "completed") return ExperimentStatus::completed;
    if (status == "failed") return ExperimentStatus::failed;
    if (status == "canceled") return ExperimentStatus::canceled;
    throw std::runtime_error("stored experiment status is invalid");
}

ExperimentStore::ExperimentStore(RecordStore& records) : records_(&records) {
    restore();
}

void ExperimentStore::restore() {
    for (const auto& item : records_->list("ml_experiments")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 19U) {
            throw std::runtime_error(
                "persisted experiment record field count is wrong");
        }
        Experiment experiment;
        experiment.id = item.first;
        experiment.project_id = fields[0];
        experiment.model_id = fields[1];
        experiment.dataset_id = fields[2];
        experiment.name = fields[3];
        experiment.description = fields[4];
        experiment.owner_id = fields[5];
        experiment.status = parse_experiment_status(fields[6]);
        experiment.hyperparameters_json = fields[7];
        experiment.random_seed =
            static_cast<std::uint32_t>(std::stoull(fields[8]));
        experiment.source_code_version = fields[9];
        experiment.configuration_version = fields[10];
        experiment.container_version = fields[11];
        experiment.tags = fields[12];
        experiment.notes = fields[13];
        experiment.started_at_epoch_seconds =
            std::stoull(fields[14].empty() ? "0" : fields[14]);
        experiment.completed_at_epoch_seconds =
            std::stoull(fields[15].empty() ? "0" : fields[15]);
        experiment.failure_reason = fields[16];
        experiment.created_at_epoch_seconds =
            std::stoull(fields[17].empty() ? "0" : fields[17]);
        experiment.updated_at_epoch_seconds =
            std::stoull(fields[18].empty() ? "0" : fields[18]);
        experiments_[experiment.id] = experiment;
    }
}

void ExperimentStore::persist(const Experiment& experiment) {
    records_->put(
        "ml_experiments", experiment.id,
        pack({experiment.project_id, experiment.model_id,
             experiment.dataset_id, experiment.name, experiment.description,
             experiment.owner_id, experiment_status_name(experiment.status),
             experiment.hyperparameters_json,
             std::to_string(experiment.random_seed),
             experiment.source_code_version,
             experiment.configuration_version, experiment.container_version,
             experiment.tags, experiment.notes,
             std::to_string(experiment.started_at_epoch_seconds),
             std::to_string(experiment.completed_at_epoch_seconds),
             experiment.failure_reason,
             std::to_string(experiment.created_at_epoch_seconds),
             std::to_string(experiment.updated_at_epoch_seconds)}));
}

Experiment ExperimentStore::create(
    const std::string& owner_id, const std::string& project_id,
    const std::string& model_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& hyperparameters_json, std::uint32_t random_seed,
    const std::string& source_code_version,
    const std::string& configuration_version,
    const std::string& container_version, const std::string& tags,
    const std::string& notes) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("experiment name is invalid");
    }
    if (project_id.empty()) {
        throw std::invalid_argument("experiment project id is required");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("experiment model id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    Experiment experiment;
    experiment.id = random_id();
    experiment.project_id = project_id;
    experiment.model_id = model_id;
    experiment.dataset_id = dataset_id;
    experiment.name = name;
    experiment.description = description;
    experiment.owner_id = owner_id;
    experiment.status = ExperimentStatus::queued;
    experiment.hyperparameters_json = hyperparameters_json;
    experiment.random_seed = random_seed;
    experiment.source_code_version = source_code_version;
    experiment.configuration_version = configuration_version;
    experiment.container_version = container_version;
    experiment.tags = tags;
    experiment.notes = notes;
    experiment.created_at_epoch_seconds = epoch_seconds();
    experiment.updated_at_epoch_seconds = experiment.created_at_epoch_seconds;
    experiments_[experiment.id] = experiment;
    if (records_) persist(experiment);
    return experiment;
}

std::optional<Experiment> ExperimentStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    return found != experiments_.end() ? std::optional<Experiment>(found->second)
                                       : std::nullopt;
}

std::vector<Experiment> ExperimentStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Experiment> result;
    result.reserve(experiments_.size());
    for (const auto& item : experiments_) result.push_back(item.second);
    return result;
}

bool ExperimentStore::set_status(const std::string& id,
                                 const ExperimentStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    if (found == experiments_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ExperimentStore::mark_started(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    if (found == experiments_.end()) return false;
    found->second.status = ExperimentStatus::running;
    found->second.started_at_epoch_seconds = epoch_seconds();
    found->second.failure_reason.clear();
    found->second.updated_at_epoch_seconds = found->second.started_at_epoch_seconds;
    if (records_) persist(found->second);
    return true;
}

bool ExperimentStore::mark_completed(const std::string& id,
                                     const ExperimentStatus status,
                                     const std::string& failure_reason) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    if (found == experiments_.end()) return false;
    found->second.status = status;
    found->second.failure_reason = failure_reason;
    found->second.completed_at_epoch_seconds = epoch_seconds();
    found->second.updated_at_epoch_seconds = found->second.completed_at_epoch_seconds;
    if (records_) persist(found->second);
    return true;
}

bool ExperimentStore::update_metadata(
    const std::string& id, const std::string& hyperparameters_json,
    std::uint32_t random_seed, const std::string& source_code_version,
    const std::string& configuration_version,
    const std::string& container_version, const std::string& tags,
    const std::string& notes) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    if (found == experiments_.end()) return false;
    found->second.hyperparameters_json = hyperparameters_json;
    found->second.random_seed = random_seed;
    found->second.source_code_version = source_code_version;
    found->second.configuration_version = configuration_version;
    found->second.container_version = container_version;
    found->second.tags = tags;
    found->second.notes = notes;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ExperimentStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = experiments_.find(id);
    if (found == experiments_.end()) return false;
    experiments_.erase(found);
    if (records_) records_->erase("ml_experiments", id);
    return true;
}

std::string experiment_json(const Experiment& experiment) {
    return "{\"id\":\"" + json_escape(experiment.id) + "\",\"projectId\":\"" +
           json_escape(experiment.project_id) + "\",\"modelId\":\"" +
           json_escape(experiment.model_id) + "\",\"datasetId\":\"" +
           json_escape(experiment.dataset_id) + "\",\"name\":\"" +
           json_escape(experiment.name) + "\",\"description\":\"" +
           json_escape(experiment.description) + "\",\"ownerId\":\"" +
           json_escape(experiment.owner_id) + "\",\"status\":\"" +
           experiment_status_name(experiment.status) +
           "\",\"hyperparametersJson\":\"" +
           json_escape(experiment.hyperparameters_json) +
           "\",\"randomSeed\":" + std::to_string(experiment.random_seed) +
           ",\"sourceCodeVersion\":\"" +
           json_escape(experiment.source_code_version) +
           "\",\"configurationVersion\":\"" +
           json_escape(experiment.configuration_version) +
           "\",\"containerVersion\":\"" +
           json_escape(experiment.container_version) + "\",\"tags\":\"" +
           json_escape(experiment.tags) + "\",\"notes\":\"" +
           json_escape(experiment.notes) +
           "\",\"startedAtEpochSeconds\":" +
           std::to_string(experiment.started_at_epoch_seconds) +
           ",\"completedAtEpochSeconds\":" +
           std::to_string(experiment.completed_at_epoch_seconds) +
           ",\"failureReason\":\"" + json_escape(experiment.failure_reason) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(experiment.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(experiment.updated_at_epoch_seconds) + "}";
}

std::string experiments_json(const std::vector<Experiment>& experiments) {
    std::string body = "[";
    bool first = true;
    for (const auto& experiment : experiments) {
        if (!first) body += ",";
        first = false;
        body += experiment_json(experiment);
    }
    return body + "]";
}

std::string fine_tuning_job_status_name(const FineTuningJobStatus status) {
    switch (status) {
        case FineTuningJobStatus::draft: return "draft";
        case FineTuningJobStatus::queued: return "queued";
        case FineTuningJobStatus::preparing: return "preparing";
        case FineTuningJobStatus::running: return "running";
        case FineTuningJobStatus::paused: return "paused";
        case FineTuningJobStatus::canceling: return "canceling";
        case FineTuningJobStatus::canceled: return "canceled";
        case FineTuningJobStatus::failed: return "failed";
        case FineTuningJobStatus::completed: return "completed";
        case FineTuningJobStatus::awaiting_evaluation: return "awaiting_evaluation";
        case FineTuningJobStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid fine-tuning job status");
}

FineTuningJobStatus parse_fine_tuning_job_status(const std::string& status) {
    if (status == "draft") return FineTuningJobStatus::draft;
    if (status == "queued") return FineTuningJobStatus::queued;
    if (status == "preparing") return FineTuningJobStatus::preparing;
    if (status == "running") return FineTuningJobStatus::running;
    if (status == "paused") return FineTuningJobStatus::paused;
    if (status == "canceling") return FineTuningJobStatus::canceling;
    if (status == "canceled") return FineTuningJobStatus::canceled;
    if (status == "failed") return FineTuningJobStatus::failed;
    if (status == "completed") return FineTuningJobStatus::completed;
    if (status == "awaiting_evaluation") return FineTuningJobStatus::awaiting_evaluation;
    if (status == "archived") return FineTuningJobStatus::archived;
    throw std::runtime_error("stored fine-tuning job status is invalid");
}

FineTuningJobStore::FineTuningJobStore(RecordStore& records) : records_(&records) {
    restore();
}

void FineTuningJobStore::restore() {
    for (const auto& item : records_->list("ml_fine_tuning_jobs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error(
                "persisted fine-tuning job record field count is wrong");
        }
        FineTuningJob job;
        job.id = item.first;
        job.project_id = fields[0];
        job.model_id = fields[1];
        job.dataset_id = fields[2];
        job.name = fields[3];
        job.description = fields[4];
        job.method = fields[5];
        job.owner_id = fields[6];
        job.status = parse_fine_tuning_job_status(fields[7]);
        jobs_[job.id] = job;
    }
}

void FineTuningJobStore::persist(const FineTuningJob& job) {
    records_->put(
        "ml_fine_tuning_jobs", job.id,
        pack({job.project_id, job.model_id, job.dataset_id, job.name,
             job.description, job.method, job.owner_id,
             fine_tuning_job_status_name(job.status)}));
}

FineTuningJob FineTuningJobStore::create(
    const std::string& owner_id, const std::string& project_id,
    const std::string& model_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& method) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("fine-tuning job name is invalid");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("fine-tuning job base model id is required");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument("fine-tuning job dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    FineTuningJob job;
    job.id = random_id();
    job.project_id = project_id;
    job.model_id = model_id;
    job.dataset_id = dataset_id;
    job.name = name;
    job.description = description;
    job.method = method;
    job.owner_id = owner_id;
    job.status = FineTuningJobStatus::draft;
    job.created_at_epoch_seconds = epoch_seconds();
    job.updated_at_epoch_seconds = job.created_at_epoch_seconds;
    jobs_[job.id] = job;
    if (records_) persist(job);
    return job;
}

std::optional<FineTuningJob> FineTuningJobStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    return found != jobs_.end() ? std::optional<FineTuningJob>(found->second)
                                : std::nullopt;
}

std::vector<FineTuningJob> FineTuningJobStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<FineTuningJob> result;
    result.reserve(jobs_.size());
    for (const auto& item : jobs_) result.push_back(item.second);
    return result;
}

bool FineTuningJobStore::set_status(const std::string& id,
                                    const FineTuningJobStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool FineTuningJobStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) return false;
    jobs_.erase(found);
    if (records_) records_->erase("ml_fine_tuning_jobs", id);
    return true;
}

std::string fine_tuning_job_json(const FineTuningJob& job) {
    return "{\"id\":\"" + json_escape(job.id) + "\",\"projectId\":\"" +
           json_escape(job.project_id) + "\",\"modelId\":\"" +
           json_escape(job.model_id) + "\",\"datasetId\":\"" +
           json_escape(job.dataset_id) + "\",\"name\":\"" +
           json_escape(job.name) + "\",\"description\":\"" +
           json_escape(job.description) + "\",\"method\":\"" +
           json_escape(job.method) + "\",\"ownerId\":\"" +
           json_escape(job.owner_id) + "\",\"status\":\"" +
           fine_tuning_job_status_name(job.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(job.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(job.updated_at_epoch_seconds) + "}";
}

std::string fine_tuning_jobs_json(const std::vector<FineTuningJob>& jobs) {
    std::string body = "[";
    bool first = true;
    for (const auto& job : jobs) {
        if (!first) body += ",";
        first = false;
        body += fine_tuning_job_json(job);
    }
    return body + "]";
}

std::string model_builder_config_status_name(const ModelBuilderConfigStatus status) {
    switch (status) {
        case ModelBuilderConfigStatus::draft: return "draft";
        case ModelBuilderConfigStatus::configuring: return "configuring";
        case ModelBuilderConfigStatus::ready: return "ready";
        case ModelBuilderConfigStatus::submitted: return "submitted";
        case ModelBuilderConfigStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid model builder config status");
}

ModelBuilderConfigStatus parse_model_builder_config_status(const std::string& status) {
    if (status == "draft") return ModelBuilderConfigStatus::draft;
    if (status == "configuring") return ModelBuilderConfigStatus::configuring;
    if (status == "ready") return ModelBuilderConfigStatus::ready;
    if (status == "submitted") return ModelBuilderConfigStatus::submitted;
    if (status == "archived") return ModelBuilderConfigStatus::archived;
    throw std::runtime_error("stored model builder config status is invalid");
}

namespace {

// Compact decimal text for the two fractional build settings (dropout,
// gradient clipping): std::to_string's fixed six decimals ("0.100000")
// would leak into both the persisted record and the JSON the web UI
// displays, so trim trailing zeros (and a bare trailing dot) instead.
std::string format_settings_double(const double value) {
    std::string text = std::to_string(value);
    while (!text.empty() && text.back() == '0') text.pop_back();
    if (!text.empty() && text.back() == '.') text.pop_back();
    return text.empty() ? "0" : text;
}

// docs/PLAN.md section 9: "The interface must provide basic and advanced
// configuration modes" -- those two modes are the whole closed set, and the
// two fractional settings have hard numeric ranges, so configure() rejects
// out-of-range values instead of persisting nonsense.
void validate_model_builder_settings(const ModelBuilderSettings& settings) {
    if (settings.configuration_mode != "basic" &&
        settings.configuration_mode != "advanced") {
        throw std::invalid_argument(
            "model builder configuration mode must be basic or advanced");
    }
    if (settings.dropout < 0.0 || settings.dropout > 1.0) {
        throw std::invalid_argument(
            "model builder dropout must be between 0.0 and 1.0");
    }
    if (settings.gradient_clipping < 0.0) {
        throw std::invalid_argument(
            "model builder gradient clipping must not be negative");
    }
}

}  // namespace

ModelBuilderConfigStore::ModelBuilderConfigStore(RecordStore& records) : records_(&records) {
    restore();
}

void ModelBuilderConfigStore::restore() {
    for (const auto& item : records_->list("ml_model_builder_configs")) {
        const auto fields = unpack(item.second);
        // 7 fields is the legacy scoped-down Phase 46 record (identity/
        // project/base-model/source-type/status only); 31 is the full-
        // surface record with the 24 ModelBuilderSettings fields appended.
        // Legacy records restore with default (unset) settings so existing
        // databases keep working without a migration step.
        if (fields.size() != 7U && fields.size() != 31U) {
            throw std::runtime_error(
                "persisted model builder config record field count is wrong");
        }
        ModelBuilderConfig config;
        config.id = item.first;
        config.project_id = fields[0];
        config.base_model_id = fields[1];
        config.name = fields[2];
        config.description = fields[3];
        config.source_type = fields[4];
        config.owner_id = fields[5];
        config.status = parse_model_builder_config_status(fields[6]);
        if (fields.size() == 31U) {
            auto& s = config.settings;
            s.configuration_mode = fields[7];
            s.architecture = fields[8];
            s.layer_configuration = fields[9];
            s.hidden_dimensions = std::stoull(fields[10]);
            s.attention_configuration = fields[11];
            s.vocabulary_tokenizer = fields[12];
            s.sequence_length = std::stoull(fields[13]);
            s.activation_functions = fields[14];
            s.dropout = std::stod(fields[15]);
            s.initialisation_strategy = fields[16];
            s.loss_function = fields[17];
            s.optimiser = fields[18];
            s.learning_rate_scheduler = fields[19];
            s.batch_size = std::stoull(fields[20]);
            s.epoch_count = std::stoull(fields[21]);
            s.gradient_accumulation = std::stoull(fields[22]);
            s.gradient_clipping = std::stod(fields[23]);
            s.mixed_precision = fields[24] == "1";
            s.checkpoint_frequency = std::stoull(fields[25]);
            s.validation_frequency = std::stoull(fields[26]);
            s.early_stopping = fields[27] == "1";
            s.random_seed = std::stoull(fields[28]);
            s.reproducibility_settings = fields[29];
            s.distributed_training_settings = fields[30];
        }
        configs_[config.id] = config;
    }
}

void ModelBuilderConfigStore::persist(const ModelBuilderConfig& config) {
    const auto& s = config.settings;
    records_->put(
        "ml_model_builder_configs", config.id,
        pack({config.project_id, config.base_model_id, config.name,
             config.description, config.source_type, config.owner_id,
             model_builder_config_status_name(config.status),
             s.configuration_mode, s.architecture, s.layer_configuration,
             std::to_string(s.hidden_dimensions), s.attention_configuration,
             s.vocabulary_tokenizer, std::to_string(s.sequence_length),
             s.activation_functions, format_settings_double(s.dropout),
             s.initialisation_strategy, s.loss_function, s.optimiser,
             s.learning_rate_scheduler, std::to_string(s.batch_size),
             std::to_string(s.epoch_count),
             std::to_string(s.gradient_accumulation),
             format_settings_double(s.gradient_clipping),
             s.mixed_precision ? "1" : "0",
             std::to_string(s.checkpoint_frequency),
             std::to_string(s.validation_frequency),
             s.early_stopping ? "1" : "0", std::to_string(s.random_seed),
             s.reproducibility_settings, s.distributed_training_settings}));
}

ModelBuilderConfig ModelBuilderConfigStore::create(
    const std::string& owner_id, const std::string& project_id,
    const std::string& base_model_id, const std::string& name,
    const std::string& description, const std::string& source_type) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("model builder config name is invalid");
    }
    if (source_type.empty()) {
        throw std::invalid_argument("model builder config source type is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ModelBuilderConfig config;
    config.id = random_id();
    config.project_id = project_id;
    config.base_model_id = base_model_id;
    config.name = name;
    config.description = description;
    config.source_type = source_type;
    config.owner_id = owner_id;
    config.status = ModelBuilderConfigStatus::draft;
    config.created_at_epoch_seconds = epoch_seconds();
    config.updated_at_epoch_seconds = config.created_at_epoch_seconds;
    configs_[config.id] = config;
    if (records_) persist(config);
    return config;
}

std::optional<ModelBuilderConfig> ModelBuilderConfigStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    return found != configs_.end() ? std::optional<ModelBuilderConfig>(found->second)
                                   : std::nullopt;
}

std::vector<ModelBuilderConfig> ModelBuilderConfigStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelBuilderConfig> result;
    result.reserve(configs_.size());
    for (const auto& item : configs_) result.push_back(item.second);
    return result;
}

bool ModelBuilderConfigStore::set_status(const std::string& id,
                                         const ModelBuilderConfigStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ModelBuilderConfigStore::configure(const std::string& id,
                                        const ModelBuilderSettings& settings) {
    validate_model_builder_settings(settings);
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    found->second.settings = settings;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ModelBuilderConfigStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    configs_.erase(found);
    if (records_) records_->erase("ml_model_builder_configs", id);
    return true;
}

std::string model_builder_config_json(const ModelBuilderConfig& config) {
    const auto& s = config.settings;
    return "{\"id\":\"" + json_escape(config.id) + "\",\"projectId\":\"" +
           json_escape(config.project_id) + "\",\"baseModelId\":\"" +
           json_escape(config.base_model_id) + "\",\"name\":\"" +
           json_escape(config.name) + "\",\"description\":\"" +
           json_escape(config.description) + "\",\"sourceType\":\"" +
           json_escape(config.source_type) + "\",\"ownerId\":\"" +
           json_escape(config.owner_id) + "\",\"status\":\"" +
           model_builder_config_status_name(config.status) +
           // The full section 9 build-settings surface, nested so list
           // consumers can ignore it and the configure form can read it as
           // one object.
           "\",\"settings\":{\"configurationMode\":\"" +
           json_escape(s.configuration_mode) + "\",\"architecture\":\"" +
           json_escape(s.architecture) + "\",\"layerConfiguration\":\"" +
           json_escape(s.layer_configuration) + "\",\"hiddenDimensions\":" +
           std::to_string(s.hidden_dimensions) +
           ",\"attentionConfiguration\":\"" +
           json_escape(s.attention_configuration) +
           "\",\"vocabularyTokenizer\":\"" +
           json_escape(s.vocabulary_tokenizer) + "\",\"sequenceLength\":" +
           std::to_string(s.sequence_length) +
           ",\"activationFunctions\":\"" +
           json_escape(s.activation_functions) + "\",\"dropout\":" +
           format_settings_double(s.dropout) +
           ",\"initialisationStrategy\":\"" +
           json_escape(s.initialisation_strategy) + "\",\"lossFunction\":\"" +
           json_escape(s.loss_function) + "\",\"optimiser\":\"" +
           json_escape(s.optimiser) + "\",\"learningRateScheduler\":\"" +
           json_escape(s.learning_rate_scheduler) + "\",\"batchSize\":" +
           std::to_string(s.batch_size) + ",\"epochCount\":" +
           std::to_string(s.epoch_count) + ",\"gradientAccumulation\":" +
           std::to_string(s.gradient_accumulation) +
           ",\"gradientClipping\":" +
           format_settings_double(s.gradient_clipping) +
           ",\"mixedPrecision\":" + (s.mixed_precision ? "true" : "false") +
           ",\"checkpointFrequency\":" +
           std::to_string(s.checkpoint_frequency) +
           ",\"validationFrequency\":" +
           std::to_string(s.validation_frequency) + ",\"earlyStopping\":" +
           (s.early_stopping ? "true" : "false") + ",\"randomSeed\":" +
           std::to_string(s.random_seed) +
           ",\"reproducibilitySettings\":\"" +
           json_escape(s.reproducibility_settings) +
           "\",\"distributedTrainingSettings\":\"" +
           json_escape(s.distributed_training_settings) +
           "\"},\"createdAtEpochSeconds\":" +
           std::to_string(config.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(config.updated_at_epoch_seconds) + "}";
}

std::string model_builder_configs_json(const std::vector<ModelBuilderConfig>& configs) {
    std::string body = "[";
    bool first = true;
    for (const auto& config : configs) {
        if (!first) body += ",";
        first = false;
        body += model_builder_config_json(config);
    }
    return body + "]";
}

std::string instruction_example_status_name(
    const InstructionExampleStatus status) {
    switch (status) {
        case InstructionExampleStatus::draft: return "draft";
        case InstructionExampleStatus::in_review: return "in_review";
        case InstructionExampleStatus::approved: return "approved";
        case InstructionExampleStatus::rejected: return "rejected";
        case InstructionExampleStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid instruction example status");
}

InstructionExampleStatus parse_instruction_example_status(
    const std::string& status) {
    if (status == "draft") return InstructionExampleStatus::draft;
    if (status == "in_review") return InstructionExampleStatus::in_review;
    if (status == "approved") return InstructionExampleStatus::approved;
    if (status == "rejected") return InstructionExampleStatus::rejected;
    if (status == "archived") return InstructionExampleStatus::archived;
    throw std::runtime_error("stored instruction example status is invalid");
}

InstructionExampleStore::InstructionExampleStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void InstructionExampleStore::restore() {
    for (const auto& item : records_->list("ml_instruction_examples")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted instruction example record field count is wrong");
        }
        InstructionExample example;
        example.id = item.first;
        example.dataset_id = fields[0];
        example.name = fields[1];
        example.description = fields[2];
        example.subject_classification = fields[3];
        example.owner_id = fields[4];
        example.status = parse_instruction_example_status(fields[5]);
        examples_[example.id] = example;
    }
}

void InstructionExampleStore::persist(const InstructionExample& example) {
    records_->put(
        "ml_instruction_examples", example.id,
        pack({example.dataset_id, example.name, example.description,
             example.subject_classification, example.owner_id,
             instruction_example_status_name(example.status)}));
}

InstructionExample InstructionExampleStore::create(
    const std::string& owner_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& subject_classification) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("instruction example name is invalid");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument(
            "instruction example dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    InstructionExample example;
    example.id = random_id();
    example.dataset_id = dataset_id;
    example.name = name;
    example.description = description;
    example.subject_classification = subject_classification;
    example.owner_id = owner_id;
    example.status = InstructionExampleStatus::draft;
    example.created_at_epoch_seconds = epoch_seconds();
    example.updated_at_epoch_seconds = example.created_at_epoch_seconds;
    examples_[example.id] = example;
    if (records_) persist(example);
    return example;
}

std::optional<InstructionExample> InstructionExampleStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = examples_.find(id);
    return found != examples_.end()
               ? std::optional<InstructionExample>(found->second)
               : std::nullopt;
}

std::vector<InstructionExample> InstructionExampleStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InstructionExample> result;
    result.reserve(examples_.size());
    for (const auto& item : examples_) result.push_back(item.second);
    return result;
}

bool InstructionExampleStore::set_status(
    const std::string& id, const InstructionExampleStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = examples_.find(id);
    if (found == examples_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool InstructionExampleStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = examples_.find(id);
    if (found == examples_.end()) return false;
    examples_.erase(found);
    if (records_) records_->erase("ml_instruction_examples", id);
    return true;
}

std::string instruction_example_json(const InstructionExample& example) {
    return "{\"id\":\"" + json_escape(example.id) + "\",\"datasetId\":\"" +
           json_escape(example.dataset_id) + "\",\"name\":\"" +
           json_escape(example.name) + "\",\"description\":\"" +
           json_escape(example.description) +
           "\",\"subjectClassification\":\"" +
           json_escape(example.subject_classification) + "\",\"ownerId\":\"" +
           json_escape(example.owner_id) + "\",\"status\":\"" +
           instruction_example_status_name(example.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(example.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(example.updated_at_epoch_seconds) + "}";
}

std::string instruction_examples_json(
    const std::vector<InstructionExample>& examples) {
    std::string body = "[";
    bool first = true;
    for (const auto& example : examples) {
        if (!first) body += ",";
        first = false;
        body += instruction_example_json(example);
    }
    return body + "]";
}

InstructionExampleContentStore::InstructionExampleContentStore(
    RecordStore& records)
    : records_(&records) {}

void InstructionExampleContentStore::put(
    const std::string& example_id, const InstructionExampleContent& content) {
    records_->put(
        "ml_instruction_example_content", example_id,
        pack({content.system_instruction, content.user_instruction,
             content.context, content.expected_response,
             content.rejected_response, content.tool_calls_json,
             content.tool_results_json, content.required_output_format,
             content.difficulty, content.safety_classification}));
}

std::optional<InstructionExampleContent> InstructionExampleContentStore::find(
    const std::string& example_id) const {
    const auto stored = records_->get("ml_instruction_example_content", example_id);
    if (!stored) return std::nullopt;
    const auto fields = unpack(*stored);
    if (fields.size() != 10U) {
        throw std::runtime_error(
            "persisted instruction example content field count is wrong");
    }
    InstructionExampleContent content;
    content.system_instruction = fields[0];
    content.user_instruction = fields[1];
    content.context = fields[2];
    content.expected_response = fields[3];
    content.rejected_response = fields[4];
    content.tool_calls_json = fields[5];
    content.tool_results_json = fields[6];
    content.required_output_format = fields[7];
    content.difficulty = fields[8];
    content.safety_classification = fields[9];
    return content;
}

bool InstructionExampleContentStore::remove(const std::string& example_id) {
    if (!records_->get("ml_instruction_example_content", example_id)) {
        return false;
    }
    records_->erase("ml_instruction_example_content", example_id);
    return true;
}

std::string instruction_example_content_json(
    const InstructionExampleContent& content) {
    return "{\"systemInstruction\":\"" +
           json_escape(content.system_instruction) +
           "\",\"userInstruction\":\"" +
           json_escape(content.user_instruction) + "\",\"context\":\"" +
           json_escape(content.context) + "\",\"expectedResponse\":\"" +
           json_escape(content.expected_response) +
           "\",\"rejectedResponse\":\"" +
           json_escape(content.rejected_response) + "\",\"toolCallsJson\":\"" +
           json_escape(content.tool_calls_json) + "\",\"toolResultsJson\":\"" +
           json_escape(content.tool_results_json) +
           "\",\"requiredOutputFormat\":\"" +
           json_escape(content.required_output_format) +
           "\",\"difficulty\":\"" + json_escape(content.difficulty) +
           "\",\"safetyClassification\":\"" +
           json_escape(content.safety_classification) + "\"}";
}

bool validate_structured_output(const InstructionExampleContent& content,
                                std::string& error_detail) {
    std::string format_lower = content.required_output_format;
    std::transform(format_lower.begin(), format_lower.end(),
                   format_lower.begin(),
                   [](const unsigned char c) { return std::tolower(c); });
    if (format_lower.find("json") == std::string::npos) {
        error_detail.clear();
        return true;
    }
    try {
        parse_json(content.expected_response);
        error_detail.clear();
        return true;
    } catch (const std::exception& error) {
        error_detail = error.what();
        return false;
    }
}

namespace {

// Lowercases and collapses runs of whitespace so two instructions that
// differ only in casing/spacing still compare as the same text.
std::string normalize_instruction_text(const std::string& text) {
    std::string result;
    result.reserve(text.size());
    bool last_was_space = false;
    for (const unsigned char character : text) {
        if (std::isspace(character)) {
            if (!last_was_space && !result.empty()) result += ' ';
            last_was_space = true;
        } else {
            result += static_cast<char>(std::tolower(character));
            last_was_space = false;
        }
    }
    while (!result.empty() && result.back() == ' ') result.pop_back();
    return result;
}

std::set<std::string> tokenize(const std::string& normalized_text) {
    std::set<std::string> tokens;
    std::istringstream stream(normalized_text);
    std::string token;
    while (stream >> token) tokens.insert(token);
    return tokens;
}

std::string instruction_text(const InstructionExampleContent& content) {
    return content.system_instruction + " " + content.user_instruction +
           " " + content.context;
}

}  // namespace

bool instruction_examples_are_near_duplicate(
    const InstructionExampleContent& a, const InstructionExampleContent& b) {
    const auto tokens_a = tokenize(normalize_instruction_text(instruction_text(a)));
    const auto tokens_b = tokenize(normalize_instruction_text(instruction_text(b)));
    if (tokens_a.empty() || tokens_b.empty()) return false;
    std::size_t intersection_size = 0U;
    for (const auto& token : tokens_a) {
        if (tokens_b.count(token) != 0U) ++intersection_size;
    }
    std::set<std::string> union_tokens = tokens_a;
    union_tokens.insert(tokens_b.begin(), tokens_b.end());
    const double jaccard = union_tokens.empty()
                               ? 0.0
                               : static_cast<double>(intersection_size) /
                                     static_cast<double>(union_tokens.size());
    return jaccard >= 0.85;
}

std::vector<std::pair<std::string, std::string>>
detect_duplicate_instruction_examples(
    const std::vector<InstructionExample>& examples,
    const std::function<std::optional<InstructionExampleContent>(
        const std::string&)>& find_content) {
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < examples.size(); ++i) {
        const auto content_i = find_content(examples[i].id);
        if (!content_i) continue;
        for (std::size_t j = i + 1U; j < examples.size(); ++j) {
            const auto content_j = find_content(examples[j].id);
            if (!content_j) continue;
            if (instruction_examples_are_near_duplicate(*content_i, *content_j)) {
                pairs.emplace_back(examples[i].id, examples[j].id);
            }
        }
    }
    return pairs;
}

std::vector<std::pair<std::string, std::string>>
detect_contradictory_instruction_examples(
    const std::vector<InstructionExample>& examples,
    const std::function<std::optional<InstructionExampleContent>(
        const std::string&)>& find_content) {
    std::vector<std::pair<std::string, std::string>> pairs;
    for (std::size_t i = 0; i < examples.size(); ++i) {
        const auto content_i = find_content(examples[i].id);
        if (!content_i) continue;
        for (std::size_t j = i + 1U; j < examples.size(); ++j) {
            const auto content_j = find_content(examples[j].id);
            if (!content_j) continue;
            const bool same_instruction =
                instruction_examples_are_near_duplicate(*content_i, *content_j);
            const bool format_disagrees =
                same_instruction &&
                content_i->required_output_format !=
                    content_j->required_output_format &&
                !content_i->required_output_format.empty() &&
                !content_j->required_output_format.empty();
            const bool direct_contradiction =
                (!content_i->expected_response.empty() &&
                 content_i->expected_response ==
                     content_j->rejected_response) ||
                (!content_j->expected_response.empty() &&
                 content_j->expected_response == content_i->rejected_response);
            if (format_disagrees || direct_contradiction) {
                pairs.emplace_back(examples[i].id, examples[j].id);
            }
        }
    }
    return pairs;
}

// Phase 48: docs/PLAN.md "Machine Learning Abilities" section 20 (Synthetic
// Data Generation) -- see SyntheticRecordStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// InstructionExampleStatus's five-state reviewer workflow.
std::string synthetic_record_status_name(const SyntheticRecordStatus status) {
    switch (status) {
        case SyntheticRecordStatus::draft: return "draft";
        case SyntheticRecordStatus::in_review: return "in_review";
        case SyntheticRecordStatus::approved: return "approved";
        case SyntheticRecordStatus::rejected: return "rejected";
        case SyntheticRecordStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid synthetic record status");
}

SyntheticRecordStatus parse_synthetic_record_status(
    const std::string& status) {
    if (status == "draft") return SyntheticRecordStatus::draft;
    if (status == "in_review") return SyntheticRecordStatus::in_review;
    if (status == "approved") return SyntheticRecordStatus::approved;
    if (status == "rejected") return SyntheticRecordStatus::rejected;
    if (status == "archived") return SyntheticRecordStatus::archived;
    throw std::runtime_error("stored synthetic record status is invalid");
}

SyntheticRecordStore::SyntheticRecordStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void SyntheticRecordStore::restore() {
    for (const auto& item : records_->list("ml_synthetic_records")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted synthetic record field count is wrong");
        }
        SyntheticRecord record;
        record.id = item.first;
        record.dataset_id = fields[0];
        record.name = fields[1];
        record.description = fields[2];
        record.generation_technique = fields[3];
        record.owner_id = fields[4];
        record.status = parse_synthetic_record_status(fields[5]);
        records_by_id_[record.id] = record;
    }
}

void SyntheticRecordStore::persist(const SyntheticRecord& record) {
    records_->put(
        "ml_synthetic_records", record.id,
        pack({record.dataset_id, record.name, record.description,
             record.generation_technique, record.owner_id,
             synthetic_record_status_name(record.status)}));
}

SyntheticRecord SyntheticRecordStore::create(
    const std::string& owner_id, const std::string& dataset_id,
    const std::string& name, const std::string& description,
    const std::string& generation_technique) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("synthetic record name is invalid");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument("synthetic record dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    SyntheticRecord record;
    record.id = random_id();
    record.dataset_id = dataset_id;
    record.name = name;
    record.description = description;
    record.generation_technique = generation_technique;
    record.owner_id = owner_id;
    record.status = SyntheticRecordStatus::draft;
    record.created_at_epoch_seconds = epoch_seconds();
    record.updated_at_epoch_seconds = record.created_at_epoch_seconds;
    records_by_id_[record.id] = record;
    if (records_) persist(record);
    return record;
}

std::optional<SyntheticRecord> SyntheticRecordStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = records_by_id_.find(id);
    return found != records_by_id_.end()
               ? std::optional<SyntheticRecord>(found->second)
               : std::nullopt;
}

std::vector<SyntheticRecord> SyntheticRecordStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SyntheticRecord> result;
    result.reserve(records_by_id_.size());
    for (const auto& item : records_by_id_) result.push_back(item.second);
    return result;
}

bool SyntheticRecordStore::set_status(
    const std::string& id, const SyntheticRecordStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = records_by_id_.find(id);
    if (found == records_by_id_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool SyntheticRecordStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = records_by_id_.find(id);
    if (found == records_by_id_.end()) return false;
    records_by_id_.erase(found);
    if (records_) records_->erase("ml_synthetic_records", id);
    return true;
}

std::string synthetic_record_json(const SyntheticRecord& record) {
    return "{\"id\":\"" + json_escape(record.id) + "\",\"datasetId\":\"" +
           json_escape(record.dataset_id) + "\",\"name\":\"" +
           json_escape(record.name) + "\",\"description\":\"" +
           json_escape(record.description) +
           "\",\"generationTechnique\":\"" +
           json_escape(record.generation_technique) + "\",\"ownerId\":\"" +
           json_escape(record.owner_id) + "\",\"status\":\"" +
           synthetic_record_status_name(record.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(record.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(record.updated_at_epoch_seconds) + "}";
}

std::string synthetic_records_json(
    const std::vector<SyntheticRecord>& records) {
    std::string body = "[";
    bool first = true;
    for (const auto& record : records) {
        if (!first) body += ",";
        first = false;
        body += synthetic_record_json(record);
    }
    return body + "]";
}

// Phase 49: docs/PLAN.md "Machine Learning Abilities" section 21
// (Embeddings and Vector Stores) -- see VectorStoreStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// DatasetApprovalStatus's three-state pending/approved/rejected workflow.
std::string vector_store_status_name(const VectorStoreStatus status) {
    switch (status) {
        case VectorStoreStatus::pending: return "pending";
        case VectorStoreStatus::approved: return "approved";
        case VectorStoreStatus::rejected: return "rejected";
    }
    throw std::runtime_error("invalid vector store status");
}

VectorStoreStatus parse_vector_store_status(const std::string& status) {
    if (status == "pending") return VectorStoreStatus::pending;
    if (status == "approved") return VectorStoreStatus::approved;
    if (status == "rejected") return VectorStoreStatus::rejected;
    throw std::runtime_error("stored vector store status is invalid");
}

VectorStoreStore::VectorStoreStore(RecordStore& records) : records_(&records) {
    restore();
}

void VectorStoreStore::restore() {
    for (const auto& item : records_->list("ml_vector_stores")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted vector store record field count is wrong");
        }
        VectorStore store;
        store.id = item.first;
        store.name = fields[0];
        store.description = fields[1];
        store.embedding_model = fields[2];
        store.distance_metric = fields[3];
        store.owner_id = fields[4];
        store.status = parse_vector_store_status(fields[5]);
        stores_[store.id] = store;
    }
}

void VectorStoreStore::persist(const VectorStore& store) {
    records_->put(
        "ml_vector_stores", store.id,
        pack({store.name, store.description, store.embedding_model,
             store.distance_metric, store.owner_id,
             vector_store_status_name(store.status)}));
}

VectorStore VectorStoreStore::create(const std::string& owner_id,
                                     const std::string& name,
                                     const std::string& description,
                                     const std::string& embedding_model,
                                     const std::string& distance_metric) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("vector store name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    VectorStore store;
    store.id = random_id();
    store.name = name;
    store.description = description;
    store.embedding_model = embedding_model;
    store.distance_metric = distance_metric;
    store.owner_id = owner_id;
    store.status = VectorStoreStatus::pending;
    store.created_at_epoch_seconds = epoch_seconds();
    store.updated_at_epoch_seconds = store.created_at_epoch_seconds;
    stores_[store.id] = store;
    if (records_) persist(store);
    return store;
}

std::optional<VectorStore> VectorStoreStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = stores_.find(id);
    return found != stores_.end() ? std::optional<VectorStore>(found->second)
                                  : std::nullopt;
}

std::vector<VectorStore> VectorStoreStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<VectorStore> result;
    result.reserve(stores_.size());
    for (const auto& item : stores_) result.push_back(item.second);
    return result;
}

bool VectorStoreStore::set_status(const std::string& id,
                                  const VectorStoreStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = stores_.find(id);
    if (found == stores_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool VectorStoreStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = stores_.find(id);
    if (found == stores_.end()) return false;
    stores_.erase(found);
    if (records_) records_->erase("ml_vector_stores", id);
    return true;
}

std::string vector_store_json(const VectorStore& store) {
    return "{\"id\":\"" + json_escape(store.id) + "\",\"name\":\"" +
           json_escape(store.name) + "\",\"description\":\"" +
           json_escape(store.description) + "\",\"embeddingModel\":\"" +
           json_escape(store.embedding_model) + "\",\"distanceMetric\":\"" +
           json_escape(store.distance_metric) + "\",\"ownerId\":\"" +
           json_escape(store.owner_id) + "\",\"status\":\"" +
           vector_store_status_name(store.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(store.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(store.updated_at_epoch_seconds) + "}";
}

std::string vector_stores_json(const std::vector<VectorStore>& stores) {
    std::string body = "[";
    bool first = true;
    for (const auto& store : stores) {
        if (!first) body += ",";
        first = false;
        body += vector_store_json(store);
    }
    return body + "]";
}

// Phase 50: docs/PLAN.md "Machine Learning Abilities" section 22
// (Retrieval-Augmented Generation) -- see RagConfigStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// the pending/approved/rejected approval workflow.
std::string rag_config_status_name(const RagConfigStatus status) {
    switch (status) {
        case RagConfigStatus::pending: return "pending";
        case RagConfigStatus::approved: return "approved";
        case RagConfigStatus::rejected: return "rejected";
    }
    throw std::runtime_error("invalid rag config status");
}

RagConfigStatus parse_rag_config_status(const std::string& status) {
    if (status == "pending") return RagConfigStatus::pending;
    if (status == "approved") return RagConfigStatus::approved;
    if (status == "rejected") return RagConfigStatus::rejected;
    throw std::runtime_error("stored rag config status is invalid");
}

RagConfigStore::RagConfigStore(RecordStore& records) : records_(&records) {
    restore();
}

void RagConfigStore::restore() {
    for (const auto& item : records_->list("ml_rag_configs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted rag config record field count is wrong");
        }
        RagConfig config;
        config.id = item.first;
        config.name = fields[0];
        config.description = fields[1];
        config.search_strategy = fields[2];
        config.vector_store_id = fields[3];
        config.owner_id = fields[4];
        config.status = parse_rag_config_status(fields[5]);
        configs_[config.id] = config;
    }
}

void RagConfigStore::persist(const RagConfig& config) {
    records_->put(
        "ml_rag_configs", config.id,
        pack({config.name, config.description, config.search_strategy,
             config.vector_store_id, config.owner_id,
             rag_config_status_name(config.status)}));
}

RagConfig RagConfigStore::create(const std::string& owner_id,
                                 const std::string& name,
                                 const std::string& description,
                                 const std::string& search_strategy,
                                 const std::string& vector_store_id) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("rag config name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    RagConfig config;
    config.id = random_id();
    config.name = name;
    config.description = description;
    config.search_strategy = search_strategy;
    config.vector_store_id = vector_store_id;
    config.owner_id = owner_id;
    config.status = RagConfigStatus::pending;
    config.created_at_epoch_seconds = epoch_seconds();
    config.updated_at_epoch_seconds = config.created_at_epoch_seconds;
    configs_[config.id] = config;
    if (records_) persist(config);
    return config;
}

std::optional<RagConfig> RagConfigStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    return found != configs_.end() ? std::optional<RagConfig>(found->second)
                                   : std::nullopt;
}

std::vector<RagConfig> RagConfigStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<RagConfig> result;
    result.reserve(configs_.size());
    for (const auto& item : configs_) result.push_back(item.second);
    return result;
}

bool RagConfigStore::set_status(const std::string& id,
                                const RagConfigStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool RagConfigStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    configs_.erase(found);
    if (records_) records_->erase("ml_rag_configs", id);
    return true;
}

std::string rag_config_json(const RagConfig& config) {
    return "{\"id\":\"" + json_escape(config.id) + "\",\"name\":\"" +
           json_escape(config.name) + "\",\"description\":\"" +
           json_escape(config.description) + "\",\"searchStrategy\":\"" +
           json_escape(config.search_strategy) + "\",\"vectorStoreId\":\"" +
           json_escape(config.vector_store_id) + "\",\"ownerId\":\"" +
           json_escape(config.owner_id) + "\",\"status\":\"" +
           rag_config_status_name(config.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(config.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(config.updated_at_epoch_seconds) + "}";
}

std::string rag_configs_json(const std::vector<RagConfig>& configs) {
    std::string body = "[";
    bool first = true;
    for (const auto& config : configs) {
        if (!first) body += ",";
        first = false;
        body += rag_config_json(config);
    }
    return body + "]";
}

// Phase 51: docs/PLAN.md "Machine Learning Abilities" section 24 (Subject
// Examination System) -- see SubjectExamStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// the five-state reviewer-approval workflow.
std::string subject_exam_status_name(const SubjectExamStatus status) {
    switch (status) {
        case SubjectExamStatus::draft: return "draft";
        case SubjectExamStatus::in_review: return "in_review";
        case SubjectExamStatus::approved: return "approved";
        case SubjectExamStatus::rejected: return "rejected";
        case SubjectExamStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid subject exam status");
}

SubjectExamStatus parse_subject_exam_status(const std::string& status) {
    if (status == "draft") return SubjectExamStatus::draft;
    if (status == "in_review") return SubjectExamStatus::in_review;
    if (status == "approved") return SubjectExamStatus::approved;
    if (status == "rejected") return SubjectExamStatus::rejected;
    if (status == "archived") return SubjectExamStatus::archived;
    throw std::runtime_error("stored subject exam status is invalid");
}

SubjectExamStore::SubjectExamStore(RecordStore& records) : records_(&records) {
    restore();
}

void SubjectExamStore::restore() {
    for (const auto& item : records_->list("ml_subject_exams")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted subject exam record field count is wrong");
        }
        SubjectExam exam;
        exam.id = item.first;
        exam.subject_id = fields[0];
        exam.name = fields[1];
        exam.description = fields[2];
        exam.question_format = fields[3];
        exam.owner_id = fields[4];
        exam.status = parse_subject_exam_status(fields[5]);
        exams_[exam.id] = exam;
    }
}

void SubjectExamStore::persist(const SubjectExam& exam) {
    records_->put(
        "ml_subject_exams", exam.id,
        pack({exam.subject_id, exam.name, exam.description,
             exam.question_format, exam.owner_id,
             subject_exam_status_name(exam.status)}));
}

SubjectExam SubjectExamStore::create(const std::string& owner_id,
                                     const std::string& subject_id,
                                     const std::string& name,
                                     const std::string& description,
                                     const std::string& question_format) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("subject exam name is invalid");
    }
    if (subject_id.empty()) {
        throw std::invalid_argument("subject exam subject id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    SubjectExam exam;
    exam.id = random_id();
    exam.subject_id = subject_id;
    exam.name = name;
    exam.description = description;
    exam.question_format = question_format;
    exam.owner_id = owner_id;
    exam.status = SubjectExamStatus::draft;
    exam.created_at_epoch_seconds = epoch_seconds();
    exam.updated_at_epoch_seconds = exam.created_at_epoch_seconds;
    exams_[exam.id] = exam;
    if (records_) persist(exam);
    return exam;
}

std::optional<SubjectExam> SubjectExamStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = exams_.find(id);
    return found != exams_.end() ? std::optional<SubjectExam>(found->second)
                                 : std::nullopt;
}

std::vector<SubjectExam> SubjectExamStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SubjectExam> result;
    result.reserve(exams_.size());
    for (const auto& item : exams_) result.push_back(item.second);
    return result;
}

bool SubjectExamStore::set_status(const std::string& id,
                                  const SubjectExamStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = exams_.find(id);
    if (found == exams_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool SubjectExamStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = exams_.find(id);
    if (found == exams_.end()) return false;
    exams_.erase(found);
    if (records_) records_->erase("ml_subject_exams", id);
    return true;
}

std::string subject_exam_json(const SubjectExam& exam) {
    return "{\"id\":\"" + json_escape(exam.id) + "\",\"subjectId\":\"" +
           json_escape(exam.subject_id) + "\",\"name\":\"" +
           json_escape(exam.name) + "\",\"description\":\"" +
           json_escape(exam.description) + "\",\"questionFormat\":\"" +
           json_escape(exam.question_format) + "\",\"ownerId\":\"" +
           json_escape(exam.owner_id) + "\",\"status\":\"" +
           subject_exam_status_name(exam.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(exam.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(exam.updated_at_epoch_seconds) + "}";
}

std::string subject_exams_json(const std::vector<SubjectExam>& exams) {
    std::string body = "[";
    bool first = true;
    for (const auto& exam : exams) {
        if (!first) body += ",";
        first = false;
        body += subject_exam_json(exam);
    }
    return body + "]";
}

// Phase 52: docs/PLAN.md "Machine Learning Abilities" section 26
// (Hyperparameter Optimization) -- see HyperparameterSearchStore's class
// comment in masterai.hpp for the scoped-down field set and the rationale
// for reusing the eleven-state job lifecycle.
std::string hyperparameter_search_status_name(
    const HyperparameterSearchStatus status) {
    switch (status) {
        case HyperparameterSearchStatus::draft: return "draft";
        case HyperparameterSearchStatus::queued: return "queued";
        case HyperparameterSearchStatus::preparing: return "preparing";
        case HyperparameterSearchStatus::running: return "running";
        case HyperparameterSearchStatus::paused: return "paused";
        case HyperparameterSearchStatus::canceling: return "canceling";
        case HyperparameterSearchStatus::canceled: return "canceled";
        case HyperparameterSearchStatus::failed: return "failed";
        case HyperparameterSearchStatus::completed: return "completed";
        case HyperparameterSearchStatus::awaiting_evaluation:
            return "awaiting_evaluation";
        case HyperparameterSearchStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid hyperparameter search status");
}

HyperparameterSearchStatus parse_hyperparameter_search_status(
    const std::string& status) {
    if (status == "draft") return HyperparameterSearchStatus::draft;
    if (status == "queued") return HyperparameterSearchStatus::queued;
    if (status == "preparing") return HyperparameterSearchStatus::preparing;
    if (status == "running") return HyperparameterSearchStatus::running;
    if (status == "paused") return HyperparameterSearchStatus::paused;
    if (status == "canceling") return HyperparameterSearchStatus::canceling;
    if (status == "canceled") return HyperparameterSearchStatus::canceled;
    if (status == "failed") return HyperparameterSearchStatus::failed;
    if (status == "completed") return HyperparameterSearchStatus::completed;
    if (status == "awaiting_evaluation") {
        return HyperparameterSearchStatus::awaiting_evaluation;
    }
    if (status == "archived") return HyperparameterSearchStatus::archived;
    throw std::runtime_error("stored hyperparameter search status is invalid");
}

HyperparameterSearchStore::HyperparameterSearchStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void HyperparameterSearchStore::restore() {
    for (const auto& item : records_->list("ml_hyperparameter_searches")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted hyperparameter search record field count is wrong");
        }
        HyperparameterSearch search;
        search.id = item.first;
        search.training_job_id = fields[0];
        search.name = fields[1];
        search.description = fields[2];
        search.strategy = fields[3];
        search.owner_id = fields[4];
        search.status = parse_hyperparameter_search_status(fields[5]);
        searches_[search.id] = search;
    }
}

void HyperparameterSearchStore::persist(const HyperparameterSearch& search) {
    records_->put(
        "ml_hyperparameter_searches", search.id,
        pack({search.training_job_id, search.name, search.description,
             search.strategy, search.owner_id,
             hyperparameter_search_status_name(search.status)}));
}

HyperparameterSearch HyperparameterSearchStore::create(
    const std::string& owner_id, const std::string& training_job_id,
    const std::string& name, const std::string& description,
    const std::string& strategy) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("hyperparameter search name is invalid");
    }
    if (training_job_id.empty()) {
        throw std::invalid_argument(
            "hyperparameter search training job id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    HyperparameterSearch search;
    search.id = random_id();
    search.training_job_id = training_job_id;
    search.name = name;
    search.description = description;
    search.strategy = strategy;
    search.owner_id = owner_id;
    search.status = HyperparameterSearchStatus::draft;
    search.created_at_epoch_seconds = epoch_seconds();
    search.updated_at_epoch_seconds = search.created_at_epoch_seconds;
    searches_[search.id] = search;
    if (records_) persist(search);
    return search;
}

std::optional<HyperparameterSearch> HyperparameterSearchStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = searches_.find(id);
    return found != searches_.end()
               ? std::optional<HyperparameterSearch>(found->second)
               : std::nullopt;
}

std::vector<HyperparameterSearch> HyperparameterSearchStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<HyperparameterSearch> result;
    result.reserve(searches_.size());
    for (const auto& item : searches_) result.push_back(item.second);
    return result;
}

bool HyperparameterSearchStore::set_status(
    const std::string& id, const HyperparameterSearchStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = searches_.find(id);
    if (found == searches_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool HyperparameterSearchStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = searches_.find(id);
    if (found == searches_.end()) return false;
    searches_.erase(found);
    if (records_) records_->erase("ml_hyperparameter_searches", id);
    return true;
}

std::string hyperparameter_search_json(const HyperparameterSearch& search) {
    return "{\"id\":\"" + json_escape(search.id) + "\",\"trainingJobId\":\"" +
           json_escape(search.training_job_id) + "\",\"name\":\"" +
           json_escape(search.name) + "\",\"description\":\"" +
           json_escape(search.description) + "\",\"strategy\":\"" +
           json_escape(search.strategy) + "\",\"ownerId\":\"" +
           json_escape(search.owner_id) + "\",\"status\":\"" +
           hyperparameter_search_status_name(search.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(search.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(search.updated_at_epoch_seconds) + "}";
}

std::string hyperparameter_searches_json(
    const std::vector<HyperparameterSearch>& searches) {
    std::string body = "[";
    bool first = true;
    for (const auto& search : searches) {
        if (!first) body += ",";
        first = false;
        body += hyperparameter_search_json(search);
    }
    return body + "]";
}

// Phase 53: docs/PLAN.md "Machine Learning Abilities" section 28 (Model
// Optimization) -- see ModelOptimizationStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// the eleven-state job lifecycle.
std::string model_optimization_status_name(
    const ModelOptimizationStatus status) {
    switch (status) {
        case ModelOptimizationStatus::draft: return "draft";
        case ModelOptimizationStatus::queued: return "queued";
        case ModelOptimizationStatus::preparing: return "preparing";
        case ModelOptimizationStatus::running: return "running";
        case ModelOptimizationStatus::paused: return "paused";
        case ModelOptimizationStatus::canceling: return "canceling";
        case ModelOptimizationStatus::canceled: return "canceled";
        case ModelOptimizationStatus::failed: return "failed";
        case ModelOptimizationStatus::completed: return "completed";
        case ModelOptimizationStatus::awaiting_evaluation:
            return "awaiting_evaluation";
        case ModelOptimizationStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid model optimization status");
}

ModelOptimizationStatus parse_model_optimization_status(
    const std::string& status) {
    if (status == "draft") return ModelOptimizationStatus::draft;
    if (status == "queued") return ModelOptimizationStatus::queued;
    if (status == "preparing") return ModelOptimizationStatus::preparing;
    if (status == "running") return ModelOptimizationStatus::running;
    if (status == "paused") return ModelOptimizationStatus::paused;
    if (status == "canceling") return ModelOptimizationStatus::canceling;
    if (status == "canceled") return ModelOptimizationStatus::canceled;
    if (status == "failed") return ModelOptimizationStatus::failed;
    if (status == "completed") return ModelOptimizationStatus::completed;
    if (status == "awaiting_evaluation") {
        return ModelOptimizationStatus::awaiting_evaluation;
    }
    if (status == "archived") return ModelOptimizationStatus::archived;
    throw std::runtime_error("stored model optimization status is invalid");
}

ModelOptimizationStore::ModelOptimizationStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void ModelOptimizationStore::restore() {
    for (const auto& item : records_->list("ml_model_optimizations")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U) {
            throw std::runtime_error(
                "persisted model optimization record field count is wrong");
        }
        ModelOptimizationRun run;
        run.id = item.first;
        run.model_id = fields[0];
        run.name = fields[1];
        run.description = fields[2];
        run.operation = fields[3];
        run.owner_id = fields[4];
        run.status = parse_model_optimization_status(fields[5]);
        runs_[run.id] = run;
    }
}

void ModelOptimizationStore::persist(const ModelOptimizationRun& run) {
    records_->put(
        "ml_model_optimizations", run.id,
        pack({run.model_id, run.name, run.description, run.operation,
             run.owner_id, model_optimization_status_name(run.status)}));
}

ModelOptimizationRun ModelOptimizationStore::create(
    const std::string& owner_id, const std::string& model_id,
    const std::string& name, const std::string& description,
    const std::string& operation) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("model optimization name is invalid");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("model optimization model id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ModelOptimizationRun run;
    run.id = random_id();
    run.model_id = model_id;
    run.name = name;
    run.description = description;
    run.operation = operation;
    run.owner_id = owner_id;
    run.status = ModelOptimizationStatus::draft;
    run.created_at_epoch_seconds = epoch_seconds();
    run.updated_at_epoch_seconds = run.created_at_epoch_seconds;
    runs_[run.id] = run;
    if (records_) persist(run);
    return run;
}

std::optional<ModelOptimizationRun> ModelOptimizationStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    return found != runs_.end()
               ? std::optional<ModelOptimizationRun>(found->second)
               : std::nullopt;
}

std::vector<ModelOptimizationRun> ModelOptimizationStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelOptimizationRun> result;
    result.reserve(runs_.size());
    for (const auto& item : runs_) result.push_back(item.second);
    return result;
}

bool ModelOptimizationStore::set_status(const std::string& id,
                                        const ModelOptimizationStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    if (found == runs_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ModelOptimizationStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    if (found == runs_.end()) return false;
    runs_.erase(found);
    if (records_) records_->erase("ml_model_optimizations", id);
    return true;
}

std::string model_optimization_json(const ModelOptimizationRun& run) {
    return "{\"id\":\"" + json_escape(run.id) + "\",\"modelId\":\"" +
           json_escape(run.model_id) + "\",\"name\":\"" +
           json_escape(run.name) + "\",\"description\":\"" +
           json_escape(run.description) + "\",\"operation\":\"" +
           json_escape(run.operation) + "\",\"ownerId\":\"" +
           json_escape(run.owner_id) + "\",\"status\":\"" +
           model_optimization_status_name(run.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(run.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(run.updated_at_epoch_seconds) + "}";
}

std::string model_optimizations_json(
    const std::vector<ModelOptimizationRun>& runs) {
    std::string body = "[";
    bool first = true;
    for (const auto& run : runs) {
        if (!first) body += ",";
        first = false;
        body += model_optimization_json(run);
    }
    return body + "]";
}

// Phase 54: docs/PLAN.md "Machine Learning Abilities" section 33
// (Checkpoint Management) -- see TrainingCheckpointStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for the
// bespoke active/pinned/archived retention lifecycle.
std::string training_checkpoint_status_name(
    const TrainingCheckpointStatus status) {
    switch (status) {
        case TrainingCheckpointStatus::active: return "active";
        case TrainingCheckpointStatus::pinned: return "pinned";
        case TrainingCheckpointStatus::archived: return "archived";
    }
    throw std::runtime_error("invalid training checkpoint status");
}

TrainingCheckpointStatus parse_training_checkpoint_status(
    const std::string& status) {
    if (status == "active") return TrainingCheckpointStatus::active;
    if (status == "pinned") return TrainingCheckpointStatus::pinned;
    if (status == "archived") return TrainingCheckpointStatus::archived;
    throw std::runtime_error("stored training checkpoint status is invalid");
}

TrainingCheckpointStore::TrainingCheckpointStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void TrainingCheckpointStore::restore() {
    for (const auto& item : records_->list("ml_training_checkpoints")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error(
                "persisted training checkpoint record field count is wrong");
        }
        TrainingCheckpoint checkpoint;
        checkpoint.id = item.first;
        checkpoint.training_job_id = fields[0];
        checkpoint.name = fields[1];
        checkpoint.description = fields[2];
        checkpoint.capture_reason = fields[3];
        checkpoint.owner_id = fields[4];
        checkpoint.status = parse_training_checkpoint_status(fields[5]);
        checkpoint.epoch = static_cast<std::uint32_t>(std::stoul(fields[6]));
        checkpoint.has_snapshot = fields[7] == "1";
        checkpoints_[checkpoint.id] = checkpoint;
    }
}

void TrainingCheckpointStore::persist(const TrainingCheckpoint& checkpoint) {
    records_->put(
        "ml_training_checkpoints", checkpoint.id,
        pack({checkpoint.training_job_id, checkpoint.name,
             checkpoint.description, checkpoint.capture_reason,
             checkpoint.owner_id,
             training_checkpoint_status_name(checkpoint.status),
             std::to_string(checkpoint.epoch),
             checkpoint.has_snapshot ? "1" : "0"}));
}

TrainingCheckpoint TrainingCheckpointStore::create(
    const std::string& owner_id, const std::string& training_job_id,
    const std::string& name, const std::string& description,
    const std::string& capture_reason, const std::uint32_t epoch,
    const bool has_snapshot) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("training checkpoint name is invalid");
    }
    if (training_job_id.empty()) {
        throw std::invalid_argument(
            "training checkpoint training job id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    TrainingCheckpoint checkpoint;
    checkpoint.id = random_id();
    checkpoint.training_job_id = training_job_id;
    checkpoint.name = name;
    checkpoint.description = description;
    checkpoint.capture_reason = capture_reason;
    checkpoint.owner_id = owner_id;
    checkpoint.status = TrainingCheckpointStatus::active;
    checkpoint.epoch = epoch;
    checkpoint.has_snapshot = has_snapshot;
    checkpoint.created_at_epoch_seconds = epoch_seconds();
    checkpoint.updated_at_epoch_seconds = checkpoint.created_at_epoch_seconds;
    checkpoints_[checkpoint.id] = checkpoint;
    if (records_) persist(checkpoint);
    return checkpoint;
}

std::optional<TrainingCheckpoint> TrainingCheckpointStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = checkpoints_.find(id);
    return found != checkpoints_.end()
               ? std::optional<TrainingCheckpoint>(found->second)
               : std::nullopt;
}

std::vector<TrainingCheckpoint> TrainingCheckpointStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<TrainingCheckpoint> result;
    result.reserve(checkpoints_.size());
    for (const auto& item : checkpoints_) result.push_back(item.second);
    return result;
}

bool TrainingCheckpointStore::set_status(
    const std::string& id, const TrainingCheckpointStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = checkpoints_.find(id);
    if (found == checkpoints_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool TrainingCheckpointStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = checkpoints_.find(id);
    if (found == checkpoints_.end()) return false;
    checkpoints_.erase(found);
    if (records_) records_->erase("ml_training_checkpoints", id);
    return true;
}

std::string training_checkpoint_json(const TrainingCheckpoint& checkpoint) {
    return "{\"id\":\"" + json_escape(checkpoint.id) +
           "\",\"trainingJobId\":\"" +
           json_escape(checkpoint.training_job_id) + "\",\"name\":\"" +
           json_escape(checkpoint.name) + "\",\"description\":\"" +
           json_escape(checkpoint.description) + "\",\"captureReason\":\"" +
           json_escape(checkpoint.capture_reason) + "\",\"ownerId\":\"" +
           json_escape(checkpoint.owner_id) + "\",\"status\":\"" +
           training_checkpoint_status_name(checkpoint.status) +
           "\",\"epoch\":" + std::to_string(checkpoint.epoch) +
           ",\"hasSnapshot\":" +
           (checkpoint.has_snapshot ? "true" : "false") +
           ",\"createdAtEpochSeconds\":" +
           std::to_string(checkpoint.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(checkpoint.updated_at_epoch_seconds) + "}";
}

std::string training_checkpoints_json(
    const std::vector<TrainingCheckpoint>& checkpoints) {
    std::string body = "[";
    bool first = true;
    for (const auto& checkpoint : checkpoints) {
        if (!first) body += ",";
        first = false;
        body += training_checkpoint_json(checkpoint);
    }
    return body + "]";
}

// Phase 55: docs/PLAN.md "Machine Learning Abilities" section 34
// (Deployment Manager) -- see DeploymentStore's class comment in
// masterai.hpp for the scoped-down field set and the rationale for reusing
// the pending/approved/rejected approval workflow.
std::string deployment_status_name(const DeploymentStatus status) {
    switch (status) {
        case DeploymentStatus::pending: return "pending";
        case DeploymentStatus::approved: return "approved";
        case DeploymentStatus::rejected: return "rejected";
    }
    throw std::runtime_error("invalid deployment status");
}

DeploymentStatus parse_deployment_status(const std::string& status) {
    if (status == "pending") return DeploymentStatus::pending;
    if (status == "approved") return DeploymentStatus::approved;
    if (status == "rejected") return DeploymentStatus::rejected;
    throw std::runtime_error("stored deployment status is invalid");
}

DeploymentStore::DeploymentStore(RecordStore& records) : records_(&records) {
    restore();
}

void DeploymentStore::restore() {
    for (const auto& item : records_->list("ml_deployments")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 7U) {
            throw std::runtime_error(
                "persisted deployment record field count is wrong");
        }
        Deployment deployment;
        deployment.id = item.first;
        deployment.model_id = fields[0];
        deployment.name = fields[1];
        deployment.description = fields[2];
        deployment.environment = fields[3];
        deployment.strategy = fields[4];
        deployment.owner_id = fields[5];
        deployment.status = parse_deployment_status(fields[6]);
        deployments_[deployment.id] = deployment;
    }
}

void DeploymentStore::persist(const Deployment& deployment) {
    records_->put(
        "ml_deployments", deployment.id,
        pack({deployment.model_id, deployment.name, deployment.description,
             deployment.environment, deployment.strategy,
             deployment.owner_id,
             deployment_status_name(deployment.status)}));
}

Deployment DeploymentStore::create(const std::string& owner_id,
                                   const std::string& model_id,
                                   const std::string& name,
                                   const std::string& description,
                                   const std::string& environment,
                                   const std::string& strategy) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("deployment name is invalid");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("deployment model id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    Deployment deployment;
    deployment.id = random_id();
    deployment.model_id = model_id;
    deployment.name = name;
    deployment.description = description;
    deployment.environment = environment;
    deployment.strategy = strategy;
    deployment.owner_id = owner_id;
    deployment.status = DeploymentStatus::pending;
    deployment.created_at_epoch_seconds = epoch_seconds();
    deployment.updated_at_epoch_seconds = deployment.created_at_epoch_seconds;
    deployments_[deployment.id] = deployment;
    if (records_) persist(deployment);
    return deployment;
}

std::optional<Deployment> DeploymentStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = deployments_.find(id);
    return found != deployments_.end()
               ? std::optional<Deployment>(found->second)
               : std::nullopt;
}

std::vector<Deployment> DeploymentStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Deployment> result;
    result.reserve(deployments_.size());
    for (const auto& item : deployments_) result.push_back(item.second);
    return result;
}

bool DeploymentStore::set_status(const std::string& id,
                                 const DeploymentStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = deployments_.find(id);
    if (found == deployments_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool DeploymentStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = deployments_.find(id);
    if (found == deployments_.end()) return false;
    deployments_.erase(found);
    if (records_) records_->erase("ml_deployments", id);
    return true;
}

std::string deployment_json(const Deployment& deployment) {
    return "{\"id\":\"" + json_escape(deployment.id) + "\",\"modelId\":\"" +
           json_escape(deployment.model_id) + "\",\"name\":\"" +
           json_escape(deployment.name) + "\",\"description\":\"" +
           json_escape(deployment.description) + "\",\"environment\":\"" +
           json_escape(deployment.environment) + "\",\"strategy\":\"" +
           json_escape(deployment.strategy) + "\",\"ownerId\":\"" +
           json_escape(deployment.owner_id) + "\",\"status\":\"" +
           deployment_status_name(deployment.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(deployment.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(deployment.updated_at_epoch_seconds) + "}";
}

std::string deployments_json(const std::vector<Deployment>& deployments) {
    std::string body = "[";
    bool first = true;
    for (const auto& deployment : deployments) {
        if (!first) body += ",";
        first = false;
        body += deployment_json(deployment);
    }
    return body + "]";
}

// Phase 57: docs/PLAN.md "Machine Learning Abilities" section 27 (Model
// Comparison) -- see ModelComparisonStore's class comment in masterai.hpp.
// The record store lives here with its scoped-down siblings; the real
// comparison executor (tabular_model_comparison_json and
// ComparisonResultStore) lives in ml_engine.cpp with the rest of the
// computing layer.
std::string model_comparison_status_name(const ModelComparisonStatus status) {
    switch (status) {
        case ModelComparisonStatus::queued: return "queued";
        case ModelComparisonStatus::running: return "running";
        case ModelComparisonStatus::completed: return "completed";
        case ModelComparisonStatus::failed: return "failed";
        case ModelComparisonStatus::canceled: return "canceled";
    }
    throw std::runtime_error("invalid model comparison status");
}

ModelComparisonStatus parse_model_comparison_status(const std::string& status) {
    if (status == "queued") return ModelComparisonStatus::queued;
    if (status == "running") return ModelComparisonStatus::running;
    if (status == "completed") return ModelComparisonStatus::completed;
    if (status == "failed") return ModelComparisonStatus::failed;
    if (status == "canceled") return ModelComparisonStatus::canceled;
    throw std::runtime_error("stored model comparison status is invalid");
}

ModelComparisonStore::ModelComparisonStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void ModelComparisonStore::restore() {
    for (const auto& item : records_->list("ml_model_comparisons")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 7U) {
            throw std::runtime_error(
                "persisted model comparison record field count is wrong");
        }
        ModelComparison comparison;
        comparison.id = item.first;
        comparison.baseline_model_id = fields[0];
        comparison.candidate_model_id = fields[1];
        comparison.dataset_id = fields[2];
        comparison.name = fields[3];
        comparison.description = fields[4];
        comparison.owner_id = fields[5];
        comparison.status = parse_model_comparison_status(fields[6]);
        comparisons_[comparison.id] = comparison;
    }
}

void ModelComparisonStore::persist(const ModelComparison& comparison) {
    records_->put(
        "ml_model_comparisons", comparison.id,
        pack({comparison.baseline_model_id, comparison.candidate_model_id,
              comparison.dataset_id, comparison.name, comparison.description,
              comparison.owner_id,
              model_comparison_status_name(comparison.status)}));
}

ModelComparison ModelComparisonStore::create(
    const std::string& owner_id, const std::string& baseline_model_id,
    const std::string& candidate_model_id, const std::string& dataset_id,
    const std::string& name, const std::string& description) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("model comparison name is invalid");
    }
    if (baseline_model_id.empty()) {
        throw std::invalid_argument(
            "model comparison baseline model id is required");
    }
    if (candidate_model_id.empty()) {
        throw std::invalid_argument(
            "model comparison candidate model id is required");
    }
    // Comparing a model against itself always produces a tie and would only
    // clutter the run history, so it is rejected as a record-shape error.
    if (baseline_model_id == candidate_model_id) {
        throw std::invalid_argument(
            "model comparison baseline and candidate must be different models");
    }
    if (dataset_id.empty()) {
        throw std::invalid_argument(
            "model comparison benchmark dataset id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ModelComparison comparison;
    comparison.id = random_id();
    comparison.baseline_model_id = baseline_model_id;
    comparison.candidate_model_id = candidate_model_id;
    comparison.dataset_id = dataset_id;
    comparison.name = name;
    comparison.description = description;
    comparison.owner_id = owner_id;
    comparison.status = ModelComparisonStatus::queued;
    comparison.created_at_epoch_seconds = epoch_seconds();
    comparison.updated_at_epoch_seconds = comparison.created_at_epoch_seconds;
    comparisons_[comparison.id] = comparison;
    if (records_) persist(comparison);
    return comparison;
}

std::optional<ModelComparison> ModelComparisonStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = comparisons_.find(id);
    return found != comparisons_.end()
               ? std::optional<ModelComparison>(found->second)
               : std::nullopt;
}

std::vector<ModelComparison> ModelComparisonStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelComparison> result;
    result.reserve(comparisons_.size());
    for (const auto& item : comparisons_) result.push_back(item.second);
    return result;
}

bool ModelComparisonStore::set_status(const std::string& id,
                                      const ModelComparisonStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = comparisons_.find(id);
    if (found == comparisons_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ModelComparisonStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = comparisons_.find(id);
    if (found == comparisons_.end()) return false;
    comparisons_.erase(found);
    if (records_) records_->erase("ml_model_comparisons", id);
    return true;
}

std::string model_comparison_json(const ModelComparison& comparison) {
    return "{\"id\":\"" + json_escape(comparison.id) +
           "\",\"baselineModelId\":\"" +
           json_escape(comparison.baseline_model_id) +
           "\",\"candidateModelId\":\"" +
           json_escape(comparison.candidate_model_id) + "\",\"datasetId\":\"" +
           json_escape(comparison.dataset_id) + "\",\"name\":\"" +
           json_escape(comparison.name) + "\",\"description\":\"" +
           json_escape(comparison.description) + "\",\"ownerId\":\"" +
           json_escape(comparison.owner_id) + "\",\"status\":\"" +
           model_comparison_status_name(comparison.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(comparison.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(comparison.updated_at_epoch_seconds) + "}";
}

std::string model_comparisons_json(
    const std::vector<ModelComparison>& comparisons) {
    std::string body = "[";
    bool first = true;
    for (const auto& comparison : comparisons) {
        if (!first) body += ",";
        first = false;
        body += model_comparison_json(comparison);
    }
    return body + "]";
}

// Phase 62: docs/PLAN.md "Machine Learning Abilities" section 35 (Inference
// Endpoints) -- see InferenceEndpoint's class comment in masterai.hpp for
// the fields this scoped-down registry defers.
std::string inference_endpoint_status_name(const InferenceEndpointStatus status) {
    switch (status) {
        case InferenceEndpointStatus::draft: return "draft";
        case InferenceEndpointStatus::active: return "active";
        case InferenceEndpointStatus::disabled: return "disabled";
    }
    throw std::runtime_error("invalid inference endpoint status");
}

InferenceEndpointStatus parse_inference_endpoint_status(const std::string& status) {
    if (status == "draft") return InferenceEndpointStatus::draft;
    if (status == "active") return InferenceEndpointStatus::active;
    if (status == "disabled") return InferenceEndpointStatus::disabled;
    throw std::runtime_error("stored inference endpoint status is invalid");
}

InferenceEndpointStore::InferenceEndpointStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void InferenceEndpointStore::restore() {
    for (const auto& item : records_->list("ml_inference_endpoints")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 8U) {
            throw std::runtime_error(
                "persisted inference endpoint record field count is wrong");
        }
        InferenceEndpoint endpoint;
        endpoint.id = item.first;
        endpoint.name = fields[0];
        endpoint.model_id = fields[1];
        endpoint.runtime = fields[2];
        endpoint.host = fields[3];
        endpoint.port = static_cast<std::uint16_t>(std::stoul(fields[4]));
        endpoint.protocol = fields[5];
        endpoint.authentication_method = fields[6];
        const auto rate_and_owner = unpack(fields[7]);
        // Phase 77 added auth_token_hash as a 4th nested field, then (this
        // pass) a 5th nested field packing the per-endpoint policy fields;
        // records written before either phase still have 3 or 4 and default
        // the newer fields (no enforced auth / the pre-policy always-on
        // heuristic-scan behavior), matching their prior "records intent
        // only" behavior.
        if (rate_and_owner.size() < 3U || rate_and_owner.size() > 5U) {
            throw std::runtime_error(
                "persisted inference endpoint tail field count is wrong");
        }
        endpoint.rate_limit_per_minute =
            static_cast<std::uint32_t>(std::stoul(rate_and_owner[0]));
        endpoint.owner_id = rate_and_owner[1];
        endpoint.status = parse_inference_endpoint_status(rate_and_owner[2]);
        if (rate_and_owner.size() >= 4U) endpoint.auth_token_hash = rate_and_owner[3];
        if (rate_and_owner.size() == 5U) {
            const auto policy = unpack(rate_and_owner[4]);
            // block_answer_on_scan_finding was added as a 6th nested policy
            // field after this policy block already shipped; 5-field
            // records default it to false (the original always-on-listener
            // "never withhold a flagged answer" behavior).
            if (policy.size() != 5U && policy.size() != 6U) {
                throw std::runtime_error(
                    "persisted inference endpoint policy field count is wrong");
            }
            endpoint.content_scan_enabled = policy[0] == "1";
            endpoint.block_on_scan_finding = policy[1] == "1";
            endpoint.safety_policy_id = policy[2];
            endpoint.model_classifier_enabled = policy[3] == "1";
            endpoint.model_classifier_confidence_floor = std::stod(policy[4]);
            if (policy.size() == 6U) {
                endpoint.block_answer_on_scan_finding = policy[5] == "1";
            }
        }
        endpoints_[endpoint.id] = endpoint;
    }
}

void InferenceEndpointStore::persist(const InferenceEndpoint& endpoint) {
    records_->put(
        "ml_inference_endpoints", endpoint.id,
        pack({endpoint.name, endpoint.model_id, endpoint.runtime,
             endpoint.host, std::to_string(endpoint.port), endpoint.protocol,
             endpoint.authentication_method,
             pack({std::to_string(endpoint.rate_limit_per_minute),
                  endpoint.owner_id,
                  inference_endpoint_status_name(endpoint.status),
                  endpoint.auth_token_hash,
                  pack({endpoint.content_scan_enabled ? "1" : "0",
                       endpoint.block_on_scan_finding ? "1" : "0",
                       endpoint.safety_policy_id,
                       endpoint.model_classifier_enabled ? "1" : "0",
                       std::to_string(endpoint.model_classifier_confidence_floor),
                       endpoint.block_answer_on_scan_finding ? "1" : "0"})})}));
}

InferenceEndpoint InferenceEndpointStore::create(
    const std::string& owner_id, const std::string& name,
    const std::string& model_id, const std::string& runtime,
    const std::string& host, const std::uint16_t port,
    const std::string& protocol, const std::string& authentication_method,
    const std::uint32_t rate_limit_per_minute, const std::string& auth_token) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("inference endpoint name is invalid");
    }
    if (model_id.empty()) {
        throw std::invalid_argument("inference endpoint model id is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    InferenceEndpoint endpoint;
    endpoint.id = random_id();
    endpoint.name = name;
    endpoint.model_id = model_id;
    endpoint.runtime = runtime;
    endpoint.host = host;
    endpoint.port = port;
    endpoint.protocol = protocol;
    endpoint.authentication_method = authentication_method;
    endpoint.rate_limit_per_minute = rate_limit_per_minute;
    endpoint.owner_id = owner_id;
    endpoint.status = InferenceEndpointStatus::draft;
    endpoint.auth_token_hash = auth_token.empty() ? std::string{} : sha256_hex(auth_token);
    endpoint.created_at_epoch_seconds = epoch_seconds();
    endpoint.updated_at_epoch_seconds = endpoint.created_at_epoch_seconds;
    endpoints_[endpoint.id] = endpoint;
    if (records_) persist(endpoint);
    return endpoint;
}

std::optional<InferenceEndpoint> InferenceEndpointStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = endpoints_.find(id);
    return found != endpoints_.end()
               ? std::optional<InferenceEndpoint>(found->second)
               : std::nullopt;
}

std::vector<InferenceEndpoint> InferenceEndpointStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<InferenceEndpoint> result;
    result.reserve(endpoints_.size());
    for (const auto& item : endpoints_) result.push_back(item.second);
    return result;
}

bool InferenceEndpointStore::set_status(const std::string& id,
                                        const InferenceEndpointStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool InferenceEndpointStore::set_policy(
    const std::string& id, const bool content_scan_enabled,
    const bool block_on_scan_finding, const bool block_answer_on_scan_finding,
    const std::string& safety_policy_id, const bool model_classifier_enabled,
    const double model_classifier_confidence_floor) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end()) return false;
    found->second.content_scan_enabled = content_scan_enabled;
    found->second.block_on_scan_finding = block_on_scan_finding;
    found->second.block_answer_on_scan_finding = block_answer_on_scan_finding;
    found->second.safety_policy_id = safety_policy_id;
    found->second.model_classifier_enabled = model_classifier_enabled;
    found->second.model_classifier_confidence_floor = model_classifier_confidence_floor;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool InferenceEndpointStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = endpoints_.find(id);
    if (found == endpoints_.end()) return false;
    endpoints_.erase(found);
    if (records_) records_->erase("ml_inference_endpoints", id);
    return true;
}

std::string inference_endpoint_json(const InferenceEndpoint& endpoint) {
    return "{\"id\":\"" + json_escape(endpoint.id) + "\",\"name\":\"" +
           json_escape(endpoint.name) + "\",\"modelId\":\"" +
           json_escape(endpoint.model_id) + "\",\"runtime\":\"" +
           json_escape(endpoint.runtime) + "\",\"host\":\"" +
           json_escape(endpoint.host) + "\",\"port\":" +
           std::to_string(endpoint.port) + ",\"protocol\":\"" +
           json_escape(endpoint.protocol) +
           "\",\"authenticationMethod\":\"" +
           json_escape(endpoint.authentication_method) +
           "\",\"rateLimitPerMinute\":" +
           std::to_string(endpoint.rate_limit_per_minute) +
           ",\"ownerId\":\"" + json_escape(endpoint.owner_id) +
           "\",\"status\":\"" +
           inference_endpoint_status_name(endpoint.status) +
           "\",\"contentScanEnabled\":" +
           (endpoint.content_scan_enabled ? "true" : "false") +
           ",\"blockOnScanFinding\":" +
           (endpoint.block_on_scan_finding ? "true" : "false") +
           ",\"blockAnswerOnScanFinding\":" +
           (endpoint.block_answer_on_scan_finding ? "true" : "false") +
           ",\"safetyPolicyId\":\"" + json_escape(endpoint.safety_policy_id) +
           "\",\"modelClassifierEnabled\":" +
           (endpoint.model_classifier_enabled ? "true" : "false") +
           ",\"modelClassifierConfidenceFloor\":" +
           std::to_string(endpoint.model_classifier_confidence_floor) +
           ",\"createdAtEpochSeconds\":" +
           std::to_string(endpoint.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(endpoint.updated_at_epoch_seconds) + "}";
}

std::string inference_endpoints_json(
    const std::vector<InferenceEndpoint>& endpoints) {
    std::string body = "[";
    bool first = true;
    for (const auto& endpoint : endpoints) {
        if (!first) body += ",";
        first = false;
        body += inference_endpoint_json(endpoint);
    }
    return body + "]";
}

// Phase 63: docs/PLAN.md "Machine Learning Abilities" section 30 (Hardware
// and Compute) -- see ComputeNode's class comment in masterai.hpp for the
// live-telemetry fields this scoped-down registry defers.
std::string compute_node_status_name(const ComputeNodeStatus status) {
    switch (status) {
        case ComputeNodeStatus::available: return "available";
        case ComputeNodeStatus::reserved: return "reserved";
        case ComputeNodeStatus::draining: return "draining";
        case ComputeNodeStatus::disabled: return "disabled";
    }
    throw std::runtime_error("invalid compute node status");
}

ComputeNodeStatus parse_compute_node_status(const std::string& status) {
    if (status == "available") return ComputeNodeStatus::available;
    if (status == "reserved") return ComputeNodeStatus::reserved;
    if (status == "draining") return ComputeNodeStatus::draining;
    if (status == "disabled") return ComputeNodeStatus::disabled;
    throw std::runtime_error("stored compute node status is invalid");
}

ComputeNodeStore::ComputeNodeStore(RecordStore& records) : records_(&records) {
    restore();
}

void ComputeNodeStore::restore() {
    for (const auto& item : records_->list("ml_compute_nodes")) {
        const auto fields = unpack(item.second);
        // Phase 67 added `is_local` as a 9th field; Phase 75 added
        // `agent_url`/`agent_shared_secret_hash` as 10th/11th fields.
        // Records written before either phase still have 8 or 9 fields and
        // default the newer ones to empty/false, matching their prior
        // behavior exactly.
        if (fields.size() != 8U && fields.size() != 9U && fields.size() != 11U) {
            throw std::runtime_error(
                "persisted compute node record field count is wrong");
        }
        ComputeNode node;
        node.id = item.first;
        node.name = fields[0];
        node.address = fields[1];
        node.operating_system = fields[2];
        node.cpu_description = fields[3];
        node.gpu_description = fields[4];
        node.memory_mib = std::stoull(fields[5]);
        node.owner_id = fields[6];
        node.status = parse_compute_node_status(fields[7]);
        node.is_local = fields.size() >= 9U && fields[8] == "1";
        if (fields.size() == 11U) {
            node.agent_url = fields[9];
            node.agent_shared_secret_hash = fields[10];
        }
        nodes_[node.id] = node;
    }
}

void ComputeNodeStore::persist(const ComputeNode& node) {
    records_->put("ml_compute_nodes", node.id,
                  pack({node.name, node.address, node.operating_system,
                       node.cpu_description, node.gpu_description,
                       std::to_string(node.memory_mib), node.owner_id,
                       compute_node_status_name(node.status),
                       node.is_local ? "1" : "0", node.agent_url,
                       node.agent_shared_secret_hash}));
}

ComputeNode ComputeNodeStore::create(
    const std::string& owner_id, const std::string& name,
    const std::string& address, const std::string& operating_system,
    const std::string& cpu_description, const std::string& gpu_description,
    const std::uint64_t memory_mib, const bool is_local,
    const std::string& agent_url, const std::string& agent_shared_secret) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("compute node name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ComputeNode node;
    node.id = random_id();
    node.name = name;
    node.address = address;
    node.operating_system = operating_system;
    node.cpu_description = cpu_description;
    node.gpu_description = gpu_description;
    node.memory_mib = memory_mib;
    node.owner_id = owner_id;
    node.status = ComputeNodeStatus::available;
    node.is_local = is_local;
    node.agent_url = agent_url;
    // Never persist the plaintext secret -- only its hash, the same
    // constant_time_equal-verified convention parse_setup_fields uses.
    node.agent_shared_secret_hash =
        agent_shared_secret.empty() ? std::string{} : sha256_hex(agent_shared_secret);
    node.created_at_epoch_seconds = epoch_seconds();
    node.updated_at_epoch_seconds = node.created_at_epoch_seconds;
    nodes_[node.id] = node;
    if (records_) persist(node);
    return node;
}

std::optional<ComputeNode> ComputeNodeStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = nodes_.find(id);
    return found != nodes_.end() ? std::optional<ComputeNode>(found->second)
                                 : std::nullopt;
}

std::vector<ComputeNode> ComputeNodeStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ComputeNode> result;
    result.reserve(nodes_.size());
    for (const auto& item : nodes_) result.push_back(item.second);
    return result;
}

bool ComputeNodeStore::set_status(const std::string& id,
                                  const ComputeNodeStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool ComputeNodeStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = nodes_.find(id);
    if (found == nodes_.end()) return false;
    nodes_.erase(found);
    if (records_) records_->erase("ml_compute_nodes", id);
    return true;
}

std::string compute_node_json(const ComputeNode& node) {
    return "{\"id\":\"" + json_escape(node.id) + "\",\"name\":\"" +
           json_escape(node.name) + "\",\"address\":\"" +
           json_escape(node.address) + "\",\"operatingSystem\":\"" +
           json_escape(node.operating_system) + "\",\"cpuDescription\":\"" +
           json_escape(node.cpu_description) + "\",\"gpuDescription\":\"" +
           json_escape(node.gpu_description) + "\",\"memoryMib\":" +
           std::to_string(node.memory_mib) + ",\"ownerId\":\"" +
           json_escape(node.owner_id) + "\",\"status\":\"" +
           compute_node_status_name(node.status) + "\",\"isLocal\":" +
           (node.is_local ? "true" : "false") + ",\"agentUrl\":\"" +
           json_escape(node.agent_url) + "\",\"hasAgentSecret\":" +
           (node.agent_shared_secret_hash.empty() ? "false" : "true") +
           ",\"createdAtEpochSeconds\":" +
           std::to_string(node.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(node.updated_at_epoch_seconds) + "}";
}

std::string compute_nodes_json(const std::vector<ComputeNode>& nodes) {
    std::string body = "[";
    bool first = true;
    for (const auto& node : nodes) {
        if (!first) body += ",";
        first = false;
        body += compute_node_json(node);
    }
    return body + "]";
}

// Phase 64/69: docs/PLAN.md "Machine Learning Abilities" section 37
// (Automated Machine Learning Pipelines) -- see AutomationPipeline's class
// comment in masterai.hpp for which stages Phase 69 made real and why the
// rest are honestly recorded as skipped.
std::string automation_pipeline_status_name(const AutomationPipelineStatus status) {
    switch (status) {
        case AutomationPipelineStatus::draft: return "draft";
        case AutomationPipelineStatus::active: return "active";
        case AutomationPipelineStatus::disabled: return "disabled";
    }
    throw std::runtime_error("invalid automation pipeline status");
}

AutomationPipelineStatus parse_automation_pipeline_status(
    const std::string& status) {
    if (status == "draft") return AutomationPipelineStatus::draft;
    if (status == "active") return AutomationPipelineStatus::active;
    if (status == "disabled") return AutomationPipelineStatus::disabled;
    throw std::runtime_error("stored automation pipeline status is invalid");
}

std::string automation_pipeline_run_status_name(
    const AutomationPipelineRunStatus status) {
    switch (status) {
        case AutomationPipelineRunStatus::queued: return "queued";
        case AutomationPipelineRunStatus::running: return "running";
        case AutomationPipelineRunStatus::completed: return "completed";
        case AutomationPipelineRunStatus::failed: return "failed";
        case AutomationPipelineRunStatus::canceled: return "canceled";
    }
    throw std::runtime_error("invalid automation pipeline run status");
}

AutomationPipelineRunStatus parse_automation_pipeline_run_status(
    const std::string& status) {
    if (status == "queued") return AutomationPipelineRunStatus::queued;
    if (status == "running") return AutomationPipelineRunStatus::running;
    if (status == "completed") return AutomationPipelineRunStatus::completed;
    if (status == "failed") return AutomationPipelineRunStatus::failed;
    if (status == "canceled") return AutomationPipelineRunStatus::canceled;
    throw std::runtime_error("stored automation pipeline run status is invalid");
}

AutomationPipelineStore::AutomationPipelineStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void AutomationPipelineStore::restore() {
    for (const auto& item : records_->list("ml_automation_pipelines")) {
        const auto fields = unpack(item.second);
        // Phase 69 added dataset_id/model_id as fields 6-7; records written
        // before that phase have 6 fields and default both to empty.
        if (fields.size() != 6U && fields.size() != 8U) {
            throw std::runtime_error(
                "persisted automation pipeline record field count is wrong");
        }
        AutomationPipeline pipeline;
        pipeline.id = item.first;
        pipeline.name = fields[0];
        pipeline.project_id = fields[1];
        pipeline.description = fields[2];
        pipeline.stages = fields[3];
        pipeline.owner_id = fields[4];
        pipeline.status = parse_automation_pipeline_status(fields[5]);
        if (fields.size() == 8U) {
            pipeline.dataset_id = fields[6];
            pipeline.model_id = fields[7];
        }
        pipelines_[pipeline.id] = pipeline;
    }
    for (const auto& item : records_->list("ml_automation_pipeline_runs")) {
        const auto fields = unpack(item.second);
        // Phase 69 added stage_results_json as field 5; records written
        // before that phase have 5 fields and default it to "[]". Phase 71
        // added the three progress fields (6-8); records written before
        // that phase have 5 or 6 fields and default progress to a finished
        // run's values (every field skipped forward before the run's
        // recorded status already reflects a terminal state).
        if (fields.size() != 5U && fields.size() != 6U && fields.size() != 9U) {
            throw std::runtime_error(
                "persisted automation pipeline run record field count is wrong");
        }
        AutomationPipelineRun run;
        run.id = item.first;
        run.pipeline_id = fields[0];
        run.owner_id = fields[1];
        run.status = parse_automation_pipeline_run_status(fields[2]);
        run.outcome_note = fields[3];
        run.created_at_epoch_seconds = std::stoull(fields[4]);
        run.updated_at_epoch_seconds = run.created_at_epoch_seconds;
        run.stage_results_json = fields.size() >= 6U ? fields[5] : "[]";
        if (fields.size() == 9U) {
            run.total_stage_count =
                static_cast<std::uint32_t>(std::stoull(fields[6]));
            run.completed_stage_count =
                static_cast<std::uint32_t>(std::stoull(fields[7]));
            run.current_stage = fields[8];
        }
        runs_[run.id] = run;
    }
}

void AutomationPipelineStore::persist(const AutomationPipeline& pipeline) {
    records_->put(
        "ml_automation_pipelines", pipeline.id,
        pack({pipeline.name, pipeline.project_id, pipeline.description,
             pipeline.stages, pipeline.owner_id,
             automation_pipeline_status_name(pipeline.status),
             pipeline.dataset_id, pipeline.model_id}));
}

void AutomationPipelineStore::persist_run(const AutomationPipelineRun& run) {
    records_->put(
        "ml_automation_pipeline_runs", run.id,
        pack({run.pipeline_id, run.owner_id,
             automation_pipeline_run_status_name(run.status),
             run.outcome_note,
             std::to_string(run.created_at_epoch_seconds),
             run.stage_results_json,
             std::to_string(run.total_stage_count),
             std::to_string(run.completed_stage_count),
             run.current_stage}));
}

AutomationPipeline AutomationPipelineStore::create(
    const std::string& owner_id, const std::string& name,
    const std::string& project_id, const std::string& description,
    const std::string& stages, const std::string& dataset_id,
    const std::string& model_id) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("automation pipeline name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    AutomationPipeline pipeline;
    pipeline.id = random_id();
    pipeline.name = name;
    pipeline.project_id = project_id;
    pipeline.description = description;
    pipeline.stages = stages;
    pipeline.dataset_id = dataset_id;
    pipeline.model_id = model_id;
    pipeline.owner_id = owner_id;
    pipeline.status = AutomationPipelineStatus::draft;
    pipeline.created_at_epoch_seconds = epoch_seconds();
    pipeline.updated_at_epoch_seconds = pipeline.created_at_epoch_seconds;
    pipelines_[pipeline.id] = pipeline;
    if (records_) persist(pipeline);
    return pipeline;
}

std::optional<AutomationPipeline> AutomationPipelineStore::find(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pipelines_.find(id);
    return found != pipelines_.end()
               ? std::optional<AutomationPipeline>(found->second)
               : std::nullopt;
}

std::vector<AutomationPipeline> AutomationPipelineStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AutomationPipeline> result;
    result.reserve(pipelines_.size());
    for (const auto& item : pipelines_) result.push_back(item.second);
    return result;
}

bool AutomationPipelineStore::set_status(
    const std::string& id, const AutomationPipelineStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pipelines_.find(id);
    if (found == pipelines_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist(found->second);
    return true;
}

bool AutomationPipelineStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = pipelines_.find(id);
    if (found == pipelines_.end()) return false;
    pipelines_.erase(found);
    if (records_) records_->erase("ml_automation_pipelines", id);
    return true;
}

AutomationPipelineRun AutomationPipelineStore::begin_run(
    const std::string& owner_id, const std::string& pipeline_id,
    const std::uint32_t total_stage_count) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (pipelines_.find(pipeline_id) == pipelines_.end()) {
        throw std::invalid_argument("automation pipeline not found");
    }
    AutomationPipelineRun run;
    run.id = random_id();
    run.pipeline_id = pipeline_id;
    run.owner_id = owner_id;
    run.status = AutomationPipelineRunStatus::running;
    run.total_stage_count = total_stage_count;
    run.created_at_epoch_seconds = epoch_seconds();
    run.updated_at_epoch_seconds = run.created_at_epoch_seconds;
    runs_[run.id] = run;
    if (records_) persist_run(run);
    return run;
}

bool AutomationPipelineStore::append_stage_result(
    const std::string& run_id, const std::string& stage_results_json,
    const std::uint32_t completed_stage_count,
    const std::string& current_stage) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(run_id);
    if (found == runs_.end()) return false;
    found->second.stage_results_json = stage_results_json;
    found->second.completed_stage_count = completed_stage_count;
    found->second.current_stage = current_stage;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist_run(found->second);
    return true;
}

bool AutomationPipelineStore::finish_run(
    const std::string& run_id, const AutomationPipelineRunStatus status,
    const std::string& outcome_note) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(run_id);
    if (found == runs_.end()) return false;
    found->second.status = status;
    found->second.outcome_note = outcome_note;
    found->second.current_stage.clear();
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist_run(found->second);
    return true;
}

std::optional<AutomationPipelineRun> AutomationPipelineStore::find_run(
    const std::string& run_id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(run_id);
    return found != runs_.end() ? std::optional<AutomationPipelineRun>(found->second)
                                 : std::nullopt;
}

std::vector<AutomationPipelineRun> AutomationPipelineStore::runs_for(
    const std::string& pipeline_id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<AutomationPipelineRun> result;
    for (const auto& item : runs_) {
        if (item.second.pipeline_id == pipeline_id) result.push_back(item.second);
    }
    return result;
}

std::string automation_pipeline_json(const AutomationPipeline& pipeline) {
    return "{\"id\":\"" + json_escape(pipeline.id) + "\",\"name\":\"" +
           json_escape(pipeline.name) + "\",\"projectId\":\"" +
           json_escape(pipeline.project_id) + "\",\"description\":\"" +
           json_escape(pipeline.description) + "\",\"stages\":\"" +
           json_escape(pipeline.stages) + "\",\"datasetId\":\"" +
           json_escape(pipeline.dataset_id) + "\",\"modelId\":\"" +
           json_escape(pipeline.model_id) + "\",\"ownerId\":\"" +
           json_escape(pipeline.owner_id) + "\",\"status\":\"" +
           automation_pipeline_status_name(pipeline.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(pipeline.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(pipeline.updated_at_epoch_seconds) + "}";
}

std::string automation_pipelines_json(
    const std::vector<AutomationPipeline>& pipelines) {
    std::string body = "[";
    bool first = true;
    for (const auto& pipeline : pipelines) {
        if (!first) body += ",";
        first = false;
        body += automation_pipeline_json(pipeline);
    }
    return body + "]";
}

std::string automation_pipeline_run_json(const AutomationPipelineRun& run) {
    return "{\"id\":\"" + json_escape(run.id) + "\",\"pipelineId\":\"" +
           json_escape(run.pipeline_id) + "\",\"ownerId\":\"" +
           json_escape(run.owner_id) + "\",\"status\":\"" +
           automation_pipeline_run_status_name(run.status) +
           "\",\"outcomeNote\":\"" + json_escape(run.outcome_note) +
           "\",\"stageResults\":" +
           (run.stage_results_json.empty() ? "[]" : run.stage_results_json) +
           // Phase 71: live progress -- totalStageCount is 0 until
           // begin_run() knows the pipeline's real stage count, and
           // currentStage is empty once the run reaches a terminal status.
           ",\"totalStageCount\":" + std::to_string(run.total_stage_count) +
           ",\"completedStageCount\":" +
           std::to_string(run.completed_stage_count) +
           ",\"currentStage\":\"" + json_escape(run.current_stage) + "\"" +
           ",\"createdAtEpochSeconds\":" +
           std::to_string(run.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(run.updated_at_epoch_seconds) + "}";
}

std::string automation_pipeline_runs_json(
    const std::vector<AutomationPipelineRun>& runs) {
    std::string body = "[";
    bool first = true;
    for (const auto& run : runs) {
        if (!first) body += ",";
        first = false;
        body += automation_pipeline_run_json(run);
    }
    return body + "]";
}

// Phase 65: docs/PLAN.md "Machine Learning Abilities" section 40 (Safety
// and Governance) -- see SafetyPolicy/ModelCard's class comment in
// masterai.hpp for the content-scanning fields this scoped-down registry
// defers. Reuses the pending/approved/rejected workflow for both the
// policy and the model card since each is an independent approval
// decision.
std::string safety_policy_status_name(const SafetyPolicyStatus status) {
    switch (status) {
        case SafetyPolicyStatus::pending: return "pending";
        case SafetyPolicyStatus::approved: return "approved";
        case SafetyPolicyStatus::rejected: return "rejected";
    }
    throw std::runtime_error("invalid safety policy status");
}

SafetyPolicyStatus parse_safety_policy_status(const std::string& status) {
    if (status == "pending") return SafetyPolicyStatus::pending;
    if (status == "approved") return SafetyPolicyStatus::approved;
    if (status == "rejected") return SafetyPolicyStatus::rejected;
    throw std::runtime_error("stored safety policy status is invalid");
}

SafetyGovernanceStore::SafetyGovernanceStore(RecordStore& records)
    : records_(&records) {
    restore();
}

void SafetyGovernanceStore::restore() {
    for (const auto& item : records_->list("ml_safety_policies")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 5U) {
            throw std::runtime_error(
                "persisted safety policy record field count is wrong");
        }
        SafetyPolicy policy;
        policy.id = item.first;
        policy.name = fields[0];
        policy.scope = fields[1];
        policy.restricted_data_categories = fields[2];
        policy.owner_id = fields[3];
        policy.status = parse_safety_policy_status(fields[4]);
        policies_[policy.id] = policy;
    }
    for (const auto& item : records_->list("ml_model_cards")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 9U) {
            throw std::runtime_error(
                "persisted model card record field count is wrong");
        }
        ModelCard card;
        card.id = item.first;
        card.model_id = fields[0];
        card.purpose = fields[1];
        card.intended_use = fields[2];
        card.prohibited_use = fields[3];
        card.training_data_reference = fields[4];
        card.evaluation_results = fields[5];
        card.known_limitations = fields[6];
        card.license = fields[7];
        const auto tail = unpack(fields[8]);
        if (tail.size() != 2U) {
            throw std::runtime_error(
                "persisted model card tail field count is wrong");
        }
        card.owner_id = tail[0];
        card.status = parse_safety_policy_status(tail[1]);
        model_cards_[card.id] = card;
    }
}

void SafetyGovernanceStore::persist_policy(const SafetyPolicy& policy) {
    records_->put("ml_safety_policies", policy.id,
                  pack({policy.name, policy.scope,
                       policy.restricted_data_categories, policy.owner_id,
                       safety_policy_status_name(policy.status)}));
}

void SafetyGovernanceStore::persist_model_card(const ModelCard& card) {
    records_->put(
        "ml_model_cards", card.id,
        pack({card.model_id, card.purpose, card.intended_use,
             card.prohibited_use, card.training_data_reference,
             card.evaluation_results, card.known_limitations, card.license,
             pack({card.owner_id, safety_policy_status_name(card.status)})}));
}

SafetyPolicy SafetyGovernanceStore::create_policy(
    const std::string& owner_id, const std::string& name,
    const std::string& scope,
    const std::string& restricted_data_categories) {
    if (name.empty() || name.size() > 160U) {
        throw std::invalid_argument("safety policy name is invalid");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    SafetyPolicy policy;
    policy.id = random_id();
    policy.name = name;
    policy.scope = scope;
    policy.restricted_data_categories = restricted_data_categories;
    policy.owner_id = owner_id;
    policy.status = SafetyPolicyStatus::pending;
    policy.created_at_epoch_seconds = epoch_seconds();
    policy.updated_at_epoch_seconds = policy.created_at_epoch_seconds;
    policies_[policy.id] = policy;
    if (records_) persist_policy(policy);
    return policy;
}

std::optional<SafetyPolicy> SafetyGovernanceStore::find_policy(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = policies_.find(id);
    return found != policies_.end() ? std::optional<SafetyPolicy>(found->second)
                                    : std::nullopt;
}

std::vector<SafetyPolicy> SafetyGovernanceStore::list_policies() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<SafetyPolicy> result;
    result.reserve(policies_.size());
    for (const auto& item : policies_) result.push_back(item.second);
    return result;
}

bool SafetyGovernanceStore::set_policy_status(
    const std::string& id, const SafetyPolicyStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = policies_.find(id);
    if (found == policies_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist_policy(found->second);
    return true;
}

bool SafetyGovernanceStore::remove_policy(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = policies_.find(id);
    if (found == policies_.end()) return false;
    policies_.erase(found);
    if (records_) records_->erase("ml_safety_policies", id);
    return true;
}

ModelCard SafetyGovernanceStore::create_model_card(
    const std::string& owner_id, const std::string& model_id,
    const std::string& purpose, const std::string& intended_use,
    const std::string& prohibited_use,
    const std::string& training_data_reference,
    const std::string& evaluation_results,
    const std::string& known_limitations, const std::string& license) {
    if (model_id.empty()) {
        throw std::invalid_argument("model card model id is required");
    }
    if (purpose.empty()) {
        throw std::invalid_argument("model card purpose is required");
    }
    const std::lock_guard<std::mutex> lock(mutex_);
    ModelCard card;
    card.id = random_id();
    card.model_id = model_id;
    card.purpose = purpose;
    card.intended_use = intended_use;
    card.prohibited_use = prohibited_use;
    card.training_data_reference = training_data_reference;
    card.evaluation_results = evaluation_results;
    card.known_limitations = known_limitations;
    card.license = license;
    card.owner_id = owner_id;
    card.status = SafetyPolicyStatus::pending;
    card.created_at_epoch_seconds = epoch_seconds();
    card.updated_at_epoch_seconds = card.created_at_epoch_seconds;
    model_cards_[card.id] = card;
    if (records_) persist_model_card(card);
    return card;
}

std::optional<ModelCard> SafetyGovernanceStore::find_model_card(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = model_cards_.find(id);
    return found != model_cards_.end() ? std::optional<ModelCard>(found->second)
                                       : std::nullopt;
}

std::vector<ModelCard> SafetyGovernanceStore::list_model_cards() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ModelCard> result;
    result.reserve(model_cards_.size());
    for (const auto& item : model_cards_) result.push_back(item.second);
    return result;
}

bool SafetyGovernanceStore::set_model_card_status(
    const std::string& id, const SafetyPolicyStatus status) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = model_cards_.find(id);
    if (found == model_cards_.end()) return false;
    found->second.status = status;
    found->second.updated_at_epoch_seconds = epoch_seconds();
    if (records_) persist_model_card(found->second);
    return true;
}

bool SafetyGovernanceStore::remove_model_card(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = model_cards_.find(id);
    if (found == model_cards_.end()) return false;
    model_cards_.erase(found);
    if (records_) records_->erase("ml_model_cards", id);
    return true;
}

std::string safety_policy_json(const SafetyPolicy& policy) {
    return "{\"id\":\"" + json_escape(policy.id) + "\",\"name\":\"" +
           json_escape(policy.name) + "\",\"scope\":\"" +
           json_escape(policy.scope) + "\",\"restrictedDataCategories\":\"" +
           json_escape(policy.restricted_data_categories) +
           "\",\"ownerId\":\"" + json_escape(policy.owner_id) +
           "\",\"status\":\"" + safety_policy_status_name(policy.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(policy.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(policy.updated_at_epoch_seconds) + "}";
}

std::string safety_policies_json(const std::vector<SafetyPolicy>& policies) {
    std::string body = "[";
    bool first = true;
    for (const auto& policy : policies) {
        if (!first) body += ",";
        first = false;
        body += safety_policy_json(policy);
    }
    return body + "]";
}

std::string model_card_json(const ModelCard& card) {
    return "{\"id\":\"" + json_escape(card.id) + "\",\"modelId\":\"" +
           json_escape(card.model_id) + "\",\"purpose\":\"" +
           json_escape(card.purpose) + "\",\"intendedUse\":\"" +
           json_escape(card.intended_use) + "\",\"prohibitedUse\":\"" +
           json_escape(card.prohibited_use) +
           "\",\"trainingDataReference\":\"" +
           json_escape(card.training_data_reference) +
           "\",\"evaluationResults\":\"" +
           json_escape(card.evaluation_results) +
           "\",\"knownLimitations\":\"" +
           json_escape(card.known_limitations) + "\",\"license\":\"" +
           json_escape(card.license) + "\",\"ownerId\":\"" +
           json_escape(card.owner_id) + "\",\"status\":\"" +
           safety_policy_status_name(card.status) +
           "\",\"createdAtEpochSeconds\":" +
           std::to_string(card.created_at_epoch_seconds) +
           ",\"updatedAtEpochSeconds\":" +
           std::to_string(card.updated_at_epoch_seconds) + "}";
}

std::string model_cards_json(const std::vector<ModelCard>& cards) {
    std::string body = "[";
    bool first = true;
    for (const auto& card : cards) {
        if (!first) body += ",";
        first = false;
        body += model_card_json(card);
    }
    return body + "]";
}

// Phase 66: docs/PLAN.md "Machine Learning Abilities" section 43 (Audit
// Logs) -- see AuditLog::recent()'s class comment in masterai.hpp; this
// only formats entries that store already read.
std::string audit_log_entries_json(const std::vector<AuditLogEntry>& entries) {
    std::string body = "[";
    bool first = true;
    for (const auto& entry : entries) {
        if (!first) body += ",";
        first = false;
        body += "{\"timestampEpochSeconds\":" +
               std::to_string(entry.timestamp_epoch_seconds) +
               ",\"event\":\"" + json_escape(entry.event) + "\",\"actor\":\"" +
               json_escape(entry.actor) + "\",\"outcome\":\"" +
               json_escape(entry.outcome) + "\",\"detail\":\"" +
               json_escape(entry.detail) + "\"}";
    }
    return body + "]";
}

}  // namespace masterai
