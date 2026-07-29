// MasterAI cancellable incremental disk-backed project indexing.
//
// This unit discovers only canonical regular project files, reads them through
// explicit byte bounds, publishes immutable checksummed segment generations,
// and atomically switches a manifest only after verification. It does not own
// retrieval ranking or authorization decisions.
#include "masterai.hpp"

#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <deque>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace masterai {
namespace {

std::string language_for(const std::filesystem::path& path) {
    const auto extension = path.extension().string();
    if (extension == ".cpp" || extension == ".cc" || extension == ".cxx")
        return "cpp";
    if (extension == ".hpp" || extension == ".h") return "cpp-header";
    if (extension == ".md") return "markdown";
    if (extension == ".json") return "json";
    if (extension == ".cmake" || path.filename() == "CMakeLists.txt")
        return "cmake";
    return "text";
}

bool indexable(const std::filesystem::path& path) {
    static const std::set<std::string> allowed{
        ".cpp", ".cc", ".cxx", ".c", ".hpp", ".h", ".md",
        ".txt", ".json", ".cmake", ".ps1", ".sh"};
    return path.filename() == "CMakeLists.txt" ||
           allowed.find(path.extension().string()) != allowed.end();
}

std::string read_bounded(const std::filesystem::path& path,
                         const std::uint64_t maximum) {
    const auto size = std::filesystem::file_size(path);
    if (size == 0U || size > maximum) return {};
    std::string text(static_cast<std::size_t>(size), '\0');
    std::ifstream input(path, std::ios::binary);
    input.read(&text[0], static_cast<std::streamsize>(text.size()));
    if (!input || !valid_utf8_text(text)) return {};
    return text;
}

std::vector<IndexChunk> make_chunks(const std::string& relative,
                                    const std::string& language,
                                    const std::string& text) {
    std::vector<IndexChunk> chunks;
    constexpr std::size_t chunk_bytes = 4096U;
    for (std::size_t offset = 0U; offset < text.size();
         offset += chunk_bytes) {
        IndexChunk chunk;
        chunk.relative_path = relative;
        chunk.language = language;
        chunk.offset = offset;
        chunk.text = text.substr(offset, chunk_bytes);
        chunk.digest = sha256_hex(chunk.text);
        chunk.id = sha256_hex(relative + ":" + std::to_string(offset) + ":" +
                              chunk.digest);
        chunks.push_back(std::move(chunk));
    }
    return chunks;
}

bool identifier_character(const char value) {
    const auto byte = static_cast<unsigned char>(value);
    return std::isalnum(byte) != 0 || value == '_';
}

bool contains_symbol(const std::string& text, const std::string& symbol) {
    std::size_t position = text.find(symbol);
    while (position != std::string::npos) {
        const bool left_boundary =
            position == 0U || !identifier_character(text[position - 1U]);
        const auto end = position + symbol.size();
        const bool right_boundary =
            end == text.size() || !identifier_character(text[end]);
        if (left_boundary && right_boundary) return true;
        position = text.find(symbol, position + 1U);
    }
    return false;
}

}  // namespace

class ProjectIndexer::State final {
public:
    State(ProjectRecord value, std::filesystem::path root,
          MemoryBudgetManager& manager, const std::uint64_t file_limit,
          const std::uint64_t index_limit)
        : project(std::move(value)), index_root(std::move(root)),
          memory(manager), maximum_file_bytes(file_limit),
          maximum_index_bytes(index_limit) {}

    void load_generation() {
        chunks.clear();
        const auto manifest = index_root / project.id / "manifest";
        for (const auto& candidate :
             {manifest, std::filesystem::path(manifest.string() + ".prev")}) {
            if (!std::filesystem::is_regular_file(candidate)) continue;
            std::ifstream input(candidate);
            std::string segment_name;
            std::string expected;
            std::uint64_t generation = 0U;
            input >> generation >> segment_name >> expected;
            const auto segment = manifest.parent_path() / segment_name;
            if (!input || !std::filesystem::is_regular_file(segment) ||
                sha256_file_hex(segment) != expected) {
                current.diagnostic = "index segment quarantined after checksum failure";
                continue;
            }
            current.generation = generation;
            active_segment = segment;
            break;
        }
        if (active_segment.empty()) return;
        std::ifstream data(active_segment, std::ios::binary);
        std::string line;
        std::getline(data, line);
        if (line != "MASTERAI-INDEX-1") {
            current.diagnostic = "index header is invalid";
            return;
        }
        while (std::getline(data, line)) {
            std::istringstream header(line);
            IndexChunk chunk;
            std::size_t path_size = 0U;
            std::size_t language_size = 0U;
            std::size_t text_size = 0U;
            header >> chunk.offset >> path_size >> language_size >> text_size >>
                chunk.digest >> chunk.id;
            if (!header || path_size > 32768U || text_size > 65536U) break;
            chunk.relative_path.resize(path_size);
            chunk.language.resize(language_size);
            chunk.text.resize(text_size);
            data.read(&chunk.relative_path[0],
                      static_cast<std::streamsize>(path_size));
            data.read(&chunk.language[0],
                      static_cast<std::streamsize>(language_size));
            data.read(&chunk.text[0],
                      static_cast<std::streamsize>(text_size));
            data.get();
            if (!data || sha256_hex(chunk.text) != chunk.digest) break;
            chunks.push_back(std::move(chunk));
        }
        current.chunks = chunks.size();
    }

