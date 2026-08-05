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

enum class LogLevel { debug, info, warning, error };

void log(LogLevel level, const std::string& event, const std::string& detail);
bool constant_time_equal(const std::string& left, const std::string& right) noexcept;
std::vector<std::uint8_t> secure_random(std::size_t size);
std::string sha256_hex(const std::string& value);
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
    // "PageFile" setting: an administrator-chosen substitute location for
    // MasterAI's own disk-backed cache/scratch area (see
    // resolve_page_file_root()). Empty means "use the existing default"
    // (runtime_root/"cache"), not the real Windows pagefile.
    std::filesystem::path page_file_root;
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
    std::uint32_t chat_max_reply_tokens{8192U};
    std::uint32_t chat_context_length{4096U};
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

std::string to_string(ThreadClass klass);
std::string hardware_topology_json(const HardwareTopology& topology);

struct ProcessResourceSample {
    std::uint64_t resident_memory_bytes{0};
    std::uint64_t private_memory_bytes{0};
    std::uint64_t commit_bytes{0};
    std::uint64_t page_faults{0};
};

ProcessResourceSample probe_process_resources();

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
// chat turn as bounded user-provided context, so they work with any model
// without granting persisted text system-instruction priority.
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

// Machine Learning foundation phase (docs/PLAN.md "Machine Learning
// Abilities" section 1-51): an administrator-only module for teaching and
// building models, covering everything from dataset ingestion through
// training, evaluation, deployment, and governance. That section describes
// 25 sidebar interfaces (Dashboard, Projects, Model Registry, Model
// Builder, Dataset Manager, ...); this registry is deliberately scoped down
// to just the Dashboard's real, honest starting state -- every interface
// beyond Dashboard is listed as "planned" and none of them exist yet. This
// mirrors how AdvancedOptimizationRegistry above starts a large gated
// section: a real, truthful acknowledgement that the subsystem exists and
// is enabled, with zero fabricated data standing in for work not yet done.
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
                     const std::string& model_task);
    std::optional<MLProject> find(const std::string& id) const;
    std::vector<MLProject> list() const;
    // Returns false (no-op) if the project doesn't exist, so callers can
    // turn that into a 404 the same way ChatStore::remove()'s callers do.
    bool set_status(const std::string& id, MLProjectStatus status);
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
                              const std::string& license);
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
                   const std::string& data_format);
    std::optional<Dataset> find(const std::string& id) const;
    std::vector<Dataset> list() const;
    bool set_approval_status(const std::string& id,
                             DatasetApprovalStatus status);
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
                         const std::string& category);
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
// dataset. Scoped down from the section's full surface (source-code/
// configuration/container version, hyperparameters, random seed, hardware,
// runtime, training/validation/evaluation metrics, checkpoints, logs,
// artifacts, tags, and side-by-side comparison) to identity, the project/
// model/dataset it relates to, and a lifecycle status -- none of the
// deferred fields mean anything before an actual training/evaluation
// executor exists to produce them.
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
                      const std::string& description);
    std::optional<Experiment> find(const std::string& id) const;
    std::vector<Experiment> list() const;
    bool set_status(const std::string& id, ExperimentStatus status);
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
                              const std::string& source_type);
    std::optional<ModelBuilderConfig> find(const std::string& id) const;
    std::vector<ModelBuilderConfig> list() const;
    bool set_status(const std::string& id, ModelBuilderConfigStatus status);
    // Replaces a configuration's build settings wholesale (the web UI always
    // submits the complete settings form, pre-filled from current values, so
    // partial merge semantics are unnecessary). Returns false for an unknown
    // id; throws std::invalid_argument for out-of-range values.
    bool configure(const std::string& id, const ModelBuilderSettings& settings);
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
// The deferred fields -- generator model, generator version, prompt,
// generation settings, confidence score, and original source linkage -- only
// mean something once a real generation executor exists to produce them,
// exactly as Phase 47 deferred its own content fields until an example-
// generation executor exists. The status enum reuses the same five-state
// reviewer workflow InstructionExampleStatus defined above: section 20
// requires that generated records carry a "human-review status" and "remain
// distinguishable from human-created and real-world data" until reviewed,
// the same rationale, so a synthetic record moves from draft through review
// to an approved or rejected outcome, or an archived discard -- it never
// queues, runs, or pauses the way TrainingJob/FineTuningJob's eleven-state
// job lifecycle would imply.
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

// Phase 49: docs/PLAN.md "Machine Learning Abilities" section 21
// (Embeddings and Vector Stores). Scoped down from the section's full field
// list (embedding-model version, vector dimensions, document count, chunk
// count, storage size, index type, security classification, access
// permissions, last rebuild date, associated subject packages/agents/
// deployed models) to identity, a free-text embedding_model field (section
// 21's "Register embedding models" operation could reference a
// ModelRegistryStore entry, but nothing yet produces a real embedding model
// registration to link against, so this stays free text like
// InstructionExample's subject_classification), a free-text distance_metric
// field (section 21 lists "Select distance metric" as an operation without
// naming a closed set), and an approval-status lifecycle -- not the full
// document-import/chunking/indexing pipeline the section describes, since
// that requires a real embedding executor. A vector store is a standalone
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
                                const std::string& strategy);
    std::optional<HyperparameterSearch> find(const std::string& id) const;
    std::vector<HyperparameterSearch> list() const;
    bool set_status(const std::string& id, HyperparameterSearchStatus status);
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
                                const std::string& operation);
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
                              const std::string& capture_reason);
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
// not reviewer-workflow content -- not the health/rollback machinery a
// real deployment executor will attach once it exists.
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

