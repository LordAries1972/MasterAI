// MasterAI native Phase 15 live file-watcher/branch-switch adapter.
//
// This unit is the one process that turns real, unattended file saves and
// Git branch switches into ProjectIndexService::request_update calls. It
// intentionally never reads or parses file contents itself -- that remains
// ProjectIndexer's job -- and never talks to the network; it only observes
// filesystem change notifications for each catalog project's root and
// forwards affected paths (or a branch-switch signal) into the existing,
// already-bounded indexing service.
#include "masterai.hpp"

#include <chrono>
#include <map>
#include <mutex>
#include <set>
#include <thread>
#include <vector>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <poll.h>
#include <sys/inotify.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

// Debounce window: real editors and compilers emit bursts of change events
// for one logical save (temp-file write, rename, metadata touch). Flushing
// only after this much quiet time coalesces a burst into one request_update
// call instead of flooding the bounded index queue with per-event calls.
constexpr std::chrono::milliseconds kDebounceWindow{300};
// Safety-net full rescan cadence per project so a notification silently
// dropped by the OS (queue overflow, watch removed under load) cannot starve
// a project's index forever; this is the same IndexTrigger::periodic path
// the authenticated index/notify route already exposes to external callers.
constexpr std::chrono::minutes kPeriodicRescanInterval{30};
// How often the watcher re-lists ProjectCatalog to notice newly added
// projects and start watching them; also the polling granularity used by
// both platform backends.
constexpr std::chrono::milliseconds kCatalogSyncInterval{2000};
// Recursive directory-watch cap on Linux, where inotify has no native
// recursive mode and each watched directory costs one kernel watch
// descriptor; bounded so a pathological project tree cannot exhaust a
// process's inotify instance. Windows ReadDirectoryChangesW watches a whole
// subtree with one handle and is not subject to this cap.
constexpr std::size_t kMaximumWatchedDirectories{16384U};

bool is_branch_switch_path(const std::filesystem::path& relative) {
    const auto text = relative.generic_string();
    return text == ".git/HEAD";
}

}  // namespace

class ProjectWatcher::State final {
public:
    State(ProjectCatalog& projects, ProjectIndexService& indexes)
        : projects_(&projects), indexes_(&indexes),
          worker_([this]() { run(); }) {}

    ~State() {
        stopping_.store(true);
        if (worker_.joinable()) worker_.join();
    }

    State(const State&) = delete;
    State& operator=(const State&) = delete;

private:
    struct Pending {
        std::set<std::filesystem::path> paths;
        std::chrono::steady_clock::time_point last_event;
        bool branch_switch{false};
    };

    // Records one observed change for later debounced flushing. Called from
    // the platform-specific event loop below with an absolute path.
    void record_change(const std::string& project_id,
                       const std::filesystem::path& project_root,
                       const std::filesystem::path& absolute_path) {
        if (!is_path_within(project_root, absolute_path)) return;
        const auto relative =
            std::filesystem::relative(absolute_path, project_root);
        std::lock_guard<std::mutex> lock(mutex_);
        auto& pending = pending_[project_id];
        pending.last_event = std::chrono::steady_clock::now();
        if (is_branch_switch_path(relative)) {
            pending.branch_switch = true;
        } else {
            pending.paths.insert(absolute_path);
        }
    }