    // Publishes one immutable segment, verifies its digest, then atomically
    // advances the manifest while retaining the prior recoverable generation.
    IndexStatus publish(std::vector<IndexChunk> next, IndexStatus result) {
        if (result.cancelled && next.empty()) return result;
        std::sort(next.begin(), next.end(),
                  [](const IndexChunk& left, const IndexChunk& right) {
                      if (left.relative_path != right.relative_path) {
                          return left.relative_path < right.relative_path;
                      }
                      return left.offset < right.offset;
                  });
        const auto directory = index_root / project.id;
        const auto segment_name =
            "segment-" + std::to_string(result.generation) + ".idx";
        const auto temporary = directory / (segment_name + ".tmp");
        {
            std::ofstream output(temporary,
                                 std::ios::binary | std::ios::trunc);
            output << "MASTERAI-INDEX-1\n";
            for (const auto& chunk : next) {
                output << chunk.offset << ' ' << chunk.relative_path.size()
                       << ' ' << chunk.language.size() << ' '
                       << chunk.text.size() << ' ' << chunk.digest << ' '
                       << chunk.id << '\n'
                       << chunk.relative_path << chunk.language << chunk.text
                       << '\n';
                if (static_cast<std::uint64_t>(output.tellp()) >
                    maximum_index_bytes) {
                    output.close();
                    std::error_code ignored;
                    std::filesystem::remove(temporary, ignored);
                    throw std::runtime_error("index disk ceiling exceeded");
                }
            }
        }
        const auto segment = directory / segment_name;
        std::error_code error;
        std::filesystem::remove(segment, error);
        std::filesystem::rename(temporary, segment);
        const auto digest = sha256_file_hex(segment);
        const auto manifest_temporary = directory / "manifest.tmp";
        {
            std::ofstream manifest(manifest_temporary,
                                   std::ios::binary | std::ios::trunc);
            manifest << result.generation << ' ' << segment_name << ' '
                     << digest << '\n';
        }
        const auto manifest = directory / "manifest";
        if (std::filesystem::is_regular_file(manifest)) {
            std::filesystem::copy_file(
                manifest, manifest.string() + ".prev",
                std::filesystem::copy_options::overwrite_existing, error);
        }
        std::filesystem::remove(manifest, error);
        std::filesystem::rename(manifest_temporary, manifest);
        result.chunks = next.size();
        result.disk_bytes = std::filesystem::file_size(segment);
        std::lock_guard<std::mutex> lock(mutex);
        chunks = std::move(next);
        active_segment = segment;
        current = result;
        return result;
    }

    ProjectRecord project;
    std::filesystem::path index_root;
    MemoryBudgetManager& memory;
    std::uint64_t maximum_file_bytes;
    std::uint64_t maximum_index_bytes;
    mutable std::mutex mutex;
    IndexStatus current;
    std::vector<IndexChunk> chunks;
    std::filesystem::path active_segment;
};

ProjectIndexer::ProjectIndexer(ProjectRecord project,
                               std::filesystem::path index_root,
                               MemoryBudgetManager& memory,
                               const std::uint64_t maximum_file_bytes,
                               const std::uint64_t maximum_index_bytes)
    : state_(std::make_unique<State>(
          std::move(project), std::move(index_root), memory,
          maximum_file_bytes, maximum_index_bytes)) {
    state_->current.project_id = state_->project.id;
    std::filesystem::create_directories(
        state_->index_root / state_->project.id);
    state_->load_generation();
}

ProjectIndexer::~ProjectIndexer() = default;

