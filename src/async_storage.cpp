// Phase 21: native asynchronous storage and prefetch engine.
//
// See masterai.hpp's Phase 21 block for the public contract and scope note.
// This unit implements: the measured storage-latency probe, the adaptive
// queue-depth policy, the pure read coalescer, the two IAsyncFileReader
// backends (Win32 IOCP / POSIX pread pool), the process-lifetime reader
// cache, and the read_file_bytes() opt-in-with-fallback entry point that
// models.cpp and indexing.cpp call instead of open-coding std::ifstream.
#include "masterai.hpp"

#include <algorithm>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <future>
#include <limits>
#include <map>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#include <sys/types.h>
#endif

namespace masterai {
namespace {

// Simple counting semaphore (C++17 has no std::counting_semaphore) used by
// both backends to bound in-flight requests to the adaptive queue depth.
class BoundedSemaphore final {
public:
    explicit BoundedSemaphore(const std::size_t permits) : permits_(permits) {}

    bool acquire(const AsyncReadCancellationToken& token) {
        std::unique_lock<std::mutex> lock(mutex_);
        while (permits_ == 0U) {
            if (token.is_cancelled()) return false;
            condition_.wait_for(lock, std::chrono::milliseconds(5));
        }
        --permits_;
        return true;
    }

    void release() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++permits_;
        }
        condition_.notify_one();
    }

private:
    std::mutex mutex_;
    std::condition_variable condition_;
    std::size_t permits_;
};

// RAII helper so every early-return path in read_range() still releases the
// semaphore permit it acquired.
class SemaphorePermit final {
public:
    SemaphorePermit(BoundedSemaphore& semaphore,
                    const AsyncReadCancellationToken& token)
        : semaphore_(semaphore), acquired_(semaphore_.acquire(token)) {
    }
    ~SemaphorePermit() { if (acquired_) semaphore_.release(); }
    SemaphorePermit(const SemaphorePermit&) = delete;
    SemaphorePermit& operator=(const SemaphorePermit&) = delete;

private:
    BoundedSemaphore& semaphore_;
    bool acquired_{false};
public:
    bool acquired() const noexcept { return acquired_; }
};

}  // namespace

// ---------------------------------------------------------------------
// Storage latency probe
// ---------------------------------------------------------------------

StorageLatencyProfile probe_storage_latency(
    const std::filesystem::path& scratch_directory,
    const std::string& storage_class) {
    StorageLatencyProfile profile;
    profile.storage_class = storage_class;
    // Fail safe before we've measured anything: if probing can't complete,
    // callers gating fan-out on `sequential` should never over-fan-out on a
    // device this function couldn't actually characterize.
    profile.sequential = true;
    profile.measured_read_latency_us = 0.0;

    std::error_code error;
    std::filesystem::create_directories(scratch_directory, error);
    if (error) return profile;
    const auto scratch_path = scratch_directory / "async_storage_probe.tmp";

    constexpr std::size_t scratch_bytes = 256U * 1024U;  // 256 KiB scratch file
    constexpr std::size_t sample_bytes = 4096U;          // 4KB probe reads, per spec
    constexpr int sample_count = 8;

    {
        std::ofstream scratch(scratch_path, std::ios::binary | std::ios::trunc);
        if (!scratch) return profile;
        const std::string filler(scratch_bytes, 'A');
        scratch.write(filler.data(), static_cast<std::streamsize>(filler.size()));
        if (!scratch) {
            std::filesystem::remove(scratch_path, error);
            return profile;
        }
    }

    std::ifstream input(scratch_path, std::ios::binary);
    if (!input) {
        std::filesystem::remove(scratch_path, error);
        return profile;
    }

    std::string buffer(sample_bytes, '\0');
    double total_microseconds = 0.0;
    int samples_taken = 0;
    const std::size_t page_count = scratch_bytes / sample_bytes;
    // Deterministic pseudo-random page offsets: we only need "not purely
    // sequential" access to measure seek+read cost, not statistical rigor,
    // so a fixed stride avoids any <random> seeding dependency.
    for (int sample = 0; sample < sample_count; ++sample) {
        const std::size_t page = (static_cast<std::size_t>(sample) * 7U + 3U) % page_count;
        const auto offset = static_cast<std::streamoff>(page * sample_bytes);
        const auto start = std::chrono::steady_clock::now();
        input.seekg(offset);
        input.read(&buffer[0], static_cast<std::streamsize>(sample_bytes));
        const auto end = std::chrono::steady_clock::now();
        if (!input) {
            input.clear();
            continue;
        }
        total_microseconds += std::chrono::duration<double, std::micro>(end - start).count();
        ++samples_taken;
    }
    input.close();
    std::filesystem::remove(scratch_path, error);

    if (samples_taken == 0) return profile;
    profile.measured_read_latency_us = total_microseconds / static_cast<double>(samples_taken);

    // Classify: a high measured per-4KB latency, or a device class the OS
    // itself reports as non-local/removable, means "don't fan out reads".
    // Everything else (fast local fixed storage) is eligible for bounded
    // parallel reads via adaptive_queue_depth() below.
    const bool slow_measured = profile.measured_read_latency_us >= 3000.0;
    const bool non_local_class =
        storage_class == "network" || storage_class == "removable" ||
        storage_class == "optical" || storage_class == "unknown";
    profile.sequential = slow_measured || non_local_class;
    return profile;
}

