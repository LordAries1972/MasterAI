// Phase 27: backend-validated reduced-precision KV quality-parity check.
// Runs the same authored prompt set through two already-loaded runners --
// one at KvPrecision::full, one at the candidate reduced precision -- and
// compares generated text. This is the "backend-validated support" evidence
// Phase 27's exit criterion requires before any precision change can ever be
// admitted (see KvCacheManager::precision_admitted()/admit_precision()):
// recording the resulting KvPrecisionEvidence never itself flips admission,
// it only makes an honest, reproducible comparison available for an
// administrator to review before calling admit_precision() themselves.
#include "masterai.hpp"

namespace masterai {

KvPrecisionEvidence run_kv_precision_quality_check(
    RunnerSupervisor& full_precision_runner, RunnerSupervisor& candidate_runner,
    const KvPrecision candidate_precision,
    const std::vector<std::string>& authored_prompts,
    const std::string& backend_hash) {
    KvPrecisionEvidence evidence;
    evidence.precision = candidate_precision;
    evidence.backend_hash = backend_hash;
    if (candidate_precision == KvPrecision::full) {
        evidence.quality_notes =
            "KvPrecision::full is the baseline itself; nothing to compare "
            "against.";
        evidence.quality_parity_verified = false;
        return evidence;
    }
    if (authored_prompts.empty()) {
        evidence.quality_notes = "no authored prompts supplied for comparison";
        evidence.quality_parity_verified = false;
        return evidence;
    }

    std::size_t mismatches = 0U;
    std::size_t compared = 0U;
    std::atomic_bool cancellation{false};
    for (const auto& prompt : authored_prompts) {
        std::string full_text;
        std::string candidate_text;
        try {
            full_text =
                full_precision_runner
                    .generate(prompt, {}, [](const std::string&) {}, cancellation)
                    .text;
            candidate_text =
                candidate_runner
                    .generate(prompt, {}, [](const std::string&) {}, cancellation)
                    .text;
        } catch (const std::exception& error) {
            evidence.quality_notes =
                "generation failed during quality comparison: " +
                std::string(error.what());
            evidence.quality_parity_verified = false;
            return evidence;
        }
        ++compared;
        if (full_text != candidate_text) ++mismatches;
    }

    evidence.quality_parity_verified = mismatches == 0U;
    evidence.quality_notes =
        std::to_string(compared - mismatches) + "/" + std::to_string(compared) +
        " authored prompts produced byte-identical output between " +
        "full precision and " + to_string(candidate_precision) +
        (mismatches == 0U ? "" : " -- quality parity not established");
    return evidence;
}

}  // namespace masterai
