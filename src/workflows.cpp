// MasterAI project, chat, attachment, download, and benchmark workflows.
//
// This unit owns durable workflow records and the shared project-text boundary
// used by MCP and IDE integrations to avoid divergent filesystem validation.
#include "masterai.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace masterai {
namespace {

bool safe_identifier(const std::string& value) {
    return !value.empty() && value.size() <= 96U &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
                      character == '-' || character == '_' || character == '.';
           });
}

bool valid_sha256(const std::string& value) {
    return value.size() == 64U &&
           std::all_of(value.begin(), value.end(), [](const char character) {
               return (character >= '0' && character <= '9') ||
                      (character >= 'a' && character <= 'f');
           });
}

bool starts_with(const std::string& value, const std::string& prefix) {
    return value.compare(0U, prefix.size(), prefix) == 0;
}

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
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
            throw std::runtime_error("persisted workflow record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted workflow record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string role_name(const ChatRole role) {
    if (role == ChatRole::assistant) return "assistant";
    if (role == ChatRole::system) return "system";
    return "user";
}

ChatRole parse_role(const std::string& role) {
    if (role == "assistant") return ChatRole::assistant;
    if (role == "system") return ChatRole::system;
    if (role == "user") return ChatRole::user;
    throw std::runtime_error("persisted chat role is invalid");
}

std::string profile_name(const BenchmarkProfile profile) {
    if (profile == BenchmarkProfile::standard) return "standard";
    if (profile == BenchmarkProfile::extended) return "extended";
    return "quick";
}

BenchmarkProfile parse_profile(const std::string& profile) {
    if (profile == "standard") return BenchmarkProfile::standard;
    if (profile == "extended") return BenchmarkProfile::extended;
    if (profile == "quick") return BenchmarkProfile::quick;
    throw std::runtime_error("persisted benchmark profile is invalid");
}

bool approved_text_extension(std::string extension) {
    std::transform(extension.begin(), extension.end(), extension.begin(),
                   [](const unsigned char c) {
                       return static_cast<char>(std::tolower(c));
                   });
    static const std::set<std::string> approved{
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".hxx",
        ".md", ".txt", ".json", ".xml", ".yaml", ".yml", ".toml",
        ".ini", ".cmake", ".py", ".rs", ".go", ".java", ".cs", ".pas",
        ".sql", ".sh", ".ps1"};
    return approved.find(extension) != approved.end();
}

}  // namespace

// Rejects malformed, overlong, surrogate, truncated, and out-of-range UTF-8.
bool valid_utf8_text(const std::string& value) noexcept {
    std::size_t index = 0U;
    while (index < value.size()) {
        const auto first = static_cast<unsigned char>(value[index]);
        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        std::size_t continuation = 0U;
        std::uint32_t codepoint = 0U;
        if (first >= 0xc2U && first <= 0xdfU) {
            continuation = 1U;
            codepoint = first & 0x1fU;
        } else if (first >= 0xe0U && first <= 0xefU) {
            continuation = 2U;
            codepoint = first & 0x0fU;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            continuation = 3U;
            codepoint = first & 0x07U;
        } else {
            return false;
        }
        if (index + continuation >= value.size()) return false;
        for (std::size_t offset = 1U; offset <= continuation; ++offset) {
            const auto next =
                static_cast<unsigned char>(value[index + offset]);
            if ((next & 0xc0U) != 0x80U) return false;
            codepoint = (codepoint << 6U) | (next & 0x3fU);
        }
        if ((continuation == 2U && codepoint < 0x800U) ||
            (continuation == 3U && codepoint < 0x10000U) ||
            (codepoint >= 0xd800U && codepoint <= 0xdfffU) ||
            codepoint > 0x10ffffU) {
            return false;
        }
        index += continuation + 1U;
    }
    return true;
}

ProjectCatalog::ProjectCatalog(std::filesystem::path workspace_root)
    : workspace_root_(std::move(workspace_root)) {
    if (workspace_root_.empty()) {
        throw std::invalid_argument("workspace root is required");
    }
}