std::size_t adaptive_queue_depth(const StorageLatencyProfile& profile) {
    // Network/removable/optical media: the plan requires active model-weight
    // reads to be denied-by-default fan-out on this class of storage.
    // Depth 1 still permits a single async read (so the reader is usable at
    // all), but callers must not treat this as license to fan out.
    if (profile.storage_class == "network" || profile.storage_class == "removable" ||
        profile.storage_class == "optical") {
        return 1U;
    }
    // HDD-shaped latency or a profile explicitly marked non-fan-out: stay
    // serialized so parallel reads don't thrash a spinning disk's head or
    // regress sequential throughput.
    if (profile.sequential || profile.measured_read_latency_us >= 3000.0) {
        return 1U;
    }
    // NVMe-shaped (very low measured latency): a larger bounded parallel
    // depth is safe.
    if (profile.measured_read_latency_us > 0.0 && profile.measured_read_latency_us < 200.0) {
        return 8U;
    }
    // SATA SSD / fast-but-unclassified: a moderate bounded depth.
    return 4U;
}

// ---------------------------------------------------------------------
// Read coalescer
// ---------------------------------------------------------------------

std::vector<CoalescedReadPlan> coalesce_read_requests(
    const std::vector<CoalescedReadRequest>& requests,
    const std::uint64_t max_gap_bytes) {
    std::vector<CoalescedReadPlan> plans;
    if (requests.empty()) return plans;

    // Sort indices by (path, offset) so adjacent/overlapping same-file
    // ranges become neighbors; the original request_index is preserved in
    // each plan's members so callers can slice the merged buffer back out
    // to the caller that asked for it.
    std::vector<std::size_t> order(requests.size());
    for (std::size_t index = 0U; index < order.size(); ++index) order[index] = index;
    std::sort(order.begin(), order.end(), [&requests](const std::size_t left,
                                                       const std::size_t right) {
        if (requests[left].path != requests[right].path) {
            return requests[left].path < requests[right].path;
        }
        return requests[left].offset < requests[right].offset;
    });

    for (const auto index : order) {
        const auto& request = requests[index];
        const auto request_end =
            request.length > std::numeric_limits<std::uint64_t>::max() -
                                 request.offset
                ? std::numeric_limits<std::uint64_t>::max()
                : request.offset + request.length;
        if (!plans.empty() && plans.back().path == request.path) {
            auto& plan = plans.back();
            const auto plan_end = plan.offset + plan.length;
            // Merge when the new request starts at or before the current
            // plan's end plus the allowed gap -- i.e. adjacent or
            // overlapping, matching "adjacent...requests" from the spec.
            const auto coalescing_end =
                max_gap_bytes > std::numeric_limits<std::uint64_t>::max() -
                                    plan_end
                    ? std::numeric_limits<std::uint64_t>::max()
                    : plan_end + max_gap_bytes;
            if (request.offset <= coalescing_end) {
                plan.length = std::max(plan_end, request_end) - plan.offset;
                plan.members.push_back({index, request.offset - plan.offset});
                continue;
            }
        }
        CoalescedReadPlan plan;
        plan.path = request.path;
        plan.offset = request.offset;
        plan.length = request.length;
        plan.members.push_back({index, 0U});
        plans.push_back(std::move(plan));
    }
    return plans;
}

