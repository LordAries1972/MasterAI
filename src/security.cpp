// MasterAI native cryptography, random generation, and audit integrity.
//
// This unit uses only operating-system cryptographic providers, keeps hash
// operations bounded, and verifies the append-only audit chain without
// introducing a third-party security foundation.
#include "masterai.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <limits>
#include <stdexcept>

#if defined(_WIN32)
#include <windows.h>
#include <bcrypt.h>
#elif defined(__linux__)
#include <cerrno>
#include <cstring>
#include <linux/if_alg.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <unistd.h>
#else
#error "MasterAI supports only Windows and Linux."
#endif

namespace masterai {
namespace {

std::string hex_encode(const std::uint8_t* bytes, const std::size_t size) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string result(size * 2U, '0');
    for (std::size_t index = 0; index < size; ++index) {
        result[index * 2U] = digits[(bytes[index] >> 4U) & 0x0fU];
        result[index * 2U + 1U] = digits[bytes[index] & 0x0fU];
    }
    return result;
}

#if defined(__linux__)
// Opens and binds one Linux kernel SHA-256 provider socket for both memory and
// file hashing so the AF_ALG setup policy is implemented once.
int open_sha256_socket(const char* failure_context) {
    const int algorithm_socket = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (algorithm_socket < 0) {
        throw std::runtime_error(std::string(failure_context) +
                                 " socket failed");
    }
    sockaddr_alg address{};
    address.salg_family = AF_ALG;
    std::strncpy(reinterpret_cast<char*>(address.salg_type), "hash",
                 sizeof(address.salg_type) - 1U);
    std::strncpy(reinterpret_cast<char*>(address.salg_name), "sha256",
                 sizeof(address.salg_name) - 1U);
    if (bind(algorithm_socket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0) {
        close(algorithm_socket);
        throw std::runtime_error(std::string(failure_context) +
                                 " bind failed");
    }
    return algorithm_socket;
}
#endif

}  // namespace

bool constant_time_equal(const std::string& left, const std::string& right) noexcept {
    const std::size_t maximum = std::max(left.size(), right.size());
    unsigned int difference = static_cast<unsigned int>(left.size() ^ right.size());
    for (std::size_t index = 0; index < maximum; ++index) {
        const unsigned char left_character =
            index < left.size() ? static_cast<unsigned char>(left[index]) : 0U;
        const unsigned char right_character =
            index < right.size() ? static_cast<unsigned char>(right[index]) : 0U;
        difference |= static_cast<unsigned int>(left_character ^ right_character);
    }
    return difference == 0U;
}

std::vector<std::uint8_t> secure_random(const std::size_t size) {
    if (size > static_cast<std::size_t>(std::numeric_limits<unsigned long>::max())) {
        throw std::invalid_argument("secure random request is too large");
    }
    std::vector<std::uint8_t> bytes(size);
#if defined(_WIN32)
    const NTSTATUS status =
        BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()),
                        BCRYPT_USE_SYSTEM_PREFERRED_RNG);
    if (status < 0) {
        throw std::runtime_error("BCryptGenRandom failed");
    }
#elif defined(__linux__)
    std::size_t completed = 0U;
    while (completed < bytes.size()) {
        const ssize_t received =
            getrandom(bytes.data() + completed, bytes.size() - completed, 0);
        if (received < 0 && errno == EINTR) {
            continue;
        }
        if (received <= 0) {
            throw std::runtime_error("Linux getrandom failed");
        }
        completed += static_cast<std::size_t>(received);
    }
#endif
    return bytes;
}

std::string sha256_hex(const std::string& value) {
    std::array<std::uint8_t, 32> digest{};
#if defined(_WIN32)
    if (value.size() >
        static_cast<std::size_t>(std::numeric_limits<ULONG>::max())) {
        throw std::invalid_argument("SHA-256 input is too large");
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_object_size = 0;
    DWORD returned = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_object_size),
                          sizeof(hash_object_size), &returned, 0) < 0) {
        if (algorithm != nullptr) {
            BCryptCloseAlgorithmProvider(algorithm, 0);
        }
        throw std::runtime_error("BCrypt SHA-256 initialization failed");
    }
    std::vector<std::uint8_t> hash_object(hash_object_size);
    if (BCryptCreateHash(algorithm, &hash, hash_object.data(), hash_object_size,
                         nullptr, 0, 0) < 0 ||
        BCryptHashData(hash, reinterpret_cast<PUCHAR>(
                                  const_cast<char*>(value.data())),
                       static_cast<ULONG>(value.size()), 0) < 0 ||
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0) {
        if (hash != nullptr) {
            BCryptDestroyHash(hash);
        }
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCrypt SHA-256 operation failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
#elif defined(__linux__)
    const int algorithm_socket =
        open_sha256_socket("Linux AF_ALG SHA-256");
    const int operation_socket = accept(algorithm_socket, nullptr, nullptr);
    if (operation_socket < 0) {
        close(algorithm_socket);
        throw std::runtime_error("Linux AF_ALG SHA-256 accept failed");
    }
    const ssize_t written = write(operation_socket, value.data(), value.size());
    const ssize_t digest_size = read(operation_socket, digest.data(), digest.size());
    close(operation_socket);
    close(algorithm_socket);
    if (written != static_cast<ssize_t>(value.size()) ||
        digest_size != static_cast<ssize_t>(digest.size())) {
        throw std::runtime_error("Linux AF_ALG SHA-256 operation failed");
    }
#endif
    return hex_encode(digest.data(), digest.size());
}