ProjectCatalog::ProjectCatalog(std::filesystem::path workspace_root,
                               RecordStore& records)
    : ProjectCatalog(std::move(workspace_root)) {
    records_ = &records;
    restore();
}

ProjectRecord ProjectCatalog::add(const std::string& id,
                                  const std::string& display_name,
                                  const std::filesystem::path& root) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe_identifier(id) || display_name.empty() ||
        display_name.size() > 160U || projects_.find(id) != projects_.end() ||
        !is_path_within(workspace_root_, root)) {
        throw std::invalid_argument("project identity or root is outside policy");
    }
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(root, error);
    if (error || !std::filesystem::is_directory(canonical) ||
        std::filesystem::is_symlink(root)) {
        throw std::invalid_argument("project root must be an existing non-symlink directory");
    }
    ProjectRecord record{id, display_name, canonical};
    projects_.emplace(id, record);
    persist(record);
    return record;
}

std::optional<ProjectRecord> ProjectCatalog::find(const std::string& id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = projects_.find(id);
    return found == projects_.end() ? std::nullopt
                                    : std::optional<ProjectRecord>(found->second);
}

std::vector<ProjectRecord> ProjectCatalog::list() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ProjectRecord> result;
    result.reserve(projects_.size());
    for (const auto& item : projects_) result.push_back(item.second);
    return result;
}

// Canonicalizes, contains, bounds, reads, and validates one project text file.
ProjectTextFile read_project_text_file(
    const ProjectRecord& project, const std::string& relative_text,
    const std::uint64_t maximum_bytes) {
    if (relative_text.empty() || relative_text.size() > 4096U ||
        maximum_bytes == 0U) {
        throw std::invalid_argument("project text path is outside policy");
    }
    const std::filesystem::path relative(relative_text);
    if (relative.is_absolute()) {
        throw std::invalid_argument("project text path must be relative");
    }
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(project.root, error);
    if (error) {
        throw std::invalid_argument("project root is unavailable");
    }
    const auto path =
        std::filesystem::weakly_canonical(project.root / relative, error);
    if (error || !is_path_within(root, path) ||
        !std::filesystem::is_regular_file(path, error) || error ||
        std::filesystem::is_symlink(project.root / relative, error)) {
        throw std::invalid_argument("project text file is unavailable");
    }
    const auto size = std::filesystem::file_size(path, error);
    if (error || size > maximum_bytes) {
        throw std::invalid_argument("project text file exceeds its limit");
    }
    std::ifstream input(path, std::ios::binary);
    std::string content(static_cast<std::size_t>(size), '\0');
    if (!content.empty()) {
        input.read(content.data(),
                   static_cast<std::streamsize>(content.size()));
    }
    if (!input || content.find('\0') != std::string::npos ||
        !valid_utf8_text(content)) {
        throw std::invalid_argument("project file is not approved UTF-8 text");
    }
    return {path, std::move(content)};
}

void ProjectCatalog::restore() {
    for (const auto& item : records_->list("projects")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 2U) {
            throw std::runtime_error("persisted project record is invalid");
        }
        std::error_code error;
        const auto root = std::filesystem::weakly_canonical(fields[1], error);
        if (!safe_identifier(item.first) || fields[0].empty() || error ||
            !is_path_within(workspace_root_, root) ||
            !std::filesystem::is_directory(root) ||
            std::filesystem::is_symlink(root)) {
            throw std::runtime_error("persisted project violates path policy");
        }
        projects_.emplace(item.first, ProjectRecord{item.first, fields[0], root});
    }
}

void ProjectCatalog::persist(const ProjectRecord& project) {
    if (records_ != nullptr) {
        records_->put("projects", project.id,
                      pack({project.display_name, project.root.string()}));
    }
}

ChatStore::ChatStore(RecordStore& records) : records_(&records) { restore(); }

