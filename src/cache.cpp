// Phase 17: security-partitioned, byte-bounded cache hierarchy.
// Phase 22: hierarchical (L0-L5) category set, TinyLFU-style bounded
// admission, segmented (probationary/protected/pinned) eviction, and
// short-lived negative caching layered on top of the same disk-backed
// entry format.
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
//
// Segmented eviction (Phase 22): each category's entries live in one of
// three tiers -- probationary (newly admitted / single access), protected
// (promoted after a second access, capped at a fraction of the category
// budget), or pinned (administrator-exempted from capacity eviction until
// explicitly unpinned). A one-time scan that floods a category with
// never-reused entries can only ever evict other probationary entries; it
// structurally cannot touch the protected tier, which is what keeps a
// proven-hot working set alive under a large one-off scan. A single-hash
// frequency sketch (a deliberately simplified stand-in for a full
// multi-hash TinyLFU count-min sketch -- see the scope note in
// docs/PLAN.md's Phase 22 entry) adds a secondary admission gate: a brand
// new candidate is refused rather than displacing a demonstrably hotter
// protected entry when the category is already full.
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
#include <vector>

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
        case CacheCategory::file_metadata:
        case CacheCategory::file_content:
        case CacheCategory::static_web_asset:
            return MemoryCategory::file_content;
        case CacheCategory::parsed_document:
        case CacheCategory::source_chunk:
        case CacheCategory::symbol:
        case CacheCategory::retrieval_result:
        case CacheCategory::reranking:
        case CacheCategory::embedding:
        case CacheCategory::mcp_resource:
            return MemoryCategory::retrieval_index_cache;
        case CacheCategory::tokenization:
        case CacheCategory::prompt_template:
        case CacheCategory::prompt_fragment:
            return MemoryCategory::prompt_cache;
        case CacheCategory::model_manifest:
        case CacheCategory::download_metadata:
        case CacheCategory::hardware_probe:
        case CacheCategory::tuning_profile:
            return MemoryCategory::background_jobs;
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

constexpr std::array<CacheCategory, 17> kAllCategories{
    CacheCategory::file_metadata,   CacheCategory::file_content,
    CacheCategory::parsed_document, CacheCategory::source_chunk,
    CacheCategory::symbol,          CacheCategory::retrieval_result,
    CacheCategory::reranking,       CacheCategory::embedding,
    CacheCategory::tokenization,    CacheCategory::prompt_template,
    CacheCategory::prompt_fragment, CacheCategory::model_manifest,
    CacheCategory::download_metadata, CacheCategory::hardware_probe,
    CacheCategory::tuning_profile,  CacheCategory::mcp_resource,
    CacheCategory::static_web_asset};

// One entry's disk header:
// "MASTERAI-CACHE-2 <bytes> <checksum> <project> <user> <tier> <negative> <expires_epoch>\n"
// tier: 0=probationary 1=protected 2=pinned. negative: 0/1. expires_epoch:
// 0 means "no expiry" (only negative entries ever set this).
struct EntryHeader {
    bool valid{false};
    std::uint64_t bytes{0};
    std::string checksum;
    std::string project_id;
    int tier{0};
    bool negative{false};
    std::uint64_t expires_epoch{0};
};

EntryHeader read_entry_header(std::ifstream& input) {
    EntryHeader header;
    std::string magic;
    std::string project_field;
    std::string user_field;
    int negative_field = 0;
    input >> magic >> header.bytes >> header.checksum >> project_field >>
        user_field >> header.tier >> negative_field >> header.expires_epoch;
    header.valid = static_cast<bool>(input) && magic == "MASTERAI-CACHE-2";
    header.project_id = project_field == "-" ? std::string() : project_field;
    header.negative = negative_field != 0;
    return header;
}

std::uint64_t now_epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

}  // namespace

