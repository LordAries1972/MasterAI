// Phase 103: Web Research and Knowledge Acquisition -- see masterai.hpp's
// ResearchEngine/ReliabilityTierStore comment block for the full design.
// This unit owns: the editable domain-reliability tier table, the in-house
// HTML-to-text extractor, the two search-provider callers (Google Custom
// Search JSON API, Bing Web Search API), the page fetcher, the run
// orchestrator, persisted run history, and this feature's JSON output.
#include "masterai.hpp"
#include "json.hpp"
#include "curl_process.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string random_id() {
    static thread_local std::mt19937_64 engine{std::random_device{}()};
    std::uniform_int_distribution<std::uint64_t> distribution;
    char buffer[17];
    std::snprintf(buffer, sizeof(buffer), "%016llx",
                 static_cast<unsigned long long>(distribution(engine)));
    return std::string(buffer);
}

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
                    static constexpr char digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result += digits[(character >> 4U) & 0x0fU];
                    result += digits[character & 0x0fU];
                } else {
                    result += static_cast<char>(character);
                }
        }
    }
    return result;
}

// Persisted-record pack/unpack, identical in shape to the copies in
// downloads.cpp/ml_knowledge.cpp (this codebase keeps one copy per
// translation unit rather than sharing a header for this small helper).
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
            throw std::runtime_error("persisted research record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted research record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string url_encode(const std::string& value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    result.reserve(value.size() * 3U);
    for (const unsigned char character : value) {
        if (std::isalnum(character) || character == '-' || character == '_' ||
            character == '.' || character == '~') {
            result += static_cast<char>(character);
        } else {
            result += '%';
            result += hex[(character >> 4U) & 0x0fU];
            result += hex[character & 0x0fU];
        }
    }
    return result;
}

std::string lowercase(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

// Extracts "host" from a URL, e.g. "https://www.nist.gov/path" -> "www.nist.gov".
std::string host_from_url(const std::string& url) {
    auto start = url.find("://");
    start = (start == std::string::npos) ? 0U : start + 3U;
    auto end = url.find_first_of("/?#", start);
    if (end == std::string::npos) end = url.size();
    auto host = url.substr(start, end - start);
    const auto at = host.find('@');
    if (at != std::string::npos) host = host.substr(at + 1U);
    return lowercase(host);
}

// Runs curl with `--output <temp file>`, reads the temp file back as text,
// and always removes it. Shared by the search-provider callers and
// fetch_page_text() below. `extra_arguments` carries whatever the caller
// needs beyond the URL itself (headers, proto/timeout flags).
std::string curl_capture(const std::filesystem::path& curl_executable,
                         const std::string& url,
                         const std::vector<std::string>& extra_arguments,
                         const std::filesystem::path& log_path,
                         std::uint32_t timeout_seconds,
                         std::uint64_t max_bytes,
                         const std::atomic_bool& cancellation) {
    if (curl_executable.empty() ||
        !std::filesystem::is_regular_file(curl_executable) ||
        std::filesystem::is_symlink(curl_executable)) {
        throw std::runtime_error(
            "research features require an approved curl executable "
            "(downloads.curlExecutable)");
    }
    const auto temp_output = log_path.parent_path() /
        ("research-fetch-" + random_id() + ".tmp");
    std::filesystem::create_directories(temp_output.parent_path());
    struct TempCleanup {
        std::filesystem::path path;
        ~TempCleanup() {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    } cleanup{temp_output};

    std::vector<std::string> arguments{
        "--proto", "=https", "--tlsv1.2", "--fail", "--location", "--silent",
        "--show-error", "--max-time", std::to_string(timeout_seconds),
        "--max-filesize", std::to_string(max_bytes), "--output",
        temp_output.string()};
    arguments.insert(arguments.end(), extra_arguments.begin(),
                     extra_arguments.end());
    arguments.push_back(url);

    const int exit_code =
        run_curl_process(curl_executable, arguments, log_path, cancellation);
    if (exit_code != 0) {
        throw std::runtime_error(
            "curl request failed (exit code " + std::to_string(exit_code) + ")");
    }
    std::ifstream input(temp_output, std::ios::binary);
    if (!input) throw std::runtime_error("curl output could not be read");
    std::ostringstream buffer;
    buffer << input.rdbuf();
    return buffer.str();
}

std::string first_string(const JsonValue& object, const std::string& key) {
    const auto* found = object.optional(key);
    if (found == nullptr || found->type() != JsonValue::Type::string) return {};
    return found->as_string();
}

}  // namespace

// ---------------------------------------------------------------------
// ReliabilityTierStore
// ---------------------------------------------------------------------

unsigned int ReliabilityTierStore::default_score() noexcept { return 40U; }

ReliabilityTierStore::ReliabilityTierStore(RecordStore& records)
    : records_(&records) {
    restore();
    if (tiers_.empty()) seed_defaults();
}

void ReliabilityTierStore::restore() {
    for (const auto& item : records_->list("research_reliability_tiers")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 3U) {
            throw std::runtime_error(
                "persisted reliability tier field count is wrong");
        }
        ReliabilityTier tier;
        tier.id = item.first;
        tier.domain_suffix = fields[0];
        tier.score = static_cast<unsigned int>(std::stoul(fields[1]));
        tier.label = fields[2];
        tiers_[tier.id] = tier;
    }
}