// Derives a short sidebar title from a user's first message: single line,
// collapsed whitespace, truncated with an ellipsis so long prompts don't
// blow out the sidebar layout.
namespace {
std::string derive_chat_title(const std::string& content) {
    std::string collapsed;
    collapsed.reserve(content.size());
    bool last_was_space = false;
    for (const char ch : content) {
        const bool is_space = ch == ' ' || ch == '\t' || ch == '\r' || ch == '\n';
        if (is_space) {
            if (!last_was_space && !collapsed.empty()) collapsed.push_back(' ');
            last_was_space = true;
        } else {
            collapsed.push_back(ch);
            last_was_space = false;
        }
    }
    while (!collapsed.empty() && collapsed.back() == ' ') collapsed.pop_back();
    constexpr std::size_t kMaxTitleLength = 60U;
    if (collapsed.size() > kMaxTitleLength) {
        collapsed.resize(kMaxTitleLength);
        collapsed += "...";
    }
    return collapsed.empty() ? "New chat" : collapsed;
}
// chat_id is always a fixed-width 32-character hex random_id(), so a '.'
// separator followed by a fixed-width zero-padded decimal index keeps
// "chat_messages" keys sorting lexicographically in the same order as
// numeric message order -- restore() relies on that to reassemble a chat's
// messages via a single pass over RecordStore::list() without a separate
// per-chat sort.
constexpr std::size_t kChatIdLength = 32U;
constexpr std::size_t kMessageIndexWidth = 6U;

std::string message_key(const std::string& chat_id, const std::size_t index) {
    auto digits = std::to_string(index);
    if (digits.size() < kMessageIndexWidth) {
        digits.insert(0U, kMessageIndexWidth - digits.size(), '0');
    }
    return chat_id + "." + digits;
}

}  // namespace

ChatRecord ChatStore::create(const std::string& owner_id,
                             const std::string& project_id,
                             const std::string& model_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe_identifier(owner_id) || !safe_identifier(project_id) ||
        !safe_identifier(model_id) || chats_.size() >= 10000U) {
        throw std::invalid_argument("chat identity is outside policy");
    }
    const auto id = random_id();
    ChatRecord record{id, owner_id, project_id, model_id, "New chat",
                      epoch_seconds(), {}};
    chats_.emplace(id, record);
    persist_header(record);
    return record;
}

void ChatStore::append(const std::string& chat_id, const ChatRole role,
                       const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = chats_.find(chat_id);
    if (found == chats_.end()) {
        throw std::invalid_argument("chat does not exist");
    }
    if (content.empty() || content.size() > 1024U * 1024U ||
        found->second.messages.size() >= 10000U) {
        throw std::invalid_argument("chat message is outside policy");
    }
    const bool title_changes =
        role == ChatRole::user && found->second.messages.empty();
    if (title_changes) {
        found->second.title = derive_chat_title(content);
    }
    const ChatMessage message{role, content, epoch_seconds()};
    const auto index = found->second.messages.size();
    found->second.messages.push_back(message);
    persist_message(chat_id, index, message);
    if (title_changes) persist_header(found->second);
}

bool ChatStore::set_model(const std::string& chat_id,
                          const std::string& owner_id,
                          const std::string& model_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe_identifier(model_id)) {
        throw std::invalid_argument("chat identity is outside policy");
    }
    const auto found = chats_.find(chat_id);
    if (found == chats_.end() || found->second.owner_id != owner_id) {
        return false;
    }
    found->second.model_id = model_id;
    persist_header(found->second);
    return true;
}

std::optional<ChatRecord> ChatStore::find_for_owner(
    const std::string& chat_id, const std::string& owner_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = chats_.find(chat_id);
    if (found == chats_.end() || found->second.owner_id != owner_id) {
        return std::nullopt;
    }
    return found->second;
}

std::vector<ChatRecord> ChatStore::list_for_owner(
    const std::string& owner_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<ChatRecord> result;
    for (const auto& item : chats_) {
        if (item.second.owner_id == owner_id) result.push_back(item.second);
    }
    std::sort(result.begin(), result.end(),
              [](const ChatRecord& left, const ChatRecord& right) {
                  return left.created_at_epoch_seconds >
                         right.created_at_epoch_seconds;
              });
    return result;
}