// Runs discovery, path filtering, bounded reads, fingerprinting, chunking, and
// atomic publication. Cancellation publishes only at the requested safe
// partial boundary and never removes the last valid manifest.
IndexStatus ProjectIndexer::rebuild(const std::atomic_bool& cancellation,
                                    const std::size_t partial_publish_files) {
    std::vector<IndexChunk> next;
    std::map<std::string, std::vector<IndexChunk>> existing;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        for (const auto& chunk : state_->chunks) {
            existing[chunk.relative_path].push_back(chunk);
        }
    }
    IndexStatus result;
    result.project_id = state_->project.id;
    result.generation = state_->current.generation + 1U;
    std::error_code error;
    for (std::filesystem::recursive_directory_iterator iterator(
             state_->project.root,
             std::filesystem::directory_options::skip_permission_denied, error),
         end;
         iterator != end && !error; iterator.increment(error)) {
        if (cancellation.load()) {
            result.cancelled = true;
            result.partial = !next.empty();
            break;
        }
        if (!iterator->is_regular_file() || iterator->is_symlink() ||
            !indexable(iterator->path()) ||
            !is_path_within(state_->project.root, iterator->path())) {
            continue;
        }
        ++result.files_discovered;
        const auto file_bytes = std::filesystem::file_size(iterator->path());
        if (file_bytes == 0U ||
            file_bytes > state_->maximum_file_bytes) {
            continue;
        }
        MemoryEstimate estimate;
        estimate.transient_bytes = file_bytes;
        estimate.safety_margin_bytes = 64U * 1024U;
        const auto lease = state_->memory.reserve(
            MemoryCategory::file_content, estimate, false);
        if (!lease.admitted) {
            result.partial = !next.empty();
            result.diagnostic = lease.diagnostic;
            break;
        }
        const auto text =
            read_bounded(iterator->path(), state_->maximum_file_bytes);
        if (text.empty()) {
            state_->memory.release(lease.lease_id);
            continue;
        }
        const auto relative =
            std::filesystem::relative(iterator->path(), state_->project.root)
                .generic_string();
        const auto found = existing.find(relative);
        std::string previous_text;
        if (found != existing.end()) {
            for (const auto& chunk : found->second) {
                previous_text += chunk.text;
            }
        }
        if (found != existing.end() && previous_text == text) {
            next.insert(next.end(), found->second.begin(), found->second.end());
            ++result.files_unchanged;
        } else {
            auto chunks =
                make_chunks(relative, language_for(iterator->path()), text);
            next.insert(next.end(),
                        std::make_move_iterator(chunks.begin()),
                        std::make_move_iterator(chunks.end()));
            ++result.files_indexed;
        }
        state_->memory.release(lease.lease_id);
        result.chunks = next.size();
        if (partial_publish_files != 0U &&
            result.files_indexed + result.files_unchanged ==
                partial_publish_files) {
            result.partial = true;
        }
    }
    return state_->publish(std::move(next), result);
}

// Applies only canonical affected paths to the current generation. Deleted or
// newly excluded files remove their prior chunks; unaffected chunks are reused.
IndexStatus ProjectIndexer::update(
    const std::vector<std::filesystem::path>& changed_paths,
    const std::atomic_bool& cancellation) {
    if (changed_paths.empty()) return status();
    if (changed_paths.size() > 4096U) {
        throw std::invalid_argument("too many changed index paths");
    }
    std::vector<IndexChunk> next;
    IndexStatus result;
    {
        std::lock_guard<std::mutex> lock(state_->mutex);
        next = state_->chunks;
        result.project_id = state_->project.id;
        result.generation = state_->current.generation + 1U;
    }
    std::set<std::string> handled;
    for (const auto& supplied : changed_paths) {
        if (cancellation.load()) {
            result.cancelled = true;
            return result;
        }
        const auto path = supplied.is_absolute()
                              ? supplied.lexically_normal()
                              : (state_->project.root / supplied)
                                    .lexically_normal();
        if (!is_path_within(state_->project.root, path)) {
            throw std::invalid_argument("changed index path escapes project");
        }
        const auto relative =
            std::filesystem::relative(path, state_->project.root)
                .generic_string();
        if (!handled.insert(relative).second) continue;
        next.erase(std::remove_if(
                       next.begin(), next.end(),
                       [&relative](const IndexChunk& chunk) {
                           return chunk.relative_path == relative;
                       }),
                   next.end());
        if (!std::filesystem::is_regular_file(path) ||
            std::filesystem::is_symlink(path) || !indexable(path)) {
            continue;
        }
        ++result.files_discovered;
        const auto file_bytes = std::filesystem::file_size(path);
        if (file_bytes == 0U || file_bytes > state_->maximum_file_bytes) {
            continue;
        }
        MemoryEstimate estimate;
        estimate.transient_bytes = file_bytes;
        estimate.safety_margin_bytes = 64U * 1024U;
        const auto lease = state_->memory.reserve(
            MemoryCategory::file_content, estimate, false);
        if (!lease.admitted) {
            result.diagnostic = lease.diagnostic;
            return result;
        }
        const auto text = read_bounded(path, state_->maximum_file_bytes);
        if (!text.empty()) {
            auto chunks = make_chunks(relative, language_for(path), text);
            next.insert(next.end(), std::make_move_iterator(chunks.begin()),
                        std::make_move_iterator(chunks.end()));
            ++result.files_indexed;
        }
        state_->memory.release(lease.lease_id);
    }
    return state_->publish(std::move(next), result);
}