    // Moves every project whose debounce window has elapsed out of
    // `pending_` and into the returned map, under the lock.
    std::map<std::string, Pending> drain_debounced(
        const std::chrono::steady_clock::time_point now) {
        std::map<std::string, Pending> ready;
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto it = pending_.begin(); it != pending_.end();) {
            if (now - it->second.last_event >= kDebounceWindow) {
                ready.emplace(it->first, std::move(it->second));
                it = pending_.erase(it);
            } else {
                ++it;
            }
        }
        return ready;
    }

    // Forwards one project's debounced change into request_update: a
    // branch-switch always wins over accumulated paths since it already
    // forces a full rescan.
    void forward_pending(const ProjectRecord& project, const Pending& pending) {
        if (pending.branch_switch) {
            indexes_->request_update(project, {}, IndexTrigger::branch_switch);
        } else if (!pending.paths.empty()) {
            indexes_->request_update(
                project,
                std::vector<std::filesystem::path>(pending.paths.begin(),
                                                    pending.paths.end()),
                IndexTrigger::watcher);
        }
    }

    // Flushes any project whose debounce window has elapsed, and forces a
    // periodic safety-net rescan for projects that have gone quiet for too
    // long without one. Returns nothing; failures to admit (queue full,
    // rebuild already active) are silently retried on the next event or
    // sync tick rather than treated as fatal -- request_update itself is
    // this project's own retry-safe boundary.
    void flush_ready(const std::map<std::string, ProjectRecord>& catalog) {
        const auto now = std::chrono::steady_clock::now();
        for (const auto& [project_id, pending] : drain_debounced(now)) {
            const auto found = catalog.find(project_id);
            if (found != catalog.end()) forward_pending(found->second, pending);
        }
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& [project_id, project] : catalog) {
            auto& last = last_periodic_[project_id];
            if (last.time_since_epoch().count() == 0) {
                last = now;
                continue;
            }
            if (now - last >= kPeriodicRescanInterval) {
                last = now;
                indexes_->request_update(project, {}, IndexTrigger::periodic);
            }
        }
    }