// ---------------------------------------------------------------------
// Win32 IOCP backend
// ---------------------------------------------------------------------

#if defined(_WIN32)

namespace {

// OVERLAPPED must be the first member: GetQueuedCompletionStatus hands back
// the LPOVERLAPPED it was given, and the worker loop recovers the owning
// request by reinterpret_cast'ing that pointer back to Win32ReadRequest*
// (the classic "container of first member" trick, well-defined because
// overlapped is the struct's first declared data member).
struct Win32ReadRequest {
    OVERLAPPED overlapped{};
    std::promise<AsyncReadResult> promise;
    std::string buffer;
    AsyncReadCancellationToken* token{nullptr};
};

}  // namespace

class Win32OverlappedFileReader::State final {
public:
    explicit State(const std::size_t queue_depth)
        : in_flight_(std::max<std::size_t>(1U, queue_depth)) {
        port_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 0);
        if (port_ == nullptr) {
            throw std::runtime_error("CreateIoCompletionPort failed");
        }
        // A handful of worker threads is enough to service completions --
        // this pool drains the port, it does not itself perform I/O.
        const auto worker_count =
            std::max<std::size_t>(1U, std::min<std::size_t>(queue_depth, 4U));
        workers_.reserve(worker_count);
        for (std::size_t index = 0U; index < worker_count; ++index) {
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }

    ~State() {
        // Post one null-OVERLAPPED completion per worker as a shutdown
        // sentinel -- no real I/O completion ever carries a null pointer,
        // so worker_loop() treats that as "stop".
        for (std::size_t index = 0U; index < workers_.size(); ++index) {
            PostQueuedCompletionStatus(port_, 0, 0, nullptr);
        }
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
        CloseHandle(port_);
    }

    AsyncReadResult read_range(const std::filesystem::path& path,
                               const std::uint64_t offset, const std::uint64_t length,
                               AsyncReadCancellationToken& token) {
        SemaphorePermit permit(in_flight_, token);
        AsyncReadResult early;
        if (!permit.acquired()) {
            early.cancelled = true;
            return early;
        }
        if (token.is_cancelled()) {
            early.cancelled = true;
            return early;
        }

        HANDLE file = CreateFileW(path.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ,
                                  nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            early.diagnostic = "CreateFileW failed";
            return early;
        }
        if (CreateIoCompletionPort(file, port_, 0, 0) == nullptr) {
            CloseHandle(file);
            early.diagnostic = "CreateIoCompletionPort (file association) failed";
            return early;
        }

        auto request = std::make_unique<Win32ReadRequest>();
        request->token = &token;
        request->buffer.resize(static_cast<std::size_t>(length));
        request->overlapped.Offset = static_cast<DWORD>(offset & 0xFFFFFFFFULL);
        request->overlapped.OffsetHigh = static_cast<DWORD>(offset >> 32U);
        auto future = request->promise.get_future();

        const BOOL immediate = ReadFile(file, request->buffer.empty() ? nullptr
                                                                       : &request->buffer[0],
                                        static_cast<DWORD>(length), nullptr,
                                        &request->overlapped);
        const DWORD read_error = GetLastError();
        if (!immediate && read_error != ERROR_IO_PENDING) {
            CloseHandle(file);
            early.diagnostic = "ReadFile failed";
            return early;
        }
        // Ownership of *request now effectively belongs to the in-flight
        // OVERLAPPED operation; worker_loop() reclaims and deletes it once
        // the completion arrives.
        Win32ReadRequest* raw_request = request.release();
        bool cancellation_requested = false;
        while (future.wait_for(std::chrono::milliseconds(2)) !=
               std::future_status::ready) {
            if (token.is_cancelled() && !cancellation_requested) {
                // Native cancellation wakes the IOCP completion instead of
                // waiting for a slow/network read and discarding it later.
                CancelIoEx(file, &raw_request->overlapped);
                cancellation_requested = true;
            }
        }
        AsyncReadResult result = future.get();
        CloseHandle(file);
        delete raw_request;
        return result;
    }

private:
    void worker_loop() {
        while (true) {
            DWORD bytes_transferred = 0;
            ULONG_PTR completion_key = 0;
            LPOVERLAPPED overlapped_ptr = nullptr;
            const BOOL ok = GetQueuedCompletionStatus(
                port_, &bytes_transferred, &completion_key, &overlapped_ptr, INFINITE);
            if (overlapped_ptr == nullptr) {
                // Shutdown sentinel (see destructor) or the port errored
                // out with no operation attached -- either way, stop.
                return;
            }
            auto* request = reinterpret_cast<Win32ReadRequest*>(overlapped_ptr);
            AsyncReadResult result;
            if (request->token != nullptr && request->token->is_cancelled()) {
                // Cancelled mid-flight: never publish whatever bytes landed.
                result.cancelled = true;
            } else if (!ok) {
                result.diagnostic = "overlapped completion reported failure";
            } else {
                request->buffer.resize(bytes_transferred);
                result.buffer = std::move(request->buffer);
                result.succeeded = true;
            }
            request->promise.set_value(std::move(result));
        }
    }