std::vector<IndexChunk> ProjectIndexer::search_text(
    const std::string& literal, const std::size_t maximum_results) const {
    if (literal.empty() || maximum_results == 0U ||
        maximum_results > 1024U) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<IndexChunk> result;
    for (const auto& chunk : state_->chunks) {
        if (chunk.text.find(literal) != std::string::npos) {
            result.push_back(chunk);
            if (result.size() == maximum_results) break;
        }
    }
    return result;
}

// Returns only exact identifier-boundary matches, preventing a request for
// "token" from silently matching unrelated identifiers such as "tokenizer".
std::vector<IndexChunk> ProjectIndexer::search_symbol(
    const std::string& symbol, const std::size_t maximum_results) const {
    if (symbol.empty() || maximum_results == 0U ||
        maximum_results > 1024U ||
        !std::all_of(symbol.begin(), symbol.end(), identifier_character)) {
        return {};
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<IndexChunk> result;
    for (const auto& chunk : state_->chunks) {
        if (contains_symbol(chunk.text, symbol)) {
            result.push_back(chunk);
            if (result.size() == maximum_results) break;
        }
    }
    return result;
}

IndexStatus ProjectIndexer::status() const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->current;
}

class ProjectIndexService::State final {
public:
    struct Task {
        ProjectRecord project;
        std::vector<std::filesystem::path> changed_paths;
        IndexTrigger trigger{IndexTrigger::manual};
    };

    State(std::filesystem::path root, MemoryBudgetManager& manager,
          const std::size_t maximum)
        : index_root(std::move(root)), memory(manager), maximum_queued(maximum),
          worker([this]() { run(); }) {}

    ~State() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            cancellation.store(true);
        }
        changed.notify_all();
        if (worker.joinable()) worker.join();
    }

    // Dequeues one project at a time, builds through ProjectIndexer, and
    // records a bounded status. The worker never owns ProjectCatalog state.
    void run() {
        while (true) {
            Task task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                changed.wait(lock,
                             [this]() { return stopping || !queued.empty(); });
                if (stopping) return;
                task = std::move(queued.front());
                queued.pop_front();
                active_project = task.project.id;
                cancellation.store(false);
                statuses[task.project.id].state = IndexJobState::running;
                statuses[task.project.id].queue_position = 0U;
                update_queue_positions();
            }
            try {
                auto index = std::make_unique<ProjectIndexer>(
                    task.project, index_root, memory);
                const auto result =
                    task.changed_paths.empty()
                        ? index->rebuild(cancellation)
                        : index->update(task.changed_paths, cancellation);
                std::lock_guard<std::mutex> lock(mutex);
                auto& status = statuses[task.project.id];
                status.index = result;
                status.state = result.cancelled ? IndexJobState::cancelled
                                                : IndexJobState::ready;
                status.trigger = task.trigger;
                indexes[task.project.id] = std::move(index);
            } catch (const std::exception& exception) {
                std::lock_guard<std::mutex> lock(mutex);
                auto& status = statuses[task.project.id];
                status.state = IndexJobState::failed;
                status.index.project_id = task.project.id;
                status.index.diagnostic = exception.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                active_project.clear();
            }
        }
    }

    // Recomputes observable positions after every queue mutation.
    void update_queue_positions() {
        std::size_t position = 1U;
        for (const auto& task : queued) {
            statuses[task.project.id].queue_position = position++;
        }
    }

    std::filesystem::path index_root;
    MemoryBudgetManager& memory;
    std::size_t maximum_queued;
    mutable std::mutex mutex;
    std::condition_variable changed;
    std::deque<Task> queued;
    std::map<std::string, IndexServiceStatus> statuses;
    std::map<std::string, std::unique_ptr<ProjectIndexer>> indexes;
    std::string active_project;
    std::atomic_bool cancellation{false};
    bool stopping{false};
    std::thread worker;
};