void ReliabilityTierStore::seed_defaults() {
    // A starting point, not an exhaustive authority list: government,
    // education, and a handful of widely recognized reference/standards
    // publishers score high; general well-known encyclopedic/news sources
    // score mid; everything else falls back to default_score(). The
    // administrator can add, edit, or remove rows freely from here.
    const std::vector<std::tuple<std::string, unsigned int, std::string>>
        defaults{
            {".gov", 95U, "Government (.gov)"},
            {".mil", 95U, "Military (.mil)"},
            {".edu", 90U, "Education (.edu)"},
            {".int", 88U, "International treaty organization (.int)"},
            {"who.int", 92U, "World Health Organization"},
            {"nist.gov", 95U, "NIST"},
            {"w3.org", 92U, "W3C standards"},
            {"ietf.org", 92U, "IETF standards"},
            {"iso.org", 90U, "ISO standards"},
            {"wikipedia.org", 70U, "Wikipedia (community-edited)"},
            {"reuters.com", 82U, "Reuters"},
            {"apnews.com", 82U, "Associated Press"},
            {"bbc.co.uk", 80U, "BBC"},
            {"bbc.com", 80U, "BBC"},
        };
    for (const auto& [suffix, score, label] : defaults) {
        upsert(suffix, score, label);
    }
}

ReliabilityTier ReliabilityTierStore::upsert(const std::string& domain_suffix,
                                             unsigned int score,
                                             const std::string& label) {
    if (domain_suffix.empty() || domain_suffix.size() > 253U) {
        throw std::invalid_argument("reliability tier domain suffix is invalid");
    }
    if (score > 100U) throw std::invalid_argument("reliability score must be 0-100");
    const std::lock_guard<std::mutex> lock(mutex_);
    ReliabilityTier tier;
    // A second upsert() of the same suffix replaces the existing row
    // instead of creating a duplicate rule.
    for (const auto& item : tiers_) {
        if (item.second.domain_suffix == lowercase(domain_suffix)) {
            tier.id = item.first;
            break;
        }
    }
    if (tier.id.empty()) tier.id = random_id();
    tier.domain_suffix = lowercase(domain_suffix);
    tier.score = score;
    tier.label = label;
    tiers_[tier.id] = tier;
    records_->put("research_reliability_tiers", tier.id,
                  pack({tier.domain_suffix, std::to_string(tier.score),
                        tier.label}));
    return tier;
}

bool ReliabilityTierStore::remove(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (tiers_.erase(id) == 0U) return false;
    records_->erase("research_reliability_tiers", id);
    return true;
}

std::vector<ReliabilityTier> ReliabilityTierStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ReliabilityTier> result;
    result.reserve(tiers_.size());
    for (const auto& item : tiers_) result.push_back(item.second);
    std::sort(result.begin(), result.end(),
             [](const ReliabilityTier& left, const ReliabilityTier& right) {
                 return left.domain_suffix < right.domain_suffix;
             });
    return result;
}

