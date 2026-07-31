// Phase 17: security-partitioned, byte-bounded cache hierarchy.
//
// Every entry is addressed by a CacheKey folding in tenant/user/project
// identity, the current policy generation, content identity/digest,
// component-version, and index generation (see masterai.hpp). Two different
// projects or users can never collide on the same cache id because their
// keys hash differently -- isolation is structural, not a runtime check.
// Staleness after a policy or index change is likewise structural: callers
// always build a fresh CacheKey from the *current* generation, so an old
// entry simply stops being reachable rather than needing to be hunted down
// and purged.
#include "masterai.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <fstream>
#include <list>
#include <map>
#include <mutex>
#include <sstream>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace masterai {
namespace {

std::string category_directory(CacheCategory category) {
    return to_string(category);
}

MemoryCategory memory_category_for(CacheCategory category) {
    switch (category) {
        case CacheCategory::file_content:
            return MemoryCategory::file_content;
        case CacheCategory::parsed_chunk:
        case CacheCategory::embedding:
        case CacheCategory::retrieval_result:
            return MemoryCategory::retrieval_index_cache;
        case CacheCategory::tokenization:
        case CacheCategory::prompt:
            return MemoryCategory::prompt_cache;
    }
    return MemoryCategory::retrieval_index_cache;
}

void atomic_replace(const std::filesystem::path& temporary,
                    const std::filesystem::path& target) {
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::runtime_error("cache entry replacement failed");
    }
#else
    std::filesystem::rename(temporary, target);
#endif
}

constexpr std::array<CacheCategory, 6> kAllCategories{
    CacheCategory::file_content,     CacheCategory::parsed_chunk,
    CacheCategory::embedding,        CacheCategory::retrieval_result,
    CacheCategory::tokenization,     CacheCategory::prompt};

// One entry's disk header: "MASTERAI-CACHE-1 <bytes> <checksum> <project> <user>\n"
struct EntryHeader {
    bool valid{false};
    std::uint64_t bytes{0};
    std::string checksum;
    std::string project_id;
};

EntryHeader read_entry_header(std::ifstream& input) {
    EntryHeader header;
    std::string magic;
    std::string project_field;
    std::string user_field;
    input >> magic >> header.bytes >> header.checksum >> project_field >>
        user_field;
    header.valid = static_cast<bool>(input) && magic == "MASTERAI-CACHE-1";
    header.project_id = project_field == "-" ? std::string() : project_field;
    return header;
}

}  // namespace

std::string to_string(CacheCategory category) {
    switch (category) {
        case CacheCategory::file_content:
            return "file_content";
        case CacheCategory::parsed_chunk:
            return "parsed_chunk";
        case CacheCategory::embedding:
            return "embedding";
        case CacheCategory::retrieval_result:
            return "retrieval_result";
        case CacheCategory::tokenization:
            return "tokenization";
        case CacheCategory::prompt:
            return "prompt";
    }
    return "unknown";
}

bool cache_category_from_string(const std::string& text, CacheCategory& out) {
    for (const auto category : kAllCategories) {
        if (to_string(category) == text) {
            out = category;
            return true;
        }
    }
    return false;
}

std::string CacheKey::to_cache_id() const {
    std::string joined;
    joined.reserve(user_id.size() + project_id.size() +
                   canonical_identity.size() + content_digest.size() +
                   version_tag.size() + 64U);
    joined += user_id;
    joined += '\x1f';
    joined += project_id;
    joined += '\x1f';
    joined += std::to_string(policy_generation);
    joined += '\x1f';
    joined += canonical_identity;
    joined += '\x1f';
    joined += content_digest;
    joined += '\x1f';
    joined += version_tag;
    joined += '\x1f';
    joined += std::to_string(index_generation);
    return sha256_hex(joined);
}

class CacheManager::State final {
public:
    struct Entry {
        std::string id;
        std::string project_id;
        std::uint64_t bytes{0};
        std::string memory_lease_id;
        std::chrono::steady_clock::time_point created{
            std::chrono::steady_clock::now()};
    };
    using EntryList = std::list<Entry>;

