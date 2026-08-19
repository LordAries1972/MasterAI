// Phase 24: git_diff retrieval adapter. Shells out to the system `git`
// binary through the same sandboxed-subprocess containment tool_exec.cpp's
// run_sandboxed_process() already applies to chat run_command tool calls --
// no new sandboxing logic, no libgit2, no git object parsing. Silently
// unavailable (empty result, not an error) whenever the project root isn't a
// git working tree or the `git` executable can't be found/run, since most
// indexed projects will not be git repositories at all.
#include "masterai.hpp"

#include <sstream>

namespace masterai {
namespace {

// Splits `git diff`/`git log -p` output on "diff --git a/X b/X" headers into
// one synthetic chunk per changed file. Bounded by max_total_bytes so one
// enormous diff can't blow past the retrieval context budget on its own --
// files are kept in the order git reports them (its own significance
// ordering) until the byte budget is exhausted.
std::vector<IndexChunk> split_diff_by_file(const std::string& diff_text,
                                           const std::uint64_t max_total_bytes) {
    std::vector<IndexChunk> chunks;
    std::size_t position = diff_text.find("diff --git ");
    std::uint64_t total_bytes = 0U;
    while (position != std::string::npos) {
        const auto next = diff_text.find("\ndiff --git ", position + 1U);
        const auto end = next == std::string::npos ? diff_text.size() : next + 1U;
        const std::string hunk = diff_text.substr(position, end - position);
        // Header shape: "diff --git a/<path> b/<path>" -- the b/ path is the
        // post-change (or, for a deletion, still the most useful) name.
        std::string relative_path;
        const auto b_marker = hunk.find(" b/");
        if (b_marker != std::string::npos) {
            const auto path_start = b_marker + 3U;
            const auto line_end = hunk.find('\n', path_start);
            relative_path = hunk.substr(
                path_start, (line_end == std::string::npos ? hunk.size()
                                                            : line_end) -
                                path_start);
        }
        if (!relative_path.empty() &&
            total_bytes + hunk.size() <= max_total_bytes) {
            IndexChunk chunk;
            chunk.relative_path = relative_path;
            chunk.language = "diff";
            chunk.offset = 0U;
            chunk.text = hunk;
            chunk.digest = sha256_hex(hunk);
            chunk.id = sha256_hex("git-diff:" + relative_path + ":" + chunk.digest);
            total_bytes += hunk.size();
            chunks.push_back(std::move(chunk));
        }
        position = next == std::string::npos ? std::string::npos : next + 1U;
    }
    return chunks;
}

}  // namespace

std::vector<IndexChunk> git_diff_search(const std::filesystem::path& project_root,
                                        const std::size_t maximum_results,
                                        const std::uint64_t max_total_bytes) {
    if (maximum_results == 0U) return {};
    std::error_code error;
    if (!std::filesystem::is_directory(project_root / ".git", error) &&
        !std::filesystem::is_regular_file(project_root / ".git", error)) {
        // Not a git working tree (no .git directory, and not a worktree's
        // .git file pointer either) -- nothing to report, not an error.
        return {};
    }

    std::atomic_bool cancellation{false};
    const std::filesystem::path git_executable{
#ifdef _WIN32
        "git.exe"
#else
        "git"
#endif
    };
    auto run = [&](const std::vector<std::string>& arguments) -> std::string {
        const auto result = run_sandboxed_process(
            git_executable, arguments, project_root, /*timeout_seconds=*/5U,
            max_total_bytes, cancellation);
        if (!result.succeeded || result.exit_code != 0) return {};
        return result.standard_output;
    };

    std::string diff_output =
        run({"diff", "--unified=1", "--no-color"});
    if (diff_output.empty()) {
        diff_output =
            run({"log", "-p", "-1", "--unified=1", "--no-color"});
    }
    if (diff_output.empty()) return {};

    auto chunks = split_diff_by_file(diff_output, max_total_bytes);
    if (chunks.size() > maximum_results) chunks.resize(maximum_results);
    return chunks;
}

}  // namespace masterai
