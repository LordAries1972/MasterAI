// MasterAI control-plane performance-probe command.
//
// This unit keeps measurement argument parsing and report formatting out of the
// primary CLI dispatcher so Phase 12 can evolve without increasing its slop.
#include "performance_cli.hpp"
#include "masterai.hpp"

#include <iomanip>
#include <iostream>
#include <stdexcept>

namespace masterai::performance_cli {

// Validates the optional iteration count and prints stable tab-separated rows.
int run(const int argc, char* argv[]) {
    if (argc > 3) {
        throw std::invalid_argument(
            "Usage: masterai performance-probe [iterations]");
    }
    const auto iterations =
        argc == 3 ? std::stoull(argv[2]) : 100000U;
    const auto report = run_control_plane_performance_probe(iterations);
    std::cout << "name\toperations\tinputBytes\telapsedNs\tnsPerOp\tMiBPerSec\n"
              << std::fixed << std::setprecision(3);
    for (const auto& sample : report.samples) {
        std::cout << sample.name << '\t' << sample.operations << '\t'
                  << sample.input_bytes << '\t'
                  << sample.elapsed_nanoseconds << '\t'
                  << sample.nanoseconds_per_operation << '\t'
                  << sample.mebibytes_per_second << '\n';
    }
    std::cout << "checksum\t" << report.checksum << "\n";
    return 0;
}

}  // namespace masterai::performance_cli
