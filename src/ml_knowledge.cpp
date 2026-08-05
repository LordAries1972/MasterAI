// Phases 58-60: dependency-free local knowledge ingestion, persisted hashing
// embeddings, and evidence-bearing retrieval for administrator RAG testing.
#include "masterai.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cctype>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>

namespace masterai {
namespace {

constexpr std::size_t embedding_dimensions = 128U;
constexpr std::size_t maximum_document_bytes = 2U * 1024U * 1024U;
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

std::vector<double> embed(const std::string& text) {
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

}  // namespace

KnowledgeIndexStore::KnowledgeIndexStore(RecordStore& records)
    : records_(&records) {
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
        if (fields.size() != 7U + embedding_dimensions) {
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
        const auto dimensions = static_cast<std::size_t>(std::stoull(fields[6]));
        if (dimensions != embedding_dimensions) {
            throw std::runtime_error("persisted knowledge embedding dimension changed");
        }
        for (std::size_t index = 0; index < dimensions; ++index) {
            chunk.embedding.push_back(std::stod(fields[7U + index]));
        }
        chunks_[chunk.id] = std::move(chunk);
    }
}

KnowledgeDocument KnowledgeIndexStore::ingest(
    const std::string& owner_id, const std::string& subject_id,
    const std::string& vector_store_id, const std::string& file_name,
    const std::string& media_type, const std::string& content) {
    if (subject_id.empty()) throw std::invalid_argument("subject id is required");
    if (vector_store_id.empty()) {
        throw std::invalid_argument("vector store id is required");
    }
    if (file_name.empty() || file_name.size() > 260U) {
        throw std::invalid_argument("knowledge file name is invalid");
    }
    if (!supported_media_type(media_type)) {
        throw std::invalid_argument("knowledge file type is not supported");
    }
    if (content.empty()) throw std::invalid_argument("knowledge file is empty");
    if (content.size() > maximum_document_bytes) {
        throw std::invalid_argument("knowledge file exceeds the 2 MiB limit");
    }
    if (content.find('\0') != std::string::npos) {
        throw std::invalid_argument("knowledge file must contain text, not binary data");
    }
    const auto pieces = make_chunks(content);
    if (pieces.empty()) throw std::invalid_argument("knowledge file contains no indexable text");

    KnowledgeDocument document;
    document.id = random_id();
    document.subject_id = subject_id;
    document.vector_store_id = vector_store_id;
    document.file_name = file_name;
    document.media_type = media_type.empty() ? "text/plain" : media_type;
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
        chunk.id = sha256_hex(document.id + ":" + std::to_string(index) +
                              ":" + chunk.text);
        chunk.embedding = embed(chunk.text);
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
    const std::size_t top_k) {
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

    const auto query_embedding = embed(query);
    const auto query_tokens = tokens(query);
    const std::set<std::string> query_words(query_tokens.begin(), query_tokens.end());
    RagRetrievalResult result;
    result.query = query;
    result.search_strategy = keyword_only ? "keyword" :
                             vector_only ? "vector" : "hybrid";
    for (const auto& chunk : index.chunks_for_store(vector_store_id)) {
        RagRetrievedChunk retrieved;
        retrieved.chunk = chunk;
        retrieved.vector_score = cosine(query_embedding, chunk.embedding);
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
    std::size_t bytes = 0U;
    for (const auto& chunk : chunks) {
        documents.insert(chunk.document_id);
        bytes += chunk.text.size();
    }
    return "{\"vectorStoreId\":\"" + json_escape(vector_store_id) +
           "\",\"embeddingMethod\":\"authored_hashing_vectorizer_v1\""
           ",\"dimensions\":" + std::to_string(embedding_dimensions) +
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
