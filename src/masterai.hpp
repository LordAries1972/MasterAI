// MasterAI public core contract.
//
// This unit declares the independently authored C++17 control-plane services
// shared by the CLI, HTTP server, MCP transports, operational tooling, and
// validation tests. Implementations remain split by responsibility so callers
// depend on stable interfaces rather than platform-specific details.
#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <iosfwd>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <new>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace masterai {

// Forward-declared rather than including json.hpp here: only
// json_records_to_csv (ml_engine.cpp) takes a JsonValue by reference, and a
// declaration needs no more than that -- every caller that actually
// constructs a std::vector<JsonValue> already includes json.hpp itself.
class JsonValue;
// Forward-declared for the same reason as JsonValue above: mcp_resource_search()
// (declared near RetrievalStrategy below, defined in mcp_retrieval.cpp) only
// needs these two by reference; their full definitions live later in this
// same header, alongside the rest of the outbound-MCP client types.
class McpOutboundRegistry;
class McpOutboundGateway;
// Forward-declared for semantic_embedding_search() (declared near
// RetrievalStrategy below, defined in semantic_retrieval.cpp); full
// definition is later in this header, alongside the rest of the cache types.
class CacheManager;
// Forward-declared so LaunchTuning (below) can carry a KvPrecision field;
// full definition is later in this header, alongside the rest of the
// Phase 27 KV-cache types. Scoped enums default to an `int` underlying type
// even when forward-declared without one, so this is well-formed and must
// stay consistent with the (also implicit-int) definition further down.
enum class KvPrecision;

enum class LogLevel { debug, info, warning, error };

void log(LogLevel level, const std::string& event, const std::string& detail);
bool constant_time_equal(const std::string& left, const std::string& right) noexcept;
std::vector<std::uint8_t> secure_random(std::size_t size);
std::string sha256_hex(const std::string& value);
// Decodes standard base64 (RFC 4648, '+'/'/', '=' padding required). Throws
// std::invalid_argument on malformed input.
std::string base64_decode(const std::string& text);
// progress, when set, is called after every chunk read with
// (bytes_hashed_so_far, total_file_bytes) so a caller hashing a large file
// can report live progress instead of blocking silently until it finishes.
std::string sha256_file_hex(
    const std::filesystem::path& path,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress = {});
// PBKDF2-HMAC-SHA256 password hashing for locally stored accounts (never
// used for OS-mapped accounts, which are always verified by LogonUserW/PAM).
// Both functions consume and zero their `password` argument.
std::string hash_password(std::string password);
bool verify_password(std::string password, const std::string& encoded_hash);

// Phase 33 (LOCAL-ONLY slice, 2026-08-13): one entry in an opt-in local
// multi-runner pool -- a per-GPU runner, a CPU+GPU split, or a dedicated
// embedding/router/benchmark runner, all running concurrently alongside (or
// instead of most requests going to) the single default `inference`
// RunnerSupervisor every pre-Phase-33 build already has. An empty
// AppConfig::local_runner_pool (the default) means exactly one control-plane
// runner, completely unchanged -- this struct only ever describes
// *additional* runners an administrator has explicitly configured.
//
// Scope note: this is deliberately the LOCAL-ONLY half of docs/PLAN.md
// Phase 33. Every runner this struct can describe is a local child process
// this control plane itself spawns via RunnerSupervisor/LlamaCppAdapter, on
// the same trust footing as the pre-existing single-runner path. The
// optional intranet-worker half of Phase 33 (mutual TLS, pinned/approved
// private PKI, signed worker registration, model-digest verification,
// encrypted transport, per-project authorization over a network boundary)
// is NOT implemented by this struct or by LocalRunnerPool below, and
// remains Planned/deferred -- see the Phase 33 section of docs/PLAN.md.
struct LocalRunnerConfig {
    // Stable identity used in routing decisions, status output, and the
    // Phase 13 query-trace runner-identity field (QueryTrace::runner_id).
    std::string id;
    // Loopback IPC port this runner's llama.cpp backend listens on. Must be
    // distinct from AppConfig::runner_port and from every other configured
    // runner's port -- validated in ConfigurationManager::validate() the
    // same way runner_port is already validated against the HTTP port.
    std::uint16_t port{0U};
    std::string accelerator_policy{"auto"};
    // What kinds of request this runner is willing to serve, e.g.
    // {"generation"}, {"embedding"}, {"router"}, {"benchmark"}. Empty means
    // "generalist" -- accepts any capability, matching the single-runner
    // default's own unrestricted behavior.
    std::set<std::string> capabilities;
    // Phase 2 project-bound authorization, reused (not reinvented) here:
    // empty means no project restriction (the same open-by-default posture
    // the single existing runner already has); non-empty restricts this
    // runner to only ever serving the listed project ids, enforced by
    // LocalRunnerPool::select_runner() as a hard filter, never a soft
    // preference.
    std::set<std::string> authorized_project_ids;
    // Tie-break weight when multiple runners are otherwise equally
    // suitable; higher serves first.
    unsigned int priority{0U};
};

// Phase 33 (LOCAL-ONLY slice): every signal LocalRunnerPool::select_runner()
// weighs when choosing which local runner process should serve one request.
// Mirrors the "declare and disclose every signal, even ones a given decision
// rule does not yet act on" discipline RoutingSignals (Phase 29,
// model_routing.cpp) already established for model-*tier* selection -- this
// is the equivalent declaration for runner-*process* selection, a distinct
// decision that reuses the same disclosure shape rather than overloading
// RoutingSignals with a second, unrelated meaning. Fields noted "disclosed
// only" below are accepted and recorded but have no real probe behind them
// yet in this pass, the same honesty convention already used elsewhere in
// this project (e.g. Phase 78's documented cache-hit-rate gap).
struct RunnerSelectionSignals {
    std::string model_id;             // resident-model match signal
    std::string required_capability;  // e.g. "generation", "embedding"
    std::string project_id;           // Phase 2 project-bound authorization
    unsigned int priority{0U};
    // Disclosed only: no per-runner thermal/power probe exists in this
    // pass (Phase 30/30A hardware probing is control-plane-wide, not
    // per-child-process), so this is never populated by a real caller yet;
    // kept here so the routing rule already accepts it once one exists.
    std::optional<double> thermal_headroom_percent;
};

// Phase 33 (INTRANET-WORKER slice, 2026-08-13): this control plane's own
// private worker PKI. One CA keypair signs every worker's client
// certificate; certificate_file is the pinned public CA cert this control
// plane verifies every worker's presented certificate chain against
// (never the OS/system trust store -- a compromised public CA must never
// be able to mint a certificate this control plane accepts), and
// private_key_file is the CA's own signing key, which never leaves this
// machine and is never transmitted over the wire. See
// issue_worker_certificate() (src/intranet_worker.cpp) and the
// WorkerListener/IntranetWorkerPool class comments below for how both
// directions of the mutual handshake consume this.
struct PrivateCertificateAuthority {
    std::filesystem::path certificate_file;
    std::filesystem::path private_key_file;
};

// Result of signing one worker's certificate against the private CA
// (POST /api/v1/system/pki/workers, administrator-only). certificate_pem/
// private_key_pem must be copied to the physical worker machine out of
// band (an administrator-controlled file transfer this codebase does not
// automate); this control plane itself retains only sha256_fingerprint,
// which an operator then adds to an IntranetWorkerConfig's
// expected_server_certificate_sha256 (if that worker is also being
// configured as a routing target here) or to a WorkerModeConfig's
// approved_client_certificate_sha256 (if this machine is the one being
// configured to receive worker requests) -- never the private key itself.
struct IssuedWorkerCertificate {
    std::string certificate_pem;
    std::string private_key_pem;
    std::string sha256_fingerprint;  // of the DER-encoded certificate
};

// Phase 33 (INTRANET-WORKER slice): one administrator-approved remote
// worker machine this control plane may route generation/embedding
// requests to over a mutually authenticated, encrypted channel. See the
// class comment on IntranetWorkerPool for the full trust model. Unlike
// LocalRunnerConfig, a remote worker's model is fixed by that worker's own
// local configuration and only ever reported back (never remotely
// selected) by this control plane -- there is no "load this model on that
// machine" RPC, closing off an entire class of remote resource-exhaustion
// risk such a call would otherwise open.
struct IntranetWorkerConfig {
    std::string id;
    std::string host;  // intranet address; never loopback (that is local_runner_pool's job)
    std::uint16_t port{0U};
    // SHA-256 of the worker's DER-encoded leaf certificate, pinned at
    // configuration time and checked in addition to (not instead of) the
    // normal CA-chain verification -- the same defense-in-depth
    // digest-pinning convention Phase 9's outbound MCP executable pinning
    // already established (see mcp_outbound.cpp's executable_sha256 check).
    std::string expected_server_certificate_sha256;
    std::set<std::string> capabilities;
    std::set<std::string> authorized_project_ids;
    unsigned int priority{0U};
};

// Phase 33 (INTRANET-WORKER slice): opt-in configuration for running this
// same masterai executable as a worker -- i.e. accepting authenticated
// requests from another control plane's IntranetWorkerPool, rather than
// only ever serving the loopback administrator UI/API the way every other
// deployment of this binary does. Disabled (enabled=false) by default:
// every existing single-machine deployment is completely unaffected. When
// enabled, this is the ONE deliberate, narrowly-scoped exception to
// HttpServer's loopback-only constraint (see HttpServer::HttpServer's own
// "127.0.0.1" check in server.cpp) -- WorkerListener speaks only the small
// mutually authenticated worker protocol declared below, never the full
// administrator HTTP/API surface, and refuses any connection whose client
// certificate does not verify against ca_certificate_file AND match an
// entry in approved_client_certificate_sha256.
struct WorkerModeConfig {
    bool enabled{false};
    std::string bind_host;  // e.g. a specific intranet interface address
    std::uint16_t port{0U};
    std::filesystem::path ca_certificate_file;
    std::filesystem::path server_certificate_file;
    std::filesystem::path server_private_key_file;
    std::set<std::string> approved_client_certificate_sha256;
};

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
    // Phase 30A: strict accelerator policy. "auto" keeps existing VRAM-based
    // select_gpu_layers() behavior; "cpu_only" forces zero GPU layers, CPU
    // KV placement, and a GPU-hiding runner process environment (see
    // LaunchSpec::environment / build_launch_spec()); "gpu_allowed" is the
    // same as "auto" today, kept distinct so a future stricter "always
    // offload" policy has a name that does not collide with "auto".
    std::string accelerator_policy{"auto"};
    std::filesystem::path llama_server_executable;
    std::filesystem::path curl_executable;
    // Phase 73: optional, administrator-vendored llama.cpp LoRA tooling
    // (same manual-placement convention as llama_server_executable above --
    // see README.md's "Where to get llama-server" section for the download
    // instructions this mirrors). Empty disables real LLM LoRA fine-tuning;
    // a FineTuningJob whose method names an LLM target then fails clearly
    // with "llama-finetune/llama-export-lora not configured" rather than
    // silently falling back to the tabular path.
    std::filesystem::path llama_finetune_executable;
    std::filesystem::path llama_export_lora_executable;
    // Phase: process-isolated DuckDB CLI backend (rule 15's second named
    // exception) used solely to convert Parquet knowledge-document uploads
    // to text before chunking (parquet_bridge.cpp). Empty disables Parquet
    // ingestion; other supported knowledge media types are unaffected.
    std::filesystem::path parquet_helper_executable;
    // Replaces the formerly hardcoded 2 MiB constant in ml_knowledge.cpp so
    // operators can raise the knowledge-document size ceiling without a
    // rebuild. Applies to every supported knowledge media type, not just
    // Parquet.
    std::uint64_t knowledge_maximum_document_bytes{25ULL * 1024ULL * 1024ULL};
    // Machine Learning Settings (docs/PLAN.md "Machine Learning Abilities"
    // section 49): replaces the previously hardcoded 8 MiB constant in
    // ml_engine.cpp's parse_tabular_csv(), the same way
    // knowledge_maximum_document_bytes above replaced its own hardcoded cap.
    std::uint64_t tabular_dataset_maximum_csv_bytes{8ULL * 1024ULL * 1024ULL};
    // "PageFile" setting: an administrator-chosen substitute location for
    // MasterAI's own disk-backed cache/scratch area (see
    // resolve_page_file_root()). Empty means "use the existing default"
    // (runtime_root/"cache"), not the real Windows pagefile.
    std::filesystem::path page_file_root;
    // Phase 31: ScratchVolumeManager configuration. scratch_root defaults to
    // runtime_root/"scratch" (resolved the same relative-to-settings-file
    // way as runtime_root/models_root -- see resolve_scratch_root()) rather
    // than reusing page_file_root/CacheManager's disk area, since scratch
    // jobs and the durable disk cache have different lifetime and quota
    // semantics (scratch is always per-job and always torn down; the cache
    // is a standing, key-indexed store). scratch_global_quota_mib bounds
    // total scratch usage across every concurrent job; scratch_free_space_
    // reserve_mib is the free-space floor ScratchVolumeManager::reserve()/
    // begin_job() refuse to go below (the concrete mechanism behind the
    // "scratch files cannot fill the system drive" Phase 31 exit
    // criterion).
    std::filesystem::path scratch_root;
    std::uint64_t scratch_global_quota_mib{4096ULL};
    std::uint64_t scratch_free_space_reserve_mib{1024ULL};
    std::uint16_t runner_port{7081};
    // Previously hardcoded to 30 seconds at the RunnerSupervisor::load() call
    // site, which was too short for large models (e.g. DeepSeek-class) to
    // finish cold-loading before readiness was declared timed out.
    std::uint32_t runner_startup_timeout_seconds{120U};
    // Watchdog for RunnerSupervisor::generate(): how long to wait between
    // bytes from the runner during a live generation before treating it as
    // hung instead of blocking forever. Resets on every byte received, so a
    // slow-but-streaming generation is never cut off.
    std::uint32_t runner_stall_timeout_seconds{120U};
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
    // Phase 24: per-strategy opt-outs for the three expensive/IO-bound
    // adapters (a network round trip, an external MCP call, and a
    // sandboxed subprocess respectively). The cheap heuristic strategies
    // (call_graph, type_reference, dependency_neighbour, filename_path,
    // recent_change) have no toggle, matching how filename_path/
    // recent_change already had none before this pass.
    bool retrieval_semantic_embedding_enabled{true};
    bool retrieval_git_diff_enabled{true};
    bool retrieval_mcp_resource_enabled{true};
    // Phase 17: security-partitioned cache hierarchy. Disabling it leaves
    // every request path exactly as it behaves without Phase 17 (always a
    // fresh RetrievalPlanner run, no reuse).
    bool cache_enabled{true};
    std::uint64_t cache_maximum_bytes_per_category{64ULL * 1024ULL * 1024ULL};
    // Chat reply length policy: the previous unconfigurable
    // GenerationOptions::max_tokens default (512) cut replies off well
    // before the model's own end-of-turn token instead of letting them run
    // until EOS/stop-sequence, which is what "max_tokens" is meant to be --
    // a safety ceiling, not a target length. Bounded by the runner's own
    // hard policy ceiling (32768, RunnerSupervisor::generate()).
    // 2026-08-18: both defaults raised well above their original
    // introductory values (8192/4096) -- a 4096-token context budget is
    // exhausted almost immediately once a chat carries any real history,
    // an attached document, or a tool result (Phase 84), forcing the
    // prompt-fit clamp at send_chat_message's assemble step to shrink
    // max_tokens far below what a real multi-step task needs. 32768/16384
    // gives a large working budget out of the box while staying well under
    // both hard policy ceilings above (1,048,576 / 32,768) and remaining
    // adjustable per-install via Settings -> Inference in the web UI
    // (POST /api/v1/admin/config) for hardware that can go higher, or
    // needs to go lower.
    std::uint32_t chat_max_reply_tokens{16384U};
    std::uint32_t chat_context_length{32768U};
    // Phase 18: runner prompt-prefix / KV-session reuse. Off by default --
    // see docs/PLAN.md Phase 18 -- until a same-host repeated-turn benchmark
    // records the exit-criterion evidence, matching how other real-model
    // exit criteria in this project (Phases 4-7) remain outstanding until a
    // pinned backend/model is exercised.
    bool session_reuse_enabled{false};
    unsigned int session_reuse_max_slots{4U};
    std::uint32_t session_reuse_idle_retention_seconds{300U};
    // Phase 19: whether POST /api/v1/performance/calibrate is permitted to
    // run at all. The named profile calibration runs against is the
    // existing memory.profile (resource_profile above, minimal/balanced/
    // performance) plus an "auto" option accepted only at calibrate-request
    // time -- Phase 19 deliberately does not add a second, competing
    // persisted profile selector.
    bool performance_auto_tune{true};
    // Phase 33 (LOCAL-ONLY slice): opt-in local multi-runner pool -- see
    // LocalRunnerConfig's class comment. Empty (the default) means exactly
    // one control-plane runner, identical to every pre-Phase-33 build;
    // single-runner mode is never required to change when this stays empty.
    std::vector<LocalRunnerConfig> local_runner_pool;
    // Phase 33 (INTRANET-WORKER slice, 2026-08-13): opt-in remote worker
    // routing -- see IntranetWorkerConfig's class comment. Empty (the
    // default) means no remote worker is ever consulted; every existing
    // deployment is unaffected. Requires private_ca and
    // intranet_worker_client_certificate_file/_key_file to be set
    // (ConfigurationManager::validate() enforces this) and this build to
    // have been compiled with OpenSSL available.
    std::vector<IntranetWorkerConfig> intranet_worker_pool;
    PrivateCertificateAuthority private_ca;
    std::filesystem::path intranet_worker_client_certificate_file;
    std::filesystem::path intranet_worker_client_private_key_file;
    // Phase 33 (INTRANET-WORKER slice): opt-in worker-mode listener -- see
    // WorkerModeConfig's class comment. Disabled by default; every existing
    // deployment stays loopback-only and unaffected.
    WorkerModeConfig worker_mode;
    // Phase 28: opt-in NUMA-local thread placement -- see
    // TopologyAffinityPolicy's comment and recommend_thread_placement().
    // Off by default, matching the plan's "applied only where measurement
    // shows benefit -- not pinned by default": an administrator enables this
    // only after Phase 36 benchmark-matrix evidence justifies it on their
    // host, and even then recommend_thread_placement() still no-ops on any
    // single-NUMA-node host. Also gated at the call site by the Phase 20
    // "numa_affinity" AdvancedOptimizationRegistry admission, so both an
    // explicit evidence-backed admission and this configuration flag are
    // required before any thread affinity is actually pinned.
    bool numa_local_placement_enabled{false};
    // Phase 29: administrator-declared tier -> registered-model-id
    // membership (keys are ModelTier's to_string() spelling: e.g.
    // "small_fast", "medium_general"). Empty (the default) means no tier is
    // configured, so ModelRouter has nothing to select from and the
    // advisory routing/cascade-escalation surface stays fully inert --
    // every existing deployment that never sets this is unaffected.
    std::map<std::string, std::vector<std::string>> model_tier_assignments;
    // Phase 29: top-level kill switch, independent of model_tier_assignments
    // above -- lets an administrator declare tiers in advance without
    // turning live chat routing on for users yet. send_chat_message() only
    // ever consults ModelRouter when this is true AND at least one tier is
    // assigned; false (the default) leaves every chat's explicit model_id
    // selection completely unaffected, even if tiers are configured.
    bool model_routing_enabled{false};
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

// Resolves the effective directory for MasterAI's "PageFile" disk-backed
// cache/scratch area: `configuration.page_file_root` if set, otherwise the
// existing default of `runtime_root/"cache"`. Centralised here so every
// caller (CacheManager construction, the System Report) agrees.
std::filesystem::path resolve_page_file_root(const AppConfig& configuration);

// Phase 31: resolves the effective ScratchVolumeManager root the same way
// resolve_page_file_root() resolves the cache root above: an explicit
// configuration.scratch_root if set, otherwise runtime_root/"scratch".
std::filesystem::path resolve_scratch_root(const AppConfig& configuration);

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

// GPU vendor telemetry (utilization percent, temperature) for the Phase 19
// calibration gap PLAN.md records as forward work: no vendor SDK was an
// approved dependency until now. NVIDIA is read through NVML and AMD through
// ADLX; both vendor libraries are loaded dynamically at runtime (NVML via the
// driver-installed nvml.dll, ADLX via its own internal driver-DLL loader in
// the vendored ADLXHelper), never linked, so a host without that vendor's
// driver present fails closed to `available == false` instead of crashing or
// failing to start. Only vendored on Windows today per the project's
// Windows-first build scope; probe_gpu_vendor_telemetry() always returns
// `available == false` elsewhere.
struct GpuVendorTelemetry {
    bool available{false};
    std::string vendor;      // "nvidia" or "amd" when available
    std::string device_name;
    unsigned int utilization_percent{0};
    int temperature_celsius{0};
};

std::vector<GpuVendorTelemetry> probe_gpu_vendor_telemetry();

// Phase 28: topology probing plus a pure, testable thread-placement
// recommendation function. Deliberately NOT wired into any real worker
// thread's startup in this pass -- the plan requires affinity be "applied
// only where measurement shows benefit -- not pinned by default", and this
// codebase has no per-host benefit measurement (that is Phase 36's
// benchmark matrix, explicitly listed as this phase's second dependency
// and still Planned). What ships working and tested this pass: real
// topology probing (packages/NUMA nodes/processor groups/hybrid core
// counts), the recommendation decision function callers can use once
// measurement exists, and a real (but unused-by-default) Windows
// NUMA-affinity primitive for a future caller to invoke. Every host with a
// single NUMA node and no efficiency/performance core split gets an
// all-disabled recommendation regardless of policy, so normal OS
// scheduling is always the fallback on non-applicable hardware.
struct ProcessorGroupInfo {
    unsigned int group_id{0};
    unsigned int processor_count{0};
};

struct NumaNodeInfo {
    unsigned int numa_node_id{0};
    unsigned int logical_processor_count{0};
};

struct HardwareTopology {
    unsigned int package_count{1U};
    unsigned int numa_node_count{1U};
    unsigned int physical_core_count{0};
    unsigned int logical_core_count{0};
    // 0 when the host is not a detected hybrid (performance/efficiency
    // split) design -- Windows exposes this via
    // RelationProcessorCore::EfficiencyClass; no such distinction exists on
    // homogeneous hosts, so both stay 0 there rather than guessing.
    unsigned int performance_core_count{0};
    unsigned int efficiency_core_count{0};
    std::vector<ProcessorGroupInfo> processor_groups;
    std::vector<NumaNodeInfo> numa_nodes;
    bool multi_node{false};
    bool hybrid_cores{false};
};

HardwareTopology probe_hardware_topology();

enum class ThreadClass {
    inference_compute,
    http_streaming,
    retrieval,
    indexing,
    storage_completion,
    download,
    background_maintenance
};

struct TopologyAffinityPolicy {
    bool numa_local_placement_enabled{false};
    bool hybrid_core_policy_enabled{false};
};

struct ThreadPlacementRecommendation {
    std::optional<unsigned int> preferred_numa_node;
    bool prefer_efficiency_core{false};
    std::string reason;
};

ThreadPlacementRecommendation recommend_thread_placement(
    const HardwareTopology& topology, ThreadClass klass,
    const TopologyAffinityPolicy& policy, bool on_battery_power);

// Pins the CALLING thread's affinity mask to `numa_node_id`'s logical
// processors. Real on Windows (GetNumaNodeProcessorMaskEx +
// SetThreadAffinityMask); a documented no-op returning false everywhere
// else and whenever the node id is out of range, never a silent partial
// pin. Not invoked by any code path in this pass -- see the scope note
// above.
bool apply_current_thread_to_numa_node(unsigned int numa_node_id);

// Real on Windows (GetSystemPowerStatus's ACLineStatus == 0); a documented
// false everywhere else and whenever the OS call itself fails, matching
// apply_current_thread_to_numa_node()'s "no silent guess" convention. Feeds
// both AdaptiveSignalSnapshot::on_battery_power (Phase 34) and the
// TopologyAffinityPolicy on_battery_power argument (Phase 28).
bool probe_on_battery_power();

std::string to_string(ThreadClass klass);
std::string hardware_topology_json(const HardwareTopology& topology);

struct ProcessResourceSample {
    std::uint64_t resident_memory_bytes{0};
    std::uint64_t private_memory_bytes{0};
    std::uint64_t commit_bytes{0};
    std::uint64_t page_faults{0};
};

ProcessResourceSample probe_process_resources();

// Drops this whole process's own scheduling priority one notch below normal
// (Windows: BELOW_NORMAL_PRIORITY_CLASS; Linux: nice(5)) so every thread it
// ever spawns -- the HTTP request handlers, project indexing, semantic
// retrieval scoring, calibration benchmarking, all of it -- inherits that
// same background-friendly base priority automatically, rather than each
// worker having to remember to lower itself individually. This is the
// process-wide sibling of the per-child-process priority already applied to
// the model runner (inference.cpp) and sandboxed tool calls (tool_exec.cpp):
// together they mean nothing MasterAI does, in-process or out-of-process,
// competes with the rest of the OS on equal footing. Best-effort and safe to
// call more than once; a failure here just leaves the process at its
// inherited default priority. Called once from the `serve` entry point, not
// from short-lived CLI subcommands the operator is actively waiting on.
void lower_process_priority_for_background_work();

// Trims this process's own working set back to the OS and returns the
// resident-memory delta freed (0 if the platform has no such primitive, or
// the call did not reduce resident memory). This is the one memory-reclaim
// action MasterAI can actually perform on the wider system's behalf: it
// cannot force another process or the OS itself to release memory, only
// shrink its own footprint, which is what increases OS-visible free RAM.
std::uint64_t release_process_working_set();

// System-wide Windows memory-manager primitives for the Model Inventory
// page's "Memory Status" widget (docs/PLAN.md Phase 35 extension note).
// Every one of these genuinely requires the MasterAI process itself to be
// running elevated (Administrator) -- Windows only grants the underlying
// privilege (SeProfileSingleProcessPrivilege, SeIncreaseQuotaPrivilege, or
// SeDebugPrivilege) to an elevated token. Each fails closed (returns
// false/0) rather than crashing or silently pretending to have worked when
// the calling process isn't elevated, or on any non-Windows platform.

// Enables `privilege_name` (e.g. "SeProfileSingleProcessPrivilege") in this
// process's own token if the account holds it but it isn't active yet.
// Returns whether the privilege is enabled after the call -- the honest
// yes/no every function below checks before attempting its real operation.
bool acquire_privilege_if_available(const char* privilege_name);

// NtSetSystemInformation(SystemMemoryListInformation, MemoryFlushModifiedList)
// -- the same primitive Sysinternals RAMMap's "Flush" uses.
bool flush_modified_page_list();

// NtSetSystemInformation(SystemMemoryListInformation, MemoryPurgeStandbyList).
bool purge_standby_list();

// NtSetSystemInformation(SystemMemoryListInformation,
// MemoryPurgeLowPriorityStandbyList) -- purges only the lowest-priority
// standby pages, leaving higher-priority cached pages (recently used)
// alone.
bool purge_low_priority_standby_list();

// Trims the working set (EmptyWorkingSet) of every other running process
// this account has permission to open, skipping any whose working set is
// below `minimum_mib` (0 = no threshold) and, when
// `protect_foreground_process` is set, skipping whatever process currently
// owns the foreground window so the operator's active application doesn't
// visibly stutter. Returns the number of processes successfully trimmed.
std::uint32_t trim_other_process_working_sets(
    std::uint64_t minimum_mib, bool protect_foreground_process);

// Like trim_other_process_working_sets(), but with no size threshold and
// requesting SeDebugPrivilege first so SYSTEM/service processes this
// account would not otherwise be able to open are included too. Returns
// the number of processes successfully trimmed.
std::uint32_t empty_system_and_service_working_sets();

// Forces the Windows system file cache down via the documented
// SetSystemFileCacheSize() shrink-then-restore technique (the same one
// Microsoft's own CacheSet/Clearmem tools use): briefly caps the cache's
// working set to a small ceiling, then immediately restores the default
// (unrestricted) limit.
bool clear_system_file_cache();

// Phase 19: a short blocking system-wide CPU-utilization and process-scoped
// disk-byte sample used only for calibration evidence, never for admission
// decisions. disk_read_bytes/disk_write_bytes are this process's own
// cumulative I/O (simpler and sufficient for calibration, which measures the
// runner it just launched, not whole-system I/O). Blocks for approximately
// interval_milliseconds while it takes two spaced readings.
struct SystemUtilizationSample {
    double cpu_percent{0.0};
    std::uint64_t disk_read_bytes{0};
    std::uint64_t disk_write_bytes{0};
    // Phase 36 benchmark-gap pass: this process's own cumulative read/write
    // I/O *operation* counts (IO_COUNTERS::ReadOperationCount/
    // WriteOperationCount on Windows, read_bytes/write_bytes' operation-
    // count-shaped analogue isn't exposed by /proc/self/io on Linux, so
    // these stay 0 there) -- distinct from disk_read_bytes/disk_write_bytes
    // above, which count bytes, not operations. Read from the exact same
    // IO_COUNTERS sample already being taken for the byte counters, so this
    // is free reuse, not a new native call.
    std::uint64_t disk_read_operations{0};
    std::uint64_t disk_write_operations{0};
};

SystemUtilizationSample probe_system_utilization(
    std::uint32_t interval_milliseconds = 100U);

// Phase 21: native asynchronous storage and prefetch engine.
//
// Scope note: this is a deliberately scoped-down implementation of
// docs/PLAN.md's Phase 21 deliverable (see the approved implementation plan
// for the full rationale). It provides a real Windows IOCP/overlapped
// backend and a real bounded pread worker-pool backend (the plan's
// explicitly-allowed fallback, used unconditionally on POSIX), a read
// coalescer, an adaptive queue-depth policy, and per-request cancellation.
// It does not implement Linux io_uring or memory-mapped file regions --
// those remain follow-on work, honestly out of scope for this pass.

// Measured storage latency classification. Extends platform.cpp's
// probe_storage_class() (device-type only, e.g. "fixed"/"removable"/
// "network") with a timed random-read sample so callers can tell a slow
// spinning disk apart from a fast NVMe SSD even though the OS reports both
// as "fixed".
struct StorageLatencyProfile {
    std::string storage_class{"unknown"};   // from probe_storage_class()
    double measured_read_latency_us{0.0};   // mean 4KB random-read latency
    bool sequential{true};                  // true => caller should not fan out reads
};

// Times a small number of 4KB random reads against a scratch file created
// under `scratch_directory` (removed again afterward) to produce a
// StorageLatencyProfile for `storage_class`. Never throws: any probing
// failure (directory not writable, etc.) yields a conservative
// sequential-only profile so callers gating fan-out on `sequential` fail
// safe toward the pre-Phase-21 serialized behavior rather than guessing.
StorageLatencyProfile probe_storage_latency(
    const std::filesystem::path& scratch_directory,
    const std::string& storage_class);

// Adaptive queue-depth policy keyed to a measured storage profile: how many
// concurrent reads an async reader backend may have in flight at once.
// HDD-shaped latency and non-local media (network/removable/optical) stay
// serialized (depth 1) so fan-out cannot thrash a disk head or saturate a
// slow link; fast local media gets a small bounded parallel depth. This is
// intentionally conservative rather than throughput-maximizing.
std::size_t adaptive_queue_depth(const StorageLatencyProfile& profile);

// ---------------------------------------------------------------------
// Phase 31: storage tiering, virtual drives, and scratch-volume management.
// ---------------------------------------------------------------------
//
// Storage tiers exist so scratch/cache/model-placement decisions are made
// from measured device behavior instead of an assumed "the disk is fast"
// default. classify_storage_tier() below reuses probe_storage_class()'s
// device-type signal and probe_storage_latency()'s measured 4KB random-read
// latency -- both already gathered by Phase 21 -- so this phase adds no new
// probing surface for the disk-tier part of the classification. Tier R (RAM-
// backed) is the odd one out: it is not a disk tier at all, and
// durable_data_class_allows_ram_tier() below is the hard prohibition the
// plan requires against ever placing durable data there.
enum class StorageTier {
    fast_local,           // Tier A: NVMe-shaped local fixed storage
    local_ssd,             // Tier B: SATA-SSD-shaped local fixed storage
    local_hdd,             // Tier C: HDD-shaped local fixed storage
    removable_or_network,  // Tier D: removable/network media, import-export only by default
    ram_backed,             // Tier R: RAM disk -- reconstructable ephemeral artifacts only
};

std::string to_string(StorageTier tier);

// Classifies a measured StorageLatencyProfile (device type + timed random-
// read latency) into one of the five Phase 31 tiers. A pure function of the
// evidence passed in -- never re-probes anything itself -- so it stays
// trivially testable with synthetic profiles, matching select_load_mode()'s
// existing shape in calibration.cpp.
StorageTier classify_storage_tier(const StorageLatencyProfile& profile);

// Categories of data a caller might ask ScratchVolumeManager to place.
// `reconstructable_scratch` is the only category eligible for Tier R
// (RAM-backed) placement; every other value names one of the durable-data
// kinds docs/PLAN.md Phase 31 explicitly forbids from ever landing on
// RAM-backed storage.
enum class DurableDataClass {
    reconstructable_scratch,
    gguf_model,
    durable_chat,
    audit_record,
    user_database,
    resumable_download,
    backup,
    security_record,
    index_generation_sole_copy,
};

std::string to_string(DurableDataClass data_class);

// The hard prohibition itself: only reconstructable_scratch may ever be
// placed on Tier R. Every ScratchVolumeManager entry point that accepts a
// DurableDataClass calls this and refuses (throws) rather than silently
// downgrading the request to a different tier -- see ScratchVolumeManager's
// class comment below.
bool durable_data_class_allows_ram_tier(DurableDataClass data_class) noexcept;

// Best-effort filesystem introspection for a path under active model/index
// storage. `detection_available` is false whenever the host platform or
// filesystem could not be queried at all, so callers can tell "measured:
// not present" apart from "could not measure" -- every other field defaults
// to the conservative "not detected" value in that case.
struct FilesystemIntegrityFlags {
    bool detection_available{false};
    bool compressed{false};
    bool encrypted{false};
    bool deduplicated{false};
    bool virtual_disk{false};
    bool network_redirected{false};
    std::string volume_filesystem;  // e.g. "NTFS", "ReFS" -- empty if undetected
};

// Windows: GetVolumeInformationW (filesystem name), FSCTL_GET_COMPRESSION
// (actual per-file/directory compression state, not just volume capability),
// FILE_ATTRIBUTE_ENCRYPTED (actual EFS encryption state), GetDriveTypeW
// (network/removable), and IOCTL_STORAGE_QUERY_PROPERTY's reported bus type
// (BusTypeVirtual/BusTypeFileBackedVirtual, e.g. a mounted VHD/VHDX) for
// virtual-disk detection. Deduplication is reported best-effort from an
// IO_REPARSE_TAG_DEDUP reparse point when `path` names a regular file --
// this project has no dependency on the (admin-only) Data Deduplication
// PowerShell/WMI surface needed for a true volume-level check, so a
// directory path leaves `deduplicated` at its conservative default rather
// than guessing. Non-Windows: detection_available stays false and every
// flag stays at its conservative default, per this project's Windows-first
// build scope (see src/platform.cpp's existing #ifdef convention).
FilesystemIntegrityFlags probe_filesystem_integrity_flags(
    const std::filesystem::path& path);

// Separate, non-conflated memory accounting (docs/PLAN.md Phase 31
// deliverable): physical/available RAM and committed/commit-limit/pagefile
// figures are reported as distinct fields rather than folded into one
// "memory used" number. `hard_page_fault_rate_per_second` is a real measured
// rate (two spaced samples, mirroring probe_system_utilization()'s existing
// pattern) rather than a cumulative counter a caller would have to diff
// itself -- see probe_memory_accounting()'s definition for the honest scope
// note on what "hard" means here (Windows' GetProcessMemoryInfo does not
// itself separate hard/disk-backed faults from soft ones; the reported rate
// is the conservative superset, used the same directional way).
// `model_mapped_bytes`/`model_resident_bytes` are caller-supplied so a model
// admission decision can compare projected resident pages against real
// physical headroom (see projected_resident_exceeds_safe_physical_capacity()
// below) without this struct needing any runner-internal knowledge itself.
struct MemoryAccountingSnapshot {
    std::uint64_t physical_total_bytes{0};
    std::uint64_t physical_available_bytes{0};
    std::uint64_t committed_bytes{0};       // this process's own commit (private bytes)
    std::uint64_t commit_limit_bytes{0};    // system-wide commit limit (RAM + pagefile)
    std::uint64_t pagefile_used_bytes{0};   // system-wide commit charge beyond physical RAM
    double hard_page_fault_rate_per_second{0.0};
    std::uint64_t model_mapped_bytes{0};    // caller-supplied: model weights mapped (virtual)
    std::uint64_t model_resident_bytes{0};  // caller-supplied: model weights actually resident
};

// Samples committed/commit-limit/pagefile and page-fault-rate evidence over
// `interval_milliseconds` (blocks, like probe_system_utilization()).
MemoryAccountingSnapshot probe_memory_accounting(
    std::uint32_t interval_milliseconds = 100U,
    std::uint64_t model_mapped_bytes = 0U,
    std::uint64_t model_resident_bytes = 0U);

// The Phase 31 exit criterion "a model is rejected or downgraded when
// projected active pages exceed safe physical capacity even if commit
// capacity remains": true only when projected_resident_bytes plus the
// configured OS reserve would exceed physically available RAM, regardless
// of how much commit/pagefile headroom the same snapshot reports separately
// -- mirrors MemoryBudgetManager::reserve()'s existing "OS reserve" idea
// (memory.cpp) but reasons about resident pages specifically, not the
// broader reservation ledger.
bool projected_resident_exceeds_safe_physical_capacity(
    const MemoryAccountingSnapshot& snapshot,
    std::uint64_t projected_resident_bytes,
    std::uint64_t os_reserve_bytes) noexcept;

// A storage-placement recommendation for active model/index storage, derived
// from measured evidence (StorageLatencyProfile + FilesystemIntegrityFlags),
// never from assumption -- the Phase 31 exit criterion "model placement
// recommendations reflect measured storage, not assumption".
struct StoragePlacementRecommendation {
    StorageTier recommended_tier{StorageTier::local_hdd};
    bool acceptable_for_active_model_storage{true};
    std::vector<std::string> concerns;  // human-readable measured concerns
};

StoragePlacementRecommendation recommend_storage_placement(
    const StorageLatencyProfile& latency,
    const FilesystemIntegrityFlags& filesystem);

// Bounded, quota-enforced, crash-recoverable scratch storage for ephemeral
// per-job working directories (runner scratch, in-flight index builds,
// download staging, etc.). ScratchVolumeManager is the single place that
// creates/tracks/reclaims this ephemeral storage so:
//   - scratch usage is bounded (per-job and global byte quotas) and can
//     never fill the system drive (a free-space reserve is checked before
//     every admission -- the concrete mechanism behind the "scratch files
//     cannot fill the system drive" exit criterion);
//   - a crash mid-job leaves a durable journal entry so the *next* startup
//     can find and delete the orphaned directory instead of it silently
//     accumulating forever (recover_orphans());
//   - durable data is never silently placed on Tier R (RAM-backed) storage
//     -- every entry point that accepts a DurableDataClass calls
//     durable_data_class_allows_ram_tier() and refuses outright (throws)
//     rather than downgrading the request to a different tier the caller
//     didn't ask for.
// Structurally a sibling of CalibrationService/AdvancedOptimizationRegistry
// (calibration.cpp/optimization_registry.cpp): an in-process service with
// its own small durable journal. It deliberately does not depend on
// RecordStore -- scratch state is itself ephemeral and must be recoverable
// (via its own journal) independently of whether the main record store even
// opened successfully.
class ScratchVolumeManager final {
public:
    // `root`: directory scratch jobs are created under (created if absent).
    // `global_quota_bytes`: hard ceiling across every job's directory at
    // once; begin_job()/reserve() refuse once reserved bytes would exceed
    // it. `free_space_reserve_bytes`: reserve() and begin_job() both refuse
    // whenever consuming the requested bytes would leave the underlying
    // volume's free space below this threshold. `preferred_tier`: the tier
    // `root` is asserted to sit on for reporting/policy purposes; this is
    // *not* re-measured here -- callers pass classify_storage_tier()'s
    // result for the volume `root` actually lives on.
    ScratchVolumeManager(std::filesystem::path root,
                        std::uint64_t global_quota_bytes,
                        std::uint64_t free_space_reserve_bytes,
                        StorageTier preferred_tier = StorageTier::fast_local);
    ~ScratchVolumeManager();
    ScratchVolumeManager(const ScratchVolumeManager&) = delete;
    ScratchVolumeManager& operator=(const ScratchVolumeManager&) = delete;

    // Deletes every job directory the crash-recovery journal still lists as
    // open (no matching "end" record) from a previous process lifetime.
    // Call once at startup, before any begin_job(). Returns the number of
    // orphaned directories removed. Never throws: a single unreadable/
    // unremovable orphan is skipped, not fatal to startup.
    std::size_t recover_orphans();

    // Creates (and journals) a new per-job scratch directory under `root`
    // with its own byte quota. Throws std::invalid_argument if `data_class`
    // is not reconstructable_scratch while `preferred_tier` is
    // StorageTier::ram_backed (the hard RAM-tier prohibition), if the job id
    // is invalid or already active, if the per-job quota would push global
    // usage over global_quota_bytes, or if the volume's current free space
    // is already below free_space_reserve_bytes.
    std::filesystem::path begin_job(
        const std::string& job_id, std::uint64_t quota_bytes,
        DurableDataClass data_class = DurableDataClass::reconstructable_scratch);

    // Admits `additional_bytes` more against `job_id`'s own quota, the
    // manager's global quota, and the free-space reserve. Returns false
    // (never throws) on refusal so hot write paths can treat it as ordinary
    // backpressure rather than an exceptional condition.
    bool reserve(const std::string& job_id, std::uint64_t additional_bytes);

    // Atomic publication: copies `scratch_source` (which must be inside
    // job_id's own scratch directory) to a `.tmp` sibling of
    // `durable_destination`, flushes it, then atomically renames it over
    // `durable_destination` -- so `durable_destination` is either fully
    // absent/unchanged or fully written, never partially observable.
    // Refuses (throws) if `durable_destination` resolves inside this
    // manager's own scratch root unless `data_class` is
    // reconstructable_scratch, which is the concrete mechanism behind the
    // "durable data is never silently redirected to ephemeral storage" exit
    // criterion.
    std::filesystem::path publish(const std::string& job_id,
                                  const std::filesystem::path& scratch_source,
                                  const std::filesystem::path& durable_destination,
                                  DurableDataClass data_class);

    // Deletes job_id's scratch directory, releases its quota, and journals
    // the matching "end" record so recover_orphans() never has to clean it
    // up on a future startup. Safe to call more than once; a second call
    // for an already-ended (or never-begun) job is a no-op.
    void end_job(const std::string& job_id) noexcept;

    // Ends every still-open job and removes their directories. Called from
    // the server's own shutdown path so a clean shutdown never leaves
    // scratch behind for recover_orphans() to find on the next startup.
    void shutdown_cleanup() noexcept;

    std::vector<std::string> active_job_ids() const;
    std::uint64_t global_quota_bytes() const noexcept;
    std::uint64_t global_reserved_bytes() const;
    std::uint64_t free_space_reserve_bytes() const noexcept;
    StorageTier preferred_tier() const noexcept;
    const std::filesystem::path& root() const noexcept;

private:
    struct JobState {
        std::filesystem::path directory;
        std::uint64_t quota_bytes{0};
        std::uint64_t reserved_bytes{0};
    };
    void append_journal(const std::string& operation, const std::string& job_id,
                        const std::filesystem::path& directory);
    bool free_space_available(std::uint64_t additional_bytes) const;

    std::filesystem::path root_;
    std::uint64_t global_quota_bytes_;
    std::uint64_t free_space_reserve_bytes_;
    StorageTier preferred_tier_;
    mutable std::mutex mutex_;
    std::map<std::string, JobState> jobs_;
    std::uint64_t global_reserved_bytes_{0};
};

std::string scratch_volume_manager_status_json(const ScratchVolumeManager& manager);

// Phase 31 (Priority B): tier-migration tooling for data that is already
// durably placed. ScratchVolumeManager::publish() above handles the
// scratch-to-durable placement decision made at creation time; this handles
// the follow-up case the plan calls out explicitly -- an administrator
// moving an already-placed file (e.g. a GGUF model) from one storage tier to
// another after the fact.
struct StorageMigrationResult {
    std::filesystem::path destination_path;
    std::uint64_t bytes_migrated{0};
    std::string sha256_hex;
};

// Closes the "no central manifest" gap the earlier Phase 31 pass left open:
// a single durable, journaled record of every migration migrate_durable_file()
// performs, so a record store holding a path captured before a migration
// (a Model Registry entry's model directory, an index generation's storage
// path, ...) can still find the file's current location without every store
// independently tracking its own migration history. Persisted through the
// same RecordStore/journal mechanism every other durable store in this
// codebase uses -- see DurableFileManifest's .cpp for the packed record
// format.
class DurableFileManifest final {
public:
    struct Entry {
        std::string source_path;
        std::string destination_path;
        std::string sha256_hex;
        std::string data_class;
        std::uint64_t migrated_at_epoch_seconds{0};
    };

    DurableFileManifest() = default;
    explicit DurableFileManifest(RecordStore& records);

    // Records that `source_path` was migrated to `destination_path`. Any
    // existing entry whose current resolved location equals `source_path`
    // is chained forward to `destination_path` too, so a file migrated more
    // than once still resolves correctly from every path it has ever lived
    // at.
    void record(const std::filesystem::path& source_path,
                const std::filesystem::path& destination_path,
                const std::string& sha256_hex, DurableDataClass data_class);

    // Returns `original_path` unchanged if it still exists on disk (the
    // common case -- nothing was ever migrated) or has no recorded
    // migration; otherwise follows the migration chain (bounded to guard
    // against a corrupted manifest) and returns the final, current location.
    std::filesystem::path resolve(
        const std::filesystem::path& original_path) const;

    // Every recorded migration, for administrator visibility
    // (GET /api/v1/system/storage/manifest).
    std::vector<Entry> entries() const;

private:
    RecordStore* records_{nullptr};
    // canonical(as-migrated-from path) -> canonical(current path). Rebuilt
    // from records_ at construction time.
    std::map<std::string, std::string> current_location_;
    mutable std::mutex mutex_;
};

std::string durable_file_manifest_json(const DurableFileManifest& manifest);

// Copies `source_path` into `destination_directory` (created if absent),
// verifies a SHA-256 digest match between source and staged copy, atomically
// renames the verified copy into place (same stage-then-rename pattern as
// ScratchVolumeManager::publish()), and only then removes the original.
// Throws std::invalid_argument for an unreadable source, a destination that
// resolves to the same file, or a destination directory that measures as
// Tier R (RAM-backed) storage while `data_class` is not
// reconstructable_scratch (the same hard RAM-tier prohibition
// ScratchVolumeManager enforces, applied here too since a migration is
// exactly the kind of operation that could otherwise "silently downgrade" a
// durable file onto ephemeral storage); throws std::runtime_error if the
// staging copy or checksum verification fails. In every throwing case the
// original file at `source_path` is left completely untouched. On success,
// records the move in `manifest` before returning.
StorageMigrationResult migrate_durable_file(
    const std::filesystem::path& source_path,
    const std::filesystem::path& destination_directory,
    DurableDataClass data_class, DurableFileManifest& manifest);

// Installs the process-wide manifest resolve_durable_path() consults. Called
// once at startup (server.cpp) after the manifest itself is constructed;
// every launch-path/index-path reader that might hold a since-migrated path
// goes through resolve_durable_path() rather than threading a
// DurableFileManifest reference through every call site between server.cpp
// and the point of use.
void install_global_durable_file_manifest(DurableFileManifest& manifest);

// Resolves a possibly-stale durable path to its current location. Returns
// `original_path` unchanged when no manifest is installed (e.g. in unit
// tests that construct components directly), when the path still exists on
// disk, or when it was never migrated.
std::filesystem::path resolve_durable_path(
    const std::filesystem::path& original_path);

// Result of one async read. `buffer` is only meaningful when `succeeded` is
// true -- a cancelled or failed request always leaves it empty so no
// partial or unverified data is ever observable by a caller.
struct AsyncReadResult {
    std::string buffer;
    bool succeeded{false};
    bool cancelled{false};
    std::string diagnostic;
};

// Per-request cancellation token. cancel() is safe to call from any thread,
// any number of times, before or after the read completes. A request
// cancelled before or during flight releases its buffer and reports
// cancelled=true instead of publishing partial bytes -- see AsyncReadResult.
class AsyncReadCancellationToken final {
public:
    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool cancelled_{false};
};

// Replaceable backend interface. One instance owns a bounded worker/
// completion-port resource and may serve many read_range() calls, up to its
// configured queue depth, from possibly-concurrent caller threads.
class IAsyncFileReader {
public:
    virtual ~IAsyncFileReader() = default;
    // Reads [offset, offset+length) from `path`. Blocks the calling thread
    // until the read settles (completes, fails, or `token` is cancelled) --
    // callers wanting overlap issue reads from multiple threads; the
    // backend's internal queue-depth bound still caps real I/O concurrency.
    virtual AsyncReadResult read_range(const std::filesystem::path& path,
                                       std::uint64_t offset,
                                       std::uint64_t length,
                                       AsyncReadCancellationToken& token) = 0;
    virtual std::size_t queue_depth() const noexcept = 0;
};

#ifdef _WIN32
// Real Windows backend: CreateFile with FILE_FLAG_OVERLAPPED, reads
// completed via an IOCP completion port serviced by a small bounded
// worker-thread pool (pimpl'd so windows.h stays out of this header, same
// convention as ProjectIndexer/ProjectIndexService below).
class Win32OverlappedFileReader final : public IAsyncFileReader {
public:
    explicit Win32OverlappedFileReader(std::size_t queue_depth);
    ~Win32OverlappedFileReader() override;
    Win32OverlappedFileReader(const Win32OverlappedFileReader&) = delete;
    Win32OverlappedFileReader& operator=(const Win32OverlappedFileReader&) = delete;

    AsyncReadResult read_range(const std::filesystem::path& path,
                               std::uint64_t offset, std::uint64_t length,
                               AsyncReadCancellationToken& token) override;
    std::size_t queue_depth() const noexcept override { return queue_depth_; }

private:
    class State;
    std::unique_ptr<State> state_;
    std::size_t queue_depth_{1U};
};
#else
// POSIX fallback backend: a bounded std::thread worker pool issuing
// blocking pread() off the calling thread -- same shape as retrieval.cpp's
// DeadlineTaskPool. This is the plan's explicitly-allowed fallback, used
// unconditionally on Linux rather than a hardware-specific io_uring
// adapter (out of scope for a Windows-primary build target).
class PosixPreadPoolReader final : public IAsyncFileReader {
public:
    explicit PosixPreadPoolReader(std::size_t queue_depth);
    ~PosixPreadPoolReader() override;
    PosixPreadPoolReader(const PosixPreadPoolReader&) = delete;
    PosixPreadPoolReader& operator=(const PosixPreadPoolReader&) = delete;

    AsyncReadResult read_range(const std::filesystem::path& path,
                               std::uint64_t offset, std::uint64_t length,
                               AsyncReadCancellationToken& token) override;
    std::size_t queue_depth() const noexcept override { return queue_depth_; }

private:
    class State;
    std::unique_ptr<State> state_;
    std::size_t queue_depth_{1U};
};
#endif

// Constructs the best available backend for this platform, sized by
// `profile`'s adaptive queue depth. Returns nullptr if backend construction
// fails (e.g. IOCP creation error) -- callers MUST treat nullptr as "async
// unavailable" and keep using the existing blocking path; this is the
// fallback contract Phase 21 requires (see read_file_bytes() below).
std::unique_ptr<IAsyncFileReader> make_async_file_reader(
    const StorageLatencyProfile& profile);

// Lazily constructs (once per distinct filesystem root, process lifetime)
// and returns the shared async reader for the drive/root containing
// `storage_root`, or nullptr if construction failed or has not been
// attempted successfully. Construction failure is cached -- callers are not
// expected to retry probing on every read.
IAsyncFileReader* global_async_file_reader(
    const std::filesystem::path& storage_root);

// One requested byte range for the read coalescer, paired with the file it
// targets -- ranges are only ever merged with other ranges on the same
// path, never across files.
struct CoalescedReadRequest {
    std::filesystem::path path;
    std::uint64_t offset{0};
    std::uint64_t length{0};
};

// One physical read the coalescer decided to actually issue, plus which
// original request indices it satisfies and where each one's data starts
// within the merged buffer.
struct CoalescedReadPlan {
    struct Member {
        std::size_t request_index{0};
        std::uint64_t buffer_offset{0};
    };
    std::filesystem::path path;
    std::uint64_t offset{0};
    std::uint64_t length{0};
    std::vector<Member> members;
};

// Merges same-file requests whose byte ranges are adjacent or overlapping
// (gap <= max_gap_bytes) into the smallest number of physical reads -- e.g.
// two back-to-back 4KB manifest reads become one 8KB read. Requests on
// different files never merge. Pure and deterministic (no I/O), so it is
// unit-testable without touching a real reader backend.
std::vector<CoalescedReadPlan> coalesce_read_requests(
    const std::vector<CoalescedReadRequest>& requests,
    std::uint64_t max_gap_bytes = 0U);

// Result of a bounded multi-range operation. `results` is always in the
// caller's original request order; `physical_reads` discloses coalescing,
// and `peak_temporary_bytes` proves the configured scratch ceiling was
// respected. A request larger than the ceiling is rejected individually
// rather than violating the process memory bound.
struct AsyncReadBatchResult {
    std::vector<AsyncReadResult> results;
    std::size_t physical_reads{0U};
    std::uint64_t peak_temporary_bytes{0U};
    bool cancelled{false};
};

// Coalesces adjacent same-file ranges, then issues only independent plans
// in parallel up to both reader.queue_depth() and maximum_temporary_bytes.
// No thread-per-file behavior: at most queue_depth worker futures exist in
// one wave, and each settled wave releases its merged buffers before the
// next begins.
AsyncReadBatchResult read_file_ranges(
    IAsyncFileReader& reader,
    const std::vector<CoalescedReadRequest>& requests,
    AsyncReadCancellationToken& token,
    std::uint64_t maximum_temporary_bytes = 16ULL * 1024ULL * 1024ULL,
    std::uint64_t maximum_coalescing_gap_bytes = 4096U);

// Reads [offset, offset+length) of `path` using `reader` if non-null and
// the read completes without cancellation or failure; otherwise
// transparently falls back to a direct blocking ifstream read of the same
// bytes. This is the opt-in Phase 21 integration point models.cpp and
// indexing.cpp call instead of open-coding std::ifstream, guaranteeing
// pre-Phase-21 blocking behavior whenever the async path isn't available
// (reader is nullptr) or fails for this particular call.
std::string read_file_bytes(IAsyncFileReader* reader,
                            const std::filesystem::path& path,
                            std::uint64_t offset, std::uint64_t length);

// ---------------------------------------------------------------------
// Phase 30 (foundational types only): immutable shared-data buffers,
// views, a request-scoped arena, and fixed-size pools.
// ---------------------------------------------------------------------
//
// Scope note: this pass builds only the buffer/arena/pool primitives from
// docs/PLAN.md's Phase 30 deliverable list (see the approved implementation
// plan). It does NOT yet wire these into retrieval.cpp's
// RetrievalCandidate/IndexChunk (Phase 24 will adopt ChunkReference there),
// server.cpp/inference.cpp's streaming copy chain (separate later task), or
// MemoryBudgetManager's live accounting (deferred to whichever phase adds
// FixedSizePool's actual consumers). Those integrations are honestly
// out of scope here -- this is the primitive layer they will build on.

// Reference-counted immutable byte buffer. Once constructed its contents
// never change; sharing is via cheap reference-counted copies (BufferView
// keeps its owning SharedBuffer alive) rather than duplicating bytes. The
// backing storage is freed only once the last owner is destroyed.
//
// HARD RULE (per docs/PLAN.md Phase 30 spec): copy-on-write / shared
// immutable-buffer semantics are restricted to this family of types
// (SharedBuffer, BufferView, MappedBufferView, ChunkReference, TokenSpan,
// PromptSegment) and to large, mostly-immutable, clearly-controlled data.
// They must NEVER be used for credentials or other secret material (see
// storage.cpp's SecretStore), mutable network buffers, audit records, or
// any data crossing a process trust boundary -- those categories need
// exclusive ownership and explicit copying so one holder can never observe
// another holder's in-place mutation, extend a secret's lifetime past its
// intended scope, or share memory across a trust boundary.
class SharedBuffer final {
public:
    SharedBuffer() = default;
    // Takes ownership of `bytes` (moved) as the immutable backing storage.
    explicit SharedBuffer(std::vector<std::uint8_t> bytes);
    // Copies size bytes starting at data into a new immutable backing
    // storage -- the explicit "yes, this copies" entry point, used when the
    // caller does not already own a movable buffer.
    static SharedBuffer copy_from(const void* data, std::size_t size);

    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;
    bool empty() const noexcept { return size() == 0U; }
    // Number of live SharedBuffer owners of this storage. Diagnostic only
    // (e.g. for tests confirming a buffer outlives its original owner) --
    // never use this for synchronization decisions.
    long use_count() const noexcept;

private:
    std::shared_ptr<const std::vector<std::uint8_t>> storage_;
};

// Offset+length window into a SharedBuffer's bytes, plus optional encoding
// metadata (e.g. "utf-8", "json"; empty means raw/unspecified). Holds a
// SharedBuffer copy internally so the owning storage stays alive for as
// long as the view exists, but never copies the underlying bytes.
class BufferView final {
public:
    BufferView() = default;
    // offset/length must fit within owner's bytes; a view that does not fit
    // is clamped to the empty view rather than reading out of bounds.
    BufferView(SharedBuffer owner, std::size_t offset, std::size_t length,
              std::string encoding = {});

    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept { return length_; }
    std::size_t offset() const noexcept { return offset_; }
    const std::string& encoding() const noexcept { return encoding_; }
    const SharedBuffer& owner() const noexcept { return owner_; }
    bool empty() const noexcept { return length_ == 0U; }
    // Materializes an independent std::string copy of this view's bytes --
    // the one place this family intentionally leaves the zero-copy world,
    // meant only for hand-off points (e.g. to a backend API expecting an
    // owned std::string), not for routine use.
    std::string to_string() const;

private:
    SharedBuffer owner_;
    std::size_t offset_{0};
    std::size_t length_{0};
    std::string encoding_;
};

// A read-only memory-mapped file region exposed as a byte view with the
// same offset/length/encoding shape as BufferView. Backed by a real OS
// mapping (Win32 CreateFileMappingW/MapViewOfFile, POSIX mmap as the
// fallback) rather than a full read into a SharedBuffer -- useful for
// large, read-mostly assets (model weight files, large index segments)
// where paging beats a full up-front copy. Pimpl'd so platform mapping
// headers stay out of this shared header, matching the
// Win32OverlappedFileReader convention above.
class MappedBufferView final {
public:
    // Maps [offset, offset+length) of `path` read-only. length == 0 maps to
    // end of file. Throws std::runtime_error if the mapping cannot be
    // established (missing file, offset beyond EOF, OS mapping failure) --
    // callers wanting a graceful fallback should catch and fall back to
    // SharedBuffer::copy_from over a plain read, mirroring Phase 21's
    // reader-unavailable fallback contract.
    explicit MappedBufferView(const std::filesystem::path& path,
                              std::uint64_t offset = 0U, std::uint64_t length = 0U,
                              std::string encoding = {});
    ~MappedBufferView();
    MappedBufferView(const MappedBufferView&) = delete;
    MappedBufferView& operator=(const MappedBufferView&) = delete;
    MappedBufferView(MappedBufferView&&) noexcept;
    MappedBufferView& operator=(MappedBufferView&&) noexcept;

    const std::uint8_t* data() const noexcept;
    std::size_t size() const noexcept;
    const std::string& encoding() const noexcept { return encoding_; }

private:
    class State;
    std::unique_ptr<State> state_;
    std::string encoding_;
};

// Generic offset+length reference into an owning buffer/segment identified
// by an opaque string key (e.g. an index segment path or cache key) --
// deliberately decoupled from any concrete storage type so Phase 24's
// RetrievalCandidate/IndexChunk can adopt this as their text-field
// replacement without this header depending on retrieval.cpp. Copying a
// ChunkReference is cheap (two integers and a short string); the referenced
// bytes are copied out only by materialize(), and only when the caller
// supplies the BufferView that actually owns the segment.
struct ChunkReference {
    std::string segment_key;
    std::uint64_t offset{0};
    std::uint64_t length{0};

    // Copies out this chunk's bytes from `segment`, which must be the view
    // over the buffer/segment that segment_key identifies. Returns an empty
    // string if [offset, offset+length) does not fit within `segment` --
    // fails safe rather than reading out of bounds.
    std::string materialize(const BufferView& segment) const;
};

// A span of token-stream data within an owning SharedBuffer -- reused by
// Phase 23's tokenization cache and Phase 30's future zero-copy streaming
// path. `token_count` is caller-defined granularity (raw bytes vs. token
// ids vs. UTF-8 codepoints); this type does not fix a tokenizer
// representation, only the ownership/view shape.
struct TokenSpan {
    SharedBuffer owner;
    std::size_t offset{0};
    std::size_t length{0};
    std::size_t token_count{0};

    const std::uint8_t* data() const noexcept;
};

// One segment of an assembled prompt (literal template text or a
// substituted variable) as a view over a shared immutable fragment buffer.
// Meant to let Phase 23's assemble_chat_prompt() build a vector of
// PromptSegments instead of repeated std::string += copies, materializing
// one contiguous string only at the point RunnerSupervisor::generate()
// needs to hand text to the backend.
struct PromptSegment {
    BufferView view;
    bool is_literal{true};  // false => substituted variable content
};

// Thrown by RequestArena::allocate_or_throw() when the arena is exhausted.
// This implementation's chosen allocation-failure policy (the plan requires
// a defined one): allocate() returns nullptr on exhaustion for call sites
// that can degrade gracefully; allocate_or_throw() throws this for call
// sites that treat exhaustion as a bug.
class ArenaExhaustedError final : public std::runtime_error {
public:
    ArenaExhaustedError() : std::runtime_error("RequestArena exhausted") {}
};

// Bounded bump allocator for request-scoped scratch (retrieval-candidate
// metadata, ranking scratch, JSON parse scratch -- per the Phase 30 spec).
// Fixed byte size given at construction; never grows, never frees
// individual allocations -- only reset() or destruction reclaims the whole
// arena at once. This is the simplest allocator shape that satisfies "no
// view/handle may outlive the arena": callers must not retain a raw
// pointer or ArenaHandle past the arena's lifetime or a reset() call.
//
// Debug-build poisoning: in builds without NDEBUG, the arena maintains a
// generation counter in a small heap block kept alive independently of the
// arena itself (via shared_ptr) so an ArenaHandle issued before a reset()
// or destroy can detect staleness safely -- it checks the independent
// counter, never arena memory that might already be freed/reused. In
// release (NDEBUG) builds this tracking compiles out entirely (no
// runtime cost); see ArenaHandle below.
class RequestArena final {
public:
    explicit RequestArena(std::size_t capacity_bytes);
    ~RequestArena();
    RequestArena(const RequestArena&) = delete;
    RequestArena& operator=(const RequestArena&) = delete;

    // Bumps the arena pointer by `size` bytes (aligned to `alignment`,
    // which must be a power of two) and returns the new region, or nullptr
    // if it does not fit within the remaining capacity.
    void* allocate(std::size_t size,
                   std::size_t alignment = alignof(std::max_align_t)) noexcept;
    // Same as allocate(), but throws ArenaExhaustedError instead of
    // returning nullptr on exhaustion.
    void* allocate_or_throw(std::size_t size,
                            std::size_t alignment = alignof(std::max_align_t));

    // Rewinds the bump pointer to the start and, in debug builds, bumps the
    // generation counter so any ArenaHandle issued before this call becomes
    // detectably stale.
    void reset() noexcept;

    std::size_t capacity_bytes() const noexcept { return capacity_bytes_; }
    std::size_t used_bytes() const noexcept { return used_bytes_; }

#ifndef NDEBUG
    // Debug-only: the independent generation token described above, plus
    // its current value. Used by allocate_handle() below to stamp an
    // ArenaHandle at issue time.
    std::shared_ptr<const std::atomic<std::uint64_t>> debug_generation_token() const noexcept {
        return generation_;
    }
    std::uint64_t debug_generation() const noexcept { return generation_->load(); }
#endif

private:
    std::unique_ptr<std::uint8_t[]> storage_;
    std::size_t capacity_bytes_{0};
    std::size_t used_bytes_{0};
#ifndef NDEBUG
    std::shared_ptr<std::atomic<std::uint64_t>> generation_;
#endif
};

// Debug-build poison check wrapper around a pointer obtained from a
// RequestArena. In debug builds, valid()/get() confirm the arena's
// generation has not advanced (via reset()) or died (via destruction)
// since this handle was issued -- see RequestArena's class comment for why
// this check never touches (possibly freed) arena memory directly. In
// release (NDEBUG) builds this collapses to a plain pointer wrapper with an
// always-true (pointer != nullptr) valid() check -- no runtime cost, and
// this is deliberately a debug-only safety net, not a substitute for
// correct scoping by callers.
template <typename T>
class ArenaHandle final {
public:
    ArenaHandle() = default;
#ifndef NDEBUG
    ArenaHandle(T* pointer,
               std::shared_ptr<const std::atomic<std::uint64_t>> generation_token,
               std::uint64_t generation_at_issue)
        : pointer_(pointer), generation_token_(std::move(generation_token)),
          generation_at_issue_(generation_at_issue) {}
#else
    explicit ArenaHandle(T* pointer) : pointer_(pointer) {}
#endif

    bool valid() const noexcept {
#ifndef NDEBUG
        return pointer_ != nullptr && generation_token_ != nullptr &&
               generation_token_->load() == generation_at_issue_;
#else
        return pointer_ != nullptr;
#endif
    }
    T* get() const noexcept { return valid() ? pointer_ : nullptr; }
    T& operator*() const { return *get(); }
    T* operator->() const { return get(); }

private:
    T* pointer_{nullptr};
#ifndef NDEBUG
    std::shared_ptr<const std::atomic<std::uint64_t>> generation_token_;
    std::uint64_t generation_at_issue_{0};
#endif
};

// Allocates room for `count` T objects from `arena` and returns an
// ArenaHandle<T> stamped with the arena's current generation (debug builds
// only -- release builds just wrap the pointer). Does not construct T; the
// caller is responsible for placement-new if T is non-trivial. Returns an
// invalid (null) handle if the arena cannot satisfy the request.
template <typename T>
ArenaHandle<T> allocate_handle(RequestArena& arena, std::size_t count = 1U) {
    void* raw = arena.allocate(sizeof(T) * count, alignof(T));
    T* pointer = static_cast<T*>(raw);
#ifndef NDEBUG
    return ArenaHandle<T>(pointer, arena.debug_generation_token(), arena.debug_generation());
#else
    return ArenaHandle<T>(pointer);
#endif
}

// Fixed-size pool of block-recycled T instances for frequently allocated
// small objects (queue nodes, request-state records, retrieval-result
// descriptors, token-stream chunks -- per the Phase 30 spec). Grows by
// whole blocks of `block_capacity` slots (never one object at a time) and
// recycles freed slots via a free list; once a block is allocated its
// storage never moves, so pointers returned by acquire() stay valid until
// release()d or the pool itself shrink()s away that block. bytes_reserved()
// reports the pool's current footprint so a future caller can register it
// against MemoryBudgetManager's existing category budgets (masterai.hpp's
// MemoryCategory) -- that live-accounting wiring is deferred to whichever
// phase adds this pool's actual consumers. Header-only (templated), so it
// lives entirely here rather than in shared_buffer.cpp.
template <typename T>
class FixedSizePool final {
public:
    explicit FixedSizePool(std::size_t block_capacity = 64U)
        : block_capacity_(block_capacity == 0U ? 1U : block_capacity) {}

    ~FixedSizePool() {
        for (auto& block : blocks_) {
            for (std::size_t i = 0; i < block_capacity_; ++i) {
                if (block.live[i]) {
                    reinterpret_cast<T*>(&block.storage[i])->~T();
                }
            }
        }
    }

    FixedSizePool(const FixedSizePool&) = delete;
    FixedSizePool& operator=(const FixedSizePool&) = delete;

    // Phase 30: fired only when bytes_reserved() actually changes -- i.e.
    // on a real block grow (never on an ordinary acquire() that recycles an
    // existing free slot) or a shrink() that released at least one block --
    // with the pool's new total reserved-byte footprint. This is the hook
    // BudgetTrackedPool (below) uses to keep MemoryBudgetManager's live
    // accounting synchronized with this pool's real footprint instead of a
    // one-time estimate. Left unset (empty std::function) by default, so
    // every pool that does not opt in pays nothing for this.
    std::function<void(std::size_t)> on_reserved_bytes_changed;

    // Constructs a new T (forwarding args) in a recycled or freshly grown
    // slot and returns a pointer to it. Never invalidates pointers issued
    // for other live slots.
    template <typename... Args>
    T* acquire(Args&&... args) {
        if (free_list_.empty()) grow_block();
        const Slot slot = free_list_.back();
        free_list_.pop_back();
        Block& block = blocks_[slot.block_index];
        T* pointer = reinterpret_cast<T*>(&block.storage[slot.slot_index]);
        new (pointer) T(std::forward<Args>(args)...);
        block.live[slot.slot_index] = true;
        ++block.live_count;
        return pointer;
    }

    // Destroys `*pointer` and returns its slot to the free list for reuse.
    // `pointer` must have come from this pool's acquire() and not already
    // be released; no-op (does not double-free) if it is not currently a
    // live slot from this pool.
    void release(T* pointer) noexcept {
        for (std::size_t bi = 0; bi < blocks_.size(); ++bi) {
            Block& block = blocks_[bi];
            auto* base = reinterpret_cast<T*>(block.storage.data());
            if (pointer >= base && pointer < base + block_capacity_) {
                const std::size_t index = static_cast<std::size_t>(pointer - base);
                if (block.live[index]) {
                    pointer->~T();
                    block.live[index] = false;
                    --block.live_count;
                    free_list_.push_back(Slot{bi, index});
                }
                return;
            }
        }
    }

    // Total bytes currently reserved across all grown blocks (live and free
    // slots alike) -- what a future MemoryBudgetManager registration would
    // report, not just bytes presently in use.
    std::size_t bytes_reserved() const noexcept {
        return blocks_.size() * block_capacity_ * sizeof(T);
    }
    std::size_t live_count() const noexcept {
        std::size_t total = 0U;
        for (const auto& block : blocks_) total += block.live_count;
        return total;
    }

    // Releases every block that currently has zero live objects back to the
    // pool's own storage (erased from blocks_), shrinking bytes_reserved()
    // accordingly -- the "shrink or release reserved blocks under memory
    // pressure" deliverable. Blocks with any live object are kept; shrink()
    // never invalidates a pointer still in use. Returns the number of
    // blocks released.
    std::size_t shrink() {
        std::size_t released = 0U;
        for (std::size_t bi = 0; bi < blocks_.size();) {
            if (blocks_[bi].live_count == 0U) {
                free_list_.erase(
                    std::remove_if(free_list_.begin(), free_list_.end(),
                                   [bi](const Slot& s) { return s.block_index == bi; }),
                    free_list_.end());
                blocks_.erase(blocks_.begin() + static_cast<std::ptrdiff_t>(bi));
                // Remaining free-list entries referencing later blocks must
                // shift down by one to track the erase() above.
                for (auto& slot : free_list_) {
                    if (slot.block_index > bi) --slot.block_index;
                }
                ++released;
            } else {
                ++bi;
            }
        }
        // Phase 30: same live-accounting hook as grow_block() above, fired
        // only when at least one block was actually released (bytes_reserved()
        // genuinely dropped) -- a no-op shrink() (nothing to release) must
        // not cause a spurious re-reservation of the unchanged footprint.
        if (released > 0U && on_reserved_bytes_changed) {
            on_reserved_bytes_changed(bytes_reserved());
        }
        return released;
    }

private:
    struct Block {
        std::vector<typename std::aligned_storage<sizeof(T), alignof(T)>::type> storage;
        std::vector<bool> live;
        std::size_t live_count{0};
        explicit Block(std::size_t capacity) : storage(capacity), live(capacity, false) {}
    };
    struct Slot {
        std::size_t block_index{0};
        std::size_t slot_index{0};
    };

    void grow_block() {
        blocks_.emplace_back(block_capacity_);
        const std::size_t block_index = blocks_.size() - 1U;
        for (std::size_t i = 0; i < block_capacity_; ++i) {
            free_list_.push_back(Slot{block_index, i});
        }
        // Phase 30: a real block was just added, so bytes_reserved() just
        // grew -- tell anyone watching (e.g. BudgetTrackedPool) the new
        // total. Not fired from acquire() itself, only from here, so
        // ordinary free-slot recycling never triggers a MemoryBudgetManager
        // round trip.
        if (on_reserved_bytes_changed) on_reserved_bytes_changed(bytes_reserved());
    }

    std::size_t block_capacity_;
    std::vector<Block> blocks_;
    std::vector<Slot> free_list_;
};

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
    // Flips UserRecord::enabled without touching any other field --
    // authentication (see the `!user->enabled` check in server.cpp) already
    // refuses a disabled account, so this is how an administrator removes a
    // user from authentication without discarding their audit history or id
    // references elsewhere. Returns false for an unknown id rather than
    // throwing, matching every other ml.* store's set_status() convention.
    bool set_enabled(const std::string& id, bool enabled);

private:
    RecordStore& records_;
};

// Phase 66: docs/PLAN.md "Machine Learning Abilities" section 43 (Audit
// Logs) reads through this same one struct, not a second write path --
// every ml.* mutation already calls AuditLog::append() (see server.cpp).
struct AuditLogEntry {
    std::uint64_t timestamp_epoch_seconds{0};
    std::string event;
    std::string actor;
    std::string outcome;
    std::string detail;
};

class AuditLog final {
public:
    explicit AuditLog(std::filesystem::path path);
    void append(const std::string& event, const std::string& actor,
                const std::string& outcome, const std::string& detail);
    // Reads the chained log file and returns up to `limit` entries, most
    // recent first, optionally restricted to events beginning with
    // `event_prefix` (e.g. "ml." for the ML admin audit viewer). A
    // malformed line is skipped rather than aborting the whole read, since
    // an operator inspecting the log after some corruption is exactly the
    // case this must stay usable for.
    std::vector<AuditLogEntry> recent(std::size_t limit,
                                      const std::string& event_prefix = {}) const;

private:
    std::filesystem::path path_;
    // append() reads the whole file to find the previous hash-chain link and
    // then writes the next line; both steps must happen as one atomic unit
    // under concurrent callers or the chain breaks / lines interleave.
    // mutable so the const recent() reader can hold the same lock.
    mutable std::mutex mutex_;
};

std::string audit_log_entries_json(const std::vector<AuditLogEntry>& entries);

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

// Phase 29: model tiering, routing, and cascade inference.
//
// Scope note (docs/PLAN.md Phase 29): actually wiring tier selection into
// the live chat pipeline (HttpServer::State::send_chat_message /
// RunnerSupervisor) so a real request is transparently routed and, on
// escalation, re-run against a second warm runner is a substantially
// larger change spanning the request-handling path this pass does not
// make -- doing that safely needs Phase 26 warm-state management driving
// which tiers stay resident, which is only scoped-down itself. What ships
// working and tested this pass: the deterministic tier-selection,
// cascade-escalation, and resident-profile-validation decision logic the
// plan describes, exercised directly rather than through a live request.
enum class ModelTier {
    deterministic_processing,
    compact_router,
    small_fast,
    medium_general,
    large_specialist
};

struct ModelTierAssignment {
    ModelTier tier{ModelTier::medium_general};
    std::string model_id;
};

// Every signal the plan requires routing to disclose. Not all signals
// influence every decision below -- each one that does is exercised in the
// Phase 29 tests, and every one is still recorded/returned so a caller can
// disclose the full input on the query trace even where the current
// decision rule does not yet use it (the same "declared, disclosed, real
// even if a later rule will use it more" discipline Phase 24's
// classification categories already followed).
struct RoutingSignals {
    std::string task_category;
    std::string language;
    std::uint64_t context_size_tokens{0};
    std::string requested_quality;  // "draft" | "standard" | "high"
    std::uint32_t latency_requirement_ms{0};
    std::set<std::string> required_capabilities;
    std::uint64_t available_ram_mib{0};
    std::uint64_t available_vram_mib{0};
    std::uint32_t queue_depth{0};
    bool benchmark_evidence_favours_small_model{false};
    std::optional<std::string> user_pinned_model_id;
};

enum class EscalationReason {
    none,
    low_confidence,
    unsupported_syntax,
    conflicting_retrieval_evidence,
    failed_deterministic_validation,
    security_sensitivity,
    explicit_user_request,
    context_exceeds_capacity
};

struct CascadeStageOutcome {
    ModelTier tier{ModelTier::small_fast};
    std::string model_id;
    bool succeeded{false};
    double confidence{0.0};
    bool security_sensitive{false};
    bool deterministic_validation_failed{false};
    bool retrieval_conflict{false};
    bool unsupported_syntax{false};
    bool user_requested_escalation{false};
};

struct CascadeDecision {
    bool escalate{false};
    EscalationReason reason{EscalationReason::none};
    // Unset when escalate is false, OR when a triggering condition fired
    // but no higher tier exists to escalate to -- callers distinguish
    // "no escalation needed" (reason == none) from "wanted to escalate but
    // the cascade is already at its ceiling" (reason != none, escalate ==
    // false, next_tier unset) by inspecting `reason`.
    std::optional<ModelTier> next_tier;
};

enum class ResidentModelProfile { minimal, balanced, performance };

// Pure decision logic; owns no runner state and never itself loads or
// unloads a model. A caller (a later, larger integration than this pass
// attempts -- see the scope note above) is responsible for actually
// invoking the selected tier's model and feeding its outcome back through
// evaluate_cascade().
class ModelRouter final {
public:
    explicit ModelRouter(
        std::map<ModelTier, std::vector<std::string>> tier_models = {});

    // Selects the cheapest tier whose declared capability/quality/context
    // requirements are met, downgraded further if needed to fit
    // available_ram_mib/available_vram_mib against each tier's notional
    // minimum footprint -- "avoid using the largest resident model for
    // every request" is enforced structurally: nothing here ever jumps
    // straight to large_specialist except an explicit high-quality/
    // large-context/security-sensitive signal or an unresolvable user pin.
    std::optional<ModelTierAssignment> select_initial_tier(
        const RoutingSignals& signals) const;
    static CascadeDecision evaluate_cascade(const CascadeStageOutcome& outcome,
                                            double confidence_threshold = 0.6);
    static std::optional<ModelTier> next_tier_after(ModelTier tier);
    // Resident-model profile validation (docs/PLAN.md Phase 29): does this
    // set of currently-warm tiers stay within `profile`'s declared ceiling?
    // Memory/benchmark-evidence gating for `performance` is
    // MemoryBudgetManager's job, not this pure function's -- it always
    // returns true for `performance` (no fixed tier-count ceiling is
    // declared for it, only an evidence requirement this function has no
    // evidence to evaluate).
    static bool resident_set_within_profile(
        ResidentModelProfile profile, const std::vector<ModelTier>& resident_tiers);

private:
    std::map<ModelTier, std::vector<std::string>> tier_models_;
};

std::string to_string(ModelTier tier);
std::string to_string(ResidentModelProfile profile);
std::string to_string(EscalationReason reason);
// Inverse of to_string(ModelTier) -- throws std::invalid_argument on any
// spelling that isn't one of the five declared tiers. Used both to validate
// AppConfig::model_tier_assignments keys at load time and to build the
// ModelRouter's tier_models_ map from that configuration.
ModelTier parse_model_tier(const std::string& text);

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
    // hash_progress, when set, is called periodically while a single model
    // file is being hashed -- (model_id, bytes_hashed, total_bytes) -- so a
    // large model doesn't leave the caller with no output at all until it
    // finishes. Distinct from `progress` above (which only ever reports a
    // model's final verified/failed outcome) so interim updates can never be
    // mistaken for one more completed model.
    std::size_t verify(
        const std::function<void(const std::string& model_id, bool verified,
                                 const std::string& message)>& progress,
        const std::function<void(const std::string& model_id,
                                 std::uint64_t bytes_hashed,
                                 std::uint64_t total_bytes)>& hash_progress =
            {}) const;

private:
    std::filesystem::path model_root_;
    HardwareInfo hardware_;
    std::uint64_t memory_reserve_mib_{2048};
};

// Records one model as verified directly into the same on-disk cache
// ModelRegistry::verify() writes, without hashing anything itself. The one
// caller of this (DownloadManager::run(), downloads.cpp) already computes a
// full SHA-256 of the file as part of its own transfer-integrity check --
// this lets that already-paid-for hash immediately promote the model to
// Ready on the very next scan() instead of requiring a separate, redundant
// `masterai verify-models` pass to re-hash a file that was just hashed
// moments ago. Best-effort: a write failure here only means the model
// falls back to showing as unverified until the next explicit verify run,
// never fails the download itself.
void record_verified_model(const std::filesystem::path& model_root,
                           const std::string& model_id,
                           const std::string& sha256_hex,
                           std::uint64_t size_bytes);

// Phase 26: use-prediction evidence for one model, recorded as it happens
// (recency, an administrator/user pin, how often a project selects this
// model, and how many requests are currently waiting on it) rather than
// computed into an opaque ranking. Deliberately plain data -- callers decide
// what to do with it; nothing here evicts, prioritizes, or reorders on its
// own.
struct ModelUsageSignals {
    std::string model_id;
    std::uint64_t last_used_epoch_seconds{0};
    bool pinned{false};
    // Recency-weighted count of times a project selected this model --
    // incremented on every record_use(), never decayed automatically (a
    // caller wanting decay applies it when reading the snapshot).
    std::uint64_t project_preference_score{0};
    std::uint32_t waiting_request_count{0};
};

// Phase 26: in-memory recorder for ModelUsageSignals, administrator-
// inspectable via model_usage_signals_json() below -- the same "small
// struct + _json() free function" reporting convention TuningProfile/
// tuning_profile_json already established, rather than a new reporting
// surface. Not persisted across a restart (matches BoundedWorkQueue's
// in-memory-only scope above): use-prediction is a live signal about the
// current process's traffic, not a durable record.
class ModelUsagePredictor final {
public:
    void record_use(const std::string& model_id,
                    std::uint64_t now_epoch_seconds);
    void set_pinned(const std::string& model_id, bool pinned);
    void set_waiting_request_count(const std::string& model_id,
                                   std::uint32_t count);
    void increment_waiting(const std::string& model_id);
    void decrement_waiting(const std::string& model_id);
    std::vector<ModelUsageSignals> snapshot() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, ModelUsageSignals> signals_;
};

std::string model_usage_signals_json(const std::vector<ModelUsageSignals>& signals);

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
    // Phase 30A: additional environment variables merged on top of the
    // current process's inherited environment before launch. Populated by
    // build_launch_spec() with GPU-visibility-hiding variables
    // (CUDA_VISIBLE_DEVICES etc.) when the effective accelerator policy is
    // cpu_only, so a backend library cannot enumerate/initialize a GPU even
    // if it ignores --n-gpu-layers 0. Empty for every other launch, which
    // reproduces the pre-Phase-30A behavior of a fully inherited
    // environment exactly (see RunnerSupervisor::Process::start()).
    std::map<std::string, std::string> environment;
};

// Phase 26: explicit model-weight load mode. "direct" (raw unbuffered I/O)
// is deliberately omitted -- no backend this codebase launches has validated
// support for it yet, and faking the distinction would be dishonest. Every
// concrete mode's *effect* on the actual llama-server launch is already
// fully expressed through LaunchTuning::allow_memory_map/allow_memory_lock
// below (see CalibrationService::resolve() and select_load_mode()), so
// build_launch_spec() needs no separate argument branch for this field --
// it is carried alongside the two booleans as explicit, inspectable intent
// rather than leaving a caller to reverse-engineer "resident" from two
// booleans.
enum class ModelLoadMode {
    streamed,     // no mmap: read weights on demand, minimize resident/commit
    mapped,       // mmap, demand-paged (this codebase's pre-Phase-26 default)
    resident,     // mmap + mlock: pin every page resident up front
    auto_select   // caller wants CalibrationService to choose from evidence
};

std::string to_string(ModelLoadMode mode);

// Phase 26: selective pre-touch policy for model weight pages. MasterAI
// realizes metadata/first-use/layer-window through read-only mapped windows
// before backend startup; full remains the backend's --mlock path.
enum class PreTouchLevel { none, metadata, first_use, layer_window, full };

std::string to_string(PreTouchLevel level);
// True for every currently implemented policy level.
bool pre_touch_level_backend_actionable(PreTouchLevel level) noexcept;
// Returns an explanation only for an unknown enum value.
std::optional<std::string> pretouch_gap_reason(PreTouchLevel level);

struct PreTouchReport {
    PreTouchLevel level{PreTouchLevel::none};
    std::uint64_t file_bytes{0U};
    std::uint64_t bytes_touched{0U};
    std::uint64_t pages_touched{0U};
    std::uint64_t elapsed_microseconds{0U};
    bool cancelled{false};
};

// Read-only, chunk-mapped selective pre-touch. Cancellation/yield are
// checked between bounded mapping windows and pages; no mapping survives
// the call. maximum_bytes==0 uses the selected policy's safe default.
PreTouchReport pre_touch_model_file(
    const std::filesystem::path& model_file, PreTouchLevel level,
    const std::atomic_bool& cancellation,
    const std::function<bool()>& should_yield = {},
    std::uint64_t maximum_bytes = 0U);

// Phase 26: chooses a concrete ModelLoadMode from real evidence --
// StorageLatencyProfile (Phase 21) and the RAM headroom MemoryBudgetManager
// already tracks -- rather than guessing. Never throws; a model_size_bytes
// of 0 (unknown) always yields the safe pre-Phase-26 default (`mapped`).
// See CalibrationService::resolve() for how this feeds a TuningProfile.
ModelLoadMode select_load_mode(const StorageLatencyProfile& storage,
                               std::uint64_t available_ram_bytes,
                               std::uint64_t model_size_bytes) noexcept;

// GPU-layer offload selection: how many transformer layers of a model's
// weights should be copied into GPU (VRAM) memory instead of staying
// CPU-resident, expressed as the same layer count the backend's
// --n-gpu-layers flag takes. kGpuLayersOffloadAll is a sentinel (llama.cpp
// and llama-server both clamp any --n-gpu-layers value larger than the
// model's real layer count down to "every layer"), used here instead of an
// exact layer count because ModelManifest does not carry per-model layer
// counts -- only a total weight size. Without that, a layer count between
// "none" and "all" cannot honestly be computed (it would be a guess dressed
// up as a measurement), so this function only ever decides the two cases it
// really can reason about from size alone: the whole model comfortably fits
// in the VRAM left after a headroom reservation (offload everything), or it
// doesn't (offload nothing, the always-safe CPU-only fallback). Returns 0
// whenever GPU offload cannot be justified: no GPU backend detected, the
// model declares a required_gpu_backend the host doesn't have, VRAM size is
// unknown (0 -- probing failed or is unsupported on this host), or
// model_size_bytes is unknown (0). Never throws.
constexpr unsigned int kGpuLayersOffloadAll = 999U;
unsigned int select_gpu_layers(const HardwareInfo& hardware,
                               const std::string& required_gpu_backend,
                               std::uint64_t model_size_bytes) noexcept;

// Phase 85: evidence-based --threads/--ubatch-size recommendations. See
// their definitions in calibration.cpp for the full rationale; declared here
// (rather than kept file-local) so calibration tests can exercise them
// directly, matching select_gpu_layers()'s own visibility.
unsigned int select_thread_count(const HardwareInfo& hardware) noexcept;
unsigned int select_ubatch_tokens(unsigned int context_length) noexcept;

// Phase 19: optional backend-launch tuning a calibration profile can
// recommend. Every field's default reproduces exactly what
// build_launch_spec() emitted before Phase 19 (no GPU-layer flag, mmap left
// on, no mlock, no explicit thread/batch override), so a caller that never
// consults CalibrationService is unaffected. Phase 26 adds load_mode/
// pre_touch as advisory metadata riding alongside allow_memory_map/
// allow_memory_lock (see ModelLoadMode/PreTouchLevel above for why they
// don't need their own build_launch_spec() argument branch); their defaults
// (`mapped`/`none`) also reproduce pre-Phase-26 behavior exactly.
struct LaunchTuning {
    unsigned int gpu_layers{0};
    bool allow_memory_map{true};
    bool allow_memory_lock{false};
    unsigned int thread_count{0};
    unsigned int batch_tokens{0};
    unsigned int ubatch_tokens{0};
    ModelLoadMode load_mode{ModelLoadMode::mapped};
    PreTouchLevel pre_touch{PreTouchLevel::none};
    // Phase 25: asks a compatible llama-server to collect compatible slot
    // work into its continuous token-generation batch. Kept default-off;
    // HttpServer enables it only after AdvancedOptimizationRegistry admits
    // the measured `continuous_batching` candidate.
    bool continuous_batching{false};
    // Phase 32: the dual-model (draft + target) launch path. Empty by
    // default, reproducing exactly today's single-model launch for every
    // existing caller. Populated only by a caller that has already run
    // check_draft_target_compatibility() and decide_speculative_decoding_
    // for_request() successfully, and only after AdvancedOptimizationRegistry
    // has admitted "speculative_decoding" -- build_launch_spec() itself does
    // not re-derive compatibility, it only emits the flags. Honest limitation
    // (matches the LoRA fine-tune CLI flags' own disclosed gap in ml_finetune
    // .cpp): these are llama.cpp server's long-standing, documented
    // speculative-decoding flags, but this codebase cannot verify which
    // flags an administrator-vendored llama-server binary actually accepts,
    // so an incompatible binary surfaces as an ordinary runner-launch
    // failure rather than a distinct diagnostic.
    std::filesystem::path speculative_draft_model_file;
    unsigned int speculative_draft_gpu_layers{0};
    // Phase 27: reduced-precision KV cache. Left at its implicit default
    // (KvPrecision::full, see the enum's definition further down) for every
    // caller that hasn't explicitly resolved a non-full precision through
    // KvCacheManager::precision_admitted() -- build_launch_spec() only ever
    // emits the corresponding --cache-type-k/v flags for a caller that
    // explicitly set this, it never decides admission itself.
    KvPrecision kv_precision{};
};

// Forward declaration only: RunnerSupervisor below stores an optional
// CacheManager* (Phase 23 tokenization cache) but must not depend on
// CacheManager's full definition, which is declared later in this header
// and itself depends on types not yet available at this point in the file.
class CacheManager;

class LlamaCppAdapter final {
public:
    explicit LlamaCppAdapter(std::filesystem::path approved_backend);
    bool available() const;
    // Phase 18: parallel_slots exposes that many independent llama.cpp
    // server KV-cache slots via --parallel so PromptSessionManager has slot
    // ids to address with cache_prompt/id_slot requests. Defaulted to 1
    // (today's pre-Phase-18 behavior) so existing callers that don't pass it
    // are unaffected.
    // accelerator_policy is AppConfig::accelerator_policy. "cpu_only"
    // throws if tuning.gpu_layers != 0 (belt-and-suspenders: the caller's
    // CalibrationService should already have resolved gpu_layers to 0 --
    // see CalibrationService::resolve()/calibrate() -- so this only ever
    // fires if a caller bypasses calibration) and populates the returned
    // LaunchSpec::environment with GPU-visibility-hiding variables.
    LaunchSpec build_launch_spec(const ModelRecord& model,
                                 unsigned int context_length,
                                 unsigned int port,
                                 unsigned int parallel_slots = 1U,
                                 const LaunchTuning& tuning = {},
                                 const std::string& accelerator_policy = "auto") const;

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

// Phase 26: warm-model state machine layered ON TOP of RunnerState above --
// it is never a replacement. Every existing consumer of RunnerState (e.g.
// server.cpp's ensure_model_loaded(), the tests below, RunnerSupervisor's
// own ready_port()) keeps reading RunnerState exactly as before; this enum
// only ever adds detail RunnerState's six values cannot express on their
// own (sub-phases of "starting"/"stopping", and an idle/busy distinction
// within "ready").
enum class WarmModelState {
    Cold,                 // never loaded in this process's lifetime
    LoadingMetadata,       // manifest re-verified, launch spec being built
    MappingWeights,        // backend process spawned, weights not yet mapped
    InitialisingBackend,   // process running, waiting on its readiness probe
    Warming,               // backend reported ready; not yet observed serving
    Ready,                 // idle and available to serve a request immediately
    Busy,                  // actively serving a generation/tokenize request
    Idle,                  // ready but unused past the configured idle threshold
    Draining,               // graceful unload requested, process still running
    Evicting,               // graceful shutdown timed out; forced termination
    Unloaded,               // process stopped, resources released
    Failed                  // load or generation failed; process may be gone
};

std::string to_string(WarmModelState state);

// The regression-safe baseline the plan requires: every RunnerState value
// maps to exactly one WarmModelState so anything derived purely from
// RunnerState never changes meaning. WarmModelTracker::observe_runner_state
// applies this table; finer detail (LoadingMetadata/MappingWeights/
// InitialisingBackend/Warming sub-phases of "starting", Idle/Draining/
// Evicting) is layered on afterward via WarmModelTracker::enter(), which is
// still checked against warm_state_transition_allowed() below.
WarmModelState translate_runner_state(RunnerState state) noexcept;

// The legal state graph for WarmModelState. Returns true when transitioning
// directly from `from` to `to` is permitted; a self-transition (from == to)
// is always legal (a no-op re-observation of the same state).
bool warm_state_transition_allowed(WarmModelState from,
                                   WarmModelState to) noexcept;

// Small stateful tracker: one WarmModelState value plus a last-activity
// timestamp, guarded by its own mutex so it can be composed into
// RunnerSupervisor (which already holds its own mutex for RunnerMetrics)
// without risking lock-ordering mistakes, and used standalone in tests.
class WarmModelTracker final {
public:
    // Applies translate_runner_state(state) as the next state. Throws
    // std::logic_error if that baseline transition is illegal from the
    // tracker's current state -- which should never happen for any
    // sequence RunnerSupervisor itself produces; if it throws, the
    // translation table and RunnerSupervisor's state machine have drifted
    // out of sync with each other.
    void observe_runner_state(RunnerState state);
    // Applies an explicit finer-grained transition. Throws
    // std::logic_error if `next` is not reachable from the current state.
    void enter(WarmModelState next);
    WarmModelState current() const noexcept;
    void record_activity(std::uint64_t now_epoch_seconds) noexcept;
    std::uint64_t last_activity_epoch_seconds() const noexcept;
    // If the tracker is currently Ready and has been idle for at least
    // idle_unload_seconds as of now_epoch_seconds, transitions to Idle and
    // returns true; otherwise leaves the state untouched and returns false.
    // A no-op (returns false) unless the current state is exactly Ready, so
    // calling this repeatedly from a background sweep is always safe.
    bool apply_idle_timeout(std::uint64_t now_epoch_seconds,
                            std::uint32_t idle_unload_seconds);

private:
    mutable std::mutex mutex_;
    WarmModelState state_{WarmModelState::Cold};
    std::uint64_t last_activity_epoch_seconds_{0};
};

struct GenerationOptions {
    unsigned int max_tokens{512};
    double temperature{0.2};
    std::uint64_t seed{1};
    std::vector<std::string> stop_sequences;
    // Anti-repetition sampling controls, forwarded to the llama.cpp runner.
    // Without an explicit repeat penalty small instruct models (seen with
    // the Qwen2.5 family) can fall into an unbounded loop, restating the
    // same sentences until max_tokens is exhausted instead of ending their
    // turn. repeat_last_n is how many recent tokens the penalty considers.
    double repeat_penalty{1.1};
    unsigned int repeat_last_n{256};
    // Nucleus/top-k sampling bounds (llama.cpp's own defaults); kept
    // explicit here so every generation runs under known sampling settings
    // rather than whatever the runner build's defaults happen to be.
    double top_p{0.95};
    unsigned int top_k{40};
    // Phase 18: when cache_prompt is true, slot_id selects the llama.cpp
    // server's own internal KV-cache slot to reuse instead of always
    // re-evaluating the whole prompt from token zero. Left at their safe
    // defaults (false / empty) for any caller that never consults
    // PromptSessionManager, which is the only intended source of these
    // values.
    bool cache_prompt{false};
    std::optional<unsigned int> slot_id;
};

struct GenerationResult {
    std::string text;
    std::uint64_t prompt_tokens{0};
    std::uint64_t generated_tokens{0};
    std::uint64_t elapsed_microseconds{0};
    bool cancelled{false};
};

struct EmbeddingResult {
    std::vector<double> values;
    std::string model_id;
    std::uint64_t elapsed_microseconds{0};
};

struct RunnerMetrics {
    RunnerState state{RunnerState::unloaded};
    std::string model_id;
    std::uint64_t process_id{0};
    std::uint64_t resident_memory_bytes{0};
    std::uint64_t requests_completed{0};
    std::uint64_t requests_cancelled{0};
    std::string diagnostic;
    // Phase 26: the finer-grained warm-state view layered on top of `state`
    // above (see WarmModelTracker). Every pre-Phase-26 caller reading only
    // `state` is unaffected -- this field is purely additive.
    WarmModelState warm_state{WarmModelState::Cold};
    // Phase 30A administration visibility (docs/PLAN.md Phase 30A
    // deliverable 5): what the last load() call actually asked for, so
    // GET /api/v1/runner/status can show requested-vs-actual GPU layers and
    // the accelerator policy that launch was validated under, rather than an
    // administrator having to infer it from launch-arg logs.
    unsigned int requested_gpu_layers{0U};
    std::string accelerator_policy;
    std::uint64_t last_activity_epoch_seconds{0U};
};

class RunnerSupervisor final {
public:
    RunnerSupervisor(std::filesystem::path approved_backend,
                     std::filesystem::path runtime_root);
    ~RunnerSupervisor();
    RunnerSupervisor(const RunnerSupervisor&) = delete;
    RunnerSupervisor& operator=(const RunnerSupervisor&) = delete;

    void load(const ModelRecord& model, unsigned int context_length,
              unsigned int port, std::uint32_t startup_timeout_seconds = 30,
              unsigned int parallel_slots = 1U,
              const LaunchTuning& tuning = {},
              const std::string& accelerator_policy = "auto");
    void unload(std::uint32_t grace_seconds = 10) noexcept;
    std::uint64_t tokenize(const std::string& text);
    // Phase 23: opt-in tokenization cache. When set, tokenize() first looks
    // up (content hash, model vocabulary fingerprint, special-token policy)
    // in `cache` under CacheCategory::tokenization and only falls back to
    // the runner's own /tokenize HTTP round trip on a miss. Left unset
    // (the default, and what every pre-Phase-23 caller/test still gets),
    // tokenize() behaves exactly as before -- always calls the runner.
    // `special_token_policy` should change whenever the caller changes how
    // special tokens are requested/handled so a policy change cannot be
    // served a token count computed under the old policy.
    void set_tokenization_cache(CacheManager* cache,
                                std::string special_token_policy = "default");
    // stall_timeout_seconds bounds how long generate() will wait between
    // bytes from the runner before treating it as hung (e.g. a deadlock
    // building the first inference graph on a cold model) rather than
    // blocking forever; it resets on every byte received, so a slow-but-
    // still-streaming generation is never cut off by it.
    GenerationResult generate(
        const std::string& prompt, const GenerationOptions& options,
        const std::function<void(const std::string&)>& on_chunk,
        const std::atomic_bool& cancellation,
        std::uint32_t stall_timeout_seconds = 120U);
    // Phase 61: asks the currently loaded, process-isolated llama.cpp model
    // for one pooled learned embedding. The vector is validated and
    // normalized before it enters MasterAI's durable knowledge index.
    EmbeddingResult embed(const std::string& text);
    RunnerMetrics metrics() const;
    // Phase 26: applies WarmModelTracker::apply_idle_timeout() against this
    // supervisor's own tracker -- see that method's contract. Intended to be
    // called periodically (e.g. from a background sweep alongside
    // MemoryBudgetManager pressure sampling) rather than on every request.
    bool apply_idle_timeout(std::uint64_t now_epoch_seconds,
                            std::uint32_t idle_unload_seconds);

private:
    class Process;
    unsigned int ready_port(bool mark_busy);
    std::filesystem::path runtime_root_;
    LlamaCppAdapter adapter_;
    std::unique_ptr<Process> process_;
    mutable std::mutex mutex_;
    RunnerMetrics metrics_;
    unsigned int port_{0};
    // Phase 26: layered warm-model state (see WarmModelTracker/WarmModelState
    // above). Owns its own mutex, so it is updated independently of mutex_
    // above rather than trying to fold it into RunnerMetrics's own locking.
    WarmModelTracker warm_tracker_;
    // Phase 23 tokenization cache wiring (see set_tokenization_cache()).
    // model_sha256_ is captured from the loaded model's manifest digest at
    // load() time and doubles as the tokenizer/vocabulary fingerprint the
    // plan requires -- the same model file always tokenizes text the same
    // way, so its already-verified content digest is exactly the right
    // fingerprint, with no separate vocabulary hash to compute or store.
    CacheManager* tokenization_cache_{nullptr};
    std::string model_sha256_;
    std::string special_token_policy_{"default"};
    // Phase 25: number of live /completion calls admitted to the external
    // runner and the calibrated ceiling. A default/off launch keeps the
    // historical exclusive value of one; an admitted continuous-batching
    // launch raises it to the configured --parallel slot count.
    unsigned int active_generations_{0U};
    unsigned int maximum_parallel_generations_{1U};
};

// Phase 33 (LOCAL-ONLY slice): a point-in-time view of one pool runner for
// administration visibility (GET /api/v1/runner/pool) and for
// LocalRunnerPool::status_json().
struct RunnerPoolEntrySnapshot {
    std::string id;
    RunnerMetrics metrics;
    std::set<std::string> capabilities;
    std::set<std::string> authorized_project_ids;
    unsigned int priority{0U};
    // A runner is considered unhealthy after
    // LocalRunnerPool::kMaxConsecutiveFailures consecutive generate()/
    // ensure_model_loaded() failures and is excluded from select_runner()
    // until it next succeeds -- see LocalRunnerPool's class comment.
    bool healthy{true};
    std::uint64_t consecutive_failures{0U};
};

// Thrown by LocalRunnerPool::generate() on any failure from the named
// runner. `any_bytes_emitted` tells the caller, per
// retry_is_semantically_safe() below, whether resending this request (to
// another runner, or to the default single-runner fallback) could ever
// duplicate output the caller has already forwarded on -- never a guess,
// since it reflects exactly whether this call's own on_chunk wrapper fired
// at least once before the failure.
struct RunnerGenerationFailure final : std::runtime_error {
    RunnerGenerationFailure(const std::string& message, std::string runner,
                            bool bytes_emitted)
        : std::runtime_error(message),
          runner_id(std::move(runner)),
          any_bytes_emitted(bytes_emitted) {}
    std::string runner_id;
    bool any_bytes_emitted;
};

// Phase 33 (LOCAL-ONLY slice) exit criterion "retries occur only when
// semantically safe and never duplicate a persisted response": a retry is
// safe only when the previous attempt is certain to have produced no
// output the caller has already forwarded on AND nothing has already been
// durably persisted from it. Centralised here so every retry decision in
// this codebase applies the identical rule instead of re-deriving it ad hoc
// at each call site.
bool retry_is_semantically_safe(bool any_bytes_already_emitted_to_caller,
                                bool request_already_marked_persisted);

// Phase 33 (LOCAL-ONLY slice): supervises N RunnerSupervisor processes
// concurrently from one control plane, generalizing the existing
// single-runner supervision pattern (RunnerSupervisor itself, and how
// ServerState::inference/ensure_model_loaded use it in server.cpp) rather
// than inventing a parallel mechanism. Strictly additive/opt-in:
// constructed only when AppConfig::local_runner_pool is non-empty; every
// existing single-runner deployment is unaffected because nothing consults
// LocalRunnerPool unless a caller explicitly asks for it.
//
// Failure isolation: every RunnerSupervisor call this class makes on behalf
// of a caller is wrapped in try/catch. A failing runner is marked with an
// incremented failure count and, past kMaxConsecutiveFailures, excluded
// from select_runner() -- but the exception is always re-thrown as a typed
// RunnerGenerationFailure/std::runtime_error the *caller* handles, never
// swallowed into a crash and never allowed to take another runner's state
// with it, since each RunnerSupervisor already owns its own process and
// mutex completely independently of every other entry in entries_.
class LocalRunnerPool final {
public:
    static constexpr std::uint64_t kMaxConsecutiveFailures = 3U;

    LocalRunnerPool(std::vector<LocalRunnerConfig> runners,
                    std::filesystem::path approved_backend,
                    std::filesystem::path runtime_root);
    ~LocalRunnerPool();
    LocalRunnerPool(const LocalRunnerPool&) = delete;
    LocalRunnerPool& operator=(const LocalRunnerPool&) = delete;

    bool empty() const noexcept;

    // Scores every configured, authorized, capable, healthy runner against
    // `signals` and returns the winning runner id, or nullopt when none
    // qualifies -- this never silently falls back to an unauthorized or
    // incapable runner. See runner_pool.cpp for the scoring rule: resident
    // model match first, then runner health/state, then priority, with
    // authorization and capability applied as hard filters before scoring
    // ever runs.
    std::optional<std::string> select_runner(
        const RunnerSelectionSignals& signals) const;

    // Per-runner equivalent of ServerState::ensure_model_loaded(), scoped to
    // one named pool runner instead of the single shared `inference`
    // supervisor. Deliberately simpler than ensure_model_loaded() -- it does
    // not integrate CalibrationService per-runner tuning in this pass; a
    // caller that wants calibrated tuning for a pool runner passes its own
    // `tuning` through, matching what ensure_model_loaded() itself would
    // have resolved for the default runner.
    void ensure_model_loaded(const std::string& runner_id,
                             const ModelRecord& model,
                             unsigned int context_length,
                             std::uint32_t startup_timeout_seconds,
                             unsigned int parallel_slots = 1U,
                             const LaunchTuning& tuning = {});

    // Failure-isolated, idempotency-disclosing generation -- see
    // RunnerGenerationFailure and the class comment above.
    GenerationResult generate(
        const std::string& runner_id, const std::string& prompt,
        const GenerationOptions& options,
        const std::function<void(const std::string&)>& on_chunk,
        const std::atomic_bool& cancellation,
        std::uint32_t stall_timeout_seconds = 120U);

    // Failure-isolated embedding call. Unlike generate(), an embedding
    // response is never partial/streamed -- it either returns one complete
    // vector or throws -- so a caller may always safely retry an embed()
    // failure on a different runner without any idempotency check.
    EmbeddingResult embed(const std::string& runner_id, const std::string& text);

    RunnerSupervisor& runner(const std::string& runner_id);
    RunnerMetrics metrics(const std::string& runner_id) const;
    bool healthy(const std::string& runner_id) const;

    std::vector<RunnerPoolEntrySnapshot> status() const;
    static std::string status_json(const std::vector<RunnerPoolEntrySnapshot>& entries);

private:
    struct Entry;
    Entry& required(const std::string& runner_id);
    const Entry& required(const std::string& runner_id) const;
    std::vector<std::unique_ptr<Entry>> entries_;
};

// Phase 33 (INTRANET-WORKER slice, 2026-08-13): signs one worker
// certificate against `ca`, generating a fresh worker keypair. Throws if
// this build was not compiled with OpenSSL available
// (MASTERAI_HAS_OPENSSL) or if `ca` cannot be loaded -- fails closed, the
// same precedent the PAM-optional OS-identity path already established for
// a missing optional native dependency (see ConfigurationManager's PAM
// warning in CMakeLists.txt).
IssuedWorkerCertificate issue_worker_certificate(
    const PrivateCertificateAuthority& ca, const std::string& worker_common_name);

// Generates a fresh private CA keypair and self-signed certificate at the
// paths named by `ca`, refusing to overwrite an existing certificate or
// key (an administrator who wants a new CA must explicitly move the old
// one aside first -- silently replacing a CA would invalidate every
// already-issued worker certificate without warning).
void initialize_private_certificate_authority(const PrivateCertificateAuthority& ca);

// True only when this build was compiled with OpenSSL available. Every
// mTLS-dependent entry point (IntranetWorkerPool, WorkerListener,
// issue_worker_certificate(), initialize_private_certificate_authority())
// checks this itself and throws a clear "not available in this build"
// message rather than silently no-op'ing, so callers do not need to guard
// every call site individually -- this is exposed only for administration
// surfaces (e.g. GET /api/v1/system/pki) that want to report build
// capability without provoking a throw.
bool openssl_available() noexcept;

// Phase 33 (INTRANET-WORKER slice): the encrypted, remote counterpart to
// LocalRunnerPool. Every worker it can route to is an administrator-
// approved intranet machine reached over mutual TLS: this control plane
// verifies the worker's certificate against its own pinned private CA
// (never the system trust store) AND against a pinned leaf-certificate
// digest (defense in depth, the same convention Phase 9's outbound MCP
// executable pinning uses -- see mcp_outbound.cpp); the worker equally
// verifies this control plane's own client certificate before accepting
// any request, so a network position between the two machines can neither
// impersonate a worker nor impersonate this control plane. Requires this
// build to have been compiled with OpenSSL available -- without it, every
// method here fails closed with a clear "not available in this build"
// error rather than silently downgrading to plaintext.
//
// Model-digest verification (Phase 33 deliverable): refresh_status() reads
// back each worker's self-reported loaded-model sha256 over the already-
// authenticated channel and only ever offers that worker to
// select_worker() when the digest matches a model this control plane's own
// registry also knows under the same id -- a worker can never be used to
// silently serve an unexpected model. There is no "load this model on that
// worker" RPC (see IntranetWorkerConfig's class comment): a worker's model
// is fixed by that worker's own local configuration.
//
// Failure isolation mirrors LocalRunnerPool exactly: every network/TLS
// call is wrapped in try/catch, a failing worker is recorded against its
// own entry only and excluded past kMaxConsecutiveFailures, and nothing
// here ever takes the control plane down with it.
class IntranetWorkerPool final {
public:
    static constexpr std::uint64_t kMaxConsecutiveFailures = 3U;

    IntranetWorkerPool(std::vector<IntranetWorkerConfig> workers,
                       PrivateCertificateAuthority ca,
                       std::filesystem::path client_certificate_file,
                       std::filesystem::path client_private_key_file);
    ~IntranetWorkerPool();
    IntranetWorkerPool(const IntranetWorkerPool&) = delete;
    IntranetWorkerPool& operator=(const IntranetWorkerPool&) = delete;

    bool empty() const noexcept;

    // Opens a fresh mutually-authenticated connection to the named worker,
    // asks its /worker/status endpoint for its currently loaded model id
    // and sha256, and records the result against that worker's entry --
    // including marking it healthy/unhealthy. Never throws: a worker that
    // cannot be reached or fails verification is simply marked unhealthy,
    // matching LocalRunnerPool's failure-isolation convention.
    // `known_model_sha256_by_id` is this control plane's own registry view
    // (model id -> ModelManifest::model_sha256); a worker whose reported
    // digest does not match is marked unhealthy with a diagnostic rather
    // than trusted.
    void refresh_status(
        const std::string& worker_id,
        const std::map<std::string, std::string>& known_model_sha256_by_id);
    void refresh_all(
        const std::map<std::string, std::string>& known_model_sha256_by_id);

    std::optional<std::string> select_worker(
        const RunnerSelectionSignals& signals) const;

    GenerationResult generate(
        const std::string& worker_id, const std::string& prompt,
        const GenerationOptions& options,
        const std::function<void(const std::string&)>& on_chunk,
        const std::atomic_bool& cancellation,
        std::uint32_t stall_timeout_seconds = 120U);

    EmbeddingResult embed(const std::string& worker_id, const std::string& text);

    bool healthy(const std::string& worker_id) const;
    std::vector<RunnerPoolEntrySnapshot> status() const;
    static std::string status_json(const std::vector<RunnerPoolEntrySnapshot>& entries);

private:
    struct Entry;
    Entry& required(const std::string& worker_id);
    const Entry& required(const std::string& worker_id) const;
    std::vector<std::unique_ptr<Entry>> entries_;
    PrivateCertificateAuthority ca_;
    std::filesystem::path client_certificate_file_;
    std::filesystem::path client_private_key_file_;
};

// Phase 33 (INTRANET-WORKER slice): the worker side of the protocol
// IntranetWorkerPool speaks. Binds `configuration.worker_mode.bind_host`/
// `port` (which may be non-loopback -- see WorkerModeConfig's class
// comment for why that is a deliberate, narrowly-scoped exception) and
// accepts ONLY: a TLS handshake presenting a client certificate that
// chains to `configuration.worker_mode.ca_certificate_file` AND whose
// SHA-256 digest is listed in `approved_client_certificate_sha256`, then
// exactly two request shapes -- GET /worker/status (returns this worker's
// own RunnerMetrics-shaped JSON, including its loaded model's sha256) and
// POST /worker/generate or /worker/embed (forwards to the local
// RunnerSupervisor this WorkerListener owns, the same isolated child-
// process supervision every other MasterAI deployment already uses).
// Never exposes the administrator web UI, model management, or any other
// route the loopback HttpServer serves -- a compromised or misconfigured
// worker-mode listener can therefore never be used as a path to this
// machine's own administrator surface.
class WorkerListener final {
public:
    WorkerListener(WorkerModeConfig configuration,
                  std::filesystem::path approved_backend,
                  std::filesystem::path runtime_root);
    ~WorkerListener();
    WorkerListener(const WorkerListener&) = delete;
    WorkerListener& operator=(const WorkerListener&) = delete;

    // Runs the accept loop until `stop_requested` is set. Returns once the
    // listening socket is closed. Every accepted connection is handled on
    // its own thread; a per-connection failure (bad handshake, malformed
    // request, backend error) closes only that connection and never
    // affects the listener or any other in-flight connection.
    void run(std::atomic_bool& stop_requested);

    RunnerSupervisor& runner();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
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
    // Tokens attributable to this message: for a user message, the prompt
    // tokens the runner evaluated for that turn (history + template +
    // attachments included); for an assistant message, the tokens it
    // generated. 0 means "not recorded" (older persisted messages, or a
    // turn that failed before the runner reported counts) and is simply
    // not displayed by the web UI.
    std::uint64_t token_count{0};
};

// Phase 84 follow-up: user-visible control over how a chat's tool calls get
// executed, replacing what used to be a single hardcoded server-wide policy.
// auto_mode keeps today's behavior (classify_tool_call_risk() decides,
// destructive calls still always pause); confirm_all forces every call
// through the same human Approve/Deny path regardless of risk; off makes
// tools_available false for this chat entirely, so the model is never even
// told tools exist and nothing can be called. None of the three modes can
// ever downgrade a destructive call below high_risk -- see
// classify_tool_call_risk()'s own comment on why that stays unconditional.
enum class ChatToolExecutionMode { auto_mode, confirm_all, off };

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
    // Phase 61: a cached snapshot of the owner's durable memory records,
    // refreshed on every turn (see send_chat_message()) so a "save to
    // memory: ..." directive applies to the rest of the conversation it was
    // typed into, not only to chats created afterward. Legacy chats without
    // a snapshot yet get one on their first post-upgrade message.
    bool memory_context_initialized{false};
    std::string memory_context;
    // Phase 27: marks this chat as a shareable public prompt template.
    // Off by default -- only an owner/administrator explicitly marking a
    // chat this way (see ChatStore::set_shared_template()) lets
    // send_chat_message() attempt the KvCacheManager::admit_prefix_sharing()
    // -gated PromptSessionManager::try_reuse_shared_template()/
    // record_shared_template() path instead of (in addition to) the
    // ordinary private per-chat try_reuse()/record() path.
    bool is_shared_template{false};
    // See ChatToolExecutionMode's own comment. Defaults to auto_mode so a
    // legacy chat restored without this field keeps today's behavior.
    ChatToolExecutionMode tool_execution_mode{ChatToolExecutionMode::auto_mode};
    std::vector<ChatMessage> messages;
};

class ChatStore final {
public:
    ChatStore() = default;
    explicit ChatStore(RecordStore& records);
    ChatRecord create(const std::string& owner_id,
                      const std::string& project_id,
                      const std::string& model_id,
                      const std::string& memory_context = {},
                      ChatToolExecutionMode tool_execution_mode =
                          ChatToolExecutionMode::auto_mode);
    // Overwrites a chat's cached memory snapshot with a freshly recalled
    // one (see send_chat_message(), which calls this every turn whenever
    // durable memory changed since the cached copy). Also used to give a
    // legacy chat its first snapshot. Returns false only when the chat is
    // absent or belongs to another owner.
    bool initialize_memory_context(const std::string& chat_id,
                                   const std::string& owner_id,
                                   const std::string& memory_context);
    // token_count is the runner-reported token figure for this message (see
    // ChatMessage::token_count); callers that don't have one yet (the user
    // message is appended before generation runs) pass the default 0 and can
    // back-fill via set_last_message_tokens() once the runner reports it.
    void append(const std::string& chat_id, ChatRole role,
                const std::string& content, std::uint64_t token_count = 0);
    // Back-fills token_count on the most recent message with the given role
    // (used for the user message of the turn that just generated, whose
    // prompt-token figure only exists after the runner ran). No-op if the
    // chat has no such message.
    void set_last_message_tokens(const std::string& chat_id, ChatRole role,
                                 std::uint64_t token_count);
    // Switches which model future messages in this chat are generated with;
    // past messages/history are untouched. Returns false (no-op) if the chat
    // doesn't exist or isn't owned by owner_id, so the caller can turn that
    // into the same 404 find_for_owner()'s callers already return.
    bool set_model(const std::string& chat_id, const std::string& owner_id,
                   const std::string& model_id);
    // Phase 27: marks/unmarks a chat as a shareable public prompt template.
    // Owner-scoped like set_model() above -- returns false (no-op) if the
    // chat doesn't exist or isn't owned by owner_id.
    bool set_shared_template(const std::string& chat_id,
                             const std::string& owner_id, bool shared);
    // Phase 84 follow-up: changes how this chat's future tool calls are
    // gated (see ChatToolExecutionMode's own comment). Owner-scoped like
    // set_model()/set_shared_template() above.
    bool set_tool_execution_mode(const std::string& chat_id,
                                 const std::string& owner_id,
                                 ChatToolExecutionMode mode);
    std::optional<ChatRecord> find_for_owner(const std::string& chat_id,
                                             const std::string& owner_id) const;
    // Newest first, so callers can split "recent" from "history" by index
    // without re-sorting.
    std::vector<ChatRecord> list_for_owner(const std::string& owner_id) const;
    // Deletes a chat and every one of its persisted messages. Returns false
    // (no-op) if the chat doesn't exist or isn't owned by owner_id, so the
    // caller can turn that into the same 404 find_for_owner()'s callers
    // already return.
    bool remove(const std::string& chat_id, const std::string& owner_id);

private:
    void restore();
    // Writes only the chat's own metadata (owner/project/model/title/
    // createdAt) to the "chats" collection -- never the message list, so
    // renaming a chat or switching its model stays a small, constant-size
    // write regardless of how long the conversation already is.
    void persist_header(const ChatRecord& chat);
    // Writes exactly one message to the "chat_messages" collection under its
    // own key (see message_key()), so append() costs one small write instead
    // of re-serializing every prior message -- the previous scheme rewrote
    // the whole chat on every append, which made the on-disk journal grow
    // O(n^2) with conversation length.
    void persist_message(const std::string& chat_id, std::size_t index,
                         const ChatMessage& message);
    std::map<std::string, ChatRecord> chats_;
    RecordStore* records_{nullptr};
    // Guards chats_ against concurrent create/append/find/list calls.
    mutable std::mutex mutex_;
};

// One remembered detail about a user (a name, a preference, a suggestion,
// a personal fact). Captured either explicitly ("save to memory: ..." in a
// chat message, or the memory API) or automatically from phrasing like
// "my name is ..." -- see extract_memory_directive() /
// extract_automatic_memories(). Recalled details are injected into every
// new conversation once as bounded user-provided context, then retained in
// that ChatRecord so later turns do not repeatedly query durable memory.
struct UserMemoryRecord {
    std::string id;
    std::string owner_id;
    // The remembered detail itself, normalized to a single trimmed line.
    std::string content;
    // "manual" for the explicit command/API, "auto" for pattern capture.
    std::string source;
    std::uint64_t created_at_epoch_seconds{0};
};

// Durable per-user memory of important chat details. All operations are
// owner-scoped: one user can never see or delete another user's memories.
class UserMemoryStore final {
public:
    UserMemoryStore() = default;
    explicit UserMemoryStore(RecordStore& records);
    // Stores one detail. Content is trimmed and single-lined; duplicates
    // (case-insensitive, same owner) return the existing record instead of
    // storing twice. When the per-user cap is reached the oldest "auto"
    // memory is evicted first so explicit saves always succeed; if every
    // slot is an explicit save, the oldest of those is evicted instead.
    UserMemoryRecord add(const std::string& owner_id,
                         const std::string& content,
                         const std::string& source);
    // Newest first, so the recall budget below keeps the freshest details.
    std::vector<UserMemoryRecord> list_for_owner(
        const std::string& owner_id) const;
    bool remove(const std::string& id, const std::string& owner_id);
    // Builds the bounded "[Saved user details]" reference block prepended to
    // the current user turn -- oldest first for stable reading order, newest
    // kept when max_bytes forces a cut. Empty string when the user has no
    // memories or the byte budget cannot fit the fixed explanatory header.
    std::string recall_context(const std::string& owner_id,
                               std::size_t max_bytes) const;

private:
    void restore();
    void persist(const UserMemoryRecord& memory);
    // Frees one slot for add() when the per-user cap is reached; drops the
    // oldest automatic capture first. Caller must hold mutex_.
    void evict_oldest(const std::string& owner_id);
    std::map<std::string, UserMemoryRecord> memories_;
    RecordStore* records_{nullptr};
    // Guards memories_ against concurrent add/list/remove calls.
    mutable std::mutex mutex_;
};

// Detects an explicit "save to memory: <detail>" (or "remember this:
// <detail>") request anywhere in a chat message, case-insensitively, and
// returns the trimmed detail after the colon. std::nullopt when the message
// contains no directive or the detail is empty. Works entirely server-side,
// so it behaves identically with every model.
std::optional<std::string> extract_memory_directive(const std::string& message);

// True when the message is ONLY the directive (so the chat handler can
// confirm the save directly instead of running inference on it).
bool memory_directive_is_whole_message(const std::string& message);

// Scans one user chat message for self-disclosed details worth remembering
// automatically -- "my name is ...", "call me ...", "i live in ...",
// "i work at ...", "i prefer ...", "my email is ...", and similar phrasing
// -- and returns them restated in third person ("The user's name is ...").
// Bounded: at most three captures per message, each capped in length.
std::vector<std::string> extract_automatic_memories(const std::string& message);

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

// Phase 19: adaptive hardware/model calibration. A TuningProfile records
// exactly what CalibrationService measured for one host/model/backend/build
// identity, plus the recommendations derived from that measurement.
// Recalibration triggers are identity-based (Phase 17 CacheManager's own
// "structural invalidation by key mismatch" idea): any change to host_hash,
// model_sha256, backend_hash, or build_id makes TuningProfileStore::find()
// return nothing, so callers fall back to safe_default_profile() rather than
// silently reusing a stale measurement.
struct TuningProfile {
    std::string host_hash;
    std::string model_sha256;
    std::string backend_hash;
    std::string build_id;
    std::string profile_name{"balanced"};
    unsigned int recommended_context_length{4096U};
    unsigned int recommended_parallel_slots{1U};
    unsigned int recommended_batch_tokens{0};
    unsigned int recommended_gpu_layers{0};
    // Phase 85: --threads/--ubatch-size were plumbed all the way through
    // LaunchTuning/build_launch_spec() since Phase 19 but never actually
    // populated by resolve()/calibrate() -- both stayed at LaunchTuning's
    // default of 0, so llama-server's own (often conservative) default
    // thread count was used regardless of the host's real core count. These
    // two fields close that gap; see select_thread_count()/
    // select_ubatch_tokens() in calibration.cpp for the evidence-based
    // derivation. 0 keeps meaning "no override" for a pre-Phase-33 profile.
    unsigned int recommended_thread_count{0};
    unsigned int recommended_ubatch_tokens{0};
    bool recommended_allow_memory_map{true};
    bool recommended_allow_memory_lock{false};
    std::uint32_t recommended_idle_unload_seconds{600U};
    std::uint64_t cold_load_microseconds{0};
    std::uint64_t prompt_evaluation_microseconds{0};
    std::uint64_t generation_microseconds{0};
    std::uint64_t prompt_tokens_measured{0};
    std::uint64_t generated_tokens_measured{0};
    std::uint64_t peak_resident_memory_bytes{0};
    std::uint64_t peak_commit_bytes{0};
    std::uint64_t page_faults{0};
    double average_cpu_percent{0.0};
    std::uint64_t disk_read_bytes{0};
    std::uint64_t disk_write_bytes{0};
    std::uint64_t calibrated_at_epoch_seconds{0};
    // Phase 26: defaults reproduce the pre-Phase-26 implied behavior
    // (demand-paged mmap, no extra pre-touch), so a profile persisted before
    // Phase 26 (restored with these at their defaults) still behaves exactly
    // as it did before.
    ModelLoadMode recommended_load_mode{ModelLoadMode::mapped};
    PreTouchLevel recommended_pre_touch{PreTouchLevel::none};
    // Phase 19 (real-hardware-class evidence, 2026-08-13): sampled by
    // calibrate() from probe_gpu_vendor_telemetry() at the same 50ms cadence
    // as the existing peak-resident-memory sampler, across whatever
    // generation actually ran on the GPU. gpu_telemetry_available stays
    // false (and the numeric fields stay at their zero defaults) on any host
    // without an approved vendor SDK present -- this is disclosed evidence,
    // never a fabricated measurement, matching this project's existing
    // "declare the gap, don't guess" convention.
    bool gpu_telemetry_available{false};
    std::string gpu_vendor;
    double average_gpu_utilization_percent{0.0};
    int peak_gpu_temperature_celsius{0};
};

// A profile-name-keyed set of safe starting points (docs/PLAN.md section
// 31.2) used whenever no matching persisted TuningProfile exists yet, or an
// existing one was invalidated by a host/model/backend/build change. Never
// throws and never depends on a real measurement.
TuningProfile safe_default_profile(const std::string& profile_name);

class TuningProfileStore final {
public:
    TuningProfileStore() = default;
    explicit TuningProfileStore(RecordStore& records);
    void save(const TuningProfile& profile);
    std::optional<TuningProfile> find(const std::string& host_hash,
                                      const std::string& model_sha256,
                                      const std::string& backend_hash,
                                      const std::string& build_id) const;
    std::vector<TuningProfile> all() const;

private:
    void restore();
    RecordStore* record_store_{nullptr};
    std::vector<TuningProfile> profiles_;
};

// Runs a short, real measurement (cold load, one small and one medium prompt
// through tokenize()/generate(), peak resident/commit/page-fault and
// CPU%/disk-byte sampling) against an already-constructed RunnerSupervisor,
// mirroring the Phase 7 BenchmarkRunner / Phase 15 index-probe pattern. It
// never applies its own recommendation automatically -- callers decide
// whether/when to load a model with the resulting LaunchTuning.
class CalibrationService final {
public:
    // accelerator_policy is AppConfig::accelerator_policy ("auto"/
    // "cpu_only"/"gpu_allowed"). "cpu_only" forces every resolve()/
    // calibrate() result to recommended_gpu_layers == 0 and rejects (throws)
    // rather than silently drops a persisted profile that recommends a
    // nonzero value -- see the Phase 30A comment on resolve() below.
    CalibrationService(RunnerSupervisor& inference, TuningProfileStore& store,
                       HardwareInfo hardware, std::string backend_hash,
                       std::string build_id,
                       std::string accelerator_policy = "auto");

    TuningProfile calibrate(const ModelRecord& model,
                            const std::string& requested_profile,
                            unsigned int port,
                            const std::atomic_bool& cancellation);

    // Returns the persisted profile matching the current host/model/
    // backend/build identity, or a safe default for requested_profile if
    // none matches (never fabricates a measurement).
    //
    // Phase 26: when no persisted profile exists yet, `storage` (a Phase 21
    // StorageLatencyProfile) and `available_ram_bytes`/`model_size_bytes`
    // let the safe default's recommended_load_mode/recommended_pre_touch be
    // chosen from real evidence (see select_load_mode()) instead of always
    // being the generic `mapped`/`none` default. `storage` left null (every
    // pre-Phase-26 call site, and any caller that hasn't got a profile yet)
    // reproduces exactly the old, evidence-free behavior -- a *persisted*
    // profile is always returned as-is, never overridden by this evidence,
    // since it already reflects a real measurement.
    //
    // `model_size_bytes` and `required_gpu_backend` also drive
    // recommended_gpu_layers (see select_gpu_layers()), independently of
    // whether `storage` is supplied -- a caller that only knows the model's
    // size still gets a real GPU-offload recommendation instead of always
    // being stuck at the pre-GPU-support default of 0.
    // Phase 30A: when the constructed accelerator_policy is "cpu_only", the
    // returned profile always has recommended_gpu_layers == 0. If a
    // *persisted* profile (found in store_) recommends a nonzero value --
    // meaning it was calibrated under a different, GPU-permitting policy --
    // this throws instead of silently returning it with GPU layers zeroed
    // out, because a caller receiving a silently-modified profile could
    // launch with settings that no longer match what was measured/saved.
    TuningProfile resolve(const std::string& model_sha256,
                         const std::string& requested_profile,
                         const StorageLatencyProfile* storage = nullptr,
                         std::uint64_t available_ram_bytes = 0U,
                         std::uint64_t model_size_bytes = 0U,
                         const std::string& required_gpu_backend = std::string()) const;

    std::string host_hash() const;
    const std::string& accelerator_policy() const noexcept { return accelerator_policy_; }

private:
    RunnerSupervisor& inference_;
    TuningProfileStore& store_;
    HardwareInfo hardware_;
    std::string backend_hash_;
    std::string build_id_;
    std::string accelerator_policy_;
};

// Derives a LaunchTuning from a TuningProfile's recommendations so a caller
// can pass it straight to RunnerSupervisor::load().
LaunchTuning launch_tuning_from_profile(const TuningProfile& profile);

std::string tuning_profile_json(const TuningProfile& profile);

// Phase 20: evidence and admission record for optional advanced throughput.
// The registry is deliberately separate from each backend implementation:
// recording measurements cannot enable a feature, and admission succeeds only
// after the complete evidence contract below passes validation.
struct AdvancedOptimizationEvidence {
    std::string feature_name;
    std::string baseline_description;
    std::string changed_setting;
    std::string host_hash;
    std::string model_sha256;
    std::string backend_hash;
    double time_to_first_token_ms{0.0};
    double prompt_throughput_tokens_per_second{0.0};
    double generation_throughput_tokens_per_second{0.0};
    std::uint64_t peak_resident_memory_bytes{0};
    std::string quality_notes;
    std::string power_thermal_notes;
    bool regression_detected{false};
    bool fallback_verified{false};
};

struct AdvancedOptimizationFeature {
    std::string name;
    std::string description;
    // Defaults false. Recording evidence never flips it; only a later
    // explicit admit() after implementation/evidence/fallback checks can.
    bool enabled{false};
    bool requires_evidence{true};
    // True only when the native implementation is wired into a production
    // path. Evidence can be retained for an unavailable candidate, but it
    // cannot be admitted until this boundary says the implementation exists.
    bool implementation_available{false};
    std::optional<AdvancedOptimizationEvidence> evidence;
};

class AdvancedOptimizationRegistry final {
public:
    AdvancedOptimizationRegistry();
    explicit AdvancedOptimizationRegistry(RecordStore& records);
    std::vector<AdvancedOptimizationFeature> features() const;
    bool has_evidence(const std::string& feature_name) const;
    bool is_enabled(const std::string& feature_name) const;
    // Stores validated evidence for later review. This never enables the
    // feature: admission remains an explicit administrator action.
    void record_evidence(const std::string& feature_name,
                         const AdvancedOptimizationEvidence& evidence);
    void admit(const std::string& feature_name);
    void disable(const std::string& feature_name);

private:
    void initialize_features();
    void restore();
    void persist(const AdvancedOptimizationFeature& feature);
    AdvancedOptimizationFeature& find_mutable(const std::string& feature_name);
    const AdvancedOptimizationFeature& find(
        const std::string& feature_name) const;
    RecordStore* records_{nullptr};
    mutable std::mutex mutex_;
    std::vector<AdvancedOptimizationFeature> features_;
};

std::string advanced_optimization_registry_json(
    const std::vector<AdvancedOptimizationFeature>& features);

// Phase 25: weighted-fair priority scheduling and backpressure across
// concurrent inference-adjacent requests. Declared in priority order
// (highest first) exactly as the plan lists it; the enum's own ordinal
// value doubles as its priority rank, so `cancellation` (rank 0) always
// preempts `maintenance` (rank 7) without a separate lookup table.
//
// Scope note (docs/PLAN.md Phase 25): true continuous batching of live
// backend token-generation steps requires cooperation from the llama.cpp
// server process this control plane launches as an external runner --
// there is no in-process generation loop here to interleave, and no
// validated backend flag yet proven safe to depend on for it (the same
// "backend-validated support required" discipline Phase 27 KV precision
// and Phase 32 speculative decoding already apply). What this phase
// delivers instead, honestly: the weighted-fair admission/scheduling and
// backpressure layer the plan calls for, ready for a batching backend to
// plug into once one exists, exercised here by request *admission* order
// rather than by an actual batched generation step.
enum class SchedulingClass {
    cancellation_shutdown,
    ide_completion,
    interactive_chat,
    interactive_analysis,
    user_background_job,
    benchmark,
    indexing_embedding,
    maintenance
};

std::string to_string(SchedulingClass klass);

struct SchedulingClassPolicy {
    unsigned int weight{1U};
    std::size_t max_queue_depth{64U};
    std::chrono::milliseconds max_residence_time{std::chrono::minutes(5)};
    unsigned int concurrency_allowance{1U};
    std::uint64_t memory_allowance_bytes{0};  // 0 = unlimited
};

std::map<SchedulingClass, SchedulingClassPolicy> default_scheduling_policies();

struct ScheduledTicket {
    std::uint64_t id{0};
    SchedulingClass klass{SchedulingClass::interactive_chat};
};

struct SchedulingAdmission {
    bool admitted{false};
    std::string reason;
    std::optional<ScheduledTicket> ticket;
    // Set when admission succeeded only after evicting a queued (not yet
    // running) lower-priority ticket to make room -- the plan's "rejects
    // low-priority background work first" backpressure rule.
    std::optional<ScheduledTicket> preempted;
};

struct SchedulingClassStatus {
    std::size_t queued{0};
    std::size_t running{0};
    std::uint64_t reserved_memory_bytes{0};
    std::uint64_t admitted_total{0};
    std::uint64_t rejected_total{0};
    std::uint64_t expired_total{0};
    std::uint64_t preempted_total{0};
};

// Bounded, in-process, admission/backpressure/dequeue scheduler over
// SchedulingClass queues. Never touches the network or disk; it decides
// *order and admission*, not how work executes -- a caller still runs the
// dequeued ticket's actual work itself and calls complete()/cancel() to
// release the accounting. Thread-safe.
class RequestScheduler final {
public:
    explicit RequestScheduler(
        std::map<SchedulingClass, SchedulingClassPolicy> policies =
            default_scheduling_policies(),
        std::size_t global_concurrency_limit = 0U /* 0 = unlimited */);
    ~RequestScheduler();
    RequestScheduler(const RequestScheduler&) = delete;
    RequestScheduler& operator=(const RequestScheduler&) = delete;

    SchedulingAdmission admit(SchedulingClass klass,
                              std::uint64_t memory_bytes_required = 0U);
    // Weighted-fair dequeue: returns the next ticket whose class has
    // running < concurrency_allowance, chosen by a deficit-round-robin
    // credit counter seeded from each class's configured weight, always
    // preferring any non-empty cancellation_shutdown queue first.
    std::optional<ScheduledTicket> next_ready();
    // Production queue hand-off: waits until this exact admitted ticket is
    // selected by the same weighted-fair policy as next_ready(). Returns
    // false if cancellation, residence expiry, or backpressure removed it.
    // The short timed wait observes an atomic cancellation flag without a
    // second callback/cancellation mechanism.
    bool wait_until_ready(const ScheduledTicket& ticket,
                          const std::atomic_bool& cancellation);
    void complete(const ScheduledTicket& ticket);
    // Removes a still-queued ticket before it was ever returned by
    // next_ready(); returns false if it was already running or unknown.
    bool cancel(const ScheduledTicket& ticket);
    // Cancels any queued ticket whose residence time already exceeds its
    // class's max_residence_time. Called internally on admit()/next_ready()
    // as well, so callers never need to poll this on their own to keep
    // queues bounded, but may call it explicitly to force a sweep.
    std::size_t expire_stale();
    std::map<SchedulingClass, SchedulingClassStatus> status() const;
    static std::string to_json(
        const std::map<SchedulingClass, SchedulingClassStatus>& status);

private:
    class State;
    std::unique_ptr<State> state_;
};

// Machine Learning module (docs/PLAN.md "Machine Learning Abilities"
// section 1-51): an administrator-only module for teaching and building
// models, covering everything from dataset ingestion through training,
// evaluation, deployment, and governance. Started (Phase 37) as a
// foundation-phase acknowledgement with every interface beyond Dashboard
// listed "planned" and nothing real behind any of them; every one of the 29
// interfaces this registry now lists is "available" -- each has a real
// backing store and, per its own class comment/roster entry, a stated real-
// vs-recorded-only boundary for anything inside it that is not (see e.g.
// ModelOptimizationRun's roster entry: the interface itself is real, but
// only its `pruning` operation actually executes). See MachineLearningDashboard
// above for the always-real dashboard counts (Phase 38+) and
// docs/ToDo.md's "Documented scope limits" section for the current honest
// list of what inside an available interface still does not execute.
struct MachineLearningInterface {
    std::string key;
    std::string label;
    // "available" once its own backing service exists; "planned" otherwise.
    std::string status;
};

struct MachineLearningDashboard {
    bool enabled{true};
    std::string phase{"phase-60"};
    std::vector<MachineLearningInterface> interfaces;
    // active_projects is real: it comes from MLProjectStore::list() (see
    // dashboard() below), not a placeholder. The rest still start at zero
    // because Phase 38 only implements Projects -- there is nothing yet
    // that could produce a nonzero training/evaluation/deployment count.
    std::uint64_t active_projects{0};
    std::uint64_t models_training{0};
    std::uint64_t models_awaiting_evaluation{0};
    std::uint64_t models_awaiting_approval{0};
    std::uint64_t deployed_models{0};
    std::uint64_t failed_training_jobs{0};
};

class MachineLearningRegistry final {
public:
    MachineLearningRegistry();
    // active_projects is the count of `projects` whose status isn't
    // archived. models_training/models_awaiting_evaluation/deployed_models
    // come from `models`' state counts (training/evaluation/production
    // respectively). models_awaiting_approval and failed_training_jobs
    // remain zero (see MachineLearningDashboard's comment above) -- there is
    // no distinct "awaiting approval" state and no training-job system yet.
    // failed_training_jobs now comes from `training_jobs` (Phase 42) instead
    // of always reporting zero -- see that store's class comment below for
    // why counting `failed`-status jobs is the only aggregate meaningful
    // before an actual training executor exists.
    MachineLearningDashboard dashboard(const class MLProjectStore& projects,
                                       const class ModelRegistryStore& models,
                                       const class TrainingJobStore& training_jobs) const;

private:
    std::vector<MachineLearningInterface> interfaces_;
};

std::string machine_learning_dashboard_json(const MachineLearningDashboard& dashboard);

// Phase 38: docs/PLAN.md "Machine Learning Abilities" section 5 (Machine
// Learning Projects) -- the organizational container a later training/
// dataset/deployment phase will attach to. Scoped down from the section's
// full field list (owner/contributors, security classification, approved
// data sources, target architecture/deployment, success/evaluation/safety
// criteria, storage/compute allocation) to the subset that is meaningful
// before any of those subsystems exist: identity, intent, subject/task
// classification, and lifecycle status. The remaining fields belong to the
// phases that actually consume them, not to this one.
enum class MLProjectStatus {
    draft,
    data_collection,
    data_preparation,
    ready_for_training,
    training,
    evaluation,
    awaiting_approval,
    approved,
    deployed,
    paused,
    archived
};

std::string ml_project_status_name(MLProjectStatus status);
MLProjectStatus parse_ml_project_status(const std::string& status);

struct MLProject {
    std::string id;
    std::string name;
    std::string description;
    std::string objective;
    std::string subject_domain;
    std::string model_task;
    std::string owner_id;
    MLProjectStatus status{MLProjectStatus::draft};
    // Phase 93: docs/PLAN.md section 5's remaining definition-time fields,
    // added once the pipeline they describe (Phases 46/56/57) actually
    // exists to make them meaningful. administrators is comma-joined user
    // ids, each validated against UserStore at create()/update_governance()
    // time (matching how owner_id is already a real user id) -- these users
    // are shown alongside the owner everywhere a project is displayed, the
    // only field here with real logic beyond storage. approved_data_sources
    // is comma-joined free text (section 5 doesn't define a closed source
    // taxonomy). security_classification/target_architecture/
    // target_deployment_environment/success_criteria/
    // evaluation_requirements/safety_requirements are free text: they are
    // administrator-declared intent, not something this codebase computes.
    // storage_allocation_mb is a real declared ceiling; nothing enforces it
    // yet (no ML executor here allocates project-scoped disk quota), so it
    // is descriptive today, same honest-boundary treatment Phase 46 already
    // gives Model Builder's non-applicable fields.
    std::string administrators;
    std::string approved_data_sources;
    std::string security_classification;
    std::string target_architecture;
    std::string target_deployment_environment;
    std::string success_criteria;
    std::string evaluation_requirements;
    std::string safety_requirements;
    std::uint64_t storage_allocation_mb{0};
    std::string compute_allocation_notes;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class MLProjectStore final {
public:
    MLProjectStore() = default;
    explicit MLProjectStore(RecordStore& records);
    MLProject create(const std::string& owner_id, const std::string& name,
                     const std::string& description,
                     const std::string& objective,
                     const std::string& subject_domain,
                     const std::string& model_task,
                     const std::string& administrators = {},
                     const std::string& approved_data_sources = {},
                     const std::string& security_classification = {},
                     const std::string& target_architecture = {},
                     const std::string& target_deployment_environment = {},
                     const std::string& success_criteria = {},
                     const std::string& evaluation_requirements = {},
                     const std::string& safety_requirements = {},
                     std::uint64_t storage_allocation_mb = 0,
                     const std::string& compute_allocation_notes = {});
    std::optional<MLProject> find(const std::string& id) const;
    std::vector<MLProject> list() const;
    // Returns false (no-op) if the project doesn't exist, so callers can
    // turn that into a 404 the same way ChatStore::remove()'s callers do.
    bool set_status(const std::string& id, MLProjectStatus status);
    // Post-creation edits to the section-5 governance/target fields added
    // in Phase 93 -- these legitimately evolve as a project moves through
    // its lifecycle, the same way Experiment::update_metadata() already
    // lets hyperparameters/tags/notes evolve after creation.
    bool update_governance(const std::string& id,
                           const std::string& administrators,
                           const std::string& approved_data_sources,
                           const std::string& security_classification,
                           const std::string& target_architecture,
                           const std::string& target_deployment_environment,
                           const std::string& success_criteria,
                           const std::string& evaluation_requirements,
                           const std::string& safety_requirements,
                           std::uint64_t storage_allocation_mb,
                           const std::string& compute_allocation_notes);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const MLProject& project);
    RecordStore* records_{nullptr};
    std::map<std::string, MLProject> projects_;
    // Guards projects_ against concurrent create/list/set_status/remove
    // calls, matching every other RecordStore-backed store in this file.
    mutable std::mutex mutex_;
};

std::string ml_project_json(const MLProject& project);
std::string ml_projects_json(const std::vector<MLProject>& projects);

// Phase 39: docs/PLAN.md "Machine Learning Abilities" section 7 (Model
// Registry) -- the authoritative catalog of models known to the ML module,
// independent of the ModelManifest catalog the inference runner uses (that
// one describes models ready to *serve*; this one tracks a model's whole
// development lifecycle, including states -- imported, training,
// quarantined -- that a servable model would never be in). Scoped down the
// same way MLProject is: identity, provenance, and lifecycle state, not the
// full field list (evaluation results, safety assessment, hardware/runtime
// requirements, model hash/signature, change log) that later training and
// evaluation phases will attach to a registry entry once they exist.
enum class ModelRegistryState {
    imported,
    unverified,
    verified,
    training,
    evaluation,
    rejected,
    approved,
    staging,
    production,
    deprecated,
    archived,
    quarantined
};

std::string model_registry_state_name(ModelRegistryState state);
ModelRegistryState parse_model_registry_state(const std::string& state);

struct ModelRegistryEntry {
    std::string id;
    std::string name;
    std::string display_name;
    std::string version;
    std::string family;
    std::string task;
    std::string format;
    std::string source;
    std::string license;
    std::string owner_id;
    // GGUF quantization label (e.g. "F16", "Q8_0", "Q4_K_M"), carried over
    // from ModelManifest::quantization when a downloaded model is resolved
    // into the registry (see resolve_or_register_base_model in server.cpp)
    // -- empty for a manually-registered entry with no known quantization.
    // The LLM LoRA fine-tuning executor (ml_finetune.cpp) reads this to warn
    // when a base model is quantized below full precision, since llama.cpp's
    // finetune tooling is only reliably accurate against an F16/F32 (or
    // lightly quantized Q8_0) base.
    std::string quantization;
    ModelRegistryState state{ModelRegistryState::imported};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ModelRegistryStore final {
public:
    ModelRegistryStore() = default;
    explicit ModelRegistryStore(RecordStore& records);
    ModelRegistryEntry create(const std::string& owner_id,
                              const std::string& name,
                              const std::string& display_name,
                              const std::string& version,
                              const std::string& family,
                              const std::string& task,
                              const std::string& format,
                              const std::string& source,
                              const std::string& license,
                              const std::string& quantization = std::string{});
    std::optional<ModelRegistryEntry> find(const std::string& id) const;
    std::vector<ModelRegistryEntry> list() const;
    // Enforces docs/PLAN.md section 7's rule that a model must never reach
    // `production` merely because training finished: the transition to
    // `production` is rejected (std::invalid_argument) unless the entry is
    // already `approved` or `staging`. Every other transition is allowed --
    // there is no evaluation/approval workflow yet to validate against.
    bool set_state(const std::string& id, ModelRegistryState state);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const ModelRegistryEntry& entry);
    RecordStore* records_{nullptr};
    std::map<std::string, ModelRegistryEntry> entries_;
    mutable std::mutex mutex_;
};

std::string model_registry_entry_json(const ModelRegistryEntry& entry);
std::string model_registry_entries_json(
    const std::vector<ModelRegistryEntry>& entries);

// Phase 39: docs/PLAN.md "Machine Learning Abilities" section 10 (Dataset
// Manager). Scoped down from the section's full field list (record/file
// count, schema, data-quality score, sensitive-data status, duplicate rate,
// train/validation/test split, dataset hash) to identity, provenance, and
// approval status -- the fields that are meaningful before an actual
// ingestion pipeline exists to populate the rest. Section 11 (Dataset
// Versioning) is deliberately not implemented here: it requires a real
// content pipeline to version, which this phase doesn't have.
enum class DatasetApprovalStatus { pending, approved, rejected };

std::string dataset_approval_status_name(DatasetApprovalStatus status);
DatasetApprovalStatus parse_dataset_approval_status(const std::string& status);

struct Dataset {
    std::string id;
    std::string name;
    std::string description;
    std::string subject_area;
    std::string source;
    std::string license;
    std::string data_format;
    std::string owner_id;
    DatasetApprovalStatus approval_status{DatasetApprovalStatus::pending};
    // "tabular" (default) means content upload validates a classification/
    // regression target column; "instruction" means content is LLM fine-
    // tuning data (instruction/prompt + response/output/completion columns)
    // and skips that target-column validation entirely -- see the content
    // upload endpoint in server.cpp.
    std::string purpose{"tabular"};
    // Phase 94: docs/PLAN.md section 10's remaining fields, computed for
    // real from the actual uploaded content (DatasetContentStore) every
    // time POST .../content succeeds -- see compute_dataset_content_metrics()
    // in ml_engine.cpp for exactly how each is derived. Zero/empty until
    // content has been uploaded at least once. file_count is always 0 or 1:
    // one upload always replaces/creates exactly one stored CSV blob, this
    // codebase has no multi-file dataset concept.
    std::uint64_t record_count{0};
    std::uint32_t file_count{0};
    // Comma-joined "columnName:numeric|categorical" pairs, in header order.
    std::string schema_summary;
    std::string content_hash;  // sha256_hex of the exact stored CSV bytes
    double duplicate_rate{0.0};       // exact-duplicate data rows / total
    // non_null_ratio * non_duplicate_ratio -- see the function comment for
    // exactly what this does and does not measure; never fabricated for an
    // empty/no-content dataset (stays 0 until real content exists).
    double data_quality_score{0.0};
    // Administrator-declared, never auto-detected -- real PII/sensitive-
    // content scanning is Phase 65/74/83, still Planned. Free text so an
    // administrator can note "contains customer emails" etc., not just a
    // bool.
    std::string sensitive_data_status;
    // Real split percentages actually used by evaluate_tabular_model's
    // seeded split (0 = "use that function's own default"). Recorded here
    // so it is administrator-visible/-adjustable per dataset rather than a
    // silent hardcoded constant.
    std::uint32_t train_split_percent{0};
    std::uint32_t validation_split_percent{0};
    std::uint32_t test_split_percent{0};
    // Current DatasetVersionStore version number for this dataset; 0 =
    // no content uploaded yet, no version exists.
    std::uint32_t current_version{0};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class DatasetStore final {
public:
    DatasetStore() = default;
    explicit DatasetStore(RecordStore& records);
    Dataset create(const std::string& owner_id, const std::string& name,
                   const std::string& description,
                   const std::string& subject_area,
                   const std::string& source, const std::string& license,
                   const std::string& data_format,
                   const std::string& purpose = "tabular");
    std::optional<Dataset> find(const std::string& id) const;
    std::vector<Dataset> list() const;
    bool set_approval_status(const std::string& id,
                             DatasetApprovalStatus status);
    // Called once per successful POST .../content, after
    // compute_dataset_content_metrics() has derived the real values from
    // the just-uploaded bytes. Bumps current_version by one -- pair this
    // with DatasetVersionStore::create() at the same call site so the
    // version number here always matches that store's latest entry.
    bool record_content_metrics(const std::string& id,
                                std::uint64_t record_count,
                                std::uint32_t file_count,
                                const std::string& schema_summary,
                                const std::string& content_hash,
                                double duplicate_rate,
                                double data_quality_score);
    // Administrator-declared fields Phase 94 added that content upload
    // cannot derive on its own.
    bool update_declared_metadata(const std::string& id,
                                  const std::string& sensitive_data_status,
                                  std::uint32_t train_split_percent,
                                  std::uint32_t validation_split_percent,
                                  std::uint32_t test_split_percent);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const Dataset& dataset);
    RecordStore* records_{nullptr};
    std::map<std::string, Dataset> datasets_;
    mutable std::mutex mutex_;
};

std::string dataset_json(const Dataset& dataset);
std::string datasets_json(const std::vector<Dataset>& datasets);

// Phase 94: docs/PLAN.md "Machine Learning Abilities" section 11 (Dataset
// Versioning), never implemented before now since it requires a real
// content pipeline to version -- Phase 56 built that pipeline. Every
// successful POST .../content creates one new immutable DatasetVersion
// instead of silently overwriting the previous upload's history;
// DatasetContentStore (which still holds only the current/latest CSV bytes,
// unchanged) and this store together give both "what does training read
// right now" and "what changed, and when." A version's own record/hash
// fields duplicate the ones just written onto Dataset -- that's
// intentional: Dataset always reflects current_version's numbers, but this
// store keeps every prior version's numbers too, which Dataset overwriting
// itself in place could never do.
struct DatasetVersion {
    std::string dataset_id;
    std::uint32_t version{0};
    std::string content_hash;
    std::uint64_t record_count{0};
    std::string uploaded_by;
    std::uint64_t uploaded_at_epoch_seconds{0};
};

class DatasetVersionStore final {
public:
    DatasetVersionStore() = default;
    explicit DatasetVersionStore(RecordStore& records);
    // Assigns the next version number for dataset_id (1 for its first
    // upload) and appends an immutable record -- there is deliberately no
    // update/remove for an individual version, only remove_all() below when
    // the whole dataset is deleted.
    DatasetVersion create(const std::string& dataset_id,
                          const std::string& content_hash,
                          std::uint64_t record_count,
                          const std::string& uploaded_by);
    std::vector<DatasetVersion> list_for_dataset(
        const std::string& dataset_id) const;
    void remove_all_for_dataset(const std::string& dataset_id);

private:
    RecordStore* records_{nullptr};
    // Keyed by dataset_id, each entry append-only in upload order.
    std::map<std::string, std::vector<DatasetVersion>> versions_;
    mutable std::mutex mutex_;
};

std::string dataset_version_json(const DatasetVersion& version);
std::string dataset_versions_json(const std::vector<DatasetVersion>& versions);

// Phase 40: docs/PLAN.md "Machine Learning Abilities" section 12 (Subject
// Knowledge Manager) -- lets an administrator register a subject domain the
// system will eventually be taught (via fine-tuning, RAG, or an embedding
// store), tracked through the same identity/ownership/lifecycle pattern as
// MLProject and Dataset above. Scoped down from the section's full field
// list (approved terminology, definitions, concepts, rules, procedures,
// examples, counterexamples, reference documents, FAQ, required reasoning
// patterns, prohibited conclusions, known limitations, evaluation
// questions, source citations, update schedule) to identity, scope,
// ownership, and review status -- the fields that mean something before the
// Knowledge Ingestion Pipeline (section 13) exists to actually populate a
// subject package's content.
enum class SubjectReviewStatus { draft, in_review, approved, needs_revision, retired };

std::string subject_review_status_name(SubjectReviewStatus status);
SubjectReviewStatus parse_subject_review_status(const std::string& status);

struct SubjectPackage {
    std::string id;
    std::string name;
    std::string description;
    std::string scope;
    std::string target_audience;
    std::string owner_id;
    SubjectReviewStatus review_status{SubjectReviewStatus::draft};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class SubjectPackageStore final {
public:
    SubjectPackageStore() = default;
    explicit SubjectPackageStore(RecordStore& records);
    SubjectPackage create(const std::string& owner_id, const std::string& name,
                          const std::string& description,
                          const std::string& scope,
                          const std::string& target_audience);
    std::optional<SubjectPackage> find(const std::string& id) const;
    std::vector<SubjectPackage> list() const;
    bool set_review_status(const std::string& id, SubjectReviewStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const SubjectPackage& package);
    RecordStore* records_{nullptr};
    std::map<std::string, SubjectPackage> packages_;
    mutable std::mutex mutex_;
};

std::string subject_package_json(const SubjectPackage& package);
std::string subject_packages_json(const std::vector<SubjectPackage>& packages);

// Phase 41: docs/PLAN.md "Machine Learning Abilities" section 14 (Data
// Labeling Interface). Every labeling task targets one dataset already
// registered in DatasetStore above. Scoped down from the section's full
// feature list (label guidelines, keyboard shortcuts, bulk labeling,
// suggested labels, confidence values, disagreement handling, consensus
// review, quality sampling, reviewer accuracy metrics, annotation history,
// label versioning) to identity, the dataset it targets, which of the
// section's labeling modes it uses (free text -- the mode list is 14 items
// and growing, not a closed set worth hardcoding into an enum), an
// optional reviewer assignment, and a lifecycle status. The deferred
// features all require actual label records to operate on, which this
// phase doesn't create.
enum class LabelTaskStatus { queued, in_progress, in_review, completed };

std::string label_task_status_name(LabelTaskStatus status);
LabelTaskStatus parse_label_task_status(const std::string& status);

struct LabelTask {
    std::string id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string label_mode;
    std::string assignee_id;
    std::string owner_id;
    LabelTaskStatus status{LabelTaskStatus::queued};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class LabelTaskStore final {
public:
    LabelTaskStore() = default;
    explicit LabelTaskStore(RecordStore& records);
    LabelTask create(const std::string& owner_id, const std::string& dataset_id,
                     const std::string& name, const std::string& description,
                     const std::string& label_mode,
                     const std::string& assignee_id);
    std::optional<LabelTask> find(const std::string& id) const;
    std::vector<LabelTask> list() const;
    bool set_status(const std::string& id, LabelTaskStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const LabelTask& task);
    RecordStore* records_{nullptr};
    std::map<std::string, LabelTask> tasks_;
    mutable std::mutex mutex_;
};

std::string label_task_json(const LabelTask& task);
std::string label_tasks_json(const std::vector<LabelTask>& tasks);

// Phase 41: docs/PLAN.md "Machine Learning Abilities" section 15 (Data
// Preparation Interface). Each job targets one dataset already registered
// in DatasetStore above and names which of the section's operations
// (remove duplicates, normalize whitespace, redact personal information,
// generate train/validation/test sets, ...) it runs -- again free text
// rather than a closed enum, matching LabelTask's label_mode above, since
// section 15 lists 28 operations and reproducible pipelines will need to
// compose them, not just pick one. Scoped down to identity, the dataset it
// targets, the operation, and a lifecycle status; the pipeline-step
// composition, logging, and reproducibility record section 15 also
// requires belongs to the phase that actually executes a pipeline.
enum class DataPreparationJobStatus { pending, running, completed, failed };

std::string data_preparation_job_status_name(DataPreparationJobStatus status);
DataPreparationJobStatus parse_data_preparation_job_status(
    const std::string& status);

struct DataPreparationJob {
    std::string id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string operation;
    std::string owner_id;
    DataPreparationJobStatus status{DataPreparationJobStatus::pending};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class DataPreparationJobStore final {
public:
    DataPreparationJobStore() = default;
    explicit DataPreparationJobStore(RecordStore& records);
    DataPreparationJob create(const std::string& owner_id,
                              const std::string& dataset_id,
                              const std::string& name,
                              const std::string& description,
                              const std::string& operation);
    std::optional<DataPreparationJob> find(const std::string& id) const;
    std::vector<DataPreparationJob> list() const;
    bool set_status(const std::string& id, DataPreparationJobStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const DataPreparationJob& job);
    RecordStore* records_{nullptr};
    std::map<std::string, DataPreparationJob> jobs_;
    mutable std::mutex mutex_;
};

std::string data_preparation_job_json(const DataPreparationJob& job);
std::string data_preparation_jobs_json(
    const std::vector<DataPreparationJob>& jobs);

// Phase 42: docs/PLAN.md "Machine Learning Abilities" section 16 (Training
// Jobs). Each job belongs to an MLProject and targets a dataset already
// registered in DatasetStore, mirroring LabelTask/DataPreparationJob's
// required-target-id pattern above. Scoped down from the section's full
// field list (compute target, hardware allocation, runtime environment,
// container image, hyperparameters, environment variables, secrets
// references, output directory, checkpoint/logging/notification policy,
// resource/cost ceilings, failure-recovery strategy) to identity, the
// project/model/dataset it relates to, which training method it uses (free
// text -- section 17 lists 20 methods depending on model architecture, not
// a closed set worth hardcoding), and a lifecycle status. None of the
// deferred fields mean anything before an actual training executor exists
// to consume them; model_id is likewise optional since a job may target a
// model not yet registered (Model Builder, still planned). The status enum
// matches section 16's eleven states exactly, including the operator
// verbs (queued/preparing/canceling/...) a real job scheduler would report,
// even though nothing here schedules or runs a job yet -- only records
// intent and lets an administrator move it through the same states by
// hand, matching Start/Pause/Resume/Stop/Archive/Delete from section 16's
// "Administrators should be able to" list.
enum class TrainingJobStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    canceling,
    canceled,
    failed,
    completed,
    awaiting_evaluation,
    archived
};

std::string training_job_status_name(TrainingJobStatus status);
TrainingJobStatus parse_training_job_status(const std::string& status);

struct TrainingJob {
    std::string id;
    std::string project_id;
    std::string model_id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string training_type;
    std::string owner_id;
    TrainingJobStatus status{TrainingJobStatus::draft};
    // Phase 95: docs/PLAN.md section 16's remaining fields, split into two
    // groups (see execute_training_job()'s comment in server.cpp for
    // exactly how the first group is enforced):
    //   - Genuinely enforced by this in-process executor: max_runtime_seconds
    //     (0 = no cap) aborts a run that overshoots it and marks the job
    //     failed with reason "timeout"; failure_recovery_strategy
    //     ("none" | "retry_once") controls whether a failed run is retried
    //     once for real before giving up; checkpoint_frequency_epochs (0 =
    //     use the run's own default stride) overrides the existing
    //     checkpoint-stride computation; output_directory names where this
    //     job's checkpoint/artifact records are attributed (informational --
    //     the actual storage location is still TrainedModelStore/
    //     TrainingCheckpointStore, unchanged).
    //   - Recorded for operator reference only, because this server always
    //     trains in-process on the host it runs on -- there is no container/
    //     cluster scheduler for these to target, so inventing execution
    //     behavior for them would misrepresent what actually runs:
    //     compute_target, hardware_allocation, runtime_environment,
    //     container_image, environment_variables, secrets_references,
    //     logging_policy, notification_policy, resource_ceiling_notes,
    //     cost_ceiling_notes.
    std::uint64_t max_runtime_seconds{0};
    std::string failure_recovery_strategy{"none"};
    std::uint32_t checkpoint_frequency_epochs{0};
    std::string output_directory;
    std::string compute_target;
    std::string hardware_allocation;
    std::string runtime_environment;
    std::string container_image;
    std::string environment_variables;
    std::string secrets_references;
    std::string logging_policy;
    std::string notification_policy;
    std::string resource_ceiling_notes;
    std::string cost_ceiling_notes;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class TrainingJobStore final {
public:
    TrainingJobStore() = default;
    explicit TrainingJobStore(RecordStore& records);
    TrainingJob create(const std::string& owner_id,
                       const std::string& project_id,
                       const std::string& model_id,
                       const std::string& dataset_id,
                       const std::string& name,
                       const std::string& description,
                       const std::string& training_type);
    std::optional<TrainingJob> find(const std::string& id) const;
    std::vector<TrainingJob> list() const;
    bool set_status(const std::string& id, TrainingJobStatus status);
    // Post-creation edits to the Phase 95 execution-policy fields --
    // separate from set_status the same way ExperimentStore separates
    // status transitions from update_metadata().
    bool update_execution_policy(
        const std::string& id, std::uint64_t max_runtime_seconds,
        const std::string& failure_recovery_strategy,
        std::uint32_t checkpoint_frequency_epochs,
        const std::string& output_directory,
        const std::string& compute_target,
        const std::string& hardware_allocation,
        const std::string& runtime_environment,
        const std::string& container_image,
        const std::string& environment_variables,
        const std::string& secrets_references,
        const std::string& logging_policy,
        const std::string& notification_policy,
        const std::string& resource_ceiling_notes,
        const std::string& cost_ceiling_notes);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const TrainingJob& job);
    RecordStore* records_{nullptr};
    std::map<std::string, TrainingJob> jobs_;
    mutable std::mutex mutex_;
};

std::string training_job_json(const TrainingJob& job);
std::string training_jobs_json(const std::vector<TrainingJob>& jobs);

// Phase 43: docs/PLAN.md "Machine Learning Abilities" section 23
// (Evaluation Lab). Every model must be evaluated before approval or
// deployment; an EvaluationRun records that a model was (or is being)
// scored against a benchmark dataset. Mirrors TrainingJob's required-
// target-id pattern above: model_id and dataset_id are both mandatory,
// since an evaluation run without a model to score or a benchmark to
// score it against means nothing. category is free text rather than a
// closed enum -- section 23 lists 24 evaluation categories (accuracy,
// F1 score, hallucination rate, safety compliance, ...) and a real
// evaluation can report more than one, so this is scoped to the single
// category a given run is organized around, not a full metrics report.
// Scoped down from the section's full surface (standard/custom benchmark
// sets, human evaluation, pairwise/blind model comparison, automated
// scoring, reviewer notes, and the actual numeric score) to identity, the
// model/dataset it relates to, the category, and a lifecycle status --
// none of the deferred fields mean anything before an actual evaluation
// harness exists to produce a score.
enum class EvaluationRunStatus { queued, running, completed, failed, canceled };

std::string evaluation_run_status_name(EvaluationRunStatus status);
EvaluationRunStatus parse_evaluation_run_status(const std::string& status);

struct EvaluationRun {
    std::string id;
    std::string model_id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string category;
    std::string owner_id;
    EvaluationRunStatus status{EvaluationRunStatus::queued};
    // Phase 96: names a real feature column (post-encoding name, e.g. a
    // one-hot "region=west" slot) in the run's dataset to break the primary
    // metric down by -- see TabularEvaluationMetrics::bias_fairness_report's
    // comment. Empty (the default) means no bias/fairness breakdown is
    // computed for this run.
    std::string sensitive_feature_name;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class EvaluationRunStore final {
public:
    EvaluationRunStore() = default;
    explicit EvaluationRunStore(RecordStore& records);
    EvaluationRun create(const std::string& owner_id,
                         const std::string& model_id,
                         const std::string& dataset_id,
                         const std::string& name,
                         const std::string& description,
                         const std::string& category,
                         const std::string& sensitive_feature_name = {});
    std::optional<EvaluationRun> find(const std::string& id) const;
    std::vector<EvaluationRun> list() const;
    bool set_status(const std::string& id, EvaluationRunStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const EvaluationRun& run);
    RecordStore* records_{nullptr};
    std::map<std::string, EvaluationRun> runs_;
    mutable std::mutex mutex_;
};

std::string evaluation_run_json(const EvaluationRun& run);
std::string evaluation_runs_json(const std::vector<EvaluationRun>& runs);

// Phase 44: docs/PLAN.md "Machine Learning Abilities" section 25
// (Experiment Tracking). Every training and evaluation run should be
// recorded as an experiment tying a project, model, and (optionally)
// dataset together under one auditable identity. Mirrors TrainingJob's
// required-project pattern and EvaluationRun's required-model pattern:
// project_id and model_id are both mandatory, since an experiment without
// a project to organize it or a model it concerns means nothing; dataset_id
// is optional the same way TrainingJob's model_id is, since not every
// experiment (e.g. a pure hyperparameter sweep note) is tied to one
// dataset.
//
// Phase 80: this is now a REAL executor phase, not a scoped-down roster
// record -- the same arc Phase 56/57/70 already gave Training Jobs,
// Evaluation Lab, Model Comparison, and Fine-Tuning. The struct now carries
// every section-25 definition-time field (hyperparameters, random seed,
// source-code/configuration/container version, tags, notes) plus the
// start/completion timestamps and failure reason a real run produces.
// Metrics/hardware/runtime/checkpoints/logs/artifacts are run *output*,
// not definition, so they live in ExperimentResultStore below (the same
// identity/content split DatasetStore/DatasetContentStore and
// ModelComparison/ComparisonResultStore already use) rather than bloating
// this struct's pack/unpack record with fields that don't exist until
// POST .../run has actually executed.
enum class ExperimentStatus { queued, running, completed, failed, canceled };

std::string experiment_status_name(ExperimentStatus status);
ExperimentStatus parse_experiment_status(const std::string& status);

struct Experiment {
    std::string id;
    std::string project_id;
    std::string model_id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string owner_id;
    ExperimentStatus status{ExperimentStatus::queued};
    std::string hyperparameters_json;    // free-form JSON text
    std::uint32_t random_seed{0};        // 0 = "use the run's own default"
    std::string source_code_version;
    std::string configuration_version;
    std::string container_version;
    std::string tags;                    // comma-joined free text
    std::string notes;
    std::uint64_t started_at_epoch_seconds{0};
    std::uint64_t completed_at_epoch_seconds{0};
    std::string failure_reason;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ExperimentStore final {
public:
    ExperimentStore() = default;
    explicit ExperimentStore(RecordStore& records);
    Experiment create(const std::string& owner_id,
                      const std::string& project_id,
                      const std::string& model_id,
                      const std::string& dataset_id,
                      const std::string& name,
                      const std::string& description,
                      const std::string& hyperparameters_json = {},
                      std::uint32_t random_seed = 0,
                      const std::string& source_code_version = {},
                      const std::string& configuration_version = {},
                      const std::string& container_version = {},
                      const std::string& tags = {},
                      const std::string& notes = {});
    std::optional<Experiment> find(const std::string& id) const;
    std::vector<Experiment> list() const;
    bool set_status(const std::string& id, ExperimentStatus status);
    // Marks a run's real start/end -- distinct from updated_at, which
    // every mutation touches; these two only move when POST .../run
    // genuinely begins/finishes executing.
    bool mark_started(const std::string& id);
    bool mark_completed(const std::string& id, ExperimentStatus status,
                        const std::string& failure_reason);
    // Post-creation edits to the definition-time fields that legitimately
    // evolve after an experiment exists -- notes/tags accrue over an
    // experiment's life, and hyperparameters/version strings may be
    // refined before the next run.
    bool update_metadata(const std::string& id,
                         const std::string& hyperparameters_json,
                         std::uint32_t random_seed,
                         const std::string& source_code_version,
                         const std::string& configuration_version,
                         const std::string& container_version,
                         const std::string& tags, const std::string& notes);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const Experiment& experiment);
    RecordStore* records_{nullptr};
    std::map<std::string, Experiment> experiments_;
    mutable std::mutex mutex_;
};

std::string experiment_json(const Experiment& experiment);
std::string experiments_json(const std::vector<Experiment>& experiments);

// Stored results of executed experiment runs, keyed by Experiment id -- the
// same opaque-JSON-blob pattern EvaluationResultStore/ComparisonResultStore
// already use for output that only means something once a real run has
// produced it (training/validation/evaluation metrics, hardware, runtime,
// checkpoint ids, logs, artifact references).
class ExperimentResultStore final {
public:
    ExperimentResultStore() = default;
    explicit ExperimentResultStore(RecordStore& records);
    void put(const std::string& experiment_id, const std::string& result_json);
    std::optional<std::string> find(const std::string& experiment_id) const;
    bool remove(const std::string& experiment_id);

private:
    RecordStore* records_{nullptr};
};

// experiment_result_json()/experiments_comparison_json() are declared
// further below (alongside ComparisonResultStore), once TabularTrainingReport/
// TabularEvaluationMetrics/HardwareInfo exist -- they need those complete
// types, which this header only defines later.

// Phase 45: docs/PLAN.md "Machine Learning Abilities" section 18
// (Fine-Tuning Interface). Fine-tuning always starts from an existing base
// model and an existing fine-tuning dataset -- unlike TrainingJob's
// optional model_id (a training job may target a model not yet
// registered), a FineTuningJob without a base model to adapt or a dataset
// to adapt it with means nothing, so both model_id and dataset_id are
// mandatory here. project_id stays optional, mirroring TrainingJob's own
// optional field, since a one-off fine-tuning run need not belong to a
// tracked project. method is free text rather than a closed enum --
// section 18 lists ten presets (general instruction tuning, subject
// specialisation, code assistant, classification, question answering,
// conversation style, tool-use behavior, structured-output generation,
// safety alignment, terminology adaptation) and a real fine-tuning job may
// use a preset the list doesn't name, so this records whichever one an
// administrator intends rather than constraining it. Scoped down from the
// section's full surface (base model version, subject package, adapter
// method, target layers, learning rate, batch size, epoch count, context
// length, precision, checkpoint strategy, validation dataset, safety
// dataset, output model name/version, hardware/storage estimate) to
// identity, the project/model/dataset it relates to, the method, and a
// lifecycle status -- none of the deferred fields mean anything before an
// actual fine-tuning executor exists to consume them. The status enum
// reuses TrainingJob's eleven states (fine-tuning is a training-job
// variant per section 17/18's overlap) rather than EvaluationRun/
// Experiment's simpler five, since a real fine-tuning run goes through the
// same queued/preparing/running/paused/canceling lifecycle a training job
// does.
enum class FineTuningJobStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    canceling,
    canceled,
    failed,
    completed,
    awaiting_evaluation,
    archived
};

std::string fine_tuning_job_status_name(FineTuningJobStatus status);
FineTuningJobStatus parse_fine_tuning_job_status(const std::string& status);

struct FineTuningJob {
    std::string id;
    std::string project_id;
    std::string model_id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string method;
    std::string owner_id;
    FineTuningJobStatus status{FineTuningJobStatus::draft};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class FineTuningJobStore final {
public:
    FineTuningJobStore() = default;
    explicit FineTuningJobStore(RecordStore& records);
    FineTuningJob create(const std::string& owner_id,
                         const std::string& project_id,
                         const std::string& model_id,
                         const std::string& dataset_id,
                         const std::string& name,
                         const std::string& description,
                         const std::string& method);
    std::optional<FineTuningJob> find(const std::string& id) const;
    std::vector<FineTuningJob> list() const;
    bool set_status(const std::string& id, FineTuningJobStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const FineTuningJob& job);
    RecordStore* records_{nullptr};
    std::map<std::string, FineTuningJob> jobs_;
    mutable std::mutex mutex_;
};

std::string fine_tuning_job_json(const FineTuningJob& job);
std::string fine_tuning_jobs_json(const std::vector<FineTuningJob>& jobs);

// Phase 73: real LLM LoRA fine-tuning, distinguished from Phase 70's
// tabular fine-tuning path by a `FineTuningJob.method` value starting with
// the "llm:" prefix (e.g. "llm:code assistant") -- a free-text convention,
// matching this codebase's existing "free text, not a closed enum" choice
// for `method` itself, rather than a second status/type field. Everything
// below shells out to administrator-vendored llama.cpp tooling
// (AppConfig::llama_finetune_executable / llama_export_lora_executable);
// this codebase does not implement transformer backpropagation itself.
// Exact CLI flags are version-dependent -- llama.cpp's finetune/
// export-lora tooling interface has changed across releases -- so
// LlmFineTuneOptions::extra_finetune_arguments/extra_export_lora_arguments
// let an administrator adapt to whatever their vendored build actually
// expects; a non-zero exit from either tool surfaces that tool's own
// stderr tail as the error, never a fabricated success.
struct LlmFineTuneOptions {
    std::uint32_t epochs{1};
    double learning_rate{1e-4};
    std::string extra_finetune_arguments;
    std::string extra_export_lora_arguments;
};

struct LlmFineTuneResult {
    std::filesystem::path adapter_gguf;
    std::filesystem::path merged_gguf;
    std::string finetune_log_tail;
    std::string export_lora_log_tail;
};

// Runs llama-finetune against base_model_gguf/training_text_path to
// produce a LoRA adapter under output_directory, then llama-export-lora to
// merge that adapter into a new standalone GGUF (also under
// output_directory). Throws std::runtime_error -- naming which tool and
// why -- if either executable is not configured, does not exist, or exits
// non-zero.
LlmFineTuneResult run_llama_lora_finetune(
    const AppConfig& configuration, const std::filesystem::path& base_model_gguf,
    const std::filesystem::path& training_text_path,
    const std::filesystem::path& output_directory,
    const LlmFineTuneOptions& options, const std::atomic_bool& cancellation);

// Converts a dataset's stored CSV content into the plain instruction/
// response text format llama.cpp's finetune tooling consumes (one
// "### Instruction:\n<...>\n### Response:\n<...>\n\n" block per row).
// Scoped-down and honest: only meaningful for datasets whose CSV header
// already has a column named "instruction" or "prompt" and one named
// "response", "output", or "completion" (case-insensitive) -- throws
// std::runtime_error naming the missing column otherwise, rather than
// guessing which columns to use.
std::filesystem::path write_llm_finetune_training_text(
    const std::string& csv, const std::filesystem::path& output_path);

// Result of validating a dataset's CSV content against the same
// instruction/response column rules write_llm_finetune_training_text
// enforces, without writing anything to disk -- used at dataset content
// upload time for a purpose="instruction" Dataset (see the Dataset::purpose
// comment above) so bad content is rejected at upload, not at training time.
struct InstructionDatasetProfile {
    std::size_t rows{0};
    std::string instruction_column;
    std::string response_column;
};

InstructionDatasetProfile validate_instruction_dataset_csv(
    const std::string& csv, std::uint64_t maximum_csv_bytes);
std::string instruction_dataset_profile_json(
    const std::string& dataset_id, const InstructionDatasetProfile& profile);

// Persisted outcome of the most recent LLM LoRA fine-tuning attempt for one
// FineTuningJob, keyed by job id -- the async counterpart to
// EvaluationResultStore above, since a real llama.cpp finetune run can take
// far longer than the HTTP request timeout and therefore runs on a
// detached background thread (see run_llm_fine_tuning_job in server.cpp),
// with this store as the only way a caller polling GET .../llm-result
// learns the outcome.
class FineTuningRunResultStore final {
public:
    FineTuningRunResultStore() = default;
    explicit FineTuningRunResultStore(RecordStore& records);
    void put(const std::string& job_id, const std::string& result_json);
    std::optional<std::string> find(const std::string& job_id) const;
    bool remove(const std::string& job_id);

private:
    RecordStore* records_{nullptr};
};

// Phase 46: docs/PLAN.md "Machine Learning Abilities" section 9 (Model
// Builder Interface), now implemented at full surface. The interface guides
// an administrator through architecture, layer, tokenizer, optimiser,
// scheduling, and reproducibility configuration (section 9's second list,
// carried by ModelBuilderSettings below) for eleven starting points
// (template, existing architecture, imported base model, previous model
// version, classical ML, neural network, language-model adaptation,
// embedding model, reranking model, vision model, audio model -- section
// 9's first list). The starting point stays free text, like FineTuningJob's
// method, since an administrator may describe a starting point the list
// doesn't name. Both project_id and base_model_id stay optional -- a
// from-template or from-scratch build has neither a tracked project nor an
// existing model to start from, unlike FineTuningJob where the base model
// is mandatory. The status enum is its own five states rather than reusing
// FineTuningJobStatus/TrainingJobStatus's eleven, because a builder
// configuration is a design-time draft, not a running job: it never
// queues, runs, or pauses, it only moves from draft through configuration
// to a submitted training/fine-tuning request or an archived discard.
enum class ModelBuilderConfigStatus {
    draft,
    configuring,
    ready,
    submitted,
    archived
};

std::string model_builder_config_status_name(ModelBuilderConfigStatus status);
ModelBuilderConfigStatus parse_model_builder_config_status(const std::string& status);

// Full-surface completion of Phase 46: the complete configuration list from
// docs/PLAN.md section 9's second list ("The builder should allow
// configuration of: ..."), plus the section's required basic/advanced
// configuration mode. Text fields stay free text (an administrator may name
// any architecture, optimiser, or scheduler; MasterAI does not curate a
// closed catalogue), numeric fields use 0 to mean "not set / use executor
// default", and the two genuinely two-state settings (mixed precision,
// early stopping) are booleans. "Vocabulary and tokenizer" is one field
// because section 9 lists it as one item. These settings describe the
// intended build; the model-construction executor that consumes them is a
// separate future phase.
struct ModelBuilderSettings {
    std::string configuration_mode{"basic"};  // "basic" or "advanced"
    // Architecture group.
    std::string architecture;
    std::string layer_configuration;
    std::uint64_t hidden_dimensions{0};
    std::string attention_configuration;
    std::string vocabulary_tokenizer;
    std::uint64_t sequence_length{0};
    std::string activation_functions;
    double dropout{0.0};  // 0.0 .. 1.0
    std::string initialisation_strategy;
    // Training group.
    std::string loss_function;
    std::string optimiser;
    std::string learning_rate_scheduler;
    std::uint64_t batch_size{0};
    std::uint64_t epoch_count{0};
    std::uint64_t gradient_accumulation{0};  // accumulation steps
    double gradient_clipping{0.0};           // max gradient norm, 0 = off
    // L1 (lasso) and L2 (ridge) weight-decay penalty strengths, both
    // 0 = off. Threaded through to TabularTrainingOptions::
    // regularization_l1/regularization_l2 by resolve_training_architecture()
    // (server.cpp) exactly like dropout/gradient_clipping above.
    double l1_regularization{0.0};
    double l2_regularization{0.0};
    bool mixed_precision{false};
    std::uint64_t checkpoint_frequency{0};  // checkpoints every N steps
    std::uint64_t validation_frequency{0};  // validations every N steps
    bool early_stopping{false};
    // Reproducibility group.
    std::uint64_t random_seed{0};
    std::string reproducibility_settings;
    std::string distributed_training_settings;
};

struct ModelBuilderConfig {
    std::string id;
    std::string project_id;
    std::string base_model_id;
    std::string name;
    std::string description;
    std::string source_type;
    std::string owner_id;
    ModelBuilderConfigStatus status{ModelBuilderConfigStatus::draft};
    ModelBuilderSettings settings;
    // This pass (closing Phase 46's real-executor gap): the dataset
    // run_model_builder_config() (server.cpp) hands to TrainingJobStore::
    // create() when this configuration is submitted. Optional at draft/
    // configuring time -- an administrator may still be designing settings
    // before picking training data -- but required before submission,
    // since the hand-off trainer cannot train on nothing; submission fails
    // clearly rather than silently if it is still empty.
    std::string dataset_id;
    // Set by run_model_builder_config() on a successful submission: the id
    // of the real TrainingJob this configuration produced. Empty until
    // then. This is how a "submitted" ModelBuilderConfig points at the
    // genuine job doing the actual training, instead of submission being a
    // status flip with nothing real behind it.
    std::string resulting_training_job_id;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ModelBuilderConfigStore final {
public:
    ModelBuilderConfigStore() = default;
    explicit ModelBuilderConfigStore(RecordStore& records);
    ModelBuilderConfig create(const std::string& owner_id,
                              const std::string& project_id,
                              const std::string& base_model_id,
                              const std::string& name,
                              const std::string& description,
                              const std::string& source_type,
                              const std::string& dataset_id = {});
    std::optional<ModelBuilderConfig> find(const std::string& id) const;
    std::vector<ModelBuilderConfig> list() const;
    bool set_status(const std::string& id, ModelBuilderConfigStatus status);
    // Replaces a configuration's build settings wholesale (the web UI always
    // submits the complete settings form, pre-filled from current values, so
    // partial merge semantics are unnecessary). Returns false for an unknown
    // id; throws std::invalid_argument for out-of-range values.
    bool configure(const std::string& id, const ModelBuilderSettings& settings);
    // This pass: lets an administrator attach/change the target dataset
    // after creation (the web UI's build-settings form flow finalizes a
    // dataset choice alongside the rest of the settings, not necessarily at
    // creation time). Returns false for an unknown id.
    bool set_dataset(const std::string& id, const std::string& dataset_id);
    // Called by run_model_builder_config() (server.cpp) once submission
    // has created the real TrainingJob -- records which job now owns the
    // actual training. Returns false for an unknown id.
    bool attach_training_job(const std::string& id,
                             const std::string& training_job_id);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const ModelBuilderConfig& config);
    RecordStore* records_{nullptr};
    std::map<std::string, ModelBuilderConfig> configs_;
    mutable std::mutex mutex_;
};

std::string model_builder_config_json(const ModelBuilderConfig& config);
std::string model_builder_configs_json(const std::vector<ModelBuilderConfig>& configs);

// docs/PLAN.md section 9: "The interface must provide basic and advanced
// configuration modes" -- those two modes are the whole closed set, and the
// two fractional settings have hard numeric ranges, so configure() rejects
// out-of-range values instead of persisting nonsense. Declared here (rather
// than staying file-local to ml.cpp) so run_model_builder_config()
// (server.cpp) can re-validate a stored configuration's settings at
// submission time using the exact same rule configure() already enforced
// at save time, instead of duplicating the checks.
void validate_model_builder_settings(const ModelBuilderSettings& settings);

// Phase 47: docs/PLAN.md "Machine Learning Abilities" section 19 (Prompt and
// Instruction Training). Every instruction example targets one dataset
// already registered in DatasetStore, mirroring LabelTask/DataPreparationJob's
// required-target-id pattern above. Scoped down from the section's full
// record (system instruction, user instruction, context, expected response,
// rejected response, tool calls, tool results, required output format,
// difficulty, safety classification) to identity, the dataset it targets,
// a free-text subject classification (the one field of the full record that
// is itself organizational metadata rather than example content, so it
// carries over even at this scoped-down level, matching category/method's
// precedent on EvaluationRun/FineTuningJob above), and a lifecycle status --
// none of the deferred content fields mean anything before an actual
// example record exists to hold them. The status enum tracks section 19's
// own reviewer workflow ("Generated training examples must require approval
// before entering an approved dataset") rather than reusing TrainingJob's
// eleven-state job lifecycle, since an instruction example never queues,
// runs, or pauses -- it only moves from draft through review to an approved
// or rejected outcome, or an archived discard, the same shape as
// ModelBuilderConfigStatus above.
enum class InstructionExampleStatus {
    draft,
    in_review,
    approved,
    rejected,
    archived
};

std::string instruction_example_status_name(InstructionExampleStatus status);
InstructionExampleStatus parse_instruction_example_status(
    const std::string& status);

struct InstructionExample {
    std::string id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string subject_classification;
    std::string owner_id;
    InstructionExampleStatus status{InstructionExampleStatus::draft};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class InstructionExampleStore final {
public:
    InstructionExampleStore() = default;
    explicit InstructionExampleStore(RecordStore& records);
    InstructionExample create(const std::string& owner_id,
                              const std::string& dataset_id,
                              const std::string& name,
                              const std::string& description,
                              const std::string& subject_classification);
    std::optional<InstructionExample> find(const std::string& id) const;
    std::vector<InstructionExample> list() const;
    bool set_status(const std::string& id, InstructionExampleStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const InstructionExample& example);
    RecordStore* records_{nullptr};
    std::map<std::string, InstructionExample> examples_;
    mutable std::mutex mutex_;
};

std::string instruction_example_json(const InstructionExample& example);
std::string instruction_examples_json(
    const std::vector<InstructionExample>& examples);

// Phase 81: the real content record Phase 47's class comment above deferred
// -- section 19's full instruction-record body. Kept as a separate store
// keyed by InstructionExample id rather than folded into the struct above,
// the same identity/content split DatasetStore/DatasetContentStore already
// use: the small fixed-shape metadata record stays cheap to list, and the
// free-form example body (which can legitimately be large -- a full
// system/user/context/expected/rejected/tool-call/tool-result bundle) lives
// as its own opaque-by-id record. tool_calls_json/tool_results_json are
// free-form JSON text rather than a typed structure, matching
// hyperparameters_json's precedent on Experiment (Phase 80) above --
// section 19 doesn't fix a schema for tool calls, so this doesn't invent
// one either.
struct InstructionExampleContent {
    std::string system_instruction;
    std::string user_instruction;
    std::string context;
    std::string expected_response;
    std::string rejected_response;
    std::string tool_calls_json;
    std::string tool_results_json;
    std::string required_output_format;
    std::string difficulty;
    std::string safety_classification;
};

class InstructionExampleContentStore final {
public:
    InstructionExampleContentStore() = default;
    explicit InstructionExampleContentStore(RecordStore& records);
    void put(const std::string& example_id,
            const InstructionExampleContent& content);
    std::optional<InstructionExampleContent> find(
        const std::string& example_id) const;
    bool remove(const std::string& example_id);

private:
    RecordStore* records_{nullptr};
};

std::string instruction_example_content_json(
    const InstructionExampleContent& content);

// True whether `required_output_format` names JSON (case-insensitive
// "json" appearing anywhere in the field, e.g. "json", "structured json")
// and `expected_response` fails to parse as JSON; `error_detail` is set to
// the parser's message in that case. A format that doesn't name JSON has no
// checkable grammar in this codebase today and always reports valid=true,
// noted honestly rather than fabricating a pass/fail for a format nothing
// here can actually validate.
bool validate_structured_output(const InstructionExampleContent& content,
                                std::string& error_detail);

// A heuristic, not semantic similarity: normalizes (lowercase, collapse
// whitespace) system_instruction+user_instruction+context for both records
// and compares by Jaccard token overlap against a fixed threshold. Two
// examples that read the same to a human but are phrased very differently
// will not be flagged -- this codebase has no embedding-based similarity
// wired into this module, so it does not claim one.
bool instruction_examples_are_near_duplicate(
    const InstructionExampleContent& a, const InstructionExampleContent& b);

// Pairs of example ids within the same set whose content (looked up via
// `find_content`) is near-duplicate per the heuristic above. Examples with
// no content yet are skipped, not falsely flagged.
std::vector<std::pair<std::string, std::string>>
detect_duplicate_instruction_examples(
    const std::vector<InstructionExample>& examples,
    const std::function<std::optional<InstructionExampleContent>(
        const std::string&)>& find_content);

// Pairs of example ids that are near-duplicate on instruction text (per the
// same heuristic) but disagree on required_output_format, or where one's
// expected_response equals another's rejected_response -- a direct,
// explicit contradiction. A heuristic, documented the same way the
// duplicate detector above is: it catches the contradictions this simple
// text comparison can actually see, not every semantic contradiction.
std::vector<std::pair<std::string, std::string>>
detect_contradictory_instruction_examples(
    const std::vector<InstructionExample>& examples,
    const std::function<std::optional<InstructionExampleContent>(
        const std::string&)>& find_content);

// Phase 48: docs/PLAN.md "Machine Learning Abilities" section 20 (Synthetic
// Data Generation). Every synthetic record targets one dataset already
// registered in DatasetStore, the same required-target-id pattern
// InstructionExample above (and LabelTask/DataPreparationJob before it)
// follow. Scoped down from the section's full set of generation operations
// (alternative questions, paraphrases, examples, counterexamples, difficult
// cases, malformed inputs, edge cases, balanced-class samples, code samples,
// unit-test cases, simulated conversations, image variations, tabular
// records) to identity, the dataset it targets, and a free-text
// generation_technique field naming which of those operations produced this
// record -- deliberately not a closed enum, the same free-text-
// classification precedent InstructionExample's subject_classification and
// DataPreparationJob's source-type field set, since section 20's list of
// techniques is open-ended and new ones can appear without a code change.
// The status enum reuses the same five-state reviewer workflow
// InstructionExampleStatus defined above: section 20 requires that generated
// records carry a "human-review status" and "remain distinguishable from
// human-created and real-world data" until reviewed, the same rationale, so
// a synthetic record moves from draft through review to an approved or
// rejected outcome, or an archived discard -- it never queues, runs, or
// pauses the way TrainingJob/FineTuningJob's eleven-state job lifecycle
// would imply.
//
// This pass (docs/PLAN.md's Deployment Manager/Inference Endpoints/Synthetic
// Data completion phase) adds the real generation executor: POST
// .../synthetic-records/generate in server.cpp composes a technique-specific
// prompt and invokes execute_rag_generation exactly as Phase 81's
// instruction-example generator does, then stores the result via
// SyntheticRecordContentStore below -- the same identity/content split
// InstructionExample/InstructionExampleContent use, since the generated body
// (generator model/version, prompt, settings, generated text, confidence,
// source linkage) is free-form and can legitimately be large.
enum class SyntheticRecordStatus {
    draft,
    in_review,
    approved,
    rejected,
    archived
};

std::string synthetic_record_status_name(SyntheticRecordStatus status);
SyntheticRecordStatus parse_synthetic_record_status(const std::string& status);

struct SyntheticRecord {
    std::string id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string generation_technique;
    std::string owner_id;
    SyntheticRecordStatus status{SyntheticRecordStatus::draft};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class SyntheticRecordStore final {
public:
    SyntheticRecordStore() = default;
    explicit SyntheticRecordStore(RecordStore& records);
    SyntheticRecord create(const std::string& owner_id,
                           const std::string& dataset_id,
                           const std::string& name,
                           const std::string& description,
                           const std::string& generation_technique);
    std::optional<SyntheticRecord> find(const std::string& id) const;
    std::vector<SyntheticRecord> list() const;
    bool set_status(const std::string& id, SyntheticRecordStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const SyntheticRecord& record);
    RecordStore* records_{nullptr};
    std::map<std::string, SyntheticRecord> records_by_id_;
    mutable std::mutex mutex_;
};

std::string synthetic_record_json(const SyntheticRecord& record);
std::string synthetic_records_json(const std::vector<SyntheticRecord>& records);

// The real generated body a SyntheticRecord's generate route produces --
// see the class comment above. confidence_score is a documented heuristic
// (1.0 if the generation completed without cancellation and produced
// non-empty text, 0.0 otherwise), not a model-reported probability: this
// codebase has no per-token logprob surfaced through execute_rag_generation
// to compute a real one from, so it is not fabricated as one.
struct SyntheticRecordContent {
    std::string generator_model;
    std::string generator_version;
    std::string prompt;
    std::string generation_settings;
    std::string generated_text;
    double confidence_score{0.0};
    std::string source_record_id;
};

class SyntheticRecordContentStore final {
public:
    SyntheticRecordContentStore() = default;
    explicit SyntheticRecordContentStore(RecordStore& records);
    void put(const std::string& record_id,
            const SyntheticRecordContent& content);
    std::optional<SyntheticRecordContent> find(
        const std::string& record_id) const;
    bool remove(const std::string& record_id);

private:
    RecordStore* records_{nullptr};
};

std::string synthetic_record_content_json(
    const SyntheticRecordContent& content);

// Phase 49: docs/PLAN.md "Machine Learning Abilities" section 21
// (Embeddings and Vector Stores). Scoped down from the section's full field
// list (embedding-model version, vector dimensions, document count, chunk
// count, storage size, index type, security classification, access
// permissions, last rebuild date, associated subject packages/agents/
// deployed models) to identity, an embedding_model field (Phase 61 resolves
// the authored method or a verified inference-model id from the dedicated
// embeddings category), a free-text distance_metric
// field (section 21 lists "Select distance metric" as an operation without
// naming a closed set), and an approval-status lifecycle -- not the full
// document-import/chunking/indexing fields themselves, which are measured by
// KnowledgeIndexStore and its profile rather than hand-entered. A vector store is a standalone
// registered resource like Dataset above, not a target-scoped content
// record like InstructionExample/SyntheticRecord, so it carries no
// required parent id and reuses Dataset's three-state pending/approved/
// rejected approval workflow rather than the five-state reviewer workflow
// content records use, since a vector store is infrastructure to be
// approved for use, not a content item to be reviewed and possibly
// rejected outright.
enum class VectorStoreStatus { pending, approved, rejected };

std::string vector_store_status_name(VectorStoreStatus status);
VectorStoreStatus parse_vector_store_status(const std::string& status);

struct VectorStore {
    std::string id;
    std::string name;
    std::string description;
    std::string embedding_model;
    std::string distance_metric;
    std::string owner_id;
    VectorStoreStatus status{VectorStoreStatus::pending};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class VectorStoreStore final {
public:
    VectorStoreStore() = default;
    explicit VectorStoreStore(RecordStore& records);
    VectorStore create(const std::string& owner_id, const std::string& name,
                       const std::string& description,
                       const std::string& embedding_model,
                       const std::string& distance_metric);
    std::optional<VectorStore> find(const std::string& id) const;
    std::vector<VectorStore> list() const;
    bool set_status(const std::string& id, VectorStoreStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const VectorStore& store);
    RecordStore* records_{nullptr};
    std::map<std::string, VectorStore> stores_;
    mutable std::mutex mutex_;
};

std::string vector_store_json(const VectorStore& store);
std::string vector_stores_json(const std::vector<VectorStore>& stores);

// Phase 50: docs/PLAN.md "Machine Learning Abilities" section 22
// (Retrieval-Augmented Generation). Scoped down from the section's full
// configuration surface (query preprocessing, query rewriting, hybrid-
// search weighting, retrieval count, relevance threshold, metadata
// filters, reranking model, context-size limit, citation requirements,
// response template, fallback behavior, source-priority rules, restricted
// documents, cache behavior, plus the section's whole retrieval-testing
// surface) to identity, a free-text search_strategy field (section 22
// lists "Search strategy" as a configurable operation without naming a
// closed set, matching how VectorStore's distance_metric stays free
// text), an optional vector_store_id referencing a VectorStoreStore entry
// (section 22 lists "Vector store" as a configurable operation, and a
// VectorStoreStore entry is the one real resource this phase can link
// against; optional because a keyword-only retrieval strategy needs no
// vector store), and an approval-status lifecycle -- not the full
// retrieval-testing/reranking/citation pipeline the section describes,
// since that requires a real retrieval executor. A RAG configuration is a
// standalone registered resource like VectorStore above, not a
// target-scoped content record, so it reuses the same three-state
// pending/approved/rejected approval workflow rather than the five-state
// reviewer workflow content records use.
enum class RagConfigStatus { pending, approved, rejected };

std::string rag_config_status_name(RagConfigStatus status);
RagConfigStatus parse_rag_config_status(const std::string& status);

struct RagConfig {
    std::string id;
    std::string name;
    std::string description;
    std::string search_strategy;
    std::string vector_store_id;
    std::string owner_id;
    RagConfigStatus status{RagConfigStatus::pending};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class RagConfigStore final {
public:
    RagConfigStore() = default;
    explicit RagConfigStore(RecordStore& records);
    RagConfig create(const std::string& owner_id, const std::string& name,
                     const std::string& description,
                     const std::string& search_strategy,
                     const std::string& vector_store_id);
    std::optional<RagConfig> find(const std::string& id) const;
    std::vector<RagConfig> list() const;
    bool set_status(const std::string& id, RagConfigStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const RagConfig& config);
    RecordStore* records_{nullptr};
    std::map<std::string, RagConfig> configs_;
    mutable std::mutex mutex_;
};

std::string rag_config_json(const RagConfig& config);
std::string rag_configs_json(const std::vector<RagConfig>& configs);

// Phase 51: docs/PLAN.md "Machine Learning Abilities" section 24
// (Subject Examination System). Scoped down from the section's full
// examination surface (question banks with multiple-choice/short-answer/
// long-answer/code/scenario/troubleshooting/tool-use/retrieval/
// fact-verification question types, computed score suites, per-topic and
// per-difficulty breakdowns, hallucination rate, citation quality, and a
// configurable minimum approval score per subject) to identity, a
// mandatory subject_id referencing a SubjectPackageStore entry (an exam
// only means something against a registered subject package, mirroring
// InstructionExample's required dataset id), a free-text question_format
// field (section 24 lists nine question types as examples, not a closed
// enum), and the five-state reviewer-approval lifecycle content records
// use -- an exam is authored content that a reviewer approves before it
// may examine anything, exactly like an InstructionExample, not
// standalone infrastructure like a VectorStore.
enum class SubjectExamStatus {
    draft,
    in_review,
    approved,
    rejected,
    archived
};

std::string subject_exam_status_name(SubjectExamStatus status);
SubjectExamStatus parse_subject_exam_status(const std::string& status);

struct SubjectExam {
    std::string id;
    std::string subject_id;
    std::string name;
    std::string description;
    std::string question_format;
    std::string owner_id;
    SubjectExamStatus status{SubjectExamStatus::draft};
    // This pass (closing Phase 51's real-executor gap): the exam's real
    // question bank, as a JSON array of {"questionText","expectedAnswer"}
    // objects -- free-form JSON text, the same precedent
    // ModelBuilderConfig's build settings and Experiment's
    // hyperparameters_json already set for this store family, rather than
    // a separate per-question store, since an exam's question count is
    // small and always wanted together with the rest of the record. Set
    // via SubjectExamStore::set_questions() below, not create(), the same
    // create-then-configure split ModelBuilderConfigStore::configure()
    // already uses.
    std::string questions_json;
    // Fraction [0.0, 1.0] of questions a run must answer correctly to pass
    // -- administrator-configurable per exam, defaulting to a reasonable
    // 0.7 (70%) rather than an all-or-nothing 1.0.
    double passing_threshold{0.7};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class SubjectExamStore final {
public:
    SubjectExamStore() = default;
    explicit SubjectExamStore(RecordStore& records);
    SubjectExam create(const std::string& owner_id,
                       const std::string& subject_id,
                       const std::string& name,
                       const std::string& description,
                       const std::string& question_format);
    std::optional<SubjectExam> find(const std::string& id) const;
    std::vector<SubjectExam> list() const;
    bool set_status(const std::string& id, SubjectExamStatus status);
    // Replaces an exam's question bank and passing threshold wholesale,
    // the same replace-wholesale semantics ModelBuilderConfigStore::
    // configure() uses for build settings. questions_json must parse as a
    // JSON array of at least one {"questionText","expectedAnswer"} object
    // -- run_subject_exam() (server.cpp) is the only real consumer, and an
    // exam with no real questions cannot be honestly administered. Throws
    // std::invalid_argument on a malformed bank or an out-of-range
    // threshold; returns false for an unknown id.
    bool set_questions(const std::string& id, const std::string& questions_json,
                       double passing_threshold);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const SubjectExam& exam);
    RecordStore* records_{nullptr};
    std::map<std::string, SubjectExam> exams_;
    mutable std::mutex mutex_;
};

std::string subject_exam_json(const SubjectExam& exam);
std::string subject_exams_json(const std::vector<SubjectExam>& exams);

// The real outcome of one run_subject_exam() (server.cpp) execution --
// which model was examined, the real per-question pass/fail detail, the
// score, and whether it cleared the exam's passing_threshold -- as one
// opaque pre-built JSON blob, keyed by SubjectExam id. The same
// identity/content-split, latest-run-wins shape ExperimentResultStore
// already uses for a different real executor's output (an exam may be
// re-run against different models over time; only the latest run's result
// is kept, matching that precedent exactly rather than inventing history-
// keeping semantics nothing else in this codebase provides).
class SubjectExamResultStore final {
public:
    SubjectExamResultStore() = default;
    explicit SubjectExamResultStore(RecordStore& records);
    void put(const std::string& exam_id, const std::string& result_json);
    std::optional<std::string> find(const std::string& exam_id) const;
    bool remove(const std::string& exam_id);

private:
    RecordStore* records_{nullptr};
};

// A heuristic, not semantic grading: true when `generated_answer`
// (case-insensitive, whitespace-collapsed) contains `expected_answer` as a
// substring, or -- when `expected_answer` is short (at most six
// whitespace-separated tokens, e.g. a single word/number/short phrase
// answer) -- when every one of its tokens appears as a whole word
// somewhere in `generated_answer`. The same class of plain, inspectable
// text-overlap check `instruction_examples_are_near_duplicate()` above
// already uses for a different judgment, not a fabricated "AI grading"
// capability. Returns false for an empty expected answer (nothing
// meaningful to check).
bool subject_exam_answer_matches(const std::string& generated_answer,
                                 const std::string& expected_answer);

// Phase 52: docs/PLAN.md "Machine Learning Abilities" section 26
// (Hyperparameter Optimization). Scoped down from the section's full
// search surface (grid/random/Bayesian/population-based/successive-
// halving strategies over a configurable search space of learning rate,
// batch size, epochs, optimiser, dropout, adapter rank and more, with
// early stopping and enforced resource/time limits) to identity, a
// mandatory training_job_id referencing a TrainingJobStore entry (a
// search tunes an existing training job's configuration, so the job
// reference is required the way FineTuningJob's base model is), a
// free-text strategy field (section 26 lists its search strategies as
// examples, not a closed enum, matching TrainingJob's own free-text
// training_type), and the same eleven-state job lifecycle Training Jobs
// and Fine-Tuning use, since a search queues, runs, pauses, and fails
// like any other job -- not the search-space/trial-history/best-result
// field list that a real search executor will attach once it exists.
enum class HyperparameterSearchStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    canceling,
    canceled,
    failed,
    completed,
    awaiting_evaluation,
    archived
};

std::string hyperparameter_search_status_name(
    HyperparameterSearchStatus status);
HyperparameterSearchStatus parse_hyperparameter_search_status(
    const std::string& status);

struct HyperparameterSearch {
    std::string id;
    std::string training_job_id;
    std::string name;
    std::string description;
    std::string strategy;
    std::string owner_id;
    HyperparameterSearchStatus status{HyperparameterSearchStatus::draft};
    // This pass (closing Phase 52's real-executor gap): the two ranges
    // run_hyperparameter_search() (server.cpp) actually searches, as a
    // small JSON object -- e.g. {"learningRate":{"min":0.01,"max":0.2},
    // "epochs":{"min":50,"max":200}}. Free-form JSON text, the same
    // precedent Experiment's hyperparameters_json field already set on
    // this store family, rather than a typed struct, since the executor
    // parses it directly with JsonValue. An empty value means "use the
    // executor's own documented default range". Only learning_rate and
    // epochs are exposed -- the only two TabularTrainingOptions fields
    // (src/masterai.hpp) that genuinely change what train_tabular_model()
    // learns; test_fraction/seed change the evaluation split, not the
    // search, so they are not tunable knobs here.
    std::string search_space_json;
    // Administrator-requested trial budget; the executor always caps the
    // real number of trials run at kMaxHyperparameterTrials (20,
    // server.cpp) regardless of this value, so a request can never trigger
    // unbounded compute.
    std::uint32_t max_trials{10};
    // Real per-trial results (learning rate, epochs, and the measured
    // held-out score) as a JSON array, written by run_hyperparameter_
    // search() after every genuine train_tabular_model()/
    // evaluate_tabular_model() trial -- the same free-form-JSON-output
    // precedent as ModelBuilderConfig's settings persistence, kept on this
    // flat record rather than a separate result store since the whole
    // trial history is small and always wanted together with the summary
    // fields below.
    std::string trials_json;
    double best_learning_rate{0.0};
    std::uint32_t best_epochs{0};
    // Held-out accuracy (classification) or R-squared (regression) of the
    // best trial -- the real evaluate_tabular_model() metric, never an
    // interpolated or fabricated number.
    double best_score{0.0};
    std::uint32_t trials_run{0};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class HyperparameterSearchStore final {
public:
    HyperparameterSearchStore() = default;
    explicit HyperparameterSearchStore(RecordStore& records);
    HyperparameterSearch create(const std::string& owner_id,
                                const std::string& training_job_id,
                                const std::string& name,
                                const std::string& description,
                                const std::string& strategy,
                                const std::string& search_space_json = {},
                                std::uint32_t max_trials = 10U);
    std::optional<HyperparameterSearch> find(const std::string& id) const;
    std::vector<HyperparameterSearch> list() const;
    bool set_status(const std::string& id, HyperparameterSearchStatus status);
    // Called by run_hyperparameter_search() (server.cpp) once a real
    // search has finished -- persists the real trial history and the best
    // trial's real parameters/score. Returns false for an unknown id.
    bool record_result(const std::string& id, const std::string& trials_json,
                       double best_learning_rate, std::uint32_t best_epochs,
                       double best_score, std::uint32_t trials_run);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const HyperparameterSearch& search);
    RecordStore* records_{nullptr};
    std::map<std::string, HyperparameterSearch> searches_;
    mutable std::mutex mutex_;
};

std::string hyperparameter_search_json(const HyperparameterSearch& search);
std::string hyperparameter_searches_json(
    const std::vector<HyperparameterSearch>& searches);

// Ensemble Methods (2026-08-24): docs/PLAN.md "Machine Learning Abilities"
// -- real bagging/boosting/stacking over the tabular/MLP trainer, closing a
// gap this codebase had no entity or executor for at all before this pass.
// Modeled directly on HyperparameterSearch immediately above: a real
// train-and-evaluate action against an existing TrainingJob's dataset/
// architecture (via the same resolve_training_architecture() lookup), not
// a long-running external job, but given the same eleven-state lifecycle
// every other job-like entity in this family uses for consistency.
enum class EnsembleMethod { bagging, boosting, stacking };
std::string ensemble_method_name(EnsembleMethod method);
EnsembleMethod parse_ensemble_method(const std::string& method);

enum class EnsembleStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    canceling,
    canceled,
    failed,
    completed,
    awaiting_evaluation,
    archived
};
std::string ensemble_status_name(EnsembleStatus status);
EnsembleStatus parse_ensemble_status(const std::string& status);

struct EnsembleModel {
    std::string id;
    std::string project_id;
    std::string training_job_id;
    std::string name;
    std::string description;
    EnsembleMethod method{EnsembleMethod::bagging};
    std::string owner_id;
    EnsembleStatus status{EnsembleStatus::draft};
    // Bagging/boosting: number of members actually trained and combined.
    // Stacking: number of diverse base members (each trained with a
    // different seed for diversity) feeding the meta-learner. Always
    // capped at kMaxEnsembleMembers (server.cpp) regardless of this value.
    std::uint32_t member_count{5};
    // Real held-out accuracy (classification) or R-squared (regression) of
    // the combined ensemble, written by run_ensemble() (server.cpp) --
    // never a fabricated or interpolated number.
    double ensemble_score{0.0};
    // The same metric for one plain model trained on the identical held-out
    // split with the job's own architecture -- a real, apples-to-apples
    // baseline so an administrator can see whether ensembling actually
    // helped on this dataset, not just a bare ensemble number with nothing
    // to compare it against.
    double baseline_score{0.0};
    std::uint32_t members_trained{0};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class EnsembleStore final {
public:
    EnsembleStore() = default;
    explicit EnsembleStore(RecordStore& records);
    EnsembleModel create(const std::string& owner_id,
                         const std::string& project_id,
                         const std::string& training_job_id,
                         const std::string& name,
                         const std::string& description,
                         const std::string& method,
                         std::uint32_t member_count = 5U);
    std::optional<EnsembleModel> find(const std::string& id) const;
    std::vector<EnsembleModel> list() const;
    bool set_status(const std::string& id, EnsembleStatus status);
    // Called by run_ensemble() (server.cpp) once a real ensemble run has
    // finished -- persists the real ensemble/baseline scores. Returns
    // false for an unknown id.
    bool record_result(const std::string& id, double ensemble_score,
                       double baseline_score, std::uint32_t members_trained);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const EnsembleModel& ensemble);
    RecordStore* records_{nullptr};
    std::map<std::string, EnsembleModel> ensembles_;
    mutable std::mutex mutex_;
};

std::string ensemble_json(const EnsembleModel& ensemble);
std::string ensembles_json(const std::vector<EnsembleModel>& ensembles);

// Phase 53: docs/PLAN.md "Machine Learning Abilities" section 28
// (Model Optimization). Scoped down from the section's full operation
// surface (quantization, pruning, distillation, graph optimization,
// operator fusion, weight compression, adapter/checkpoint merging,
// vocabulary reduction, context/cache/batch optimization, runtime
// conversion, plus the quality-loss comparison against the original
// model) to identity, a mandatory model_id referencing a
// ModelRegistryStore entry (an optimization run only means something
// against a registered model), a free-text operation field (section 28
// lists thirteen operations as examples, not a closed enum), and the
// same eleven-state job lifecycle Training Jobs use, since an
// optimization run executes like any other job -- not the
// before/after-comparison field list that a real optimizer executor
// will attach once it exists.
enum class ModelOptimizationStatus {
    draft,
    queued,
    preparing,
    running,
    paused,
    canceling,
    canceled,
    failed,
    completed,
    awaiting_evaluation,
    archived
};

std::string model_optimization_status_name(ModelOptimizationStatus status);
ModelOptimizationStatus parse_model_optimization_status(
    const std::string& status);

struct ModelOptimizationRun {
    std::string id;
    std::string model_id;
    std::string name;
    std::string description;
    std::string operation;
    std::string owner_id;
    ModelOptimizationStatus status{ModelOptimizationStatus::draft};
    // Phase 53/72 unification: the magnitude-pruning threshold
    // run_model_optimization() passes to prune_tabular_model() when
    // operation == "pruning" (the only operation with a real executor
    // today). Administrator-supplied at creation time; defaults to the
    // same 1e-3 the Phase 72 Automation Pipeline "Optimize" stage
    // previously hardcoded, so existing pipeline-triggered runs behave
    // identically.
    double pruning_threshold{1e-3};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ModelOptimizationStore final {
public:
    ModelOptimizationStore() = default;
    explicit ModelOptimizationStore(RecordStore& records);
    ModelOptimizationRun create(const std::string& owner_id,
                                const std::string& model_id,
                                const std::string& name,
                                const std::string& description,
                                const std::string& operation,
                                double pruning_threshold = 1e-3);
    std::optional<ModelOptimizationRun> find(const std::string& id) const;
    std::vector<ModelOptimizationRun> list() const;
    bool set_status(const std::string& id, ModelOptimizationStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const ModelOptimizationRun& run);
    RecordStore* records_{nullptr};
    std::map<std::string, ModelOptimizationRun> runs_;
    mutable std::mutex mutex_;
};

std::string model_optimization_json(const ModelOptimizationRun& run);
std::string model_optimizations_json(
    const std::vector<ModelOptimizationRun>& runs);

// Phase 54: docs/PLAN.md "Machine Learning Abilities" section 33
// (Checkpoint Management). Scoped down from the section's full checkpoint
// record (step, epoch, validation metric, size, hash, parent model,
// dataset/configuration version, retention policy, and the resume/
// compare/promote/download operations) to identity, a mandatory
// training_job_id referencing a TrainingJobStore entry (a checkpoint is
// a child record of the training job that produced it), a free-text
// capture_reason field (section 33 describes automatic epoch/step
// capture alongside manual capture without naming a closed set), and a
// bespoke three-state retention lifecycle: active (subject to normal
// retention), pinned (section 33's "protect" operation -- exempt from
// retention deletion), and archived. This is a retention lifecycle, not
// an approval workflow, so it deliberately does not reuse the pending/
// approved/rejected shape -- nobody "approves" a checkpoint; they keep
// it, protect it, or archive it.
enum class TrainingCheckpointStatus { active, pinned, archived };

std::string training_checkpoint_status_name(TrainingCheckpointStatus status);
TrainingCheckpointStatus parse_training_checkpoint_status(
    const std::string& status);

struct TrainingCheckpoint {
    std::string id;
    std::string training_job_id;
    std::string name;
    std::string description;
    std::string capture_reason;
    std::string owner_id;
    TrainingCheckpointStatus status{TrainingCheckpointStatus::active};
    // Phase 79: the real step/epoch half of section 33's deferred record.
    // `epoch` is the training epoch this checkpoint was captured at (0 for a
    // manually-created checkpoint, which has no training run behind it);
    // `has_snapshot` is true only when a real learned-weight snapshot for
    // this checkpoint id exists in CheckpointModelStore below -- a manual
    // note never sets it, since there is no weight state to attach it to.
    std::uint32_t epoch{0};
    bool has_snapshot{false};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class TrainingCheckpointStore final {
public:
    TrainingCheckpointStore() = default;
    explicit TrainingCheckpointStore(RecordStore& records);
    TrainingCheckpoint create(const std::string& owner_id,
                              const std::string& training_job_id,
                              const std::string& name,
                              const std::string& description,
                              const std::string& capture_reason,
                              std::uint32_t epoch = 0,
                              bool has_snapshot = false);
    std::optional<TrainingCheckpoint> find(const std::string& id) const;
    std::vector<TrainingCheckpoint> list() const;
    bool set_status(const std::string& id, TrainingCheckpointStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const TrainingCheckpoint& checkpoint);
    RecordStore* records_{nullptr};
    std::map<std::string, TrainingCheckpoint> checkpoints_;
    mutable std::mutex mutex_;
};

std::string training_checkpoint_json(const TrainingCheckpoint& checkpoint);
std::string training_checkpoints_json(
    const std::vector<TrainingCheckpoint>& checkpoints);

// Phase 55: docs/PLAN.md "Machine Learning Abilities" section 34
// (Deployment Manager). Scoped down from the section's full deployment
// record (model version, runtime, target node, configuration, time,
// administrator, rollback version, health status, and the direct/
// blue-green/canary/shadow/A-B/rolling strategies across dev/test/
// staging/production/offline/intranet/MCP/desktop/web targets) to
// identity, a mandatory model_id referencing a ModelRegistryStore entry
// (a deployment promotes a registered model and nothing else), free-text
// environment and strategy fields (section 34 lists both sets as
// examples, not closed enums), and the three-state pending/approved/
// rejected approval workflow VectorStore and RagConfig use -- section 34
// explicitly names approval as part of the deployment record, and a
// deployment is a standalone registered resource awaiting authorization,
// not reviewer-workflow content.
//
// This pass (docs/PLAN.md's Deployment Manager/Inference Endpoints/
// Synthetic Data completion phase) adds the health/rollback machinery this
// comment used to say only a real deployment executor would attach --
// Deployment Manager now has its own real deploy/rollback action
// (DeploymentStore::deploy/rollback below, driven by POST .../{id}/deploy
// and .../{id}/rollback in server.cpp), reusing exactly the same
// approved-ModelCard gate AutomationPipeline's "Request approval"/"Deploy"
// stages already enforce (see run_safety_tests_stage's comment in
// server.cpp) so a deployment approved through this module's own API is
// held to the identical bar as one approved through a pipeline run.
// health_status is a real, cheap, honest signal -- whether
// TrainedModelStore holds trained weights for model_id -- not a live
// serving health check (this codebase's live "is this model actually
// answering requests" surface is Inference Endpoints' listener, a
// different resource); a model with no trained tabular artifact (e.g. an
// LLM-backed model reached only through execute_rag_generation) still
// deploys, just with health_status "unverified" rather than a fabricated
// "healthy". Rollback is real state, not merely a status flip: deploying a
// new approved deployment for the same environment supersedes (rejects)
// whichever deployment was previously approved for it and records that
// prior deployment's id as previous_deployment_id, so .../rollback can
// restore it and reject the one being replaced.
enum class DeploymentStatus { pending, approved, rejected };

std::string deployment_status_name(DeploymentStatus status);
DeploymentStatus parse_deployment_status(const std::string& status);

struct Deployment {
    std::string id;
    std::string model_id;
    std::string name;
    std::string description;
    std::string environment;
    std::string strategy;
    std::string owner_id;
    DeploymentStatus status{DeploymentStatus::pending};
    std::string health_status;
    std::uint64_t deployed_at_epoch_seconds{0};
    std::string previous_deployment_id;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class DeploymentStore final {
public:
    DeploymentStore() = default;
    explicit DeploymentStore(RecordStore& records);
    Deployment create(const std::string& owner_id,
                      const std::string& model_id, const std::string& name,
                      const std::string& description,
                      const std::string& environment,
                      const std::string& strategy);
    std::optional<Deployment> find(const std::string& id) const;
    std::vector<Deployment> list() const;
    bool set_status(const std::string& id, DeploymentStatus status);
    // Real deploy action: caller (server.cpp) has already verified
    // `has_approved_model_card` (an approved ModelCard exists for this
    // deployment's model_id) and computed `trained_weights_present` (a
    // TrainedModelStore lookup). Returns false if `id` does not exist or
    // `has_approved_model_card` is false (deploy refused). On success, any
    // other deployment currently `approved` for the same environment is
    // superseded (set to `rejected`, its id recorded on this deployment's
    // previous_deployment_id), then this deployment is set `approved` with
    // health_status set from trained_weights_present and
    // deployed_at_epoch_seconds set to now.
    bool deploy(const std::string& id, bool has_approved_model_card,
               bool trained_weights_present);
    // Requires this deployment to have a non-empty previous_deployment_id
    // (set by a prior deploy() that superseded it). Rejects this
    // deployment and re-approves the previous one (refreshing its
    // deployed_at_epoch_seconds). Returns the id of the deployment that is
    // now active, or an empty string if `id` does not exist or has no
    // previous_deployment_id to roll back to.
    std::string rollback(const std::string& id);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const Deployment& deployment);
    RecordStore* records_{nullptr};
    std::map<std::string, Deployment> deployments_;
    mutable std::mutex mutex_;
};

std::string deployment_json(const Deployment& deployment);
std::string deployments_json(const std::vector<Deployment>& deployments);

// Phase 56: docs/PLAN.md "Machine Learning Abilities" -- the first REAL
// execution layer for the module. Everything above this comment records
// intent; everything below actually computes. The engine trains genuine
// models (gradient-descent linear regression for numeric targets,
// softmax/logistic classification for categorical targets) on tabular CSV
// datasets uploaded against a DatasetStore entry, evaluates them on a
// held-out split with real metrics, persists the learned weights as a
// reloadable artifact tied to a ModelRegistryStore entry, and serves live
// predictions from those weights. Implemented in src/ml_engine.cpp.

// A parsed tabular dataset: with categorical feature encoding off (the
// original, still-default behavior), every feature column must be
// numeric; the target column decides the task (all-numeric -> regression,
// otherwise classification with targets stored as class-label indices).
struct TabularDataset {
    std::vector<std::string> feature_names;
    std::string target_name;
    bool classification{false};
    std::vector<std::string> class_labels;      // classification only
    std::vector<std::vector<double>> features;  // row-major, one row per example
    std::vector<double> targets;                // class index or numeric value
};

// A fitted (or, applied, imposed) one-hot scheme for a dataset's
// non-numeric feature columns. `columns` maps an original CSV header name
// to the ordered, distinct raw string values seen for it -- that order is
// exactly the order parse_tabular_csv() below expands into one-hot
// "columnName=value" feature slots, so it must stay fixed once a model has
// trained against it (see parse_tabular_csv's own comment on "fit" vs.
// "apply" mode). Only columns that were actually non-numeric are present;
// an all-numeric column is never listed here even when categorical
// encoding is on.
struct CategoricalEncoding {
    std::map<std::string, std::vector<std::string>> columns;
    bool empty() const { return columns.empty(); }
};

// Parses CSV text (header row required, quoted fields supported) into a
// TabularDataset. target_column names the label column; empty selects the
// last column. Throws std::runtime_error with a human-readable reason on
// any structural problem (missing column, too few rows, ...).
//
// `encode_categorical_features` (default off, preserving the original
// strict behavior byte-for-byte for every caller that doesn't pass it):
// when on, a feature column that isn't purely numeric is no longer
// rejected -- it is one-hot encoded instead, using `encoding`:
//   - `encoding == nullptr`, or non-null but `encoding->empty()`: FIT mode.
//     Every non-numeric feature column's distinct values (sorted) become
//     its one-hot scheme; when `encoding` is non-null, the fitted scheme
//     is written into it so the caller (a training run) can persist it
//     for later reuse.
//   - `encoding` non-null and non-empty: APPLY mode. Every column named in
//     `encoding->columns` is treated as categorical and expanded using
//     exactly that column's stored value list, regardless of whether this
//     particular CSV's own cells for it look numeric -- this is what keeps
//     an evaluation/comparison/benchmark dataset's feature schema aligned
//     with the model it is being scored against. A value not present in
//     the stored list is a clear "not seen while training this model"
//     error rather than a silent guess. A feature column NOT named in
//     `encoding->columns` still gets the original strict numeric-only
//     treatment, so an evaluation dataset that changed a column from
//     numeric to text (real schema drift, not a legitimate category) is
//     still caught.
TabularDataset parse_tabular_csv(
    const std::string& csv, const std::string& target_column,
    std::uint64_t maximum_csv_bytes = 8ULL * 1024ULL * 1024ULL,
    bool encode_categorical_features = false,
    CategoricalEncoding* encoding = nullptr);

// Dataset content upload completion: Dataset Manager (docs/PLAN.md
// "Machine Learning Abilities" section 10) originally accepted CSV text
// only. These three converters let the same upload route accept JSON,
// JSONL, and Parquet too, each converging on the exact CSV text
// parse_tabular_csv above already validates -- DatasetContentStore and
// every downstream trainer/evaluator/comparator stay CSV-only and
// unchanged. json_records_to_csv is the shared tail: the header is every
// key seen across all records (alphabetical, since JsonValue::Object is a
// std::map), a record missing a key renders that cell empty, and a nested
// array/object field is rejected (the tabular engine has no concept of a
// nested column) rather than silently stringified.
std::string json_records_to_csv(const std::vector<JsonValue>& records);
// Parses `json_text` as a top-level JSON array of flat objects.
std::string json_array_to_csv(const std::string& json_text);
// Parses `jsonl_text` as one JSON object per non-blank line -- the same
// shape parquet_bytes_to_json's DuckDB helper output already is, so a
// Parquet upload is base64-decoded, run through parquet_bytes_to_json, and
// handed to this same function.
std::string jsonl_to_csv(const std::string& jsonl_text);

// Raw uploaded content for one DatasetStore entry, keyed by dataset id.
// Kept as the original CSV plus the chosen target column so training and
// evaluation always re-parse from the exact bytes the administrator
// approved, not a lossy intermediate form.
class DatasetContentStore final {
public:
    struct Content {
        std::string csv;
        std::string target_column;
    };
    DatasetContentStore() = default;
    explicit DatasetContentStore(RecordStore& records);
    void put(const std::string& dataset_id, const std::string& csv,
             const std::string& target_column);
    std::optional<Content> find(const std::string& dataset_id) const;
    bool remove(const std::string& dataset_id);

private:
    RecordStore* records_{nullptr};
};

// JSON profile of a parsed dataset (row/column counts, task, classes) for
// the upload response and the dataset-content GET endpoint.
std::string tabular_dataset_profile_json(const std::string& dataset_id,
                                         const TabularDataset& data);

// Phase 94: real content-derived metrics for Dataset::record_count/
// file_count/schema_summary/content_hash/duplicate_rate/data_quality_score
// (docs/PLAN.md section 10). Computed directly from the raw CSV bytes
// DatasetContentStore stores -- header-agnostic to a dataset's purpose
// (works the same for a tabular target-column CSV and an instruction
// prompt/response CSV), unlike parse_tabular_csv which only tabular
// datasets go through. Never invents a value: an empty/header-only CSV
// yields record_count 0 and data_quality_score 0.
struct DatasetContentMetrics {
    std::uint64_t record_count{0};
    // "columnName:numeric" or "columnName:categorical" per header column,
    // comma-joined in header order -- a column is "numeric" only when every
    // one of its cells across every row parses as a number.
    std::string schema_summary;
    std::string content_hash;  // sha256_hex of the exact csv bytes
    // Exact-duplicate data rows (byte-identical row, header excluded) /
    // total data rows. 0 when there are 0 or 1 data rows.
    double duplicate_rate{0.0};
    // non_null_ratio * (1 - duplicate_rate): the fraction of cells that are
    // non-empty, discounted by how much of the dataset is exact duplicate
    // rows. A simple, stated formula -- not a claim about label
    // correctness, outlier presence, or any deeper notion of "quality"
    // real data-quality tooling would check.
    double data_quality_score{0.0};
};

DatasetContentMetrics compute_dataset_content_metrics(const std::string& csv);

// Content-status-aware dataset listing (ML forms clarity pass): every
// dataset in a listing carries "hasContent" and, when true, an approximate
// "contentRows" count (a cheap newline count of the stored CSV, not a full
// parse), so the web UI can show "content: ready" vs. "content: missing"
// instead of a user discovering the gap only when /run rejects it with
// ml_dataset_has_no_content. Row 0 (the header) is never counted.
std::string datasets_json_with_content_status(
    const std::vector<Dataset>& datasets,
    const DatasetContentStore& content_store);

// Hyperparameters for one training run. Every field has a working default
// so a bare "run" request trains sensibly.
//
// Phase 46 (this pass): the fields below `checkpoint_interval` turn on a
// real multi-layer-perceptron (MLP) training path in train_tabular_model()
// instead of the original plain linear/logistic/softmax regression path.
// `hidden_layer_sizes` empty (the default) means "no hidden layers" and
// train_tabular_model() behaves exactly as it always has -- byte-for-byte
// the same code path, so every existing caller/test is unaffected. A
// non-empty `hidden_layer_sizes` is the only thing that switches on the
// new backprop/mini-batch/optimizer machinery; the fields below it are
// then genuinely consumed (see train_tabular_model()'s own comment for
// exactly how each one is used).
struct TabularTrainingOptions {
    std::uint32_t epochs{200};
    double learning_rate{0.05};
    double test_fraction{0.2};   // held-out share, [0, 0.9]
    std::uint32_t seed{42};      // deterministic shuffle/split
    std::uint32_t checkpoint_interval{50};  // epochs between checkpoints

    // MLP architecture. One entry per hidden layer, in order (e.g. {128,64}
    // is a 128-neuron layer feeding a 64-neuron layer). Empty = classic
    // linear/logistic/softmax regression, the original behavior.
    std::vector<std::uint32_t> hidden_layer_sizes;
    // "relu" | "tanh" | "sigmoid" -- applied after every hidden layer, not
    // the output layer (which stays linear; softmax/sigmoid is applied to
    // its logits the same way the original classifier already does).
    std::string activation{"relu"};
    // Inverted dropout on hidden-layer activations, training time only
    // (never applied by predict_tabular()/evaluate_tabular_model()). 0 = off.
    double dropout{0.0};
    // "sgd" | "sgd_momentum" | "adam". Only meaningful with a non-empty
    // hidden_layer_sizes -- the plain-linear path keeps its original
    // full-batch gradient descent regardless of this field.
    std::string optimiser{"sgd"};
    // Mini-batch size for the MLP path. 0 = full batch (all training rows
    // per epoch, matching the plain-linear path's own behavior).
    std::uint32_t batch_size{0};
    // Max L2 norm of the gradient vector before a batch's update is scaled
    // down to match it. 0 = disabled.
    double gradient_clip_norm{0.0};
    // Number of mini-batches whose gradients are summed before one weight
    // update is applied. 1 = update after every mini-batch (no accumulation).
    std::uint32_t gradient_accumulation_steps{1};
    // "constant" | "step" | "cosine" -- decays the base learning_rate over
    // the run; see train_tabular_model()'s comment for the exact formulas.
    std::string lr_schedule{"constant"};
    // Epochs of no held-out-loss improvement before training stops early.
    // 0 = disabled. Requires test_fraction > 0 to have a held-out loss to
    // judge; falls back to training loss when there is no held-out split.
    std::uint32_t early_stopping_patience{0};
    // "he" | "xavier" | "uniform" -- hidden/output layer weight
    // initialization scheme. Empty/unrecognized defaults to "he" for relu
    // and "xavier" otherwise.
    std::string initialisation;
    // L1 (lasso, drives small weights exactly to zero) and L2 (ridge,
    // shrinks weights proportionally) penalty strengths, applied to every
    // weight in every layer -- output layer and every hidden layer alike,
    // in both the plain linear/logistic/softmax path and the MLP path --
    // but never to a row's bias term (its last column), since penalizing
    // the bias would bias predictions toward zero rather than regularizing
    // the model's sensitivity to its inputs. Both 0 = off (unchanged
    // behavior from before this field existed). Applied as decoupled weight
    // decay at update time (add lambda*sign(w) for L1 and 2*lambda*w for L2
    // directly to the gradient before the optimizer step), not folded into
    // the reported loss -- report.loss_history stays a pure data-fit metric
    // so early stopping and loss curves remain comparable across different
    // regularization_l1/regularization_l2 settings.
    double regularization_l1{0.0};
    double regularization_l2{0.0};
};

// The learned model: standardization statistics plus weight rows (one row
// of n_features+1 values including bias for regression; one row per class
// for classification). This is the artifact that gets persisted and later
// reloaded for evaluation and prediction.
//
// Phase 46 (this pass): `hidden_layers`, when non-empty, is a real trained
// MLP -- one weight matrix per hidden layer (each row is one neuron's
// [w_1..w_prevdim, bias]), applied in order before `weights` (which stays
// the final linear output layer, exactly as it always was). Empty
// `hidden_layers` (the default, and every model trained before this pass)
// means `weights` is applied directly to the standardized input features,
// identical to the original behavior.
struct TrainedTabularModel {
    std::string model_id;         // owning ModelRegistryEntry id
    std::string training_job_id;  // job that produced it
    std::string method;  // "linear_regression" | "logistic_regression" |
                         // "softmax_regression" | "mlp_regression" |
                         // "mlp_classification"
    bool classification{false};
    std::vector<std::string> feature_names;
    std::string target_name;
    std::vector<std::string> class_labels;
    std::vector<double> feature_means;
    std::vector<double> feature_stddevs;
    std::vector<std::vector<double>> weights;
    std::vector<std::vector<std::vector<double>>> hidden_layers;
    std::string activation{"relu"};  // only meaningful when hidden_layers is non-empty
    std::uint64_t trained_at_epoch_seconds{0};
    // The one-hot scheme fitted against the training dataset's non-numeric
    // feature columns, empty when none were categorical (every model
    // trained before this field existed has an empty map here, which is
    // exactly correct for it -- it never saw a categorical column).
    // Evaluation/comparison re-parse a benchmark dataset in "apply" mode
    // against this same scheme (see parse_tabular_csv's own comment) so
    // its feature layout always matches what these `weights` were trained
    // on; prediction uses it to accept the natural category value (e.g.
    // {"record_type":"document"}) instead of requiring the caller to know
    // the internal one-hot feature names.
    CategoricalEncoding categorical_encoding;
};

// Real evaluation metrics computed against actual labels: classification
// reports accuracy plus macro precision/recall/F1 and a confusion matrix;
// regression reports MSE, MAE, and R-squared.
struct TabularEvaluationMetrics {
    bool classification{false};
    std::size_t evaluated_rows{0};
    double accuracy{0.0};
    double macro_precision{0.0};
    double macro_recall{0.0};
    double macro_f1{0.0};
    std::vector<std::vector<std::size_t>> confusion;  // [actual][predicted]
    double mse{0.0};
    double mae{0.0};
    double r_squared{0.0};
    // Phase 96: docs/PLAN.md section 23's remaining categories that are
    // honestly computable for a tabular classifier/regressor -- see
    // evaluate_tabular_model()'s comment in ml_engine.cpp for exactly how
    // each is measured. The categories that only mean something for a
    // generative/LLM evaluation (perplexity, hallucination rate,
    // groundedness, retrieval accuracy, response relevance, code
    // correctness/compilation, adversarial-prompt resistance, ...) are
    // deliberately absent here rather than fabricated -- see this codebase's
    // documented boundary note in docs/HowToUse-MachineLearning.md.
    double latency_ms{0.0};                    // total wall time to score every row
    double throughput_predictions_per_sec{0.0};
    double memory_usage_mb{0.0};  // peak RSS delta measured during scoring
    // 1.0 - (stdev of the primary metric across a few reseeded re-splits of
    // this same evaluation data) / max(1.0, |mean|) -- higher is more
    // stable. 0 rows or a single re-split yields 0 (undefined), never a
    // fabricated "perfectly stable" 1.0.
    double stability_score{0.0};
    // primary metric on the same rows with small injected feature noise,
    // divided by the clean primary metric (capped at 1.0). 0 when the clean
    // metric itself is 0 (undefined ratio).
    double robustness_score{0.0};
    // Empty unless the caller named a real sensitive column present in the
    // evaluation dataset -- per-group breakdown of the primary metric, e.g.
    // {"male": 0.82, "female": 0.79}. Never auto-detected or fabricated;
    // absent, not guessed, when no sensitive column was named.
    std::map<std::string, double> bias_fairness_report;
};

// Outcome of one real training run: the per-epoch loss curve (cross-entropy
// for classification, mean squared error for regression), the split sizes,
// and the held-out metrics (computed on the training rows when the split
// leaves no test rows, flagged by evaluated_on_test).
struct TabularTrainingReport {
    std::vector<double> loss_history;
    double final_loss{0.0};
    std::size_t train_rows{0};
    std::size_t test_rows{0};
    bool evaluated_on_test{false};
    TabularEvaluationMetrics metrics;
};

// Trains by full-batch gradient descent on standardized features when
// `options.hidden_layer_sizes` is empty (the original, unchanged path):
// picks the method from the dataset's task (regression vs 2-class vs
// k-class). Fills `model` (except model_id/training_job_id, which the
// caller owns) and returns the report. Throws std::runtime_error on an
// untrainable dataset.
//
// Phase 46 (this pass): a non-empty `options.hidden_layer_sizes` instead
// trains a real multi-layer perceptron -- forward pass through each hidden
// layer (options.activation, `on_epoch`'s dropout applied only to this
// forward pass, never at evaluation/prediction time) into the same linear
// output layer/softmax the plain path already used, backpropagated via the
// chain rule. `options.batch_size` (0 = full batch), `options.optimiser`
// ("sgd" | "sgd_momentum" | "adam"), `options.gradient_clip_norm` (global
// L2 clip, 0 = off), `options.gradient_accumulation_steps`, and
// `options.lr_schedule` ("constant" | "step": halves every epochs/4 |
// "cosine": cosine anneal to ~0) are all genuinely consumed by this path.
// `options.early_stopping_patience` (0 = off) stops the run early on
// held-out-loss (or training-loss, if there is no held-out split)
// stagnation; the returned report's loss_history simply has fewer entries
// than options.epochs when that happens. `warm_start`'s architecture (if
// it has hidden_layers) always wins over `options.hidden_layer_sizes` --
// fine-tuning/checkpoint-resume must continue the exact architecture that
// was already trained, not silently reshape it.
//
// `warm_start`, when non-null, is Phase 70's real Fine-Tuning executor
// hook: instead of zero-initializing `model.weights`, gradient descent
// continues from `warm_start`'s already-learned weights, so the run
// genuinely adapts an existing model to the fine-tuning dataset rather
// than training a new one from scratch. `warm_start`'s feature schema,
// task, and (for classification) class label set must match `data`
// exactly -- fine-tuning adapts a model to more examples of the same
// problem, not a different one -- and a mismatch throws
// std::runtime_error before any training happens.
// `on_epoch`, when set (Phase 78, this pass), is called synchronously after
// every epoch's weight update with (epoch, this epoch's real loss) -- the
// live per-step training curve the Phase 78 gap note said this codebase
// could not provide, since the tabular trainer's run completes before a
// post-hoc report could ever be sampled mid-run. server.cpp wires this to
// TrainingProgressTracker::update() so a concurrent GET request on another
// connection thread can observe genuinely in-flight epoch/loss values while
// this run is still executing, not just the finished report. Phase 79 widens
// the callback with a third argument, `model` itself, at that exact epoch --
// its weights are already updated and its feature/target/class schema is
// already fixed for the whole run, so this is precisely the state a real
// checkpoint snapshot needs. server.cpp uses it to persist genuine learned
// weights mid-training instead of the previous post-hoc, loss-text-only
// checkpoint record.
TabularTrainingReport train_tabular_model(
    const TabularDataset& data, const TabularTrainingOptions& options,
    TrainedTabularModel& model,
    const TrainedTabularModel* warm_start = nullptr,
    const std::function<void(std::uint32_t, double, const TrainedTabularModel&)>&
        on_epoch = {});

// Phase 78 (this pass): in-process, deliberately non-persisted live
// training-progress state -- the concrete mechanism behind "live per-step
// training curves". A previous run's finished loss_history (TabularTraining
// Report, above) is already real post-hoc data (Phase 56); what did not
// exist was a way to observe an epoch/loss value *while* a run was still
// executing on its own thread. begin()/update()/end() are called by the
// training executor around/inside its train_tabular_model() call (via the
// on_epoch callback for update()); snapshot() is what GET
// /api/v1/ml/training-jobs/{id}/live-progress and build_ml_monitoring_json()
// read. A job with no tracked entry (never started, or already finished and
// cleared) reports running=false rather than stale or fabricated data.
struct TrainingProgressSnapshot {
    bool running{false};
    std::uint32_t current_epoch{0};
    std::uint32_t total_epochs{0};
    double current_loss{0.0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class TrainingProgressTracker final {
public:
    void begin(const std::string& job_id, std::uint32_t total_epochs);
    void update(const std::string& job_id, std::uint32_t epoch, double loss);
    void end(const std::string& job_id) noexcept;
    TrainingProgressSnapshot snapshot(const std::string& job_id) const;
    // Every job this tracker currently considers in-flight (begin() called,
    // end() not yet called) -- what build_ml_monitoring_json() iterates to
    // report every live run at once, not just one job at a time.
    std::vector<std::string> active_job_ids() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, TrainingProgressSnapshot> progress_;
};

std::string training_progress_snapshot_json(const TrainingProgressSnapshot& snapshot);

// Scores an existing model against a dataset with the same schema (feature
// names must match; classification labels must be known to the model).
// Throws std::runtime_error on schema mismatch. `sensitive_feature_name`
// (Phase 96), when non-empty and present in data.feature_names, fills
// bias_fairness_report with the primary metric computed separately for each
// distinct raw value of that feature column; empty/absent leaves that map
// empty rather than guessing which column, if any, is sensitive.
TabularEvaluationMetrics evaluate_tabular_model(
    const TrainedTabularModel& model, const TabularDataset& data,
    const std::string& sensitive_feature_name = {});

// One live prediction. For classification, label/class_probabilities are
// filled and value is the winning class index; for regression, value is
// the predicted number.
struct TabularPrediction {
    double value{0.0};
    std::string label;
    std::vector<double> class_probabilities;
};

// Predicts from raw (unstandardized) feature values ordered like
// model.feature_names. Throws std::runtime_error on arity mismatch.
TabularPrediction predict_tabular(const TrainedTabularModel& model,
                                  const std::vector<double>& features);

// Phase 72: Automation Pipeline "Clean data" stage support. Removes blank
// rows and exact-duplicate data rows from raw CSV text (the header row is
// always kept as-is); reports real before/after row counts rather than a
// fabricated outcome. Throws std::runtime_error if the CSV has no rows at
// all to clean.
struct TabularCleanReport {
    std::string csv;
    std::size_t rows_before{0};
    std::size_t rows_after{0};
    std::size_t blank_rows_removed{0};
    std::size_t duplicate_rows_removed{0};
};
TabularCleanReport clean_tabular_csv(const std::string& csv);

// Real data augmentation for tabular datasets (2026-08-24), producing
// synthetic rows appended to the original data -- never modifying or
// removing an original row. Two families of real, independent operations,
// freely composable:
//   - Word-level text augmentation (the four classic "Easy Data
//     Augmentation"/EDA operations: synonym replacement, random insertion,
//     random deletion, random swap), applied to any feature column that
//     is not fully numeric across the dataset -- the same all-or-nothing
//     numeric test parse_tabular_csv() already uses to decide one-hot vs.
//     reject. Synonym replacement/insertion draw from a small built-in
//     synonym list (see synonym_clusters() in ml_engine.cpp) -- not a full
//     thesaurus/WordNet, an honestly bounded scope.
//   - Numeric augmentation: Gaussian noise jitter scaled to each numeric
//     column's own standard deviation, and classification-only minority-
//     class oversampling (duplicating, optionally jittered, minority-class
//     rows toward a target ratio of the majority class's row count).
// Every random choice this function makes (which words/rows to touch,
// which synonym/noise draw, which minority row to duplicate) is driven by
// a single std::mt19937 seeded from TabularAugmentOptions::seed, so the
// same input CSV and options always produce byte-identical output.
struct TabularAugmentOptions {
    bool synonym_replacement{false};
    bool random_insertion{false};
    bool random_deletion{false};
    bool random_swap{false};
    // Fraction of a text cell's words touched by each enabled word-level
    // operation above (e.g. 0.1 = roughly one word in ten). [0.0, 1.0].
    double text_augmentation_fraction{0.1};
    bool gaussian_noise{false};
    // Noise stddev as a fraction of each numeric column's own stddev.
    double noise_stddev_fraction{0.05};
    // Classification-only (skipped, not an error, when the target column
    // has more than 64 distinct values or only one): duplicates minority-
    // class rows until every class reaches at least this fraction of the
    // majority class's row count. [0.0, 1.0]; 1.0 = fully balanced.
    bool oversample_minority_classes{false};
    double target_minority_ratio{0.5};
    std::uint32_t seed{42};
};
struct TabularAugmentReport {
    std::string csv;
    std::size_t rows_before{0};
    std::size_t rows_after{0};
    std::size_t synthetic_rows_added{0};
};
TabularAugmentReport augment_tabular_csv(const std::string& csv,
                                         const std::string& target_column,
                                         const TabularAugmentOptions& options);

// Phase 72: Automation Pipeline "Split data" stage support. Parses the CSV
// for real (so a malformed dataset fails the same way "Validate data"
// does) and reports the exact train/holdout row counts
// train_tabular_model's own internal deterministic split would produce for
// the same holdout_fraction -- not a separately-computed, possibly
// inconsistent number.
struct TabularSplitReport {
    std::size_t total_rows{0};
    std::size_t train_rows{0};
    std::size_t holdout_rows{0};
};
TabularSplitReport split_tabular_csv(const std::string& csv,
                                     const std::string& target_column,
                                     double holdout_fraction = 0.2);

// Phase 72: Automation Pipeline "Optimize" stage support. Zeroes weights
// whose magnitude is below `threshold` in place -- real magnitude pruning
// on the model's already-learned weight matrix, not a simulated result --
// and reports how many of the model's weights ended up pruned (zero)
// afterward.
struct TabularPruneReport {
    std::size_t weights_total{0};
    std::size_t weights_pruned{0};
};
TabularPruneReport prune_tabular_model(TrainedTabularModel& model,
                                       double threshold = 1e-3);

// Phase 72 (this pass): Automation Pipeline "Label data" stage support --
// a real, deterministic auto-labeler rather than the "skipped, no automated
// labeler exists" outcome Phase 72's initial pass left this stage with (see
// masterai.hpp's LabelTaskStore comment). When every row already carries a
// non-empty value in `target_column`, those are treated as real ground
// truth and simply validated (method "existing_labels_validated"; no value
// is invented). When one or more rows have an empty target, the labeler
// picks the first fully-numeric non-target column, computes that column's
// real 33rd/66th percentile thresholds across the dataset, and fills each
// missing target with "low"/"medium"/"high" based on where that row's value
// in the chosen column falls -- an honest, inspectable heuristic (the
// source column and thresholds are reported so a reviewer can see exactly
// why a row got the label it did), not a claim of semantic understanding.
// Throws std::runtime_error if the CSV cannot be parsed at all or if no
// fully-numeric column is available to derive labels from.
struct TabularAutoLabelReport {
    std::string csv;                  // dataset CSV with target_column filled
    std::size_t rows_total{0};
    std::size_t rows_already_labeled{0};
    std::size_t rows_labeled{0};      // rows the heuristic actually filled in
    std::string method;               // "existing_labels_validated" | "quantile_binning"
    std::string source_column;        // binning column name (quantile_binning only)
    double low_medium_threshold{0.0}; // 33rd percentile (quantile_binning only)
    double medium_high_threshold{0.0}; // 66th percentile (quantile_binning only)
};
TabularAutoLabelReport auto_label_tabular_dataset(const std::string& csv,
                                                  const std::string& target_column);

// Persisted trained-model artifacts, keyed by ModelRegistryEntry id, so a
// model trained in one server run predicts in the next.
class TrainedModelStore final {
public:
    TrainedModelStore() = default;
    explicit TrainedModelStore(RecordStore& records);
    void put(const TrainedTabularModel& model);
    std::optional<TrainedTabularModel> find(const std::string& model_id) const;
    bool remove(const std::string& model_id);

private:
    RecordStore* records_{nullptr};
};

// Phase 79: real checkpoint weight snapshots, keyed by TrainingCheckpoint id
// (not by model id -- a training job can produce many checkpoints for one
// eventual model). Same flat pack/unpack shape as TrainedModelStore above;
// the snapshot's own `model_id` field is repurposed to hold the checkpoint
// id it belongs to, and `training_job_id` still names the real training job
// that produced it, so a checkpoint's snapshot is genuinely resumable via
// train_tabular_model's warm_start parameter without any new serialization
// format.
class CheckpointModelStore final {
public:
    CheckpointModelStore() = default;
    explicit CheckpointModelStore(RecordStore& records);
    void put(const TrainedTabularModel& model);
    std::optional<TrainedTabularModel> find(const std::string& checkpoint_id) const;
    bool remove(const std::string& checkpoint_id);

private:
    RecordStore* records_{nullptr};
};

// Stored results of executed evaluation runs, keyed by EvaluationRun id --
// the "actual numeric score" Evaluation Lab was scoped down without.
class EvaluationResultStore final {
public:
    EvaluationResultStore() = default;
    explicit EvaluationResultStore(RecordStore& records);
    void put(const std::string& run_id, const std::string& metrics_json);
    std::optional<std::string> find(const std::string& run_id) const;
    bool remove(const std::string& run_id);

private:
    RecordStore* records_{nullptr};
};

std::string tabular_evaluation_metrics_json(const TabularEvaluationMetrics& metrics);
std::string tabular_training_report_json(const TabularTrainingReport& report,
                                         const TrainedTabularModel& model);
std::string trained_tabular_model_summary_json(const TrainedTabularModel& model);
std::string tabular_prediction_json(const TabularPrediction& prediction,
                                    const TrainedTabularModel& model);

// Phase 57: docs/PLAN.md "Machine Learning Abilities" section 27 (Model
// Comparison) -- a REAL executor phase like Phase 56, not a scoped-down
// roster record. A comparison names two registered models (a baseline and
// a candidate) plus one shared benchmark dataset; running it evaluates
// both trained artifacts against that dataset's real uploaded content via
// Phase 56's evaluate_tabular_model and stores a genuine side-by-side
// result: both metric sets, the per-metric primary delta, and the winner
// (macro F1 decides classification, MSE decides regression). Scoped
// honestly against section 27's full wishlist: hallucination rate,
// safety, latency/throughput/GPU cost, and blind response comparison only
// mean something for generative models, which this tabular engine does
// not train -- what IS here computes every number it reports. The record
// itself follows EvaluationRun's shape: baseline/candidate/dataset ids
// are all mandatory (a comparison without two models and a shared
// benchmark means nothing), baseline and candidate must differ, and the
// lifecycle reuses Evaluation Lab's five run states since a comparison
// executes like an evaluation, not an approval workflow.
enum class ModelComparisonStatus { queued, running, completed, failed, canceled };

std::string model_comparison_status_name(ModelComparisonStatus status);
ModelComparisonStatus parse_model_comparison_status(const std::string& status);

struct ModelComparison {
    std::string id;
    std::string baseline_model_id;
    std::string candidate_model_id;
    std::string dataset_id;
    std::string name;
    std::string description;
    std::string owner_id;
    ModelComparisonStatus status{ModelComparisonStatus::queued};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ModelComparisonStore final {
public:
    ModelComparisonStore() = default;
    explicit ModelComparisonStore(RecordStore& records);
    ModelComparison create(const std::string& owner_id,
                           const std::string& baseline_model_id,
                           const std::string& candidate_model_id,
                           const std::string& dataset_id,
                           const std::string& name,
                           const std::string& description);
    std::optional<ModelComparison> find(const std::string& id) const;
    std::vector<ModelComparison> list() const;
    bool set_status(const std::string& id, ModelComparisonStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const ModelComparison& comparison);
    RecordStore* records_{nullptr};
    std::map<std::string, ModelComparison> comparisons_;
    mutable std::mutex mutex_;
};

std::string model_comparison_json(const ModelComparison& comparison);
std::string model_comparisons_json(
    const std::vector<ModelComparison>& comparisons);

// Stored results of executed comparisons, keyed by ModelComparison id --
// the same pattern as EvaluationResultStore, in its own collection so a
// comparison result and an evaluation result can never collide.
class ComparisonResultStore final {
public:
    ComparisonResultStore() = default;
    explicit ComparisonResultStore(RecordStore& records);
    void put(const std::string& comparison_id, const std::string& result_json);
    std::optional<std::string> find(const std::string& comparison_id) const;
    bool remove(const std::string& comparison_id);

private:
    RecordStore* records_{nullptr};
};

// Builds the real side-by-side comparison JSON from two models' metrics
// against the same benchmark: both metric sets, the primary-metric name
// and delta (candidate minus baseline), and the winner. Throws
// std::runtime_error if the two metric sets are for different tasks.
std::string tabular_model_comparison_json(
    const TrainedTabularModel& baseline,
    const TabularEvaluationMetrics& baseline_metrics,
    const TrainedTabularModel& candidate,
    const TabularEvaluationMetrics& candidate_metrics);

// Phase 80: builds one experiment run's real result JSON: the loss-history-
// derived training metrics, the held-out validation metrics
// train_tabular_model already computed, an independent full-dataset
// evaluation pass, real probed hardware, measured runtime, and the
// checkpoint/artifact ids the run actually produced.
std::string experiment_result_json(
    const TabularTrainingReport& report,
    const TabularEvaluationMetrics& evaluation_metrics,
    const HardwareInfo& hardware, std::uint64_t runtime_milliseconds,
    const std::vector<std::string>& checkpoint_ids,
    const std::string& log_text, const std::string& artifact_model_id);

// Phase 80: builds the side-by-side comparison of two or more already-
// created experiments (POST /api/v1/ml/experiments/compare): each
// experiment's identity/hyperparameters/dataset plus, relative to the
// first id (baseline), the parameter/dataset/metric/runtime/hardware
// diffs and a regression flag section 25 asks for. Every experiment id
// must exist and resolve via the two lookup callbacks; throws
// std::invalid_argument otherwise. Safety differences are honestly
// reported as not applicable -- this tabular engine has no safety-scoring
// executor -- rather than fabricated, the same honesty convention
// tabular_model_comparison_json already sets.
std::string experiments_comparison_json(
    const std::vector<std::string>& experiment_ids,
    const std::function<std::optional<Experiment>(const std::string&)>&
        find_experiment,
    const std::function<std::optional<std::string>(const std::string&)>&
        find_result_json);

// Phases 58-61: real local knowledge ingestion, authored or learned vector
// indexing, and RAG retrieval. Documents are uploaded as bounded text through
// a browser file picker, hashed, split into overlapping chunks, and embedded
// by either MasterAI's deterministic hashing vectorizer or a verified GGUF
// behind the isolated llama.cpp runner. Vectors persist with their exact
// source and embedding-model provenance. This is deliberately a local
// retrieval/context executor, not an LLM fine-tuning or answer-generation
// executor.
struct KnowledgeDocument {
    std::string id;
    std::string subject_id;
    std::string vector_store_id;
    std::string file_name;
    std::string media_type;
    std::string sha256;
    std::string owner_id;
    std::size_t byte_count{0};
    std::size_t chunk_count{0};
    std::uint64_t ingested_at_epoch_seconds{0};
};

struct KnowledgeChunk {
    std::string id;
    std::string document_id;
    std::string subject_id;
    std::string vector_store_id;
    std::string file_name;
    std::size_t chunk_index{0};
    std::string text;
    std::string embedding_method{"authored_hashing_vectorizer_v1"};
    std::vector<double> embedding;
};

using KnowledgeEmbeddingFunction =
    std::function<std::vector<double>(const std::string&)>;

struct RagRetrievedChunk {
    KnowledgeChunk chunk;
    double vector_score{0.0};
    double keyword_score{0.0};
    double score{0.0};
};

struct RagRetrievalResult {
    std::string query;
    std::string search_strategy;
    std::vector<RagRetrievedChunk> chunks;
};

class KnowledgeIndexStore final {
public:
    KnowledgeIndexStore() = default;
    explicit KnowledgeIndexStore(RecordStore& records);
    // parquet_helper_executable: configured DuckDB CLI path (rule 15's
    // second process-isolated exception); empty disables Parquet ingestion.
    // maximum_document_bytes: replaces the previously hardcoded 2 MiB cap.
    KnowledgeIndexStore(RecordStore& records,
                        std::filesystem::path parquet_helper_executable,
                        std::uint64_t maximum_document_bytes);
    KnowledgeDocument ingest(const std::string& owner_id,
                             const std::string& subject_id,
                             const std::string& vector_store_id,
                             const std::string& file_name,
                             const std::string& media_type,
                             const std::string& content,
                             const std::string& embedding_method =
                                 "authored_hashing_vectorizer_v1",
                             const KnowledgeEmbeddingFunction& vectorize = {});
    std::optional<KnowledgeDocument> find_document(
        const std::string& id) const;
    std::vector<KnowledgeDocument> list_documents() const;
    std::vector<KnowledgeChunk> chunks_for_store(
        const std::string& vector_store_id) const;
    bool remove_document(const std::string& id);

private:
    void restore();
    RecordStore* records_{nullptr};
    std::filesystem::path parquet_helper_executable_;
    std::uint64_t maximum_document_bytes_{2ULL * 1024ULL * 1024ULL};
    std::map<std::string, KnowledgeDocument> documents_;
    std::map<std::string, KnowledgeChunk> chunks_;
    mutable std::mutex mutex_;
};

// Converts Parquet bytes to newline-delimited JSON text via the configured,
// process-isolated DuckDB CLI helper (rule 15's second named exception).
// Throws std::runtime_error if the helper is not configured, cannot be
// spawned, times out, or exits non-zero.
std::string parquet_bytes_to_json(
    const std::filesystem::path& helper_executable,
    const std::string& parquet_bytes);

// Shared by KnowledgeIndexStore::ingest() and the knowledge-documents REST
// handler, which must base64-decode Parquet content before it reaches
// ingest() (binary cannot travel as raw JSON text like other media types).
bool is_parquet_knowledge_upload(const std::string& media_type,
                                 const std::string& file_name);

RagRetrievalResult retrieve_knowledge(
    const KnowledgeIndexStore& index, const std::string& vector_store_id,
    const std::string& search_strategy, const std::string& query,
    std::size_t top_k = 5U,
    const std::string& embedding_method =
        "authored_hashing_vectorizer_v1",
    const KnowledgeEmbeddingFunction& vectorize = {});
std::vector<double> authored_hash_embedding(const std::string& text);
std::string knowledge_document_json(const KnowledgeDocument& document);
std::string knowledge_documents_json(
    const std::vector<KnowledgeDocument>& documents);
std::string knowledge_index_profile_json(
    const std::string& vector_store_id,
    const std::vector<KnowledgeChunk>& chunks);
std::string rag_retrieval_result_json(const RagRetrievalResult& result);

// Phases 62-65: the remaining four docs/PLAN.md "Machine Learning
// Abilities" section-2 interfaces (Inference Endpoints, Hardware and
// Compute, Automation Pipelines, Safety and Governance). Each follows the
// same scoped-down pattern as every interface above -- an identity/intent/
// lifecycle registry recording administrator intent, not a live network
// listener, hardware poller, job orchestrator, or content scanner. See
// each store's own comment for the fields this phase intentionally defers.

enum class InferenceEndpointStatus { draft, active, disabled };

std::string inference_endpoint_status_name(InferenceEndpointStatus status);
InferenceEndpointStatus parse_inference_endpoint_status(
    const std::string& status);

// Phase 62: docs/PLAN.md "Machine Learning Abilities" section 35. Records
// an administrator's intent to expose a model behind a controlled
// endpoint. Phase 77 closes the "does not open a real network listener,
// enforce the rate limit, or apply the safety/tool policy" gap this
// comment used to name in full: a listener now really starts while the
// endpoint is `active` (see run_inference_endpoint in server.cpp), it
// really enforces rate_limit_per_minute (an in-memory, per-endpoint,
// per-minute counter -- not persisted, resets on restart), and it really
// runs Phase 74's scan_content_for_risks over every request/response by
// default. This pass replaces "by default" with real per-endpoint policy:
// see the content_scan_enabled/block_on_scan_finding/safety_policy_id/
// model_classifier_enabled fields below -- the scan is no longer fixed, an
// administrator can disable it, make it advisory-only, attach a named
// SafetyPolicy's restricted terms, or opt into Phase 74's LLM-judge
// classifier pass, all re-read fresh on every request so a policy change
// takes effect without restarting the listener.
// `authentication_method` remains free text (an administrator-facing
// label), but every non-"none" value is enforced identically: a single
// Bearer shared-secret check against `auth_token_hash` below (this phase
// does not build a distinct wire format per named scheme) -- honest about
// that simplification rather than claiming per-scheme fidelity it
// doesn't have.
struct InferenceEndpoint {
    std::string id;
    std::string name;
    std::string model_id;
    std::string runtime;
    std::string host;
    std::uint16_t port{0};
    std::string protocol;
    std::string authentication_method;
    std::uint32_t rate_limit_per_minute{0};
    std::string owner_id;
    InferenceEndpointStatus status{InferenceEndpointStatus::draft};
    // Phase 77: sha256_hex of the bearer token this endpoint requires when
    // authentication_method != "none" -- never the plaintext, matching
    // ComputeNode::agent_shared_secret_hash's convention. The plaintext
    // itself lives only in HttpServer::State's `secrets` SecretStore
    // (key "inference-endpoint:<id>"), the same encrypted-at-rest store
    // Phase 75 also uses for telemetry-agent shared secrets.
    std::string auth_token_hash;
    // Phase 77 (this pass): per-endpoint policy configuration, closing the
    // "fixed content scan" gap the Phase 77 class comment above named --
    // previously every active endpoint ran the exact same unconditional
    // scan_content_for_risks() call with no way to tighten, loosen, or
    // extend it per deployment. `content_scan_enabled` off skips the
    // heuristic scan entirely (e.g. a trusted internal endpoint that
    // already scans upstream); `block_on_scan_finding` off still runs the
    // scan and reports findings but never rejects the request/response;
    // `safety_policy_id`, when set to a real SafetyPolicy id, adds that
    // policy's restricted_data_categories terms to the scan the same way
    // the safety-policy scan route (POST .../safety-policies/{id}/scan)
    // already does; `model_classifier_enabled` opts this endpoint into
    // Phase 74's real LLM-judge classifier pass (scan_content_with_model_
    // classifier) for bias/hallucination/subtler-harmful-content coverage
    // the heuristic scan cannot provide -- off by default because it costs
    // a second real generation call per request.
    bool content_scan_enabled{true};
    // Default true preserves the pre-policy behavior: a flagged prompt
    // always 400s before generation runs.
    bool block_on_scan_finding{true};
    // Default false preserves the pre-policy behavior: a flagged answer is
    // still returned alongside its scan result, not silently withheld,
    // since the heuristic scanner can false-positive and an endpoint should
    // not fail closed on it by default. Distinct from block_on_scan_finding
    // above (which governs the prompt) so an administrator can tighten
    // either side independently.
    bool block_answer_on_scan_finding{false};
    std::string safety_policy_id;
    bool model_classifier_enabled{false};
    double model_classifier_confidence_floor{0.5};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class InferenceEndpointStore final {
public:
    InferenceEndpointStore() = default;
    explicit InferenceEndpointStore(RecordStore& records);
    InferenceEndpoint create(const std::string& owner_id,
                             const std::string& name,
                             const std::string& model_id,
                             const std::string& runtime,
                             const std::string& host, std::uint16_t port,
                             const std::string& protocol,
                             const std::string& authentication_method,
                             std::uint32_t rate_limit_per_minute,
                             const std::string& auth_token = {});
    std::optional<InferenceEndpoint> find(const std::string& id) const;
    std::vector<InferenceEndpoint> list() const;
    bool set_status(const std::string& id, InferenceEndpointStatus status);
    // Phase 77 (this pass): updates the policy fields documented above for
    // an existing endpoint. Returns false (no-op) if `id` does not exist.
    bool set_policy(const std::string& id, bool content_scan_enabled,
                    bool block_on_scan_finding, bool block_answer_on_scan_finding,
                    const std::string& safety_policy_id, bool model_classifier_enabled,
                    double model_classifier_confidence_floor);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const InferenceEndpoint& endpoint);
    RecordStore* records_{nullptr};
    std::map<std::string, InferenceEndpoint> endpoints_;
    mutable std::mutex mutex_;
};

std::string inference_endpoint_json(const InferenceEndpoint& endpoint);
std::string inference_endpoints_json(
    const std::vector<InferenceEndpoint>& endpoints);

enum class ComputeNodeStatus { available, reserved, draining, disabled };

std::string compute_node_status_name(ComputeNodeStatus status);
ComputeNodeStatus parse_compute_node_status(const std::string& status);

// Phase 63: docs/PLAN.md "Machine Learning Abilities" section 30. Records
// a compute node's static description and administrative status.
// Deliberately does not poll live telemetry for an arbitrary remote node
// (temperature, power draw, queue length, current workload) -- that
// requires an agent process on the node this phase does not build. Phase
// 67 closes the one case that needs no remote agent at all: a node flagged
// `is_local` is the same host this MasterAI process is already running on,
// so its live CPU/RAM/GPU capacity can be probed in-process via
// probe_hardware() on demand (see the compute-node telemetry endpoint in
// server.cpp). Remote nodes still report only the static description
// entered at creation time. Phase 75 closes the remote-agent case
// unlocked for a node that names a real telemetry agent: `agent_url`
// (`host:port` of a `masterai telemetry-agent` process running on that
// node -- see run_telemetry_agent/fetch_remote_telemetry) and
// `agent_shared_secret_hash` (sha256_hex of the shared secret that agent
// requires, never the plaintext secret -- matching the setup-token hash
// convention elsewhere in this codebase). A node with neither set still
// reports only its static description, exactly as before.
struct ComputeNode {
    std::string id;
    std::string name;
    std::string address;
    std::string operating_system;
    std::string cpu_description;
    std::string gpu_description;
    std::uint64_t memory_mib{0};
    std::string owner_id;
    ComputeNodeStatus status{ComputeNodeStatus::available};
    bool is_local{false};
    std::string agent_url;
    std::string agent_shared_secret_hash;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class ComputeNodeStore final {
public:
    ComputeNodeStore() = default;
    explicit ComputeNodeStore(RecordStore& records);
    ComputeNode create(const std::string& owner_id, const std::string& name,
                       const std::string& address,
                       const std::string& operating_system,
                       const std::string& cpu_description,
                       const std::string& gpu_description,
                       std::uint64_t memory_mib, bool is_local,
                       const std::string& agent_url = {},
                       const std::string& agent_shared_secret = {});
    std::optional<ComputeNode> find(const std::string& id) const;
    std::vector<ComputeNode> list() const;
    bool set_status(const std::string& id, ComputeNodeStatus status);
    bool remove(const std::string& id);

private:
    void restore();
    void persist(const ComputeNode& node);
    RecordStore* records_{nullptr};
    std::map<std::string, ComputeNode> nodes_;
    mutable std::mutex mutex_;
};

std::string compute_node_json(const ComputeNode& node);
std::string compute_nodes_json(const std::vector<ComputeNode>& nodes);

enum class AutomationPipelineStatus { draft, active, disabled };
enum class AutomationPipelineRunStatus {
    queued, running, completed, failed, canceled
};

std::string automation_pipeline_status_name(AutomationPipelineStatus status);
AutomationPipelineStatus parse_automation_pipeline_status(
    const std::string& status);
std::string automation_pipeline_run_status_name(
    AutomationPipelineRunStatus status);
AutomationPipelineRunStatus parse_automation_pipeline_run_status(
    const std::string& status);

// Phase 64: docs/PLAN.md "Machine Learning Abilities" section 37. A
// pipeline definition names an ordered subset of the sixteen lifecycle
// stages that section lists (import/validate/clean/label/split/train/
// validate model/evaluate/safety test/optimize/approve/deploy staging/
// stage test/deploy production/monitor/rollback), stored as a
// comma-joined string of stage names.
// Phase 69: docs/PLAN.md "Machine Learning Abilities" section 37, closing
// Automation Pipelines out of the "records intent" set. dataset_id/
// model_id name the real target a run trains/evaluates against. Running a
// pipeline now genuinely executes each recognized stage that already has a
// real executor elsewhere in this codebase -- "Train model" via Phase 56's
// train_tabular_model (a fresh TrainingJob is created and run for real) and
// "Evaluate model" via Phase 56's evaluate_tabular_model (a fresh
// EvaluationRun is created and run for real against the model the pipeline
// just trained, or model_id if no Train model stage ran first) -- and
// records every other named stage honestly as skipped, since this codebase
// has no data-labeling, safety-scanning, deployment-serving, or monitoring
// executor for a pipeline to call.
// Phase 71: seven more stages gain a real executor -- "Validate data"
// (parse_tabular_csv against the dataset's uploaded content), "Validate
// model" (the current model actually has trained weights), "Safety tests"
// (an approved ModelCard exists for the current model), "Request approval"/
// "Deploy staging"/"Deploy production" (a real Deployment record is
// created/approved for that environment, matching Deployment Manager's own
// documented scope of an approval workflow rather than live traffic
// serving), and "Rollback" (the pipeline's most recent deployment is
// rejected, i.e. its approval is revoked). "Monitor" also becomes real,
// reusing Phase 68's build_ml_monitoring_json(). Phase 72 closes the
// remaining six: "Import data" (a real content-presence check), "Clean
// data" (real blank/duplicate row removal, persisted back to the
// dataset), "Split data" (real train/holdout counts via the trainer's own
// split formula), "Optimize" (a real ModelOptimizationRun that actually
// prunes the model's learned weights), and "Staging tests" (a real
// evaluation run). "Label data" still honestly reports "skipped" when no
// completed LabelTaskStore entry exists for the dataset -- this codebase
// has no automated labeler. A run no longer blocks the HTTP request
// until every stage finishes: it now executes on a detached background
// thread while GET .../runs reports live progress (current stage, and
// completed/total stage counts) so the web UI can render a progress bar.
struct AutomationPipeline {
    std::string id;
    std::string name;
    std::string project_id;
    std::string description;
    std::string stages;
    std::string dataset_id;
    std::string model_id;
    std::string owner_id;
    AutomationPipelineStatus status{AutomationPipelineStatus::draft};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

struct AutomationPipelineRun {
    std::string id;
    std::string pipeline_id;
    std::string owner_id;
    AutomationPipelineRunStatus status{AutomationPipelineRunStatus::queued};
    std::string outcome_note;
    // Phase 69: JSON array of {"stage","status","detail"} objects, one per
    // recognized pipeline stage, in the order the pipeline names them.
    std::string stage_results_json{"[]"};
    // Phase 71: live progress, updated once per stage as a background
    // thread executes the run, so GET .../runs can report how far along a
    // still-`running` run is instead of only showing the final result.
    std::uint32_t total_stage_count{0};
    std::uint32_t completed_stage_count{0};
    std::string current_stage;
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class AutomationPipelineStore final {
public:
    AutomationPipelineStore() = default;
    explicit AutomationPipelineStore(RecordStore& records);
    AutomationPipeline create(const std::string& owner_id,
                              const std::string& name,
                              const std::string& project_id,
                              const std::string& description,
                              const std::string& stages,
                              const std::string& dataset_id,
                              const std::string& model_id);
    std::optional<AutomationPipeline> find(const std::string& id) const;
    std::vector<AutomationPipeline> list() const;
    bool set_status(const std::string& id, AutomationPipelineStatus status);
    bool remove(const std::string& id);
    // Phase 71: begin_run() persists a `running` row with a known total
    // stage count before any stage executes, so a poller sees the run
    // immediately; append_stage_result() is called once per stage as a
    // background thread works through them; finish_run() sets the terminal
    // status once every stage has run.
    AutomationPipelineRun begin_run(const std::string& owner_id,
                                    const std::string& pipeline_id,
                                    std::uint32_t total_stage_count);
    bool append_stage_result(const std::string& run_id,
                             const std::string& stage_results_json,
                             std::uint32_t completed_stage_count,
                             const std::string& current_stage);
    bool finish_run(const std::string& run_id,
                    AutomationPipelineRunStatus status,
                    const std::string& outcome_note);
    std::optional<AutomationPipelineRun> find_run(
        const std::string& run_id) const;
    std::vector<AutomationPipelineRun> runs_for(
        const std::string& pipeline_id) const;

private:
    void restore();
    void persist(const AutomationPipeline& pipeline);
    void persist_run(const AutomationPipelineRun& run);
    RecordStore* records_{nullptr};
    std::map<std::string, AutomationPipeline> pipelines_;
    std::map<std::string, AutomationPipelineRun> runs_;
    mutable std::mutex mutex_;
};

std::string automation_pipeline_json(const AutomationPipeline& pipeline);
std::string automation_pipelines_json(
    const std::vector<AutomationPipeline>& pipelines);
std::string automation_pipeline_run_json(const AutomationPipelineRun& run);
std::string automation_pipeline_runs_json(
    const std::vector<AutomationPipelineRun>& runs);

enum class SafetyPolicyStatus { pending, approved, rejected };

std::string safety_policy_status_name(SafetyPolicyStatus status);
SafetyPolicyStatus parse_safety_policy_status(const std::string& status);

// Phase 65: docs/PLAN.md "Machine Learning Abilities" section 40. A
// governance policy records restricted data categories and an approval
// requirement for a project/scope; a model card records the section-40
// disclosure fields for one approved model. The policy/model-card records
// here only record administrator intent and an approval decision, matching
// Dataset/Deployment approval above; Phase 74 (scan_content_for_risks
// below) adds real secret-token, prompt-injection-phrasing, and
// policy-restricted-term scanning, but that is heuristic pattern matching,
// not a classifier -- bias, hallucination, and subtler harmful content
// still have no real detector in this codebase.
struct SafetyPolicy {
    std::string id;
    std::string name;
    std::string scope;
    std::string restricted_data_categories;
    std::string owner_id;
    SafetyPolicyStatus status{SafetyPolicyStatus::pending};
    // Admin-only toggle: when false, scan_content_for_risks() skips this
    // policy's restricted_data_categories check (see ml_safety_scan.cpp).
    // The unconditional secret-token and prompt-injection-phrase scans are
    // never gated by this flag -- only the policy's own configured terms
    // can be turned off, not the baseline scans.
    bool enforcement_enabled{true};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

struct ModelCard {
    std::string id;
    std::string model_id;
    std::string purpose;
    std::string intended_use;
    std::string prohibited_use;
    std::string training_data_reference;
    std::string evaluation_results;
    std::string known_limitations;
    std::string license;
    std::string owner_id;
    SafetyPolicyStatus status{SafetyPolicyStatus::pending};
    std::uint64_t created_at_epoch_seconds{0};
    std::uint64_t updated_at_epoch_seconds{0};
};

class SafetyGovernanceStore final {
public:
    SafetyGovernanceStore() = default;
    explicit SafetyGovernanceStore(RecordStore& records);
    SafetyPolicy create_policy(const std::string& owner_id,
                               const std::string& name,
                               const std::string& scope,
                               const std::string& restricted_data_categories);
    std::optional<SafetyPolicy> find_policy(const std::string& id) const;
    std::vector<SafetyPolicy> list_policies() const;
    bool set_policy_status(const std::string& id, SafetyPolicyStatus status);
    bool set_policy_enforcement(const std::string& id, bool enabled);
    bool remove_policy(const std::string& id);

    ModelCard create_model_card(const std::string& owner_id,
                                const std::string& model_id,
                                const std::string& purpose,
                                const std::string& intended_use,
                                const std::string& prohibited_use,
                                const std::string& training_data_reference,
                                const std::string& evaluation_results,
                                const std::string& known_limitations,
                                const std::string& license);
    std::optional<ModelCard> find_model_card(const std::string& id) const;
    std::vector<ModelCard> list_model_cards() const;
    bool set_model_card_status(const std::string& id,
                               SafetyPolicyStatus status);
    bool remove_model_card(const std::string& id);

private:
    void restore();
    void persist_policy(const SafetyPolicy& policy);
    void persist_model_card(const ModelCard& card);
    RecordStore* records_{nullptr};
    std::map<std::string, SafetyPolicy> policies_;
    std::map<std::string, ModelCard> model_cards_;
    mutable std::mutex mutex_;
};

std::string safety_policy_json(const SafetyPolicy& policy);
std::string safety_policies_json(const std::vector<SafetyPolicy>& policies);
std::string model_card_json(const ModelCard& card);
std::string model_cards_json(const std::vector<ModelCard>& cards);

// Phase 74: real, local, heuristic/regex-based content scanning -- the
// content-scanning executor SafetyPolicy's class comment above says this
// module didn't have. Deliberately not an ML classifier: it detects
// secret-shaped tokens (API-key patterns, PEM key headers, generic
// high-entropy strings), prompt-injection phrasing (role-override/
// instruction-override keyword patterns), and any of a SafetyPolicy's own
// restricted_data_categories terms found verbatim in the scanned text.
// Every finding is a real, reproducible pattern match against the actual
// text passed in -- never a fabricated risk score. It does not detect
// bias, hallucination, or subtler harmful content, since that needs a real
// classifier this phase does not build.
struct ContentScanFinding {
    std::string category;  // "secret" | "prompt_injection" | "restricted_term"
    std::string detail;    // human-readable description, e.g. which pattern/term matched
};

struct ContentScanReport {
    std::vector<ContentScanFinding> findings;
    bool clean() const { return findings.empty(); }
};

// Scans `text` for secret-shaped tokens and prompt-injection phrasing
// unconditionally, plus a verbatim (case-insensitive) search for each
// comma-separated term in `policy.restricted_data_categories` when
// `policy` is supplied.
ContentScanReport scan_content_for_risks(const std::string& text,
                                         const SafetyPolicy* policy = nullptr);

std::string content_scan_report_json(const ContentScanReport& report);

// Phase 74 (this pass): a real ML-classifier-based scorer layered on top of
// scan_content_for_risks()'s hand-rolled pattern matching above. Bias,
// hallucination risk, and subtler harmful content are exactly the
// categories keyword/substring matching structurally cannot catch (the
// original Phase 74 gap note, docs/PLAN.md); this asks the same locally
// loaded language model this server already runs inference through to
// judge the text against those three categories via a fixed, low-
// temperature, JSON-only prompt -- a real LLM-as-judge classifier, not a
// second keyword list dressed up as one. `generate` is a caller-supplied
// callback (server.cpp wires it to execute_rag_generation(), which is what
// keeps this respecting the exact same memory/scheduler admission every
// other generation call goes through) so this function stays independently
// testable with a canned response and carries no direct RunnerSupervisor
// dependency itself.
struct ModelClassifierFinding {
    std::string category;    // "bias" | "hallucination_risk" | "harmful_content"
    double confidence{0.0};  // 0..1, as reported by the judge model
    std::string rationale;
};
struct ModelClassifierReport {
    bool available{false};   // false when `generate` threw or its reply did not
                             // parse as the requested JSON shape
    std::string diagnostic;  // populated when available is false
    std::vector<ModelClassifierFinding> findings;  // only entries >= confidence_floor
};

// Never throws: any failure from `generate` (no model loaded, generation
// error, unparseable reply) is reported as available=false with a
// diagnostic, never silently treated as "clean" -- a classifier that could
// not be evaluated is a different, honestly distinguishable outcome from a
// classifier that ran and found nothing.
ModelClassifierReport scan_content_with_model_classifier(
    const std::string& text,
    const std::function<std::string(const std::string&)>& generate,
    double confidence_floor = 0.5);

std::string model_classifier_report_json(const ModelClassifierReport& report);

// Continual Learning (2026-08-24): docs/PLAN.md "Machine Learning
// Abilities" section 38's 12-step human-gated workflow. Steps 8-12
// (scheduled fine-tuning, evaluation, comparison, approval, controlled
// rollout) already have real executors elsewhere in this codebase --
// Fine-Tuning Jobs, Evaluation Lab, Model Comparison, and Deployment
// Manager -- so this closes steps 1-6 (collect, scrub, classify, score,
// detect harmful content, present for review) with one real executor
// rather than duplicating machinery that already exists. Step 7 (add
// approved examples to a versioned dataset) is exactly what
// InstructionExample's existing draft/in_review/approved reviewer
// lifecycle already gates -- collect_continual_learning_candidates()
// below creates real `draft` InstructionExample records for an
// administrator to review through that same existing page, never a
// production model trained automatically from raw conversation content.
//
// Hand-rolled character scanning, not std::regex (this codebase does not
// use <regex> anywhere -- see scan_content_for_risks's own comment),
// following the same "reproducible pattern match, not a fabricated
// score" discipline. Deliberately not a full PII/NER model: email
// addresses, US Social Security numbers, Luhn-valid credit-card-shaped
// digit runs, and phone-shaped digit runs are the four categories
// detected. A redacted category is replaced in the scrubbed text with a
// "[REDACTED_<CATEGORY>]" placeholder; the original text is never
// persisted anywhere by the caller.
struct PiiScrubReport {
    std::string scrubbed_text;
    // Category name -> count of redactions made for it, e.g.
    // {"email": 1, "phone": 2}. Empty when nothing was found.
    std::map<std::string, std::uint32_t> redaction_counts;
    bool clean() const { return redaction_counts.empty(); }
};
PiiScrubReport scrub_pii(const std::string& text);

// Real heuristic classification of one collected instruction/response
// pair into a small closed set of categories -- deterministic, not a
// fabricated label: "code" when the response contains a fenced code
// block; "long_form" when the response exceeds kLongFormCharacterFloor
// characters (server.cpp-visible via this header's own constant below);
// "short_qa" otherwise. Documented as a coarse, structural heuristic, not
// a topic/intent classifier.
std::string classify_continual_learning_candidate(const std::string& response);

// Real, deterministic quality score in [0.0, 1.0] for one collected
// candidate, computed from: response non-emptiness (0 if empty), a
// minimum real length floor, and the absence of a small set of low-value
// response markers (e.g. the response is just an error/refusal echoed
// back). Never a fabricated or random number -- every point deducted
// corresponds to a real, checkable condition on the actual text.
double score_continual_learning_candidate_quality(
    const std::string& user_message, const std::string& response);

// Real report from collect_continual_learning_candidates() below -- every
// count is genuine, computed from the actual chats/messages scanned.
struct ContinualLearningCollectionReport {
    std::size_t chats_scanned{0};
    std::size_t turns_considered{0};
    std::size_t candidates_created{0};
    std::size_t rejected_low_quality{0};
    std::size_t rejected_unsafe{0};
    std::uint32_t pii_redactions_applied{0};
    // Ids of every real InstructionExample created, in creation order --
    // an administrator reviews these through the existing Prompt and
    // Instruction Training page, exactly like a manually authored example.
    std::vector<std::string> created_example_ids;
};

// Scans every user-message/assistant-reply turn pair in `chats`, applying
// (in order) PII scrubbing, safety scanning (scan_content_for_risks(),
// same as any other Safety and Governance content scan), quality
// scoring, and structural classification, then creates a real, `draft`-
// status InstructionExample (plus its InstructionExampleContent body) in
// `examples`/`contents` for every turn that clears `quality_floor` and
// has no safety findings -- never for one that does not, and never
// auto-approved. A turn below the quality floor or with any safety
// finding is counted, not silently dropped, so the returned report is a
// complete, honest accounting of what was scanned versus what became a
// reviewable candidate.
ContinualLearningCollectionReport collect_continual_learning_candidates(
    const std::vector<ChatRecord>& chats, const std::string& target_dataset_id,
    const std::string& owner_id, InstructionExampleStore& examples,
    InstructionExampleContentStore& contents, double quality_floor = 0.4,
    const SafetyPolicy* policy = nullptr);

// Phase 75: remote/fleet Hardware and Compute telemetry -- the "agent
// process on the node" ComputeNode's class comment said this codebase did
// not build. run_telemetry_agent blocks the calling thread serving one
// authenticated `GET /telemetry` endpoint (a real probe_hardware()
// snapshot) on a listener that, unlike HttpServer (which hard-enforces
// loopback-only binding), can bind a non-loopback interface. Started via
// `masterai telemetry-agent <host> <port> <shared-secret>` on the remote
// node itself (see main.cpp), never by the main server process.
void run_telemetry_agent(const std::string& host, std::uint16_t port,
                         const std::string& shared_secret,
                         const std::filesystem::path& storage_root,
                         std::atomic_bool& stop_requested);

// Client side of the same protocol: the ComputeNode `.../telemetry` route
// (server.cpp) calls this to fetch a real probe_hardware() snapshot from a
// non-local node's agent over plain HTTP, presenting shared_secret as a
// Bearer token. Returns the agent's raw hardware_info_json() response body
// verbatim -- embedded directly into the route's own JSON response rather
// than re-parsed, since this codebase controls both ends of the wire
// format. Throws std::runtime_error with a real network/auth error --
// never fabricated numbers -- if the agent is unreachable, times out, or
// rejects the shared secret. agent_url is `host:port` or `http://host:port`
// (no TLS support -- this is a loopback-adjacent operator tool for a
// trusted fleet, not an internet-facing endpoint).
std::string fetch_remote_telemetry(const std::string& agent_url,
                                   const std::string& shared_secret,
                                   std::uint32_t timeout_seconds = 5U);

// Phase 78: real per-request inference telemetry for Monitoring and
// Diagnostics -- Phase 68's class comment on build_ml_monitoring_json()
// (server.cpp) named live per-request instrumentation (latency, queue
// depth, requests/sec) as something no request-path instrumentation
// existed to report. This is that instrumentation: every real generation
// call site (the chat handler, Phase 76's execute_rag_generation --
// itself also used by Phase 77's inference-endpoint listener) calls
// begin_request()/end_request() around admission and completion, so a
// snapshot reflects genuinely measured activity, never a fabricated
// number. Deliberately does not track per-step training curves (the
// tabular trainer's runs are synchronous and complete before there is a
// meaningful "live" window to sample one from -- Phase 56 already exposes
// real post-hoc loss curves), closed by TrainingProgressTracker above, or
// cache-hit rate, closed by record_cache_decision()/cache_hits/cache_misses
// below (this pass): no KV-cache-hit instrumentation previously existed in
// the inference adapter to report on. PromptSessionManager::try_reuse() is
// the actual reuse decision every generation call site already makes
// (chat_handler, server.cpp) -- record_cache_decision() is called with
// exactly that decision's `reuse` bool right after, so the reported rate
// reflects genuinely measured session-reuse outcomes, never a fabricated
// number. Scope note, stated honestly: this counts *session-level* KV-slot
// reuse decisions (whole-prompt-prefix cache hits), not sub-prompt/per-token
// cache hits inside a single generation -- llama.cpp's own runner does not
// expose that finer-grained counter to this adapter.
class InferenceMetricsStore final {
public:
    // Call once when a request is admitted into the scheduler (queue depth
    // +1), and once when it leaves -- successfully, cancelled, or failed --
    // with its real measured latency in microseconds (queue depth -1, and
    // the latency recorded for the rolling percentile/throughput window).
    void begin_request();
    void end_request(std::uint64_t latency_microseconds);
    // Phase 78 (this pass): records one real PromptSessionManager::
    // try_reuse() outcome. `reuse` true = the request reused an existing
    // KV-cache slot (a hit); false = it had to re-evaluate the prompt from
    // scratch (a miss, including "session reuse is disabled" and "no prior
    // session to reuse").
    void record_cache_decision(bool reuse);

    struct Snapshot {
        std::int64_t current_queue_depth{0};
        std::uint64_t requests_last_minute{0};
        double p50_latency_ms{0.0};
        double p95_latency_ms{0.0};
        double p99_latency_ms{0.0};
        std::uint64_t cache_hits{0};
        std::uint64_t cache_misses{0};
        double cache_hit_rate{0.0};  // 0 when cache_hits + cache_misses == 0
    };
    Snapshot snapshot() const;

private:
    mutable std::mutex mutex_;
    std::int64_t queue_depth_{0};
    // (epoch_seconds, latency_microseconds) per completed request, pruned
    // to the last 15 minutes on every access so this cannot grow without
    // bound on a long-running server.
    std::deque<std::pair<std::uint64_t, std::uint64_t>> samples_;
    std::uint64_t cache_hits_{0};
    std::uint64_t cache_misses_{0};
};

std::string inference_metrics_json(const InferenceMetricsStore::Snapshot& snapshot);

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
    // Phase 24: the deterministic request classification (see
    // RetrievalRequestClassification), recorded as its string form so
    // QueryTrace does not need to depend on retrieval's enum. Empty when no
    // classification was ever recorded for this query.
    std::string request_classification;
    // Phase 24: "no adapter" skip reasons for every declared-but-disabled
    // retrieval strategy this query considered, so a caller inspecting the
    // trace can see exactly why (e.g.) semantic search never ran, instead of
    // it silently appearing to have found nothing.
    std::vector<std::string> disabled_retrieval_strategies;
    // Phase 33 (LOCAL-ONLY slice): which local runner process actually
    // served this query -- "inference" for the always-present default
    // single-runner supervisor, or a LocalRunnerConfig::id when an opt-in
    // LocalRunnerPool routed the request elsewhere. Empty only for traces
    // that never reached a runner (e.g. failed before admission).
    std::string runner_id;
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
    // Phase 24: records the deterministic request classification.
    void record_classification(const std::string& id, std::string classification);
    // Phase 24: records "no adapter" skip reasons for declared-but-disabled
    // retrieval strategies considered for this query.
    void record_retrieval_strategy_skips(const std::string& id,
                                         std::vector<std::string> reasons);
    // Phase 33 (LOCAL-ONLY slice): records which local runner process
    // served this query -- see QueryTrace::runner_id. Follows the same
    // "independent of the terminal diagnostic" convention as
    // record_classification()/record_retrieval() so a later finish() call
    // never erases it.
    void record_runner(const std::string& id, std::string runner_id);
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
    // Phase 34 (full completion pass): now a live, hot-swappable knob --
    // AdaptiveController::evaluate() applies real reductions under pressure
    // through the exact same set_policy() mechanism as the four fields
    // above, instead of only ever disclosing a recommendation. Seeded from
    // the same resource-profile default MemorySweeper's constructor argument
    // used before this field existed (see server.cpp's runner_idle_unload_
    // seconds); MemorySweeper reads this live field every sweep now, not a
    // constructor-frozen value.
    std::uint32_t idle_unload_seconds{600U};
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
    // Phase 30A: read-only access to the resolved policy (in particular
    // maximum_active_inference/maximum_queued_inference) so callers that
    // need to enforce the configured concurrency ceiling -- e.g.
    // send_chat_message()'s one-slot-under-cpu_only admission gate -- do not
    // have to keep a second copy of it in sync by hand.
    MemoryPolicy policy() const;
    // Phase 34: the one live-mutable entry point the adaptive performance
    // controller (adaptive_controller.cpp) actually adjusts at runtime --
    // every subsequent reserve()/sample()/permits_background_work() call
    // reads the newly assigned policy immediately, since they all read
    // state_->policy fresh rather than a value captured at construction.
    // Applies the exact same bound validation the constructor already
    // enforces (throws std::invalid_argument on an out-of-bounds policy),
    // so this can never be used to bypass the safety ceilings every other
    // entry point already respects.
    void set_policy(MemoryPolicy policy);
    static MemoryPolicy policy_for(ResourceProfile profile,
                                   const HardwareInfo& hardware,
                                   std::uint64_t hard_limit_bytes = 0U);
    static std::string to_json(const MemoryStatus& status);

private:
    class State;
    std::unique_ptr<State> state_;
};

// Model Inventory page "Memory Status" widget: the set of real memory-
// reclaim actions an administrator can trigger by hand, and the progress
// tracker the frontend polls while they run. Every action here is a real
// operation this process can actually perform -- MasterAI cannot force
// another process or the OS itself to release memory, only shrink its own
// footprint (bounded caches, scratch/temp files, its own working set, and
// optionally the loaded model), which is what increases OS-visible free RAM.
enum class MemoryCleanupState { idle, running, complete, failed };

struct MemoryCleanupOptions {
    bool trim_caches{true};
    bool clear_scratch{true};
    bool release_working_set{true};
    // Opt-in and defaulted off: unloading the active model is the most
    // disruptive option (the next chat message has to reload it), so it
    // must be explicitly requested rather than assumed.
    bool unload_model{false};
    // Windows system-memory options below (docs/PLAN.md Phase 35 extension
    // note). Each calls a real OS-level memory-manager primitive rather
    // than anything scoped to MasterAI's own process, and each genuinely
    // requires the MasterAI process itself to be running elevated
    // (Administrator) -- Windows only grants SeProfileSingleProcessPrivilege/
    // SeIncreaseQuotaPrivilege/SeDebugPrivilege to elevated tokens. Every one
    // of these fails closed (reports unavailable, never crashes or silently
    // no-ops as if it worked) when the process isn't elevated -- see
    // platform.cpp's acquire_privilege_if_available(). Defaulted off:
    // trimming other applications or purging the standby list can make the
    // OS momentarily slower (previously-cached pages have to be re-read from
    // disk), so this is opt-in the same way unload_model is.
    bool trim_other_process_working_sets{false};
    // "Only applications using N MB or more" -- 0 means no threshold.
    std::uint64_t trim_other_process_minimum_mib{100};
    bool protect_foreground_application{true};
    bool flush_modified_page_list{false};
    bool purge_standby_list{false};
    bool purge_low_priority_standby_pages{false};
    bool empty_system_and_service_working_sets{false};
    bool clear_system_file_cache{false};
};

struct MemoryCleanupResult {
    std::string id;
    MemoryCleanupState state{MemoryCleanupState::idle};
    unsigned int percent{0};
    std::string current_step;
    std::uint64_t cache_bytes_freed{0};
    std::size_t scratch_orphans_removed{0};
    std::uint64_t process_working_set_bytes_freed{0};
    bool model_unloaded{false};
    std::uint32_t other_processes_trimmed{0};
    bool modified_page_list_flushed{false};
    bool standby_list_purged{false};
    bool low_priority_standby_purged{false};
    std::uint32_t system_working_sets_emptied{0};
    bool system_file_cache_cleared{false};
    // Names (matching the option, e.g. "purgeStandbyList") of every
    // requested privileged step that could not run because the process
    // isn't elevated -- surfaced to the administrator instead of a silent
    // no-op, so "I checked the box and nothing happened" always has an
    // honest, visible reason.
    std::vector<std::string> privilege_denied_steps;
    std::string diagnostic;
};

// Single-flight, poll-friendly tracker for the administrator-triggered
// "Clean Memory" action. Mirrors DownloadJob's progress-reporting shape
// (begin/record_progress-style updates a GET poll can observe mid-flight)
// but intentionally simpler: cleanup runs to completion in one pass with no
// pause/resume, so only current state/percent/step need to survive polls.
class MemoryCleanupTracker final {
public:
    // Refuses (returns false, leaves any prior finished result untouched) if
    // a run is already in flight, so two concurrent POSTs can't race each
    // other's step sequencing or progress counter.
    bool begin(std::string id, unsigned int total_steps);
    // Marks one real sub-step as complete and recomputes percent from
    // completed/total steps -- progress a caller can trust, not a fake timer.
    void advance_step(std::string step_label);
    void finish(MemoryCleanupResult partial);
    void fail(std::string diagnostic);
    MemoryCleanupResult status() const;

private:
    mutable std::mutex mutex_;
    MemoryCleanupResult current_;
    unsigned int total_steps_{0};
    unsigned int completed_steps_{0};
};

std::string memory_cleanup_result_json(const MemoryCleanupResult& result);

// Phase 30: bridges a FixedSizePool<T>'s bytes_reserved() to
// MemoryBudgetManager's live category accounting, closing the gap the
// foundational Phase 30 pass left open (FixedSizePool's own class comment
// notes bytes_reserved() reporting is real but that "live-accounting wiring
// is deferred to whichever phase adds this pool's actual consumers"). Every
// time the wrapped pool actually grows or shrinks a block (never on an
// ordinary acquire()/release() that only recycles an existing slot), this
// releases whatever lease it currently holds and requests a fresh one sized
// to the pool's new footprint via FixedSizePool::on_reserved_bytes_changed,
// so MemoryStatus::category_bytes always reflects real reserved bytes
// instead of a one-time estimate taken at construction. A declined
// admission (budget pressure) is intentionally non-fatal here: pool memory
// for small descriptor objects is tiny relative to model weights/KV cache,
// so this is best-effort visibility, not admission control -- the pool
// keeps working either way, it just goes unregistered until the next
// growth/shrink event succeeds in reserving.
template <typename T>
class BudgetTrackedPool final {
public:
    BudgetTrackedPool(MemoryBudgetManager& memory, MemoryCategory category,
                      bool interactive, std::size_t block_capacity = 64U)
        : memory_(memory), category_(category), interactive_(interactive),
          pool_(block_capacity) {
        pool_.on_reserved_bytes_changed = [this](std::size_t total_bytes) {
            update_lease(total_bytes);
        };
    }
    ~BudgetTrackedPool() {
        if (!lease_id_.empty()) memory_.release(lease_id_);
    }
    BudgetTrackedPool(const BudgetTrackedPool&) = delete;
    BudgetTrackedPool& operator=(const BudgetTrackedPool&) = delete;

    FixedSizePool<T>& pool() noexcept { return pool_; }
    // Diagnostic/test-only: the lease id currently registered against
    // MemoryBudgetManager, empty when nothing is currently reserved
    // (freshly constructed, fully shrunk, or the last reserve() attempt was
    // declined).
    const std::string& lease_id() const noexcept { return lease_id_; }

private:
    void update_lease(const std::size_t total_bytes) {
        if (!lease_id_.empty()) {
            memory_.release(lease_id_);
            lease_id_.clear();
        }
        if (total_bytes == 0U) return;
        MemoryEstimate estimate;
        estimate.transient_bytes = total_bytes;
        const auto admission = memory_.reserve(category_, estimate, interactive_);
        if (admission.admitted) lease_id_ = admission.lease_id;
    }

    MemoryBudgetManager& memory_;
    MemoryCategory category_;
    bool interactive_;
    FixedSizePool<T> pool_;
    std::string lease_id_;
};

// Phase 26: cooperative cancellation token for background model warm-up,
// matching the shape already established by AsyncReadCancellationToken
// (Phase 21) and RetrievalPlanner's DeadlineTaskPool (Phase 24) rather than
// introducing a third cancellation idiom.
class WarmupCancellationToken final {
public:
    void cancel() noexcept { cancelled_.store(true, std::memory_order_release); }
    bool is_cancelled() const noexcept {
        return cancelled_.load(std::memory_order_acquire);
    }

private:
    std::atomic_bool cancelled_{false};
};

enum class WarmupOutcome {
    completed,
    cancelled,
    skipped_low_memory,
    skipped_system_pressure
};

// Phase 26: runs `step` (one bounded unit of warm-up work -- e.g. a short
// generate() call that faults model weights and primes the runner's
// KV-cache machinery) repeatedly until `step` returns true (warm-up is
// done), `token` is cancelled, MemoryBudgetManager reports the process
// should not be doing background work right now (permits_background_work()
// -- the same signal Phase 14's pressure model already exposes), the
// caller's optional thermal/storage/interactive pressure probe asks it to
// yield, or
// `maximum_steps` is exhausted. Checked *between* steps only, never
// mid-step -- the same cooperative-cancellation shape as DeadlineTaskPool
// in retrieval.cpp, deliberately not a second worker-pool implementation
// since Phase 26 only ever needs one cancellable task per model at a time.
// Runs synchronously on the calling thread; a caller wanting this "in the
// background" wraps the call in its own std::thread and calls token.cancel()
// from elsewhere (this keeps the primitive itself deterministically
// testable without racing a background thread).
WarmupOutcome run_cancellable_warmup(const std::function<bool()>& step,
                                     WarmupCancellationToken& token,
                                     const MemoryBudgetManager& memory,
                                     std::size_t maximum_steps = 64U,
                                     const std::function<bool()>&
                                         should_yield = {});

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
    // Phase 24: filename/path-match adapter -- reuses the relative_path
    // metadata every chunk already carries (no new index data required),
    // matching chunks whose path contains `path_fragment` as a substring.
    std::vector<IndexChunk> search_path(const std::string& path_fragment,
                                        std::size_t maximum_results) const;
    // Phase 24: call_graph adapter -- heuristic call-site scan (see
    // src/indexing.cpp's call_site_match for exactly what counts as a call
    // site). Not a parser; scope is C++/Python/JavaScript/TypeScript.
    std::vector<IndexChunk> search_calls(const std::string& symbol,
                                         std::size_t maximum_results) const;
    // Phase 24: type_reference adapter -- heuristic type-position scan (see
    // type_reference_match in src/indexing.cpp).
    std::vector<IndexChunk> search_type_usage(const std::string& type_name,
                                              std::size_t maximum_results) const;
    // Phase 24: dependency_neighbour adapter -- heuristic import/include
    // graph built on the fly from every chunk's text (see
    // import_targets_for in src/indexing.cpp); no persisted dependency data.
    std::vector<IndexChunk> search_dependency_neighbours(
        const std::string& anchor_relative_path,
        std::size_t maximum_results) const;
    // Phase 24: bounded full enumeration -- needed by the semantic_embedding
    // adapter, which must consider every chunk rather than ones matching a
    // literal.
    std::vector<IndexChunk> all_chunks(std::size_t maximum_results) const;
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
    // Phase 24: read-only filename/path-match lookup, same generation/
    // availability contract as search_text/search_symbol above.
    IndexSearchResult search_path(const std::string& project_id,
                                  const std::string& path_fragment,
                                  std::size_t maximum_results) const;
    // Phase 24: call_graph/type_reference/dependency_neighbour/full-
    // enumeration adapters -- same generation/availability contract as the
    // lookups above; see ProjectIndexer's declarations for what each does.
    IndexSearchResult search_calls(const std::string& project_id,
                                   const std::string& symbol,
                                   std::size_t maximum_results) const;
    IndexSearchResult search_type_usage(const std::string& project_id,
                                        const std::string& type_name,
                                        std::size_t maximum_results) const;
    IndexSearchResult search_dependency_neighbours(
        const std::string& project_id,
        const std::string& anchor_relative_path,
        std::size_t maximum_results) const;
    IndexSearchResult all_chunks(const std::string& project_id,
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

// Phase 24: every retrieval strategy the planner knows about. Every entry
// now has a real adapter (retrieval_strategy_has_adapter() -- this build can
// execute all of them). The four below the recency boost are also gated at
// runtime on an optional per-instance dependency this planner may or may not
// have been constructed with (an embedding runner, an MCP registry/gateway,
// a user memory store) -- a planner instance missing one of those records a
// dynamic "not configured for this instance" skip reason instead of running
// the strategy, distinct from (and layered on top of) the static
// build-capability question retrieval_strategy_has_adapter() answers. See
// RetrievalPlanner's class comment and retrieve_uncached() in retrieval.cpp.
enum class RetrievalStrategy {
    exact_symbol,
    exact_text,
    lexical,
    filename_path,
    recent_change,
    // Real adapters, each gated at runtime on an optional planner
    // dependency (see RetrievalPlanner's constructor).
    semantic_embedding,
    mcp_resource,
    call_graph,
    type_reference,
    git_diff,
    dependency_neighbour,
    conversation_memory
};

// Phase 24: git_diff adapter -- shells out to the system `git` binary
// (sandboxed via run_sandboxed_process(), see src/git_retrieval.cpp) for
// uncommitted changes, falling back to the most recent commit's diff when
// the working tree is clean. Returns an empty result (not an error) whenever
// `project_root` isn't a git working tree or `git` can't be run.
std::vector<IndexChunk> git_diff_search(const std::filesystem::path& project_root,
                                        std::size_t maximum_results,
                                        std::uint64_t max_total_bytes = 32U * 1024U);

// Phase 24: mcp_resource adapter -- lists and reads resources on outbound
// MCP servers enabled/authorized for `project_id` through the existing
// McpOutboundRegistry/McpOutboundGateway (mcp_outbound.cpp), keeping only
// resources whose uri/name lexically matches `query_tokens`. Performs no
// authorization decision itself -- McpOutboundGateway::invoke() already
// enforces the registry's rules on every call. Best-effort: a server that
// fails to list/read is silently skipped (see src/mcp_retrieval.cpp).
std::vector<IndexChunk> mcp_resource_search(
    McpOutboundRegistry& registry, McpOutboundGateway& gateway,
    const std::string& requester_id, const std::set<std::string>& requester_scopes,
    const std::string& project_id, const std::vector<std::string>& query_tokens,
    std::size_t maximum_results, std::atomic_bool& cancellation);

// Phase 24: semantic_embedding adapter -- real cosine-similarity search over
// `candidates`' embeddings (computed through embedding_runner.embed(), the
// same POST /v1/embeddings backend call RunnerSupervisor already makes for
// Phase 21's knowledge index). `cache`, when non-null, memoizes each
// candidate's embedding under CacheCategory::embedding keyed by chunk
// digest; nullptr still works, just re-embeds every call. Bounded by
// `deadline` -- stops considering further candidates (not mid-computation)
// once it passes, returning whatever was already scored. See
// src/semantic_retrieval.cpp.
std::vector<IndexChunk> semantic_embedding_search(
    RunnerSupervisor& embedding_runner, CacheManager* cache,
    const std::string& project_id, const std::vector<IndexChunk>& candidates,
    const std::string& query_text, std::size_t maximum_results,
    std::chrono::steady_clock::time_point deadline);

std::string to_string(RetrievalStrategy strategy);
// True only for strategies this build can actually execute.
bool retrieval_strategy_has_adapter(RetrievalStrategy strategy) noexcept;
// Every strategy that retrieval_strategy_has_adapter() returns false for,
// paired with a human-readable "no adapter" reason -- used to populate
// RetrievalOutcome::disabled_strategy_reasons without duplicating the list
// at each call site.
const std::vector<std::pair<RetrievalStrategy, std::string>>&
disabled_retrieval_strategy_reasons();

// Phase 24: coarse worker-budget tag for a retrieval request/stage.
// Interactive (a user is actively waiting on this chat turn) gets the full
// bounded worker count; background (speculative/prefetch) requests get a
// reduced worker count so they cannot starve an interactive request that
// shares DeadlineTaskPool's small thread budget.
enum class RetrievalPriority { interactive, background };

// Phase 24: deterministic, keyword/shape-based classification of a query's
// retrieval intent -- no ML, no embeddings, just cheap textual signals
// (presence of a path-looking token, a diagnostic-looking string, or an
// identifier-shaped token) mapped to the categories that are meaningful
// given the strategies this planner can actually run.
enum class RetrievalRequestClassification {
    completion,
    symbol_explanation,
    navigation,
    documentation,
    generic_lexical
};

std::string to_string(RetrievalRequestClassification classification);
RetrievalRequestClassification classify_retrieval_request(
    const std::string& query_text);

// Phase 16: deadline-bound hybrid retrieval over Phase 15 project indexes.
struct RetrievalRequest {
    ProjectRecord project;
    // Phase 24: identity/policy-generation pair used (together with project
    // and the settings fields below) as the in-flight join key -- two
    // requests only ever share one in-flight computation when every one of
    // these fields matches, so joining can never cross an authorization or
    // project boundary.
    std::string requester_id;
    // Phase 24: only consulted by the mcp_resource adapter, forwarded
    // verbatim into McpOutboundCall::actor_scopes so
    // McpOutboundGateway::invoke() authorizes each call against the
    // requester's real scopes rather than an implicit "trust everything"
    // default. Every other adapter ignores this field.
    std::set<std::string> requester_scopes;
    std::uint64_t policy_generation{0};
    std::string query_text;
    std::chrono::milliseconds deadline{1500};
    std::uint64_t maximum_context_bytes{16U * 1024U};
    std::uint64_t maximum_chunks_per_source{6U};
    std::uint64_t maximum_total_chunks{20U};
    RetrievalPriority priority{RetrievalPriority::interactive};
    // Phase 24: per-strategy opt-outs for the three expensive/IO-bound
    // adapters, mirrored from AppConfig::retrieval_semantic_embedding_enabled/
    // retrieval_git_diff_enabled/retrieval_mcp_resource_enabled by the
    // caller that builds this request (see server.cpp) -- defaulting true
    // here too so a test/fixture that never sets these keeps every
    // strategy this planner instance has a live dependency for.
    bool semantic_embedding_enabled{true};
    bool git_diff_enabled{true};
    bool mcp_resource_enabled{true};
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

// Phase 24: `chunk` carries only metadata (id/path/language/offset/digest)
// once fused -- its `text` field is cleared and the actual bytes live once
// in the retrieval call's shared arena buffer, addressed by `reference`.
// materialize() is only ever called for candidates ContextBudgeter::apply
// admits, and only once per unique chunk id (fusion already dedups by id
// before this struct is even constructed), so duplicate evidence surfaced
// by more than one strategy never pays for a second copy of its bytes.
struct RetrievalCandidate {
    IndexChunk chunk;
    ChunkReference reference;
    RetrievalDisclosureEntry disclosure;
};

struct RetrievalOutcome {
    std::string context_text;
    std::vector<RetrievalDisclosureEntry> disclosure;
    std::string strategy;
    bool partial{false};
    std::string diagnostic;
    // Phase 24: the deterministic classification this request received, and
    // the "no adapter" reasons for every declared-but-disabled strategy --
    // callers push both onto QueryTrace without RetrievalPlanner depending
    // on QueryCoordinator.
    std::string classification;
    std::vector<std::string> disabled_strategy_reasons;
};

// Chooses the least expensive sufficient strategy across an explicit staged
// list (cheap-exact symbol/exact-text first, lexical/path/heuristic
// call-graph/type-reference second, the expensive/IO-bound strategies --
// semantic embedding, MCP resource, conversation memory, git diff -- next,
// then the always-on post-fusion boosts -- recent-change and dependency-
// neighbour -- last), running independent strategy steps across bounded
// parallel workers with a hard wall-clock deadline and sticky sufficiency
// (once any candidate-generating stage finds evidence, later, more
// expensive candidate-generating stages are skipped entirely). On expiry it
// stops launching further steps and returns whatever evidence bounded
// workers already produced rather than blocking; it never performs a
// security or membership decision itself -- callers must have already
// authorized the caller against `project` before calling retrieve(), and
// every optional adapter dependency below (embedding runner, MCP
// registry/gateway, memory store) is itself never an authorization
// decision-maker either. Every RetrievalStrategy (see enum above) now has a
// real adapter; the four expensive/IO-bound ones only run when this
// instance was constructed with their optional dependency -- a planner
// built without one records a runtime "not configured for this instance"
// skip reason instead of silently omitting the strategy (see
// retrieve_uncached()'s stamp_common in retrieval.cpp).
class RetrievalPlanner final {
public:
    // Every dependency after `indexes` is optional (nullable) so every
    // existing test fixture and call site keeps compiling and behaving
    // exactly as before -- pass nullptr for any adapter this planner
    // instance shouldn't (or, in a test, doesn't need to) run. `memory`
    // (Phase 30) accounts the fusion-candidate pool; `user_memory` backs
    // conversation_memory; `embedding_runner`+`embedding_cache` back
    // semantic_embedding (embedding_cache alone, with no runner, is never
    // used); `mcp_registry`+`mcp_gateway` back mcp_resource (both are
    // required together -- either alone is treated as "not configured").
    explicit RetrievalPlanner(ProjectIndexService& indexes,
                              MemoryBudgetManager* memory = nullptr,
                              UserMemoryStore* user_memory = nullptr,
                              RunnerSupervisor* embedding_runner = nullptr,
                              CacheManager* embedding_cache = nullptr,
                              McpOutboundRegistry* mcp_registry = nullptr,
                              McpOutboundGateway* mcp_gateway = nullptr);
    RetrievalOutcome retrieve(const RetrievalRequest& request) const;
    // Diagnostic only (mirrors SharedBuffer::use_count()'s convention):
    // counts how many times retrieve_uncached() actually ran, so tests can
    // assert the in-flight join table actually joined concurrent duplicate
    // work rather than only comparing outcomes (which a deterministic
    // planner would produce identically either way). Never use this for
    // synchronization decisions.
    std::uint64_t uncached_invocation_count() const noexcept;

private:
    RetrievalOutcome retrieve_uncached(const RetrievalRequest& request) const;

    ProjectIndexService& indexes_;
    MemoryBudgetManager* memory_{nullptr};
    UserMemoryStore* user_memory_{nullptr};
    RunnerSupervisor* embedding_runner_{nullptr};
    CacheManager* embedding_cache_{nullptr};
    McpOutboundRegistry* mcp_registry_{nullptr};
    McpOutboundGateway* mcp_gateway_{nullptr};
    mutable std::atomic<std::uint64_t> uncached_invocations_{0};
    // Phase 24: request-key -> shared_future join table. The leader (first
    // caller to observe a given key with nothing in flight) actually runs
    // retrieve_uncached() and publishes its result to every follower that
    // arrived while it was running; the entry is removed before the leader
    // returns, so this is a join for genuinely concurrent duplicate work,
    // never a standing cache (repeated sequential calls always re-run).
    mutable std::mutex inflight_mutex_;
    struct InflightRetrieval final {
        std::mutex mutex;
        std::condition_variable ready;
        std::optional<RetrievalOutcome> outcome;
        std::exception_ptr exception;
        bool completed{false};
        std::size_t followers{0U};
    };
    mutable std::map<std::string, std::shared_ptr<InflightRetrieval>> inflight_;
};

// Applies per-source and total chunk/byte caps to already-ranked candidates.
// Candidates must arrive sorted by descending score; the budgeter accepts
// the highest-ranked evidence first and never truncates a chunk's text to
// fit more chunks in. `segment` is the shared arena BufferView every
// admitted candidate's `reference` is relative to -- materialize() is
// called exactly once per admitted candidate, here, and nowhere else.
class ContextBudgeter final {
public:
    static RetrievalOutcome apply(std::vector<RetrievalCandidate> ranked,
                                  const BufferView& segment,
                                  const std::string& strategy, bool partial,
                                  std::string diagnostic,
                                  std::uint64_t maximum_context_bytes,
                                  std::uint64_t maximum_chunks_per_source,
                                  std::uint64_t maximum_total_chunks);
};

// Phase 16 exit evidence: an authored case names the natural-language query,
// the source file that should be recovered, and a marker that must appear in
// admitted context. The evaluator compares the hybrid planner with a literal
// full-query text lookup against the same published index generation.
struct RetrievalEvaluationCase {
    std::string name;
    std::string query_text;
    std::string expected_relative_path;
    std::string expected_context_marker;
};

struct RetrievalEvaluationReport {
    std::uint64_t cases{0};
    std::uint64_t hybrid_hits{0};
    std::uint64_t full_text_hits{0};
    std::uint64_t deadline_violations{0};
    std::uint64_t context_budget_violations{0};
    std::uint64_t hybrid_total_microseconds{0};
    std::uint64_t phase_sixteen_total_microseconds{0};
    std::uint64_t hybrid_highest_case_microseconds{0};
    std::uint64_t phase_sixteen_highest_case_microseconds{0};
    bool latency_improved{false};
    bool improved{false};
};

RetrievalEvaluationReport evaluate_retrieval_quality(
    ProjectIndexService& indexes, const RetrievalPlanner& planner,
    RetrievalRequest request,
    const std::vector<RetrievalEvaluationCase>& cases);

// Phase 17 declared one segment per kind of repeated work worth avoiding.
// Phase 22 expands that to the full L0-L5 layering the plan calls for --
// file-level, parsed/derived, retrieval/ranking, prompt-preparation, and
// model/hardware/environment metadata each get their own independently
// bounded category so one large scan in any one of them cannot evict another
// category's hot working set (categories are already separate `Segment`s in
// cache.cpp, so this costs no new eviction logic, only finer admission
// boundaries). `tokenization` and `retrieval_result` are the two names
// already load-bearing (src/inference.cpp, src/server.cpp) and keep their
// exact spelling; every other value here is newly declared, some (like
// Phase 16/24's disabled-adapter strategies) without a producer yet.
enum class CacheCategory {
    file_metadata,
    file_content,
    parsed_document,
    source_chunk,
    symbol,
    retrieval_result,
    reranking,
    embedding,
    tokenization,
    prompt_template,
    prompt_fragment,
    model_manifest,
    download_metadata,
    hardware_probe,
    tuning_profile,
    mcp_resource,
    static_web_asset
};

std::string to_string(CacheCategory category);
bool cache_category_from_string(const std::string& text, CacheCategory& out);

// Every field that must match for a cached value to still be valid for a
// caller. `policy_generation` and `index_generation` make membership/policy
// changes and index republication an automatic miss rather than something a
// cache must actively chase down and purge; `version_tag` folds in
// parser/chunker/embedding/tokenizer/model/backend/prompt-template identity
// so any of those changing is also an automatic miss.
struct CacheKey {
    std::string user_id;
    std::string project_id;
    std::uint64_t policy_generation{0};
    std::string canonical_identity;
    std::string content_digest;
    std::string version_tag;
    std::uint64_t index_generation{0};

    std::string to_cache_id() const;
};

struct CacheCategoryStatus {
    std::uint64_t capacity_bytes{0};
    std::uint64_t used_bytes{0};
    std::uint64_t entries{0};
    std::uint64_t hits{0};
    std::uint64_t misses{0};
    std::uint64_t evictions{0};
    std::uint64_t oldest_entry_age_seconds{0};
    // Phase 22 segmented-eviction/negative-cache visibility.
    std::uint64_t protected_entries{0};
    std::uint64_t pinned_entries{0};
    std::uint64_t streaming_entries{0};
    std::uint64_t negative_entries{0};
    std::uint64_t admission_rejections{0};
};

struct CacheStatus {
    std::map<CacheCategory, CacheCategoryStatus> categories;
};

struct CachePolicy {
    std::uint64_t maximum_bytes_per_category{64ULL * 1024ULL * 1024ULL};
};

// Byte-bounded, security-partitioned cache over disk-backed entries. Every
// entry is addressed by a versioned CacheKey (see above), so a value is only
// ever returned to a caller that presents the exact tenant/user/project,
// policy generation, content identity/digest, component-version, and index
// generation it was written under -- cross-project or cross-user reuse is
// structurally impossible rather than policy-enforced. A cached lookup or
// store is never itself an authorization decision: callers must already have
// authorized the caller for the project before calling get()/put(), the same
// discipline RetrievalPlanner (Phase 16) already follows.
class CacheManager final {
public:
    CacheManager(std::filesystem::path cache_root, MemoryBudgetManager& memory,
                CachePolicy policy);
    ~CacheManager();
    CacheManager(const CacheManager&) = delete;
    CacheManager& operator=(const CacheManager&) = delete;

    std::optional<std::string> get(CacheCategory category, const CacheKey& key);
    void put(CacheCategory category, const CacheKey& key, std::string value);
    // Streaming entries represent one-pass scan/read-ahead data. They are
    // persisted and checksummed like ordinary entries but never promoted
    // to protected on a hit and are evicted before probationary data.
    void put_streaming(CacheCategory category, const CacheKey& key,
                       std::string value);
    // Phase 22: pinned entries are exempt from capacity-driven eviction
    // (administrator/streaming use only -- never a substitute for the
    // version-bound key already making staleness impossible) until
    // explicitly unpinned or invalidated by project/policy.
    void pin(CacheCategory category, const CacheKey& key);
    void unpin(CacheCategory category, const CacheKey& key);
    // Short-lived, version-bound "known absent" marker so a repeated miss
    // (e.g. a lookup that requires an expensive round trip to discover
    // nothing exists) does not repeat the expensive work every call. Bound
    // to the same CacheKey generations as a normal entry, so a policy
    // change, index republication, or new content immediately invalidates
    // it exactly like a positive entry -- it can never hide newly granted
    // access or newly completed work past `ttl`.
    void put_negative(CacheCategory category, const CacheKey& key,
                      std::chrono::seconds ttl);
    bool is_negative(CacheCategory category, const CacheKey& key);
    // Drops every entry for one project (e.g. on project deletion). Not
    // required for staleness -- generation-versioned keys already make a
    // republished index or a policy change an automatic miss -- but keeps
    // disk/RAM bytes from lingering for a project that no longer exists.
    void invalidate_project(const std::string& project_id);
    // Bumps the process-lifetime policy generation counter. Every CacheKey
    // built after this call embeds the new generation, so every entry
    // written under a prior generation becomes unreachable immediately.
    void invalidate_policy();
    std::uint64_t current_policy_generation() const;
    // Phase 34 (full completion pass): a genuine live hot-swap of the byte
    // quota -- unlike invalidate_policy() above (which only bumps the
    // generation counter so old entries become unreachable), this actually
    // changes maximum_bytes_per_category, mirroring MemoryBudgetManager::
    // set_policy()'s shape so AdaptiveController can shrink/grow the cache
    // quota under pressure the same way it already adjusts memory policy.
    // Existing entries over the new (lower) quota are left for the next
    // trim()/natural eviction rather than force-evicted synchronously here.
    void set_policy(CachePolicy policy);
    CachePolicy policy() const;
    CacheStatus status() const;
    // Administrative forced eviction down to each category's configured
    // capacity (a no-op for categories already within budget).
    void trim();
    void clear(std::optional<CacheCategory> category = std::nullopt);
    static std::string to_json(const CacheStatus& status);

private:
    void put_with_disposition(CacheCategory category, const CacheKey& key,
                              std::string value, bool streaming);
    class State;
    std::unique_ptr<State> state_;
};

// Phase 34 (2026-08-13): named operating modes an administrator selects, or
// leaves on `automatic` so AdaptiveController::evaluate() chooses the
// best-fit mode itself each cycle from live signals.
// `administrator_custom` applies exactly the configured PerformanceCeilings
// with no automatic tuning at all; every other named mode biases
// evaluate()'s scoring toward its own priority (e.g. `lowest_latency`
// favors higher inference concurrency and shorter idle-unload;
// `minimal_memory` favors the opposite).
enum class PerformanceMode {
    minimal_memory,
    balanced,
    lowest_latency,
    maximum_throughput,
    battery_saver,
    quiet_thermal_conservative,
    administrator_custom,
    automatic
};
std::string to_string(PerformanceMode mode);

// Administrator-configured ceilings no automatic adjustment may ever
// exceed -- every value AdaptiveController proposes is clamped to this
// struct before it is ever applied, and MemoryBudgetManager::set_policy()
// independently re-validates its own bounds regardless. Defaults mirror
// MemoryPolicy's own existing defaults rather than being more permissive.
struct PerformanceCeilings {
    std::uint32_t max_inference_concurrency{4U};
    std::uint32_t max_queued_inference{16U};
    std::uint32_t max_index_workers{4U};
    std::uint32_t max_context_tokens{8192U};
    std::uint32_t min_idle_unload_seconds{60U};
    std::uint32_t max_idle_unload_seconds{3600U};
    // Bounds how large one evaluate() call's step on any single numeric
    // knob may be, expressed as a percentage of the knob's current value --
    // the plan's "bounded step size" stability control.
    unsigned int max_step_percent{25U};
    // The plan's "max changes per interval" stability control.
    std::uint32_t max_changes_per_interval{3U};
    std::uint32_t interval_seconds{300U};
    // The plan's "minimum dwell time" / "cooldown" stability controls: no
    // second adjustment is applied until this many seconds have passed
    // since the last one.
    std::uint32_t minimum_dwell_seconds{120U};
};

// Every live signal the plan requires the controller to weigh, following
// the same "declare and disclose every signal, even ones a given decision
// rule does not yet act on" discipline RunnerSelectionSignals (Phase 33)
// and RoutingSignals (Phase 29) already established. Fields marked
// "disclosed only" below have no real rolling-aggregate probe behind them
// in this pass -- left std::optional and populated only by a caller that
// has computed one itself, the same honest-gap convention already used for
// e.g. RunnerSelectionSignals::thermal_headroom_percent.
struct AdaptiveSignalSnapshot {
    MemoryStatus memory;
    std::map<SchedulingClass, SchedulingClassStatus> scheduler;
    CacheStatus cache;
    // Disclosed only: Phase 13 records TTFT/tokens-per-second per
    // individual QueryTrace, not as a rolling aggregate: a caller that
    // wants this populated computes its own rolling average from recent
    // traces (e.g. QueryTraceStore) before calling evaluate().
    std::optional<double> average_ttft_ms;
    std::optional<double> average_tokens_per_second;
    // Disclosed only: Phase 78 records cache-hit decisions per request in
    // InferenceMetricsStore; a caller aggregates its own rate the same way.
    std::optional<double> cache_hit_rate_percent;
    // Disclosed only: no per-cycle thermal/power probe is wired to this
    // struct in this pass -- Phase 19/30A's GPU utilization/thermal probing
    // is control-plane-wide evidence recorded through CalibrationService,
    // not a live per-evaluate()-call signal yet.
    std::optional<double> thermal_headroom_percent;
    std::optional<double> estimated_power_draw_watts;
    std::uint32_t active_user_count{0U};
    bool on_battery_power{false};
    // Phase 34 (full completion pass): real, cheap inputs a caller already
    // has to hand -- probe_hardware_topology()'s own core count, and
    // whatever GPU-layer figure CalibrationService::resolve() most recently
    // recommended for the model currently loaded -- so thread_count/
    // gpu_offload below can compute a genuine bounded-reduction target
    // instead of the placeholder direction-only value this struct's
    // absence forced before. Left std::optional and unset when a caller has
    // neither on hand (no live launch-tuning recommendation is computed
    // that cycle, same honest-gap convention as thermal_headroom_percent).
    std::optional<unsigned int> host_thread_count;
    std::optional<unsigned int> calibrated_gpu_layers;
    std::optional<unsigned int> calibrated_batch_tokens;
};

// One proposed or applied change to a single named parameter.
// `confidence` is the fraction of AdaptiveController's rolling pressure-
// agreement window that supports this direction of change -- the plan's
// "confidence requirement" stability control, disclosed rather than
// hidden inside a boolean.
struct AdaptiveAdjustment {
    std::string parameter;
    std::string reason;
    double previous_value{0.0};
    double proposed_value{0.0};
    double confidence{0.0};
};

struct AdaptiveControllerReport {
    PerformanceMode active_mode{PerformanceMode::automatic};
    PerformanceMode selected_mode{PerformanceMode::balanced};
    // Adjustments actually applied to a live-mutable target this cycle
    // (currently: MemoryBudgetManager::set_policy() fields only -- see the
    // AdaptiveController class comment's honest scope note).
    std::vector<AdaptiveAdjustment> applied;
    // Every other adjustment evaluate() computed but either has no live
    // setter to apply to yet, or is gated by dwell time/cooldown/max-
    // changes-per-interval/confidence this cycle -- still surfaced so the
    // Phase 35 administration UI can show it as a recommendation.
    std::vector<AdaptiveAdjustment> proposed_not_yet_applied;
    std::uint64_t evaluation_epoch_seconds{0U};
    // Phase 34 (full completion pass): the ceilings this evaluation ran
    // under, so the Performance settings page can pre-fill its ceilings
    // editor with the administrator's current values instead of only ever
    // showing blanks.
    PerformanceCeilings ceilings;
};

// Phase 34: bounded, hysteresis-guarded automatic tuning extending Phase 19
// CalibrationService's advisory profiles into a live controller. See
// docs/PLAN.md Phase 34's stability-control deliverable -- minimum dwell
// time, hysteresis, bounded step size, cooldown, rolling measurement,
// confidence requirement, safe rollback, and max-changes-per-interval are
// each a real, independently testable check in evaluate() below, not
// asserted only in documentation.
//
// Honest scope note (updated for the full-completion pass): every plan-
// listed knob now has a real, genuinely-consumed target computed by
// evaluate() below. Three shapes of "applied" exist, and each knob is one
// of them:
//   - Instantly live via a mutable object's set_policy(): inference
//     concurrency, queued-inference ceiling, index worker count (this is
//     also "background-job rate" -- background indexing is what that name
//     refers to in this codebase; there is no separate background-job
//     subsystem), default context tokens, and now idle_unload_seconds and
//     the cache byte quota (CacheManager::set_policy(), new this pass).
//   - Applied at the *next natural load* rather than instantly, because the
//     knob is a launch-time argument to a separate llama.cpp process with
//     no live-reload primitive, and forcing an unload+reload of an
//     in-flight model was explicitly rejected as too disruptive: thread
//     count, GPU offload layers, batch size (prompt-processing batch
//     tokens), and NUMA local placement. See AdaptiveLaunchRecommendation
//     and ensure_model_loaded()'s overlay of it in server.cpp.
//   - Read queue depth and KV placement remain disclosed-only: no live
//     setter exists for either in this codebase (KV placement beyond the
//     existing KvPrecision admission path Phase 27 already applies has no
//     separate knob to compute a target for).
// Prefetch distance and warm-up policy are a permanent, documented scope
// limit rather than a temporarily-deferred gap (the same "documented scope
// limit, not a gap" precedent Phase 53 established for its own unimplemented
// operations): this codebase has no prefetch-distance or warm-up-policy
// subsystem at all to apply a computed value to, and inventing one with no
// real caller would be exactly the speculative machinery this project's own
// engineering norms reject. evaluate() therefore never fabricates a
// recommendation for either.
// Phase 34 (full completion pass): thread_count/gpu_offload/numa_local_
// placement/parallel_slots are launch-time arguments to a separate llama.cpp
// process, not a live-mutable object like MemoryBudgetManager -- there is no
// way to apply a changed recommendation to an already-running model without
// forcibly unloading and reloading it (disruptive, and deliberately not done
// automatically here; see ensure_model_loaded()'s overlay of this struct in
// server.cpp). Unset fields mean "no override -- use calibration/manifest
// defaults exactly as before this struct existed."
struct AdaptiveLaunchRecommendation {
    std::optional<unsigned int> thread_count;
    std::optional<unsigned int> gpu_offload_layers;
    std::optional<bool> numa_local_placement;
    // Prompt-processing batch size (llama.cpp's --batch-size/-b,
    // LaunchTuning::batch_tokens) -- the plan's "batch size" knob. Reduced
    // under pressure to shrink the compute-buffer memory a launch reserves;
    // never grown past whatever a calibration profile already recommended.
    std::optional<unsigned int> batch_tokens;
};

class AdaptiveController final {
public:
    explicit AdaptiveController(PerformanceCeilings ceilings = {});

    void set_mode(PerformanceMode mode);
    PerformanceMode mode() const noexcept;
    void set_ceilings(PerformanceCeilings ceilings);
    PerformanceCeilings ceilings() const noexcept;

    // Runs one evaluation cycle against `signals` and `memory` (whose
    // set_policy() this may call when a due, confident, in-bounds
    // memory-related adjustment exists) and, when `cache` is supplied,
    // `cache`'s own set_policy() for the cache-quota knob. now_epoch_seconds
    // drives dwell-time/cooldown/interval bookkeeping so this is
    // deterministically testable without depending on a real wall clock.
    AdaptiveControllerReport evaluate(const AdaptiveSignalSnapshot& signals,
                                      MemoryBudgetManager& memory,
                                      std::uint64_t now_epoch_seconds,
                                      CacheManager* cache = nullptr);

    // The most recently computed launch-time overrides (thread_count,
    // gpu_offload_layers, numa_local_placement, parallel_slots) -- see
    // AdaptiveLaunchRecommendation's own comment for why these are consumed
    // at the next natural model load rather than applied instantly.
    AdaptiveLaunchRecommendation launch_recommendation() const noexcept;

    // Reverts the most recently applied change back to the MemoryPolicy
    // that was active immediately before it -- the plan's "failed
    // recommendations revert to the last safe profile" exit criterion.
    // A no-op if nothing has been applied yet.
    void rollback(MemoryBudgetManager& memory);

    static std::string to_json(const AdaptiveControllerReport& report);

private:
    struct AppliedChange {
        std::uint64_t applied_epoch_seconds{0U};
        MemoryPolicy policy_before;
    };
    mutable std::mutex mutex_;
    PerformanceMode mode_{PerformanceMode::automatic};
    PerformanceCeilings ceilings_;
    std::uint64_t last_change_epoch_seconds_{0U};
    std::uint32_t changes_this_interval_{0U};
    std::uint64_t current_interval_epoch_seconds_{0U};
    std::optional<AppliedChange> last_applied_;
    AdaptiveLaunchRecommendation launch_recommendation_;
    // Rolling agreement window over recent evaluate() calls' memory
    // pressure reading -- AdaptiveAdjustment::confidence for a memory-
    // related proposal is the fraction of this window agreeing with the
    // proposed direction, so a single noisy sample can never trigger a
    // live change on its own.
    std::deque<MemoryPressure> pressure_history_;
};

// Phase 36: full performance benchmark matrix and regression gate.
//
// Honest scope note (the same "declare the gap, don't guess" precedent
// Phase 30A/32 already established): the plan's full matrix spans physical
// dimensions a single host cannot manufacture on demand -- multiple storage
// media (HDD/SATA SSD/NVMe), multiple physical machines, and GPU-offloaded
// hardware that may not be present on the host running this build. What
// ships here is real for every dimension actually controllable in software
// on one host at run time: cache cold/warm (CacheManager::trim() before a
// "cold" run), sequential concurrency depth, prompt/context size
// (BenchmarkProfile's existing quick/standard/extended tiers), and whatever
// accelerator mode the current launch actually used (recorded from
// RunnerMetrics, never assumed). An administrator runs the matrix across
// whichever of those axes their real hardware supports; a result is only
// ever compared against a previous run sharing its exact fingerprint (see
// PerformanceCertificationRecord::fingerprint()), so mismatched environments
// are never presented as a direct comparison -- the plan's own exit
// criterion. The five named regression test groups are real, independently
// callable checks against this codebase's own live decision logic (below),
// not fabricated pass results.
struct RegressionThresholds {
    double max_ttft_regression_percent{20.0};
    double max_memory_increase_percent{15.0};
    // Negative means throughput is allowed to regress by up to this many
    // percent before the gate fails it; positive would require an actual
    // improvement to pass.
    double min_throughput_percent{-10.0};
    double max_quality_regression_percent{5.0};
    double max_cpu_increase_percent{25.0};
    double max_queue_wait_increase_percent{25.0};
    double max_storage_amplification_percent{25.0};
};

struct RegressionMetricComparison {
    std::string metric;
    double baseline{0.0};
    double current{0.0};
    double delta_percent{0.0};
    double threshold_percent{0.0};
    bool passed{true};
};

struct RegressionCheckResult {
    std::string name;
    bool passed{false};
    std::string detail;
};

struct PerformanceCertificationRecord {
    std::string id;
    std::string build_id;
    std::string hardware_id;
    std::string model_id;
    std::string backend_version;
    std::string prompt_suite_hash;
    std::string settings_json{"{}"};
    std::string cache_state{"warm"};          // "cold" | "warm"
    std::string accelerator_mode{"unknown"};  // recorded, never assumed
    BenchmarkProfile profile{BenchmarkProfile::quick};
    std::uint32_t concurrency{1U};
    std::uint64_t created_epoch_seconds{0};
    std::uint64_t ttft_microseconds{0};
    std::uint64_t total_elapsed_microseconds{0};
    std::uint64_t prompt_tokens{0};
    std::uint64_t generated_tokens{0};
    std::uint64_t peak_resident_memory_bytes{0};
    // Real wall-clock time from RequestScheduler::admit() to the ticket
    // becoming ready under SchedulingClass::benchmark's own weighted-fair
    // queue -- not a synthesized figure.
    std::uint64_t queue_wait_microseconds{0};
    // Delta of this process's own cumulative disk-read bytes
    // (probe_system_utilization()) across the run, the same real per-
    // process I/O counter CalibrationService already uses for its own
    // disk_read_bytes evidence.
    std::uint64_t storage_bytes_read{0};
    // Phase 36 benchmark-gap pass: read/write operation counts, distinct
    // from storage_bytes_read's byte count -- see SystemUtilizationSample::
    // disk_read_operations's comment.
    std::uint64_t storage_read_operations{0};
    std::uint64_t storage_write_operations{0};
    // This process's own commit charge (PROCESS_MEMORY_COUNTERS_EX::
    // PrivateUsage on Windows -- see probe_process_resources()'s comment in
    // platform.cpp for why this equals "commit_bytes" in this codebase's
    // existing ProcessResourceSample), sampled once after the run.
    std::uint64_t commit_bytes{0};
    // This process's own page-fault count (probe_process_resources()'s
    // ProcessResourceSample::page_faults) across the run. On Windows this
    // is PROCESS_MEMORY_COUNTERS_EX::PageFaultCount, which counts every
    // page fault (soft and hard) -- Windows exposes no per-process hard-
    // fault-only counter without ETW tracing, which this codebase does not
    // do, so this is reported honestly as "page faults", not "hard page
    // faults", rather than mislabeling a soft+hard count as hard-only.
    std::uint64_t page_faults{0};
    double average_cpu_percent{0.0};
    double quality_score{0.0};  // passed_cases / total_cases of the embedded quality run
    std::vector<RegressionCheckResult> group_results;
    std::vector<RegressionMetricComparison> comparisons;
    bool accepted{false};
    std::string rejection_reason;

    // Identity used to find a comparable prior run -- matched
    // host/model/backend/settings/prompt-suite/cache-state/profile, the
    // plan's exact "matched ... fingerprints" requirement.
    std::string fingerprint() const;
};

class PerformanceCertificationStore final {
public:
    PerformanceCertificationStore() = default;
    explicit PerformanceCertificationStore(RecordStore& records);
    void add(const PerformanceCertificationRecord& record);
    std::vector<PerformanceCertificationRecord> all() const;
    // The most recent *accepted* run sharing `fingerprint`, or nullopt if
    // none exists yet -- a rejected run is still persisted (see add()) but
    // never offered as the comparison baseline for the next one.
    std::optional<PerformanceCertificationRecord> previous_accepted(
        const std::string& fingerprint) const;

private:
    void restore();
    RecordStore* record_store_{nullptr};
    std::vector<PerformanceCertificationRecord> records_;
};

// The five named regression test groups from docs/PLAN.md Phase 36. Each is
// a real, independently callable check against this codebase's own live
// decision logic (ModelRouter, TuningProfileStore,
// projected_resident_exceeds_safe_physical_capacity(), a fresh, self-
// contained PromptSessionManager instance) rather than a fabricated pass --
// safe to run on every certification pass since none of them mutate
// production state.
RegressionCheckResult check_runner_attribution_regression(
    const RunnerMetrics& metrics, const std::string& accelerator_policy);
RegressionCheckResult check_low_memory_regression();
RegressionCheckResult check_prompt_cache_regression();
RegressionCheckResult check_calibration_regression();
RegressionCheckResult check_model_routing_regression();

// Applies `thresholds` to `current` against `baseline`, appending one
// RegressionMetricComparison per named metric with the plan's exact
// "max TTFT regression, max memory increase, min throughput benefit, max
// quality regression, max queue-wait increase, max CPU increase" list, and
// returns true only when every comparison passes.
bool compare_against_baseline(const PerformanceCertificationRecord& baseline,
                              PerformanceCertificationRecord& current,
                              const RegressionThresholds& thresholds);

class PerformanceCertificationRunner final {
public:
    PerformanceCertificationRunner(RunnerSupervisor& inference,
                                   BenchmarkStore& quality_store,
                                   CacheManager& cache,
                                   RequestScheduler& scheduler,
                                   PerformanceCertificationStore& store,
                                   RegressionThresholds thresholds = {});

    RegressionThresholds thresholds() const;
    void set_thresholds(const RegressionThresholds& thresholds);

    // Runs one certification pass: the existing quality BenchmarkRunner
    // suite (real generate() calls), the five regression check groups, and
    // real live metrics (TTFT of the first case, total elapsed, peak
    // resident memory, real queue wait measured by actually admitting one
    // SchedulingClass::benchmark ticket through `scheduler` and timing
    // wait_until_ready(), and real storage bytes read measured as the
    // delta of probe_system_utilization()'s cumulative disk-read counter
    // across the run). cache_state == "cold" trims the cache first
    // (CacheManager::trim()) so the run measures a genuinely cold cache
    // rather than only claiming to. Compares against the previous accepted
    // run sharing this run's exact fingerprint, if any, applies
    // `thresholds_`, and persists the resulting accepted/rejected verdict
    // either way -- silently dropping failing evidence would defeat the
    // point of a regression gate.
    PerformanceCertificationRecord run(
        const std::string& model_id, const std::string& backend_version,
        const std::string& build_id, const std::string& hardware_id,
        BenchmarkProfile profile, const std::string& cache_state,
        std::uint32_t concurrency, const std::string& accelerator_policy,
        const std::atomic_bool& cancellation);

    static std::string to_json(const PerformanceCertificationRecord& record);

private:
    RunnerSupervisor& inference_;
    BenchmarkStore& quality_store_;
    CacheManager& cache_;
    RequestScheduler& scheduler_;
    PerformanceCertificationStore& store_;
    RegressionThresholds thresholds_;
};

// Phase 32 (2026-08-13, evidence-pending): speculative decoding
// draft/target compatibility checking and the per-request enable/disable
// decision engine. Honest scope note (see docs/PLAN.md Phase 32's status
// entry for the full reasoning): this codebase's RunnerSupervisor/
// LlamaCppAdapter launch exactly one model per external backend process
// (see LaunchSpec in inference.cpp) -- there is no dual-model (draft +
// target resident together) launch path yet, and adding one safely
// requires Phase 26 warm-state management for a second concurrently
// resident runner (itself only validated for a single model at a time so
// far) plus Phase 27 KV accounting for the combined memory cost of two
// models loaded at once, neither of which this pass touched. What ships
// here is the real, independently testable decision logic every dual-model
// execution path would need regardless of how it launches the second
// model: exact compatibility checking, acceptance-rate tracking, and the
// bounded per-request enable/disable rule -- deliberately NOT wired to any
// live generation call site, so this phase changes no existing request's
// behavior. The plan's exit criterion ("generation throughput improves on
// representative prompts") is therefore explicitly unvalidated, not
// claimed -- there is no execution path yet to measure.
struct DraftTargetCompatibilityResult {
    bool compatible{false};
    std::vector<std::string> incompatibility_reasons;
};

// Exact-match compatibility, never a heuristic "close enough": speculative
// decoding is only ever correct when the draft model's proposed tokens are
// verified against literally the same vocabulary the target model uses, so
// every check here is an exact equality, and a single mismatch anywhere
// fails the whole check. Honest limitation: ModelManifest has no separate
// vocabulary-size/tokenizer-identity field in this codebase (see
// ModelManifest in this header) -- architecture string equality is used as
// the best available proxy, which is a real but incomplete approximation
// of true tokenizer/vocabulary identity; this is disclosed here rather
// than silently treated as sufficient.
DraftTargetCompatibilityResult check_draft_target_compatibility(
    const ModelManifest& target, const ModelManifest& draft);

// Rolling acceptance-rate tracker for one (target, draft) pair. Every
// generate() step that would run under real dual-model execution records
// how many of the draft's proposed tokens the target actually accepted;
// this is the evidence dynamic per-request disablement (below) is judged
// against, never a fixed assumption.
class SpeculativeDecodingStats final {
public:
    explicit SpeculativeDecodingStats(std::size_t rolling_window = 50U);
    void record_step(std::uint32_t draft_tokens_proposed,
                     std::uint32_t draft_tokens_accepted);
    // Fraction of proposed draft tokens accepted over the rolling window,
    // or nullopt when no step has been recorded yet (never fabricates a
    // starting assumption).
    std::optional<double> acceptance_rate() const;
    std::size_t steps_recorded() const noexcept;

private:
    mutable std::mutex mutex_;
    std::size_t rolling_window_;
    std::deque<std::pair<std::uint32_t, std::uint32_t>> steps_;  // proposed, accepted
};

// Every signal the plan requires the per-request disablement rule to
// weigh, following the same "declare and disclose every signal" discipline
// RunnerSelectionSignals/AdaptiveSignalSnapshot already established.
struct SpeculativeDecodingRequestContext {
    std::optional<double> measured_acceptance_rate;  // from SpeculativeDecodingStats
    std::uint64_t combined_memory_estimate_bytes{0};
    std::uint64_t available_memory_bytes{0};
    std::uint32_t requested_max_tokens{0};
    bool draft_runner_queued{false};
    // llama.cpp's own server speculative-decoding implementation runs the
    // general rejection-sampling algorithm (Leviathan et al.), which is
    // mathematically valid for any temperature/top-p/top-k/repeat-penalty
    // sampling, not only exactly-greedy decoding -- it does NOT need this
    // control plane to replicate any accept/reject math itself, since
    // llama-server does that internally once launched with
    // --model-draft. What it does NOT support is grammar-constrained or
    // logit-bias-modified decoding, where the verification step would be
    // comparing against a distribution the draft model never actually
    // sampled from. This field is true whenever the request's sampling
    // uses none of those unsupported features -- GenerationOptions (see
    // this header) exposes no grammar/logit-bias/json-schema field at all,
    // so every live chat request through this control plane already
    // qualifies today.
    bool sampling_supported_by_speculative_verification{true};
    // Disclosed only: no per-runner thermal probe feeds this in this pass
    // (see RunnerSelectionSignals::thermal_headroom_percent's identical
    // honest gap).
    std::optional<double> thermal_headroom_percent;
};

struct SpeculativeDecodingDecision {
    bool enabled{false};
    std::string reason;
};

// Pure decision function -- the plan's exact deliverable list: low
// acceptance rate, overhead exceeding savings (approximated here as
// combined memory pressure leaving no real headroom), short requests,
// memory pressure, a queued draft runner, incompatible sampling settings,
// or thermal throttling all independently disable speculative decoding for
// this one request; every condition must clear for `enabled` to be true.
// `minimum_acceptance_rate`/`minimum_tokens_to_bother` are administrator-
// tunable rather than hardcoded, mirroring PerformanceCeilings' own
// administrator-configured-bound convention (Phase 34).
SpeculativeDecodingDecision decide_speculative_decoding_for_request(
    const SpeculativeDecodingRequestContext& context,
    double minimum_acceptance_rate = 0.6,
    std::uint32_t minimum_tokens_to_bother = 64U);

// Real dual-model launch path (closes the exact gap the Phase 32 status
// entry named: "there is no dual-model (draft+target concurrently
// resident) launch path yet"). Filters `candidates` down to every model
// check_draft_target_compatibility() accepts against `target`, then returns
// the smallest compatible one (the draft with the most speed potential --
// mirrors this codebase's existing "smaller is always at least as good"
// reasoning already used for other size-driven picks). Returns nullopt when
// no candidate is compatible, never a best-effort guess at an incompatible
// pair. `candidates` may include `target` itself; it is always excluded
// (check_draft_target_compatibility() already rejects target.id == draft.id).
std::optional<ModelRecord> select_speculative_draft_candidate(
    const ModelManifest& target, const std::vector<ModelRecord>& candidates);

// Phase 32: durable, administrator-submitted measured acceptance rate for
// one (target, draft) model pair. Deliberately separate from
// AdvancedOptimizationRegistry's evidence (Phase 20): that registry's
// AdvancedOptimizationEvidence is a one-time global admission gate for the
// "speculative_decoding" feature as a whole (before/after throughput,
// never a per-pair figure), while decide_speculative_decoding_for_request()
// needs a real measured acceptance rate for the *specific* pair about to be
// launched together -- and this codebase never fabricates a starting
// assumption for an unproven pair (see SpeculativeDecodingStats' own class
// comment). An administrator records this only after actually observing a
// pair run (e.g. via an external benchmark, or a vendor's published
// figure); nothing here self-populates from live traffic in this pass.
class SpeculativeDecodingPairEvidenceStore final {
public:
    SpeculativeDecodingPairEvidenceStore() = default;
    explicit SpeculativeDecodingPairEvidenceStore(RecordStore& records);

    // Throws for an acceptance_rate outside [0.0, 1.0] -- the same
    // "reject rather than silently clamp a bad input" discipline every
    // other evidence-recording entry point in this codebase already
    // follows (see AdvancedOptimizationRegistry::record_evidence()).
    void record(const std::string& target_model_sha256,
               const std::string& draft_model_sha256,
               double acceptance_rate);
    std::optional<double> lookup(const std::string& target_model_sha256,
                                 const std::string& draft_model_sha256) const;
    struct PairRecord {
        std::string target_model_sha256;
        std::string draft_model_sha256;
        double acceptance_rate{0.0};
    };
    std::vector<PairRecord> all() const;

private:
    static std::string pair_key(const std::string& target_model_sha256,
                                const std::string& draft_model_sha256);
    RecordStore* records_{nullptr};
    mutable std::mutex mutex_;
    std::map<std::string, PairRecord> pairs_;
};

std::string speculative_decoding_pairs_json(
    const std::vector<SpeculativeDecodingPairEvidenceStore::PairRecord>& pairs);

struct RetrievalCacheBenchmarkReport {
    std::uint64_t iterations{0};
    std::uint64_t uncached_microseconds{0};
    std::uint64_t cached_microseconds{0};
    bool output_stable{false};
    bool latency_improved{false};
};

// Phase 17 exit evidence over the production serializer and CacheManager
// path. The caller supplies an already-authorized request/key pair.
RetrievalCacheBenchmarkReport benchmark_retrieval_cache(
    CacheManager& cache, const RetrievalPlanner& planner,
    const RetrievalRequest& request, const CacheKey& key,
    std::uint64_t iterations);

// Phase 18: in-process registry of llama.cpp server "slot" reuse eligibility
// for prompt-prefix / KV-session reuse across chat turns. Unlike CacheManager
// (Phase 17) this never persists to disk -- the state being tracked (a live
// KV cache inside the runner process) does not survive a runner restart
// either, so an in-memory map that is naturally empty after a restart has
// exactly the right lifetime. Every entry is keyed by chat id, which the
// caller has already authorized for the requesting user/project before ever
// reaching here (see HttpServer::State::send_chat_message /
// ChatStore::find_for_owner), so reuse can never cross a chat/user/project
// boundary.
struct SessionFingerprint {
    std::string model_sha256;
    std::string backend_executable;
    std::string architecture;
    unsigned int context_length{0};
    std::uint64_t project_index_generation{0};
    // Phase 19: a calibration profile can change launch-affecting settings
    // (GPU layers, batch size, mmap/mlock, thread count) that context_length
    // alone does not capture. Callers that apply calibrated launch tuning
    // set this to a hash of the tuning actually in effect so a recalibration
    // that changes those settings invalidates any cached KV slot instead of
    // falsely reusing it; callers that never apply calibration leave it
    // empty, which still compares equal turn-to-turn and changes nothing
    // for them.
    std::string settings_fingerprint;

    bool operator==(const SessionFingerprint& other) const;
};

// Phase 23: why try_reuse() did or didn't grant reuse -- an explicit,
// inspectable reason instead of a bare bool, per the plan's requirement
// for "explicit invalidation reason". `none` is the only reason paired
// with `reuse == true`.
enum class SessionInvalidationReason {
    none,                     // reuse granted
    no_prior_session,         // no entry recorded for this chat yet
    idle_expired,              // entry existed but exceeded idle retention
    fingerprint_mismatch,      // model/backend/architecture/settings changed
    prefix_diverged,           // generation_prompt is not a byte-prefix
                               // extension of the recorded prompt (e.g. an
                               // earlier turn was edited/resubmitted)
    prefix_ceiling_exceeded,   // a literal prefix match exists but exceeds
                               // the configured maximum retained prefix
                               // byte ceiling
};

struct SessionDecision {
    bool reuse{false};
    unsigned int slot_id{0};
    // Phase 23: length, in bytes, of the longest prefix generation_prompt
    // shares with the chat's previously recorded prompt. This is a byte
    // count rather than a token count deliberately: PromptSessionManager
    // works on raw prompt strings before tokenization (that's precisely
    // what lets try_reuse() stay a cheap in-memory comparison instead of a
    // runner round trip), so it has no tokenizer available to it here. A
    // caller that needs an actual token count can run this many bytes
    // through the now-cached RunnerSupervisor::tokenize() (Phase 23
    // tokenization cache) itself.
    std::size_t reusable_prefix_bytes{0};
    // Exact tokenizer-reported token count for the reusable prior prompt.
    // PromptSessionManager does not tokenize text itself: record() receives
    // the count returned by the runner for the successful prior turn and
    // try_reuse() returns that stored value only after the byte-prefix and
    // fingerprint checks above succeed. `reusable_prefix_tokens_exact` is
    // false for legacy/test callers that recorded no token count.
    std::uint64_t reusable_prefix_tokens{0};
    bool reusable_prefix_tokens_exact{false};
    // Byte offset into generation_prompt where it stops matching the
    // recorded prior prompt -- 0 when there is no prior session at all,
    // equal to reusable_prefix_bytes on both a clean prefix-extension
    // match and a rejected match (the longest common prefix is reported
    // either way, since it is useful diagnostic information even on a
    // miss).
    std::size_t divergence_offset{0};
    SessionInvalidationReason invalidation_reason{
        SessionInvalidationReason::no_prior_session};
    // The configured ceiling this decision was evaluated against, so a
    // caller can log/report why reuse was capped even when it succeeded.
    std::size_t prefix_byte_ceiling{0};
};

// Phase 27: per-slot KV-cache accounting, bounded context-aware
// reservation, and deterministic eviction ordering, layered over the same
// slot identity PromptSessionManager (Phase 18/23) already tracks.
//
// Scope note (docs/PLAN.md Phase 27), updated this pass: reduced-precision
// KV and cross-request prefix-tree sharing are now real, backend-validated
// mechanisms (--cache-type-k/v launch flags in models.cpp;
// run_kv_precision_quality_check() in src/kv_quality.cpp; shared-template
// prefix lookup in PromptSessionManager, session_cache.cpp) rather than
// pure accounting scaffolding -- but recording evidence (or building the
// shared-prefix mechanism) still never self-enables anything, exactly as
// AdvancedOptimizationRegistry (Phase 20) established. `precision_admitted()`
// and `prefix_sharing_admitted()` both stay false until an administrator
// calls `admit_precision()`/`admit_prefix_sharing()` explicitly, after
// reviewing recorded evidence -- there is no automatic threshold or
// evidence count that flips either gate on its own.
enum class KvPrecision { full, half, quantized_k, quantized_v };
enum class KvPlacement { cpu, gpu, split };
enum class KvSlotState { active, idle, failed_cancelled, expired };

struct KvSlotAccounting {
    unsigned int slot_id{0};
    std::string owning_user_id;
    std::string owning_chat_id;
    std::string owning_project_id;
    std::uint64_t context_length_tokens{0};
    std::uint64_t token_count{0};
    KvPrecision precision{KvPrecision::full};
    KvPlacement placement{KvPlacement::cpu};
    std::uint64_t bytes_reserved{0};
    bool pinned{false};
    // Marks a slot as holding a shareable immutable-prefix node rather than
    // a private per-chat slot; see the prefix-tree scope note above -- no
    // sharing is actually performed yet, this field only feeds the
    // deterministic eviction order's "large low-value reusable prefixes"
    // bucket ahead of time.
    bool reusable_prefix{false};
    std::uint64_t reuse_count{0};
    KvSlotState state{KvSlotState::active};
};

struct KvReservationResult {
    bool admitted{false};
    std::string reason;
    std::uint64_t granted_bytes{0};
};

// Backend-validation evidence for a reduced-precision KV policy. Recording
// evidence never enables the policy (see KvCacheManager::precision_admitted
// -- always false for anything but KvPrecision::full in this pass), exactly
// as AdvancedOptimizationEvidence/AdvancedOptimizationRegistry (Phase 20)
// already never let recorded evidence self-enable a feature.
struct KvPrecisionEvidence {
    KvPrecision precision{KvPrecision::half};
    std::string backend_hash;
    std::string quality_notes;
    bool quality_parity_verified{false};
};

class KvCacheManager final {
public:
    // `records`, when non-null, persists admit_precision()/
    // admit_prefix_sharing()'s admission state (never per-slot accounting,
    // which is process-lifetime only, same rationale as
    // PromptSessionManager) across a MasterAI restart -- mirroring
    // AdvancedOptimizationRegistry's own RecordStore-backed admission
    // persistence (Phase 20), so an administrator's explicit admission
    // decision does not silently reset on every restart. nullptr (the
    // default) keeps every existing call site/test compiling and behaving
    // exactly as before: admission state is then process-lifetime only.
    KvCacheManager(MemoryBudgetManager& memory,
                  std::uint64_t hard_max_bytes_per_slot,
                  std::uint64_t growth_step_bytes,
                  RecordStore* records = nullptr);
    ~KvCacheManager();
    KvCacheManager(const KvCacheManager&) = delete;
    KvCacheManager& operator=(const KvCacheManager&) = delete;

    // Admits a new slot at `initial.bytes_reserved` (rounded up to the next
    // growth step), never exceeding `hard_max_bytes_per_slot`.
    KvReservationResult reserve(KvSlotAccounting initial);
    // Grows an existing slot's reservation by bounded steps up to the hard
    // ceiling; refuses (does not partially grant) once the ceiling would be
    // exceeded.
    KvReservationResult grow(unsigned int slot_id,
                             std::uint64_t additional_bytes_requested);
    void touch(unsigned int slot_id);  // reuse: bumps reuse_count, marks active
    void set_state(unsigned int slot_id, KvSlotState state);
    void release(unsigned int slot_id);
    // Deterministic eviction order (docs/PLAN.md Phase 27): failed/
    // cancelled slots, expired idle prefixes, lowest-reuse private slots,
    // large low-value reusable prefixes, idle non-pinned sessions, then
    // nullopt ("safe rejection" -- no eviction candidate exists). An active
    // (in-flight) or pinned slot is never returned.
    std::optional<unsigned int> evict_one();
    std::optional<KvSlotAccounting> find(unsigned int slot_id) const;
    std::vector<KvSlotAccounting> status() const;
    static std::string to_json(const std::vector<KvSlotAccounting>& slots);

    // Backend-validated reduced-precision admission gate; see class-level
    // scope note. Always true for KvPrecision::full; for any other value,
    // true only after an administrator has explicitly called
    // admit_precision() for that exact precision -- recording evidence
    // alone (record_precision_evidence()) never flips this.
    bool precision_admitted(KvPrecision precision) const;
    void record_precision_evidence(const KvPrecisionEvidence& evidence);
    std::vector<KvPrecisionEvidence> precision_evidence() const;
    // Administrator-only explicit admission action -- see class-level scope
    // note. Not gated on evidence existing (an administrator may have
    // validated it out of band), but every production call site (see
    // server.cpp's admin KV precision admission route) requires the
    // administrator role before reaching here.
    void admit_precision(KvPrecision precision);

    // Cross-request prefix-tree sharing admission gate (docs/PLAN.md Phase
    // 27's "cross-request prefix-tree sharing" deliverable). False until an
    // administrator explicitly calls admit_prefix_sharing() -- see
    // PromptSessionManager::try_reuse_shared_template() (session_cache.cpp),
    // which consults this gate before ever resolving a shared-template
    // prefix lookup across different chats/users.
    bool prefix_sharing_admitted() const;
    void admit_prefix_sharing();

private:
    class State;
    std::unique_ptr<State> state_;
};

std::string to_string(KvPrecision precision);
std::string to_string(KvPlacement placement);
std::string to_string(KvSlotState state);

// Phase 27: real backend-validated quality-parity check -- both runners
// must already be loaded (one at KvPrecision::full, one at
// `candidate_precision`) against the same model/context by the caller; this
// function only runs `authored_prompts` through both and compares. See
// src/kv_quality.cpp. Never called by anything that itself decides
// admission -- the resulting KvPrecisionEvidence is for a human to review.
KvPrecisionEvidence run_kv_precision_quality_check(
    RunnerSupervisor& full_precision_runner, RunnerSupervisor& candidate_runner,
    KvPrecision candidate_precision,
    const std::vector<std::string>& authored_prompts,
    const std::string& backend_hash);

// Phase 27: identity for a cross-request shareable prefix. Deliberately
// carries no chat_id/user_id -- see try_reuse_shared_template()'s comment
// below for why that absence is the actual security property.
struct SharedTemplateKey {
    std::string prefix_content_sha256;
    std::string model_sha256;
    std::uint64_t policy_generation{0};
    std::set<std::string> authorized_roles;

    // Stable, order-independent serialization used as the internal lookup
    // key -- authorized_roles is a std::set so this is already sorted.
    std::string to_key() const;
};

class PromptSessionManager final {
public:
    // `max_retained_prefix_bytes` bounds how large a recorded prompt's
    // reusable byte-prefix is allowed to be before try_reuse() refuses
    // reuse outright (SessionInvalidationReason::prefix_ceiling_exceeded)
    // even though it is a literal, otherwise-valid prefix match. Without
    // this, one pathologically long-running chat could pin an
    // ever-growing KV cache in the runner indefinitely. Default chosen as
    // a generous multiple of typical multi-turn chat prompt sizes while
    // staying well under configuration.max_request_bytes's 16 MiB default
    // (masterai.hpp), so ordinary chats are never affected.
    static constexpr std::size_t kDefaultMaxRetainedPrefixBytes =
        4ULL * 1024ULL * 1024ULL;

    PromptSessionManager(
        unsigned int max_slots, std::uint32_t idle_retention_seconds,
        std::size_t max_retained_prefix_bytes = kDefaultMaxRetainedPrefixBytes);
    ~PromptSessionManager();
    PromptSessionManager(const PromptSessionManager&) = delete;
    PromptSessionManager& operator=(const PromptSessionManager&) = delete;

    // Looks up whether `chat_id`'s previous turn can be resumed on its
    // already-warm slot: the fingerprint must match exactly and
    // `generation_prompt` must extend the prior turn's prompt as a literal
    // byte-prefix (docs/PLAN.md Phase 18 "stable-prefix detection"), and
    // that prefix must fit within the configured byte ceiling. Reuse is
    // refused -- never guessed -- on any mismatch, missing entry,
    // idle-expired entry, non-prefix divergence, or ceiling overrun; see
    // SessionDecision::invalidation_reason for exactly which. No fuzzy
    // prefix matching is performed anywhere in this decision (explicit
    // Phase 23 requirement).
    SessionDecision try_reuse(const std::string& chat_id,
                              const SessionFingerprint& fingerprint,
                              const std::string& generation_prompt) const;
    // Records a successful (non-cancelled) generation as the new reusable
    // state for `chat_id`. When `reused_slot` is unset, allocates a fresh
    // slot, evicting the least-recently-used entry first if the pool is
    // full.
    unsigned int record(const std::string& chat_id,
                        const SessionFingerprint& fingerprint,
                        const std::string& generation_prompt,
                        std::optional<unsigned int> reused_slot,
                        std::optional<std::uint64_t> prompt_token_count =
                            std::nullopt);
    // Drops any session state for `chat_id` (model unload, chat deletion, or
    // a cancelled/failed generation that must not be reused next turn).
    void release(const std::string& chat_id);
    // Drops every entry (runner unloaded/restarted -- every slot's KV cache
    // is gone with it).
    void reset();
    std::size_t active_sessions() const;

    // Phase 27: cross-request prefix-tree sharing for explicitly-marked
    // public templates -- a second, deliberately separate lookup from
    // try_reuse() above. Keyed on (prefix content hash, model fingerprint,
    // policy generation, authorized role set) -- never chat_id or user_id --
    // so two different users under the *same* admin-approved policy/role
    // can share a slot only for the byte-identical public prefix; a private
    // (non-template) prompt never enters this lookup at all, since callers
    // decide what counts as a "template" and only ever call these two
    // methods for that explicit, opt-in case. Both refuse outright
    // (SessionInvalidationReason::no_prior_session, reuse=false) unless
    // `prefix_sharing_admitted` is true -- the caller is expected to pass
    // `kv_cache.prefix_sharing_admitted()`, so this mechanism existing at
    // all never itself turns cross-user sharing on.
    SessionDecision try_reuse_shared_template(
        const SharedTemplateKey& key, const std::string& generation_prompt,
        bool prefix_sharing_admitted) const;
    unsigned int record_shared_template(
        const SharedTemplateKey& key, const std::string& generation_prompt,
        std::optional<unsigned int> reused_slot,
        std::optional<std::uint64_t> prompt_token_count = std::nullopt);

private:
    class State;
    std::unique_ptr<State> state_;
};

// Phase 30A deliverable 4 (docs/PLAN.md Phase 30A, implementation-order item
// 4): the one background sweep that turns the already-built idle-unload and
// pressure-action primitives into something that actually runs
// unattended -- WarmModelTracker::apply_idle_timeout()/
// RunnerSupervisor::apply_idle_timeout() (Phase 26) and
// MemoryBudgetManager::sample()'s active_pressure_actions (Phase 14) were
// both callable but had no periodic caller before this. Mirrors
// ProjectWatcher's pimpl/worker-thread shape (src/project_watcher.cpp)
// rather than inventing a third background-thread idiom. Every dependency is
// an existing pointer/reference into HttpServer::State's already-owned
// objects -- this class owns none of them and creates no second memory
// authority or inference pipeline.
class MemorySweeper final {
public:
    // `inference`, `cache`, and `prompt_sessions` are all nullable exactly
    // like their HttpServer::State counterparts (no configured runner/cache
    // means nothing to sweep for that concern). `on_idle_unload` is invoked
    // immediately after an idle-timeout-triggered unload()/reset() so the
    // caller can release whatever runner_weights budget lease it is holding
    // (MemorySweeper itself has no visibility into that lease -- it lives in
    // HttpServer::State, see admit_runner_weights()/
    // release_runner_weights_lease()).
    // Phase 29/26: `runner_pool` is nullable exactly like the others -- when
    // absent (no multi-runner pool configured), sweeping behaves exactly as
    // before this parameter existed. When present, every one of its warm
    // runners is idle-timeout-checked the same way the single default
    // `inference` supervisor already is, closing the gap where a tiering
    // cascade could leave a smaller tier warm in the pool with nothing ever
    // sweeping it (see docs/PLAN.md Phase 26's pool-awareness note).
    MemorySweeper(MemoryBudgetManager& memory, RunnerSupervisor* inference,
                 CacheManager* cache, PromptSessionManager* prompt_sessions,
                 std::uint32_t idle_unload_seconds,
                 std::function<void()> on_idle_unload = {},
                 LocalRunnerPool* runner_pool = nullptr);
    ~MemorySweeper();
    MemorySweeper(const MemorySweeper&) = delete;
    MemorySweeper& operator=(const MemorySweeper&) = delete;

private:
    class State;
    std::unique_ptr<State> state_;
};

// Phase 23: compiled per-architecture chat-wrap template plus segmented
// prompt assembly built over shared immutable literal buffers, instead of
// server.cpp's previous repeated std::string += concatenation. Implemented
// in src/prompt_assembly.cpp, independent of HttpServer::State so it can
// be exercised directly by tests without a full server harness.

// Mirrors server.cpp's private ChatTemplate (system/user/assistant
// prefix+suffix literals, the trailing generation-prompt marker, and the
// architecture's stop sequence) but lives here so prompt_assembly.cpp and
// tests can use it without depending on server.cpp internals.
struct ChatWrapTemplate {
    std::string system_prefix, system_suffix;
    std::string user_prefix, user_suffix;
    std::string assistant_prefix, assistant_suffix;
    std::string generation_prompt;
    std::string stop_sequence;
};

// A ChatWrapTemplate's 8 literal fields, each parsed into a shared
// immutable BufferView exactly once. compiled_chat_template() caches one
// of these per distinct architecture name for the life of the process --
// the architecture set is small and static, so a full CacheManager entry
// would be pure overhead (see docs/PLAN.md Phase 23).
struct ChatTemplatePlan {
    BufferView system_prefix, system_suffix;
    BufferView user_prefix, user_suffix;
    BufferView assistant_prefix, assistant_suffix;
    BufferView generation_prompt;
    std::string stop_sequence;
};

// Returns the process-lifetime-cached compiled plan for `architecture`,
// building and caching it from `tmpl` on first use. Thread-safe. Callers
// must pass the same `tmpl` content for a given `architecture` on every
// call (true for every current caller, which derives both from the same
// static per-architecture table) -- a first-writer-wins race on a brand
// new architecture is otherwise harmless since the content is identical.
const ChatTemplatePlan& compiled_chat_template(const std::string& architecture,
                                               const ChatWrapTemplate& tmpl);

// Builds an ordered list of PromptSegments for one chat's full wrapped
// prompt (prior history, then the latest user turn, then the format's
// generation-prompt marker) over `plan`'s cached literal buffers,
// without concatenating any bytes. Each message's own content is copied
// once into its own SharedBuffer (necessary: PromptSegment/BufferView
// must own or share ownership of the bytes they view, and message content
// does not otherwise outlive this call) -- the win over the old
// concatenation approach is that the literal delimiters are never
// recopied, and the whole prompt is never repeatedly reallocated as one
// growing std::string while it is built.
std::vector<PromptSegment> assemble_chat_prompt_segments(
    const ChatTemplatePlan& plan, const std::vector<ChatMessage>& history,
    const std::string& latest_user_content);

// Materializes a vector<PromptSegment> into one contiguous std::string --
// the single point where segmented assembly rejoins the "backend needs one
// buffer" world (RunnerSupervisor::generate() takes a plain std::string).
// Reserves the exact total size up front so this is one allocation plus
// one copy pass, not the repeated-reallocation pattern the old
// std::string += approach had.
std::string materialize_prompt(const std::vector<PromptSegment>& segments);

// Phase 23: process-lifetime intern table restricted, by construction, to
// short (<= kMaxInternedLength byte) identifiers -- role names, route
// names, repeated JSON keys -- never arbitrary user messages or file
// content (explicit exclusion per docs/PLAN.md Phase 23 spec). Interning
// something longer throws std::invalid_argument rather than silently
// growing an unbounded cache of arbitrary strings; this is the structural
// enforcement of "ONLY high-repetition immutable identifiers" rather than
// a comment callers could ignore. Returns a reference into the table's own
// storage that stays valid for the rest of the process's lifetime.
constexpr std::size_t kMaxInternedLength = 128U;
const std::string& intern_identifier(const std::string& text);

// Internal (not a public API contract) length-prefixed serialization of a
// RetrievalOutcome for storage in the retrieval-result cache segment. Not
// JSON: RetrievalDisclosureEntry::score is a double and this codebase's JSON
// parser deliberately rejects floating-point numbers (json.cpp).
std::string serialize_retrieval_outcome(const RetrievalOutcome& outcome);
RetrievalOutcome deserialize_retrieval_outcome(const std::string& encoded);

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

// Declared here (defined further below, alongside AllowedCommandRecord) so
// McpInboundServer can hold a reference to it without reordering the whole
// Phase 84 tool-support section above the Phase 8 MCP section that predates it.
class AllowedCommandStore;

// Implements the pinned MCP server contract independently of its transport.
// Both stdio and Streamable HTTP pass bounded JSON-RPC messages through this
// class so capability, authorization, resource, and tool behavior cannot drift.
// allowed_commands lets the same run_command tool a connected chat can call
// be reached through MCP tools/call, sharing execute_chat_tool() (Phase 84)
// verbatim so the two surfaces' six built-in tools cannot drift apart.
class McpInboundServer final {
public:
    McpInboundServer(ProjectCatalog& projects,
                     std::filesystem::path models_root,
                     std::uint64_t memory_reserve_mib,
                     AllowedCommandStore& allowed_commands);

    std::string handle(const std::string& request_json,
                       const McpIdentity& identity,
                       std::atomic_bool& cancellation) const;

private:
    ProjectCatalog& projects_;
    std::filesystem::path models_root_;
    std::uint64_t memory_reserve_mib_{0};
    AllowedCommandStore& allowed_commands_;
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

// ---------------------------------------------------------------------------
// Agentic chat tool use (docs/PLAN.md Phase 84).
//
// Gives a chat turn (and, through the same dispatch table, an MCP-connected
// IDE) a small, explicit set of real tools -- read/list/search project
// files, write a file, delete a file, and run one admin-allow-listed
// external command -- instead of pure text completion. The design deliberately
// mirrors the existing outbound-MCP registry/sandboxing/audit shape
// (McpOutboundServer/McpOutboundRegistry/McpOutboundGateway above) rather
// than inventing a parallel one: a RecordStore-backed allow-list registry, a
// sandboxed-subprocess helper with the same Job Object/rlimit containment
// invoke_stdio() already uses, and the same AuditLog convention.
// ---------------------------------------------------------------------------

// Whether a tool call may run immediately or must pause for an explicit
// human Approve/Deny click. This is deliberately a separate axis from the
// admin allow-list below: a destructive pattern (delete, rm, format, a
// forced git reset, ...) is always high_risk even for an allow-listed
// executable, and the allow-list can never downgrade it back to safe.
enum class ChatToolRisk { safe, high_risk };

// Which operating system(s) an allow-listed executable applies to. This is
// informational/filtering metadata for the admin UI -- it does not gate
// run_command itself, since a record for the "wrong" OS simply never
// resolves to an executable that exists on this machine.
enum class CommandOs { windows, linux, both };

// One admin-approved external executable a chat/MCP `run_command` tool call
// is permitted to invoke, optionally restricted to specific projects. Tools
// that never shell out (read_file, search, list_directory, write_file,
// delete_file) do not need an entry here; only `run_command`'s target
// executable is gated by this registry.
struct AllowedCommandRecord {
    std::string id;
    // Matched against the caller-supplied executable name/path exactly
    // (case-insensitive on Windows) -- never a glob or regex, so an
    // allow-list entry cannot be widened by a clever argument.
    std::string executable;
    std::string description;
    ChatToolRisk risk_default{ChatToolRisk::safe};
    // Empty means every project the calling identity can already access;
    // non-empty narrows this executable to only those projects.
    std::set<std::string> allowed_project_ids;
    bool enabled{false};
    // Which OS this executable belongs to, shown as a column/badge in the
    // admin UI and used to seed the built-in Windows/Linux command catalog
    // (see AllowedCommandStore::ensure_default_catalog() in tool_exec.cpp).
    CommandOs os{CommandOs::both};
};

// Durable admin allow-list, persisted the same way McpOutboundRegistry
// persists McpOutboundServer records (see AllowedCommandStore's
// implementation in tool_exec.cpp).
class AllowedCommandStore final {
public:
    explicit AllowedCommandStore(RecordStore& records);
    AllowedCommandRecord register_command(AllowedCommandRecord command);
    // Updates an existing record in place (same id). Unlike
    // register_command(), this throws if the id is not already present, so
    // an edit can never accidentally create a fresh entry under a
    // client-supplied id.
    AllowedCommandRecord update_command(AllowedCommandRecord command);
    void remove(const std::string& command_id);
    std::optional<AllowedCommandRecord> find_by_executable(
        const std::string& executable) const;
    std::vector<AllowedCommandRecord> list() const;

private:
    void restore();
    void persist(const AllowedCommandRecord& command);
    // Adds the built-in catalog of well-known Windows/Linux/cross-platform
    // executables (enabled by default) for any entry whose executable
    // isn't already present, so run_command works out of the box and an
    // admin reviews/disables from a ready-made list instead of
    // hand-typing every command. Safe to call on every startup: it never
    // touches or duplicates an executable the admin already has a record
    // for.
    void ensure_default_catalog();
    RecordStore& records_;
    std::map<std::string, AllowedCommandRecord> commands_;
    // Guards commands_ against concurrent register/remove/find/list calls.
    mutable std::mutex mutex_;
};

// Result of one sandboxed external process run by run_sandboxed_process().
// Mirrors McpOutboundResult's success/cancelled/diagnostic shape, plus the
// exit code and separate stdout/stderr a shell-command tool result needs to
// show the model and the chat transcript.
struct ToolProcessResult {
    bool succeeded{false};
    bool cancelled{false};
    bool timed_out{false};
    int exit_code{-1};
    std::string standard_output;
    std::string standard_error;
    std::string diagnostic;
};

// Runs one argv-array executable with no shell interpretation (so no
// argument can ever be interpreted as shell syntax) inside the same
// containment invoke_stdio() already applies to outbound MCP stdio
// servers -- a Windows Job Object capping memory/process count, or Linux
// rlimit(CPU/FSIZE/AS/NPROC) plus prctl(PR_SET_NO_NEW_PRIVS) -- bounded by
// a wall-clock timeout, a combined stdout+stderr byte cap, and a
// cancellation flag polled during the wait loop. Declared in masterai.hpp
// (not mcp_outbound_internal.hpp) because it is shared by the chat tool
// loop and the MCP inbound tool dispatch, not private to outbound MCP.
ToolProcessResult run_sandboxed_process(
    const std::filesystem::path& executable,
    const std::vector<std::string>& arguments,
    const std::filesystem::path& working_directory,
    std::uint64_t timeout_seconds, std::uint64_t maximum_output_bytes,
    std::atomic_bool& cancellation);

// Classifies a proposed tool call by name and JSON arguments against the
// fixed destructive-pattern table (delete_file always; write_file that
// would blank out an existing file; run_command whose executable/arguments
// match rm/del/rmdir/format/DROP/TRUNCATE/reset --hard/clean -f/push
// --force/shutdown/taskkill/diskpart/reg delete and similar). This
// classification cannot be weakened by the admin allow-list, auto-drive
// mode, or the model's own request -- it is the single enforcement point
// for "destructive actions always need a human's explicit approval."
ChatToolRisk classify_tool_call_risk(const std::string& tool_name,
                                     const JsonValue& arguments);

// A tool call the chat loop has decided is high_risk and has therefore
// paused on, waiting for a human Approve/Deny decision (see
// PendingToolApprovalStore below and POST
// /api/v1/chats/{id}/tool-approvals/{approvalId} in server.cpp).
struct PendingToolApproval {
    std::string id;
    std::string chat_id;
    std::string user_id;
    std::string tool_name;
    std::string arguments_json;
    // Human-readable explanation of why this call was classified high_risk,
    // shown directly in the chat's Approve/Deny card.
    std::string reason;
    std::uint64_t created_epoch_seconds{0};
};

// Short-lived durable store for pending approvals -- durable (not just
// in-memory) so a paused turn survives a server restart, matching how
// every other in-flight job state in this codebase (downloads, training
// runs, memory-clean progress) is a RecordStore entry rather than
// process memory.
class PendingToolApprovalStore final {
public:
    explicit PendingToolApprovalStore(RecordStore& records);
    PendingToolApproval create(PendingToolApproval approval);
    std::optional<PendingToolApproval> find(const std::string& approval_id) const;
    void remove(const std::string& approval_id);

private:
    RecordStore& records_;
    mutable std::mutex mutex_;
};

// Outcome of one execute_chat_tool() call: `result_text` is what gets shown
// back to the model as the tool's result (and, truncated, in the chat
// transcript's tool-result card); `structured_json` is the same result in a
// machine-shaped form for the `tool_result` NDJSON event / MCP
// structuredContent.
struct ChatToolCallResult {
    bool succeeded{false};
    std::string result_text;
    std::string structured_json;
};

// Executes one of the six built-in tools -- read_file, list_directory,
// search, write_file, delete_file, run_command -- against a single project.
// The caller (server.cpp's chat tool loop, or mcp.cpp's tools/call
// dispatch) is responsible for classify_tool_call_risk() and, for a
// high_risk call, obtaining human approval *before* calling this function;
// execute_chat_tool() itself always executes immediately. Shared verbatim
// by both surfaces so chat and MCP tool behavior cannot drift apart. Only
// run_command consults `allowed_commands` (its target executable must be a
// registered, enabled, project-authorized entry); the other five tools are
// always available, bounded only by the project root containment every
// project file operation in this codebase already enforces (see
// read_project_text_file()).
ChatToolCallResult execute_chat_tool(const std::string& tool_name,
                                     const JsonValue& arguments,
                                     const ProjectRecord& project,
                                     AllowedCommandStore& allowed_commands,
                                     std::atomic_bool& cancellation);

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
    // Phase 30A: `settings_file` is the on-disk settings.json path the admin
    // configuration API (GET/PUT /api/v1/admin/config) round-trips through
    // ConfigurationManager::save_atomic() -- see State::settings_file below.
    // Left empty by the single-argument constructor above (and by the
    // host/port convenience constructor), in which case the admin
    // configuration API is unavailable rather than silently writing to a
    // path nobody chose.
    HttpServer(AppConfig configuration, std::filesystem::path settings_file);
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
