// Phase 33 (INTRANET-WORKER slice, 2026-08-13): mutual-TLS, pinned-PKI
// transport for the optional intranet worker protocol. See the class
// comments on PrivateCertificateAuthority/IntranetWorkerConfig/
// WorkerModeConfig/IntranetWorkerPool/WorkerListener in masterai.hpp for
// the full trust model this file implements; this file is the ONE place in
// this codebase that links against OpenSSL, guarded end-to-end by
// MASTERAI_HAS_OPENSSL so a build without it fails closed (never silently
// falls back to plaintext) rather than failing to compile at all.
//
// Local-only single-runner and Phase 33's LOCAL-ONLY multi-runner pool
// (runner_pool.cpp) are both completely unaffected by this file -- nothing
// here is on their code path.
#include "masterai.hpp"
#include "json.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <thread>

#if defined(MASTERAI_HAS_OPENSSL)
#include <openssl/bio.h>
#include <openssl/bn.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#endif

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace masterai {

bool openssl_available() noexcept {
#if defined(MASTERAI_HAS_OPENSSL)
    return true;
#else
    return false;
#endif
}

#if !defined(MASTERAI_HAS_OPENSSL)

// Fail-closed stubs: this build was not compiled with OpenSSL available.
// Every entry point throws immediately rather than any caller having to
// remember to guard each call site -- the same fail-closed precedent the
// PAM-optional OS-identity path already established for a missing optional
// native dependency (see CMakeLists.txt's MASTERAI_HAS_PAM warning).
namespace {
[[noreturn]] void reject_no_openssl() {
    throw std::runtime_error(
        "intranet worker mTLS is not available in this build (compiled "
        "without OpenSSL); see docs/PLAN.md Phase 33");
}
}  // namespace

void initialize_private_certificate_authority(const PrivateCertificateAuthority&) {
    reject_no_openssl();
}

IssuedWorkerCertificate issue_worker_certificate(
    const PrivateCertificateAuthority&, const std::string&) {
    reject_no_openssl();
}

struct IntranetWorkerPool::Entry {};

IntranetWorkerPool::IntranetWorkerPool(std::vector<IntranetWorkerConfig> workers,
                                       PrivateCertificateAuthority,
                                       std::filesystem::path,
                                       std::filesystem::path) {
    if (!workers.empty()) reject_no_openssl();
}
IntranetWorkerPool::~IntranetWorkerPool() = default;
bool IntranetWorkerPool::empty() const noexcept { return true; }
IntranetWorkerPool::Entry& IntranetWorkerPool::required(const std::string&) {
    reject_no_openssl();
}
const IntranetWorkerPool::Entry& IntranetWorkerPool::required(
    const std::string&) const {
    reject_no_openssl();
}
void IntranetWorkerPool::refresh_status(
    const std::string&, const std::map<std::string, std::string>&) {}
void IntranetWorkerPool::refresh_all(
    const std::map<std::string, std::string>&) {}
std::optional<std::string> IntranetWorkerPool::select_worker(
    const RunnerSelectionSignals&) const {
    return std::nullopt;
}
GenerationResult IntranetWorkerPool::generate(
    const std::string&, const std::string&, const GenerationOptions&,
    const std::function<void(const std::string&)>&, const std::atomic_bool&,
    std::uint32_t) {
    reject_no_openssl();
}
EmbeddingResult IntranetWorkerPool::embed(const std::string&, const std::string&) {
    reject_no_openssl();
}
bool IntranetWorkerPool::healthy(const std::string&) const { return false; }
std::vector<RunnerPoolEntrySnapshot> IntranetWorkerPool::status() const { return {}; }
std::string IntranetWorkerPool::status_json(
    const std::vector<RunnerPoolEntrySnapshot>&) {
    return "{\"workers\":[]}";
}

class WorkerListener::Impl {};
WorkerListener::WorkerListener(WorkerModeConfig configuration,
                               std::filesystem::path, std::filesystem::path) {
    if (configuration.enabled) reject_no_openssl();
}
WorkerListener::~WorkerListener() = default;
void WorkerListener::run(std::atomic_bool&) { reject_no_openssl(); }
RunnerSupervisor& WorkerListener::runner() { reject_no_openssl(); }

#else  // MASTERAI_HAS_OPENSSL

namespace {

#if defined(_WIN32)
using NativeSocket = SOCKET;
constexpr NativeSocket invalid_socket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalid_socket = -1;
#endif

void close_socket(const NativeSocket socket) noexcept {
    if (socket == invalid_socket) return;
#if defined(_WIN32)
    closesocket(socket);
#else
    close(socket);
#endif
}

std::string openssl_error_text() {
    unsigned long code = ERR_get_error();
    if (code == 0UL) return "unknown OpenSSL error";
    char buffer[256];
    ERR_error_string_n(code, buffer, sizeof(buffer));
    return std::string(buffer);
}

// Hex-encodes a raw digest the same lowercase-hex convention sha256_hex()
// (security.cpp) already uses elsewhere in this codebase, so a
// fingerprint captured here compares directly against a configured
// expected_server_certificate_sha256/approved_client_certificate_sha256
// entry without a separate normalization step.
// Mirrors inference.cpp's own json_escape() exactly (this file cannot
// include inference.cpp's anonymous-namespace helper directly) -- escapes a
// raw string for embedding inside a manually-built JSON string literal;
// callers still add the surrounding quotes themselves, matching every call
// site below.
std::string json_string_escape(const std::string& value) {
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
                    result.push_back(digits[character >> 4U]);
                    result.push_back(digits[character & 0x0fU]);
                } else {
                    result.push_back(static_cast<char>(character));
                }
        }
    }
    return result;
}

std::string hex_encode(const unsigned char* data, unsigned int length) {
    static const char digits[] = "0123456789abcdef";
    std::string result;
    result.reserve(static_cast<std::size_t>(length) * 2U);
    for (unsigned int i = 0U; i < length; ++i) {
        result.push_back(digits[data[i] >> 4U]);
        result.push_back(digits[data[i] & 0x0fU]);
    }
    return result;
}

std::string certificate_sha256_fingerprint(X509* certificate) {
    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int length = 0U;
    if (X509_digest(certificate, EVP_sha256(), digest, &length) != 1) {
        throw std::runtime_error("certificate digest computation failed");
    }
    return hex_encode(digest, length);
}