bool ChatStore::remove(const std::string& chat_id, const std::string& owner_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = chats_.find(chat_id);
    if (found == chats_.end() || found->second.owner_id != owner_id) {
        return false;
    }
    if (records_ != nullptr) {
        for (std::size_t index = 0U; index < found->second.messages.size();
             ++index) {
            records_->erase("chat_messages", message_key(chat_id, index));
        }
        records_->erase("chats", chat_id);
    }
    chats_.erase(found);
    return true;
}

void ChatStore::restore() {
    // Three header shapes can be on disk: the original 4-field header
    // (owner, project, model, count) with messages packed inline; the later
    // 6-field header (...title, createdAt, count) also with messages inline;
    // and the current 5-field header (owner, project, model, title,
    // createdAt) with messages stored separately in "chat_messages". Field
    // count alone tells them apart with no ambiguity: inline-message records
    // land on 4+3*count (%3==1) or 6+3*count (%3==0), while the current
    // header is always exactly 5 fields (%3==2) -- a count can never produce
    // 5 under either older scheme.
    for (const auto& item : records_->list("chats")) {
        const auto fields = unpack(item.second);
        const bool split_format = fields.size() == 5U;
        const bool current_format =
            !split_format && fields.size() >= 6U && fields.size() % 3U == 0U;
        const bool legacy_format =
            !split_format && !current_format && fields.size() >= 4U &&
            fields.size() % 3U == 1U;
        if ((!split_format && !current_format && !legacy_format) ||
            !safe_identifier(item.first) || !safe_identifier(fields[0]) ||
            !safe_identifier(fields[1]) || !safe_identifier(fields[2])) {
            throw std::runtime_error("persisted chat record is invalid");
        }
        ChatRecord chat;
        chat.id = item.first;
        chat.owner_id = fields[0];
        chat.project_id = fields[1];
        chat.model_id = fields[2];
        if (split_format) {
            chat.title = fields[3];
            chat.created_at_epoch_seconds = std::stoull(fields[4]);
            chats_.emplace(chat.id, std::move(chat));
            continue;
        }
        std::size_t message_start;
        std::uint64_t declared_count;
        if (current_format) {
            chat.title = fields[3];
            chat.created_at_epoch_seconds = std::stoull(fields[4]);
            declared_count = std::stoull(fields[5]);
            message_start = 6U;
            if (declared_count != (fields.size() - 6U) / 3U) {
                throw std::runtime_error("persisted chat message count is invalid");
            }
        } else {
            declared_count = std::stoull(fields[3]);
            message_start = 4U;
            if (declared_count != (fields.size() - 4U) / 3U) {
                throw std::runtime_error("persisted chat message count is invalid");
            }
        }
        if (declared_count > 10000U) {
            throw std::runtime_error("persisted chat message count is invalid");
        }
        for (std::size_t index = message_start; index < fields.size(); index += 3U) {
            chat.messages.push_back(
                {parse_role(fields[index]), fields[index + 1U],
                 std::stoull(fields[index + 2U])});
        }
        // Upgrade in memory using the same title/timestamp rules a new chat
        // gets (only the oldest, 4-field format never set these), then
        // migrate onto the current split header/message-per-key scheme so
        // this record never gets rewritten wholesale again.
        if (!current_format) {
            for (const auto& message : chat.messages) {
                if (message.role == ChatRole::user) {
                    chat.title = derive_chat_title(message.content);
                    break;
                }
            }
            chat.created_at_epoch_seconds = chat.messages.empty()
                ? epoch_seconds() : chat.messages.front().created_at_epoch_seconds;
        }
        persist_header(chat);
        for (std::size_t index = 0U; index < chat.messages.size(); ++index) {
            persist_message(chat.id, index, chat.messages[index]);
        }
        // Messages are populated below from "chat_messages" -- the write
        // above lands there too, so keeping them here as well would double
        // every migrated chat's history.
        chat.messages.clear();
        chats_.emplace(chat.id, std::move(chat));
    }
    // Messages sort into place automatically: keys are
    // "<32-char chat id>.<zero-padded index>", and RecordStore::list()
    // returns entries in std::map key order, which is exactly chat id then
    // numeric index for same-width keys -- no separate grouping/sort needed.
    for (const auto& item : records_->list("chat_messages")) {
        if (item.first.size() <= kChatIdLength ||
            item.first[kChatIdLength] != '.') {
            throw std::runtime_error("persisted chat message key is invalid");
        }
        const auto chat_id = item.first.substr(0U, kChatIdLength);
        const auto found = chats_.find(chat_id);
        if (found == chats_.end()) {
            throw std::runtime_error("persisted chat message is orphaned");
        }
        const auto fields = unpack(item.second);
        if (fields.size() != 3U) {
            throw std::runtime_error("persisted chat message is invalid");
        }
        if (found->second.messages.size() >= 10000U) {
            throw std::runtime_error("persisted chat message count is invalid");
        }
        found->second.messages.push_back(
            {parse_role(fields[0]), fields[1], std::stoull(fields[2])});
    }
}

