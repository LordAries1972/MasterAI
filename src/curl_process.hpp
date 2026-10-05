// The one place in this codebase that knows how to launch curl as a
// subprocess (rule 15's curl-based process-isolation pattern). Extracted out
// of downloads.cpp so both DownloadManager (model transfers) and the
// Research engine (research.cpp: search-API calls and page fetches) share
// exactly one launch/wait/cancel implementation instead of two copies
// drifting apart.
#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace masterai {

// Launches `executable` with `arguments`, appending both stdout and stderr
// to `log_path`. Polls roughly every 100ms, invoking `progress` (if set) on
// each tick; if `cancellation` becomes true the process is terminated.
// Returns the process exit code, or a non-zero sentinel if it was
// terminated/could not report one.
int run_curl_process(const std::filesystem::path& executable,
                     const std::vector<std::string>& arguments,
                     const std::filesystem::path& log_path,
                     const std::atomic_bool& cancellation,
                     const std::function<void()>& progress = {});

}  // namespace masterai