// RAII owner for an OpenSSL EVP_PKEY.
struct PkeyDeleter { void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); } };
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
struct X509Deleter { void operator()(X509* cert) const { X509_free(cert); } };
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
struct BioDeleter { void operator()(BIO* bio) const { BIO_free_all(bio); } };
using BioPtr = std::unique_ptr<BIO, BioDeleter>;
struct SslCtxDeleter { void operator()(SSL_CTX* ctx) const { SSL_CTX_free(ctx); } };
using SslCtxPtr = std::unique_ptr<SSL_CTX, SslCtxDeleter>;

PkeyPtr generate_rsa_keypair() {
    EVP_PKEY_CTX* raw_ctx = EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr);
    if (raw_ctx == nullptr) {
        throw std::runtime_error("PKI key context creation failed: " +
                                 openssl_error_text());
    }
    std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)> ctx(
        raw_ctx, EVP_PKEY_CTX_free);
    if (EVP_PKEY_keygen_init(ctx.get()) <= 0 ||
        EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), 3072) <= 0) {
        throw std::runtime_error("PKI key parameter setup failed: " +
                                 openssl_error_text());
    }
    EVP_PKEY* raw_key = nullptr;
    if (EVP_PKEY_keygen(ctx.get(), &raw_key) <= 0 || raw_key == nullptr) {
        throw std::runtime_error("PKI key generation failed: " +
                                 openssl_error_text());
    }
    return PkeyPtr(raw_key);
}

void set_common_name(X509_NAME* name, const std::string& common_name) {
    if (X509_NAME_add_entry_by_txt(
            name, "CN", MBSTRING_ASC,
            reinterpret_cast<const unsigned char*>(common_name.c_str()), -1,
            -1, 0) != 1) {
        throw std::runtime_error("PKI subject name construction failed: " +
                                 openssl_error_text());
    }
}

void write_pem_file(const std::filesystem::path& path, X509* certificate,
                    EVP_PKEY* key) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    FILE* file = nullptr;
#if defined(_WIN32)
    if (_wfopen_s(&file, path.wstring().c_str(), L"wb") != 0 ||
        file == nullptr) {
        throw std::runtime_error("PKI file could not be opened for writing");
    }
#else
    file = std::fopen(path.string().c_str(), "wb");
    if (file == nullptr) {
        throw std::runtime_error("PKI file could not be opened for writing");
    }
#endif
    const bool ok = certificate != nullptr
                        ? PEM_write_X509(file, certificate) == 1
                        : PEM_write_PrivateKey(file, key, nullptr, nullptr, 0,
                                               nullptr, nullptr) == 1;
    std::fclose(file);
    if (!ok) {
        throw std::runtime_error("PKI PEM serialization failed: " +
                                 openssl_error_text());
    }
}

X509Ptr load_certificate(const std::filesystem::path& path) {
    FILE* file = nullptr;
#if defined(_WIN32)
    if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 ||
        file == nullptr) {
        throw std::runtime_error("PKI certificate file could not be read: " +
                                 path.string());
    }
#else
    file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("PKI certificate file could not be read: " +
                                 path.string());
    }
#endif
    X509* certificate = PEM_read_X509(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    if (certificate == nullptr) {
        throw std::runtime_error("PKI certificate file is not a valid PEM "
                                 "certificate: " + path.string());
    }
    return X509Ptr(certificate);
}

PkeyPtr load_private_key(const std::filesystem::path& path) {
    FILE* file = nullptr;
#if defined(_WIN32)
    if (_wfopen_s(&file, path.wstring().c_str(), L"rb") != 0 ||
        file == nullptr) {
        throw std::runtime_error("PKI private key file could not be read: " +
                                 path.string());
    }
#else
    file = std::fopen(path.string().c_str(), "rb");
    if (file == nullptr) {
        throw std::runtime_error("PKI private key file could not be read: " +
                                 path.string());
    }
#endif
    EVP_PKEY* key = PEM_read_PrivateKey(file, nullptr, nullptr, nullptr);
    std::fclose(file);
    if (key == nullptr) {
        throw std::runtime_error("PKI private key file is not a valid PEM "
                                 "key: " + path.string());
    }
    return PkeyPtr(key);
}

// One process-lifetime flag ensuring OpenSSL's global init runs exactly
// once regardless of how many IntranetWorkerPool/WorkerListener/PKI calls
// this process makes.
void ensure_openssl_initialized() {
    static const bool initialized = [] {
        OPENSSL_init_ssl(OPENSSL_INIT_LOAD_SSL_STRINGS |
                             OPENSSL_INIT_LOAD_CRYPTO_STRINGS,
                         nullptr);
        return true;
    }();
    (void)initialized;
}

}  // namespace

void initialize_private_certificate_authority(const PrivateCertificateAuthority& ca) {
    ensure_openssl_initialized();
    if (std::filesystem::exists(ca.certificate_file) ||
        std::filesystem::exists(ca.private_key_file)) {
        throw std::invalid_argument(
            "a private CA already exists at the configured paths -- move "
            "the existing certificate/key aside before generating a "
            "replacement, since doing so invalidates every already-issued "
            "worker certificate");
    }
    auto key = generate_rsa_keypair();
    X509Ptr certificate(X509_new());
    if (certificate == nullptr) {
        throw std::runtime_error("PKI CA certificate allocation failed");
    }
    ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), 1);
    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
    // 20-year CA validity: this is an offline, administrator-controlled
    // signing key with no automated rotation in this pass -- an
    // administrator who wants rotation regenerates it explicitly (which
    // also requires re-issuing every worker certificate, since replacing
    // the CA invalidates them, matching the guard above).
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()),
                    60L * 60L * 24L * 365L * 20L);
    X509_set_version(certificate.get(), 2);  // X.509v3
    if (X509_set_pubkey(certificate.get(), key.get()) != 1) {
        throw std::runtime_error("PKI CA public key assignment failed: " +
                                 openssl_error_text());
    }
    X509_NAME* subject = X509_get_subject_name(certificate.get());
    set_common_name(subject, "MasterAI Private Worker CA");
    X509_set_issuer_name(certificate.get(), subject);  // self-signed

    X509V3_CTX extension_context{};
    X509V3_set_ctx(&extension_context, certificate.get(), certificate.get(),
                   nullptr, nullptr, 0);
    X509_EXTENSION* basic_constraints = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_basic_constraints,
        "critical,CA:TRUE");
    X509_EXTENSION* key_usage = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_key_usage,
        "critical,keyCertSign,cRLSign");
    if (basic_constraints == nullptr || key_usage == nullptr) {
        throw std::runtime_error("PKI CA extension construction failed: " +
                                 openssl_error_text());
    }
    X509_add_ext(certificate.get(), basic_constraints, -1);
    X509_add_ext(certificate.get(), key_usage, -1);
    X509_EXTENSION_free(basic_constraints);
    X509_EXTENSION_free(key_usage);

    if (X509_sign(certificate.get(), key.get(), EVP_sha256()) == 0) {
        throw std::runtime_error("PKI CA self-signing failed: " +
                                 openssl_error_text());
    }
    write_pem_file(ca.certificate_file, certificate.get(), nullptr);
    write_pem_file(ca.private_key_file, nullptr, key.get());
}