// A parsed tabular dataset: every feature column must be numeric; the
// target column decides the task (all-numeric -> regression, otherwise
// classification with targets stored as class-label indices).
struct TabularDataset {
    std::vector<std::string> feature_names;
    std::string target_name;
    bool classification{false};
    std::vector<std::string> class_labels;      // classification only
    std::vector<std::vector<double>> features;  // row-major, one row per example
    std::vector<double> targets;                // class index or numeric value
};

// Parses CSV text (header row required, quoted fields supported) into a
// TabularDataset. target_column names the label column; empty selects the
// last column. Throws std::runtime_error with a human-readable reason on
// any structural problem (missing column, non-numeric feature, too few
// rows, ...).
TabularDataset parse_tabular_csv(const std::string& csv,
                                 const std::string& target_column);

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

// Hyperparameters for one training run. Every field has a working default
// so a bare "run" request trains sensibly.
struct TabularTrainingOptions {
    std::uint32_t epochs{200};
    double learning_rate{0.05};
    double test_fraction{0.2};   // held-out share, [0, 0.9]
    std::uint32_t seed{42};      // deterministic shuffle/split
    std::uint32_t checkpoint_interval{50};  // epochs between checkpoints
};

// The learned model: standardization statistics plus weight rows (one row
// of n_features+1 values including bias for regression; one row per class
// for classification). This is the artifact that gets persisted and later
// reloaded for evaluation and prediction.
struct TrainedTabularModel {
    std::string model_id;         // owning ModelRegistryEntry id
    std::string training_job_id;  // job that produced it
    std::string method;  // "linear_regression" | "logistic_regression" |
                         // "softmax_regression"
    bool classification{false};
    std::vector<std::string> feature_names;
    std::string target_name;
    std::vector<std::string> class_labels;
    std::vector<double> feature_means;
    std::vector<double> feature_stddevs;
    std::vector<std::vector<double>> weights;
    std::uint64_t trained_at_epoch_seconds{0};
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

// Trains by full-batch gradient descent on standardized features. Picks the
// method from the dataset's task (regression vs 2-class vs k-class). Fills
// `model` (except model_id/training_job_id, which the caller owns) and
// returns the report. Throws std::runtime_error on an untrainable dataset.
TabularTrainingReport train_tabular_model(const TabularDataset& data,
                                          const TabularTrainingOptions& options,
                                          TrainedTabularModel& model);

// Scores an existing model against a dataset with the same schema (feature
// names must match; classification labels must be known to the model).
// Throws std::runtime_error on schema mismatch.
TabularEvaluationMetrics evaluate_tabular_model(const TrainedTabularModel& model,
                                                const TabularDataset& data);

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

// Phases 58-60: real local knowledge ingestion, vector indexing, and RAG
// retrieval. Documents are uploaded as bounded text through a browser file
// picker, hashed, split into overlapping chunks, and embedded by an authored
// deterministic hashing-vectorizer. The vectors are persisted with the exact
// source chunks; retrieval therefore produces repeatable evidence without a
// network service or third-party ML foundation. This is deliberately a local
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
    std::vector<double> embedding;
};

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
    KnowledgeDocument ingest(const std::string& owner_id,
                             const std::string& subject_id,
                             const std::string& vector_store_id,
                             const std::string& file_name,
                             const std::string& media_type,
                             const std::string& content);
    std::optional<KnowledgeDocument> find_document(
        const std::string& id) const;
    std::vector<KnowledgeDocument> list_documents() const;
    std::vector<KnowledgeChunk> chunks_for_store(
        const std::string& vector_store_id) const;
    bool remove_document(const std::string& id);

private:
    void restore();
    RecordStore* records_{nullptr};
    std::map<std::string, KnowledgeDocument> documents_;
    std::map<std::string, KnowledgeChunk> chunks_;
    mutable std::mutex mutex_;
};

RagRetrievalResult retrieve_knowledge(
    const KnowledgeIndexStore& index, const std::string& vector_store_id,
    const std::string& search_strategy, const std::string& query,
    std::size_t top_k = 5U);
std::string knowledge_document_json(const KnowledgeDocument& document);
std::string knowledge_documents_json(
    const std::vector<KnowledgeDocument>& documents);
std::string knowledge_index_profile_json(
    const std::string& vector_store_id,
    const std::vector<KnowledgeChunk>& chunks);