std::string sha256_file_hex(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("file could not be opened for SHA-256");
    std::array<std::uint8_t, 32> digest{};
    std::vector<char> buffer(1024U * 1024U);
#if defined(_WIN32)
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD returned = 0;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0 ||
        BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &returned, 0) < 0) {
        if (algorithm != nullptr) BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCrypt file SHA-256 initialization failed");
    }
    std::vector<std::uint8_t> object(object_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                         nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCrypt file SHA-256 hash creation failed");
    }
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(count), 0) < 0) {
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            throw std::runtime_error("BCrypt file SHA-256 update failed");
        }
    }
    if (!input.eof() ||
        BCryptFinishHash(hash, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        BCryptCloseAlgorithmProvider(algorithm, 0);
        throw std::runtime_error("BCrypt file SHA-256 completion failed");
    }
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
#elif defined(__linux__)
    const int algorithm_socket =
        open_sha256_socket("Linux AF_ALG file SHA-256");
    const int operation = accept(algorithm_socket, nullptr, nullptr);
    if (operation < 0) {
        close(algorithm_socket);
        throw std::runtime_error("AF_ALG file SHA-256 accept failed");
    }
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 && send(operation, buffer.data(),
                              static_cast<std::size_t>(count),
                              input.eof() ? 0 : MSG_MORE) != count) {
            close(operation);
            close(algorithm_socket);
            throw std::runtime_error("AF_ALG file SHA-256 update failed");
        }
    }
    if (!input.eof() ||
        read(operation, digest.data(), digest.size()) !=
            static_cast<ssize_t>(digest.size())) {
        close(operation);
        close(algorithm_socket);
        throw std::runtime_error("AF_ALG file SHA-256 completion failed");
    }
    close(operation);
    close(algorithm_socket);
#endif
    return hex_encode(digest.data(), digest.size());
}

SessionStore::SessionStore(RecordStore& records) : records_(&records) {
    for (const auto& item : records.list("sessions")) {
        const auto first = item.second.find('|');
        const auto second = item.second.find('|', first + 1U);
        const auto third = item.second.find('|', second + 1U);
        if (first == std::string::npos || second == std::string::npos ||
            third == std::string::npos) {
            continue;
        }
        try {
            sessions_[item.first] =
                Session{item.second.substr(0U, first),
                        std::stoull(item.second.substr(first + 1U,
                                                      second - first - 1U)),
                        item.second.substr(second + 1U,
                                           third - second - 1U) == "1",
                        item.second.substr(third + 1U)};
        } catch (const std::exception&) {
            // Corrupt individual session records fail closed.
        }
    }
}

void SessionStore::persist(const std::string& token_hash,
                           const Session& session) {
    if (records_ != nullptr) {
        records_->put("sessions", token_hash,
                      session.user_id + "|" +
                      std::to_string(session.expires_at_epoch_seconds) + "|" +
                      (session.revoked ? "1" : "0") + "|" +
                      session.csrf_secret);
    }
}

std::string SessionStore::create(const std::string& user_id,
                                 const std::uint64_t now_epoch_seconds,
                                 const std::uint64_t lifetime_seconds) {
    if (user_id.empty() || lifetime_seconds == 0U ||
        now_epoch_seconds > std::numeric_limits<std::uint64_t>::max() - lifetime_seconds) {
        throw std::invalid_argument("invalid session parameters");
    }
    const auto bytes = secure_random(32U);
    const std::string token = hex_encode(bytes.data(), bytes.size());
    const std::string hash = sha256_hex(token);
    const auto csrf = secure_random(32U);
    const Session session{user_id, now_epoch_seconds + lifetime_seconds, false,
                          hex_encode(csrf.data(), csrf.size())};
    sessions_[hash] = session;
    persist(hash, session);
    return token;
}

std::optional<SessionStore::Session> SessionStore::validate(
    const std::string& token, const std::uint64_t now_epoch_seconds) const {
    const auto found = sessions_.find(sha256_hex(token));
    if (found == sessions_.end() || found->second.revoked ||
        now_epoch_seconds >= found->second.expires_at_epoch_seconds) {
        return std::nullopt;
    }
    return found->second;
}

void SessionStore::revoke(const std::string& token) {
    const auto found = sessions_.find(sha256_hex(token));
    if (found != sessions_.end()) {
        found->second.revoked = true;
        persist(found->first, found->second);
    }
}

std::string SessionStore::rotate(const std::string& token,
                                 const std::uint64_t now_epoch_seconds,
                                 const std::uint64_t lifetime_seconds) {
    const auto session = validate(token, now_epoch_seconds);
    if (!session) throw std::invalid_argument("session cannot be rotated");
    revoke(token);
    return create(session->user_id, now_epoch_seconds, lifetime_seconds);
}

void SessionStore::revoke_user(const std::string& user_id) {
    for (auto& item : sessions_) {
        if (item.second.user_id == user_id && !item.second.revoked) {
            item.second.revoked = true;
            persist(item.first, item.second);
        }
    }
}

}  // namespace masterai