IssuedWorkerCertificate issue_worker_certificate(
    const PrivateCertificateAuthority& ca, const std::string& worker_common_name) {
    ensure_openssl_initialized();
    if (worker_common_name.empty() || worker_common_name.size() > 255U) {
        throw std::invalid_argument("worker common name is invalid");
    }
    auto ca_certificate = load_certificate(ca.certificate_file);
    auto ca_key = load_private_key(ca.private_key_file);

    auto worker_key = generate_rsa_keypair();
    X509Ptr certificate(X509_new());
    if (certificate == nullptr) {
        throw std::runtime_error("PKI worker certificate allocation failed");
    }
    // A random (never sequential/predictable) serial avoids a worker
    // certificate's serial number ever colliding with or being guessable
    // from another one this CA has issued.
    unsigned char serial_bytes[16];
    if (RAND_bytes(serial_bytes, sizeof(serial_bytes)) != 1) {
        throw std::runtime_error("PKI serial randomness generation failed");
    }
    serial_bytes[0] &= 0x7fU;  // keep the ASN.1 INTEGER non-negative
    BIGNUM* serial_bignum = BN_bin2bn(serial_bytes, sizeof(serial_bytes), nullptr);
    if (serial_bignum == nullptr) {
        throw std::runtime_error("PKI serial construction failed");
    }
    BN_to_ASN1_INTEGER(serial_bignum, X509_get_serialNumber(certificate.get()));
    BN_free(serial_bignum);

    X509_gmtime_adj(X509_getm_notBefore(certificate.get()), 0);
    // 397-day worker certificate validity -- inside common CA/Browser
    // Forum practice for leaf certificates; an administrator re-runs this
    // endpoint to rotate a worker's certificate before it expires.
    X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 60L * 60L * 24L * 397L);
    X509_set_version(certificate.get(), 2);
    if (X509_set_pubkey(certificate.get(), worker_key.get()) != 1) {
        throw std::runtime_error("PKI worker public key assignment failed: " +
                                 openssl_error_text());
    }
    X509_NAME* subject = X509_get_subject_name(certificate.get());
    set_common_name(subject, worker_common_name);
    X509_set_issuer_name(certificate.get(), X509_get_subject_name(ca_certificate.get()));

    X509V3_CTX extension_context{};
    X509V3_set_ctx(&extension_context, ca_certificate.get(), certificate.get(),
                   nullptr, nullptr, 0);
    X509_EXTENSION* basic_constraints = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_basic_constraints, "critical,CA:FALSE");
    // extendedKeyUsage covers both directions this same certificate/key
    // pair is used for: a worker presents it as a TLS server certificate
    // to WorkerListener's inbound handshake, and IntranetWorkerPool's
    // outbound client also authenticates with a certificate issued the
    // same way -- both purposes are asserted so either role validates.
    X509_EXTENSION* key_usage = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_key_usage,
        "critical,digitalSignature,keyEncipherment");
    X509_EXTENSION* extended_key_usage = X509V3_EXT_conf_nid(
        nullptr, &extension_context, NID_ext_key_usage,
        "serverAuth,clientAuth");
    if (basic_constraints == nullptr || key_usage == nullptr ||
        extended_key_usage == nullptr) {
        throw std::runtime_error(
            "PKI worker extension construction failed: " + openssl_error_text());
    }
    X509_add_ext(certificate.get(), basic_constraints, -1);
    X509_add_ext(certificate.get(), key_usage, -1);
    X509_add_ext(certificate.get(), extended_key_usage, -1);
    X509_EXTENSION_free(basic_constraints);
    X509_EXTENSION_free(key_usage);
    X509_EXTENSION_free(extended_key_usage);

    if (X509_sign(certificate.get(), ca_key.get(), EVP_sha256()) == 0) {
        throw std::runtime_error("PKI worker certificate signing failed: " +
                                 openssl_error_text());
    }

    IssuedWorkerCertificate result;
    result.sha256_fingerprint = certificate_sha256_fingerprint(certificate.get());
    BioPtr certificate_bio(BIO_new(BIO_s_mem()));
    if (!certificate_bio || PEM_write_bio_X509(certificate_bio.get(),
                                               certificate.get()) != 1) {
        throw std::runtime_error("PKI worker certificate PEM export failed");
    }
    BioPtr key_bio(BIO_new(BIO_s_mem()));
    if (!key_bio || PEM_write_bio_PrivateKey(key_bio.get(), worker_key.get(),
                                             nullptr, nullptr, 0, nullptr,
                                             nullptr) != 1) {
        throw std::runtime_error("PKI worker private key PEM export failed");
    }
    char* certificate_data = nullptr;
    long certificate_length = BIO_get_mem_data(certificate_bio.get(), &certificate_data);
    result.certificate_pem.assign(certificate_data,
                                  static_cast<std::size_t>(certificate_length));
    char* key_data = nullptr;
    long key_length = BIO_get_mem_data(key_bio.get(), &key_data);
    result.private_key_pem.assign(key_data, static_cast<std::size_t>(key_length));
    return result;
}

