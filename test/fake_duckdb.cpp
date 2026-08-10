// Minimal stand-in for the DuckDB CLI used by parquet_bytes_to_json() (see
// src/parquet_bridge.cpp). Parses just enough of the "-json -c <query>"
// contract to extract the staged Parquet file path from
// "read_parquet('<path>')" inside the query, and reports back through
// stdout whether that file existed and its size -- proving the caller
// staged the exact bytes it was given -- without needing a real Parquet
// parser. Exit code and stdout body are overridable via environment
// variables so tests can exercise both the success and failure paths.
//
// Environment variables (all optional):
//   MASTERAI_FAKE_DUCKDB_EXIT_CODE  process exit code (default 0)
//   MASTERAI_FAKE_DUCKDB_OUTPUT     literal stdout body to emit instead of
//                                   the default staged-file evidence JSON
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <string>

namespace {

std::string extract_path(const std::string& query) {
    const std::string marker = "read_parquet('";
    const auto begin = query.find(marker);
    if (begin == std::string::npos) return "";
    const auto start = begin + marker.size();
    const auto end = query.find('\'', start);
    if (end == std::string::npos) return "";
    return query.substr(start, end - start);
}

}  // namespace

int main(int argc, char** argv) {
    std::string query;
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "-c" && i + 1 < argc) {
            query = argv[i + 1];
        }
    }

    const char* output_override = std::getenv("MASTERAI_FAKE_DUCKDB_OUTPUT");
    if (output_override != nullptr) {
        std::cout << output_override;
    } else {
        const auto path = extract_path(query);
        std::ifstream input(path, std::ios::binary | std::ios::ate);
        const long long size = input ? static_cast<long long>(input.tellg()) : -1;
        std::cout << "[{\"stagedFileExists\":" << (input.good() ? "true" : "false")
                  << ",\"stagedFileBytes\":" << size << "}]";
    }

    const char* exit_code_text = std::getenv("MASTERAI_FAKE_DUCKDB_EXIT_CODE");
    return exit_code_text != nullptr ? std::atoi(exit_code_text) : 0;
}
