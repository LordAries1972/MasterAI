#include "masterai.hpp"
#include "curl_process.hpp"

#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace masterai {
namespace {

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
            throw std::runtime_error("persisted download record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted download record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string state_name(const DownloadState state) {
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
    throw std::runtime_error("download state is invalid");
}

DownloadState parse_state(const std::string& value) {
    if (value == "queued") return DownloadState::queued;
    if (value == "transferring" || value == "paused") return DownloadState::paused;
    if (value == "verifying") return DownloadState::paused;
    if (value == "complete") return DownloadState::complete;
    if (value == "failed") return DownloadState::failed;
    if (value == "quarantined") return DownloadState::quarantined;
    if (value == "cancelled") return DownloadState::cancelled;
    throw std::runtime_error("persisted download state is invalid");
}

}  // namespace

DownloadManager::DownloadManager(std::filesystem::path approved_curl,
                                 std::filesystem::path download_root,
                                 RecordStore& records)
    : approved_curl_(std::move(approved_curl)),
      download_root_(std::move(download_root)), records_(records) {
    if (!std::filesystem::is_regular_file(approved_curl_) ||
        std::filesystem::is_symlink(approved_curl_) || download_root_.empty()) {
        throw std::invalid_argument(
            "download transport must be an approved regular file");
    }
    std::filesystem::create_directories(download_root_);
    restore();
}

DownloadJob DownloadManager::create(const DownloadRequest& request) {
    std::lock_guard<std::mutex> lock(mutex_);
    DownloadJob job(request);
    if (!is_path_within(download_root_, request.destination) ||
        request.source_url.find(request.immutable_revision) == std::string::npos) {
        throw std::invalid_argument(
            "download destination or immutable source revision violates policy");
    }
    if (jobs_.size() >= 1000U) {
        throw std::runtime_error("download job capacity reached");
    }
    persist(job);
    jobs_.emplace(job.id(), job);
    return job;
}

