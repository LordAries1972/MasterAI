// Phases 58-61: bounded knowledge ingestion, durable authored or learned
// embeddings, and evidence-bearing retrieval for administrator RAG testing.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <string_view>

namespace masterai {
namespace {

constexpr std::size_t embedding_dimensions = 128U;
constexpr std::size_t maximum_chunks_per_document = 4096U;
constexpr std::size_t chunk_target_bytes = 1200U;
constexpr std::size_t chunk_overlap_bytes = 200U;

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
}

std::string random_id() {
    const auto random = secure_random(16U);
    static constexpr char digits[] = "0123456789abcdef";
    std::string id(random.size() * 2U, '0');
    for (std::size_t index = 0; index < random.size(); ++index) {
        id[index * 2U] = digits[(random[index] >> 4U) & 0x0fU];
        id[index * 2U + 1U] = digits[random[index] & 0x0fU];
    }
    return id;
}

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
            throw std::runtime_error("persisted knowledge record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted knowledge record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
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

std::string number_field(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g",
                  std::isfinite(value) ? value : 0.0);
    return buffer;
}

std::string json_number(const double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g",
                  std::isfinite(value) ? value : 0.0);
    return buffer;
}

std::uint64_t fnv1a(const std::string& token) {
    std::uint64_t hash = 1469598103934665603ULL;
    for (const unsigned char character : token) {
        hash ^= static_cast<std::uint64_t>(character);
        hash *= 1099511628211ULL;
    }
    return hash;
}

std::vector<std::string> tokens(const std::string& text) {
    std::vector<std::string> result;
    std::string token;
    for (const unsigned char character : text) {
        if (std::isalnum(character) != 0 || character == '_') {
            token += static_cast<char>(std::tolower(character));
        } else if (!token.empty()) {
            if (token.size() > 1U) result.push_back(token);
            token.clear();
        }
    }
    if (token.size() > 1U) result.push_back(token);
    return result;
}

std::vector<double> authored_hash_embedding_impl(const std::string& text) {
    std::vector<double> result(embedding_dimensions, 0.0);
    const auto words = tokens(text);
    std::map<std::string, std::size_t> counts;
    for (const auto& word : words) ++counts[word];
    for (const auto& item : counts) {
        const auto hash = fnv1a(item.first);
        const auto slot = static_cast<std::size_t>(hash % embedding_dimensions);
        const double sign = (hash & (1ULL << 63U)) != 0U ? -1.0 : 1.0;
        result[slot] += sign * (1.0 + std::log(static_cast<double>(item.second)));
    }
    double squared = 0.0;
    for (const auto value : result) squared += value * value;
    if (squared > 0.0) {
        const double length = std::sqrt(squared);
        for (auto& value : result) value /= length;
    }
    return result;
}

// Treat an embedding backend as untrusted input even though llama.cpp runs
// on loopback: dimensions, finite values, and non-zero magnitude are checked
// before normalization so malformed backend output is never persisted.
std::vector<double> validate_and_normalize_embedding(
    std::vector<double> values) {
    if (values.empty() || values.size() > 8192U) {
        throw std::invalid_argument(
            "embedding dimensions must be between 1 and 8192");
    }
    double squared = 0.0;
    for (const auto value : values) {
        if (!std::isfinite(value)) {
            throw std::invalid_argument("embedding contains a non-finite value");
        }
        squared += value * value;
    }
    if (!std::isfinite(squared) || squared <= 0.0) {
        throw std::invalid_argument("embedding has zero or invalid magnitude");
    }
    const double length = std::sqrt(squared);
    for (auto& value : values) value /= length;
    return values;
}

double cosine(const std::vector<double>& left,
              const std::vector<double>& right) {
    if (left.size() != right.size()) return 0.0;
    double score = 0.0;
    for (std::size_t index = 0; index < left.size(); ++index) {
        score += left[index] * right[index];
    }
    return std::max(0.0, score);
}

double keyword_overlap(const std::set<std::string>& query_words,
                       const std::string& text) {
    if (query_words.empty()) return 0.0;
    const auto chunk_words_vector = tokens(text);
    const std::set<std::string> chunk_words(chunk_words_vector.begin(),
                                            chunk_words_vector.end());
    std::size_t matches = 0U;
    for (const auto& word : query_words) {
        if (chunk_words.count(word) != 0U) ++matches;
    }
    return static_cast<double>(matches) /
           static_cast<double>(query_words.size());
}