unsigned int ReliabilityTierStore::score_for_host(
    const std::string& host) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto candidate = lowercase(host);
    std::size_t best_length = 0U;
    unsigned int best_score = default_score();
    for (const auto& item : tiers_) {
        const auto& suffix = item.second.domain_suffix;
        if (suffix.size() > candidate.size()) continue;
        if (candidate.compare(candidate.size() - suffix.size(), suffix.size(),
                              suffix) != 0) {
            continue;
        }
        // A suffix match must land on a label boundary (either an exact
        // match or immediately preceded by '.'), so "iso.org" doesn't
        // accidentally match "notiso.org".
        const auto boundary_index = candidate.size() - suffix.size();
        if (boundary_index != 0U && candidate[boundary_index - 1U] != '.' &&
            suffix.front() != '.') {
            continue;
        }
        if (suffix.size() > best_length) {
            best_length = suffix.size();
            best_score = item.second.score;
        }
    }
    return best_score;
}

// ---------------------------------------------------------------------
// html_extract_text
// ---------------------------------------------------------------------

ExtractedPageText html_extract_text(const std::string& html,
                                    std::size_t maximum_text_bytes) {
    ExtractedPageText result;
    std::string text;
    text.reserve(std::min(html.size(), maximum_text_bytes));

    std::size_t position = 0U;
    while (position < html.size()) {
        const auto tag_start = html.find('<', position);
        if (tag_start == std::string::npos) {
            text += html.substr(position);
            break;
        }
        text += html.substr(position, tag_start - position);

        const auto tag_end = html.find('>', tag_start);
        if (tag_end == std::string::npos) break;
        const auto tag_body = html.substr(tag_start + 1U, tag_end - tag_start - 1U);
        const auto tag_lower = lowercase(tag_body);

        if (tag_lower.rfind("title", 0U) == 0U && result.title.empty()) {
            const auto close = html.find("</title", tag_end);
            if (close != std::string::npos) {
                result.title = html.substr(tag_end + 1U, close - tag_end - 1U);
            }
        } else if (tag_lower.rfind("meta", 0U) == 0U &&
                  tag_lower.find("description") != std::string::npos &&
                  result.description.empty()) {
            const auto content_key = tag_lower.find("content=");
            if (content_key != std::string::npos) {
                auto value_start = tag_body.find('=', content_key) + 1U;
                if (value_start < tag_body.size() &&
                    (tag_body[value_start] == '"' ||
                     tag_body[value_start] == '\'')) {
                    const char quote = tag_body[value_start];
                    const auto value_end = tag_body.find(quote, value_start + 1U);
                    if (value_end != std::string::npos) {
                        result.description =
                            tag_body.substr(value_start + 1U,
                                            value_end - value_start - 1U);
                    }
                }
            }
        } else if (tag_lower.rfind("script", 0U) == 0U ||
                  tag_lower.rfind("style", 0U) == 0U) {
            const auto closing_tag =
                tag_lower.rfind("script", 0U) == 0U ? "</script" : "</style";
            const auto close = html.find(closing_tag, tag_end);
            position = (close == std::string::npos) ? html.size()
                                                     : html.find('>', close) + 1U;
            continue;
        }
        // Block-level tags become a paragraph break so extracted text isn't
        // one giant run-on line.
        static const std::vector<std::string> block_tags{
            "p", "br", "div", "li", "h1", "h2", "h3", "h4", "h5", "h6", "tr"};
        for (const auto& block_tag : block_tags) {
            if (tag_lower.rfind(block_tag, 0U) == 0U &&
                (tag_lower.size() == block_tag.size() ||
                 !std::isalnum(static_cast<unsigned char>(
                     tag_lower[block_tag.size()])))) {
                text += '\n';
                break;
            }
        }
        position = tag_end + 1U;
        if (text.size() >= maximum_text_bytes) break;
    }

    // Decode the handful of entities search/reference pages commonly use.
    static const std::vector<std::pair<std::string, std::string>> entities{
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&apos;", "'"}, {"&nbsp;", " "}};
    for (const auto& [entity, replacement] : entities) {
        std::size_t found = 0U;
        while ((found = text.find(entity, found)) != std::string::npos) {
            text.replace(found, entity.size(), replacement);
            found += replacement.size();
        }
    }

    // Collapse repeated whitespace left over from tag stripping.
    std::string collapsed;
    collapsed.reserve(text.size());
    bool previous_was_space = false;
    for (const char character : text) {
        const bool is_space = std::isspace(static_cast<unsigned char>(character));
        if (is_space) {
            if (!previous_was_space) collapsed += (character == '\n') ? '\n' : ' ';
        } else {
            collapsed += character;
        }
        previous_was_space = is_space;
    }
    if (collapsed.size() > maximum_text_bytes) collapsed.resize(maximum_text_bytes);
    result.text = collapsed;
    if (result.title.size() > 512U) result.title.resize(512U);
    if (result.description.size() > 1024U) result.description.resize(1024U);
    return result;
}

// ---------------------------------------------------------------------
// Search providers
// ---------------------------------------------------------------------

std::vector<RawSearchHit> google_custom_search(
    const std::string& query, const std::string& api_key,
    const std::string& engine_id, const std::filesystem::path& curl_executable,
    const std::filesystem::path& log_path, std::uint32_t timeout_seconds,
    std::uint32_t max_results, const std::atomic_bool& cancellation) {
    if (api_key.empty() || engine_id.empty()) {
        throw std::runtime_error(
            "Google search requires an API key and a Programmable Search "
            "Engine id");
    }
    const std::string url =
        "https://www.googleapis.com/customsearch/v1?key=" + url_encode(api_key) +
        "&cx=" + url_encode(engine_id) + "&q=" + url_encode(query) +
        "&num=" + std::to_string(std::min<std::uint32_t>(max_results, 10U));
    const auto body = curl_capture(curl_executable, url, {}, log_path,
                                   timeout_seconds, 4U * 1024U * 1024U,
                                   cancellation);
    const auto root = parse_json(body, 4U * 1024U * 1024U);
    std::vector<RawSearchHit> hits;
    const auto* items = root.optional("items");
    if (items == nullptr) return hits;
    for (const auto& item : items->as_array()) {
        RawSearchHit hit;
        hit.engine = "google";
        hit.title = first_string(item, "title");
        hit.url = first_string(item, "link");
        hit.snippet = first_string(item, "snippet");
        if (!hit.url.empty()) hits.push_back(std::move(hit));
        if (hits.size() >= max_results) break;
    }
    return hits;
}

std::vector<RawSearchHit> bing_web_search(
    const std::string& query, const std::string& api_key,
    const std::filesystem::path& curl_executable,
    const std::filesystem::path& log_path, std::uint32_t timeout_seconds,
    std::uint32_t max_results, const std::atomic_bool& cancellation) {
    if (api_key.empty()) {
        throw std::runtime_error("Bing search requires an API key");
    }
    const std::string url =
        "https://api.bing.microsoft.com/v7.0/search?q=" + url_encode(query) +
        "&count=" + std::to_string(std::min<std::uint32_t>(max_results, 10U));
    const std::vector<std::string> headers{
        "-H", "Ocp-Apim-Subscription-Key: " + api_key};
    const auto body = curl_capture(curl_executable, url, headers, log_path,
                                   timeout_seconds, 4U * 1024U * 1024U,
                                   cancellation);
    const auto root = parse_json(body, 4U * 1024U * 1024U);
    std::vector<RawSearchHit> hits;
    const auto* web_pages = root.optional("webPages");
    if (web_pages == nullptr) return hits;
    const auto* values = web_pages->optional("value");
    if (values == nullptr) return hits;
    for (const auto& item : values->as_array()) {
        RawSearchHit hit;
        hit.engine = "bing";
        hit.title = first_string(item, "name");
        hit.url = first_string(item, "url");
        hit.snippet = first_string(item, "snippet");
        if (!hit.url.empty()) hits.push_back(std::move(hit));
        if (hits.size() >= max_results) break;
    }
    return hits;
}

ExtractedPageText fetch_page_text(const std::string& url,
                                  const std::filesystem::path& curl_executable,
                                  const std::filesystem::path& log_path,
                                  std::uint32_t timeout_seconds,
                                  std::uint64_t max_bytes,
                                  const std::atomic_bool& cancellation) {
    const auto body = curl_capture(curl_executable, url, {}, log_path,
                                   timeout_seconds, max_bytes, cancellation);
    return html_extract_text(body);
}

// ---------------------------------------------------------------------
// ResearchRunStore
// ---------------------------------------------------------------------

ResearchRunStore::ResearchRunStore(RecordStore& records) : records_(&records) {
    restore();
}

namespace {
std::string pack_finding(const ResearchFinding& finding) {
    return pack({finding.url, finding.title, finding.engine,
                std::to_string(finding.reliability_score),
                finding.fetched ? "1" : "0", finding.relevant ? "1" : "0",
                finding.ingested ? "1" : "0", finding.knowledge_document_id,
                finding.snippet, finding.error});
}

ResearchFinding unpack_finding(const std::string& packed) {
    const auto fields = unpack(packed);
    if (fields.size() != 10U) {
        throw std::runtime_error("persisted research finding field count is wrong");
    }
    ResearchFinding finding;
    finding.url = fields[0];
    finding.title = fields[1];
    finding.engine = fields[2];
    finding.reliability_score = static_cast<unsigned int>(std::stoul(fields[3]));
    finding.fetched = fields[4] == "1";
    finding.relevant = fields[5] == "1";
    finding.ingested = fields[6] == "1";
    finding.knowledge_document_id = fields[7];
    finding.snippet = fields[8];
    finding.error = fields[9];
    return finding;
}
}  // namespace

void ResearchRunStore::restore() {
    for (const auto& item : records_->list("research_runs")) {
        const auto fields = unpack(item.second);
        if (fields.size() < 6U) {
            throw std::runtime_error("persisted research run field count is wrong");
        }
        ResearchRun run;
        run.id = item.first;
        run.query = fields[0];
        run.requested_by = fields[1];
        run.subject_id = fields[2];
        run.vector_store_id = fields[3];
        run.summary = fields[4];
        run.started_at_epoch_seconds = std::stoull(fields[5]);
        run.completed_at_epoch_seconds = fields.size() > 6U ? std::stoull(fields[6]) : 0U;
        for (std::size_t index = 7U; index < fields.size(); ++index) {
            run.findings.push_back(unpack_finding(fields[index]));
        }
        runs_[run.id] = run;
    }
}

void ResearchRunStore::put(const ResearchRun& run) {
    const std::lock_guard<std::mutex> lock(mutex_);
    runs_[run.id] = run;
    std::vector<std::string> fields{
        run.query, run.requested_by, run.subject_id, run.vector_store_id,
        run.summary, std::to_string(run.started_at_epoch_seconds),
        std::to_string(run.completed_at_epoch_seconds)};
    for (const auto& finding : run.findings) fields.push_back(pack_finding(finding));
    records_->put("research_runs", run.id, pack(fields));
}

std::optional<ResearchRun> ResearchRunStore::find(const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = runs_.find(id);
    return found == runs_.end() ? std::nullopt : std::optional<ResearchRun>(found->second);
}

std::vector<ResearchRun> ResearchRunStore::list() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ResearchRun> result;
    result.reserve(runs_.size());
    for (const auto& item : runs_) result.push_back(item.second);
    std::sort(result.begin(), result.end(),
             [](const ResearchRun& left, const ResearchRun& right) {
                 return left.started_at_epoch_seconds > right.started_at_epoch_seconds;
             });
    return result;
}