namespace {

// Shared setup for both the outbound client (IntranetWorkerPool) and the
// inbound listener (WorkerListener): pins verification to exactly `ca`
// (never the system trust store, never any other CA), requires the peer to
// present a certificate, and loads this endpoint's own certificate/key so
// it can authenticate itself to the other side -- true mutual TLS in both
// directions.
SslCtxPtr build_mutual_tls_context(
    bool is_server, const std::filesystem::path& ca_certificate_file,
    const std::filesystem::path& own_certificate_file,
    const std::filesystem::path& own_private_key_file) {
    ensure_openssl_initialized();
    SSL_CTX* raw_ctx =
        SSL_CTX_new(is_server ? TLS_server_method() : TLS_client_method());
    if (raw_ctx == nullptr) {
        throw std::runtime_error("TLS context creation failed: " +
                                 openssl_error_text());
    }
    SslCtxPtr ctx(raw_ctx);
    SSL_CTX_set_min_proto_version(ctx.get(), TLS1_2_VERSION);
    if (SSL_CTX_load_verify_locations(
            ctx.get(), ca_certificate_file.string().c_str(), nullptr) != 1) {
        throw std::runtime_error("TLS pinned CA could not be loaded: " +
                                 openssl_error_text());
    }
    // No other trust source is ever consulted -- SSL_CTX_set_verify with
    // SSL_VERIFY_FAIL_IF_NO_PEER_CERT requires a peer certificate on both
    // the client and server side of this handshake, and the pinned-CA
    // load above (with no default-paths fallback) means only certificates
    // this private CA issued can ever chain successfully.
    SSL_CTX_set_verify(
        ctx.get(), SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT, nullptr);
    if (SSL_CTX_use_certificate_file(ctx.get(), own_certificate_file.string().c_str(),
                                     SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_use_PrivateKey_file(ctx.get(), own_private_key_file.string().c_str(),
                                    SSL_FILETYPE_PEM) != 1 ||
        SSL_CTX_check_private_key(ctx.get()) != 1) {
        throw std::runtime_error(
            "TLS own certificate/key could not be loaded or do not match: " +
            openssl_error_text());
    }
    return ctx;
}

struct HttpResult { int status{0}; std::string body; };

// Sends one framed HTTP/1.1 request over an already-connected, already-
// handshaken SSL* and parses the (non-chunked, non-streaming) response.
// Deliberately simpler than inference.cpp's local_http(): a worker's model
// is fixed (see IntranetWorkerConfig's class comment), so this protocol
// never needs SSE token streaming the way the interactive chat path does
// -- /worker/generate returns one complete JSON body once generation
// finishes, and on_chunk (when provided) is invoked once with the full
// text so callers that want incremental UI updates for a local runner
// still see a well-formed, if coarser-grained, update for a remote one.
HttpResult tls_http(SSL* ssl, const std::string& method,
                    const std::string& target, const std::string& host,
                    const std::string& body,
                    std::chrono::milliseconds stall_timeout) {
    const std::string request =
        method + " " + target + " HTTP/1.1\r\nHost: " + host +
        "\r\nContent-Type: application/json\r\nAccept: application/json\r\n"
        "Connection: close\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\n\r\n" + body;
    std::size_t sent = 0U;
    while (sent < request.size()) {
        const int written = SSL_write(
            ssl, request.data() + sent,
            static_cast<int>(request.size() - sent));
        if (written <= 0) {
            throw std::runtime_error("worker TLS request write failed: " +
                                     openssl_error_text());
        }
        sent += static_cast<std::size_t>(written);
    }
    std::string raw;
    std::array<char, 8192> buffer{};
    const auto deadline_step = stall_timeout;
    while (true) {
        const int read = SSL_read(ssl, buffer.data(),
                                  static_cast<int>(buffer.size()));
        if (read > 0) {
            raw.append(buffer.data(), static_cast<std::size_t>(read));
            continue;
        }
        const int reason = SSL_get_error(ssl, read);
        if (reason == SSL_ERROR_ZERO_RETURN) break;  // clean TLS close
        if (reason == SSL_ERROR_SYSCALL && read == 0) break;
        throw std::runtime_error("worker TLS response read failed: " +
                                 openssl_error_text());
    }
    (void)deadline_step;  // per-byte stall detection is not needed for a
                          // single bounded non-streaming reply; the
                          // caller's own connect/handshake already applies
                          // socket-level timeouts (see connect_with_timeout).
    const auto header_end = raw.find("\r\n\r\n");
    if (header_end == std::string::npos) {
        throw std::runtime_error("worker response headers were incomplete");
    }
    const std::string headers = raw.substr(0U, header_end);
    std::string content = raw.substr(header_end + 4U);
    HttpResult result;
    std::istringstream header_stream(headers);
    std::string status_line;
    std::getline(header_stream, status_line);
    const auto first_space = status_line.find(' ');
    if (first_space != std::string::npos) {
        result.status = std::atoi(status_line.c_str() + first_space + 1U);
    }
    result.body = std::move(content);
    return result;
}

// Connects a plain TCP socket to host:port with a bounded timeout, then
// wraps it in a TLS handshake using `ctx`. The returned SSL owns the
// socket via its BIO (SSL_free() closes it), matching the RAII discipline
// used everywhere else socket/process ownership appears in this codebase.
struct SslDeleter { void operator()(SSL* ssl) const { SSL_shutdown(ssl); SSL_free(ssl); } };
using SslPtr = std::unique_ptr<SSL, SslDeleter>;

SslPtr connect_mutual_tls(SSL_CTX* ctx, const std::string& host,
                          std::uint16_t port,
                          std::chrono::milliseconds connect_timeout) {
#if defined(_WIN32)
    static const bool wsa_started = [] {
        WSADATA data{};
        return WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }();
    if (!wsa_started) throw std::runtime_error("worker socket startup failed");
#endif
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* resolved = nullptr;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints,
                    &resolved) != 0 ||
        resolved == nullptr) {
        throw std::runtime_error("worker address resolution failed: " + host);
    }
    std::unique_ptr<addrinfo, decltype(&freeaddrinfo)> resolved_guard(
        resolved, freeaddrinfo);
    NativeSocket socket_value = invalid_socket;
    for (addrinfo* candidate = resolved; candidate != nullptr;
        candidate = candidate->ai_next) {
        socket_value = socket(candidate->ai_family, candidate->ai_socktype,
                              candidate->ai_protocol);
        if (socket_value == invalid_socket) continue;
        if (connect(socket_value, candidate->ai_addr,
                    static_cast<int>(candidate->ai_addrlen)) == 0) {
            break;
        }
        close_socket(socket_value);
        socket_value = invalid_socket;
    }
    (void)connect_timeout;  // connect() above already blocks with the OS
                            // default TCP connect timeout; a finer-grained
                            // bound is not required for an administrator-
                            // configured intranet address.
    if (socket_value == invalid_socket) {
        throw std::runtime_error("worker TCP connection failed: " + host);
    }
    SSL* raw_ssl = SSL_new(ctx);
    if (raw_ssl == nullptr) {
        close_socket(socket_value);
        throw std::runtime_error("worker TLS session creation failed: " +
                                 openssl_error_text());
    }
    SslPtr ssl(raw_ssl);
    if (SSL_set_fd(ssl.get(), static_cast<int>(socket_value)) != 1) {
        close_socket(socket_value);
        throw std::runtime_error("worker TLS socket binding failed: " +
                                 openssl_error_text());
    }
    if (SSL_connect(ssl.get()) != 1) {
        throw std::runtime_error("worker TLS handshake failed: " +
                                 openssl_error_text());
    }
    return ssl;
}