std::vector<std::string> make_chunks(const std::string& content) {
    std::vector<std::string> result;
    std::size_t begin = 0U;
    while (begin < content.size()) {
        std::size_t end = std::min(content.size(), begin + chunk_target_bytes);
        if (end < content.size()) {
            const auto paragraph = content.rfind("\n\n", end);
            const auto newline = content.rfind('\n', end);
            const auto space = content.rfind(' ', end);
            std::size_t boundary = std::string::npos;
            for (const auto candidate : {paragraph, newline, space}) {
                if (candidate != std::string::npos &&
                    candidate > begin + chunk_target_bytes / 2U) {
                    boundary = candidate;
                    break;
                }
            }
            if (boundary != std::string::npos) end = boundary;
        }
        std::size_t trimmed_begin = begin;
        std::size_t trimmed_end = end;
        while (trimmed_begin < trimmed_end &&
               std::isspace(static_cast<unsigned char>(content[trimmed_begin])) != 0) {
            ++trimmed_begin;
        }
        while (trimmed_end > trimmed_begin &&
               std::isspace(static_cast<unsigned char>(content[trimmed_end - 1U])) != 0) {
            --trimmed_end;
        }
        if (trimmed_end > trimmed_begin) {
            result.push_back(content.substr(trimmed_begin,
                                            trimmed_end - trimmed_begin));
        }
        if (end >= content.size()) break;
        const std::size_t advance = end > begin + chunk_overlap_bytes
                                        ? end - chunk_overlap_bytes : end;
        begin = advance > begin ? advance : end;
        if (result.size() >= maximum_chunks_per_document) {
            throw std::runtime_error("knowledge document produces too many chunks");
        }
    }
    return result;
}

bool supported_media_type(const std::string& media_type) {
    return media_type == "text/plain" || media_type == "text/markdown" ||
           media_type == "text/csv" || media_type == "application/json" ||
           media_type == "application/x-ndjson" || media_type.empty();
}

// Reads a string-typed field out of a JSON object, leaving `out` untouched
// (and returning false) if the key is absent or not a string -- callers use
// the false case to recognize a record that doesn't match one of the
// Scraper Project's fixed shapes below, rather than throwing.
bool json_string_field(const JsonValue::Object& object, const std::string& key,
                       std::string& out) {
    const auto it = object.find(key);
    if (it == object.end() || it->second.type() != JsonValue::Type::string) {
        return false;
    }
    out = it->second.as_string();
    return true;
}

// Renders one record of the Scraper Project's "MasterAI Web Dataset
// Builder" export (instruction/input/output, conversation messages, or
// raw document text, each with an optional metadata.source_title/
// source_url) as plain text worth chunking. Returns an empty string for
// any object that matches none of the three shapes, so the caller can
// abandon the whole-file rewrite and fall back to raw byte chunking rather
// than silently drop content it doesn't recognize.
std::string flatten_scraper_record(const JsonValue& record) {
    if (!record.is_object()) return {};
    const auto& object = record.as_object();
    // Branch on which content field actually holds text, not merely on key
    // presence: a Parquet/CSV-derived row (parquet_bytes_to_json) carries
    // every column from the superset schema on every row, blank for
    // whichever record type that row isn't, so "instruction" can be
    // present-but-empty on a document-type row whose real content is in
    // "text".
    std::string instruction, output, body;
    json_string_field(object, "instruction", instruction);
    json_string_field(object, "output", output);
    json_string_field(object, "text", body);
    const auto messages = object.find("messages");
    const bool has_messages = messages != object.end() &&
        messages->second.type() == JsonValue::Type::array &&
        !messages->second.as_array().empty();
    std::string text;
    if (has_messages) {
        for (const auto& message : messages->second.as_array()) {
            if (!message.is_object()) return {};
            const auto& fields = message.as_object();
            std::string role, content;
            if (!json_string_field(fields, "role", role) ||
                !json_string_field(fields, "content", content)) {
                return {};
            }
            text += role + ": " + content + "\n";
        }
    } else if (!instruction.empty()) {
        std::string input;
        json_string_field(object, "input", input);
        text = "Instruction: " + instruction + "\n";
        if (!input.empty()) text += "Input: " + input + "\n";
        text += "Output: " + output + "\n";
    } else if (!body.empty()) {
        text = body + "\n";
    } else {
        return {};
    }
    if (const auto metadata = object.find("metadata");
        metadata != object.end() && metadata->second.is_object()) {
        const auto& fields = metadata->second.as_object();
        std::string title, url, source;
        if (json_string_field(fields, "source_title", title)) source = title;
        if (json_string_field(fields, "source_url", url)) {
            source += source.empty() ? url : " (" + url + ")";
        }
        if (!source.empty()) text = "Source: " + source + "\n" + text;
    }
    return text;
}

