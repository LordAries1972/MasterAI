// MasterAI public core contract.
//
// This unit declares the independently authored C++17 control-plane services
// shared by the CLI, HTTP server, MCP transports, operational tooling, and
// validation tests. Implementations remain split by responsibility so callers
// depend on stable interfaces rather than platform-specific details.
#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace masterai {

enum class LogLevel { debug, info, warning, error };

void log(LogLevel level, const std::string& event, const std::string& detail);
bool constant_time_equal(const std::string& left, const std::string& right) noexcept;
std::vector<std::uint8_t> secure_random(std::size_t size);
std::string sha256_hex(const std::string& value);
std::string sha256_file_hex(const std::filesystem::path& path);
// PBKDF2-HMAC-SHA256 password hashing for locally stored accounts (never
// used for OS-mapped accounts, which are always verified by LogonUserW/PAM).
// Both functions consume and zero their `password` argument.
std::string hash_password(std::string password);
bool verify_password(std::string password, const std::string& encoded_hash);

struct AppConfig {
    int schema_version{1};
    std::string host{"127.0.0.1"};
    std::uint16_t port{7070};
    bool allow_intranet{false};
    std::string tls_mode{"disabled-loopback-only"};
    std::filesystem::path tls_certificate_file;
    std::filesystem::path tls_private_key_file;
    bool authentication_enabled{true};
    std::uint64_t session_minutes{480};
    // On by default: locally stored, MasterAI-hashed password accounts
    // (POST /api/v1/setup/local, POST /api/v1/users) are the default sign-in
    // system and do not depend on the OS identity provider being present or
    // configured.
    bool allow_local_password_accounts{true};
    // Off by default: OS-verified sign-in (Windows LogonUserW / Linux PAM,
    // via POST /api/v1/setup and the OS fallback branch of login()) is an
    // explicit opt-in, not a requirement. Enable it for operators who want
    // accounts mapped to their OS/Windows identity instead of, or alongside,
    // locally stored passwords.
    bool allow_os_identity_accounts{false};
    std::uint64_t max_request_bytes{16U * 1024U * 1024U};
    std::uint32_t request_timeout_seconds{30};
    std::uint32_t max_concurrent_connections{64};
    std::uint32_t rate_limit_per_minute{120};
    std::set<std::string> allowed_hosts{"127.0.0.1", "localhost"};
    std::set<std::string> allowed_origins;
    std::filesystem::path runtime_root{"runtime"};
    std::filesystem::path models_root{"models"};
    std::uint64_t memory_reserve_mib{2048};
    std::uint64_t memory_hard_limit_mib{0};
    unsigned int minimum_free_ram_percent{15U};
    unsigned int critical_memory_percent{92U};
    std::string resource_profile{"balanced"};
    std::filesystem::path llama_server_executable;
    std::filesystem::path curl_executable;
    std::uint16_t runner_port{7081};
    // On by default: a native ProjectWatcher observes every catalog project's
    // root for file saves and `.git/HEAD` branch switches and forwards them
    // into ProjectIndexService::request_update automatically. Off disables
    // only the automatic adapter; the authenticated index/notify route and
    // manual rebuild remain available either way.
    bool watch_project_files{true};
    // Phase 16: deadline-bound hybrid retrieval budget applied once per chat
    // message before prompt assembly. Disabling retrieval leaves attachment
    // context (Phase 5) untouched.
    bool retrieval_enabled{true};
    std::uint32_t retrieval_deadline_milliseconds{1500U};
    std::uint64_t retrieval_maximum_context_bytes{16U * 1024U};
    std::uint32_t retrieval_maximum_chunks_per_source{6U};
    std::uint32_t retrieval_maximum_total_chunks{20U};
};

class ConfigurationManager final {
public:
    static AppConfig safe_defaults();
    static AppConfig load(const std::filesystem::path& settings,
                          const std::map<std::string, std::string>& environment = {},
                          const std::map<std::string, std::string>& overrides = {});
    static void validate(const AppConfig& configuration);
    static void save_atomic(const AppConfig& configuration,
                            const std::filesystem::path& settings);
    static std::string serialize(const AppConfig& configuration);
};

class RecordStore final {
public:
    explicit RecordStore(std::filesystem::path root);
    void open();
    void put(const std::string& collection, const std::string& key,
             const std::string& value);
    void erase(const std::string& collection, const std::string& key);
    std::optional<std::string> get(const std::string& collection,
                                   const std::string& key) const;
    std::vector<std::pair<std::string, std::string>>
    list(const std::string& collection) const;
    void checkpoint();
    std::filesystem::path backup(const std::filesystem::path& backup_root) const;
    unsigned int schema_version() const noexcept;

private:
    std::filesystem::path root_;
    std::map<std::string, std::map<std::string, std::string>> records_;
    unsigned int schema_version_{1};
    bool opened_{false};
    // Guards records_ and the journal/snapshot files together so concurrent
    // request threads never interleave journal writes or observe a
    // checkpoint mid-rewrite. Mutable because get()/list() are logically
    // read-only but still need to take the lock.
    mutable std::mutex mutex_;
};

class SecretStore final {
public:
    explicit SecretStore(std::filesystem::path root);
    bool available() const noexcept;
    void set(const std::string& name, const std::string& secret);
    std::optional<std::string> get(const std::string& name) const;
    void erase(const std::string& name);

private:
    std::filesystem::path root_;
};

struct HardwareInfo {
    std::uint64_t total_ram_mib{0};
    std::uint64_t available_ram_mib{0};
    std::uint64_t total_virtual_memory_mib{0};
    std::uint64_t available_virtual_memory_mib{0};
    std::uint64_t free_disk_mib{0};
    std::uint64_t storage_capacity_mib{0};
    std::uint64_t gpu_memory_mib{0};
    unsigned int physical_cpu_count{0};
    unsigned int logical_cpu_count{0};
    unsigned int numa_node_count{1};
    std::string architecture;
    std::string platform;
    std::string storage_class{"unknown"};
    std::set<std::string> cpu_features;
    std::vector<std::string> gpu_backends;
    std::set<std::string> backend_capabilities;
};

HardwareInfo probe_hardware(const std::filesystem::path& storage_root);

struct ProcessResourceSample {
    std::uint64_t resident_memory_bytes{0};
    std::uint64_t private_memory_bytes{0};
    std::uint64_t commit_bytes{0};
    std::uint64_t page_faults{0};
};