// ---------------------------------------------------------------------
// ResearchEngine
// ---------------------------------------------------------------------

ResearchEngine::ResearchEngine(AppConfig configuration,
                               std::filesystem::path curl_executable,
                               std::filesystem::path log_root,
                               std::string google_api_key,
                               std::string bing_api_key,
                               ReliabilityTierStore& tiers,
                               KnowledgeIndexStore& knowledge,
                               const KnowledgeEmbeddingFunction& vectorize)
    : configuration_(std::move(configuration)),
      curl_executable_(std::move(curl_executable)),
      log_root_(std::move(log_root)),
      google_api_key_(std::move(google_api_key)),
      bing_api_key_(std::move(bing_api_key)),
      tiers_(&tiers),
      knowledge_(&knowledge),
      vectorize_(vectorize) {}

namespace {
// A deliberately simple, explainable relevance test: how many of the
// query's significant (3+ letter) words appear in the page's title or
// text. Not a model-judgment pass -- see the Phase 103 design comment in
// masterai.hpp for why that's an intentional, documented limit of this
// scoped-down phase.
bool looks_relevant(const std::string& query, const ExtractedPageText& page) {
    std::istringstream words(lowercase(query));
    std::string word;
    const auto haystack = lowercase(page.title + " " + page.description + " " + page.text);
    std::size_t matched = 0U;
    std::size_t total = 0U;
    while (words >> word) {
        if (word.size() < 3U) continue;
        ++total;
        if (haystack.find(word) != std::string::npos) ++matched;
    }
    if (total == 0U) return true;
    return matched * 2U >= total;  // at least half the significant words appear
}
}  // namespace