// Detects a Scraper Project JSON or JSONL export and rewrites it into
// clean, human-readable text before it reaches make_chunks() -- otherwise
// the 1200-byte sliding window below slices straight through
// `{"instruction":...}` syntax, producing garbled chunks and meaningless
// embeddings for what is otherwise clean instruction/conversation/document
// data (see docs/HowToUse-MachineLearning.md's Scraper import lesson).
// Returns an empty string when the content doesn't match any recognized
// shape, telling the caller to chunk the original bytes unchanged.
std::string flatten_scraper_records(const std::string& content,
                                    const std::size_t max_json_bytes) {
    std::vector<JsonValue> records;
    // Try JSONL first: one JSON object per non-blank line.
    std::size_t position = 0U;
    bool jsonl_failed = false;
    while (position <= content.size()) {
        const auto newline = content.find('\n', position);
        const auto line_end = newline == std::string::npos ? content.size() : newline;
        std::string line = content.substr(position, line_end - position);
        std::size_t begin = 0U;
        while (begin < line.size() &&
               std::isspace(static_cast<unsigned char>(line[begin])) != 0) {
            ++begin;
        }
        std::size_t end = line.size();
        while (end > begin &&
               std::isspace(static_cast<unsigned char>(line[end - 1U])) != 0) {
            --end;
        }
        const std::string trimmed = line.substr(begin, end - begin);
        if (!trimmed.empty()) {
            try {
                records.push_back(parse_json(trimmed, max_json_bytes));
            } catch (const std::exception&) {
                jsonl_failed = true;
                break;
            }
        }
        if (newline == std::string::npos) break;
        position = newline + 1U;
    }
    if (jsonl_failed || records.empty()) {
        records.clear();
        try {
            auto parsed = parse_json(content, max_json_bytes);
            if (parsed.type() != JsonValue::Type::array) return {};
            for (auto& item : parsed.as_array()) records.push_back(item);
        } catch (const std::exception&) {
            return {};
        }
    }
    if (records.empty()) return {};
    std::string flattened;
    for (const auto& record : records) {
        const auto piece = flatten_scraper_record(record);
        if (piece.empty()) return {};
        if (!flattened.empty()) flattened += "\n\n---\n\n";
        flattened += piece;
    }
    return flattened;
}

}  // namespace

// Browsers commonly report unrecognized extensions as "application/
// octet-stream" or omit a media type entirely, so the .parquet extension is
// treated as authoritative alongside the registered media type. Shared by
// KnowledgeIndexStore::ingest() and the knowledge-documents REST handler
// (server.cpp), which must decode base64 before content reaches ingest().
bool is_parquet_knowledge_upload(const std::string& media_type,
                                 const std::string& file_name) {
    static constexpr std::string_view extension = ".parquet";
    return media_type == "application/vnd.apache.parquet" ||
           ((media_type.empty() || media_type == "application/octet-stream") &&
            file_name.size() >= extension.size() &&
            file_name.compare(file_name.size() - extension.size(),
                              extension.size(), extension) == 0);
}

std::vector<double> authored_hash_embedding(const std::string& text) {
    return authored_hash_embedding_impl(text);
}

KnowledgeIndexStore::KnowledgeIndexStore(RecordStore& records)
    : records_(&records) {
    restore();
}