    HANDLE port_{nullptr};
    std::vector<std::thread> workers_;
    BoundedSemaphore in_flight_;
};

Win32OverlappedFileReader::Win32OverlappedFileReader(const std::size_t queue_depth)
    : state_(std::make_unique<State>(queue_depth)),
      queue_depth_(std::max<std::size_t>(1U, queue_depth)) {}

Win32OverlappedFileReader::~Win32OverlappedFileReader() = default;

AsyncReadResult Win32OverlappedFileReader::read_range(
    const std::filesystem::path& path, const std::uint64_t offset,
    const std::uint64_t length, AsyncReadCancellationToken& token) {
    return state_->read_range(path, offset, length, token);
}

#else  // !_WIN32

// ---------------------------------------------------------------------
// POSIX bounded pread worker-pool backend
// ---------------------------------------------------------------------

namespace {

struct PosixReadTask {
    std::filesystem::path path;
    std::uint64_t offset{0};
    std::uint64_t length{0};
    AsyncReadCancellationToken* token{nullptr};
    std::promise<AsyncReadResult> promise;
};

}  // namespace

class PosixPreadPoolReader::State final {
public:
    explicit State(const std::size_t queue_depth) {
        const auto worker_count = std::max<std::size_t>(1U, queue_depth);
        workers_.reserve(worker_count);
        for (std::size_t index = 0U; index < worker_count; ++index) {
            workers_.emplace_back([this]() { worker_loop(); });
        }
    }

    ~State() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            shutting_down_ = true;
        }
        condition_.notify_all();
        for (auto& worker : workers_) {
            if (worker.joinable()) worker.join();
        }
    }

    AsyncReadResult read_range(const std::filesystem::path& path,
                               const std::uint64_t offset, const std::uint64_t length,
                               AsyncReadCancellationToken& token) {
        if (token.is_cancelled()) {
            AsyncReadResult early;
            early.cancelled = true;
            return early;
        }
        auto task = std::make_shared<PosixReadTask>();
        task->path = path;
        task->offset = offset;
        task->length = length;
        task->token = &token;
        auto future = task->promise.get_future();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(task);
        }
        condition_.notify_one();
        return future.get();
    }

