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

#if defined(_WIN32)
// Response-time optimization: BCrypt algorithm-provider handles are
// documented thread-safe and designed to be opened once and shared;
// opening/closing a provider is by far the most expensive part of a short
// hash. sha256_hex() runs on every authenticated request (session-token
// lookup hashes the cookie token), so the old open-per-call pattern paid
// that provider setup cost on every single request. These providers are
// opened lazily on first use and kept for the process lifetime; if the
// opening lambda throws, C++ static-local rules leave the initialization
// incomplete so the next call retries rather than caching a null handle.
BCRYPT_ALG_HANDLE cached_sha256_provider() {
    static const BCRYPT_ALG_HANDLE provider = [] {
        BCRYPT_ALG_HANDLE handle = nullptr;
        if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM,
                                        nullptr, 0) < 0) {
            throw std::runtime_error("BCrypt SHA-256 provider open failed");
        }
        return handle;
    }();
    return provider;
}

// Same lifetime policy as cached_sha256_provider(), for the HMAC-flagged
// provider PBKDF2 password hashing/verification uses.
BCRYPT_ALG_HANDLE cached_hmac_sha256_provider() {
    static const BCRYPT_ALG_HANDLE provider = [] {
        BCRYPT_ALG_HANDLE handle = nullptr;
        if (BCryptOpenAlgorithmProvider(&handle, BCRYPT_SHA256_ALGORITHM,
                                        nullptr,
                                        BCRYPT_ALG_HANDLE_HMAC_FLAG) < 0) {
            throw std::runtime_error(
                "BCrypt HMAC-SHA256 provider open failed");
        }
        return handle;
    }();
    return provider;
}
#endif

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
    // Uses the process-lifetime cached provider (see cached_sha256_provider)
    // instead of opening/closing one per call -- this function runs on every
    // authenticated request, so provider reuse directly improves response
    // times.
    const BCRYPT_ALG_HANDLE algorithm = cached_sha256_provider();
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD hash_object_size = 0;
    DWORD returned = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&hash_object_size),
                          sizeof(hash_object_size), &returned, 0) < 0) {
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
        throw std::runtime_error("BCrypt SHA-256 operation failed");
    }
    BCryptDestroyHash(hash);
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