void ChatStore::persist_header(const ChatRecord& chat) {
    if (records_ == nullptr) return;
    records_->put("chats", chat.id,
                  pack({chat.owner_id, chat.project_id, chat.model_id,
                        chat.title,
                        std::to_string(chat.created_at_epoch_seconds)}));
}

void ChatStore::persist_message(const std::string& chat_id,
                                const std::size_t index,
                                const ChatMessage& message) {
    if (records_ == nullptr) return;
    records_->put("chat_messages", message_key(chat_id, index),
                  pack({role_name(message.role), message.content,
                        std::to_string(message.created_at_epoch_seconds)}));
}

AttachmentStore::AttachmentStore(std::filesystem::path root,
                                 RecordStore& records)
    : root_(std::move(root)), records_(records) {
    if (root_.empty()) {
        throw std::invalid_argument("attachment root is required");
    }
    std::filesystem::create_directories(root_);
    restore();
}

AttachmentRecord AttachmentStore::add_text(
    const std::string& owner_id, const std::string& project_id,
    const std::string& filename, const std::string& content) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto leaf = std::filesystem::path(filename).filename();
    if (!safe_identifier(owner_id) || !safe_identifier(project_id) ||
        filename.empty() || filename.size() > 255U ||
        leaf.string() != filename || !approved_text_extension(leaf.extension().string()) ||
        content.empty() || content.size() > 16U * 1024U * 1024U ||
        !valid_utf8_text(content)) {
        throw std::invalid_argument("attachment violates text, name, or size policy");
    }
    const auto id = random_id();
    const auto directory = root_ / owner_id / project_id;
    std::filesystem::create_directories(directory);
    const auto stored = directory / (id + leaf.extension().string());
    const auto temporary = stored.string() + ".tmp";
    if (!is_path_within(root_, stored)) {
        throw std::runtime_error("attachment path escaped its approved root");
    }
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(content.data(), static_cast<std::streamsize>(content.size()));
        output.flush();
        if (!output) throw std::runtime_error("attachment write failed");
    }
    std::filesystem::rename(temporary, stored);
    AttachmentRecord record{id, owner_id, project_id, filename, stored,
                            static_cast<std::uint64_t>(content.size()),
                            sha256_hex(content)};
    records_.put("attachments", id,
                 pack({owner_id, project_id, filename, stored.string(),
                       std::to_string(record.size_bytes), record.sha256}));
    attachments_.emplace(id, record);
    return record;
}

std::optional<AttachmentRecord> AttachmentStore::find_for_owner(
    const std::string& id, const std::string& owner_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = attachments_.find(id);
    if (found == attachments_.end() || found->second.owner_id != owner_id) {
        return std::nullopt;
    }
    return found->second;
}