private:
    void worker_loop() {
        while (true) {
            std::shared_ptr<PosixReadTask> task;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                condition_.wait(lock, [this]() { return shutting_down_ || !queue_.empty(); });
                if (queue_.empty()) {
                    if (shutting_down_) return;
                    continue;
                }
                task = queue_.front();
                queue_.pop_front();
            }
            task->promise.set_value(perform_read(*task));
        }
    }

    static AsyncReadResult perform_read(const PosixReadTask& task) {
        AsyncReadResult result;
        if (task.token != nullptr && task.token->is_cancelled()) {
            result.cancelled = true;
            return result;
        }
        const int file_descriptor = ::open(task.path.c_str(), O_RDONLY);
        if (file_descriptor < 0) {
            result.diagnostic = "open failed";
            return result;
        }
        std::string buffer(static_cast<std::size_t>(task.length), '\0');
        std::size_t total_read = 0U;
        while (total_read < buffer.size()) {
            if (task.token != nullptr && task.token->is_cancelled()) {
                ::close(file_descriptor);
                result.cancelled = true;
                return result;
            }
            const auto bytes_read = ::pread(
                file_descriptor, &buffer[total_read], buffer.size() - total_read,
                static_cast<off_t>(task.offset + total_read));
            if (bytes_read < 0) {
                ::close(file_descriptor);
                result.diagnostic = "pread failed";
                return result;
            }
            if (bytes_read == 0) break;  // short read at EOF
            total_read += static_cast<std::size_t>(bytes_read);
        }
        ::close(file_descriptor);
        if (task.token != nullptr && task.token->is_cancelled()) {
            result.cancelled = true;
            return result;
        }
        buffer.resize(total_read);
        result.buffer = std::move(buffer);
        result.succeeded = true;
        return result;
    }

    std::vector<std::thread> workers_;
    std::deque<std::shared_ptr<PosixReadTask>> queue_;
    std::mutex mutex_;
    std::condition_variable condition_;
    bool shutting_down_{false};
};

PosixPreadPoolReader::PosixPreadPoolReader(const std::size_t queue_depth)
    : state_(std::make_unique<State>(queue_depth)),
      queue_depth_(std::max<std::size_t>(1U, queue_depth)) {}

PosixPreadPoolReader::~PosixPreadPoolReader() = default;

AsyncReadResult PosixPreadPoolReader::read_range(
    const std::filesystem::path& path, const std::uint64_t offset,
    const std::uint64_t length, AsyncReadCancellationToken& token) {
    return state_->read_range(path, offset, length, token);
}

#endif  // _WIN32

// ---------------------------------------------------------------------
// Backend construction / process-lifetime reader cache / fallback wrapper
// ---------------------------------------------------------------------

std::unique_ptr<IAsyncFileReader> make_async_file_reader(
    const StorageLatencyProfile& profile) {
    const auto queue_depth = adaptive_queue_depth(profile);
    try {
#if defined(_WIN32)
        return std::make_unique<Win32OverlappedFileReader>(queue_depth);
#else
        return std::make_unique<PosixPreadPoolReader>(queue_depth);
#endif
    } catch (const std::exception&) {
        // Backend construction failed (e.g. IOCP creation error): return
        // nullptr so callers fall back to the blocking path, per the Phase
        // 21 fallback contract.
        return nullptr;
    }
}

IAsyncFileReader* global_async_file_reader(const std::filesystem::path& storage_root) {
    static std::mutex mutex;
    static std::map<std::string, std::unique_ptr<IAsyncFileReader>> readers;

    std::error_code error;
    const auto absolute_root = std::filesystem::absolute(storage_root, error);
    // Cache key is the drive/filesystem root, not the exact directory, so
    // every manifest/segment read under the same volume shares one reader
    // instance instead of re-probing storage latency per call site.
    const std::string key = error ? storage_root.string() : absolute_root.root_path().string();

    std::lock_guard<std::mutex> lock(mutex);
    const auto found = readers.find(key);
    if (found != readers.end()) return found->second.get();

    std::unique_ptr<IAsyncFileReader> reader;
    try {
        const auto storage_class = probe_hardware(storage_root).storage_class;
        const auto profile = probe_storage_latency(storage_root, storage_class);
        reader = make_async_file_reader(profile);
    } catch (const std::exception&) {
        reader.reset();  // cached below as "async unavailable" for this root
    }
    IAsyncFileReader* raw_reader = reader.get();
    readers.emplace(key, std::move(reader));
    return raw_reader;  // may be nullptr; callers must fall back to blocking I/O
}

