// Machine Learning foundation phase: administrator-only scaffolding for
// docs/PLAN.md's "Machine Learning Abilities" section. Dashboard, Projects,
// Model Registry, Dataset Manager, Subject Knowledge Manager, Data
// Labeling, Data Preparation, and Training Jobs are real; the remaining 18
// planned interfaces (Model Builder, Fine-Tuning, ...) are listed so an
// administrator can see the roadmap, but none of them have a backing
// service yet -- see MachineLearningRegistry's class comment in
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
        {"data-labeling", "Data Labeling", "available"},
        {"data-preparation", "Data Preparation", "available"},
        {"training-jobs", "Training Jobs", "available"},
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
        if (fields.size() != 7U) {
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
        experiments_[experiment.id] = experiment;
    }
}

void ExperimentStore::persist(const Experiment& experiment) {
    records_->put(
        "ml_experiments", experiment.id,
        pack({experiment.project_id, experiment.model_id,
             experiment.dataset_id, experiment.name, experiment.description,
             experiment.owner_id,
             experiment_status_name(experiment.status)}));
}

Experiment ExperimentStore::create(
    const std::string& owner_id, const std::string& project_id,
    const std::string& model_id, const std::string& dataset_id,
    const std::string& name, const std::string& description) {
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

ModelBuilderConfigStore::ModelBuilderConfigStore(RecordStore& records) : records_(&records) {
    restore();
}

void ModelBuilderConfigStore::restore() {
    for (const auto& item : records_->list("ml_model_builder_configs")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 7U) {
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
        configs_[config.id] = config;
    }
}

void ModelBuilderConfigStore::persist(const ModelBuilderConfig& config) {
    records_->put(
        "ml_model_builder_configs", config.id,
        pack({config.project_id, config.base_model_id, config.name,
             config.description, config.source_type, config.owner_id,
             model_builder_config_status_name(config.status)}));
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

bool ModelBuilderConfigStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = configs_.find(id);
    if (found == configs_.end()) return false;
    configs_.erase(found);
    if (records_) records_->erase("ml_model_builder_configs", id);
    return true;
}

std::string model_builder_config_json(const ModelBuilderConfig& config) {
    return "{\"id\":\"" + json_escape(config.id) + "\",\"projectId\":\"" +
           json_escape(config.project_id) + "\",\"baseModelId\":\"" +
           json_escape(config.base_model_id) + "\",\"name\":\"" +
           json_escape(config.name) + "\",\"description\":\"" +
           json_escape(config.description) + "\",\"sourceType\":\"" +
           json_escape(config.source_type) + "\",\"ownerId\":\"" +
           json_escape(config.owner_id) + "\",\"status\":\"" +
           model_builder_config_status_name(config.status) +
           "\",\"createdAtEpochSeconds\":" +
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

}  // namespace masterai