KnowledgeIndexStore::KnowledgeIndexStore(
    RecordStore& records, std::filesystem::path parquet_helper_executable,
    std::uint64_t maximum_document_bytes)
    : records_(&records),
      parquet_helper_executable_(std::move(parquet_helper_executable)),
      maximum_document_bytes_(maximum_document_bytes) {
    restore();
}

void KnowledgeIndexStore::restore() {
    for (const auto& item : records_->list("ml_knowledge_documents")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 9U) {
            throw std::runtime_error("persisted knowledge document field count is wrong");
        }
        KnowledgeDocument document;
        document.id = item.first;
        document.subject_id = fields[0];
        document.vector_store_id = fields[1];
        document.file_name = fields[2];
        document.media_type = fields[3];
        document.sha256 = fields[4];
        document.owner_id = fields[5];
        document.byte_count = static_cast<std::size_t>(std::stoull(fields[6]));
        document.chunk_count = static_cast<std::size_t>(std::stoull(fields[7]));
        document.ingested_at_epoch_seconds = std::stoull(fields[8]);
        documents_[document.id] = document;
    }
    for (const auto& item : records_->list("ml_knowledge_chunks")) {
        const auto fields = unpack(item.second);
        const bool legacy = fields.size() == 7U + embedding_dimensions &&
                            fields[6] == std::to_string(embedding_dimensions);
        if ((!legacy && fields.size() < 9U)) {
            throw std::runtime_error("persisted knowledge chunk field count is wrong");
        }
        KnowledgeChunk chunk;
        chunk.id = item.first;
        chunk.document_id = fields[0];
        chunk.subject_id = fields[1];
        chunk.vector_store_id = fields[2];
        chunk.file_name = fields[3];
        chunk.chunk_index = static_cast<std::size_t>(std::stoull(fields[4]));
        chunk.text = fields[5];
        const std::size_t method_field = legacy ? 7U : 6U;
        const std::size_t dimensions_field = legacy ? 6U : 7U;
        chunk.embedding_method = legacy ? "authored_hashing_vectorizer_v1"
                                        : fields[method_field];
        const auto dimensions = static_cast<std::size_t>(
            std::stoull(fields[dimensions_field]));
        const std::size_t values_begin = legacy ? 7U : 8U;
        if (chunk.embedding_method.empty() || dimensions == 0U ||
            dimensions > 8192U || fields.size() != values_begin + dimensions) {
            throw std::runtime_error("persisted knowledge embedding metadata is invalid");
        }
        for (std::size_t index = 0; index < dimensions; ++index) {
            chunk.embedding.push_back(std::stod(fields[values_begin + index]));
        }
        chunk.embedding = validate_and_normalize_embedding(
            std::move(chunk.embedding));
        chunks_[chunk.id] = std::move(chunk);
    }
}

