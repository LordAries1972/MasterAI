// MasterAI private Phase 11 command-line routing contract.
//
// Keeping lifecycle argument parsing outside main.cpp prevents the primary
// command dispatcher from accumulating each operations workflow.
#pragma once

namespace masterai::operations_cli {

// Reports whether the command belongs to the offline lifecycle controller.
bool recognizes(const char* command) noexcept;

// Validates arguments, invokes one lifecycle operation, and prints its report.
int run(int argc, char* argv[]);

}  // namespace masterai::operations_cli
