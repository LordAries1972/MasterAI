// MasterAI durable storage, authorization, secret, and audit operations.
//
// This unit owns recovery-safe records and security-sensitive persistence. It
// stores only hashes or OS-protected secret material and reconstructs corrupted
// or legacy records with fail-closed authorization semantics.
#include "masterai.hpp"

#include <chrono>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <dpapi.h>
#elif defined(__linux__)
#include <cerrno>
#include <sys/syscall.h>
#include <unistd.h>
#endif

namespace masterai {
namespace {

bool safe_name(const std::string& value) {
    if (value.empty() || value.size() > 128U) return false;
    for (const unsigned char c : value) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.')) {
            return false;
        }
    }
    return true;
}

std::string hex(const std::string& value) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string output(value.size() * 2U, '0');
    for (std::size_t i = 0; i < value.size(); ++i) {
        const auto c = static_cast<unsigned char>(value[i]);
        output[i * 2U] = digits[c >> 4U];
        output[i * 2U + 1U] = digits[c & 0x0fU];
    }
    return output;
}

unsigned char nibble(const char c) {
    if (c >= '0' && c <= '9') return static_cast<unsigned char>(c - '0');
    if (c >= 'a' && c <= 'f') return static_cast<unsigned char>(c - 'a' + 10);
    throw std::runtime_error("record contains invalid hexadecimal data");
}

std::string unhex(const std::string& value) {
    if (value.size() % 2U != 0U) throw std::runtime_error("record hex is truncated");
    std::string output(value.size() / 2U, '\0');
    for (std::size_t i = 0; i < output.size(); ++i) {
        output[i] = static_cast<char>((nibble(value[i * 2U]) << 4U) |
                                      nibble(value[i * 2U + 1U]));
    }
    return output;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> parts;
    std::size_t start = 0U;
    while (true) {
        const auto next = line.find('\t', start);
        parts.push_back(line.substr(start, next - start));
        if (next == std::string::npos) break;
        start = next + 1U;
    }
    return parts;
}

std::string record_line(const char operation, const std::string& collection,
                        const std::string& key, const std::string& value) {
    const std::string body =
        std::string(1U, operation) + "\t" + hex(collection) + "\t" +
        hex(key) + "\t" + hex(value);
    return sha256_hex(body) + "\t" + body + "\n";
}

void append_record(const std::filesystem::path& path, const std::string& line) {
    std::ofstream output(path, std::ios::binary | std::ios::app);
    output.write(line.data(), static_cast<std::streamsize>(line.size()));
    output.flush();
    if (!output) throw std::runtime_error("record journal append failed");
}

std::string role_name(const UserRole role) {
    switch (role) {
        case UserRole::administrator: return "administrator";
        case UserRole::developer: return "developer";
        case UserRole::viewer: return "viewer";
    }
    throw std::runtime_error("invalid user role");
}

UserRole parse_role(const std::string& value) {
    if (value == "administrator") return UserRole::administrator;
    if (value == "developer") return UserRole::developer;
    if (value == "viewer") return UserRole::viewer;
    throw std::runtime_error("stored user role is invalid");
}

std::string user_value(const UserRecord& user) {
    return hex(user.os_principal) + ":" + hex(user.display_name) + ":" +
           role_name(user.role) + ":" + (user.enabled ? "1" : "0") + ":" +
           hex(user.password_hash);
}

UserRecord parse_user(const std::string& id, const std::string& value) {
    // Stored user fields use ':' rather than tabs. The password-hash field
    // was added after release; records written before it have 4 fields and
    // are treated as OS-verified accounts with no local password.
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (true) {
        const auto next = value.find(':', start);
        fields.push_back(value.substr(start, next - start));
        if (next == std::string::npos) break;
        start = next + 1U;
    }
    if (fields.size() != 4U && fields.size() != 5U) {
        throw std::runtime_error("stored user is corrupt");
    }
    return UserRecord{id, unhex(fields[0]), unhex(fields[1]),
                      parse_role(fields[2]), fields[3] == "1",
                      fields.size() == 5U ? unhex(fields[4]) : std::string{}};
}