ProcessResourceSample probe_process_resources();
bool is_path_within(const std::filesystem::path& root,
                    const std::filesystem::path& candidate);
bool valid_utf8_text(const std::string& value) noexcept;

enum class AuthenticationStatus {
    authenticated,
    denied,
    unavailable,
    invalid_input,
    system_error
};

struct AuthenticationResult {
    AuthenticationStatus status{AuthenticationStatus::unavailable};
    std::string principal;
    std::string diagnostic;

    bool authenticated() const noexcept {
        return status == AuthenticationStatus::authenticated;
    }
};

class OsIdentityProvider final {
public:
    bool available() const noexcept;

    // The supplied password is consumed and securely erased before this returns.
    // It is never persisted, encrypted, hashed, logged, or placed on a command line.
    AuthenticationResult authenticate(const std::string& username,
                                      std::string& password) const noexcept;
};

class SessionStore final {
public:
    struct Session {
        std::string user_id;
        std::uint64_t expires_at_epoch_seconds{0};
        bool revoked{false};
        std::string csrf_secret;
    };

    SessionStore() = default;
    explicit SessionStore(RecordStore& records);
    std::string create(const std::string& user_id,
                       std::uint64_t now_epoch_seconds,
                       std::uint64_t lifetime_seconds);
    std::optional<Session> validate(const std::string& token,
                                    std::uint64_t now_epoch_seconds) const;
    void revoke(const std::string& token);
    std::string rotate(const std::string& token,
                       std::uint64_t now_epoch_seconds,
                       std::uint64_t lifetime_seconds);
    void revoke_user(const std::string& user_id);

private:
    void persist(const std::string& token_hash, const Session& session);
    std::map<std::string, Session> sessions_;
    RecordStore* records_{nullptr};
    // Guards sessions_ against concurrent login/logout/refresh requests.
    mutable std::mutex mutex_;
};

enum class UserRole { administrator, developer, viewer };
bool role_allows(UserRole role, const std::string& permission);

struct UserRecord {
    std::string id;
    std::string os_principal;
    std::string display_name;
    UserRole role{UserRole::viewer};
    bool enabled{true};
    // Empty for OS-verified accounts (checked via LogonUserW/PAM). Non-empty
    // holds a PBKDF2 encoding for a locally stored password account, in
    // which case os_principal is a chosen local username rather than an OS
    // identity.
    std::string password_hash;
};

class UserStore final {
public:
    explicit UserStore(RecordStore& records);
    bool setup_required() const;
    UserRecord create_first_administrator(const std::string& os_principal,
                                          const std::string& display_name,
                                          const std::string& password_hash = {});
    UserRecord add(const std::string& os_principal,
                   const std::string& display_name, UserRole role,
                   const std::string& password_hash = {});
    std::optional<UserRecord> find_by_principal(
        const std::string& os_principal) const;
    std::optional<UserRecord> find_by_id(const std::string& id) const;
    std::vector<UserRecord> all() const;

private:
    RecordStore& records_;
};

class AuditLog final {
public:
    explicit AuditLog(std::filesystem::path path);
    void append(const std::string& event, const std::string& actor,
                const std::string& outcome, const std::string& detail);

private:
    std::filesystem::path path_;
    // append() reads the whole file to find the previous hash-chain link and
    // then writes the next line; both steps must happen as one atomic unit
    // under concurrent callers or the chain breaks / lines interleave.
    std::mutex mutex_;
};

class ApiTokenStore final {
public:
    struct Token {
        std::string user_id;
        std::set<std::string> scopes;
        std::set<std::string> project_ids;
        std::uint64_t expires_at_epoch_seconds{0};
        bool revoked{false};
    };

    explicit ApiTokenStore(RecordStore& records);
    std::string create(const std::string& user_id,
                       const std::set<std::string>& scopes,
                       std::uint64_t now_epoch_seconds,
                       std::uint64_t lifetime_seconds,
                       const std::set<std::string>& project_ids = {});
    std::optional<Token> validate(const std::string& token,
                                  const std::string& required_scope,
                                  std::uint64_t now_epoch_seconds) const;
    void revoke(const std::string& token);

private:
    void persist(const std::string& token_hash, const Token& token);
    RecordStore& records_;
    std::map<std::string, Token> tokens_;
    // Guards tokens_ against concurrent create/validate/revoke calls.
    mutable std::mutex mutex_;
};

enum class ModelState {
    discovered,
    valid,
    invalid,
    unverified,
    ready,
    loading,
    loaded,
    failed,
    quarantined,
    // Manifest is well-formed (a valid queued/in-progress download wrote it
    // before the transfer started), but its model file has not landed on
    // disk yet -- distinct from `invalid`, which means something is actually
    // wrong rather than merely not finished yet.
    downloading
};

struct ModelManifest {
    int schema_version{0};
    std::string id;
    std::string display_name;
    std::string category;
    std::string format;
    std::string architecture;
    std::string quantization;
    std::string model_file;
    std::string backend;
    std::uint64_t minimum_ram_mib{0};
    std::uint64_t recommended_ram_mib{0};
    std::uint64_t estimated_disk_mib{0};
    std::uint64_t model_size_bytes{0};
    std::string model_sha256;
    std::string source_url;
    std::string source_revision;
    std::string license_id;
    bool license_accepted{false};
    std::set<std::string> required_cpu_features;
    std::string required_gpu_backend;
};

struct ModelRecord {
    std::filesystem::path directory;
    ModelManifest manifest;
    ModelState state{ModelState::discovered};
    std::string diagnostic;
};

class ModelRegistry final {
public:
    explicit ModelRegistry(std::filesystem::path model_root,
                           HardwareInfo hardware = {},
                           std::uint64_t memory_reserve_mib = 2048);
    // Fast path used on every page load/API call: trusts the on-disk
    // verification cache (see verify() below) instead of hashing model
    // files, so an inventory/downloads/chat request never blocks on
    // multi-gigabyte SHA-256 work. A model with no matching cache entry
    // (new, or changed since it was last verified) comes back as
    // ModelState::unverified rather than ready.
    std::vector<ModelRecord> scan() const;
    // Slow path used only by the `masterai verify-models` CLI command (see
    // scripts/build.ps1 -VerifyModels): actually hashes every discovered
    // model file, reporting each one through `progress` as it completes, and
    // persists the results to the verification cache so subsequent scan()
    // calls recognize them as verified without re-hashing. Returns the
    // number of models newly confirmed verified.
    std::size_t verify(
        const std::function<void(const std::string& model_id, bool verified,
                                 const std::string& message)>& progress) const;

private:
    std::filesystem::path model_root_;
    HardwareInfo hardware_;
    std::uint64_t memory_reserve_mib_{2048};
};

