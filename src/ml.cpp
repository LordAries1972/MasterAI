// Machine Learning foundation phase: administrator-only scaffolding for
// docs/PLAN.md's "Machine Learning Abilities" section. Dashboard, Projects,
// Model Registry, Dataset Manager, and Subject Knowledge Manager are real;
// the remaining 20 planned interfaces (Model Builder, Training Jobs, ...)
// are listed so an administrator can see the roadmap, but none of them have
// a backing service yet -- see MachineLearningRegistry's class comment in
// masterai.hpp for why this stays honest rather than fabricating data.
#include "masterai.hpp"

#include <algorithm>
#include <chrono>
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
        {"model-builder", "Model Builder", "planned"},
        {"dataset-manager", "Dataset Manager", "available"},
        {"subject-knowledge", "Subject Knowledge Manager", "available"},
        {"data-labeling", "Data Labeling", "planned"},
        {"data-preparation", "Data Preparation", "planned"},
        {"training-jobs", "Training Jobs", "planned"},
        {"fine-tuning", "Fine-Tuning", "planned"},
        {"evaluation-lab", "Evaluation Lab", "planned"},
        {"experiment-tracking", "Experiment Tracking", "planned"},
        {"prompt-instruction-training", "Prompt and Instruction Training",
         "planned"},
        {"embeddings-vector-stores", "Embeddings and Vector Stores",
         "planned"},
        {"retrieval-augmented-generation", "Retrieval-Augmented Generation",
         "planned"},
        {"synthetic-data", "Synthetic Data", "planned"},
        {"model-comparison", "Model Comparison", "planned"},
        {"deployment-manager", "Deployment Manager", "planned"},
        {"inference-endpoints", "Inference Endpoints", "planned"},
        {"hardware-compute", "Hardware and Compute", "planned"},
        {"automation-pipelines", "Automation Pipelines", "planned"},
        {"safety-governance", "Safety and Governance", "planned"},
        {"monitoring-diagnostics", "Monitoring and Diagnostics", "planned"},
        {"audit-logs", "Audit Logs", "planned"},
        {"ml-settings", "Machine Learning Settings", "planned"},
    };
}

MachineLearningDashboard MachineLearningRegistry::dashboard(
    const MLProjectStore& projects, const ModelRegistryStore& models) const {
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
    // models_awaiting_approval and failed_training_jobs still default to
    // zero (see the struct's own comment): there is no distinct "awaiting
    // approval" state and no training-job store behind this phase yet.
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

}  // namespace masterai