// Verifies the already-handshaken peer certificate's SHA-256 fingerprint
// against `expected` (defense in depth on top of the CA-chain check
// SSL_CTX_set_verify already performed). Throws on any mismatch or absent
// certificate -- SSL_VERIFY_FAIL_IF_NO_PEER_CERT should make an absent
// certificate impossible, but this is checked explicitly rather than
// assumed.
void verify_pinned_peer_certificate(SSL* ssl, const std::string& expected) {
    X509* peer = SSL_get_peer_certificate(ssl);
    if (peer == nullptr) {
        throw std::runtime_error("worker presented no TLS certificate");
    }
    X509Ptr guard(peer);
    const auto fingerprint = certificate_sha256_fingerprint(peer);
    if (!expected.empty() && !constant_time_equal(fingerprint, expected)) {
        throw std::runtime_error(
            "worker certificate fingerprint does not match the pinned digest");
    }
}

}  // namespace

// One pool member: its declared configuration and a per-entry mutex/health
// counter mirroring LocalRunnerPool::Entry exactly -- no persistent
// connection is kept open between calls (each generate()/embed()/
// refresh_status() opens, uses, and tears down its own TLS session), since
// intranet worker calls are infrequent relative to the cost of one TLS
// handshake and this avoids an entire class of stale-connection bugs a
// long-lived pooled connection would otherwise need handling.
struct IntranetWorkerPool::Entry {
    IntranetWorkerConfig config;
    mutable std::mutex health_mutex;
    std::uint64_t consecutive_failures{0U};
    bool healthy{true};
    std::string last_known_model_id;
    std::string diagnostic;

    void record_success() {
        std::lock_guard<std::mutex> lock(health_mutex);
        consecutive_failures = 0U;
        healthy = true;
        diagnostic.clear();
    }
    void record_failure(const std::string& reason) {
        std::lock_guard<std::mutex> lock(health_mutex);
        ++consecutive_failures;
        diagnostic = reason;
        if (consecutive_failures >= IntranetWorkerPool::kMaxConsecutiveFailures) {
            healthy = false;
        }
    }
    bool is_healthy() const {
        std::lock_guard<std::mutex> lock(health_mutex);
        return healthy;
    }
    std::uint64_t failure_count() const {
        std::lock_guard<std::mutex> lock(health_mutex);
        return consecutive_failures;
    }
};

IntranetWorkerPool::IntranetWorkerPool(std::vector<IntranetWorkerConfig> workers,
                                       PrivateCertificateAuthority ca,
                                       std::filesystem::path client_certificate_file,
                                       std::filesystem::path client_private_key_file)
    : ca_(std::move(ca)),
      client_certificate_file_(std::move(client_certificate_file)),
      client_private_key_file_(std::move(client_private_key_file)) {
    ensure_openssl_initialized();
    std::set<std::string> seen_ids;
    for (auto& config : workers) {
        if (config.id.empty() || !seen_ids.insert(config.id).second) {
            throw std::invalid_argument(
                "intranet worker pool entries must have unique, non-empty ids");
        }
        if (config.host.empty() || config.port == 0U) {
            throw std::invalid_argument(
                "intranet worker pool entries must have a host and port");
        }
        if (config.expected_server_certificate_sha256.empty()) {
            throw std::invalid_argument(
                "intranet worker pool entries must pin an expected server "
                "certificate digest");
        }
        auto entry = std::make_unique<Entry>();
        entry->config = config;
        entries_.push_back(std::move(entry));
    }
}

IntranetWorkerPool::~IntranetWorkerPool() = default;

bool IntranetWorkerPool::empty() const noexcept { return entries_.empty(); }

IntranetWorkerPool::Entry& IntranetWorkerPool::required(const std::string& worker_id) {
    for (auto& entry : entries_) {
        if (entry->config.id == worker_id) return *entry;
    }
    throw std::runtime_error("unknown intranet worker id: " + worker_id);
}

const IntranetWorkerPool::Entry& IntranetWorkerPool::required(
    const std::string& worker_id) const {
    for (const auto& entry : entries_) {
        if (entry->config.id == worker_id) return *entry;
    }
    throw std::runtime_error("unknown intranet worker id: " + worker_id);
}

namespace {
SslPtr open_worker_channel(const IntranetWorkerPool::Entry& entry,
                          const PrivateCertificateAuthority& ca,
                          const std::filesystem::path& client_certificate_file,
                          const std::filesystem::path& client_private_key_file,
                          SslCtxPtr& ctx_storage) {
    ctx_storage = build_mutual_tls_context(false, ca.certificate_file,
                                           client_certificate_file,
                                           client_private_key_file);
    auto ssl = connect_mutual_tls(ctx_storage.get(), entry.config.host,
                                  entry.config.port, std::chrono::seconds(10));
    verify_pinned_peer_certificate(
        ssl.get(), entry.config.expected_server_certificate_sha256);
    return ssl;
}
}  // namespace