// Writes model_directory/manifest.json in the one shape ModelRegistry::scan()
// (via load_manifest in models.cpp) accepts, from fields the caller has
// already validated. Shared by the download HTTP route and the
// download-model CLI command so both ways of fetching a model produce a
// manifest ModelRegistry can actually recognize -- a model file that lands
// without one is invisible to the inventory no matter how it got there.
// Throws std::runtime_error if the file cannot be written.
void write_model_manifest(const std::filesystem::path& model_directory,
                          const std::string& model_id,
                          const std::string& display_name,
                          const std::string& category,
                          const std::string& architecture,
                          const std::string& quantization,
                          std::uint64_t minimum_ram_mib,
                          std::uint64_t recommended_ram_mib,
                          const std::string& filename,
                          std::uint64_t size_bytes,
                          const std::string& sha256,
                          const std::string& source_url,
                          const std::string& revision,
                          const std::string& license_spdx);

enum class Suitability { unsupported, memory_risk, slow, usable, recommended };

struct SuitabilityResult {
    Suitability rating{Suitability::unsupported};
    std::uint64_t required_ram_mib{0};
    std::uint64_t available_ram_mib{0};
    std::string reason;
};

SuitabilityResult assess_model(const ModelManifest& manifest,
                               const HardwareInfo& hardware,
                               std::uint64_t reserve_mib);

struct LaunchSpec {
    std::filesystem::path executable;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
};

class LlamaCppAdapter final {
public:
    explicit LlamaCppAdapter(std::filesystem::path approved_backend);
    bool available() const;
    LaunchSpec build_launch_spec(const ModelRecord& model,
                                 unsigned int context_length,
                                 unsigned int port) const;

private:
    std::filesystem::path approved_backend_;
};

enum class RunnerState {
    unloaded,
    starting,
    ready,
    busy,
    stopping,
    failed
};

struct GenerationOptions {
    unsigned int max_tokens{512};
    double temperature{0.2};
    std::uint64_t seed{1};
    std::vector<std::string> stop_sequences;
};

struct GenerationResult {
    std::string text;
    std::uint64_t prompt_tokens{0};
    std::uint64_t generated_tokens{0};
    std::uint64_t elapsed_microseconds{0};
    bool cancelled{false};
};

struct RunnerMetrics {
    RunnerState state{RunnerState::unloaded};
    std::string model_id;
    std::uint64_t process_id{0};
    std::uint64_t resident_memory_bytes{0};
    std::uint64_t requests_completed{0};
    std::uint64_t requests_cancelled{0};
    std::string diagnostic;
};

class RunnerSupervisor final {
public:
    RunnerSupervisor(std::filesystem::path approved_backend,
                     std::filesystem::path runtime_root);
    ~RunnerSupervisor();
    RunnerSupervisor(const RunnerSupervisor&) = delete;
    RunnerSupervisor& operator=(const RunnerSupervisor&) = delete;

    void load(const ModelRecord& model, unsigned int context_length,
              unsigned int port, std::uint32_t startup_timeout_seconds = 30);
    void unload(std::uint32_t grace_seconds = 10) noexcept;
    std::uint64_t tokenize(const std::string& text);
    GenerationResult generate(
        const std::string& prompt, const GenerationOptions& options,
        const std::function<void(const std::string&)>& on_chunk,
        const std::atomic_bool& cancellation);
    RunnerMetrics metrics() const;

private:
    class Process;
    unsigned int ready_port(bool mark_busy);
    std::filesystem::path runtime_root_;
    LlamaCppAdapter adapter_;
    std::unique_ptr<Process> process_;
    mutable std::mutex mutex_;
    RunnerMetrics metrics_;
    unsigned int port_{0};
};

struct ProjectRecord {
    std::string id;
    std::string display_name;
    std::filesystem::path root;
};

struct ProjectTextFile {
    std::filesystem::path canonical_path;
    std::string content;
};

ProjectTextFile read_project_text_file(
    const ProjectRecord& project, const std::string& relative_path,
    std::uint64_t maximum_bytes);

class ProjectCatalog final {
public:
    explicit ProjectCatalog(std::filesystem::path workspace_root);
    ProjectCatalog(std::filesystem::path workspace_root, RecordStore& records);
    ProjectRecord add(const std::string& id, const std::string& display_name,
                      const std::filesystem::path& root);
    std::optional<ProjectRecord> find(const std::string& id) const;
    std::vector<ProjectRecord> list() const;

private:
    void restore();
    void persist(const ProjectRecord& project);
    std::filesystem::path workspace_root_;
    std::map<std::string, ProjectRecord> projects_;
    RecordStore* records_{nullptr};
    // Guards projects_ against concurrent add/find/list calls.
    mutable std::mutex mutex_;
};

enum class ChatRole { user, assistant, system };

struct ChatMessage {
    ChatRole role{ChatRole::user};
    std::string content;
    std::uint64_t created_at_epoch_seconds{0};
};

struct ChatRecord {
    std::string id;
    std::string owner_id;
    std::string project_id;
    std::string model_id;
    // Derived from the first user message once one exists; "New chat" until
    // then. Never accepted from client input, so it can never carry markup
    // or exceed the truncation this store applies when deriving it.
    std::string title{"New chat"};
    std::uint64_t created_at_epoch_seconds{0};
    std::vector<ChatMessage> messages;
};

class ChatStore final {
public:
    ChatStore() = default;
    explicit ChatStore(RecordStore& records);
    ChatRecord create(const std::string& owner_id,
                      const std::string& project_id,
                      const std::string& model_id);
    void append(const std::string& chat_id, ChatRole role,
                const std::string& content);
    std::optional<ChatRecord> find_for_owner(const std::string& chat_id,
                                             const std::string& owner_id) const;
    // Newest first, so callers can split "recent" from "history" by index
    // without re-sorting.
    std::vector<ChatRecord> list_for_owner(const std::string& owner_id) const;

private:
    void restore();
    void persist(const ChatRecord& chat);
    std::map<std::string, ChatRecord> chats_;
    RecordStore* records_{nullptr};
    // Guards chats_ against concurrent create/append/find/list calls.
    mutable std::mutex mutex_;
};

