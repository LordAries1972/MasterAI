#include "masterai.hpp"

#include <algorithm>
#include <cctype>
#include <stdexcept>

namespace masterai {
namespace {

struct BenchmarkCase {
    std::string prompt;
    std::vector<std::string> required_terms;
};

std::vector<BenchmarkCase> suite(const BenchmarkProfile profile) {
    std::vector<BenchmarkCase> cases{
        {"In ISO C++17, write a function named add that returns the sum of two "
         "integers. Return only code.",
         {"add", "return"}},
        {"Identify the defect in this C++17 expression and give the corrected "
         "expression: if (value = 4).",
         {"=="}},
        {"Write a C++17 RAII example using std::unique_ptr. Return only code.",
         {"unique_ptr"}},
        {"Explain in one sentence why shell interpolation is unsafe for "
         "untrusted process arguments.",
         {"injection"}},
        {"Write a C++17 bounds check before indexing a std::vector named values "
         "with index i. Return only code.",
         {"size", "i"}},
        {"Give the asymptotic lookup complexity of std::map and name its usual "
         "underlying data structure.",
         {"log", "tree"}},
        {"Write a C++17 function declaration that takes a const std::string by "
         "reference and returns std::size_t. Return only code.",
         {"const", "string", "size_t"}},
        {"State one reason to keep model memory outside a control-plane process.",
         {"isolation"}},
        {"Write a C++17 lock_guard statement for a mutex named mutex. Return "
         "only code.",
         {"lock_guard", "mutex"}},
        {"Explain what SHA-256 verification detects after a model download.",
         {"integrity"}}};
    const std::size_t count =
        profile == BenchmarkProfile::quick
            ? 2U
            : (profile == BenchmarkProfile::standard ? 5U : cases.size());
    cases.resize(count);
    return cases;
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](const unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    return value;
}

std::string profile_name(const BenchmarkProfile profile) {
    if (profile == BenchmarkProfile::standard) return "standard";
    if (profile == BenchmarkProfile::extended) return "extended";
    return "quick";
}

}  // namespace

BenchmarkRunner::BenchmarkRunner(RunnerSupervisor& inference,
                                 BenchmarkStore& store)
    : inference_(inference), store_(store) {}

BenchmarkRecord BenchmarkRunner::run(
    const std::string& model_id, const std::string& backend_version,
    const std::string& build_id, const std::string& hardware_id,
    const BenchmarkProfile profile, const std::atomic_bool& cancellation) {
    const auto cases = suite(profile);
    std::string prompt_material;
    for (const auto& item : cases) {
        prompt_material += std::to_string(item.prompt.size()) + ":" + item.prompt;
    }
    BenchmarkRecord record;
    record.model_id = model_id;
    record.backend_version = backend_version;
    record.build_id = build_id;
    record.hardware_id = hardware_id;
    record.prompt_suite_hash = sha256_hex(prompt_material);
    record.profile = profile;
    record.total_cases = static_cast<std::uint64_t>(cases.size());
    record.settings_json =
        "{\"temperature\":0,\"seed\":1,\"maxTokens\":256,\"profile\":\"" +
        profile_name(profile) + "\"}";

    GenerationOptions options;
    options.max_tokens = 256U;
    options.temperature = 0.0;
    options.seed = 1U;
    for (const auto& item : cases) {
        if (cancellation.load()) {
            throw std::runtime_error("benchmark cancelled");
        }
        const auto generated =
            inference_.generate(item.prompt, options, {}, cancellation);
        if (generated.cancelled) {
            throw std::runtime_error("benchmark cancelled");
        }
        record.prompt_tokens += generated.prompt_tokens;
        record.generated_tokens += generated.generated_tokens;
        record.elapsed_microseconds += generated.elapsed_microseconds;
        const auto output = lower(generated.text);
        bool passed = !output.empty();
        for (const auto& term : item.required_terms) {
            if (output.find(lower(term)) == std::string::npos) {
                passed = false;
                break;
            }
        }
        if (passed) ++record.passed_cases;
        record.peak_resident_memory_bytes =
            std::max(record.peak_resident_memory_bytes,
                     inference_.metrics().resident_memory_bytes);
    }
    store_.add(record);
    return record;
}

}  // namespace masterai