KnowledgeDocument KnowledgeIndexStore::ingest(
    const std::string& owner_id, const std::string& subject_id,
    const std::string& vector_store_id, const std::string& file_name,
    const std::string& media_type, const std::string& content,
    const std::string& embedding_method,
    const KnowledgeEmbeddingFunction& vectorize) {
    if (subject_id.empty()) throw std::invalid_argument("subject id is required");
    if (vector_store_id.empty()) {
        throw std::invalid_argument("vector store id is required");
    }
    if (file_name.empty() || file_name.size() > 260U) {
        throw std::invalid_argument("knowledge file name is invalid");
    }
    const bool is_parquet = is_parquet_knowledge_upload(media_type, file_name);
    if (!is_parquet && !supported_media_type(media_type)) {
        throw std::invalid_argument("knowledge file type is not supported");
    }
    if (content.empty()) throw std::invalid_argument("knowledge file is empty");
    if (content.size() > maximum_document_bytes_) {
        throw std::invalid_argument("knowledge file exceeds the configured size limit");
    }

    std::string text_content;
    if (is_parquet) {
        if (parquet_helper_executable_.empty()) {
            throw std::invalid_argument(
                "Parquet ingestion requires a configured DuckDB helper executable. "
                "Configure knowledge.parquetHelperExecutable, or upload as text, "
                "Markdown, CSV, JSON, or JSONL instead.");
        }
        text_content = parquet_bytes_to_json(parquet_helper_executable_, content);
        if (text_content.empty()) {
            throw std::invalid_argument("Parquet file contains no rows to index");
        }
    } else {
        if (content.find('\0') != std::string::npos) {
            throw std::invalid_argument("knowledge file must contain text, not binary data");
        }
        text_content = content;
    }

    // A JSON/JSONL/Parquet upload whose records match the Scraper Project's
    // instruction/conversation/document shapes gets rewritten to clean text
    // before chunking; anything else (including JSON that doesn't match any
    // recognized shape) chunks the original bytes exactly as before.
    static constexpr std::string_view json_extension = ".json";
    static constexpr std::string_view jsonl_extension = ".jsonl";
    const bool looks_like_json = is_parquet || media_type == "application/json" ||
        media_type == "application/x-ndjson" ||
        (file_name.size() >= json_extension.size() &&
         file_name.compare(file_name.size() - json_extension.size(),
                           json_extension.size(), json_extension) == 0) ||
        (file_name.size() >= jsonl_extension.size() &&
         file_name.compare(file_name.size() - jsonl_extension.size(),
                           jsonl_extension.size(), jsonl_extension) == 0);
    if (looks_like_json) {
        const auto flattened =
            flatten_scraper_records(text_content, maximum_document_bytes_);
        if (!flattened.empty()) text_content = flattened;
    }

    if (embedding_method.empty() || embedding_method.size() > 160U) {
        throw std::invalid_argument("embedding method is invalid");
    }
    if (embedding_method != "authored_hashing_vectorizer_v1" && !vectorize) {
        throw std::invalid_argument("learned embedding backend is unavailable");
    }
    const auto pieces = make_chunks(text_content);
    if (pieces.empty()) throw std::invalid_argument("knowledge file contains no indexable text");

    KnowledgeDocument document;
    document.id = random_id();
    document.subject_id = subject_id;
    document.vector_store_id = vector_store_id;
    document.file_name = file_name;
    document.media_type = is_parquet ? "application/vnd.apache.parquet"
                                     : (media_type.empty() ? "text/plain" : media_type);
    document.sha256 = sha256_hex(content);
    document.owner_id = owner_id;
    document.byte_count = content.size();
    document.chunk_count = pieces.size();
    document.ingested_at_epoch_seconds = epoch_seconds();

    std::vector<KnowledgeChunk> pending;
    pending.reserve(pieces.size());
    for (std::size_t index = 0; index < pieces.size(); ++index) {
        KnowledgeChunk chunk;
        chunk.document_id = document.id;
        chunk.subject_id = subject_id;
        chunk.vector_store_id = vector_store_id;
        chunk.file_name = file_name;
        chunk.chunk_index = index;
        chunk.text = pieces[index];
        chunk.embedding_method = embedding_method;
        chunk.id = sha256_hex(document.id + ":" + std::to_string(index) +
                              ":" + chunk.text);
        chunk.embedding = validate_and_normalize_embedding(
            vectorize ? vectorize(chunk.text)
                      : authored_hash_embedding_impl(chunk.text));
        pending.push_back(std::move(chunk));
    }

    const std::lock_guard<std::mutex> lock(mutex_);
    documents_[document.id] = document;
    records_->put("ml_knowledge_documents", document.id,
                  pack({document.subject_id, document.vector_store_id,
                        document.file_name, document.media_type,
                        document.sha256, document.owner_id,
                        std::to_string(document.byte_count),
                        std::to_string(document.chunk_count),
                        std::to_string(document.ingested_at_epoch_seconds)}));
    for (const auto& chunk : pending) {
        std::vector<std::string> fields{
            chunk.document_id, chunk.subject_id, chunk.vector_store_id,
            chunk.file_name, std::to_string(chunk.chunk_index), chunk.text,
            chunk.embedding_method,
            std::to_string(chunk.embedding.size())};
        for (const auto value : chunk.embedding) fields.push_back(number_field(value));
        chunks_[chunk.id] = chunk;
        records_->put("ml_knowledge_chunks", chunk.id, pack(fields));
    }
    return document;
}

std::optional<KnowledgeDocument> KnowledgeIndexStore::find_document(
    const std::string& id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    const auto found = documents_.find(id);
    return found == documents_.end() ? std::nullopt
                                     : std::optional<KnowledgeDocument>(found->second);
}