    struct Segment {
        std::list<Entry> entries;
        std::map<std::string, EntryList::iterator> index;
        std::uint64_t used_bytes{0};
        std::uint64_t hits{0};
        std::uint64_t misses{0};
        std::uint64_t evictions{0};
    };

    State(std::filesystem::path root, MemoryBudgetManager& manager,
          CachePolicy value)
        : cache_root(std::move(root)), memory(manager), policy(value) {
        for (const auto category : kAllCategories) {
            const auto directory = cache_root / category_directory(category);
            std::filesystem::create_directories(directory);
            scan(category, directory);
        }
    }

    ~State() { release_all_leases(); }

    void release_all_leases() {
        for (const auto category : kAllCategories) {
            release_segment_leases(segments[category]);
        }
    }

    void release_segment_leases(Segment& segment) {
        for (auto& entry : segment.entries) {
            if (entry.memory_lease_id.empty()) continue;
            memory.release(entry.memory_lease_id);
        }
    }

    void scan(CacheCategory category, const std::filesystem::path& directory) {
        auto& segment = segments[category];
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(directory, error), end;
             iterator != end && !error; iterator.increment(error)) {
            if (!iterator->is_regular_file()) continue;
            const auto& path = iterator->path();
            if (path.extension() != ".entry") continue;
            scan_one(segment, path);
        }
    }

    void scan_one(Segment& segment, const std::filesystem::path& path) {
        EntryHeader header;
        {
            std::ifstream input(path, std::ios::binary);
            header = read_entry_header(input);
        }
        if (!header.valid) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            return;
        }
        Entry entry;
        entry.id = path.stem().string();
        entry.project_id = header.project_id;
        entry.bytes = header.bytes;
        segment.entries.push_back(std::move(entry));
        segment.index[segment.entries.back().id] =
            std::prev(segment.entries.end());
        segment.used_bytes += header.bytes;
    }

    std::filesystem::path entry_path(CacheCategory category,
                                     const std::string& id) const {
        return cache_root / category_directory(category) / (id + ".entry");
    }

    // Caller must hold `mutex`. Takes `iterator` BY VALUE: some callers pass
    // a reference living inside `segment.index`'s own storage (e.g.
    // `found->second`), and erasing that map entry would otherwise
    // invalidate the reference out from under the rest of this function. A
    // copy of a list iterator is cheap and stays valid independent of the
    // map until the list node itself is erased below.
    void evict_locked(CacheCategory category, Segment& segment,
                      EntryList::iterator iterator) {
        const auto id = iterator->id;
        const auto bytes = iterator->bytes;
        const auto lease_id = iterator->memory_lease_id;
        if (!lease_id.empty()) {
            memory.release(lease_id);
        }
        std::error_code ignored;
        std::filesystem::remove(entry_path(category, id), ignored);
        segment.used_bytes -= bytes;
        segment.entries.erase(iterator);
        segment.index.erase(id);
    }

    // Caller must hold `mutex`. Evicts least-recently-used entries (list
    // back) until `segment` is within `policy.maximum_bytes_per_category`.
    void enforce_capacity_locked(CacheCategory category, Segment& segment) {
        while (segment.used_bytes > policy.maximum_bytes_per_category &&
              !segment.entries.empty()) {
            const auto victim = std::prev(segment.entries.end());
            evict_locked(category, segment, victim);
            ++segment.evictions;
        }
    }

    std::filesystem::path cache_root;
    MemoryBudgetManager& memory;
    CachePolicy policy;
    mutable std::mutex mutex;
    std::map<CacheCategory, Segment> segments;
    std::atomic<std::uint64_t> policy_generation{0};
};

CacheManager::CacheManager(std::filesystem::path cache_root,
                           MemoryBudgetManager& memory, CachePolicy policy)
    : state_(std::make_unique<State>(std::move(cache_root), memory, policy)) {}

CacheManager::~CacheManager() = default;