struct AttachmentRecord {
    std::string id;
    std::string owner_id;
    std::string project_id;
    std::string filename;
    std::filesystem::path stored_path;
    std::uint64_t size_bytes{0};
    std::string sha256;
};

class AttachmentStore final {
public:
    AttachmentStore(std::filesystem::path root, RecordStore& records);
    AttachmentRecord add_text(const std::string& owner_id,
                              const std::string& project_id,
                              const std::string& filename,
                              const std::string& content);
    std::optional<AttachmentRecord> find_for_owner(
        const std::string& id, const std::string& owner_id) const;
    std::string read_text_for_owner(const std::string& id,
                                    const std::string& owner_id) const;

private:
    void restore();
    std::filesystem::path root_;
    RecordStore& records_;
    std::map<std::string, AttachmentRecord> attachments_;
    // Guards attachments_ against concurrent add/find/read calls.
    mutable std::mutex mutex_;
};

class TranscriptionAdapter {
public:
    virtual ~TranscriptionAdapter() = default;
    virtual bool available() const noexcept = 0;
    virtual std::string transcribe(const std::filesystem::path& audio_file,
                                   const std::atomic_bool& cancellation) = 0;
};

enum class DownloadState {
    queued,
    transferring,
    paused,
    verifying,
    complete,
    failed,
    quarantined,
    cancelled
};

struct DownloadRequest {
    std::string source_url;
    std::string immutable_revision;
    std::string expected_sha256;
    std::filesystem::path destination;
    bool license_accepted{false};
    std::uint64_t minimum_ram_mib{0};
    std::uint64_t recommended_ram_mib{0};
};

class DownloadJob final {
public:
    explicit DownloadJob(DownloadRequest request);
    DownloadJob(std::string id, DownloadRequest request, DownloadState state,
                std::uint64_t completed_bytes, std::string diagnostic);
    const std::string& id() const noexcept;
    const DownloadRequest& request() const noexcept;
    DownloadState state() const noexcept;
    std::uint64_t completed_bytes() const noexcept;
    const std::string& diagnostic() const noexcept;
    void begin(std::uint64_t existing_bytes);
    void record_progress(std::uint64_t completed_bytes);
    void pause();
    void begin_verification();
    void complete(const std::string& actual_sha256);
    void fail(std::string diagnostic = {});
    void cancel();

private:
    std::string id_;
    DownloadRequest request_;
    DownloadState state_{DownloadState::queued};
    std::uint64_t completed_bytes_{0};
    std::string diagnostic_;
};

// Thrown by DownloadManager::run() when a second run() call arrives for a
// job id that already has a transfer in flight, so callers (the HTTP layer)
// can report 409 Conflict instead of racing two curl invocations against the
// same .part file.
class DownloadAlreadyRunning final : public std::invalid_argument {
public:
    explicit DownloadAlreadyRunning(const std::string& id)
        : std::invalid_argument("download already running: " + id) {}
};

class DownloadManager final {
public:
    DownloadManager(std::filesystem::path approved_curl,
                    std::filesystem::path download_root,
                    RecordStore& records);
    DownloadJob create(const DownloadRequest& request);
    DownloadJob run(const std::string& id, const std::atomic_bool& cancellation);
    std::optional<DownloadJob> find(const std::string& id) const;
    std::vector<DownloadJob> list() const;
    // Pauses an in-flight transfer (must currently be transferring): signals
    // the running run() call to stop the transport and leave the job in the
    // resumable 'paused' state rather than 'cancelled'.
    void pause(const std::string& id);
    // Stops a download. If it is currently in flight, signals the running
    // run() call to terminate the transport and land in 'cancelled'
    // (non-resumable); otherwise marks it cancelled immediately.
    void cancel(const std::string& id);
    // Signals every currently in-flight transfer to stop, without waiting for
    // them to land -- used during process shutdown so an active download
    // does not keep the server alive past its graceful-stop window.
    void cancel_all() noexcept;
    // Discards a finished/paused/queued job's record and any leftover
    // partial or quarantined artifact. Rejected while the job is in flight --
    // pause() or cancel() it first.
    void remove(const std::string& id);

private:
    void restore();
    void persist(const DownloadJob& job);
    std::filesystem::path approved_curl_;
    std::filesystem::path download_root_;
    RecordStore& records_;
    std::map<std::string, DownloadJob> jobs_;
    // Guards jobs_ and in_flight_. Never held across the (potentially
    // multi-minute) curl transfer itself in run() -- only around the brief
    // admission check, each progress-tick persist, and final state
    // transition -- so polling/list/cancel from other request threads is
    // never blocked by an active download.
    mutable std::mutex mutex_;
    // Job ids with a run() currently executing, so a second run() for the
    // same id is rejected instead of racing the first over the same
    // destination/.part file.
    std::set<std::string> in_flight_;
    // Cross-request signal for each in-flight run(): a separate HTTP request
    // (pause/cancel/shutdown) sets these flags to interrupt the transfer
    // thread, which is otherwise blocked inside run() for the whole transfer
    // and cannot observe anything but the atomic_bool its own caller passed
    // in.
    struct RunSignal {
        std::atomic_bool cancellation{false};
        std::atomic_bool pause_requested{false};
    };
    std::map<std::string, std::shared_ptr<RunSignal>> signals_;
};

enum class BenchmarkProfile { quick, standard, extended };

struct BenchmarkRecord {
    std::string model_id;
    std::string backend_version;
    std::string build_id;
    std::string hardware_id;
    std::string prompt_suite_hash;
    BenchmarkProfile profile{BenchmarkProfile::quick};
    std::uint64_t prompt_tokens{0};
    std::uint64_t generated_tokens{0};
    std::uint64_t elapsed_microseconds{0};
    std::uint64_t peak_resident_memory_bytes{0};
    std::uint64_t passed_cases{0};
    std::uint64_t total_cases{1};
    std::string settings_json{"{}"};
};