std::vector<KnowledgeDocument> KnowledgeIndexStore::list_documents() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KnowledgeDocument> result;
    result.reserve(documents_.size());
    for (const auto& item : documents_) result.push_back(item.second);
    return result;
}

std::vector<KnowledgeChunk> KnowledgeIndexStore::chunks_for_store(
    const std::string& vector_store_id) const {
    const std::lock_guard<std::mutex> lock(mutex_);
    std::vector<KnowledgeChunk> result;
    for (const auto& item : chunks_) {
        if (item.second.vector_store_id == vector_store_id) {
            result.push_back(item.second);
        }
    }
    return result;
}

bool KnowledgeIndexStore::remove_document(const std::string& id) {
    const std::lock_guard<std::mutex> lock(mutex_);
    if (documents_.erase(id) == 0U) return false;
    records_->erase("ml_knowledge_documents", id);
    for (auto iterator = chunks_.begin(); iterator != chunks_.end();) {
        if (iterator->second.document_id == id) {
            records_->erase("ml_knowledge_chunks", iterator->first);
            iterator = chunks_.erase(iterator);
        } else {
            ++iterator;
        }
    }
    return true;
}

RagRetrievalResult retrieve_knowledge(
    const KnowledgeIndexStore& index, const std::string& vector_store_id,
    const std::string& search_strategy, const std::string& query,
    const std::size_t top_k, const std::string& embedding_method,
    const KnowledgeEmbeddingFunction& vectorize) {
    if (vector_store_id.empty()) throw std::invalid_argument("vector store id is required");
    if (query.empty() || query.size() > 8192U) {
        throw std::invalid_argument("RAG query must contain 1 to 8192 bytes");
    }
    if (top_k == 0U || top_k > 20U) {
        throw std::invalid_argument("RAG retrieval count must be between 1 and 20");
    }
    std::string strategy;
    strategy.reserve(search_strategy.size());
    for (const unsigned char character : search_strategy) {
        if (std::isalnum(character) != 0) {
            strategy += static_cast<char>(std::tolower(character));
        }
    }
    if (strategy.empty()) strategy = "hybrid";
    const bool keyword_only = strategy == "keyword" || strategy == "lexical" ||
                              strategy == "keywordonly";
    const bool vector_only = strategy == "vector" || strategy == "vectoronly";
    if (!keyword_only && !vector_only && strategy != "hybrid") {
        throw std::invalid_argument(
            "search strategy must be hybrid, vector, or keyword");
    }

    const auto stored_chunks = index.chunks_for_store(vector_store_id);
    for (const auto& chunk : stored_chunks) {
        if (chunk.embedding_method != embedding_method) {
            throw std::invalid_argument(
                "vector store contains embeddings from a different model");
        }
    }
    std::vector<double> query_embedding;
    if (!keyword_only) {
        if (embedding_method != "authored_hashing_vectorizer_v1" && !vectorize) {
            throw std::invalid_argument("learned embedding backend is unavailable");
        }
        query_embedding = validate_and_normalize_embedding(
            vectorize ? vectorize(query)
                      : authored_hash_embedding_impl(query));
    }
    const auto query_tokens = tokens(query);
    const std::set<std::string> query_words(query_tokens.begin(), query_tokens.end());
    RagRetrievalResult result;
    result.query = query;
    result.search_strategy = keyword_only ? "keyword" :
                             vector_only ? "vector" : "hybrid";
    for (const auto& chunk : stored_chunks) {
        RagRetrievedChunk retrieved;
        retrieved.chunk = chunk;
        retrieved.vector_score = keyword_only
                                     ? 0.0
                                     : cosine(query_embedding, chunk.embedding);
        retrieved.keyword_score = keyword_overlap(query_words, chunk.text);
        retrieved.score = keyword_only ? retrieved.keyword_score :
                          vector_only ? retrieved.vector_score :
                          0.65 * retrieved.vector_score +
                              0.35 * retrieved.keyword_score;
        if (retrieved.score > 0.0) result.chunks.push_back(std::move(retrieved));
    }
    std::sort(result.chunks.begin(), result.chunks.end(),
              [](const RagRetrievedChunk& left,
                 const RagRetrievedChunk& right) {
                  if (left.score != right.score) return left.score > right.score;
                  return left.chunk.id < right.chunk.id;
              });
    if (result.chunks.size() > top_k) result.chunks.resize(top_k);
    return result;
}