std::optional<std::string> CacheManager::get(CacheCategory category,
                                              const CacheKey& key) {
    const auto id = key.to_cache_id();
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    const auto found = segment.index.find(id);
    if (found == segment.index.end()) {
        ++segment.misses;
        return std::nullopt;
    }
    const auto path = state_->entry_path(category, id);
    EntryHeader header;
    std::string value;
    bool read_ok = false;
    {
        std::ifstream input(path, std::ios::binary);
        header = read_entry_header(input);
        input.get();  // consume the header's trailing newline
        value.resize(header.bytes, '\0');
        if (header.bytes > 0U) {
            input.read(&value[0], static_cast<std::streamsize>(header.bytes));
        }
        read_ok = static_cast<bool>(input);
    }
    const bool valid =
        header.valid && read_ok && sha256_hex(value) == header.checksum;
    if (!valid) {
        // Quarantine-by-deletion: a corrupted or tampered entry is removed
        // and reported as a miss rather than ever being served.
        state_->evict_locked(category, segment, found->second);
        ++segment.misses;
        return std::nullopt;
    }
    segment.entries.splice(segment.entries.begin(), segment.entries,
                           found->second);
    ++segment.hits;
    return value;
}

void CacheManager::put(CacheCategory category, const CacheKey& key,
                       std::string value) {
    const auto id = key.to_cache_id();
    const auto bytes = static_cast<std::uint64_t>(value.size());
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    if (bytes > state_->policy.maximum_bytes_per_category) {
        return;  // never large enough to admit, regardless of eviction
    }
    const auto existing = segment.index.find(id);
    if (existing != segment.index.end()) {
        state_->evict_locked(category, segment, existing->second);
    }
    MemoryEstimate estimate;
    estimate.transient_bytes = bytes;
    const auto admission =
        state_->memory.reserve(memory_category_for(category), estimate, false);
    if (!admission.admitted) {
        return;  // best-effort: skip caching rather than fail the request
    }
    const auto directory = state_->cache_root / category_directory(category);
    const auto temporary = directory / (id + ".entry.tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "MASTERAI-CACHE-1 " << bytes << ' ' << sha256_hex(value)
               << ' ' << (key.project_id.empty() ? "-" : key.project_id) << ' '
               << (key.user_id.empty() ? "-" : key.user_id) << '\n';
        output.write(value.data(), static_cast<std::streamsize>(value.size()));
        if (!output) {
            output.close();
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            state_->memory.release(admission.lease_id);
            return;
        }
    }
    atomic_replace(temporary, state_->entry_path(category, id));
    State::Entry entry;
    entry.id = id;
    entry.project_id = key.project_id;
    entry.bytes = bytes;
    entry.memory_lease_id = admission.lease_id;
    segment.entries.push_front(std::move(entry));
    segment.index[id] = segment.entries.begin();
    segment.used_bytes += bytes;
    state_->enforce_capacity_locked(category, segment);
}

void CacheManager::invalidate_project(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (const auto category : kAllCategories) {
        auto& segment = state_->segments[category];
        for (auto iterator = segment.entries.begin();
            iterator != segment.entries.end();) {
            if (iterator->project_id != project_id) {
                ++iterator;
                continue;
            }
            const auto next = std::next(iterator);
            state_->evict_locked(category, segment, iterator);
            iterator = next;
        }
    }
}

void CacheManager::invalidate_policy() {
    state_->policy_generation.fetch_add(1U, std::memory_order_relaxed);
}

std::uint64_t CacheManager::current_policy_generation() const {
    return state_->policy_generation.load(std::memory_order_relaxed);
}

CacheStatus CacheManager::status() const {
    CacheStatus result;
    std::lock_guard<std::mutex> lock(state_->mutex);
    const auto now = std::chrono::steady_clock::now();
    for (const auto category : kAllCategories) {
        const auto found = state_->segments.find(category);
        CacheCategoryStatus entry;
        entry.capacity_bytes = state_->policy.maximum_bytes_per_category;
        if (found != state_->segments.end()) {
            const auto& segment = found->second;
            entry.used_bytes = segment.used_bytes;
            entry.entries = segment.entries.size();
            entry.hits = segment.hits;
            entry.misses = segment.misses;
            entry.evictions = segment.evictions;
            if (!segment.entries.empty()) {
                const auto oldest = segment.entries.back().created;
                entry.oldest_entry_age_seconds = static_cast<std::uint64_t>(
                    std::chrono::duration_cast<std::chrono::seconds>(now - oldest)
                        .count());
            }
        }
        result.categories[category] = entry;
    }
    return result;
}