std::string random_id() {
    const auto bytes = secure_random(16U);
    return hex(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
}

std::string clean_audit(const std::string& value) {
    std::string result;
    result.reserve(std::min<std::size_t>(value.size(), 2048U));
    for (const unsigned char c : value) {
        result.push_back(c < 0x20U || c == '|' ? ' ' : static_cast<char>(c));
        if (result.size() == 2048U) break;
    }
    return result;
}

}  // namespace

RecordStore::RecordStore(std::filesystem::path root) : root_(std::move(root)) {}

void RecordStore::open() {
    std::filesystem::create_directories(root_);
    records_.clear();
    bool recovered = false;
    const auto load_file = [&](const std::filesystem::path& path) {
        if (!std::filesystem::exists(path)) return;
        std::ifstream input(path, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            const auto fields = split(line);
            if (fields.size() != 5U) {
                recovered = true;
                break;
            }
            const std::string body = fields[1] + "\t" + fields[2] + "\t" +
                                     fields[3] + "\t" + fields[4];
            if (!constant_time_equal(fields[0], sha256_hex(body))) {
                recovered = true;
                break;
            }
            const auto collection = unhex(fields[2]);
            const auto key = unhex(fields[3]);
            if (!safe_name(collection) || !safe_name(key)) {
                recovered = true;
                break;
            }
            if (fields[1] == "P") {
                records_[collection][key] = unhex(fields[4]);
            } else if (fields[1] == "D") {
                records_[collection].erase(key);
            } else {
                recovered = true;
                break;
            }
        }
    };
    load_file(root_ / "records.snapshot");
    load_file(root_ / "records.journal");
    opened_ = true;
    bool migrated = false;
    const auto stored_version = get("_meta", "schema");
    if (!stored_version) {
        // Migration 0 -> 1: establish explicit schema metadata. Existing
        // journal-only records are already compatible with schema 1.
        records_["_meta"]["schema"] = "1";
        migrated = true;
    } else {
        const auto version = std::stoul(*stored_version);
        if (version > schema_version_) {
            throw std::runtime_error(
                "record store was created by a newer MasterAI schema");
        }
        while (schema_version_ < version) ++schema_version_;
    }
    if (recovered) {
        const auto corrupt = root_ / "records.journal.corrupt";
        if (std::filesystem::exists(root_ / "records.journal")) {
            std::filesystem::copy_file(
                root_ / "records.journal", corrupt,
                std::filesystem::copy_options::overwrite_existing);
        }
    }
    if (recovered || migrated) checkpoint();
}

void RecordStore::put(const std::string& collection, const std::string& key,
                      const std::string& value) {
    // Locked for the whole body: appending the journal line and updating
    // records_ must happen as one unit, or two concurrent request threads
    // could interleave journal writes or race the in-memory map.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_ || !safe_name(collection) || !safe_name(key) ||
        value.size() > 16U * 1024U * 1024U) {
        throw std::invalid_argument("record write is outside policy");
    }
    append_record(root_ / "records.journal",
                  record_line('P', collection, key, value));
    records_[collection][key] = value;
}

void RecordStore::erase(const std::string& collection, const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_ || !safe_name(collection) || !safe_name(key)) {
        throw std::invalid_argument("record erase is outside policy");
    }
    append_record(root_ / "records.journal", record_line('D', collection, key, ""));
    records_[collection].erase(key);
}

std::optional<std::string> RecordStore::get(const std::string& collection,
                                            const std::string& key) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto group = records_.find(collection);
    if (group == records_.end()) return std::nullopt;
    const auto item = group->second.find(key);
    return item == group->second.end() ? std::nullopt
                                       : std::optional<std::string>(item->second);
}