ResearchRun ResearchEngine::run(const std::string& requested_by,
                                const std::string& query,
                                const std::string& subject_id,
                                const std::string& vector_store_id,
                                const std::atomic_bool& cancellation) const {
    if (query.empty() || query.size() > 512U) {
        throw std::invalid_argument("research query is invalid");
    }
    if (!configuration_.research_enabled) {
        throw std::runtime_error("web research is not enabled in configuration");
    }
    const auto effective_subject_id =
        subject_id.empty() ? configuration_.research_default_subject_id : subject_id;
    const auto effective_vector_store_id =
        vector_store_id.empty() ? configuration_.research_default_vector_store_id
                                : vector_store_id;
    if (effective_subject_id.empty() || effective_vector_store_id.empty()) {
        throw std::invalid_argument(
            "research requires a subject and vector store, either supplied "
            "with the request or configured as research defaults");
    }

    ResearchRun run;
    run.id = random_id();
    run.query = query;
    run.requested_by = requested_by;
    run.subject_id = effective_subject_id;
    run.vector_store_id = effective_vector_store_id;
    run.started_at_epoch_seconds = epoch_seconds();

    const auto log_path = log_root_ / "research.log";
    std::vector<RawSearchHit> hits;
    std::vector<std::string> provider_errors;
    if (configuration_.research_google_enabled) {
        try {
            const auto google_hits = google_custom_search(
                query, google_api_key_, configuration_.research_google_engine_id,
                curl_executable_, log_path,
                configuration_.research_fetch_timeout_seconds,
                configuration_.research_max_results_per_query, cancellation);
            hits.insert(hits.end(), google_hits.begin(), google_hits.end());
        } catch (const std::exception& error) {
            provider_errors.push_back(std::string("google: ") + error.what());
        }
    }
    if (configuration_.research_bing_enabled) {
        try {
            const auto bing_hits = bing_web_search(
                query, bing_api_key_, curl_executable_, log_path,
                configuration_.research_fetch_timeout_seconds,
                configuration_.research_max_results_per_query, cancellation);
            hits.insert(hits.end(), bing_hits.begin(), bing_hits.end());
        } catch (const std::exception& error) {
            provider_errors.push_back(std::string("bing: ") + error.what());
        }
    }
    if (hits.empty() && !provider_errors.empty()) {
        std::string joined;
        for (const auto& message : provider_errors) {
            if (!joined.empty()) joined += "; ";
            joined += message;
        }
        throw std::runtime_error("every enabled search provider failed: " + joined);
    }

    struct ScoredHit {
        RawSearchHit hit;
        unsigned int score;
    };
    std::vector<ScoredHit> scored;
    scored.reserve(hits.size());
    for (auto& hit : hits) {
        const auto score = tiers_->score_for_host(host_from_url(hit.url));
        scored.push_back({std::move(hit), score});
    }
    std::sort(scored.begin(), scored.end(),
             [](const ScoredHit& left, const ScoredHit& right) {
                 return left.score > right.score;
             });

    const auto threshold = configuration_.research_reliability_threshold_percent;
    std::size_t fetched_count = 0U;
    std::size_t ingested_count = 0U;
    for (const auto& item : scored) {
        ResearchFinding finding;
        finding.url = item.hit.url;
        finding.title = item.hit.title;
        finding.engine = item.hit.engine;
        finding.reliability_score = item.score;
        finding.snippet = item.hit.snippet;

        if (item.score < threshold) {
            finding.error = "below the configured reliability threshold";
            run.findings.push_back(std::move(finding));
            continue;
        }
        if (fetched_count >= configuration_.research_max_pages_to_fetch) {
            finding.error = "skipped: research_max_pages_to_fetch reached";
            run.findings.push_back(std::move(finding));
            continue;
        }
        // Checked here (not just inside fetch_page_text()'s own curl
        // subprocess) so a shutdown that lands between two page fetches
        // doesn't start another one it would then have to wait out.
        if (cancellation.load()) {
            finding.error = "skipped: research run was cancelled";
            run.findings.push_back(std::move(finding));
            continue;
        }

        try {
            const auto page = fetch_page_text(
                item.hit.url, curl_executable_, log_path,
                configuration_.research_fetch_timeout_seconds,
                4U * 1024U * 1024U, cancellation);
            finding.fetched = true;
            ++fetched_count;
            finding.relevant = looks_relevant(query, page);
            if (finding.relevant) {
                auto document_title =
                    !page.title.empty() ? page.title : finding.title;
                if (document_title.empty()) document_title = item.hit.url;
                // KnowledgeIndexStore::ingest() rejects a file name over 260
                // bytes; a page <title> or a long URL can easily exceed
                // that, so the base name is bounded before ".txt" is added.
                if (document_title.size() > 250U) document_title.resize(250U);
                const auto document = knowledge_->ingest(
                    requested_by, effective_subject_id, effective_vector_store_id,
                    document_title + ".txt", "text/plain", page.text,
                    "authored_hashing_vectorizer_v1", vectorize_);
                finding.ingested = true;
                finding.knowledge_document_id = document.id;
                ++ingested_count;
            } else {
                finding.error = "fetched but did not look relevant to the query";
            }
        } catch (const std::exception& error) {
            finding.error = error.what();
        }
        run.findings.push_back(std::move(finding));
    }

    run.completed_at_epoch_seconds = epoch_seconds();
    run.summary = "Searched " +
        std::to_string((configuration_.research_google_enabled ? 1U : 0U) +
                       (configuration_.research_bing_enabled ? 1U : 0U)) +
        " engine(s), found " + std::to_string(scored.size()) +
        " result(s), " + std::to_string(fetched_count) +
        " at or above the " + std::to_string(threshold) +
        "% reliability threshold were fetched, and " +
        std::to_string(ingested_count) + " relevant page(s) were saved "
        "to the knowledge base.";
    return run;
}

