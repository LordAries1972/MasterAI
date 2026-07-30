// Minimal stand-in for the approved curl executable used by
// DownloadManager::run() (see src/downloads.cpp). Parses just enough of
// curl's CLI contract -- "--output <path>" and a trailing URL argument (the
// URL itself is ignored, since this never touches the network) -- to let
// tests exercise DownloadManager::run() concurrently without a real
// download. Writes the output file in a few delayed chunks so a test can
// observe an in-flight transfer, controlled by environment variables since
// DownloadManager::run() builds its own fixed argument list and cannot pass
// extra flags through.
//
// Environment variables (all optional, with small defaults so an
// unconfigured run still produces a valid, quickly-finished file):
//   MASTERAI_FAKE_CURL_TOTAL_BYTES     total bytes to write (default 4096)
//   MASTERAI_FAKE_CURL_CHUNK_BYTES     bytes per chunk (default = total)
//   MASTERAI_FAKE_CURL_CHUNK_DELAY_MS  delay between chunks (default 0)
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <thread>
#include <vector>

namespace {

std::uint64_t env_uint64(const char* name, const std::uint64_t fallback) {
    const char* value = std::getenv(name);
    if (value == nullptr) return fallback;
    try {
        return std::stoull(value);
    } catch (...) {
        return fallback;
    }
}

}  // namespace

int main(int argc, char** argv) {
    std::string output_path;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--output" && i + 1 < argc) {
            output_path = argv[i + 1];
        }
    }
    if (output_path.empty()) return 1;

    const std::uint64_t total = env_uint64("MASTERAI_FAKE_CURL_TOTAL_BYTES", 4096U);
    std::uint64_t chunk = env_uint64("MASTERAI_FAKE_CURL_CHUNK_BYTES", total);
    if (chunk == 0U) chunk = total;
    const std::uint64_t delay_ms =
        env_uint64("MASTERAI_FAKE_CURL_CHUNK_DELAY_MS", 0U);

    std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
    if (!output) return 1;
    std::uint64_t written = 0U;
    const std::vector<char> buffer(static_cast<std::size_t>(chunk), 'x');
    while (written < total) {
        const auto amount = std::min<std::uint64_t>(chunk, total - written);
        output.write(buffer.data(), static_cast<std::streamsize>(amount));
        written += amount;
        output.flush();
        if (delay_ms > 0U && written < total) {
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    return output ? 0 : 1;
}