// Not itself locked: it only calls the already-locked find_for_owner() above
// and then does unlocked filesystem I/O, so locking here too would either
// deadlock (non-recursive mutex_) or needlessly hold the lock across file
// reads.
std::string AttachmentStore::read_text_for_owner(
    const std::string& id, const std::string& owner_id) const {
    const auto record = find_for_owner(id, owner_id);
    if (!record) throw std::invalid_argument("attachment is not available");
    if (record->size_bytes > 16U * 1024U * 1024U ||
        !std::filesystem::is_regular_file(record->stored_path) ||
        std::filesystem::is_symlink(record->stored_path) ||
        !constant_time_equal(sha256_file_hex(record->stored_path),
                             record->sha256)) {
        throw std::runtime_error("attachment failed its load-time integrity check");
    }
    std::ifstream input(record->stored_path, std::ios::binary);
    std::string content(static_cast<std::size_t>(record->size_bytes), '\0');
    input.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!input || !valid_utf8_text(content)) {
        throw std::runtime_error("attachment content is unavailable or invalid");
    }
    return content;
}

void AttachmentStore::restore() {
    for (const auto& item : records_.list("attachments")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 6U || !safe_identifier(item.first) ||
            !safe_identifier(fields[0]) || !safe_identifier(fields[1])) {
            throw std::runtime_error("persisted attachment record is invalid");
        }
        const std::filesystem::path stored(fields[3]);
        if (!is_path_within(root_, stored) ||
            !std::filesystem::is_regular_file(stored) ||
            std::filesystem::is_symlink(stored) ||
            !constant_time_equal(sha256_file_hex(stored), fields[5])) {
            throw std::runtime_error("persisted attachment failed integrity policy");
        }
        attachments_.emplace(
            item.first,
            AttachmentRecord{item.first, fields[0], fields[1], fields[2], stored,
                             std::stoull(fields[4]), fields[5]});
    }
}

DownloadJob::DownloadJob(DownloadRequest request)
    : DownloadJob(random_id(), std::move(request), DownloadState::queued, 0U,
                  {}) {}

DownloadJob::DownloadJob(std::string id, DownloadRequest request,
                         const DownloadState state,
                         const std::uint64_t completed_bytes,
                         std::string diagnostic)
    : id_(std::move(id)), request_(std::move(request)), state_(state),
      completed_bytes_(completed_bytes), diagnostic_(std::move(diagnostic)) {
    const bool hugging_face =
        starts_with(request_.source_url, "https://huggingface.co/") &&
        request_.source_url.find("/resolve/" + request_.immutable_revision + "/") !=
            std::string::npos;
    const bool github =
        starts_with(request_.source_url, "https://github.com/") &&
        request_.source_url.find("/releases/download/" +
                                 request_.immutable_revision + "/") !=
            std::string::npos;
    // ModelScope serves immutable-revision files the same way Hugging Face
    // does (/models/<org>/<repo>/resolve/<revision>/<file>), so it gets the
    // identical substring check rather than a bespoke one.
    const bool modelscope =
        starts_with(request_.source_url, "https://modelscope.cn/") &&
        request_.source_url.find("/resolve/" + request_.immutable_revision + "/") !=
            std::string::npos;
    const bool approved_source = hugging_face || github || modelscope;
    if (!safe_identifier(id_) || !approved_source ||
        !safe_identifier(request_.immutable_revision) ||
        request_.immutable_revision.size() < 7U ||
        request_.immutable_revision.size() > 128U ||
        !valid_sha256(request_.expected_sha256) ||
        request_.destination.empty() || !request_.license_accepted ||
        ((request_.minimum_ram_mib == 0U) !=
         (request_.recommended_ram_mib == 0U)) ||
        request_.recommended_ram_mib < request_.minimum_ram_mib) {
        throw std::invalid_argument("download request violates source, digest, or license policy");
    }
}

const std::string& DownloadJob::id() const noexcept { return id_; }
const DownloadRequest& DownloadJob::request() const noexcept { return request_; }
DownloadState DownloadJob::state() const noexcept { return state_; }
std::uint64_t DownloadJob::completed_bytes() const noexcept {
    return completed_bytes_;
}
const std::string& DownloadJob::diagnostic() const noexcept {
    return diagnostic_;
}

void DownloadJob::begin(const std::uint64_t existing_bytes) {
    if (state_ != DownloadState::queued && state_ != DownloadState::paused) {
        throw std::logic_error("download cannot begin from its current state");
    }
    completed_bytes_ = existing_bytes;
    state_ = DownloadState::transferring;
    diagnostic_.clear();
}