// Runs one transfer synchronously on the calling (request) thread, but never
// holds mutex_ across the transfer itself: only the brief admission check,
// each ~100ms progress tick, and the final state transition take the lock.
// This is what lets other request threads keep calling find()/list()/cancel()
// -- i.e. the frontend's own progress poll -- while a download is in flight,
// instead of the whole server stalling for the download's full duration.
DownloadJob DownloadManager::run(const std::string& id,
                                 const std::atomic_bool& cancellation) {
    std::unique_lock<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) throw std::invalid_argument("download job not found");
    if (in_flight_.find(id) != in_flight_.end()) {
        // A second run() for the same id would race this one over the same
        // .part file; reject it instead so the caller (the HTTP layer) can
        // report 409 Conflict rather than corrupting the transfer.
        throw DownloadAlreadyRunning(id);
    }
    in_flight_.insert(id);
    // Owns the cross-request pause/cancel signal for this run: a separate
    // HTTP request (pause_download/cancel_download, or shutdown's
    // cancel_all()) locates this same shared_ptr by id and flips its flags,
    // which is the only way to reach a transfer that is blocked inside this
    // function on another thread for the whole download.
    const auto signal = std::make_shared<RunSignal>();
    signals_[id] = signal;
    // Safe to hold a reference into jobs_ across the unlocked phases below:
    // std::map insertion (the only other mutation create() performs) never
    // invalidates references to existing elements, and in_flight_ rules out
    // a second run() mutating this same job concurrently. cancel() still can
    // run concurrently, but it takes mutex_ too, so job's own mutations are
    // always serialized with it.
    auto& job = found->second;
    const auto destination = job.request().destination;
    const auto partial = std::filesystem::path(destination.string() + ".part");
    lock.unlock();

    // Guarantees in_flight_ and signals_ are cleared on every exit path --
    // success, failure, exception, or cancellation -- so a retry is never
    // blocked by a stale entry and pause()/cancel() never signal a shared_ptr
    // nobody is listening to anymore.
    struct InFlightGuard {
        std::mutex& guarded_mutex;
        std::set<std::string>& in_flight;
        std::map<std::string, std::shared_ptr<RunSignal>>& signals;
        std::string job_id;
        ~InFlightGuard() {
            std::lock_guard<std::mutex> guard(guarded_mutex);
            in_flight.erase(job_id);
            signals.erase(job_id);
        }
    } release{mutex_, in_flight_, signals_, id};

    std::filesystem::create_directories(destination.parent_path());
    const std::uint64_t existing =
        std::filesystem::is_regular_file(partial)
            ? static_cast<std::uint64_t>(std::filesystem::file_size(partial))
            : 0U;
    {
        std::lock_guard<std::mutex> tick(mutex_);
        job.begin(existing);
        persist(job);
    }
    try {
        const std::vector<std::string> arguments{
            "--proto", "=https", "--tlsv1.2", "--fail", "--location",
            "--silent", "--show-error", "--continue-at", "-", "--output",
            partial.string(), job.request().source_url};
        // run_curl_process's own wait loop only ever consults this one flag, but a
        // pause/cancel request arrives on a different thread as a mutation of
        // signal->cancellation (the caller-supplied `cancellation` reference
        // is otherwise the only thing that thread could observe). The
        // progress tick below -- which already runs once per ~100ms wait
        // iteration -- is what folds both sources in before each check.
        std::atomic_bool combined_cancellation{false};
        const int exit_code = run_curl_process(
            approved_curl_, arguments,
            download_root_ / "logs" / ("download-" + id + ".log"),
            combined_cancellation, [&]() {
                if (cancellation.load() || signal->cancellation.load()) {
                    combined_cancellation.store(true);
                }
                std::error_code error;
                const auto size = std::filesystem::file_size(partial, error);
                if (error) return;
                // Locked only for this one progress-tick mutation, not for
                // the wait between ticks, so the transfer's ~100ms polling
                // loop never blocks other DownloadManager callers.
                std::lock_guard<std::mutex> tick(mutex_);
                if (size > job.completed_bytes()) {
                    job.record_progress(static_cast<std::uint64_t>(size));
                    persist(job);
                }
            });
        if (combined_cancellation.load()) {
            std::lock_guard<std::mutex> tick(mutex_);
            // A pause request lands the job back in the resumable 'paused'
            // state; a stop request (or the caller's own cancellation flag,
            // e.g. process shutdown) lands it in the terminal 'cancelled'
            // state instead.
            if (signal->pause_requested.load()) {
                job.pause();
            } else {
                job.cancel();
            }
            persist(job);
            return job;
        }
        if (exit_code != 0 || !std::filesystem::is_regular_file(partial)) {
            std::lock_guard<std::mutex> tick(mutex_);
            job.fail("download transport failed with exit code " +
                     std::to_string(exit_code));
            persist(job);
            return job;
        }
        {
            std::lock_guard<std::mutex> tick(mutex_);
            job.record_progress(
                static_cast<std::uint64_t>(std::filesystem::file_size(partial)));
            job.begin_verification();
            persist(job);
        }
        // Checked before complete() (not after): a prior job may already
        // have populated this destination (e.g. the same model queued
        // twice). Removing the now-redundant partial and failing cleanly
        // here avoids both an orphaned .part file and a job that reports
        // complete without ever having moved its own download into place.
        if (std::filesystem::exists(destination)) {
            std::error_code ignored;
            std::filesystem::remove(partial, ignored);
            std::lock_guard<std::mutex> tick(mutex_);
            job.fail("verified destination already exists from a prior download");
            persist(job);
            return job;
        }
        // Unlocked: hashing a potentially multi-gigabyte file is CPU-bound
        // work that would otherwise block every other DownloadManager caller
        // for its whole duration. No other thread touches this id's .part
        // file while in_flight_ holds it.
        const auto actual = sha256_file_hex(partial);
        DownloadState final_state;
        {
            std::lock_guard<std::mutex> tick(mutex_);
            job.complete(actual);
            persist(job);
            final_state = job.state();
        }
        if (final_state == DownloadState::complete) {
            std::filesystem::rename(partial, destination);
            // The rename above lands the file at
            // models_root/category/model_id/filename (see create_download,
            // workload_http.cpp, for the one place that layout is defined);
            // walking up from it here avoids DownloadManager needing to
            // know models_root as a separate constructor argument. Recorded
            // with the hash `actual` already computed just above -- this is
            // the same file, so re-hashing it again via a later
            // `masterai verify-models` run would just reconfirm what
            // integrity verification already just proved. Best-effort: a
            // failure here (e.g. the models root layout not matching, an
            // unwritable cache file) only means this model shows as
            // unverified until the next explicit verify run, never fails
            // the download that already succeeded.
            try {
                record_verified_model(
                    destination.parent_path().parent_path().parent_path(),
                    destination.parent_path().filename().string(), actual,
                    std::filesystem::file_size(destination));
            } catch (const std::exception& record_exception) {
                std::cerr << "download: failed to record verified model '"
                          << destination.string()
                          << "' (model still downloaded successfully; will "
                             "show unverified until next verify run): "
                          << record_exception.what() << std::endl;
            }
        } else {
            const auto quarantine =
                std::filesystem::path(partial.string() + ".quarantine." + id);
            std::filesystem::rename(partial, quarantine);
        }
    } catch (const std::exception& exception) {
        std::lock_guard<std::mutex> tick(mutex_);
        if (job.state() != DownloadState::complete &&
            job.state() != DownloadState::cancelled &&
            job.state() != DownloadState::quarantined) {
            job.fail(exception.what());
        }
        persist(job);
        return job;
    }
    std::lock_guard<std::mutex> tick(mutex_);
    persist(job);
    return job;
}