class BenchmarkStore final {
public:
    BenchmarkStore() = default;
    explicit BenchmarkStore(RecordStore& records);
    void add(const BenchmarkRecord& record);
    std::vector<BenchmarkRecord> comparable(const std::string& hardware_id,
                                            const std::string& prompt_suite_hash,
                                            BenchmarkProfile profile) const;
    std::optional<BenchmarkRecord> recommend(
        const std::string& hardware_id,
        const std::string& prompt_suite_hash,
        BenchmarkProfile profile) const;
    std::vector<BenchmarkRecord> all() const;

private:
    void restore();
    RecordStore* record_store_{nullptr};
    std::vector<BenchmarkRecord> records_;
};

class BenchmarkRunner final {
public:
    BenchmarkRunner(RunnerSupervisor& inference, BenchmarkStore& store);
    BenchmarkRecord run(const std::string& model_id,
                        const std::string& backend_version,
                        const std::string& build_id,
                        const std::string& hardware_id,
                        BenchmarkProfile profile,
                        const std::atomic_bool& cancellation);

private:
    RunnerSupervisor& inference_;
    BenchmarkStore& store_;
};

struct PerformanceSample {
    std::string name;
    std::uint64_t operations{0};
    std::uint64_t input_bytes{0};
    std::uint64_t elapsed_nanoseconds{0};
    double nanoseconds_per_operation{0.0};
    double mebibytes_per_second{0.0};
};

struct PerformanceReport {
    std::vector<PerformanceSample> samples;
    std::uint64_t checksum{0};
};

// Measures canonical control-plane serialization without changing its policy.
PerformanceReport run_control_plane_performance_probe(
    std::uint64_t iterations);

enum class QueryStage {
    admission,
    authentication,
    normalization,
    classification,
    retrieval_planning,
    retrieval,
    ranking,
    prompt_assembly,
    runner_queue,
    prompt_evaluation,
    generation,
    persistence,
    release
};

enum class QueryStatus {
    accepted,
    retrieving,
    queued,
    evaluating_prompt,
    generating,
    completed,
    cancelled,
    failed
};

struct QueryStageMetric {
    QueryStage stage{QueryStage::admission};
    std::uint64_t started_microseconds{0};
    std::uint64_t elapsed_microseconds{0};
    bool cancellation_observed{false};
};

struct QueryTrace {
    std::string id;
    std::string user_id;
    std::string project_id;
    std::string model_id;
    QueryStatus status{QueryStatus::accepted};
    std::vector<QueryStageMetric> stages;
    std::uint64_t queue_wait_microseconds{0};
    std::uint64_t time_to_first_token_microseconds{0};
    std::uint64_t prompt_tokens{0};
    std::uint64_t generated_tokens{0};
    double prompt_tokens_per_second{0.0};
    double generation_tokens_per_second{0.0};
    ProcessResourceSample control_plane_start;
    ProcessResourceSample control_plane_peak;
    std::uint64_t runner_peak_resident_memory_bytes{0};
    bool partial_retrieval{false};
    std::string diagnostic;
    // Phase 16: raw JSON array of RetrievalOutcome disclosure entries
    // (which chunks/files, index generation, ranking reason, and whether
    // each was included or omitted by the context budget). Empty when
    // retrieval never ran for this query.
    std::string retrieval_disclosure;
};

// Owns bounded, monotonic query traces. Callers explicitly transition through
// the real pipeline; no elapsed time or progress state is synthesized.
class QueryCoordinator final {
public:
    explicit QueryCoordinator(std::size_t maximum_retained_traces = 256U);
    ~QueryCoordinator();
    QueryCoordinator(const QueryCoordinator&) = delete;
    QueryCoordinator& operator=(const QueryCoordinator&) = delete;

    std::string begin(const std::string& user_id,
                      const std::string& project_id,
                      const std::string& model_id);
    void transition(const std::string& id, QueryStage stage,
                    QueryStatus status);
    void observe_resources(const std::string& id,
                           std::uint64_t runner_resident_memory_bytes = 0U);
    void record_inference(const std::string& id,
                          const GenerationResult& result,
                          std::uint64_t time_to_first_token_microseconds);
    // Phase 16: records the retrieval disclosure separately from the
    // terminal diagnostic set by finish(), so a later successful finish()
    // does not erase what was actually retrieved.
    void record_retrieval(const std::string& id, bool partial,
                          std::string disclosure_json);
    void finish(const std::string& id, QueryStatus status,
                std::string diagnostic = {});
    std::optional<QueryTrace> find(const std::string& id) const;
    std::vector<QueryTrace> list() const;
    static std::string to_json(const QueryTrace& trace);

private:
    class State;
    std::unique_ptr<State> state_;
};

struct QueryBaseline {
    std::string host_hash;
    std::string model_hash;
    std::string backend_hash;
    std::string build_hash;
    std::string settings_hash;
    std::string prompt_suite_hash;
    bool cold{false};
    QueryTrace trace;
    std::uint64_t instrumentation_overhead_nanoseconds{0};
};

QueryBaseline make_query_baseline(
    const HardwareInfo& hardware, std::string model_hash,
    std::string backend_hash, std::string build_hash,
    std::string settings_hash, std::string prompt_suite_hash, bool cold,
    const QueryTrace& trace, std::uint64_t instrumentation_iterations = 1000U);
std::string query_baseline_json(const QueryBaseline& baseline);
std::string hardware_info_json(const HardwareInfo& hardware);

enum class MemoryCategory {
    control_plane,
    runner_weights,
    compute_buffers,
    kv_cache,
    prompt_cache,
    retrieval_index_cache,
    file_content,
    attachments,
    downloads,
    background_jobs
};

enum class MemoryPressure { normal, elevated, high, critical };
enum class ResourceProfile { minimal, balanced, performance };

struct MemoryPolicy {
    std::uint64_t hard_limit_bytes{0};
    std::uint64_t minimum_os_reserve_bytes{2ULL * 1024ULL * 1024ULL * 1024ULL};
    unsigned int minimum_free_percent{15U};
    unsigned int elevated_percent{70U};
    unsigned int high_percent{82U};
    unsigned int critical_percent{92U};
    std::uint32_t maximum_active_inference{1U};
    std::uint32_t maximum_queued_inference{8U};
    std::uint32_t maximum_index_workers{2U};
    std::uint32_t default_context_tokens{4096U};
    bool keep_idle_model{true};
    bool pause_background_during_inference{true};
};