void DownloadJob::record_progress(const std::uint64_t completed_bytes) {
    if (state_ != DownloadState::transferring ||
        completed_bytes < completed_bytes_) {
        throw std::logic_error("download progress is invalid");
    }
    completed_bytes_ = completed_bytes;
}

void DownloadJob::pause() {
    if (state_ != DownloadState::transferring) {
        throw std::logic_error("only an active download can pause");
    }
    state_ = DownloadState::paused;
}

void DownloadJob::begin_verification() {
    if (state_ != DownloadState::transferring) {
        throw std::logic_error("only a transferred download can be verified");
    }
    state_ = DownloadState::verifying;
}

void DownloadJob::complete(const std::string& actual_sha256) {
    if (state_ != DownloadState::verifying) {
        throw std::logic_error("download is not awaiting verification");
    }
    if (!constant_time_equal(request_.expected_sha256, actual_sha256)) {
        state_ = DownloadState::quarantined;
        diagnostic_ = "SHA-256 mismatch; artifact quarantined";
        return;
    }
    state_ = DownloadState::complete;
    diagnostic_.clear();
}

void DownloadJob::fail(std::string diagnostic) {
    if (state_ == DownloadState::complete ||
        state_ == DownloadState::cancelled) {
        throw std::logic_error("terminal download state cannot fail");
    }
    state_ = DownloadState::failed;
    diagnostic_ = std::move(diagnostic);
}

void DownloadJob::cancel() {
    if (state_ == DownloadState::complete) {
        throw std::logic_error("completed download cannot be cancelled");
    }
    state_ = DownloadState::cancelled;
    diagnostic_ = "cancelled by operator";
}

BenchmarkStore::BenchmarkStore(RecordStore& records) : record_store_(&records) {
    restore();
}

void BenchmarkStore::add(const BenchmarkRecord& record) {
    if (!safe_identifier(record.model_id) || record.backend_version.empty() ||
        record.build_id.empty() || record.hardware_id.empty() ||
        !valid_sha256(record.prompt_suite_hash) || record.prompt_tokens == 0U ||
        record.generated_tokens == 0U || record.elapsed_microseconds == 0U ||
        record.total_cases == 0U || record.passed_cases > record.total_cases ||
        record.settings_json.empty()) {
        throw std::invalid_argument("benchmark record is incomplete or invalid");
    }
    records_.push_back(record);
    if (record_store_ != nullptr) {
        const auto value = pack(
            {record.model_id, record.backend_version, record.build_id,
             record.hardware_id, record.prompt_suite_hash,
             profile_name(record.profile), std::to_string(record.prompt_tokens),
             std::to_string(record.generated_tokens),
             std::to_string(record.elapsed_microseconds),
             std::to_string(record.peak_resident_memory_bytes),
             std::to_string(record.passed_cases),
             std::to_string(record.total_cases), record.settings_json});
        record_store_->put("benchmarks", random_id(), value);
    }
}

std::vector<BenchmarkRecord> BenchmarkStore::comparable(
    const std::string& hardware_id, const std::string& prompt_suite_hash,
    const BenchmarkProfile profile) const {
    std::vector<BenchmarkRecord> result;
    for (const auto& record : records_) {
        if (record.hardware_id == hardware_id &&
            record.prompt_suite_hash == prompt_suite_hash &&
            record.profile == profile) {
            result.push_back(record);
        }
    }
    return result;
}