std::vector<std::pair<std::string, std::string>>
RecordStore::list(const std::string& collection) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::pair<std::string, std::string>> result;
    const auto group = records_.find(collection);
    if (group != records_.end()) {
        result.assign(group->second.begin(), group->second.end());
    }
    return result;
}

void RecordStore::checkpoint() {
    // Locked for the whole body: this iterates records_ while writing the
    // snapshot file and then truncates the journal, so no put()/erase() may
    // observe or mutate state mid-checkpoint.
    std::lock_guard<std::mutex> lock(mutex_);
    if (!opened_) throw std::logic_error("record store is not open");
    const auto temporary = root_ / "records.snapshot.tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        for (const auto& group : records_) {
            for (const auto& item : group.second) {
                const auto line = record_line('P', group.first, item.first, item.second);
                output.write(line.data(), static_cast<std::streamsize>(line.size()));
            }
        }
        output.flush();
        if (!output) throw std::runtime_error("record checkpoint write failed");
    }
#if defined(_WIN32)
    if (MoveFileExW(temporary.c_str(), (root_ / "records.snapshot").c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) == 0) {
        throw std::runtime_error("record checkpoint replacement failed");
    }
#else
    std::filesystem::rename(temporary, root_ / "records.snapshot");
#endif
    std::ofstream clear(root_ / "records.journal",
                        std::ios::binary | std::ios::trunc);
    if (!clear) throw std::runtime_error("record journal checkpoint failed");
}

std::filesystem::path RecordStore::backup(
    const std::filesystem::path& backup_root) const {
    // Locked so a concurrent checkpoint() cannot be mid-rewrite of the
    // snapshot/journal files while they're being copied here.
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(backup_root);
    const auto now = std::chrono::system_clock::now().time_since_epoch().count();
    const auto target = backup_root / ("records-" + std::to_string(now));
    std::filesystem::create_directories(target);
    for (const auto& name : {"records.snapshot", "records.journal"}) {
        const auto source = root_ / name;
        if (std::filesystem::exists(source)) {
            std::filesystem::copy_file(source, target / name);
        }
    }
    return target;
}

unsigned int RecordStore::schema_version() const noexcept {
    return schema_version_;
}

SecretStore::SecretStore(std::filesystem::path root) : root_(std::move(root)) {}

bool SecretStore::available() const noexcept {
#if defined(_WIN32)
    return true;
#elif defined(__linux__) && defined(SYS_add_key) && defined(SYS_keyctl)
    return true;
#else
    return false;
#endif
}

void SecretStore::set(const std::string& name, const std::string& secret) {
    if (!safe_name(name) || secret.empty() || secret.size() > 1024U * 1024U ||
        !available()) {
        throw std::invalid_argument("secret write is outside policy");
    }
#if defined(_WIN32)
    DATA_BLOB input{static_cast<DWORD>(secret.size()),
                    reinterpret_cast<BYTE*>(const_cast<char*>(secret.data()))};
    DATA_BLOB output{};
    if (CryptProtectData(&input, L"MasterAI non-password secret", nullptr, nullptr,
                         nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output) == 0) {
        throw std::runtime_error("Windows DPAPI secret protection failed");
    }
    std::filesystem::create_directories(root_);
    std::ofstream file(root_ / (name + ".dpapi"),
                       std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char*>(output.pbData), output.cbData);
    LocalFree(output.pbData);
    if (!file) throw std::runtime_error("protected secret persistence failed");
#elif defined(__linux__) && defined(SYS_add_key) && defined(SYS_keyctl)
    constexpr long user_keyring = -4;
    const std::string description = "masterai:" + name;
    const long id = syscall(SYS_add_key, "user", description.c_str(),
                            secret.data(), secret.size(), user_keyring);
    if (id < 0) throw std::runtime_error("Linux kernel keyring write failed");
#endif
}