// ---------------------------------------------------------------------
// JSON output
// ---------------------------------------------------------------------

std::string reliability_tier_json(const ReliabilityTier& tier) {
    return "{\"id\":\"" + json_escape(tier.id) + "\",\"domainSuffix\":\"" +
          json_escape(tier.domain_suffix) + "\",\"score\":" +
          std::to_string(tier.score) + ",\"label\":\"" +
          json_escape(tier.label) + "\"}";
}

std::string reliability_tiers_json(const std::vector<ReliabilityTier>& tiers) {
    std::string result = "[";
    for (std::size_t index = 0; index < tiers.size(); ++index) {
        if (index > 0U) result += ",";
        result += reliability_tier_json(tiers[index]);
    }
    return result + "]";
}

namespace {
std::string research_finding_json(const ResearchFinding& finding) {
    return "{\"url\":\"" + json_escape(finding.url) + "\",\"title\":\"" +
          json_escape(finding.title) + "\",\"engine\":\"" +
          json_escape(finding.engine) + "\",\"reliabilityScore\":" +
          std::to_string(finding.reliability_score) + ",\"fetched\":" +
          (finding.fetched ? "true" : "false") + ",\"relevant\":" +
          (finding.relevant ? "true" : "false") + ",\"ingested\":" +
          (finding.ingested ? "true" : "false") +
          ",\"knowledgeDocumentId\":\"" +
          json_escape(finding.knowledge_document_id) + "\",\"snippet\":\"" +
          json_escape(finding.snippet) + "\",\"error\":\"" +
          json_escape(finding.error) + "\"}";
}
}  // namespace

std::string research_run_json(const ResearchRun& run) {
    std::string findings = "[";
    for (std::size_t index = 0; index < run.findings.size(); ++index) {
        if (index > 0U) findings += ",";
        findings += research_finding_json(run.findings[index]);
    }
    findings += "]";
    return "{\"id\":\"" + json_escape(run.id) + "\",\"query\":\"" +
          json_escape(run.query) + "\",\"requestedBy\":\"" +
          json_escape(run.requested_by) + "\",\"subjectId\":\"" +
          json_escape(run.subject_id) + "\",\"vectorStoreId\":\"" +
          json_escape(run.vector_store_id) + "\",\"summary\":\"" +
          json_escape(run.summary) + "\",\"startedAtEpochSeconds\":" +
          std::to_string(run.started_at_epoch_seconds) +
          ",\"completedAtEpochSeconds\":" +
          std::to_string(run.completed_at_epoch_seconds) +
          ",\"findings\":" + findings + "}";
}

std::string research_runs_json(const std::vector<ResearchRun>& runs) {
    std::string result = "[";
    for (std::size_t index = 0; index < runs.size(); ++index) {
        if (index > 0U) result += ",";
        result += research_run_json(runs[index]);
    }
    return result + "]";
}

}  // namespace masterai