struct MemoryEstimate {
    std::uint64_t weights_bytes{0};
    std::uint64_t runtime_buffer_bytes{0};
    std::uint64_t kv_bytes_per_sequence{0};
    std::uint32_t sequences{1U};
    std::uint64_t transient_bytes{0};
    std::uint64_t safety_margin_bytes{0};

    std::uint64_t total_bytes() const noexcept;
};

struct MemoryAdmission {
    bool admitted{false};
    std::string lease_id;
    std::uint64_t reserved_bytes{0};
    std::string diagnostic;
    std::vector<std::string> corrective_actions;
};

struct MemoryStatus {
    MemoryPressure pressure{MemoryPressure::normal};
    std::uint64_t hard_limit_bytes{0};
    std::uint64_t reserved_bytes{0};
    std::uint64_t observed_process_bytes{0};
    std::uint64_t available_physical_bytes{0};
    std::map<MemoryCategory, std::uint64_t> category_bytes;
    std::vector<std::string> active_pressure_actions;
};

// Sole process-wide reservation authority. Every accepted reservation is
// bounded by both the configured ceiling and live OS safety reserve.
class MemoryBudgetManager final {
public:
    MemoryBudgetManager(MemoryPolicy policy, HardwareInfo hardware);
    ~MemoryBudgetManager();
    MemoryBudgetManager(const MemoryBudgetManager&) = delete;
    MemoryBudgetManager& operator=(const MemoryBudgetManager&) = delete;

    MemoryAdmission reserve(MemoryCategory category,
                            const MemoryEstimate& estimate,
                            bool interactive);
    void release(const std::string& lease_id) noexcept;
    MemoryStatus sample();
    MemoryStatus status() const;
    bool permits_background_work() const;
    static MemoryPolicy policy_for(ResourceProfile profile,
                                   const HardwareInfo& hardware,
                                   std::uint64_t hard_limit_bytes = 0U);
    static std::string to_json(const MemoryStatus& status);

private:
    class State;
    std::unique_ptr<State> state_;
};

class BoundedWorkQueue final {
public:
    BoundedWorkQueue(std::size_t maximum_items,
                     std::uint64_t maximum_payload_bytes);
    bool enqueue(const std::string& id, std::uint64_t payload_bytes,
                 bool interactive);
    std::optional<std::string> begin_next();
    void complete(const std::string& id) noexcept;
    void cancel_all() noexcept;
    std::size_t queued() const;
    std::size_t active() const;
    std::uint64_t payload_bytes() const;

private:
    struct Item {
        std::string id;
        std::uint64_t bytes{0};
        bool interactive{false};
    };
    std::size_t maximum_items_;
    std::uint64_t maximum_payload_bytes_;
    mutable std::mutex mutex_;
    std::vector<Item> queued_;
    std::map<std::string, std::uint64_t> active_;
    std::uint64_t payload_bytes_{0};
};

struct IndexChunk {
    std::string id;
    std::string relative_path;
    std::string language;
    std::uint64_t offset{0};
    std::string text;
    std::string digest;
};

struct IndexStatus {
    std::string project_id;
    std::uint64_t generation{0};
    std::uint64_t files_discovered{0};
    std::uint64_t files_indexed{0};
    std::uint64_t files_unchanged{0};
    std::uint64_t chunks{0};
    std::uint64_t disk_bytes{0};
    bool partial{false};
    bool cancelled{false};
    std::string diagnostic;
};

class ProjectIndexer final {
public:
    ProjectIndexer(ProjectRecord project, std::filesystem::path index_root,
                   MemoryBudgetManager& memory,
                   std::uint64_t maximum_file_bytes = 4U * 1024U * 1024U,
                   std::uint64_t maximum_index_bytes =
                       1024ULL * 1024ULL * 1024ULL);
    ~ProjectIndexer();
    ProjectIndexer(const ProjectIndexer&) = delete;
    ProjectIndexer& operator=(const ProjectIndexer&) = delete;

    IndexStatus rebuild(const std::atomic_bool& cancellation,
                        std::size_t partial_publish_files = 128U);
    IndexStatus update(const std::vector<std::filesystem::path>& changed_paths,
                       const std::atomic_bool& cancellation);
    std::vector<IndexChunk> search_text(const std::string& literal,
                                        std::size_t maximum_results) const;
    std::vector<IndexChunk> search_symbol(
        const std::string& symbol, std::size_t maximum_results) const;
    IndexStatus status() const;

private:
    class State;
    std::unique_ptr<State> state_;
};

enum class IndexJobState { absent, queued, running, ready, cancelled, failed };

enum class IndexTrigger {
    manual,
    save,
    watcher,
    branch_switch,
    periodic
};

struct IndexServiceStatus {
    IndexStatus index;
    IndexJobState state{IndexJobState::absent};
    IndexTrigger trigger{IndexTrigger::manual};
    std::size_t queue_position{0U};
};

// Phase 16: bundles a live search result with the disk generation it was
// read from, so callers can disclose exactly which index snapshot the
// evidence came from and detect a not-yet-indexed project.
struct IndexSearchResult {
    std::vector<IndexChunk> chunks;
    std::uint64_t generation{0};
    bool available{false};
};

class ProjectIndexService final {
public:
    ProjectIndexService(std::filesystem::path index_root,
                        MemoryBudgetManager& memory,
                        std::size_t maximum_queued_projects = 16U);
    ~ProjectIndexService();
    ProjectIndexService(const ProjectIndexService&) = delete;
    ProjectIndexService& operator=(const ProjectIndexService&) = delete;

    bool request_rebuild(const ProjectRecord& project);
    bool request_update(const ProjectRecord& project,
                        const std::vector<std::filesystem::path>& changed_paths,
                        IndexTrigger trigger);
    bool cancel(const std::string& project_id);
    std::optional<IndexServiceStatus> status(
        const std::string& project_id) const;
    // Phase 16: read-only literal/symbol lookups against whatever index
    // generation is currently published for the project. `available` is
    // false, with an empty chunk list, when no generation has ever
    // published for this project yet.
    IndexSearchResult search_text(const std::string& project_id,
                                  const std::string& literal,
                                  std::size_t maximum_results) const;
    IndexSearchResult search_symbol(const std::string& project_id,
                                    const std::string& symbol,
                                    std::size_t maximum_results) const;

private:
    class State;
    std::unique_ptr<State> state_;
};