std::optional<std::string> SecretStore::get(const std::string& name) const {
    if (!safe_name(name) || !available()) return std::nullopt;
#if defined(_WIN32)
    const auto path = root_ / (name + ".dpapi");
    if (!std::filesystem::exists(path)) return std::nullopt;
    std::ifstream file(path, std::ios::binary);
    std::vector<char> encrypted((std::istreambuf_iterator<char>(file)),
                                std::istreambuf_iterator<char>());
    if (encrypted.empty()) return std::nullopt;
    DATA_BLOB input{static_cast<DWORD>(encrypted.size()),
                    reinterpret_cast<BYTE*>(encrypted.data())};
    DATA_BLOB output{};
    if (CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr,
                           CRYPTPROTECT_UI_FORBIDDEN, &output) == 0) {
        throw std::runtime_error("Windows DPAPI secret recovery failed");
    }
    std::string secret(reinterpret_cast<char*>(output.pbData), output.cbData);
    SecureZeroMemory(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return secret;
#elif defined(__linux__) && defined(SYS_add_key) && defined(SYS_keyctl)
    constexpr long user_keyring = -4;
    constexpr long keyctl_search = 10;
    constexpr long keyctl_read = 11;
    const std::string description = "masterai:" + name;
    const long id = syscall(SYS_keyctl, keyctl_search, user_keyring, "user",
                            description.c_str(), 0);
    if (id < 0) return std::nullopt;
    const long size = syscall(SYS_keyctl, keyctl_read, id, nullptr, 0);
    if (size <= 0 || size > 1024 * 1024) return std::nullopt;
    std::string secret(static_cast<std::size_t>(size), '\0');
    if (syscall(SYS_keyctl, keyctl_read, id, &secret[0], size) != size) {
        throw std::runtime_error("Linux kernel keyring read failed");
    }
    return secret;
#else
    return std::nullopt;
#endif
}

void SecretStore::erase(const std::string& name) {
    if (!safe_name(name)) throw std::invalid_argument("secret name is invalid");
#if defined(_WIN32)
    std::filesystem::remove(root_ / (name + ".dpapi"));
#elif defined(__linux__) && defined(SYS_add_key) && defined(SYS_keyctl)
    constexpr long user_keyring = -4;
    constexpr long keyctl_search = 10;
    constexpr long keyctl_revoke = 3;
    const std::string description = "masterai:" + name;
    const long id = syscall(SYS_keyctl, keyctl_search, user_keyring, "user",
                            description.c_str(), 0);
    if (id >= 0) static_cast<void>(syscall(SYS_keyctl, keyctl_revoke, id));
#endif
}

UserStore::UserStore(RecordStore& records) : records_(records) {}

bool role_allows(const UserRole role, const std::string& permission) {
    static const std::map<UserRole, std::set<std::string>> permissions{
        {UserRole::administrator,
         {"identity.read", "models.read", "models.load", "tokens.create",
          "users.manage", "settings.manage", "ide.connect", "mcp.connect",
          "mcp.invoke", "mcp.tools.invoke", "projects.read",
          "projects.write", "chats.read", "chats.write", "attachments.write",
          "downloads.manage", "benchmarks.read", "benchmarks.run",
          // Machine Learning module (docs/PLAN.md "Machine Learning
          // Abilities" section 3): administrator-only, unlike every other
          // permission above which developer/viewer also hold a subset of.
          // The granular permissions section 3 recommends (Machine
          // Learning Administrator, Data Administrator, ...) aren't
          // implemented yet -- every ml.* permission maps to the single
          // administrator role until a granular ML role model exists.
          "ml.dashboard.view", "ml.projects.view", "ml.projects.create",
          "ml.projects.delete", "ml.models.view", "ml.models.import",
          "ml.models.approve", "ml.models.delete", "ml.datasets.view",
          "ml.datasets.import", "ml.datasets.approve", "ml.datasets.delete",
          "ml.subjects.view", "ml.subjects.create", "ml.subjects.review",
          "ml.subjects.delete", "ml.labels.view", "ml.labels.manage",
          "ml.dataprep.view", "ml.dataprep.manage", "ml.training.view",
          "ml.training.manage", "ml.evaluation.view",
          "ml.evaluation.manage", "ml.experiments.view",
          "ml.experiments.manage", "ml.finetuning.view",
          "ml.finetuning.manage", "ml.modelbuilder.view",
          "ml.modelbuilder.manage", "ml.instructions.view",
          "ml.instructions.manage"}},
        {UserRole::developer,
         {"identity.read", "models.read", "models.load", "tokens.create",
          "ide.connect", "mcp.connect", "mcp.invoke", "mcp.tools.invoke",
          "projects.read",
          "projects.write", "chats.read",
          "chats.write", "attachments.write", "benchmarks.read",
          "benchmarks.run"}},
        {UserRole::viewer,
         {"identity.read", "models.read", "tokens.create", "projects.read",
          "chats.read", "benchmarks.read"}}};
    const auto found = permissions.find(role);
    return found != permissions.end() &&
           found->second.find(permission) != found->second.end();
}