std::string rag_retrieval_result_json(const RagRetrievalResult& result);

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
    // Phase 30A: read-only access to the resolved policy (in particular
    // maximum_active_inference/maximum_queued_inference) so callers that
    // need to enforce the configured concurrency ceiling -- e.g.
    // send_chat_message()'s one-slot-under-cpu_only admission gate -- do not
    // have to keep a second copy of it in sync by hand.
    MemoryPolicy policy() const;
    static MemoryPolicy policy_for(ResourceProfile profile,
                                   const HardwareInfo& hardware,
                                   std::uint64_t hard_limit_bytes = 0U);
    static std::string to_json(const MemoryStatus& status);

private:
    class State;
    std::unique_ptr<State> state_;
};

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

// Phase 24: every retrieval strategy the planner knows about. The first
// group has a working adapter today (either since Phase 16, or added by
// Phase 24 against index metadata already tracked by indexing.cpp); the
// second group is intentionally declared but has no adapter wired to it
// yet (no embedding model, no MCP resource plumbing, no call-graph/type
// index, no git integration, no cross-session memory store exist in this
// codebase) -- retrieval_strategy_has_adapter() is the single source of
// truth callers use to decide whether a strategy can ever run, and
// RetrievalPlanner records a "no adapter" skip reason on every disabled
// entry rather than silently ignoring it or faking results for it.
enum class RetrievalStrategy {
    exact_symbol,
    exact_text,
    lexical,
    filename_path,
    recent_change,
    // Declared but disabled: no adapter exists in-repo yet.
    semantic_embedding,
    mcp_resource,
    call_graph,
    type_reference,
    git_diff,
    dependency_neighbour,
    conversation_memory
};

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
    std::uint64_t policy_generation{0};
    std::string query_text;
    std::chrono::milliseconds deadline{1500};
    std::uint64_t maximum_context_bytes{16U * 1024U};
    std::uint64_t maximum_chunks_per_source{6U};
    std::uint64_t maximum_total_chunks{20U};
    RetrievalPriority priority{RetrievalPriority::interactive};
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

// Chooses the least expensive sufficient index-backed strategy across an
// explicit staged list (cheap-exact symbol/exact-text first, lexical/path
// second, remaining enabled strategies -- currently just the recent-change
// recency boost -- last), running independent strategy steps across bounded
// parallel workers with a hard wall-clock deadline and sticky sufficiency
// (once any stage finds evidence, later, more expensive stages are skipped
// entirely). On expiry it stops launching further steps and returns
// whatever evidence bounded workers already produced rather than blocking;
// it never performs a security or membership decision itself -- callers
// must have already authorized the caller against `project` before calling
// retrieve(). Semantic/embedding, MCP-resource, call-graph, type-reference,
// git-diff, dependency-neighbour, and conversation-memory strategies remain
// forward work (see RetrievalStrategy) -- this planner covers every
// strategy that Phase 15's disk-backed index can actually serve today, plus
// Phase 24's own in-flight de-duplication of identical concurrent requests.
class RetrievalPlanner final {
public:
    // Phase 30: `memory` is optional (nullable) so every existing test
    // fixture and call site that predates this pass keeps compiling and
    // behaving exactly as before -- pass nullptr to opt out of the
    // fusion-candidate pool's MemoryBudgetManager accounting entirely (the
    // pool itself still works identically either way, since bytes_reserved()
    // registration is a diagnostic side effect of BudgetTrackedPool, not a
    // correctness dependency of RetrievalCandidate fusion).
    explicit RetrievalPlanner(ProjectIndexService& indexes,
                              MemoryBudgetManager* memory = nullptr);
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
// Scope note (docs/PLAN.md Phase 27): reduced-precision KV (half/quantized)
// and the cross-request prefix-tree sharing the plan describes both
// explicitly "require backend-validated support before any precision
// change is admitted" / must "never expose private conversation KV state
// across unauthorized boundaries" -- this codebase launches llama.cpp as an
// external process and has not validated either against it, so both stay
// declared-but-disabled here, following the exact honesty convention
// AdvancedOptimizationRegistry (Phase 20) already established: recording
// evidence is supported and tested, but it can never itself flip a
// precision policy or prefix-sharing on. What ships working and tested this
// pass: per-slot/layer accounting by context length, token count, dtype,
// and CPU/GPU/split placement; bounded-growth-step reservation against a
// hard per-slot ceiling; and the plan's deterministic eviction order.
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
    KvCacheManager(MemoryBudgetManager& memory,
                  std::uint64_t hard_max_bytes_per_slot,
                  std::uint64_t growth_step_bytes);
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
    // scope note. Always false for non-full precision in this pass.
    bool precision_admitted(KvPrecision precision) const;
    void record_precision_evidence(const KvPrecisionEvidence& evidence);

private:
    class State;
    std::unique_ptr<State> state_;
};

std::string to_string(KvPrecision precision);
std::string to_string(KvPlacement placement);
std::string to_string(KvSlotState state);

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
    MemorySweeper(MemoryBudgetManager& memory, RunnerSupervisor* inference,
                 CacheManager* cache, PromptSessionManager* prompt_sessions,
                 std::uint32_t idle_unload_seconds,
                 std::function<void()> on_idle_unload = {});
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