std::optional<BenchmarkRecord> BenchmarkStore::recommend(
    const std::string& hardware_id, const std::string& prompt_suite_hash,
    const BenchmarkProfile profile) const {
    const auto candidates =
        comparable(hardware_id, prompt_suite_hash, profile);
    if (candidates.empty()) return std::nullopt;
    const auto better = [](const BenchmarkRecord& left,
                           const BenchmarkRecord& right) {
        const double left_quality =
            static_cast<double>(left.passed_cases) /
            static_cast<double>(left.total_cases);
        const double right_quality =
            static_cast<double>(right.passed_cases) /
            static_cast<double>(right.total_cases);
        if (left_quality != right_quality) return left_quality < right_quality;
        const double left_rate =
            static_cast<double>(left.generated_tokens) * 1000000.0 /
            static_cast<double>(left.elapsed_microseconds);
        const double right_rate =
            static_cast<double>(right.generated_tokens) * 1000000.0 /
            static_cast<double>(right.elapsed_microseconds);
        return left_rate < right_rate;
    };
    return *std::max_element(candidates.begin(), candidates.end(), better);
}

std::vector<BenchmarkRecord> BenchmarkStore::all() const { return records_; }

void BenchmarkStore::restore() {
    for (const auto& item : record_store_->list("benchmarks")) {
        const auto fields = unpack(item.second);
        if (fields.size() != 13U) {
            throw std::runtime_error("persisted benchmark record is invalid");
        }
        BenchmarkRecord record;
        record.model_id = fields[0];
        record.backend_version = fields[1];
        record.build_id = fields[2];
        record.hardware_id = fields[3];
        record.prompt_suite_hash = fields[4];
        record.profile = parse_profile(fields[5]);
        record.prompt_tokens = std::stoull(fields[6]);
        record.generated_tokens = std::stoull(fields[7]);
        record.elapsed_microseconds = std::stoull(fields[8]);
        record.peak_resident_memory_bytes = std::stoull(fields[9]);
        record.passed_cases = std::stoull(fields[10]);
        record.total_cases = std::stoull(fields[11]);
        record.settings_json = fields[12];
        if (!safe_identifier(record.model_id) ||
            !valid_sha256(record.prompt_suite_hash)) {
            throw std::runtime_error("persisted benchmark violates policy");
        }
        records_.push_back(std::move(record));
    }
}

void McpPolicy::register_inbound_tool(const McpInboundTool& tool) {
    if (!safe_identifier(tool.name) || !safe_identifier(tool.required_scope) ||
        inbound_tools_.find(tool.name) != inbound_tools_.end()) {
        throw std::invalid_argument("inbound MCP tool registration is invalid");
    }
    inbound_tools_.emplace(tool.name, tool);
}

bool McpPolicy::authorize_inbound(
    const std::string& tool, const std::set<std::string>& client_scopes,
    const bool project_authorized) const {
    const auto found = inbound_tools_.find(tool);
    return project_authorized && found != inbound_tools_.end() &&
           client_scopes.find(found->second.required_scope) != client_scopes.end();
}

void McpPolicy::register_outbound_server(const McpOutboundServer& server) {
    if (!safe_identifier(server.id) ||
        outbound_servers_.find(server.id) != outbound_servers_.end() ||
        server.transport == McpTransport::legacy_sse) {
        throw std::invalid_argument("outbound MCP server registration is invalid");
    }
    if (server.transport == McpTransport::stdio_transport) {
        if (server.executable.empty() ||
            !std::filesystem::is_regular_file(server.executable) ||
            std::filesystem::is_symlink(server.executable)) {
            throw std::invalid_argument("stdio MCP executable is not an approved file");
        }
    } else if (!starts_with(server.endpoint, "https://") &&
               !starts_with(server.endpoint, "http://127.0.0.1:") &&
               !starts_with(server.endpoint, "http://[::1]:")) {
        throw std::invalid_argument("MCP HTTP endpoint requires TLS or loopback");
    }
    outbound_servers_.emplace(server.id, server);
}

bool McpPolicy::authorize_outbound(
    const std::string& server_id, const std::string& tool,
    const std::set<std::string>& user_scopes, const bool project_authorized,
    const bool user_approved) const {
    const auto found = outbound_servers_.find(server_id);
    return found != outbound_servers_.end() && found->second.enabled &&
           found->second.allowed_tools.find(tool) !=
               found->second.allowed_tools.end() &&
           user_scopes.find("mcp.tools.invoke") != user_scopes.end() &&
           project_authorized && user_approved;
}

}  // namespace masterai