std::string to_string(CacheCategory category) {
    switch (category) {
        case CacheCategory::file_metadata:
            return "file_metadata";
        case CacheCategory::file_content:
            return "file_content";
        case CacheCategory::parsed_document:
            return "parsed_document";
        case CacheCategory::source_chunk:
            return "source_chunk";
        case CacheCategory::symbol:
            return "symbol";
        case CacheCategory::retrieval_result:
            return "retrieval_result";
        case CacheCategory::reranking:
            return "reranking";
        case CacheCategory::embedding:
            return "embedding";
        case CacheCategory::tokenization:
            return "tokenization";
        case CacheCategory::prompt_template:
            return "prompt_template";
        case CacheCategory::prompt_fragment:
            return "prompt_fragment";
        case CacheCategory::model_manifest:
            return "model_manifest";
        case CacheCategory::download_metadata:
            return "download_metadata";
        case CacheCategory::hardware_probe:
            return "hardware_probe";
        case CacheCategory::tuning_profile:
            return "tuning_profile";
        case CacheCategory::mcp_resource:
            return "mcp_resource";
        case CacheCategory::static_web_asset:
            return "static_web_asset";
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

namespace {

// Deliberately simplified single-hash frequency sketch: a fixed-size ring of
// saturating 4-bit counters (packed two-per-byte) with periodic halving
// ("aging"), the same reset-under-pressure shape a full multi-hash
// count-min-sketch TinyLFU would use, without the extra hash functions a
// stricter implementation would add. Good enough to distinguish "never seen
// before" from "seen repeatedly" for the admission gate below; not a
// precise frequency estimate.
class FrequencySketch {
public:
    explicit FrequencySketch(std::size_t slots = 8192U)
        : size_(slots), counters_(new std::atomic<unsigned int>[slots]) {
        for (std::size_t i = 0U; i < size_; ++i) {
            counters_[i].store(0U, std::memory_order_relaxed);
        }
    }

    unsigned int estimate(const std::string& id) const {
        return counters_[slot_for(id)].load(std::memory_order_relaxed);
    }

    void record(const std::string& id) {
        auto& counter = counters_[slot_for(id)];
        unsigned int value = counter.load(std::memory_order_relaxed);
        if (value >= 15U) {
            maybe_age();
            return;
        }
        counter.fetch_add(1U, std::memory_order_relaxed);
        if (total_.fetch_add(1U, std::memory_order_relaxed) + 1U > size_ * 10U) {
            maybe_age();
        }
    }

private:
    std::size_t slot_for(const std::string& id) const {
        return std::hash<std::string>{}(id) % size_;
    }

    void maybe_age() {
        for (std::size_t i = 0U; i < size_; ++i) {
            unsigned int value = counters_[i].load(std::memory_order_relaxed);
            counters_[i].store(value >> 1, std::memory_order_relaxed);
        }
        total_.store(0U, std::memory_order_relaxed);
    }

    std::size_t size_;
    std::unique_ptr<std::atomic<unsigned int>[]> counters_;
    std::atomic<std::uint64_t> total_{0};
};

}  // namespace

class CacheManager::State final {
public:
    enum class Tier { probationary = 0, protected_tier = 1, pinned = 2 };

    struct Entry {
        std::string id;
        std::string project_id;
        std::uint64_t bytes{0};
        std::string memory_lease_id;
        Tier tier{Tier::probationary};
        bool negative{false};
        std::uint64_t expires_epoch{0};
        std::chrono::steady_clock::time_point created{
            std::chrono::steady_clock::now()};
    };
    using EntryList = std::list<Entry>;

    struct Segment {
        std::list<Entry> entries;  // probationary, MRU-first
        std::list<Entry> protected_entries;  // MRU-first
        std::list<Entry> pinned_entries;
        std::map<std::string, std::pair<EntryList*, EntryList::iterator>> index;
        std::uint64_t used_bytes{0};
        std::uint64_t hits{0};
        std::uint64_t misses{0};
        std::uint64_t evictions{0};
        std::uint64_t admission_rejections{0};
        FrequencySketch sketch;
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
            auto& segment = segments[category];
            release_list_leases(segment.entries);
            release_list_leases(segment.protected_entries);
            release_list_leases(segment.pinned_entries);
        }
    }

    void release_list_leases(std::list<Entry>& list) {
        for (auto& entry : list) {
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
        if (header.negative && header.expires_epoch != 0U &&
            header.expires_epoch <= now_epoch_seconds()) {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
            return;
        }
        Entry entry;
        entry.id = path.stem().string();
        entry.project_id = header.project_id;
        entry.bytes = header.bytes;
        entry.negative = header.negative;
        entry.expires_epoch = header.expires_epoch;
        entry.tier = header.tier == 2 ? Tier::pinned
                    : header.tier == 1 ? Tier::protected_tier
                                       : Tier::probationary;
        auto* list = list_for(segment, entry.tier);
        list->push_back(std::move(entry));
        segment.index[list->back().id] = {list, std::prev(list->end())};
        segment.used_bytes += header.bytes;
    }

    static std::list<Entry>* list_for_tier(Segment& segment, Tier tier) {
        return list_for(segment, tier);
    }

    static std::list<Entry>* list_for(Segment& segment, Tier tier) {
        switch (tier) {
            case Tier::probationary:
                return &segment.entries;
            case Tier::protected_tier:
                return &segment.protected_entries;
            case Tier::pinned:
                return &segment.pinned_entries;
        }
        return &segment.entries;
    }

    std::filesystem::path entry_path(CacheCategory category,
                                     const std::string& id) const {
        return cache_root / category_directory(category) / (id + ".entry");
    }

    // Caller must hold `mutex`.
    void evict_locked(CacheCategory category, Segment& segment,
                      std::list<Entry>* list, EntryList::iterator iterator) {
        const auto id = iterator->id;
        const auto bytes = iterator->bytes;
        const auto lease_id = iterator->memory_lease_id;
        if (!lease_id.empty()) {
            memory.release(lease_id);
        }
        std::error_code ignored;
        std::filesystem::remove(entry_path(category, id), ignored);
        segment.used_bytes -= bytes;
        list->erase(iterator);
        segment.index.erase(id);
    }

    // Protected tier is capped at a fraction of the category budget so it
    // can never itself grow to consume the whole budget and starve
    // probationary admission entirely.
    std::uint64_t protected_capacity_bytes() const {
        return policy.maximum_bytes_per_category * 4U / 5U;
    }

    // Demotes the coldest protected entry back to probationary when the
    // protected tier exceeds its cap, instead of deleting it outright --
    // protected entries have already proven reuse, so losing them entirely
    // to a transient cap breach is wasteful when probationary has room.
    void enforce_protected_cap_locked(CacheCategory category, Segment& segment) {
        (void)category;
        while (protected_bytes(segment) > protected_capacity_bytes() &&
              !segment.protected_entries.empty()) {
            auto victim = std::prev(segment.protected_entries.end());
            Entry moved = std::move(*victim);
            moved.tier = Tier::probationary;
            segment.protected_entries.erase(victim);
            segment.entries.push_front(std::move(moved));
            segment.index[segment.entries.front().id] = {&segment.entries,
                                                          segment.entries.begin()};
        }
    }

    static std::uint64_t protected_bytes(const Segment& segment) {
        std::uint64_t total = 0U;
        for (const auto& entry : segment.protected_entries) total += entry.bytes;
        return total;
    }

    // Caller must hold `mutex`. Evicts least-recently-used probationary
    // entries first; only reaches into the protected tier if probationary
    // alone cannot bring the category back under budget. Pinned entries are
    // never touched here.
    void enforce_capacity_locked(CacheCategory category, Segment& segment) {
        while (segment.used_bytes > policy.maximum_bytes_per_category &&
              !segment.entries.empty()) {
            const auto victim = std::prev(segment.entries.end());
            evict_locked(category, segment, &segment.entries, victim);
            ++segment.evictions;
        }
        while (segment.used_bytes > policy.maximum_bytes_per_category &&
              !segment.protected_entries.empty()) {
            const auto victim = std::prev(segment.protected_entries.end());
            evict_locked(category, segment, &segment.protected_entries, victim);
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
    auto* list = found->second.first;
    auto iterator = found->second.second;
    if (iterator->negative) {
        // Negative markers are surfaced through is_negative(), never get().
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
        state_->evict_locked(category, segment, list, iterator);
        ++segment.misses;
        return std::nullopt;
    }
    segment.sketch.record(id);
    ++segment.hits;
    if (list == &segment.entries) {
        // Second observed access: promote probationary -> protected.
        CacheManager::State::Entry moved = std::move(*iterator);
        moved.tier = CacheManager::State::Tier::protected_tier;
        segment.entries.erase(iterator);
        segment.protected_entries.push_front(std::move(moved));
        segment.index[id] = {&segment.protected_entries,
                             segment.protected_entries.begin()};
        state_->enforce_protected_cap_locked(category, segment);
    } else if (list == &segment.protected_entries) {
        segment.protected_entries.splice(segment.protected_entries.begin(),
                                         segment.protected_entries, iterator);
    }
    // Pinned entries need no reordering: they are exempt from LRU eviction.
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
    auto existing = segment.index.find(id);
    CacheManager::State::Tier tier = CacheManager::State::Tier::probationary;
    if (existing != segment.index.end()) {
        tier = existing->second.second->tier;
        // TinyLFU-style admission gate: if replacing this entry would need
        // to evict a colder-tier entry with the exact same id it can't --
        // same id means same content identity, so this is always a refresh
        // of the current tier, never a competing candidate.
        state_->evict_locked(category, segment, existing->second.first,
                             existing->second.second);
    } else if (segment.used_bytes + bytes >
              state_->policy.maximum_bytes_per_category) {
        // A genuinely new candidate competing for space: refuse admission
        // if the coldest evictable (probationary, else protected) entry is
        // estimated hotter than this brand-new, never-yet-seen id. This is
        // the admission half of TinyLFU; the segmented eviction above is
        // the eviction half.
        const CacheManager::State::Entry* victim = nullptr;
        if (!segment.entries.empty()) {
            victim = &segment.entries.back();
        } else if (!segment.protected_entries.empty()) {
            victim = &segment.protected_entries.back();
        }
        if (victim != nullptr) {
            const auto candidate_frequency = segment.sketch.estimate(id);
            const auto victim_frequency = segment.sketch.estimate(victim->id);
            if (victim_frequency > candidate_frequency) {
                ++segment.admission_rejections;
                return;  // best-effort: skip caching rather than evict a hotter entry
            }
        }
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
        output << "MASTERAI-CACHE-2 " << bytes << ' ' << sha256_hex(value)
               << ' ' << (key.project_id.empty() ? "-" : key.project_id) << ' '
               << (key.user_id.empty() ? "-" : key.user_id) << ' '
               << static_cast<int>(tier) << ' ' << 0 << ' ' << 0 << '\n';
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
    CacheManager::State::Entry entry;
    entry.id = id;
    entry.project_id = key.project_id;
    entry.bytes = bytes;
    entry.memory_lease_id = admission.lease_id;
    entry.tier = tier;
    auto* list = CacheManager::State::list_for_tier(segment, tier);
    list->push_front(std::move(entry));
    segment.index[id] = {list, list->begin()};
    segment.used_bytes += bytes;
    // Frequency is recorded on `get()` hits only (see below), not on every
    // put(): a brand-new, never-yet-reused entry must start at estimate 0
    // so the admission gate above only ever protects entries that have
    // actually proven reuse, never merely-inserted ones -- otherwise a
    // stream of first-time-distinct puts (the common non-scan case) would
    // starve itself the moment the category fills.
    state_->enforce_capacity_locked(category, segment);
}

void CacheManager::pin(CacheCategory category, const CacheKey& key) {
    const auto id = key.to_cache_id();
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    const auto found = segment.index.find(id);
    if (found == segment.index.end()) return;
    auto* list = found->second.first;
    if (list == &segment.pinned_entries) return;
    CacheManager::State::Entry moved = std::move(*found->second.second);
    moved.tier = CacheManager::State::Tier::pinned;
    list->erase(found->second.second);
    segment.pinned_entries.push_front(std::move(moved));
    segment.index[id] = {&segment.pinned_entries, segment.pinned_entries.begin()};
}

void CacheManager::unpin(CacheCategory category, const CacheKey& key) {
    const auto id = key.to_cache_id();
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    const auto found = segment.index.find(id);
    if (found == segment.index.end()) return;
    if (found->second.first != &segment.pinned_entries) return;
    CacheManager::State::Entry moved = std::move(*found->second.second);
    moved.tier = CacheManager::State::Tier::probationary;
    segment.pinned_entries.erase(found->second.second);
    segment.entries.push_front(std::move(moved));
    segment.index[id] = {&segment.entries, segment.entries.begin()};
    state_->enforce_capacity_locked(category, segment);
}

void CacheManager::put_negative(CacheCategory category, const CacheKey& key,
                                std::chrono::seconds ttl) {
    const auto id = key.to_cache_id();
    const auto expires =
        now_epoch_seconds() + static_cast<std::uint64_t>(std::max<long long>(
                                  0LL, static_cast<long long>(ttl.count())));
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    const auto existing = segment.index.find(id);
    if (existing != segment.index.end()) {
        state_->evict_locked(category, segment, existing->second.first,
                             existing->second.second);
    }
    const auto directory = state_->cache_root / category_directory(category);
    const auto temporary = directory / (id + ".entry.tmp");
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output << "MASTERAI-CACHE-2 " << 0U << ' ' << sha256_hex(std::string())
               << ' ' << (key.project_id.empty() ? "-" : key.project_id) << ' '
               << (key.user_id.empty() ? "-" : key.user_id) << ' ' << 0 << ' '
               << 1 << ' ' << expires << '\n';
    }
    atomic_replace(temporary, state_->entry_path(category, id));
    CacheManager::State::Entry entry;
    entry.id = id;
    entry.project_id = key.project_id;
    entry.bytes = 0U;
    entry.negative = true;
    entry.expires_epoch = expires;
    entry.tier = CacheManager::State::Tier::probationary;
    segment.entries.push_front(std::move(entry));
    segment.index[id] = {&segment.entries, segment.entries.begin()};
}

bool CacheManager::is_negative(CacheCategory category, const CacheKey& key) {
    const auto id = key.to_cache_id();
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto& segment = state_->segments[category];
    const auto found = segment.index.find(id);
    if (found == segment.index.end()) return false;
    auto iterator = found->second.second;
    if (!iterator->negative) return false;
    if (iterator->expires_epoch != 0U &&
        iterator->expires_epoch <= now_epoch_seconds()) {
        state_->evict_locked(category, segment, found->second.first, iterator);
        return false;
    }
    return true;
}

void CacheManager::invalidate_project(const std::string& project_id) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    for (const auto category : kAllCategories) {
        auto& segment = state_->segments[category];
        for (auto* list : {&segment.entries, &segment.protected_entries,
                           &segment.pinned_entries}) {
            for (auto iterator = list->begin(); iterator != list->end();) {
                if (iterator->project_id != project_id) {
                    ++iterator;
                    continue;
                }
                const auto next = std::next(iterator);
                state_->evict_locked(category, segment, list, iterator);
                iterator = next;
            }
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
            entry.entries = segment.entries.size() + segment.protected_entries.size() +
                            segment.pinned_entries.size();
            entry.hits = segment.hits;
            entry.misses = segment.misses;
            entry.evictions = segment.evictions;
            entry.protected_entries = segment.protected_entries.size();
            entry.pinned_entries = segment.pinned_entries.size();
            entry.admission_rejections = segment.admission_rejections;
            for (const auto* list : {&segment.entries, &segment.protected_entries,
                                     &segment.pinned_entries}) {
                for (const auto& item : *list) {
                    if (item.negative) ++entry.negative_entries;
                }
            }
            std::chrono::steady_clock::time_point oldest;
            bool have_oldest = false;
            for (const auto* list : {&segment.entries, &segment.protected_entries,
                                     &segment.pinned_entries}) {
                if (list->empty()) continue;
                const auto candidate = list->back().created;
                if (!have_oldest || candidate < oldest) {
                    oldest = candidate;
                    have_oldest = true;
                }
            }
            if (have_oldest) {
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
        for (auto* list : {&segment.entries, &segment.protected_entries,
                           &segment.pinned_entries}) {
            while (!list->empty()) {
                state_->evict_locked(candidate, segment, list, list->begin());
            }
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

RetrievalCacheBenchmarkReport benchmark_retrieval_cache(
    CacheManager& cache, const RetrievalPlanner& planner,
    const RetrievalRequest& request, const CacheKey& key,
    const std::uint64_t iterations) {
    if (iterations == 0U) {
        throw std::invalid_argument("cache benchmark iterations must be positive");
    }
    RetrievalCacheBenchmarkReport report;
    report.iterations = iterations;
    RetrievalOutcome reference;
    std::size_t observed_bytes = 0U;
    const auto uncached_started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < iterations; ++index) {
        auto outcome = planner.retrieve(request);
        observed_bytes += outcome.context_text.size();
        if (index == 0U) reference = std::move(outcome);
    }
    report.uncached_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - uncached_started).count());

    cache.put(CacheCategory::retrieval_result, key,
              serialize_retrieval_outcome(reference));
    bool stable = true;
    const auto cached_started = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < iterations; ++index) {
        const auto encoded = cache.get(CacheCategory::retrieval_result, key);
        if (!encoded) {
            stable = false;
            continue;
        }
        const auto outcome = deserialize_retrieval_outcome(*encoded);
        observed_bytes += outcome.context_text.size();
        stable = stable && outcome.context_text == reference.context_text &&
                 outcome.strategy == reference.strategy &&
                 outcome.disclosure.size() == reference.disclosure.size();
    }
    report.cached_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - cached_started).count());
    report.output_stable = stable && observed_bytes > 0U;
    report.latency_improved = report.cached_microseconds <
                              report.uncached_microseconds;
    return report;
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
               ",\"protectedEntries\":" + std::to_string(value.protected_entries) +
               ",\"pinnedEntries\":" + std::to_string(value.pinned_entries) +
               ",\"negativeEntries\":" + std::to_string(value.negative_entries) +
               ",\"admissionRejections\":" +
               std::to_string(value.admission_rejections) +
               ",\"oldestEntryAgeSeconds\":" +
               std::to_string(value.oldest_entry_age_seconds) + "}";
    }
    return body + "}}";
}

}  // namespace masterai