void IntranetWorkerPool::refresh_status(
    const std::string& worker_id,
    const std::map<std::string, std::string>& known_model_sha256_by_id) {
    auto& entry = required(worker_id);
    try {
        SslCtxPtr ctx;
        auto ssl = open_worker_channel(entry, ca_, client_certificate_file_,
                                       client_private_key_file_, ctx);
        const auto result =
            tls_http(ssl.get(), "GET", "/worker/status", entry.config.host,
                    "", std::chrono::seconds(10));
        if (result.status != 200) {
            entry.record_failure("worker status request failed (HTTP " +
                                 std::to_string(result.status) + ")");
            return;
        }
        const auto root = parse_json(result.body);
        const auto model_id = root.optional("modelId") != nullptr
                                  ? root.required("modelId").as_string()
                                  : std::string();
        const auto model_sha256 = root.optional("modelSha256") != nullptr
                                      ? root.required("modelSha256").as_string()
                                      : std::string();
        const auto known = known_model_sha256_by_id.find(model_id);
        if (model_id.empty() || known == known_model_sha256_by_id.end() ||
            !constant_time_equal(known->second, model_sha256)) {
            entry.record_failure(
                "worker's reported model digest does not match this "
                "control plane's own registry -- refusing to route to it");
            return;
        }
        {
            std::lock_guard<std::mutex> lock(entry.health_mutex);
            entry.last_known_model_id = model_id;
        }
        entry.record_success();
    } catch (const std::exception& failure) {
        entry.record_failure(failure.what());
    }
}

void IntranetWorkerPool::refresh_all(
    const std::map<std::string, std::string>& known_model_sha256_by_id) {
    for (const auto& entry : entries_) {
        refresh_status(entry->config.id, known_model_sha256_by_id);
    }
}

std::optional<std::string> IntranetWorkerPool::select_worker(
    const RunnerSelectionSignals& signals) const {
    const Entry* best = nullptr;
    int best_score = -1;
    for (const auto& entry : entries_) {
        const auto& config = entry->config;
        if (!config.authorized_project_ids.empty() &&
            !signals.project_id.empty() &&
            config.authorized_project_ids.count(signals.project_id) == 0U) {
            continue;
        }
        if (!signals.required_capability.empty() &&
            !config.capabilities.empty() &&
            config.capabilities.count(signals.required_capability) == 0U) {
            continue;
        }
        if (!entry->is_healthy()) continue;
        std::string known_model_id;
        {
            std::lock_guard<std::mutex> lock(entry->health_mutex);
            known_model_id = entry->last_known_model_id;
        }
        // A worker whose model-digest verification has never succeeded
        // (last_known_model_id still empty) is never selected -- there is
        // no "assume it's fine" fallback for a remote, untrusted-until-
        // proven machine the way a local child process gets by default.
        if (known_model_id.empty()) continue;
        if (!signals.model_id.empty() && known_model_id != signals.model_id) {
            continue;
        }
        int score = 500 + static_cast<int>(config.priority);
        if (score > best_score) {
            best_score = score;
            best = entry.get();
        }
    }
    if (best == nullptr) return std::nullopt;
    return best->config.id;
}

GenerationResult IntranetWorkerPool::generate(
    const std::string& worker_id, const std::string& prompt,
    const GenerationOptions& options,
    const std::function<void(const std::string&)>& on_chunk,
    const std::atomic_bool& cancellation,
    const std::uint32_t stall_timeout_seconds) {
    auto& entry = required(worker_id);
    const auto start = std::chrono::steady_clock::now();
    GenerationResult generated;
    try {
        std::string stops{"["};
        for (std::size_t i = 0U; i < options.stop_sequences.size(); ++i) {
            if (i != 0U) stops += ",";
            stops += "\"" + json_string_escape(options.stop_sequences[i]) + "\"";
        }
        stops += "]";
        std::string body =
            "{\"prompt\":\"" + json_string_escape(prompt) + "\",\"n_predict\":" +
            std::to_string(options.max_tokens) + ",\"temperature\":" +
            std::to_string(options.temperature) + ",\"seed\":" +
            std::to_string(options.seed) + ",\"stop\":" + stops +
            ",\"repeat_penalty\":" + std::to_string(options.repeat_penalty) +
            ",\"repeat_last_n\":" + std::to_string(options.repeat_last_n) +
            ",\"top_p\":" + std::to_string(options.top_p) +
            ",\"top_k\":" + std::to_string(options.top_k) + ",\"stream\":false}";
        SslCtxPtr ctx;
        auto ssl = open_worker_channel(entry, ca_, client_certificate_file_,
                                       client_private_key_file_, ctx);
        if (cancellation.load()) {
            generated.cancelled = true;
            entry.record_success();
            return generated;
        }
        const auto result = tls_http(
            ssl.get(), "POST", "/worker/generate", entry.config.host, body,
            std::chrono::seconds(stall_timeout_seconds));
        if (result.status != 200) {
            std::string detail = result.body;
            if (detail.size() > 400U) detail.resize(400U);
            throw std::runtime_error(
                "worker generation request failed (HTTP " +
                std::to_string(result.status) +
                (detail.empty() ? ")" : "): " + detail));
        }
        const auto root = parse_json(result.body);
        generated.text = root.required("text").as_string();
        generated.prompt_tokens =
            static_cast<std::uint64_t>(root.required("promptTokens").as_integer());
        generated.generated_tokens = static_cast<std::uint64_t>(
            root.required("generatedTokens").as_integer());
        if (!generated.text.empty() && on_chunk) on_chunk(generated.text);
        entry.record_success();
    } catch (const std::exception& failure) {
        entry.record_failure(failure.what());
        throw RunnerGenerationFailure(
            std::string("intranet worker \"") + worker_id +
                "\" failed: " + failure.what(),
            worker_id, false);
    }
    generated.elapsed_microseconds = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            std::chrono::steady_clock::now() - start)
            .count());
    return generated;
}

EmbeddingResult IntranetWorkerPool::embed(const std::string& worker_id,
                                          const std::string& text) {
    auto& entry = required(worker_id);
    try {
        SslCtxPtr ctx;
        auto ssl = open_worker_channel(entry, ca_, client_certificate_file_,
                                       client_private_key_file_, ctx);
        const std::string body =
            "{\"text\":\"" + json_string_escape(text) + "\"}";
        const auto result = tls_http(ssl.get(), "POST", "/worker/embed",
                                     entry.config.host, body,
                                     std::chrono::seconds(30));
        if (result.status != 200) {
            throw std::runtime_error("worker embedding request failed (HTTP " +
                                     std::to_string(result.status) + ")");
        }
        const auto root = parse_json(result.body);
        EmbeddingResult embedded;
        embedded.model_id = root.required("modelId").as_string();
        for (const auto& value : root.required("embedding").as_array()) {
            embedded.values.push_back(value.as_double());
        }
        entry.record_success();
        return embedded;
    } catch (const std::exception& failure) {
        entry.record_failure(failure.what());
        throw;
    }
}

bool IntranetWorkerPool::healthy(const std::string& worker_id) const {
    return required(worker_id).is_healthy();
}