std::string knowledge_document_json(const KnowledgeDocument& document) {
    return "{\"id\":\"" + json_escape(document.id) +
           "\",\"subjectId\":\"" + json_escape(document.subject_id) +
           "\",\"vectorStoreId\":\"" + json_escape(document.vector_store_id) +
           "\",\"fileName\":\"" + json_escape(document.file_name) +
           "\",\"mediaType\":\"" + json_escape(document.media_type) +
           "\",\"sha256\":\"" + document.sha256 +
           "\",\"byteCount\":" + std::to_string(document.byte_count) +
           ",\"chunkCount\":" + std::to_string(document.chunk_count) +
           ",\"ownerId\":\"" + json_escape(document.owner_id) +
           "\",\"ingestedAtEpochSeconds\":" +
           std::to_string(document.ingested_at_epoch_seconds) + "}";
}

std::string knowledge_documents_json(
    const std::vector<KnowledgeDocument>& documents) {
    std::string result = "[";
    for (std::size_t index = 0; index < documents.size(); ++index) {
        if (index > 0U) result += ",";
        result += knowledge_document_json(documents[index]);
    }
    return result + "]";
}

std::string knowledge_index_profile_json(
    const std::string& vector_store_id,
    const std::vector<KnowledgeChunk>& chunks) {
    std::set<std::string> documents;
    std::set<std::string> methods;
    std::set<std::size_t> dimensions;
    std::size_t bytes = 0U;
    for (const auto& chunk : chunks) {
        documents.insert(chunk.document_id);
        methods.insert(chunk.embedding_method);
        dimensions.insert(chunk.embedding.size());
        bytes += chunk.text.size();
    }
    const std::string method = methods.empty()
                                   ? "unpopulated"
                                   : (methods.size() == 1U ? *methods.begin()
                                                          : "mixed");
    const std::size_t dimension_count =
        dimensions.size() == 1U ? *dimensions.begin() : 0U;
    return "{\"vectorStoreId\":\"" + json_escape(vector_store_id) +
           "\",\"embeddingMethod\":\"" + json_escape(method) + "\""
           ",\"dimensions\":" + std::to_string(dimension_count) +
           ",\"documentCount\":" + std::to_string(documents.size()) +
           ",\"chunkCount\":" + std::to_string(chunks.size()) +
           ",\"indexedTextBytes\":" + std::to_string(bytes) + "}";
}

std::string rag_retrieval_result_json(const RagRetrievalResult& result) {
    std::string chunks = "[";
    std::string context;
    for (std::size_t index = 0; index < result.chunks.size(); ++index) {
        if (index > 0U) chunks += ",";
        const auto& item = result.chunks[index];
        const std::string citation = "[" + item.chunk.file_name + "#chunk-" +
                                     std::to_string(item.chunk.chunk_index + 1U) + "]";
        chunks += "{\"chunkId\":\"" + json_escape(item.chunk.id) +
                  "\",\"documentId\":\"" +
                  json_escape(item.chunk.document_id) +
                  "\",\"subjectId\":\"" +
                  json_escape(item.chunk.subject_id) +
                  "\",\"fileName\":\"" +
                  json_escape(item.chunk.file_name) +
                  "\",\"chunkIndex\":" +
                  std::to_string(item.chunk.chunk_index) +
                  ",\"citation\":\"" + json_escape(citation) +
                  "\",\"score\":" + json_number(item.score) +
                  ",\"vectorScore\":" + json_number(item.vector_score) +
                  ",\"keywordScore\":" + json_number(item.keyword_score) +
                  ",\"text\":\"" + json_escape(item.chunk.text) + "\"}";
        if (!context.empty()) context += "\n\n";
        context += citation + "\n" + item.chunk.text;
    }
    chunks += "]";
    return "{\"query\":\"" + json_escape(result.query) +
           "\",\"searchStrategy\":\"" +
           json_escape(result.search_strategy) +
           "\",\"retrievedCount\":" + std::to_string(result.chunks.size()) +
           ",\"chunks\":" + chunks + ",\"context\":\"" +
           json_escape(context) + "\"}";
}

}  // namespace masterai