bool UserStore::setup_required() const { return records_.list("users").empty(); }

UserRecord UserStore::create_first_administrator(
    const std::string& principal, const std::string& display_name,
    const std::string& password_hash) {
    if (!setup_required()) throw std::logic_error("administrator already exists");
    return add(principal, display_name, UserRole::administrator, password_hash);
}

UserRecord UserStore::add(const std::string& principal,
                          const std::string& display_name, const UserRole role,
                          const std::string& password_hash) {
    if (principal.empty() || principal.size() > 256U || display_name.empty() ||
        display_name.size() > 160U || find_by_principal(principal).has_value()) {
        throw std::invalid_argument("user mapping is invalid or duplicate");
    }
    UserRecord user{random_id(), principal, display_name, role, true, password_hash};
    records_.put("users", user.id, user_value(user));
    return user;
}

std::optional<UserRecord> UserStore::find_by_principal(
    const std::string& principal) const {
    for (const auto& item : records_.list("users")) {
        auto user = parse_user(item.first, item.second);
        if (user.os_principal == principal) return user;
    }
    return std::nullopt;
}

std::optional<UserRecord> UserStore::find_by_id(const std::string& id) const {
    const auto value = records_.get("users", id);
    return value ? std::optional<UserRecord>(parse_user(id, *value)) : std::nullopt;
}

std::vector<UserRecord> UserStore::all() const {
    std::vector<UserRecord> result;
    for (const auto& item : records_.list("users")) {
        result.push_back(parse_user(item.first, item.second));
    }
    return result;
}

AuditLog::AuditLog(std::filesystem::path path) : path_(std::move(path)) {}

void AuditLog::append(const std::string& event, const std::string& actor,
                      const std::string& outcome, const std::string& detail) {
    // Locked for the whole body: reading the previous hash-chain link and
    // appending the next line must be atomic, or two concurrent callers (now
    // routine, since almost every request appends an audit line) would race
    // reading the same "previous" hash and could interleave or break the
    // chain.
    std::lock_guard<std::mutex> lock(mutex_);
    std::filesystem::create_directories(path_.parent_path());
    std::string previous(64U, '0');
    if (std::filesystem::exists(path_)) {
        std::ifstream input(path_, std::ios::binary);
        std::string line;
        while (std::getline(input, line)) {
            const auto separator = line.rfind('|');
            if (separator != std::string::npos) previous = line.substr(separator + 1U);
        }
    }
    const auto timestamp = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    const std::string body = previous + "|" + std::to_string(timestamp) + "|" +
        clean_audit(event) + "|" + clean_audit(actor) + "|" +
        clean_audit(outcome) + "|" + clean_audit(detail);
    std::ofstream output(path_, std::ios::binary | std::ios::app);
    output << body << "|" << sha256_hex(body) << "\n";
    output.flush();
    if (!output) throw std::runtime_error("audit append failed");
}

