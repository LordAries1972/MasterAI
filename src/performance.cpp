// MasterAI deterministic control-plane performance measurements.
//
// This source unit benchmarks the canonical JSON encoder and parser used by
// HTTP, MCP, and IDE operations. It performs no optimization itself, allowing
// identical Release probes to provide before/after evidence.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <stdexcept>

namespace masterai {
namespace {

// Converts one timed operation into comparable latency and throughput fields.
PerformanceSample sample(const std::string& name,
                         const std::uint64_t operations,
                         const std::uint64_t input_bytes,
                         const std::chrono::nanoseconds elapsed) {
    const auto elapsed_count =
        static_cast<std::uint64_t>(elapsed.count() > 0 ? elapsed.count() : 1);
    const double seconds =
        static_cast<double>(elapsed_count) / 1000000000.0;
    return {name,
            operations,
            input_bytes,
            elapsed_count,
            static_cast<double>(elapsed_count) /
                static_cast<double>(operations),
            (static_cast<double>(input_bytes) / (1024.0 * 1024.0)) /
                seconds};
}

}  // namespace

// Times small and 16 KiB JSON-string encoding plus strict object parsing.
PerformanceReport run_control_plane_performance_probe(
    const std::uint64_t iterations) {
    if (iterations < 100U || iterations > 10000000U) {
        throw std::invalid_argument(
            "performance iterations must be between 100 and 10000000");
    }
    const std::string small{
        "MasterAI \"programming\" path\\source.cpp\nbounded"};
    std::string large(16U * 1024U, 'a');
    for (std::size_t index = 127U; index < large.size(); index += 127U) {
        large[index] = index % 2U == 0U ? '"' : '\\';
    }
    const std::string object{
        "{\"schemaVersion\":1,\"projectId\":\"demo\","
        "\"path\":\"src/sample.cpp\",\"enabled\":true}"};
    PerformanceReport report;

    std::uint64_t checksum = 0U;
    auto begin = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < iterations; ++index) {
        const auto encoded = json_string(small);
        checksum += encoded.size() + static_cast<unsigned char>(encoded[1]);
    }
    auto end = std::chrono::steady_clock::now();
    report.samples.push_back(sample(
        "json-string-small", iterations,
        iterations * static_cast<std::uint64_t>(small.size()), end - begin));

    const auto large_iterations =
        std::max<std::uint64_t>(100U, iterations / 100U);
    begin = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < large_iterations; ++index) {
        const auto encoded = json_string(large);
        checksum += encoded.size() + static_cast<unsigned char>(encoded[1]);
    }
    end = std::chrono::steady_clock::now();
    report.samples.push_back(sample(
        "json-string-16k", large_iterations,
        large_iterations * static_cast<std::uint64_t>(large.size()),
        end - begin));

    begin = std::chrono::steady_clock::now();
    for (std::uint64_t index = 0U; index < iterations; ++index) {
        const auto parsed = parse_json(object);
        checksum += static_cast<std::uint64_t>(
            parsed.required("path").as_string().size());
    }
    end = std::chrono::steady_clock::now();
    report.samples.push_back(sample(
        "json-parse-object", iterations,
        iterations * static_cast<std::uint64_t>(object.size()), end - begin));
    report.checksum = checksum;
    return report;
}

}  // namespace masterai