void CacheManager::trim() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (const auto category : kAllCategories) {
        state_->enforce_capacity_locked(category, state_->segments[category]);
    }
}

void CacheManager::clear(std::optional<CacheCategory> category) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (const auto candidate : kAllCategories) {
        if (category.has_value() && *category != candidate) continue;
        auto& segment = state_->segments[candidate];
        while (!segment.entries.empty()) {
            state_->evict_locked(candidate, segment, segment.entries.begin());
        }
    }
}

namespace {

void write_framed(std::ostringstream& output, const std::string& text) {
    output << text.size() << ' ';
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
}

std::string read_framed(std::istringstream& input) {
    std::size_t size = 0U;
    input >> size;
    input.get();  // consume the single separating space
    std::string text(size, '\0');
    if (size > 0U) input.read(&text[0], static_cast<std::streamsize>(size));
    return text;
}

}  // namespace

std::string serialize_retrieval_outcome(const RetrievalOutcome& outcome) {
    std::ostringstream output;
    output << "MASTERAI-RETRIEVAL-CACHE-1 " << (outcome.partial ? 1 : 0)
           << ' ' << outcome.disclosure.size() << '\n';
    write_framed(output, outcome.context_text);
    write_framed(output, outcome.strategy);
    write_framed(output, outcome.diagnostic);
    for (const auto& entry : outcome.disclosure) {
        output << entry.offset << ' ' << entry.index_generation << ' '
               << static_cast<std::int64_t>(entry.score * 1000.0) << ' '
               << (entry.included ? 1 : 0) << '\n';
        write_framed(output, entry.source);
        write_framed(output, entry.relative_path);
        write_framed(output, entry.reason);
    }
    return output.str();
}

RetrievalOutcome deserialize_retrieval_outcome(const std::string& encoded) {
    std::istringstream input(encoded);
    std::string magic;
    int partial = 0;
    std::size_t count = 0U;
    input >> magic >> partial >> count;
    input.get();
    RetrievalOutcome outcome;
    if (!input || magic != "MASTERAI-RETRIEVAL-CACHE-1") {
        outcome.diagnostic = "cached retrieval outcome is invalid";
        return outcome;
    }
    outcome.partial = partial != 0;
    outcome.context_text = read_framed(input);
    outcome.strategy = read_framed(input);
    outcome.diagnostic = read_framed(input);
    for (std::size_t i = 0U; i < count && input; ++i) {
        RetrievalDisclosureEntry entry;
        std::int64_t score_milli = 0;
        int included = 0;
        input >> entry.offset >> entry.index_generation >> score_milli >>
            included;
        input.get();
        entry.score = static_cast<double>(score_milli) / 1000.0;
        entry.included = included != 0;
        entry.source = read_framed(input);
        entry.relative_path = read_framed(input);
        entry.reason = read_framed(input);
        outcome.disclosure.push_back(std::move(entry));
    }
    if (!input) {
        return RetrievalOutcome{};
    }
    return outcome;
}

std::string CacheManager::to_json(const CacheStatus& status) {
    std::string body = "{\"categories\":{";
    bool first = true;
    for (const auto& [category, value] : status.categories) {
        if (!first) body += ",";
        first = false;
        body += "\"" + to_string(category) + "\":{" +
               "\"capacityBytes\":" + std::to_string(value.capacity_bytes) +
               ",\"usedBytes\":" + std::to_string(value.used_bytes) +
               ",\"entries\":" + std::to_string(value.entries) +
               ",\"hits\":" + std::to_string(value.hits) +
               ",\"misses\":" + std::to_string(value.misses) +
               ",\"evictions\":" + std::to_string(value.evictions) +
               ",\"oldestEntryAgeSeconds\":" +
               std::to_string(value.oldest_entry_age_seconds) + "}";
    }
    return body + "}}";
}

}  // namespace masterai