// Native live adapter for Phase 15: watches every ProjectCatalog project's
// root directory (Windows `ReadDirectoryChangesW`, Linux `inotify`, both
// recursive and bounded) and forwards observed changes into
// ProjectIndexService::request_update without any external editor, IDE
// plugin, or version-control hook. A change to a project's `.git/HEAD` is
// reported as IndexTrigger::branch_switch (forcing a full rescan); every
// other observed file change is reported as IndexTrigger::watcher with the
// specific changed paths. Owns one background thread; construction starts
// watching immediately and the destructor stops and joins it.
class ProjectWatcher final {
public:
    ProjectWatcher(ProjectCatalog& projects, ProjectIndexService& indexes);
    ~ProjectWatcher();
    ProjectWatcher(const ProjectWatcher&) = delete;
    ProjectWatcher& operator=(const ProjectWatcher&) = delete;

private:
    class State;
    std::unique_ptr<State> state_;
};

// Phase 16: deadline-bound hybrid retrieval over Phase 15 project indexes.
struct RetrievalRequest {
    ProjectRecord project;
    std::string query_text;
    std::chrono::milliseconds deadline{1500};
    std::uint64_t maximum_context_bytes{16U * 1024U};
    std::uint64_t maximum_chunks_per_source{6U};
    std::uint64_t maximum_total_chunks{20U};
};

// One disclosed piece of evidence: which file/offset it came from, which
// index generation supplied it, why it ranked, and whether the context
// budget ultimately included or omitted it.
struct RetrievalDisclosureEntry {
    std::string source;
    std::string relative_path;
    std::uint64_t offset{0};
    std::uint64_t index_generation{0};
    double score{0.0};
    bool included{false};
    std::string reason;
};

struct RetrievalCandidate {
    IndexChunk chunk;
    RetrievalDisclosureEntry disclosure;
};

struct RetrievalOutcome {
    std::string context_text;
    std::vector<RetrievalDisclosureEntry> disclosure;
    std::string strategy;
    bool partial{false};
    std::string diagnostic;
};

// Chooses the least expensive sufficient index-backed strategy (exact
// symbol match, then exact literal text, then per-token lexical union),
// running independent strategy steps across bounded parallel workers with a
// hard wall-clock deadline. On expiry it stops launching further steps and
// returns whatever evidence bounded workers already produced rather than
// blocking; it never performs a security or membership decision itself --
// callers must have already authorized the caller against `project` before
// calling retrieve(). Semantic/embedding, dependency-neighbour, and MCP
// resource strategies remain forward work (no embedding adapter or MCP
// resource plumbing is wired to retrieval yet); this planner covers every
// strategy that Phase 15's disk-backed index can actually serve today.
class RetrievalPlanner final {
public:
    explicit RetrievalPlanner(ProjectIndexService& indexes);
    RetrievalOutcome retrieve(const RetrievalRequest& request) const;

private:
    ProjectIndexService& indexes_;
};

// Applies per-source and total chunk/byte caps to already-ranked candidates.
// Candidates must arrive sorted by descending score; the budgeter accepts
// the highest-ranked evidence first and never truncates a chunk's text to
// fit more chunks in.
class ContextBudgeter final {
public:
    static RetrievalOutcome apply(std::vector<RetrievalCandidate> ranked,
                                  const std::string& strategy, bool partial,
                                  std::string diagnostic,
                                  std::uint64_t maximum_context_bytes,
                                  std::uint64_t maximum_chunks_per_source,
                                  std::uint64_t maximum_total_chunks);
};

struct McpInboundTool {
    std::string name;
    std::string required_scope;
};

enum class McpTransport { stdio_transport, streamable_http, legacy_sse };

struct McpOutboundServer {
    std::string id;
    McpTransport transport{McpTransport::stdio_transport};
    std::filesystem::path executable;
    std::string executable_sha256;
    std::vector<std::string> arguments;
    std::filesystem::path working_directory;
    std::string endpoint;
    std::set<std::string> allowed_tools;
    std::set<std::string> allowed_project_ids;
    std::string credential_secret_name;
    bool enabled{false};
    std::uint64_t timeout_seconds{30};
    std::uint64_t maximum_output_bytes{1024U * 1024U};
    std::uint64_t memory_limit_mib{512};
};

class McpPolicy final {
public:
    void register_inbound_tool(const McpInboundTool& tool);
    bool authorize_inbound(const std::string& tool,
                           const std::set<std::string>& client_scopes,
                           bool project_authorized) const;
    void register_outbound_server(const McpOutboundServer& server);
    bool authorize_outbound(const std::string& server_id,
                            const std::string& tool,
                            const std::set<std::string>& user_scopes,
                            bool project_authorized,
                            bool user_approved) const;

private:
    std::map<std::string, McpInboundTool> inbound_tools_;
    std::map<std::string, McpOutboundServer> outbound_servers_;
};

// Persists the outbound server registry independently from inbound MCP client
// identities. Registration canonicalizes executables, pins their digest, and
// validates transport, allow-list, timeout, and project boundaries.
class McpOutboundRegistry final {
public:
    explicit McpOutboundRegistry(RecordStore& records);
    McpOutboundServer register_server(McpOutboundServer server);
    void remove(const std::string& server_id);
    std::optional<McpOutboundServer> find(const std::string& server_id) const;
    std::vector<McpOutboundServer> list() const;

private:
    void restore();
    void persist(const McpOutboundServer& server);
    RecordStore& records_;
    std::map<std::string, McpOutboundServer> servers_;
    // Guards servers_ against concurrent register/remove/find/list calls.
    mutable std::mutex mutex_;
};

struct McpOutboundCall {
    std::string server_id;
    std::string tool;
    std::string arguments_json{"{}"};
    std::string actor_id;
    std::set<std::string> actor_scopes;
    std::set<std::string> actor_project_ids;
    std::string project_id;
    bool user_approved{false};
};

struct McpOutboundResult {
    bool succeeded{false};
    bool cancelled{false};
    std::string response_json;
    std::string diagnostic;
};

// Executes an authorized outbound tool call through an isolated stdio process
// or bounded Streamable HTTP connection and appends a sanitized audit result.
class McpOutboundGateway final {
public:
    McpOutboundGateway(McpOutboundRegistry& registry, SecretStore& secrets,
                       AuditLog& audit);
    McpOutboundResult invoke(const McpOutboundCall& call,
                             std::atomic_bool& cancellation) const;

private:
    McpOutboundRegistry& registry_;
    SecretStore& secrets_;
    AuditLog& audit_;
};