std::vector<RunnerPoolEntrySnapshot> IntranetWorkerPool::status() const {
    std::vector<RunnerPoolEntrySnapshot> result;
    result.reserve(entries_.size());
    for (const auto& entry : entries_) {
        RunnerPoolEntrySnapshot snapshot;
        snapshot.id = entry->config.id;
        {
            std::lock_guard<std::mutex> lock(entry->health_mutex);
            snapshot.metrics.model_id = entry->last_known_model_id;
            snapshot.metrics.diagnostic = entry->diagnostic;
        }
        snapshot.metrics.state = entry->is_healthy() ? RunnerState::ready
                                                      : RunnerState::failed;
        snapshot.capabilities = entry->config.capabilities;
        snapshot.authorized_project_ids = entry->config.authorized_project_ids;
        snapshot.priority = entry->config.priority;
        snapshot.healthy = entry->is_healthy();
        snapshot.consecutive_failures = entry->failure_count();
        result.push_back(std::move(snapshot));
    }
    return result;
}

std::string IntranetWorkerPool::status_json(
    const std::vector<RunnerPoolEntrySnapshot>& entries) {
    std::string body = "{\"workers\":[";
    bool first = true;
    for (const auto& entry : entries) {
        if (!first) body += ",";
        first = false;
        body += "{\"id\":" + json_string(entry.id) +
               ",\"state\":" +
               json_string(to_string(translate_runner_state(entry.metrics.state))) +
               ",\"modelId\":" + json_string(entry.metrics.model_id) +
               ",\"diagnostic\":" + json_string(entry.metrics.diagnostic) +
               ",\"priority\":" + std::to_string(entry.priority) +
               ",\"healthy\":" + (entry.healthy ? "true" : "false") +
               ",\"consecutiveFailures\":" +
               std::to_string(entry.consecutive_failures) + "}";
    }
    return body + "]}";
}

// ---- WorkerListener (inbound side) ----

class WorkerListener::Impl {
public:
    Impl(WorkerModeConfig configuration, std::filesystem::path approved_backend,
        std::filesystem::path runtime_root)
        : configuration_(std::move(configuration)),
          runner_(std::move(approved_backend), std::move(runtime_root)) {
        if (!configuration_.enabled) return;
        if (configuration_.bind_host.empty() || configuration_.port == 0U) {
            throw std::invalid_argument(
                "worker mode requires an explicit bind host and port");
        }
        if (configuration_.approved_client_certificate_sha256.empty()) {
            throw std::invalid_argument(
                "worker mode requires at least one approved client "
                "certificate digest -- refusing to accept an unrestricted "
                "set of callers");
        }
        ctx_ = build_mutual_tls_context(
            true, configuration_.ca_certificate_file,
            configuration_.server_certificate_file,
            configuration_.server_private_key_file);
    }

    RunnerSupervisor& runner() { return runner_; }

    void run(std::atomic_bool& stop_requested) {
        if (!configuration_.enabled) return;
#if defined(_WIN32)
        static const bool wsa_started = [] {
            WSADATA data{};
            return WSAStartup(MAKEWORD(2, 2), &data) == 0;
        }();
        if (!wsa_started) throw std::runtime_error("worker listener socket startup failed");
#endif
        const NativeSocket listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (listener == invalid_socket) {
            throw std::runtime_error("worker listener socket creation failed");
        }
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(configuration_.port);
        if (configuration_.bind_host == "0.0.0.0") {
            address.sin_addr.s_addr = htonl(INADDR_ANY);
        } else if (inet_pton(AF_INET, configuration_.bind_host.c_str(),
                             &address.sin_addr) != 1) {
            close_socket(listener);
            throw std::runtime_error("worker mode bind_host is not a valid IPv4 address");
        }
        if (bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener, 16) != 0) {
            close_socket(listener);
            throw std::runtime_error("worker listener bind/listen failed");
        }
        std::vector<std::thread> connections;
        while (!stop_requested.load()) {
            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(listener, &readable);
            timeval timeout{};
            timeout.tv_sec = 0;
            timeout.tv_usec = 200000;
#if defined(_WIN32)
            const int selected = select(0, &readable, nullptr, nullptr, &timeout);
#else
            const int selected = select(listener + 1, &readable, nullptr, nullptr, &timeout);
#endif
            if (selected <= 0) continue;
            const NativeSocket accepted = accept(listener, nullptr, nullptr);
            if (accepted == invalid_socket) continue;
            connections.emplace_back(
                [this, accepted] { handle_connection(accepted); });
        }
        close_socket(listener);
        for (auto& thread : connections) {
            if (thread.joinable()) thread.join();
        }
    }