ProjectIndexService::ProjectIndexService(std::filesystem::path index_root,
                                         MemoryBudgetManager& memory,
                                         const std::size_t maximum_queued_projects)
    : state_(std::make_unique<State>(
          std::move(index_root), memory, maximum_queued_projects)) {
    if (maximum_queued_projects == 0U) {
        throw std::invalid_argument("index queue limit must be positive");
    }
}

ProjectIndexService::~ProjectIndexService() = default;

// Admits one unique project rebuild into the fixed single-worker queue.
bool ProjectIndexService::request_rebuild(const ProjectRecord& project) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping || state_->queued.size() >= state_->maximum_queued ||
        state_->active_project == project.id ||
        std::any_of(state_->queued.begin(), state_->queued.end(),
                    [&project](const State::Task& queued) {
                        return queued.project.id == project.id;
                    })) {
        return false;
    }
    auto& status = state_->statuses[project.id];
    status.index.project_id = project.id;
    status.state = IndexJobState::queued;
    status.trigger = IndexTrigger::manual;
    state_->queued.push_back({project, {}, IndexTrigger::manual});
    state_->update_queue_positions();
    state_->changed.notify_one();
    return true;
}

// Coalesces save/watcher paths for one queued project. Branch-switch and
// periodic notifications intentionally request a full scan because their
// affected path set cannot be proven complete.
bool ProjectIndexService::request_update(
    const ProjectRecord& project,
    const std::vector<std::filesystem::path>& changed_paths,
    const IndexTrigger trigger) {
    if (trigger == IndexTrigger::manual ||
        ((trigger == IndexTrigger::save ||
          trigger == IndexTrigger::watcher) &&
         changed_paths.empty()) ||
        changed_paths.size() > 4096U) {
        return false;
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->stopping || state_->active_project == project.id) return false;
    const auto queued = std::find_if(
        state_->queued.begin(), state_->queued.end(),
        [&project](const State::Task& task) {
            return task.project.id == project.id;
        });
    if (queued != state_->queued.end()) {
        if (queued->changed_paths.empty()) return true;
        if (trigger == IndexTrigger::branch_switch ||
            trigger == IndexTrigger::periodic) {
            queued->changed_paths.clear();
            queued->trigger = trigger;
            state_->statuses[project.id].trigger = trigger;
            return true;
        }
        std::set<std::filesystem::path> unique(
            queued->changed_paths.begin(), queued->changed_paths.end());
        unique.insert(changed_paths.begin(), changed_paths.end());
        if (unique.size() > 4096U) return false;
        queued->changed_paths.assign(unique.begin(), unique.end());
        queued->trigger = trigger;
        state_->statuses[project.id].trigger = trigger;
        return true;
    }
    if (state_->queued.size() >= state_->maximum_queued) return false;
    auto& status = state_->statuses[project.id];
    status.index.project_id = project.id;
    status.state = IndexJobState::queued;
    status.trigger = trigger;
    const bool full_scan = trigger == IndexTrigger::branch_switch ||
                           trigger == IndexTrigger::periodic;
    state_->queued.push_back(
        {project, full_scan ? std::vector<std::filesystem::path>{}
                            : changed_paths,
         trigger});
    state_->update_queue_positions();
    state_->changed.notify_one();
    return true;
}

// Cancels either the active rebuild or removes one queued request.
bool ProjectIndexService::cancel(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->active_project == project_id) {
        state_->cancellation.store(true);
        return true;
    }
    const auto found = std::find_if(
        state_->queued.begin(), state_->queued.end(),
        [&project_id](const State::Task& task) {
            return task.project.id == project_id;
        });
    if (found == state_->queued.end()) return false;
    state_->queued.erase(found);
    auto& status = state_->statuses[project_id];
    status.state = IndexJobState::cancelled;
    status.index.cancelled = true;
    status.queue_position = 0U;
    state_->update_queue_positions();
    return true;
}

// Returns a snapshot without exposing worker or mutable index ownership.
std::optional<IndexServiceStatus> ProjectIndexService::status(
    const std::string& project_id) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto found = state_->statuses.find(project_id);
    if (found != state_->statuses.end()) return found->second;
    const auto index = state_->indexes.find(project_id);
    if (index == state_->indexes.end()) return std::nullopt;
    return IndexServiceStatus{index->second->status(), IndexJobState::ready,
                              IndexTrigger::manual, 0U};
}

}  // namespace masterai