// Restores hashed API-token records. Legacy four-field records remain readable,
// but receive no project bindings so they cannot gain MCP filesystem authority
// merely because the storage schema evolved.
ApiTokenStore::ApiTokenStore(RecordStore& records) : records_(records) {
    for (const auto& item : records.list("api_tokens")) {
        const auto fields = split(item.second);
        if (fields.size() != 4U && fields.size() != 5U) continue;
        try {
            Token token;
            token.user_id = fields[0];
            std::size_t start = 0U;
            while (start < fields[1].size()) {
                const auto end = fields[1].find(',', start);
                token.scopes.insert(fields[1].substr(start, end - start));
                if (end == std::string::npos) break;
                start = end + 1U;
            }
            if (fields.size() == 5U) {
                start = 0U;
                while (start < fields[2].size()) {
                    const auto end = fields[2].find(',', start);
                    token.project_ids.insert(
                        fields[2].substr(start, end - start));
                    if (end == std::string::npos) break;
                    start = end + 1U;
                }
            }
            const std::size_t time_index = fields.size() == 5U ? 3U : 2U;
            token.expires_at_epoch_seconds = std::stoull(fields[time_index]);
            token.revoked = fields[time_index + 1U] == "1";
            tokens_[item.first] = std::move(token);
        } catch (const std::exception&) {
            // Corrupt tokens fail closed.
        }
    }
}

// Serializes scopes and explicit project bindings beside the token hash. Raw
// bearer credentials never enter the record store.
void ApiTokenStore::persist(const std::string& token_hash,
                            const Token& token) {
    std::string scopes;
    for (const auto& scope : token.scopes) {
        if (!scopes.empty()) scopes += ",";
        scopes += scope;
    }
    std::string projects;
    for (const auto& project : token.project_ids) {
        if (!projects.empty()) projects += ",";
        projects += project;
    }
    records_.put("api_tokens", token_hash,
                 token.user_id + "\t" + scopes + "\t" + projects + "\t" +
                 std::to_string(token.expires_at_epoch_seconds) + "\t" +
                 (token.revoked ? "1" : "0"));
}

// Issues a random bearer credential after validating its lifetime, scopes, and
// project bindings. The returned plaintext is shown once; only its digest and
// authority record are persisted.
std::string ApiTokenStore::create(
    const std::string& user_id, const std::set<std::string>& scopes,
    const std::uint64_t now, const std::uint64_t lifetime,
    const std::set<std::string>& project_ids) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!safe_name(user_id) || scopes.empty() || lifetime == 0U ||
        now > std::numeric_limits<std::uint64_t>::max() - lifetime) {
        throw std::invalid_argument("API token parameters are invalid");
    }
    for (const auto& scope : scopes) {
        if (!safe_name(scope)) throw std::invalid_argument("API token scope is invalid");
    }
    for (const auto& project_id : project_ids) {
        if (!safe_name(project_id)) {
            throw std::invalid_argument("API token project binding is invalid");
        }
    }
    const auto bytes = secure_random(32U);
    const std::string token =
        hex(std::string(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
    const std::string hash = sha256_hex(token);
    Token record{user_id, scopes, project_ids, now + lifetime, false};
    tokens_[hash] = record;
    persist(hash, record);
    return token;
}

std::optional<ApiTokenStore::Token> ApiTokenStore::validate(
    const std::string& token, const std::string& required_scope,
    const std::uint64_t now) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tokens_.find(sha256_hex(token));
    if (found == tokens_.end() || found->second.revoked ||
        now >= found->second.expires_at_epoch_seconds ||
        found->second.scopes.find(required_scope) == found->second.scopes.end()) {
        return std::nullopt;
    }
    return found->second;
}

void ApiTokenStore::revoke(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = tokens_.find(sha256_hex(token));
    if (found != tokens_.end()) {
        found->second.revoked = true;
        persist(found->first, found->second);
    }
}

}  // namespace masterai