std::optional<DownloadJob> DownloadManager::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    return found == jobs_.end() ? std::nullopt
                                : std::optional<DownloadJob>(found->second);
}

std::vector<DownloadJob> DownloadManager::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<DownloadJob> result;
    result.reserve(jobs_.size());
    for (const auto& item : jobs_) result.push_back(item.second);
    return result;
}

void DownloadManager::pause(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (jobs_.find(id) == jobs_.end()) {
        throw std::invalid_argument("download job not found");
    }
    const auto in_flight = in_flight_.find(id);
    if (in_flight == in_flight_.end()) {
        throw std::invalid_argument("download is not currently transferring");
    }
    // The transfer's own run() call (on another thread) observes this at its
    // next ~100ms progress tick and lands the job in 'paused' itself; pause()
    // never mutates job state directly, since only the thread inside run()
    // knows the transfer has actually stopped.
    const auto signal = signals_.find(id);
    if (signal != signals_.end()) {
        signal->second->pause_requested.store(true);
        signal->second->cancellation.store(true);
    }
}

void DownloadManager::cancel(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) throw std::invalid_argument("download job not found");
    const auto in_flight = in_flight_.find(id);
    if (in_flight != in_flight_.end()) {
        // Signal the running transfer to stop rather than mutating state
        // here directly -- run() itself transitions to 'cancelled' once it
        // observes this and unwinds, avoiding a state race with its own
        // in-progress persist() calls.
        const auto signal = signals_.find(id);
        if (signal != signals_.end()) {
            signal->second->pause_requested.store(false);
            signal->second->cancellation.store(true);
        }
        return;
    }
    found->second.cancel();
    persist(found->second);
}

void DownloadManager::remove(const std::string& id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = jobs_.find(id);
    if (found == jobs_.end()) throw std::invalid_argument("download job not found");
    if (in_flight_.find(id) != in_flight_.end()) {
        throw std::invalid_argument("an in-flight download cannot be removed");
    }
    const auto& destination = found->second.request().destination;
    const auto partial = std::filesystem::path(destination.string() + ".part");
    std::error_code ignored;
    std::filesystem::remove(partial, ignored);
    std::filesystem::remove(
        std::filesystem::path(partial.string() + ".quarantine." + id), ignored);
    records_.erase("downloads", id);
    jobs_.erase(found);
}

void DownloadManager::cancel_all() noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& id : in_flight_) {
            const auto signal = signals_.find(id);
            if (signal != signals_.end()) {
                signal->second->pause_requested.store(false);
                signal->second->cancellation.store(true);
            }
        }
    } catch (const std::exception&) {
        // Best-effort: shutdown must never throw out of this call.
    }
}

void DownloadManager::persist(const DownloadJob& job) {
    const auto& request = job.request();
    records_.put(
        "downloads", job.id(),
        pack({request.source_url, request.immutable_revision,
              request.expected_sha256, request.destination.string(),
              request.license_accepted ? "1" : "0", state_name(job.state()),
              std::to_string(job.completed_bytes()), job.diagnostic(),
              std::to_string(request.minimum_ram_mib),
              std::to_string(request.recommended_ram_mib)}));
}

void DownloadManager::restore() {
    for (const auto& item : records_.list("downloads")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 10U) {
            throw std::runtime_error("persisted download job is invalid");
        }
        DownloadRequest request{
            fields[0], fields[1], fields[2], fields[3], fields[4] == "1",
            std::stoull(fields[8]), std::stoull(fields[9])};
        if (!is_path_within(download_root_, request.destination)) {
            throw std::runtime_error(
                "persisted download destination violates path policy");
        }
        jobs_.emplace(
            item.first,
            DownloadJob(item.first, request, parse_state(fields[5]),
                        std::stoull(fields[6]), fields[7]));
    }
}

}  // namespace masterai