std::string read_file_bytes(IAsyncFileReader* reader, const std::filesystem::path& path,
                            const std::uint64_t offset, const std::uint64_t length) {
    if (reader != nullptr) {
        AsyncReadCancellationToken token;
        const auto result = reader->read_range(path, offset, length, token);
        if (result.succeeded && !result.cancelled) {
            return result.buffer;
        }
        // Falls through to the direct blocking read below -- this is the
        // "existing blocking path retained as automatic fallback" behavior
        // the Phase 21 wiring requires, whether reader is null (async never
        // initialized) or this specific read failed/was cancelled.
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("file could not be opened: " + path.string());
    }
    input.seekg(static_cast<std::streamoff>(offset));
    std::string buffer(static_cast<std::size_t>(length), '\0');
    if (!buffer.empty()) {
        input.read(&buffer[0], static_cast<std::streamsize>(buffer.size()));
    }
    const auto actually_read = static_cast<std::size_t>(input.gcount());
    buffer.resize(actually_read);
    return buffer;
}

AsyncReadBatchResult read_file_ranges(
    IAsyncFileReader& reader,
    const std::vector<CoalescedReadRequest>& requests,
    AsyncReadCancellationToken& token,
    const std::uint64_t maximum_temporary_bytes,
    const std::uint64_t maximum_coalescing_gap_bytes) {
    AsyncReadBatchResult batch;
    batch.results.resize(requests.size());
    if (requests.empty()) return batch;
    if (maximum_temporary_bytes == 0U) {
        for (auto& result : batch.results) {
            result.diagnostic = "temporary read memory limit is zero";
        }
        return batch;
    }
    const auto plans =
        coalesce_read_requests(requests, maximum_coalescing_gap_bytes);
    std::size_t cursor = 0U;
    while (cursor < plans.size() && !token.is_cancelled()) {
        std::vector<std::size_t> wave;
        std::uint64_t wave_bytes = 0U;
        while (cursor < plans.size() && wave.size() < reader.queue_depth()) {
            const auto& plan = plans[cursor];
            if (plan.length > maximum_temporary_bytes) {
                for (const auto& member : plan.members) {
                    batch.results[member.request_index].diagnostic =
                        "coalesced read exceeds temporary memory limit";
                }
                ++cursor;
                continue;
            }
            if (!wave.empty() && wave_bytes + plan.length >
                                     maximum_temporary_bytes) {
                break;
            }
            wave.push_back(cursor++);
            wave_bytes += plan.length;
        }
        if (wave.empty()) continue;
        batch.physical_reads += wave.size();
        batch.peak_temporary_bytes =
            std::max(batch.peak_temporary_bytes, wave_bytes);
        std::vector<std::future<AsyncReadResult>> futures;
        futures.reserve(wave.size());
        for (const auto plan_index : wave) {
            const auto& plan = plans[plan_index];
            const auto path = plan.path;
            const auto offset = plan.offset;
            const auto length = plan.length;
            futures.push_back(std::async(
                std::launch::async, [&reader, &token, path, offset, length]() {
                    return reader.read_range(path, offset, length, token);
                }));
        }
        for (std::size_t index = 0U; index < wave.size(); ++index) {
            const auto& plan = plans[wave[index]];
            auto physical = futures[index].get();
            for (const auto& member : plan.members) {
                auto& result = batch.results[member.request_index];
                if (!physical.succeeded || physical.cancelled) {
                    result.cancelled = physical.cancelled;
                    result.diagnostic = physical.diagnostic;
                    continue;
                }
                const auto requested_length =
                    requests[member.request_index].length;
                if (member.buffer_offset > physical.buffer.size() ||
                    requested_length > physical.buffer.size() -
                                           member.buffer_offset) {
                    result.diagnostic = "coalesced read returned a short range";
                    continue;
                }
                result.buffer = physical.buffer.substr(
                    static_cast<std::size_t>(member.buffer_offset),
                    static_cast<std::size_t>(requested_length));
                result.succeeded = true;
            }
        }
    }
    batch.cancelled = token.is_cancelled();
    if (batch.cancelled) {
        for (auto& result : batch.results) {
            if (!result.succeeded && result.diagnostic.empty()) {
                result.cancelled = true;
            }
        }
    }
    return batch;
}

}  // namespace masterai
