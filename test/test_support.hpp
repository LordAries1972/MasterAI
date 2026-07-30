// MasterAI shared native test fixtures and assertions.
//
// The validation source units use these helpers so temporary-workspace,
// executable-fixture, file-writing, and assertion behavior are not duplicated.
#pragma once

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace masterai_test {

// Fails the active test with its operation-specific evidence.
inline void require(const bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class TemporaryDirectory final {
public:
    // Creates an isolated per-test directory under the operating-system temp.
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
                ("masterai-test-" +
                 std::to_string(
                     std::chrono::high_resolution_clock::now()
                         .time_since_epoch()
                         .count()));
        std::filesystem::create_directories(path_);
    }

    // Removes the isolated tree without masking an active test failure.
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    std::filesystem::path path_;
};

// Writes a deterministic binary-safe text fixture and verifies completion.
inline void write_text(const std::filesystem::path& path,
                       const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!output) {
        throw std::runtime_error("test fixture write failed");
    }
}

// Resolves the current test binary used as a harmless executable fixture.
inline std::filesystem::path test_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_tests.exe";
#else
    return std::filesystem::current_path() / "masterai_tests";
#endif
}

// Resolves the platform-specific fake llama service used by runner and
// download tests; callers remain responsible for checking fixture presence.
inline std::filesystem::path fake_llama_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_fake_llama.exe";
#else
    return std::filesystem::current_path() / "masterai_fake_llama";
#endif
}

// Resolves the minimal curl stand-in used to exercise DownloadManager::run()
// without a real network transfer; see test/fake_curl.cpp.
inline std::filesystem::path fake_curl_executable() {
#if defined(_WIN32)
    return std::filesystem::current_path() / "masterai_fake_curl.exe";
#else
    return std::filesystem::current_path() / "masterai_fake_curl";
#endif
}

}  // namespace masterai_test