std::string sha256_file_hex(
    const std::filesystem::path& path,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("file could not be opened for SHA-256");
    std::error_code size_error;
    const auto total_bytes = std::filesystem::file_size(path, size_error);
    std::uint64_t bytes_hashed = 0U;
    std::array<std::uint8_t, 32> digest{};
    std::vector<char> buffer(1024U * 1024U);
#if defined(_WIN32)
    // Shares the process-lifetime cached provider with sha256_hex() -- one
    // provider open per process instead of one per hashed file.
    const BCRYPT_ALG_HANDLE algorithm = cached_sha256_provider();
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD object_size = 0;
    DWORD returned = 0;
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
                          reinterpret_cast<PUCHAR>(&object_size), sizeof(object_size),
                          &returned, 0) < 0) {
        throw std::runtime_error("BCrypt file SHA-256 initialization failed");
    }
    std::vector<std::uint8_t> object(object_size);
    if (BCryptCreateHash(algorithm, &hash, object.data(), object_size,
                         nullptr, 0, 0) < 0) {
        throw std::runtime_error("BCrypt file SHA-256 hash creation failed");
    }
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto count = input.gcount();
        if (count > 0 &&
            BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()),
                           static_cast<ULONG>(count), 0) < 0) {
            BCryptDestroyHash(hash);
            throw std::runtime_error("BCrypt file SHA-256 update failed");
        }
        if (count > 0 && progress && !size_error) {
            bytes_hashed += static_cast<std::uint64_t>(count);
            progress(bytes_hashed, total_bytes);
        }
    }
    if (!input.eof() ||
        BCryptFinishHash(hash, digest.data(),
                         static_cast<ULONG>(digest.size()), 0) < 0) {
        BCryptDestroyHash(hash);
        throw std::runtime_error("BCrypt file SHA-256 completion failed");
    }
    BCryptDestroyHash(hash);
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
        if (count > 0 && progress && !size_error) {
            bytes_hashed += static_cast<std::uint64_t>(count);
            progress(bytes_hashed, total_bytes);
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

namespace {

// OWASP-recommended floor for PBKDF2-HMAC-SHA256 as of this writing; local
// accounts are the only credential MasterAI stores, so this cost applies
// only there, never to OS-verified sign-in.
constexpr unsigned int kPasswordHashIterations = 210000U;
constexpr std::size_t kPasswordSaltBytes = 16U;
constexpr std::size_t kPasswordHashBytes = 32U;

std::string hex_decode(const std::string& text) {
    if (text.empty() || text.size() % 2U != 0U) {
        throw std::invalid_argument("hex value has an invalid length");
    }
    std::string result(text.size() / 2U, '\0');
    for (std::size_t index = 0; index < result.size(); ++index) {
        const auto nibble = [](const char character) -> unsigned int {
            if (character >= '0' && character <= '9') {
                return static_cast<unsigned int>(character - '0');
            }
            if (character >= 'a' && character <= 'f') {
                return static_cast<unsigned int>(character - 'a') + 10U;
            }
            throw std::invalid_argument("hex value has an invalid digit");
        };
        result[index] = static_cast<char>(
            (nibble(text[index * 2U]) << 4U) | nibble(text[index * 2U + 1U]));
    }
    return result;
}

#if defined(__linux__)
// One HMAC-SHA256 operation over the Linux kernel crypto API, used only to
// assemble PBKDF2 below (there is no kernel PBKDF2 primitive to call directly).
std::array<std::uint8_t, 32> hmac_sha256_linux(
    const std::vector<std::uint8_t>& key,
    const std::vector<std::uint8_t>& message) {
    const int algorithm_socket = socket(AF_ALG, SOCK_SEQPACKET, 0);
    if (algorithm_socket < 0) {
        throw std::runtime_error("Linux AF_ALG HMAC-SHA256 socket failed");
    }
    sockaddr_alg address{};
    address.salg_family = AF_ALG;
    std::strncpy(reinterpret_cast<char*>(address.salg_type), "hash",
                sizeof(address.salg_type) - 1U);
    std::strncpy(reinterpret_cast<char*>(address.salg_name), "hmac(sha256)",
                sizeof(address.salg_name) - 1U);
    if (bind(algorithm_socket, reinterpret_cast<sockaddr*>(&address),
             sizeof(address)) != 0) {
        close(algorithm_socket);
        throw std::runtime_error("Linux AF_ALG HMAC-SHA256 bind failed");
    }
    if (setsockopt(algorithm_socket, SOL_ALG, ALG_SET_KEY, key.data(),
                   static_cast<socklen_t>(key.size())) != 0) {
        close(algorithm_socket);
        throw std::runtime_error("Linux AF_ALG HMAC-SHA256 key setup failed");
    }
    const int operation = accept(algorithm_socket, nullptr, nullptr);
    if (operation < 0) {
        close(algorithm_socket);
        throw std::runtime_error("Linux AF_ALG HMAC-SHA256 accept failed");
    }
    std::array<std::uint8_t, 32> digest{};
    const ssize_t written = write(operation, message.data(), message.size());
    const ssize_t received = read(operation, digest.data(), digest.size());
    close(operation);
    close(algorithm_socket);
    if (written != static_cast<ssize_t>(message.size()) ||
        received != static_cast<ssize_t>(digest.size())) {
        throw std::runtime_error("Linux AF_ALG HMAC-SHA256 operation failed");
    }
    return digest;
}
#endif

// RFC 8018 PBKDF2-HMAC-SHA256, restricted to a single derived block (32
// bytes), which is all a SHA-256-sized key ever needs.
std::vector<std::uint8_t> pbkdf2_hmac_sha256_32(
    const std::string& password, const std::vector<std::uint8_t>& salt,
    const unsigned int iterations) {
#if defined(_WIN32)
    // Uses the process-lifetime cached HMAC provider (see
    // cached_hmac_sha256_provider) instead of opening/closing one per
    // password check.
    const BCRYPT_ALG_HANDLE algorithm = cached_hmac_sha256_provider();
    std::vector<std::uint8_t> derived(kPasswordHashBytes);
    const NTSTATUS status = BCryptDeriveKeyPBKDF2(
        algorithm,
        reinterpret_cast<PUCHAR>(const_cast<char*>(password.data())),
        static_cast<ULONG>(password.size()),
        const_cast<PUCHAR>(salt.data()), static_cast<ULONG>(salt.size()),
        iterations, derived.data(), static_cast<ULONG>(derived.size()), 0);
    if (status < 0) throw std::runtime_error("BCrypt PBKDF2 derivation failed");
    return derived;
#elif defined(__linux__)
    const std::vector<std::uint8_t> key(password.begin(), password.end());
    std::vector<std::uint8_t> block(salt);
    block.push_back(0U);
    block.push_back(0U);
    block.push_back(0U);
    block.push_back(1U);  // INT(1), big-endian, per RFC 8018.
    auto u = hmac_sha256_linux(key, block);
    std::array<std::uint8_t, 32> t = u;
    for (unsigned int round = 1U; round < iterations; ++round) {
        u = hmac_sha256_linux(key, std::vector<std::uint8_t>(u.begin(), u.end()));
        for (std::size_t index = 0; index < t.size(); ++index) t[index] ^= u[index];
    }
    return std::vector<std::uint8_t>(t.begin(), t.end());
#endif
}

}  // namespace

std::string hash_password(std::string password) {
    const auto salt = secure_random(kPasswordSaltBytes);
    const auto derived =
        pbkdf2_hmac_sha256_32(password, salt, kPasswordHashIterations);
    std::fill(password.begin(), password.end(), '\0');
    return "pbkdf2-sha256$" + std::to_string(kPasswordHashIterations) + "$" +
           hex_encode(salt.data(), salt.size()) + "$" +
           hex_encode(derived.data(), derived.size());
}

bool verify_password(std::string password, const std::string& encoded_hash) {
    const auto first = encoded_hash.find('$');
    const auto second =
        first == std::string::npos ? std::string::npos : encoded_hash.find('$', first + 1U);
    const auto third =
        second == std::string::npos ? std::string::npos : encoded_hash.find('$', second + 1U);
    if (first == std::string::npos || second == std::string::npos ||
        third == std::string::npos ||
        encoded_hash.compare(0U, first, "pbkdf2-sha256") != 0) {
        std::fill(password.begin(), password.end(), '\0');
        return false;
    }
    try {
        const auto iterations = static_cast<unsigned int>(
            std::stoul(encoded_hash.substr(first + 1U, second - first - 1U)));
        const auto salt_hex = encoded_hash.substr(second + 1U, third - second - 1U);
        const auto expected_hex = encoded_hash.substr(third + 1U);
        if (iterations == 0U || iterations > 5000000U || salt_hex.empty() ||
            expected_hex.size() != kPasswordHashBytes * 2U) {
            std::fill(password.begin(), password.end(), '\0');
            return false;
        }
        const auto decoded_salt = hex_decode(salt_hex);
        const std::vector<std::uint8_t> salt(decoded_salt.begin(), decoded_salt.end());
        const auto derived = pbkdf2_hmac_sha256_32(password, salt, iterations);
        std::fill(password.begin(), password.end(), '\0');
        return constant_time_equal(hex_encode(derived.data(), derived.size()),
                                   expected_hex);
    } catch (const std::exception&) {
        std::fill(password.begin(), password.end(), '\0');
        return false;
    }
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
    std::lock_guard<std::mutex> lock(mutex_);
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
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(sha256_hex(token));
    if (found == sessions_.end() || found->second.revoked ||
        now_epoch_seconds >= found->second.expires_at_epoch_seconds) {
        return std::nullopt;
    }
    return found->second;
}

void SessionStore::revoke(const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = sessions_.find(sha256_hex(token));
    if (found != sessions_.end()) {
        found->second.revoked = true;
        persist(found->first, found->second);
    }
}

// Not itself locked: it only calls the other public (independently locked)
// methods below and never touches sessions_ directly, so locking here too
// would deadlock on this class's non-recursive mutex_.
std::string SessionStore::rotate(const std::string& token,
                                 const std::uint64_t now_epoch_seconds,
                                 const std::uint64_t lifetime_seconds) {
    const auto session = validate(token, now_epoch_seconds);
    if (!session) throw std::invalid_argument("session cannot be rotated");
    revoke(token);
    return create(session->user_id, now_epoch_seconds, lifetime_seconds);
}

void SessionStore::revoke_user(const std::string& user_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& item : sessions_) {
        if (item.second.user_id == user_id && !item.second.revoked) {
            item.second.revoked = true;
            persist(item.first, item.second);
        }
    }
}

}  // namespace masterai