#if defined(_WIN32)
    struct Watch {
        ProjectRecord project;
        HANDLE directory{INVALID_HANDLE_VALUE};
        HANDLE completion_port{nullptr};
        OVERLAPPED overlapped{};
        std::vector<BYTE> buffer;
        bool pending_read{false};
    };

    static constexpr DWORD kNotifyFilter =
        FILE_NOTIFY_CHANGE_FILE_NAME | FILE_NOTIFY_CHANGE_DIR_NAME |
        FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE;

    // Walks the FILE_NOTIFY_INFORMATION chain filled in by one completed
    // ReadDirectoryChangesW call and records each changed path.
    void handle_windows_notifications(Watch& watch) {
        std::size_t offset = 0U;
        for (;;) {
            const auto* entry =
                reinterpret_cast<const FILE_NOTIFY_INFORMATION*>(
                    watch.buffer.data() + offset);
            const std::wstring name(entry->FileName,
                                    entry->FileNameLength / sizeof(WCHAR));
            record_change(watch.project.id, watch.project.root,
                         watch.project.root / std::filesystem::path(name));
            if (entry->NextEntryOffset == 0U) break;
            offset += entry->NextEntryOffset;
        }
    }

    bool issue_read(Watch& watch) {
        DWORD unused = 0U;
        watch.pending_read = ReadDirectoryChangesW(
                                 watch.directory, watch.buffer.data(),
                                 static_cast<DWORD>(watch.buffer.size()), TRUE,
                                 kNotifyFilter, &unused, &watch.overlapped,
                                 nullptr) != 0;
        return watch.pending_read;
    }

    void run() {
        HANDLE port =
            CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
        std::map<std::string, std::unique_ptr<Watch>> watches;
        while (!stopping_.load()) {
            const auto catalog_list = projects_->list();
            std::map<std::string, ProjectRecord> catalog;
            for (const auto& project : catalog_list) catalog[project.id] = project;
            for (const auto& project : catalog_list) {
                if (watches.find(project.id) != watches.end()) continue;
                const HANDLE directory = CreateFileW(
                    project.root.c_str(), FILE_LIST_DIRECTORY,
                    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                    nullptr, OPEN_EXISTING,
                    FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
                if (directory == INVALID_HANDLE_VALUE) continue;
                auto watch = std::make_unique<Watch>();
                watch->project = project;
                watch->directory = directory;
                watch->completion_port = port;
                watch->buffer.resize(64U * 1024U);
                CreateIoCompletionPort(directory, port,
                                       reinterpret_cast<ULONG_PTR>(watch.get()),
                                       0);
                issue_read(*watch);
                watches.emplace(project.id, std::move(watch));
                // A project just noticed for the first time has no
                // baseline generation yet; request one so it becomes
                // searchable even before its next real file change.
                indexes_->request_rebuild(project);
            }
            for (auto it = watches.begin(); it != watches.end();) {
                if (catalog.find(it->first) == catalog.end()) {
                    CloseHandle(it->second->directory);
                    it = watches.erase(it);
                } else {
                    ++it;
                }
            }

            const auto deadline =
                std::chrono::steady_clock::now() + kCatalogSyncInterval;
            while (std::chrono::steady_clock::now() < deadline &&
                   !stopping_.load()) {
                DWORD bytes = 0U;
                ULONG_PTR key = 0U;
                LPOVERLAPPED overlapped = nullptr;
                const auto remaining =
                    std::chrono::duration_cast<std::chrono::milliseconds>(
                        deadline - std::chrono::steady_clock::now())
                        .count();
                const auto wait_ms =
                    std::min<std::int64_t>(remaining, 200);
                const BOOL ok = GetQueuedCompletionStatus(
                    port, &bytes, &key, &overlapped,
                    wait_ms > 0 ? static_cast<DWORD>(wait_ms) : 0U);
                auto* watch = reinterpret_cast<Watch*>(key);
                if (watch == nullptr) {
                    flush_ready(catalog);  // timed out or spurious wakeup
                    continue;
                }
                if (ok && bytes > 0U) handle_windows_notifications(*watch);
                issue_read(*watch);
                flush_ready(catalog);
            }
            flush_ready(catalog);
        }
        for (auto& [id, watch] : watches) {
            CancelIoEx(watch->directory, &watch->overlapped);
            CloseHandle(watch->directory);
        }
        if (port != nullptr) CloseHandle(port);
    }
#elif defined(__linux__)
    struct DirectoryWatch {
        std::string project_id;
        std::filesystem::path project_root;
        std::filesystem::path directory;
    };

    // Recursively adds inotify watches under `directory` (bounded by
    // kMaximumWatchedDirectories total across every project), recording each
    // watch descriptor's owning project and directory so events can be
    // resolved back to an absolute path and a project.
    void add_watches_recursive(const std::string& project_id,
                               const std::filesystem::path& project_root,
                               const std::filesystem::path& directory,
                               std::size_t& watched_count) {
        if (watched_count >= kMaximumWatchedDirectories) return;
        constexpr std::uint32_t mask =
            IN_CREATE | IN_DELETE | IN_MODIFY | IN_MOVED_FROM | IN_MOVED_TO |
            IN_CLOSE_WRITE | IN_DELETE_SELF;
        const int wd = inotify_add_watch(inotify_fd_, directory.c_str(), mask);
        if (wd < 0) return;
        watch_directories_[wd] = {project_id, project_root, directory};
        ++watched_count;
        std::error_code error;
        for (const auto& entry :
             std::filesystem::directory_iterator(directory, error)) {
            if (error) break;
            std::error_code type_error;
            if (entry.is_directory(type_error) && !type_error &&
                !entry.path().filename().string().empty() &&
                entry.path().filename() != ".git") {
                add_watches_recursive(project_id, project_root, entry.path(),
                                     watched_count);
            }
        }
    }

    // Removes every inotify watch belonging to `project_id`, invoked when a
    // project disappears from the catalog.
    void remove_watches_for_project(const std::string& project_id) {
        for (auto it = watch_directories_.begin();
             it != watch_directories_.end();) {
            if (it->second.project_id == project_id) {
                inotify_rm_watch(inotify_fd_, it->first);
                it = watch_directories_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // Adds/removes inotify watch trees so `watched_projects` matches the
    // live ProjectCatalog listing; returns the current catalog as a map.
    std::map<std::string, ProjectRecord> sync_projects_with_catalog(
        std::map<std::string, ProjectRecord>& watched_projects) {
        std::map<std::string, ProjectRecord> catalog;
        for (const auto& project : projects_->list()) catalog[project.id] = project;
        for (const auto& [id, project] : catalog) {
            if (watched_projects.find(id) != watched_projects.end()) continue;
            std::size_t watched_count = watch_directories_.size();
            add_watches_recursive(id, project.root, project.root, watched_count);
            watched_projects[id] = project;
            // A project just noticed for the first time has no baseline
            // generation yet; request one so it becomes searchable even
            // before its next real file change.
            indexes_->request_rebuild(project);
        }
        for (auto it = watched_projects.begin(); it != watched_projects.end();) {
            if (catalog.find(it->first) == catalog.end()) {
                remove_watches_for_project(it->first);
                it = watched_projects.erase(it);
            } else {
                ++it;
            }
        }
        return catalog;
    }

    // Handles one already-read inotify_event: records the change and, for a
    // newly created subdirectory, extends the watch tree onto it.
    void handle_inotify_event(const struct inotify_event& event) {
        const auto watch_found = watch_directories_.find(event.wd);
        if (watch_found == watch_directories_.end() || event.len == 0U) return;
        const std::filesystem::path name(event.name);
        const auto absolute = watch_found->second.directory / name;
        record_change(watch_found->second.project_id,
                     watch_found->second.project_root, absolute);
        if ((event.mask & IN_ISDIR) != 0U && (event.mask & IN_CREATE) != 0U) {
            std::size_t watched_count = watch_directories_.size();
            add_watches_recursive(watch_found->second.project_id,
                                 watch_found->second.project_root, absolute,
                                 watched_count);
        }
    }

    void run() {
        inotify_fd_ = inotify_init1(IN_NONBLOCK);
        std::map<std::string, ProjectRecord> watched_projects;
        std::vector<char> buffer(64U * 1024U);
        auto last_sync = std::chrono::steady_clock::time_point{};
        while (!stopping_.load()) {
            const auto now = std::chrono::steady_clock::now();
            const bool due = last_sync.time_since_epoch().count() == 0 ||
                            now - last_sync >= kCatalogSyncInterval;
            std::map<std::string, ProjectRecord> catalog =
                due ? sync_projects_with_catalog(watched_projects)
                    : watched_projects;
            if (due) last_sync = now;

            struct pollfd poll_descriptor {};
            poll_descriptor.fd = inotify_fd_;
            poll_descriptor.events = POLLIN;
            const int ready = poll(&poll_descriptor, 1, 250);
            if (ready > 0 && (poll_descriptor.revents & POLLIN) != 0) {
                const auto bytes_read =
                    read(inotify_fd_, buffer.data(), buffer.size());
                std::size_t offset = 0U;
                while (bytes_read > 0 &&
                      offset + sizeof(struct inotify_event) <=
                          static_cast<std::size_t>(bytes_read)) {
                    const auto* event = reinterpret_cast<const struct inotify_event*>(
                        buffer.data() + offset);
                    handle_inotify_event(*event);
                    offset += sizeof(struct inotify_event) + event->len;
                }
            }
            flush_ready(catalog);
        }
        for (const auto& [wd, info] : watch_directories_) {
            inotify_rm_watch(inotify_fd_, wd);
        }
        if (inotify_fd_ >= 0) close(inotify_fd_);
    }

    int inotify_fd_{-1};
    std::map<int, DirectoryWatch> watch_directories_;
#endif

    ProjectCatalog* projects_;
    ProjectIndexService* indexes_;
    std::mutex mutex_;
    std::map<std::string, Pending> pending_;
    std::map<std::string, std::chrono::steady_clock::time_point> last_periodic_;
    std::atomic_bool stopping_{false};
    std::thread worker_;
};

ProjectWatcher::ProjectWatcher(ProjectCatalog& projects,
                               ProjectIndexService& indexes)
    : state_(std::make_unique<State>(projects, indexes)) {}

ProjectWatcher::~ProjectWatcher() = default;

}  // namespace masterai