// Carries the authenticated client authority into the MCP protocol layer.
// Project identifiers are explicit token bindings; an empty set grants no
// project filesystem access even when the token has a projects.read scope.
struct McpIdentity {
    std::string user_id;
    std::set<std::string> scopes;
    std::set<std::string> project_ids;
};

// Implements the pinned MCP server contract independently of its transport.
// Both stdio and Streamable HTTP pass bounded JSON-RPC messages through this
// class so capability, authorization, resource, and tool behavior cannot drift.
class McpInboundServer final {
public:
    McpInboundServer(ProjectCatalog& projects,
                     std::filesystem::path models_root,
                     std::uint64_t memory_reserve_mib);

    std::string handle(const std::string& request_json,
                       const McpIdentity& identity,
                       std::atomic_bool& cancellation) const;

private:
    ProjectCatalog& projects_;
    std::filesystem::path models_root_;
    std::uint64_t memory_reserve_mib_{0};
};

const char* mcp_protocol_version() noexcept;

// Runs newline-delimited UTF-8 JSON-RPC over caller-supplied streams. The
// function writes protocol data only to output and keeps diagnostics on error,
// matching the MCP stdio transport separation required by IDE hosts.
int run_mcp_stdio(McpInboundServer& server,
                  const McpIdentity& identity,
                  std::istream& input,
                  std::ostream& output,
                  std::ostream& error,
                  std::atomic_bool& cancellation);

enum class IdeClientKind { vscode, visual_studio };

// Supplies backend-neutral IDE capabilities through the same project catalogue
// and HTTP/MCP boundaries used by all other clients.
class IdeIntegrationService final {
public:
    explicit IdeIntegrationService(ProjectCatalog& projects);
    std::string capabilities_json() const;
    std::string diagnostics_json(
        const std::string& project_id, const std::string& relative_path,
        const std::set<std::string>& authorized_project_ids) const;
    std::string diff_preview_json(
        const std::string& project_id, const std::string& unified_diff,
        const std::set<std::string>& authorized_project_ids) const;

private:
    ProjectCatalog& projects_;
};

std::string ide_client_name(IdeClientKind client);
IdeClientKind parse_ide_client(const std::string& value);
std::string ide_secret_name(IdeClientKind client);
std::string ide_connection_profile_json(
    IdeClientKind client, const std::filesystem::path& masterai_executable,
    const std::filesystem::path& settings, const AppConfig& configuration);
std::string read_hidden_console_line(const std::string& prompt);
ApiTokenStore::Token store_ide_token(
    IdeClientKind client, std::string token, ApiTokenStore& tokens,
    UserStore& users, SecretStore& secrets, AuditLog& audit,
    std::uint64_t now_epoch_seconds);
void remove_ide_token(IdeClientKind client, SecretStore& secrets,
                      AuditLog& audit, const std::string& actor_id);

struct BackupReport {
    std::filesystem::path backup_path;
    std::uint64_t file_count{0};
    std::uint64_t total_bytes{0};
    std::string manifest_sha256;
};

struct RestoreReport {
    std::filesystem::path runtime_root;
    std::filesystem::path settings_path;
    std::uint64_t file_count{0};
    std::uint64_t total_bytes{0};
};

struct UpgradeReport {
    std::filesystem::path receipt_path;
    std::string previous_sha256;
    std::string installed_sha256;
};

struct RecoveryReport {
    bool stale_stop_request_removed{false};
    bool stale_checkpoint_removed{false};
    bool record_store_validated{false};
};

class OperationsManager final {
public:
    explicit OperationsManager(std::filesystem::path runtime_root);

    // Creates a verified, host-portable data backup without OS-bound secrets.
    BackupReport create_backup(const std::filesystem::path& settings_path,
                               const std::filesystem::path& backup_root) const;

    // Restores a verified backup only into clean settings/runtime destinations.
    static RestoreReport restore_backup(
        const std::filesystem::path& backup_path,
        const std::filesystem::path& settings_destination,
        const std::filesystem::path& runtime_destination);

    // Replaces one named non-password secret in native OS-protected storage.
    std::string rotate_secret(const std::string& name,
                              const std::string& actor_id) const;

    // Rotates bounded operational logs while preserving the audit chain.
    std::uint64_t rotate_logs(std::uint64_t maximum_bytes,
                              std::uint64_t retained_files,
                              const std::string& actor_id) const;

    // Installs an offline candidate and writes a hash-bound rollback receipt.
    UpgradeReport install_upgrade(
        const std::filesystem::path& active_executable,
        const std::filesystem::path& candidate_executable,
        const std::filesystem::path& rollback_root,
        const std::string& actor_id) const;

    // Restores the receipt-bound executable only if the active hash is intact.
    UpgradeReport rollback_upgrade(
        const std::filesystem::path& active_executable,
        const std::filesystem::path& receipt_path,
        const std::string& actor_id) const;

    // Clears known crash residue and validates/checkpoints the existing store.
    RecoveryReport recover(const std::string& actor_id) const;

private:
    std::filesystem::path runtime_root_;
};

class HttpServer final {
public:
    HttpServer(std::string host, std::uint16_t port);
    explicit HttpServer(AppConfig configuration);
    ~HttpServer();
    HttpServer(const HttpServer&) = delete;
    HttpServer& operator=(const HttpServer&) = delete;

    bool run(std::atomic_bool& stop_requested);
    void stop() noexcept;

private:
    std::string host_;
    std::uint16_t port_;
    std::intptr_t socket_{-1};
    AppConfig configuration_;
    class State;
    std::unique_ptr<State> state_;
    // Thread-per-connection bookkeeping: one worker thread is spawned per
    // accepted client (capped by configuration_.max_concurrent_connections)
    // so a slow request (e.g. a model download) never blocks any other
    // connection. active_connections_ is checked/incremented before a
    // worker is spawned and decremented by the worker itself on exit; the
    // "finished" flag lets run()'s accept loop reap completed threads
    // without blocking, since std::thread has no non-blocking join check.
    std::atomic<std::uint32_t> active_connections_{0};
    std::mutex workers_mutex_;
    std::vector<std::pair<std::thread, std::shared_ptr<std::atomic_bool>>> workers_;
};

}  // namespace masterai
