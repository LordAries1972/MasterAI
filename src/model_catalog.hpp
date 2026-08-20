// MasterAI curated downloadable-model catalog: the one source of truth for
// the "suggested model" list every download surface offers.
//
// Before this file existed, this exact data lived only as a hand-authored
// JS array literal (`const PRESETS=[...]`) embedded as browser-served text
// inside src/web_ui.cpp -- fine for the web download page, which was its
// only consumer, but it meant a second consumer (the new GET
// /api/v1/model-catalog route, added for the Agent-Coder VS Code extension
// integration) would have had to duplicate all 149 entries verbatim, with
// every future edit needing to stay manually in sync in two places. This
// header/its .cpp defines the catalog exactly once, in native C++ data, so
// both web_ui.cpp's script generator (which now builds the `PRESETS` JS
// text FROM this data at serve time) and the new HTTP route read the same
// records.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace masterai {

// One curated, downloadable GGUF suggestion. Field names/shapes mirror the
// original PRESETS JS object literal exactly (same names, same optionality)
// so nothing downstream -- the web UI's existing PRESETS.find()/.filter()
// consumers, or a fresh API client -- has to change to keep working.
struct ModelCatalogEntry {
    std::string id;
    std::string tier;
    std::string label;
    std::string category;
    std::string model_id;
    std::string filename;
    std::string source_url;
    std::string revision;
    std::string sha256;
    std::uint64_t min_ram_mib{0};
    std::uint64_t rec_ram_mib{0};
    std::uint64_t size_bytes{0};
    // Optional in the original JS literal (roughly a third of entries never
    // set these four -- see PRESETS.find()'s `p.displayName||''` etc. reads
    // in web_ui.cpp) -- left empty here rather than duplicating the
    // `label`/`id` fallback value, so a consumer's own fallback policy (JS
    // `||''`, or this codebase's own JSON serializer) is the single place
    // that decides what an absent value becomes.
    std::string display_name;
    std::string architecture;
    std::string quantization;
    std::string license_spdx;
};

// Returns the full curated catalog, in the exact authored order the former
// PRESETS literal used. Process-lifetime static data -- there is no runtime
// mutation path (the catalog is a code-shipped constant, not an operator-
// editable store), matching the original literal's own nature.
const std::vector<ModelCatalogEntry>& model_catalog();

}  // namespace masterai