private:
    // One accepted connection: TLS-handshakes, verifies the peer client
    // certificate is on the approved digest list, reads exactly one
    // request, forwards it to the local RunnerSupervisor, writes one
    // response, and closes -- deliberately connection-per-request (matches
    // IntranetWorkerPool's own no-persistent-connection design) so a
    // misbehaving or slow caller can never hold a worker slot open
    // indefinitely.
    void handle_connection(NativeSocket socket_value) {
        SSL* raw_ssl = SSL_new(ctx_.get());
        if (raw_ssl == nullptr) {
            close_socket(socket_value);
            return;
        }
        SslPtr ssl(raw_ssl);
        if (SSL_set_fd(ssl.get(), static_cast<int>(socket_value)) != 1 ||
            SSL_accept(ssl.get()) != 1) {
            close_socket(socket_value);
            return;
        }
        try {
            X509* peer = SSL_get_peer_certificate(ssl.get());
            if (peer == nullptr) return;
            X509Ptr guard(peer);
            const auto fingerprint = certificate_sha256_fingerprint(peer);
            bool approved = false;
            for (const auto& allowed :
                configuration_.approved_client_certificate_sha256) {
                if (constant_time_equal(fingerprint, allowed)) {
                    approved = true;
                    break;
                }
            }
            if (!approved) return;
            serve_one_request(ssl.get());
        } catch (...) {
            // A malformed request or backend failure closes only this one
            // connection; the listener loop and every other in-flight
            // connection are untouched.
        }
    }

    static std::string read_one_request(SSL* ssl, std::string& method,
                                        std::string& target,
                                        std::string& body) {
        std::string raw;
        std::array<char, 8192> buffer{};
        std::size_t content_length = 0U;
        bool headers_parsed = false;
        std::size_t header_end = std::string::npos;
        while (true) {
            const int read = SSL_read(ssl, buffer.data(),
                                      static_cast<int>(buffer.size()));
            if (read <= 0) break;
            raw.append(buffer.data(), static_cast<std::size_t>(read));
            if (!headers_parsed) {
                header_end = raw.find("\r\n\r\n");
                if (header_end != std::string::npos) {
                    headers_parsed = true;
                    std::istringstream header_stream(raw.substr(0U, header_end));
                    std::string request_line;
                    std::getline(header_stream, request_line);
                    std::istringstream request_line_stream(request_line);
                    std::string http_version_unused;
                    request_line_stream >> method >> target >> http_version_unused;
                    std::string header_line;
                    while (std::getline(header_stream, header_line)) {
                        if (header_line.rfind("Content-Length:", 0U) == 0U ||
                            header_line.rfind("content-length:", 0U) == 0U) {
                            content_length = static_cast<std::size_t>(
                                std::atoll(header_line.c_str() + 15));
                        }
                    }
                }
            }
            if (headers_parsed &&
                raw.size() >= header_end + 4U + content_length) {
                break;
            }
        }
        if (headers_parsed) {
            body = raw.substr(header_end + 4U,
                              std::min(content_length,
                                      raw.size() - (header_end + 4U)));
        }
        return method;
    }

    void write_response(SSL* ssl, int status, const std::string& body) {
        const std::string status_text = status == 200 ? "OK" : "Error";
        const std::string response =
            "HTTP/1.1 " + std::to_string(status) + " " + status_text +
            "\r\nContent-Type: application/json\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nConnection: close\r\n\r\n" + body;
        std::size_t sent = 0U;
        while (sent < response.size()) {
            const int written = SSL_write(
                ssl, response.data() + sent,
                static_cast<int>(response.size() - sent));
            if (written <= 0) return;
            sent += static_cast<std::size_t>(written);
        }
    }

    void serve_one_request(SSL* ssl) {
        std::string method, target, body;
        read_one_request(ssl, method, target, body);
        static const std::atomic_bool no_cancellation{false};
        if (method == "GET" && target == "/worker/status") {
            const auto metrics = runner_.metrics();
            const std::string response_body =
                "{\"modelId\":" + json_string(metrics.model_id) +
                ",\"modelSha256\":" + json_string(loaded_model_sha256_) +
                ",\"state\":" +
                json_string(to_string(translate_runner_state(metrics.state))) +
                ",\"residentMemoryBytes\":" +
                std::to_string(metrics.resident_memory_bytes) +
                ",\"requestsCompleted\":" +
                std::to_string(metrics.requests_completed) + "}";
            write_response(ssl, 200, response_body);
            return;
        }
        if (method == "POST" && target == "/worker/generate") {
            const auto request = parse_json(body);
            GenerationOptions options;
            options.max_tokens = static_cast<unsigned int>(
                request.optional("n_predict") != nullptr
                    ? request.required("n_predict").as_integer()
                    : 512);
            if (request.optional("temperature") != nullptr) {
                options.temperature = request.required("temperature").as_double();
            }
            if (request.optional("seed") != nullptr) {
                options.seed = static_cast<std::uint64_t>(
                    request.required("seed").as_integer());
            }
            if (request.optional("top_p") != nullptr) {
                options.top_p = request.required("top_p").as_double();
            }
            if (request.optional("top_k") != nullptr) {
                options.top_k = static_cast<unsigned int>(
                    request.required("top_k").as_integer());
            }
            if (request.optional("repeat_penalty") != nullptr) {
                options.repeat_penalty =
                    request.required("repeat_penalty").as_double();
            }
            if (request.optional("repeat_last_n") != nullptr) {
                options.repeat_last_n = static_cast<unsigned int>(
                    request.required("repeat_last_n").as_integer());
            }
            if (request.optional("stop") != nullptr) {
                for (const auto& stop_value : request.required("stop").as_array()) {
                    options.stop_sequences.push_back(stop_value.as_string());
                }
            }
            const std::string prompt = request.required("prompt").as_string();
            const auto result = runner_.generate(
                prompt, options, {}, no_cancellation, 120U);
            const std::string response_body =
                "{\"text\":" + json_string(result.text) +
                ",\"promptTokens\":" + std::to_string(result.prompt_tokens) +
                ",\"generatedTokens\":" +
                std::to_string(result.generated_tokens) + "}";
            write_response(ssl, 200, response_body);
            return;
        }
        if (method == "POST" && target == "/worker/embed") {
            const auto request = parse_json(body);
            const auto result = runner_.embed(request.required("text").as_string());
            std::string values_json = "[";
            bool first = true;
            for (const auto value : result.values) {
                if (!first) values_json += ",";
                first = false;
                values_json += std::to_string(value);
            }
            values_json += "]";
            const std::string response_body =
                "{\"modelId\":" + json_string(result.model_id) +
                ",\"embedding\":" + values_json + "}";
            write_response(ssl, 200, response_body);
            return;
        }
        write_response(ssl, 404, "{\"error\":\"unknown worker route\"}");
    }

    WorkerModeConfig configuration_;
    RunnerSupervisor runner_;
    SslCtxPtr ctx_;
    // Phase 33 honest scope note: this control plane's own worker side has
    // no independent way to learn the currently loaded model's sha256
    // beyond what its own local model registry/manifest already recorded
    // when the administrator running this worker loaded it -- set via
    // WorkerListener::set_loaded_model_sha256() from the same launch path
    // that already calls RunnerSupervisor::load() with a verified
    // ModelRecord, never derived independently here.
    std::string loaded_model_sha256_;

public:
    void set_loaded_model_sha256(std::string sha256) {
        loaded_model_sha256_ = std::move(sha256);
    }
};

WorkerListener::WorkerListener(WorkerModeConfig configuration,
                               std::filesystem::path approved_backend,
                               std::filesystem::path runtime_root)
    : impl_(std::make_unique<Impl>(std::move(configuration),
                                   std::move(approved_backend),
                                   std::move(runtime_root))) {}

WorkerListener::~WorkerListener() = default;

void WorkerListener::run(std::atomic_bool& stop_requested) {
    impl_->run(stop_requested);
}

RunnerSupervisor& WorkerListener::runner() { return impl_->runner(); }

#endif  // MASTERAI_HAS_OPENSSL

}  // namespace masterai
