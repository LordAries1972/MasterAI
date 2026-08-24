// MasterAI dependency-free loopback browser presentation.
//
// This unit contains only static HTML and JavaScript documents. Moving browser
// presentation out of the HTTP router keeps transport, policy, and UI concerns
// independently reviewable without introducing a web framework.
#include "server_internal.hpp"
#include "model_catalog.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <ctime>
#include <iterator>
#include <random>
#include <utility>
#include <vector>

// Small inline-SVG glyphs for the primary submit button on every form --
// mirrors the dynamically-built ICONS set in application_script() (used for
// row-toolbar buttons) but as C++ literals, since these buttons are baked
// into the static server-rendered HTML rather than assembled client-side.
// Every submit button gets one of these plus a title="" tooltip so no
// button on the page is icon-less or hint-less, matching the row-toolbar
// buttons' icon+title convention.
#define ICON_PLUS_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M8 2a1 1 0 0 1 1 1v4h4a1 1 0 1 1 0 " \
    "2H9v4a1 1 0 1 1-2 0V9H3a1 1 0 1 1 0-2h4V3a1 1 0 0 1 1-1z\"/></svg>"
#define ICON_SAVE_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M13.7 3.3a1 1 0 0 1 0 1.4l-7 7a1 1 " \
    "0 0 1-1.4 0l-3.5-3.5a1 1 0 1 1 1.4-1.4L6 9.6l6.3-6.3a1 1 0 0 1 1.4 " \
    "0z\"/></svg>"
#define ICON_UPLOAD_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M7.5 1.5a.5.5 0 0 1 1 0v7.79l2.15-" \
    "2.15a.5.5 0 0 1 .7.71l-3 3a.5.5 0 0 1-.7 0l-3-3a.5.5 0 1 1 .7-.71L" \
    "7.5 9.29V1.5zM2 12.5a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-11a.5.5 0 " \
    "0 1-.5-.5z\"/></svg>"
#define ICON_DOWNLOAD_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M7.5 1.5a.5.5 0 0 1 1 0v7.29l2.15-" \
    "2.15a.5.5 0 1 1 .7.71l-3 3a.5.5 0 0 1-.7 0l-3-3a.5.5 0 1 1 .7-.71L" \
    "7.5 8.79V1.5zM2 12.5a.5.5 0 0 1 .5-.5h11a.5.5 0 0 1 0 1h-11a.5.5 0 " \
    "0 1-.5-.5z\"/></svg>"
#define ICON_PLAY_SVG \
    "<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" " \
    "fill=\"currentColor\"><path d=\"M4 2.5a.5.5 0 0 1 .77-.42l9 5.5a." \
    "5.5 0 0 1 0 .84l-9 5.5A.5.5 0 0 1 4 13.5v-11z\"/></svg>"

// Shared dark theme for both browser documents. A macro (not a function)
// because it expands into adjacent C++ string-literal concatenation inside
// each page's own <style> block, alongside that page's own overrides.
#define DARK_THEME_CSS \
    ":root{color-scheme:dark;--bg:#121214;--panel:#1c1c22;--panel-border:#2a2a33;" \
    "--text:#e8e8ec;--muted:#9a9aa5;--accent:#7c5cff;--accent-hover:#9179ff}" \
    "*{box-sizing:border-box}" \
    "body{font:15px/1.6 system-ui,-apple-system,'Segoe UI',sans-serif;" \
    "margin:auto;padding:1.5rem;background:var(--bg);color:var(--text)}" \
    "h1{background:linear-gradient(135deg,#7c5cff,#22d3ee);" \
    "-webkit-background-clip:text;background-clip:text;color:transparent;margin:0}" \
    "h2{margin-top:0;font-size:1.1rem}" \
    "h3{color:var(--muted);font-size:.85rem;text-transform:uppercase;" \
    "letter-spacing:.06em;margin-bottom:.5rem}" \
    "section{background:var(--panel);border:1px solid var(--panel-border);" \
    "border-radius:.75rem;padding:1.25rem;box-shadow:0 4px 24px rgba(0,0,0,.35)}" \
    "pre{white-space:pre-wrap;overflow-wrap:anywhere;background:#0e0e11;" \
    "border:1px solid var(--panel-border);border-radius:.5rem;padding:.75rem;" \
    "color:#c8c8d4;font-size:.8rem;max-height:16rem;overflow:auto}" \
    "label{display:block;margin-top:.7rem;color:var(--muted);font-size:.8rem}" \
    "input,select,textarea,button{font:inherit;width:100%;box-sizing:border-box;" \
    "padding:.55rem;border-radius:.5rem;border:1px solid var(--panel-border);" \
    "background:#15151a;color:var(--text)}" \
    "input:focus,select:focus,textarea:focus{outline:none;border-color:var(--accent);" \
    "box-shadow:0 0 0 3px rgba(124,92,255,.25)}" \
    "button{margin-top:.75rem;background:var(--accent);border:none;color:#fff;" \
    "font-weight:600;cursor:pointer;transition:background .15s}" \
    "button:hover{background:var(--accent-hover)}" \
    "button svg{vertical-align:-2px;margin-right:.4rem}" \
    "a{color:var(--accent-hover)}" \
    "a:hover{text-decoration:none}" \
    "progress{width:100%;height:.6rem;margin-top:.6rem;accent-color:var(--accent)}" \
    "#actionStatus,#status{color:var(--muted);min-height:1.2em}" \
    /* Floating error bubble every action-failure catch block now raises \
       through showSystemError() instead of the easy-to-miss #actionStatus \
       line -- pinned to a corner and width-capped (not a full-width top \
       banner) so it can never blow out over the rest of the page, on the \
       Machine Learning screens or anywhere else. Has its own title bar \
       (a red gradient, with the close button pinned to its far right) \
       above a body panel that carries the actual error text in light \
       rose on a dark maroon background, so the message itself stays \
       legible while the title bar signals severity. Auto-fades to solid \
       black over the last .4s before hiding itself 10 seconds after the \
       most recent showSystemError() call (see that function) -- the \
       default timeout for any error that is not dismissed sooner; its \
       close and copy buttons both act immediately instead of waiting on \
       that timer. */ \
    "#systemErrorBanner{position:fixed;bottom:1rem;right:1rem;z-index:9999;" \
    "width:min(24rem,calc(100vw - 2rem));padding:0;" \
    "border:1px solid #7a0d0d;border-radius:.75rem;overflow:hidden;" \
    "box-shadow:0 6px 20px rgba(0,0,0,.45);" \
    "display:flex;flex-direction:column;" \
    "transition:border-color .4s ease}" \
    /* The ID selector above outranks the browser's default \
       [hidden]{display:none} (an attribute selector), so without this \
       explicit override setting el.hidden=true from hideSystemError() -- \
       both the close button and the 10-second auto-hide use it -- never \
       actually hid the banner; it just sat there after "closing". */ \
    "#systemErrorBanner[hidden]{display:none}" \
    "#systemErrorBanner.systemErrorFading{border-color:#000}" \
    "#systemErrorBanner .systemErrorTitleBar{display:flex;align-items:center;" \
    "justify-content:space-between;gap:.75rem;padding:.55rem .75rem;" \
    "background:linear-gradient(135deg,#8a1414,#c92a2a 55%,#5c0a0a);" \
    "color:#fff2f2;transition:background-color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorTitleBar{background:#000}" \
    "#systemErrorBanner .systemErrorTitle{font-weight:800;letter-spacing:.03em}" \
    "#systemErrorBanner .systemErrorClose{width:auto;margin:0;padding:0 .3rem;" \
    "background:transparent;border:none;color:#fff2f2;font-weight:700;" \
    "font-size:1.15rem;line-height:1;cursor:pointer}" \
    "#systemErrorBanner .systemErrorClose:hover{color:#ffd6d6}" \
    "#systemErrorBanner .systemErrorBody{flex:1;overflow-wrap:anywhere;" \
    "max-height:12rem;overflow:auto;padding:.75rem 1rem;" \
    "background:#5a0d0d;color:#ffd9d9;font-weight:600;" \
    "transition:background-color .4s ease,color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorBody{background:#000;" \
    "color:#000}" \
    "#systemErrorBanner .systemErrorActions{display:flex;justify-content:flex-end;" \
    "padding:.5rem .75rem .75rem;background:#5a0d0d;" \
    "transition:background-color .4s ease}" \
    "#systemErrorBanner.systemErrorFading .systemErrorActions{background:#000}" \
    "#systemErrorBanner .systemErrorCopy{width:auto;margin:0;" \
    "padding:.3rem .7rem;background:transparent;border:1px solid #ffd9d9;" \
    "color:#ffd9d9;font-size:.75rem;border-radius:.4rem;cursor:pointer}" \
    "#systemErrorBanner .systemErrorCopy:hover{background:rgba(255,217,217,.15)}" \
    /* Machine Learning form-success confirmation: centered on screen (both \
       axes) rather than pinned to a corner, since a completed ML action is \
       a positive result worth a brief, hard-to-miss interruption instead of \
       an easy-to-miss corner note. Same title-bar-plus-body construction as \
       #systemErrorBanner just above (see that block's own comment) but in \
       green instead of red, with the copy button living in the title bar \
       itself next to the close button rather than a separate footer strip. \
       Auto-fades to solid black over its last .4s before hiding itself 10 \
       seconds after the most recent showFormSuccess() call, exactly like \
       the error banner's own timer; its close and copy buttons both act \
       immediately instead of waiting on that timer. */ \
    "#formSuccessBanner{position:fixed;top:50%;left:50%;" \
    "transform:translate(-50%,-50%);z-index:9999;" \
    "width:min(24rem,calc(100vw - 2rem));padding:0;" \
    "border:1px solid #2f9e44;border-radius:.75rem;overflow:hidden;" \
    "box-shadow:0 6px 20px rgba(0,0,0,.45);" \
    "display:flex;flex-direction:column;" \
    "transition:border-color .4s ease}" \
    "#formSuccessBanner[hidden]{display:none}" \
    "#formSuccessBanner.formSuccessFading{border-color:#000}" \
    "#formSuccessBanner .formSuccessTitleBar{display:flex;align-items:center;" \
    "justify-content:space-between;gap:.5rem;padding:.55rem .75rem;" \
    "background:linear-gradient(135deg,#14532d,#2f9e44 55%,#0a3d1f);" \
    "color:#eafff0;transition:background-color .4s ease}" \
    "#formSuccessBanner.formSuccessFading .formSuccessTitleBar{background:#000}" \
    "#formSuccessBanner .formSuccessTitle{font-weight:800;letter-spacing:.03em;" \
    "flex:1;min-width:0}" \
    "#formSuccessBanner .formSuccessCopy{width:auto;margin:0;" \
    "padding:.25rem .6rem;background:transparent;border:1px solid #eafff0;" \
    "color:#eafff0;font-size:.75rem;border-radius:.4rem;cursor:pointer;" \
    "flex:none}" \
    "#formSuccessBanner .formSuccessCopy:hover{background:rgba(234,255,240,.15)}" \
    "#formSuccessBanner .formSuccessClose{width:auto;margin:0;padding:0 .3rem;" \
    "background:transparent;border:none;color:#eafff0;font-weight:700;" \
    "font-size:1.15rem;line-height:1;cursor:pointer;flex:none}" \
    "#formSuccessBanner .formSuccessClose:hover{color:#c9f7d9}" \
    "#formSuccessBanner .formSuccessBody{flex:1;overflow-wrap:anywhere;" \
    "max-height:12rem;overflow:auto;padding:.75rem 1rem;" \
    "background:#0a3319;color:#c9f7d9;font-weight:600;" \
    "transition:background-color .4s ease,color .4s ease}" \
    "#formSuccessBanner.formSuccessFading .formSuccessBody{background:#000;" \
    "color:#000}" \
    ".checkboxLabel{display:flex;align-items:center;gap:.5rem}" \
    ".checkboxLabel input{width:auto;margin:0;vertical-align:middle}" \
    /* A checkbox used outside .checkboxLabel (e.g. inline inside a plain \
       sentence) still gets the same vertical centering against its text \
       instead of falling back to the browser's own default (usually a \
       couple pixels low against the text baseline). */ \
    "input[type=\"checkbox\"]{vertical-align:middle}" \
    /* Field hint bubbles (ML forms clarity pass): a small "?" badge sits \
       after a label's own text; hovering or focusing it reveals a detailed \
       explanation in a floating bubble instead of cramming that text into \
       the label itself. body.hintsOff (toggled from Machine Learning \
       Settings, see the #cfgHintsEnabled checkbox) hides the badge \
       entirely rather than just suppressing the bubble, so a user who \
       finds them distracting gets a form with the same layout every other \
       field already has. */ \
    ".mlHint{display:inline-flex;align-items:center;justify-content:center;" \
    "width:1.1rem;height:1.1rem;margin-left:.4rem;border-radius:50%;" \
    "background:var(--panel-border);color:var(--muted);font-size:.7rem;" \
    "line-height:1;font-weight:700;font-style:normal;cursor:help;" \
    /* line-height:1 above stops the badge's own line box from inheriting \
       body's line-height:1.6, which otherwise made vertical-align:middle \
       center it against an inflated line and leave it sitting visibly \
       high relative to the label text's actual glyph height. */ \
    "position:relative;vertical-align:middle}" \
    ".mlHint:hover,.mlHint:focus{background:var(--accent);color:#fff;" \
    "outline:none}" \
    ".mlHint:hover .mlHintBubble,.mlHint:focus .mlHintBubble{" \
    "display:block}" \
    ".mlHintBubble{display:none;position:absolute;z-index:30;left:0;" \
    "top:1.5rem;width:18rem;max-width:70vw;padding:.6rem .75rem;" \
    "border-radius:.5rem;background:var(--panel);" \
    "border:1px solid var(--panel-border);color:var(--text);" \
    "font-size:.8rem;font-weight:400;font-style:normal;line-height:1.4;" \
    "box-shadow:0 4px 16px rgba(0,0,0,.4);text-align:left;" \
    "white-space:normal;cursor:auto}" \
    "body.hintsOff .mlHint{display:none}" \
    /* Multi-select control (ML forms clarity pass): replaces a raw \
       comma-separated free-text field (e.g. "Model IDs, comma-separated") \
       with a scrollable checkbox list so the field's actual valid values \
       are visible and clickable instead of requiring the administrator to \
       already know and correctly spell every id/name. See multiSelect()/ \
       multiSelectValues() in the shared JS below. */ \
    ".multiSelect{display:flex;flex-wrap:wrap;gap:.3rem 1rem;padding:.5rem .6rem;" \
    "border:1px solid var(--panel-border);border-radius:.5rem;" \
    "max-height:10rem;overflow-y:auto;background:var(--bg)}" \
    ".multiSelect label{display:flex;align-items:center;gap:.35rem;" \
    "width:auto;margin:0;font-weight:400}" \
    ".multiSelect input{width:auto}" \
    ".multiSelect:empty::before{content:'Nothing to choose from yet.';" \
    "color:var(--muted);font-size:.8rem}" \
    /* Step-flow banner (ML forms clarity pass): a plain-language "you are \
       here" strip at the top of any panel that is one stage of a real \
       multi-panel pipeline -- see ml_step_flow()'s own comment for why it \
       shows the whole sequence but only highlights the current stage(s). \
       A CSS grid with auto-fit columns (not a non-wrapping flex row with \
       a horizontal scrollbar, which is what this used to be) so it always \
       lays out cleanly at any browser width or height -- narrow viewports \
       simply get more rows instead of clipped/scrolled content, with \
       nothing to overflow its container in either direction. There is no \
       connecting arrow between boxes (a wrapping grid has no single "next \
       box" direction to point one at); the number circles alone already \
       state the order. */ \
    ".mlStepFlow{display:grid;" \
    "grid-template-columns:repeat(auto-fit,minmax(9rem,1fr));gap:.5rem;" \
    "padding:.85rem;margin-bottom:1.25rem;background:var(--panel);" \
    "border:1px solid var(--panel-border);border-radius:.6rem}" \
    ".mlStepBox{display:flex;align-items:flex-start;gap:.55rem;" \
    "min-width:0;padding:.4rem .5rem;border-radius:.5rem}" \
    ".mlStepBox.mlStepActive{background:rgba(124,92,255,.16)}" \
    ".mlStepNum{flex:none;display:inline-flex;align-items:center;" \
    "justify-content:center;width:1.6rem;height:1.6rem;border-radius:50%;" \
    "background:var(--panel-border);color:var(--muted);font-weight:700;" \
    "font-size:.8rem}" \
    ".mlStepBox.mlStepActive .mlStepNum{background:var(--accent);color:#fff}" \
    ".mlStepBox strong{display:block;font-size:.85rem}" \
    ".mlStepDesc{display:block;color:var(--muted);font-size:.75rem;" \
    "line-height:1.35;margin-top:.15rem}" \
    /* Short callouts under a step's <h2> (ML forms clarity pass): \
       .mlPipelineNote for a plain "here is what this does" aside (e.g. \
       "this step is optional"), .mlPipelineWarning for a required-step \
       gap that would otherwise only surface later as a raw server error \
       (e.g. missing dataset content before training). */ \
    ".mlPipelineNote{color:var(--muted);font-size:.82rem;line-height:1.4;" \
    "margin:-.25rem 0 .85rem}" \
    ".mlPipelineWarning{color:#f2c96d;background:#3a2f0d;" \
    "border:1px solid #5c4b13;border-radius:.5rem;padding:.5rem .7rem;" \
    "font-size:.82rem;line-height:1.4;margin:-.15rem 0 .85rem}" \
    /* Machine Learning Dashboard: the same five-box grid .mlStepFlow uses, \
       but every box is a real link to that step's page (clicking anywhere \
       lands you there) -- overrides the global anchor color/underline so \
       a clickable step still reads as plain step-box text, not a link. */ \
    ".mlPipelineOverview .mlStepBox{text-decoration:none;color:inherit;" \
    "cursor:pointer}" \
    ".mlPipelineOverview .mlStepBox:hover{background:var(--panel-border)}" \
    ".mlPipelineOverview .mlStepBox.mlStepActive:hover{" \
    "background:rgba(124,92,255,.28)}"

namespace masterai::server_internal {

// Escapes a value for embedding inside a single-quoted JS string literal.
// The catalog's own data (ids, URLs, hashes, license identifiers) never
// actually contains a backslash or a single quote, but a hand-maintained
// data file could grow one later without anybody noticing until the served
// script silently broke -- escaping unconditionally here costs nothing and
// removes that failure mode entirely.
std::string js_single_quoted_escape(const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        if (character == '\\' || character == '\'') escaped += '\\';
        escaped += character;
    }
    return escaped;
}

// Builds the exact `const PRESETS=[...];` JS statement the download page's
// script consumes, from model_catalog()'s single C++ source of truth instead
// of a second hand-maintained copy of the same 149 entries. Every field name
// and the optional-field-omission behavior (an entry that never set
// displayName/architecture/quantization/licenseSpdx in the original literal
// produces no such key here either, so the page's existing `p.displayName||
// ''`-style fallback reads still see `undefined`, not an empty string that
// would look like a deliberate value) is preserved byte-for-byte in meaning,
// even though the generated text itself is not byte-identical to the old
// literal (different whitespace/ordering of object keys is immaterial to a
// JS object literal).
std::string model_catalog_presets_js() {
    // Sorted the same way (displayName ascending, case-insensitive) as the
    // /api/v1/model-catalog REST route (workload_http.cpp) serving the same
    // underlying data, so the download page's own preset list and the API
    // response for it present models in the same order.
    auto entries = model_catalog();
    std::sort(entries.begin(), entries.end(),
              [](const ModelCatalogEntry& left, const ModelCatalogEntry& right) {
                  const auto lower = [](const std::string& value) {
                      std::string result = value;
                      std::transform(result.begin(), result.end(), result.begin(),
                                      [](const unsigned char ch) {
                                          return static_cast<char>(std::tolower(ch));
                                      });
                      return result;
                  };
                  return lower(left.display_name) < lower(right.display_name);
              });
    std::string js{"const PRESETS=["};
    for (const auto& entry : entries) {
        js += "{id:'" + js_single_quoted_escape(entry.id) +
              "',tier:'" + js_single_quoted_escape(entry.tier) +
              "',label:'" + js_single_quoted_escape(entry.label) +
              "',category:'" + js_single_quoted_escape(entry.category) +
              "',modelId:'" + js_single_quoted_escape(entry.model_id) +
              "',filename:'" + js_single_quoted_escape(entry.filename) +
              "',sourceUrl:'" + js_single_quoted_escape(entry.source_url) +
              "',revision:'" + js_single_quoted_escape(entry.revision) +
              "',sha256:'" + js_single_quoted_escape(entry.sha256) +
              "',minRam:" + std::to_string(entry.min_ram_mib) +
              ",recRam:" + std::to_string(entry.rec_ram_mib) +
              ",sizeBytes:" + std::to_string(entry.size_bytes);
        if (!entry.display_name.empty()) {
            js += ",displayName:'" +
                  js_single_quoted_escape(entry.display_name) + "'";
        }
        if (!entry.architecture.empty()) {
            js += ",architecture:'" +
                  js_single_quoted_escape(entry.architecture) + "'";
        }
        if (!entry.quantization.empty()) {
            js += ",quantization:'" +
                  js_single_quoted_escape(entry.quantization) + "'";
        }
        if (!entry.license_spdx.empty()) {
            js += ",licenseSpdx:'" +
                  js_single_quoted_escape(entry.license_spdx) + "'";
        }
        js += "},";
    }
    js += "];";
    return js;
}

// Supplies the small fetch/streaming client used by both browser documents.
std::string application_script() {
    return
        "const q=s=>document.querySelector(s);let csrf='',generation=null,"
        "lastAppliedPreset=null;const downloadSizes={};"
        // Populated in load() from GET /api/v1/models' own architecture
        // field, keyed by model id -- lets the composer's model settings
        // panel (see openModelSettingsPanel()) tell whether the selected
        // model can actually honor a reasoning/thinking directive or only
        // ever the sampling-preset fallback (see modelSupportsReasoning()).
        "let MODEL_ARCHS={};"
        "const REASONING_ARCHS=['qwen','gpt-oss'];"
        "function modelSupportsReasoning(modelId){"
        "const arch=(MODEL_ARCHS[modelId]||'').toLowerCase();"
        "return REASONING_ARCHS.some(a=>arch.includes(a));}"
        // Per-model effort/thinking preference, persisted client-side (no
        // server-side account state needed for a browser-local UI
        // preference) so the same model reopens with whatever was last
        // chosen for it, in this chat or any other.
        "function loadModelSettings(modelId){"
        "const fallback={effort:'medium',thinking:'off'};if(!modelId)return fallback;"
        "try{const all=JSON.parse(localStorage.getItem('modelSettings')||'{}');"
        "return all[modelId]||fallback;}catch(x){return fallback;}}"
        "function saveModelSettings(modelId,settings){if(!modelId)return;"
        "let all={};try{all=JSON.parse(localStorage.getItem('modelSettings')||"
        "'{}');}catch(x){console.warn('saveModelSettings: corrupt "
        "modelSettings, resetting',x);}"
        "all[modelId]=settings;localStorage.setItem('modelSettings',"
        "JSON.stringify(all));}"
        "function saveCurrentModelSettings(){const select=q('#chatModel');"
        "if(!select||!select.value)return;"
        "saveModelSettings(select.value,"
        "{effort:q('#modelEffort').value,thinking:q('#modelThinking').value});}"
        // Refreshes the settings panel's two controls (and its note) for
        // whatever model is currently selected, without changing whether
        // the panel is shown -- called whenever the picker's value changes
        // programmatically (a fresh chat's default model, an existing chat
        // being reopened) so the panel is ready with the right values
        // without popping open on its own. openModelSettingsPanel() below
        // is the user-facing entry point that also shows it.
        "function refreshModelSettingsPanel(){"
        "const select=q('#chatModel'),panel=q('#modelSettingsPanel');"
        "if(!select||!panel||!select.value)return;"
        "const saved=loadModelSettings(select.value);"
        "q('#modelEffort').value=saved.effort;q('#modelThinking').value=saved.thinking;"
        "const note=q('#modelSettingsNote');"
        "if(note)note.textContent=modelSupportsReasoning(select.value)?"
        "'This model supports explicit reasoning -- these settings are sent "
        "to it as reasoning instructions.':"
        "'This model has no reasoning mode of its own -- these settings "
        "instead adjust its sampling (temperature and reply length).';}"
        // Opens the settings panel for the currently selected model --
        // called only from explicit user action (the gear button, or the
        // user changing the model picker themselves).
        "function openModelSettingsPanel(){"
        "refreshModelSettingsPanel();"
        "const panel=q('#modelSettingsPanel');if(panel)panel.hidden=false;}"
        // Files attached to the message currently being composed, and the
        // project id they were (or will be) uploaded against -- an existing
        // chat's project is fixed and known once openChat() loads it, but a
        // brand-new chat only has one once the composer's own project picker
        // has a value, so attachFiles() below re-reads that picker each time
        // rather than caching it once.
        "let attachedFiles=[],openedProjectId='';"
        // Last chat list rendered by renderChatList(), kept around so a
        // brand-new chat can be shown in the sidebar immediately (see
        // streamMessage()) instead of waiting on the next full load()
        // round trip, which the composer only fires once more anyway.
        "let lastChats=[];"
        "function currentProjectId(){return openedProjectId||"
        "(q('#chatProject')?q('#chatProject').value:'');}"
        // Tracks which download ids already have a pollDownloadProgress
        // interval running, keyed by id, so a job left in the 'transferring'
        // state across a page reload (or a second renderDownloads call from
        // another action) gets exactly one poll loop instead of zero or
        // several stacking up.
        "const activePolls={};"
        "async function api(path,method='GET',body){const h={};"
        "if(body)h['Content-Type']='application/json';if(csrf)h['X-CSRF-Token']=csrf;"
        "const r=await fetch(path,{method,headers:h,body:body?JSON.stringify(body):undefined});"
        "const t=await r.text();if(!r.ok)throw new Error(t||r.status);"
        "return t?JSON.parse(t):{};}"
        // Each workspace section is served at its own URL (see the /app
        // route handlers in server.cpp), so only one section's own list/form
        // elements ever exist in the DOM at a time. load() below still
        // builds one shared Promise.all for every possible section, so
        // fetchFor() skips the network round trip entirely -- resolving
        // straight to the render function's own empty-state fallback --
        // whenever this page doesn't contain the element that fetch's data
        // would render into. Without this, every single page load (chat
        // included) fired all ~20 Machine Learning list/dashboard requests
        // regardless of which section (if any) was showing, which was
        // enough on its own to trip the server's shared rate_limit_per_minute
        // budget after a handful of page navigations.
        "function fetchFor(elementId,path,fallback){"
        "return q(elementId)?api(path).catch(()=>fallback):"
        "Promise.resolve(fallback);}"
        // Foreign-key fields use named choices instead of asking an
        // administrator to copy opaque ids out of another table.
        "function fillMlSelect(selector,items,prompt,label){const el=q(selector);"
        "if(!el)return;const selected=el.value;el.innerHTML='';"
        "const empty=document.createElement('option');empty.value='';"
        "empty.textContent=prompt;el.appendChild(empty);"
        "for(const item of items){const option=document.createElement('option');"
        "option.value=item.id;option.textContent=label(item);el.appendChild(option);}"
        "if([...el.options].some(x=>x.value===selected))el.value=selected;}"
        // The checkbox-list counterpart to fillMlSelect() above, for a
        // field that must accept more than one id at once (e.g. "test this
        // instruction example against these models"): populates a
        // '.multiSelect' container (see that CSS class) from a fetched
        // list, preserving whichever boxes were already checked across the
        // periodic re-render every load() does, the same preserved-value
        // courtesy fillMlSelect gives a plain <select>.
        "function fillMultiSelect(selector,items,valueOf,labelOf){"
        "const el=q(selector);if(!el)return;"
        "const checked=new Set([...el.querySelectorAll('input:checked')]"
        ".map(i=>i.value));"
        "el.innerHTML=items.map(item=>{const v=valueOf(item);"
        "return '<label><input type=\"checkbox\" value=\"'+esc(v)+'\"'+"
        "(checked.has(v)?' checked':'')+'> '+esc(labelOf(item))+"
        "'</label>';}).join('');}"
        // Every action-failure catch block across the app raises through
        // here instead of quietly setting the small #actionStatus line --
        // a floating bubble pinned to a bottom corner, width-capped and
        // scrollable so a long message is contained rather than blowing out
        // over the rest of the page (the Machine Learning screens' own
        // failures included). Its own title bar -- a red gradient, titled
        // 'SYSTEM ERROR!', with the close (x) button pinned to its far
        // right -- sits above a body panel that carries the actual error
        // text in light rose on a dark maroon background, so the message
        // stays legible while the title bar signals severity. A copy button
        // lets the exact message be pasted elsewhere (bug reports, chat
        // with an administrator) without retyping it. Unless closed sooner
        // (the x button), the whole bubble fades to solid black over its
        // last .4 seconds and then closes 10 seconds after this call -- the
        // default timeout for any error left on screen. Reuses one bubble
        // element (created lazily) so a second failure while the first is
        // still showing just replaces the message and restarts the timer
        // rather than stacking bubbles.
        "function showSystemError(message){let el=q('#systemErrorBanner');"
        "if(!el){el=document.createElement('div');el.id='systemErrorBanner';"
        "const titleBar=document.createElement('div');"
        "titleBar.className='systemErrorTitleBar';"
        "const title=document.createElement('div');"
        "title.className='systemErrorTitle';title.textContent='SYSTEM ERROR!';"
        "const close=document.createElement('button');close.type='button';"
        "close.className='systemErrorClose';close.textContent='\\u00d7';"
        "close.setAttribute('aria-label','Dismiss error');"
        "close.addEventListener('click',()=>hideSystemError(el));"
        "titleBar.append(title,close);"
        "const body=document.createElement('div');body.className='systemErrorBody';"
        "const actions=document.createElement('div');"
        "actions.className='systemErrorActions';"
        "const copyBtn=document.createElement('button');copyBtn.type='button';"
        "copyBtn.className='systemErrorCopy';copyBtn.textContent='Copy';"
        "copyBtn.setAttribute('aria-label','Copy error message');"
        "copyBtn.addEventListener('click',()=>"
        "copyToClipboard(el.querySelector('.systemErrorBody').textContent,copyBtn));"
        "actions.append(copyBtn);el.append(titleBar,body,actions);"
        "document.body.append(el);}"
        "el.querySelector('.systemErrorBody').textContent=message;"
        "el.hidden=false;el.classList.remove('systemErrorFading');"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el._fadeTimer=setTimeout(()=>el.classList.add('systemErrorFading'),9600);"
        "el._hideTimer=setTimeout(()=>hideSystemError(el),10000);}"
        "function hideSystemError(el){"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el.hidden=true;el.classList.remove('systemErrorFading');}"
        // The success counterpart to showSystemError() just above, raised
        // by every completed Machine Learning form action instead of the
        // easy-to-miss #actionStatus line -- centered on screen on both
        // axes (not pinned to a corner) since a completed action is worth a
        // brief, hard-to-miss interruption. Its own title bar -- a green
        // gradient, with a Copy button and then the close (x) button both
        // pinned to its far right -- sits above a body panel that carries
        // the completion text in light green on a very dark green
        // background, so the message stays legible while the title bar
        // signals success. Unless closed sooner (the x button), the whole
        // banner fades to solid black over its last .4 seconds and then
        // closes 10 seconds after this call, mirroring showSystemError's
        // own timer. Reuses one element (created lazily) so a second
        // completion while the first is still showing just replaces the
        // message and restarts the timer rather than stacking banners.
        "function showFormSuccess(message){let el=q('#formSuccessBanner');"
        "if(!el){el=document.createElement('div');el.id='formSuccessBanner';"
        "const titleBar=document.createElement('div');"
        "titleBar.className='formSuccessTitleBar';"
        "const title=document.createElement('div');"
        "title.className='formSuccessTitle';title.textContent='COMPLETED';"
        "const copyBtn=document.createElement('button');copyBtn.type='button';"
        "copyBtn.className='formSuccessCopy';copyBtn.textContent='Copy';"
        "copyBtn.setAttribute('aria-label','Copy success message');"
        "copyBtn.addEventListener('click',()=>"
        "copyToClipboard(el.querySelector('.formSuccessBody').textContent,copyBtn));"
        "const close=document.createElement('button');close.type='button';"
        "close.className='formSuccessClose';close.textContent='\\u00d7';"
        "close.setAttribute('aria-label','Dismiss success message');"
        "close.addEventListener('click',()=>hideFormSuccess(el));"
        "titleBar.append(title,copyBtn,close);"
        "const body=document.createElement('div');body.className='formSuccessBody';"
        "el.append(titleBar,body);document.body.append(el);}"
        "el.querySelector('.formSuccessBody').textContent=message;"
        "el.hidden=false;el.classList.remove('formSuccessFading');"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el._fadeTimer=setTimeout(()=>el.classList.add('formSuccessFading'),9600);"
        "el._hideTimer=setTimeout(()=>hideFormSuccess(el),10000);}"
        "function hideFormSuccess(el){el=el||q('#formSuccessBanner');if(!el)return;"
        "if(el._fadeTimer)clearTimeout(el._fadeTimer);"
        "if(el._hideTimer)clearTimeout(el._hideTimer);"
        "el.hidden=true;el.classList.remove('formSuccessFading');}"
        "async function login(e){e.preventDefault();try{const d=await api('/api/v1/auth/login','POST',"
        "{username:q('#username').value,password:q('#password').value});"
        "sessionStorage.setItem('csrf',d.csrfToken);location.href='/app';}"
        "catch(x){showSystemError('Login failed. Check your username and password.');}}"
        // Toggles the setup-vs-login sections on the initial page load by
        // asking the already-public /health/ready route whether the first
        // administrator still needs to be created.
        "async function initLogin(){try{const r=await fetch('/health/ready');"
        "const d=await r.json();if(d.setupRequired){q('#setupSection').hidden=false;"
        "q('#loginSection').hidden=true;}}catch(x){console.warn('initLogin: "
        "/health/ready check failed, defaulting to login form',x);}}"
        "async function setupLocalAdmin(e){e.preventDefault();const s=q('#status');"
        "try{await api('/api/v1/setup/local','POST',{setupToken:q('#setupToken').value,"
        "username:q('#setupUsername').value,password:q('#setupPassword').value,"
        "displayName:q('#setupDisplay').value});"
        "s.textContent='Administrator created. Sign in below.';"
        "q('#setupSection').hidden=true;q('#loginSection').hidden=false;}"
        "catch(x){showSystemError('Setup failed: '+x.message);}}"
        "async function load(){csrf=sessionStorage.getItem('csrf')||'';"
        "applyHintsPref();try{"
        "const [me,p,c,mem,m,b,d,u,ml,mlp,mlm,mld,mls,mllt,mlpj,mltj,mler,mlex,mlft,mlmb,mlie,mlsr,mlvs,mlrag,mlse,mlhs,mle,mlmo,mlck,mldp,mlcmp,mlkd,mlend,mlnode,mlpipe,mlpolicy,mlcard,mlaudit,mlmon,cfg,report]="
        "await Promise.all([api('/api/v1/users/me'),"
        "api('/api/v1/projects').catch(()=>({projects:[]})),"
        "api('/api/v1/chats'),"
        "api('/api/v1/memories').catch(()=>({memories:[]})),"
        "api('/api/v1/models').catch(()=>({models:[]})),"
        "api('/api/v1/benchmarks').catch(()=>({benchmarks:[]})),"
        "api('/api/v1/model-downloads').catch(()=>({downloads:[]})),"
        "api('/api/v1/users').catch(()=>({users:[]})),"
        // Every ml.* fetch below is skipped (see fetchFor() above) unless
        // this page actually renders that resource's list -- 403s for
        // anyone who isn't an administrator (see role_allows'
        // ml.dashboard.view) still fall back to the same empty state on the
        // rare page that does render it.
        "fetchFor('#mlAck','/api/v1/ml/dashboard',"
        "{enabled:false,phase:'',activeProjects:0,modelsTraining:0,"
        "modelsAwaitingEvaluation:0,modelsAwaitingApproval:0,"
        "deployedModels:0,failedTrainingJobs:0,interfaces:[]}),"
        "fetchFor('#mlProjectsList,#mlTrainingJobProjectId,#mlExperimentProjectId,#mlFineTuningJobProjectId,#mlModelBuilderConfigProjectId',"
        "'/api/v1/ml/projects',{projects:[]}),"
        "fetchFor('#mlModelsList,#mlPredictModelId,#mlTrainingJobModelId,#mlEvaluationRunModelId,#mlExperimentModelId,#mlFineTuningJobModelId,#mlModelBuilderConfigBaseModelId,#mlModelOptimizationModelId,#mlDeploymentModelId,#mlModelComparisonBaselineModelId,#mlModelComparisonCandidateModelId,#mlPipelineModelId,#mlInstructionExampleGenerateModelId,#mlSyntheticRecordGenerateModelId',"
        "'/api/v1/ml/models',{models:[]}),"
        "fetchFor('#mlDatasetsList,#mlDatasetContentId,#mlLabelTaskDatasetId,#mlPrepJobDatasetId,#mlTrainingJobDatasetId,#mlEvaluationRunDatasetId,#mlExperimentDatasetId,#mlFineTuningJobDatasetId,#mlInstructionExampleDatasetId,#mlInstructionExampleGenerateDatasetId,#mlSyntheticRecordDatasetId,#mlSyntheticRecordGenerateDatasetId,#mlModelComparisonDatasetId,#mlPipelineDatasetId',"
        "'/api/v1/ml/datasets',{datasets:[]}),"
        "fetchFor('#mlSubjectsList,#mlKnowledgeSubjectId,#mlSubjectExamSubjectId',"
        "'/api/v1/ml/subjects',{subjects:[]}),"
        "fetchFor('#mlLabelTasksList','/api/v1/ml/label-tasks',{labelTasks:[]}),"
        "fetchFor('#mlPrepJobsList','/api/v1/ml/prep-jobs',{prepJobs:[]}),"
        "fetchFor('#mlTrainingJobsList,#mlHyperparameterSearchTrainingJobId,#mlCheckpointTrainingJobId,#mlEnsembleTrainingJobId','/api/v1/ml/training-jobs',"
        "{trainingJobs:[]}),"
        "fetchFor('#mlEvaluationRunsList','/api/v1/ml/evaluation-runs',"
        "{evaluationRuns:[]}),"
        "fetchFor('#mlExperimentsList','/api/v1/ml/experiments',"
        "{experiments:[]}),"
        "fetchFor('#mlFineTuningJobsList','/api/v1/ml/fine-tuning-jobs',"
        "{fineTuningJobs:[]}),"
        "fetchFor('#mlModelBuilderConfigsList',"
        "'/api/v1/ml/model-builder-configs',{modelBuilderConfigs:[]}),"
        "fetchFor('#mlInstructionExamplesList',"
        "'/api/v1/ml/instruction-examples',{instructionExamples:[]}),"
        "fetchFor('#mlSyntheticRecordsList','/api/v1/ml/synthetic-records',"
        "{syntheticRecords:[]}),"
        "fetchFor('#mlVectorStoresList,#mlKnowledgeVectorStoreId,#mlRagConfigVectorStoreId',"
        "'/api/v1/ml/vector-stores',"
        "{vectorStores:[]}),"
        "fetchFor('#mlRagConfigsList','/api/v1/ml/rag-configs',"
        "{ragConfigs:[]}),"
        "fetchFor('#mlSubjectExamsList','/api/v1/ml/subject-exams',"
        "{subjectExams:[]}),"
        "fetchFor('#mlHyperparameterSearchesList',"
        "'/api/v1/ml/hyperparameter-searches',{hyperparameterSearches:[]}),"
        "fetchFor('#mlEnsemblesList','/api/v1/ml/ensembles',{ensembles:[]}),"
        "fetchFor('#mlModelOptimizationsList',"
        "'/api/v1/ml/model-optimizations',{modelOptimizations:[]}),"
        "fetchFor('#mlCheckpointsList','/api/v1/ml/checkpoints',"
        "{checkpoints:[]}),"
        "fetchFor('#mlDeploymentsList','/api/v1/ml/deployments',"
        "{deployments:[]}),"
        "fetchFor('#mlModelComparisonsList','/api/v1/ml/model-comparisons',"
        "{modelComparisons:[]}),"
        "fetchFor('#mlKnowledgeDocumentsList','/api/v1/ml/knowledge-documents',"
        "{knowledgeDocuments:[]}),"
        "fetchFor('#mlInferenceEndpointsList,#mlEndpointModelId',"
        "'/api/v1/ml/inference-endpoints',{inferenceEndpoints:[]}),"
        "fetchFor('#mlComputeNodesList','/api/v1/ml/compute-nodes',"
        "{computeNodes:[]}),"
        "fetchFor('#mlAutomationPipelinesList,#mlPipelineProjectId',"
        "'/api/v1/ml/automation-pipelines',{automationPipelines:[]}),"
        "fetchFor('#mlSafetyPoliciesList','/api/v1/ml/safety-policies',"
        "{safetyPolicies:[]}),"
        "fetchFor('#mlModelCardsList,#mlModelCardModelId',"
        "'/api/v1/ml/model-cards',{modelCards:[]}),"
        "fetchFor('#mlAuditLogsList','/api/v1/ml/audit-logs',{auditLogs:[]}),"
        "fetchFor('#mlMonitoringPanel','/api/v1/ml/monitoring',"
        "{systemResources:null,trainingJobCounts:{},evaluationMetrics:[],"
        "inferenceBenchmarks:[]}),"
        // 403/503 for anyone who isn't an administrator, or when no
        // settings.json path is known to the running server -- both are
        // quiet, expected no-ops here exactly like the ml.* fetches above.
        "fetchFor('#systemConfigForm','/api/v1/admin/config',null),"
        // 403 for anyone who isn't an administrator -- quiet no-op like the
        // ml.*/admin.config fetches above.
        "fetchFor('#systemReport','/api/v1/system/report',null)]);"
        "q('#who').textContent=me.displayName+' ('+me.role+')';"
        // A viewer's sidebar renders none of the settings pages, so these
        // elements legitimately don't exist -- guard every write instead of
        // assuming every page is present.
        "renderProjects(p.projects);renderModels(m.models);"
        // Phase 61: vector stores can choose either the always-available
        // authored backend or a verified GGUF embedding model from the
        // ordinary model inventory. No opaque model id needs to be typed.
        "const embeddingSelect=q('#mlVectorStoreEmbeddingModel');"
        "if(embeddingSelect){embeddingSelect.innerHTML="
        "'<option value=\"authored_hashing_vectorizer_v1\">MasterAI authored ' +"
        "'hashing vectorizer v1 (128 dimensions)</option>'+"
        "m.models.filter(x=>x.category==='embeddings-code-search'&&x.state==='ready')"
        ".map(x=>'<option value=\"'+esc(x.id)+'\">'+esc(x.displayName)+"
        "' (learned via llama.cpp)</option>').join('');}"
        "renderBenchmarks(b.benchmarks);renderDownloads(d.downloads);"
        "renderUsers(u.users,me.id);renderMlDashboard(ml);renderMlProjects(mlp.projects);"
        "renderMlModels(mlm.models);renderMlDatasets(mld.datasets);"
        "renderMlSubjects(mls.subjects);"
        "renderMlLabelTasks(mllt.labelTasks);renderMlPrepJobs(mlpj.prepJobs);"
        "renderMlTrainingJobs(mltj.trainingJobs,mld.datasets);"
        "renderMlEvaluationRuns(mler.evaluationRuns);"
        "renderMlExperiments(mlex.experiments);"
        "renderMlFineTuningJobs(mlft.fineTuningJobs);"
        "renderMlModelBuilderConfigs(mlmb.modelBuilderConfigs);"
        "renderMlInstructionExamples(mlie.instructionExamples);"
        "renderMlSyntheticRecords(mlsr.syntheticRecords);"
        "renderMlVectorStores(mlvs.vectorStores);"
        "renderMlRagConfigs(mlrag.ragConfigs);"
        "renderMlSubjectExams(mlse.subjectExams);"
        "renderMlHyperparameterSearches(mlhs.hyperparameterSearches);"
        "renderMlEnsembles(mle.ensembles);"
        "renderMlModelOptimizations(mlmo.modelOptimizations);"
        "renderMlCheckpoints(mlck.checkpoints);"
        "renderMlDeployments(mldp.deployments);"
        "renderMlModelComparisons(mlcmp.modelComparisons);"
        "renderMlKnowledgeDocuments(mlkd.knowledgeDocuments);"
        "renderMlInferenceEndpoints(mlend.inferenceEndpoints);"
        "renderMlComputeNodes(mlnode.computeNodes);"
        "renderMlAutomationPipelines(mlpipe.automationPipelines);"
        "renderMlSafetyPolicies(mlpolicy.safetyPolicies);"
        "renderMlModelCards(mlcard.modelCards);"
        "renderMlAuditLogs(mlaudit.auditLogs);"
        "renderMlMonitoring(mlmon);"
        "fillMlSelect('#mlKnowledgeSubjectId',mls.subjects,'Choose a subject',"
        "x=>x.name);fillMlSelect('#mlSubjectExamSubjectId',mls.subjects,"
        "'Choose a subject',x=>x.name);"
        "fillMlSelect('#mlKnowledgeVectorStoreId',mlvs.vectorStores,"
        "'Choose a vector store',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlRagConfigVectorStoreId',mlvs.vectorStores,"
        "'Choose a vector store',x=>x.name+' ('+x.status+')');"
        "for(const id of ['#mlTrainingJobProjectId','#mlExperimentProjectId',"
        "'#mlFineTuningJobProjectId','#mlModelBuilderConfigProjectId',"
        "'#mlPipelineProjectId'])"
        "fillMlSelect(id,mlp.projects,'None / choose a project',x=>x.name);"
        "for(const id of ['#mlPredictModelId','#mlTrainingJobModelId',"
        "'#mlEvaluationRunModelId','#mlExperimentModelId',"
        "'#mlModelOptimizationModelId',"
        "'#mlDeploymentModelId','#mlModelComparisonBaselineModelId',"
        "'#mlModelComparisonCandidateModelId','#mlEndpointModelId',"
        "'#mlModelCardModelId','#mlPipelineModelId',"
        "'#mlInstructionExampleGenerateModelId',"
        "'#mlSyntheticRecordGenerateModelId'])fillMlSelect(id,mlm.models,"
        "'None / choose a model',x=>x.displayName||x.name);"
        // Fine-Tuning Job's and Model Builder Config's "base model" fields
        // are the two places you pick what training actually starts from,
        // so unlike every other model field above (which only makes sense
        // against something already in the ML registry -- predict,
        // evaluate, deploy, compare, ...) these also offer every model
        // already downloaded into the local model catalog (GET
        // /api/v1/models -- 'm' below), not only ones some prior training
        // run already registered. Picking a catalog entry here works with
        // no separate registration step: the create endpoints transparently
        // register it into the ML registry on submit (see
        // resolve_or_register_base_model() in server.cpp).
        "const mlBaseModelChoices=[...mlm.models.map(x=>"
        "({id:x.id,label:(x.displayName||x.name)+' (ML registry)'})),"
        "...m.models.map(x=>"
        "({id:x.id,label:(x.displayName||x.id)+' (downloaded)'}))];"
        "for(const id of ['#mlFineTuningJobModelId',"
        "'#mlModelBuilderConfigBaseModelId'])"
        "fillMlSelect(id,mlBaseModelChoices,'None / choose a model',"
        "x=>x.label);"
        // ML forms clarity pass: every dataset picker now shows whether
        // that dataset actually has uploaded rows to work with, so a user
        // can no longer select an empty dataset shell without warning and
        // only discover it later from a raw ml_dataset_has_no_content
        // error at training/evaluation time.
        "const mlDatasetLabel=x=>x.name+(x.hasContent?"
        "' \\u2014 '+x.contentRows+' row(s) ready':"
        "' \\u2014 \\u26a0 no content uploaded yet');"
        // Dataset purpose filtering: Training Jobs/Evaluation Lab/
        // Experiment Tracking/Model Comparison/Automation Pipelines only
        // ever run parse_tabular_csv against the dataset's content (see
        // those same endpoints in server.cpp) -- picking an
        // Instruction/fine-tuning-text-purpose dataset there was
        // previously only caught at run/evaluate time with a raw
        // classification-validation error, even though it can never work.
        // Their pickers now only list Tabular-purpose datasets so that
        // dead end is no longer offered in the first place. Dataset
        // Manager's own content-upload picker, and Fine-Tuning Jobs'
        // (whose Method decides which purpose it actually needs -- a plain
        // method trains tabular, an \"llm:\" method fine-tunes an
        // Instruction dataset), still list every dataset regardless of
        // purpose.
        "const mlTabularDatasets=mld.datasets.filter(x=>x.purpose!=='instruction');"
        "for(const id of ['#mlTrainingJobDatasetId','#mlEvaluationRunDatasetId',"
        "'#mlExperimentDatasetId','#mlModelComparisonDatasetId','#mlPipelineDatasetId'])"
        "fillMlSelect(id,mlTabularDatasets,'None / choose a tabular dataset',"
        "mlDatasetLabel);"
        "for(const id of ['#mlDatasetContentId','#mlDatasetAugmentId',"
        "'#mlLabelTaskDatasetId',"
        "'#mlPrepJobDatasetId','#mlFineTuningJobDatasetId',"
        "'#mlInstructionExampleDatasetId','#mlInstructionExampleGenerateDatasetId',"
        "'#mlInstructionExampleCheckDatasetId','#mlContinualLearningDatasetId',"
        "'#mlSyntheticRecordDatasetId','#mlSyntheticRecordGenerateDatasetId'])"
        "fillMlSelect(id,mld.datasets,'None / choose a dataset',mlDatasetLabel);"
        "fillMlSelect('#mlHyperparameterSearchTrainingJobId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlCheckpointTrainingJobId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlEnsembleTrainingJobId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlLabelTaskAssigneeId',u.users,'Unassigned',"
        "x=>x.displayName+' ('+x.role+')');"
        // ML forms clarity pass: every field below used to require pasting
        // an opaque id copied from a table row; each now offers the same
        // named choices fillMlSelect already gives model/dataset/project
        // fields above.
        "for(const id of ['#mlInstructionExampleContentId',"
        "'#mlInstructionExampleTestId'])fillMlSelect(id,"
        "mlie.instructionExamples,'Choose an instruction example',"
        "x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlProjectGovernanceId',mlp.projects,"
        "'Choose a project',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlDatasetDeclareId',mld.datasets,"
        "'Choose a dataset',x=>x.name+' ('+x.approvalStatus+')');"
        "fillMlSelect('#mlTrainingJobPolicyId',mltj.trainingJobs,"
        "'Choose a training job',x=>x.name+' ('+x.status+')');"
        "fillMultiSelect('#mlInstructionExampleTestModelIds',mlm.models,"
        "x=>x.id,x=>x.displayName||x.name);"
        "fillMlSelect('#mlEndpointPolicyId',mlend.inferenceEndpoints,"
        "'Choose an inference endpoint',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlEndpointPolicySafetyPolicyId',mlpolicy.safetyPolicies,"
        "'None / choose a safety policy',x=>x.name+' ('+x.status+')');"
        "fillMlSelect('#mlExperimentCompareBaselineId',mlex.experiments,"
        "'Choose the baseline experiment',x=>x.name+' ('+x.status+')');"
        "fillMultiSelect('#mlExperimentCompareCandidateIds',mlex.experiments,"
        "x=>x.id,x=>x.name+' ('+x.status+')');"
        "renderSystemConfig(cfg);"
        "renderSystemReport(report);"
        "fill('#chatProject',p.projects,x=>x.id,x=>x.displayName);"
        // Chat offers every downloaded, verified model whose backend/format
        // and required CPU/GPU features this machine actually has (state
        // 'ready' on the server -- see ModelRegistry::scan()) -- anything
        // else genuinely can't be loaded, so it would just be a broken
        // option here. A model that's merely short on RAM *right now* still
        // counts as ready: that estimate is against current available RAM,
        // which can change, so it's left selectable with its recommended RAM
        // shown against its name (and a warning icon when the server's
        // diagnostic flags it) so the user can judge it themselves.
        "MODEL_ARCHS=Object.fromEntries(m.models.map(x=>[x.id,x.architecture||'']));"
        "const ready=m.models.filter(x=>x.state==='ready');"
        // Phase 29: a synthetic, non-model picker entry -- present only when
        // the administrator has both turned tiered routing on and assigned
        // at least one model to a tier (see WorkloadHttpController::
        // model_inventory()'s tieredRoutingAvailable). Chosen exactly like a
        // real model id; send_chat_message() recognizes this one sentinel
        // string and routes instead of loading a model literally named this.
        "const pickerEntries=m.tieredRoutingAvailable?"
        "[{id:'auto:tiered',displayName:'Auto (Tiered "
        "\\u2014 routes to the fastest model that can answer well)',"
        "diagnostic:'',quantization:'',recommendedRamMiB:null},"
        "...ready]:ready;"
        "fill('#chatModel',pickerEntries,x=>x.id,"
        "x=>x.id==='auto:tiered'?x.displayName:"
        "(x.diagnostic&&x.diagnostic.startsWith('Warning:')?'\\u26a0\\ufe0f ':'')+"
        "quantTag(x.quantization)+x.displayName+"
        "' ('+Math.round(x.recommendedRamMiB/1024)+'GB)');"
        "renderChatList(c.chats);renderMemories(mem.memories);"
        // Landing directly on a chat's own URL (/app/chat/<id>) preloads its
        // id into this hidden field server-side; load its history now that
        // the page's other data has arrived.
        "const initialChat=q('#messageChat');"
        "if(initialChat&&initialChat.value)await openChat(initialChat.value);"
        "else if(q('#chatEmpty')){q('#chatEmpty').hidden=!ready.length;"
        "if(!ready.length)q('#chatEmpty').textContent="
        "'No downloaded models are ready for this machine yet. Ask an "
        "administrator or developer to download one from Settings.';"
        // A brand-new chat's model picker otherwise just falls back to
        // whatever option the browser puts first. Prefer the model that's
        // already warm in the runner (reusing it avoids paying another
        // load/warm-up cycle) and only fall back to the most recent chat's
        // model -- the "last known model used" -- when nothing is warm right
        // now (e.g. right after server start). Both candidates are checked
        // against the ready list since a warm or previously-used model can
        // have since been removed or gone stale.
        "await selectDefaultChatModel(ready,c.chats);}}"
        "catch(x){location.href='/';}}"
        // Renders the sidebar's chat list as real links to each chat's own
        // URL -- clicking one is a normal page navigation, not a client-side
        // panel swap. Chats arrive newest-first from the server; only the
        // most recent 20 render directly under Chats, and anything older
        // moves into a collapsed History section so the sidebar stays a
        // fixed, scannable size regardless of how many chats exist.
        "const RECENT_CHAT_LIMIT=20;"
        // Each sidebar row is now the chat link plus a bin icon that deletes
        // it -- deletion is permanent (the server drops the chat and every
        // one of its messages), so it's confirmed before the request fires.
        // Deleting the chat currently open just navigates to a blank /app
        // rather than trying to patch the now-gone chat out of the visible
        // panel.
        "function chatLink(c){const row=document.createElement('div');"
        "row.className='chatListItem';"
        "const a=document.createElement('a');"
        "a.className='navButton';a.href='/app/chat/'+encodeURIComponent(c.id);"
        "a.textContent=c.title||c.id;a.title=c.title||c.id;"
        "const del=document.createElement('button');del.type='button';"
        "del.className='chatDeleteBtn';del.title='Delete chat';"
        // An emoji glyph's font metrics can render outside the button's own
        // box (Windows in particular draws color emoji with extra vertical
        // padding baked into the glyph), so the visible icon and the actual
        // clickable rectangle can end up misaligned enough that a click on
        // what looks like the icon lands outside it. A fixed-size inline SVG
        // guarantees the drawn icon and the hit target are the same box.
        "del.setAttribute('aria-label','Delete chat');"
        "del.innerHTML='<svg viewBox=\"0 0 24 24\" width=\"15\" height=\"15\" "
        "fill=\"none\" stroke=\"currentColor\" stroke-width=\"2\" "
        "stroke-linecap=\"round\" stroke-linejoin=\"round\">"
        "<path d=\"M4 7h16\"/><path d=\"M10 11v6\"/><path d=\"M14 11v6\"/>"
        "<path d=\"M6 7l1 13a2 2 0 0 0 2 2h6a2 2 0 0 0 2-2l1-13\"/>"
        "<path d=\"M9 7V4a1 1 0 0 1 1-1h4a1 1 0 0 1 1 1v3\"/></svg>';"
        "del.addEventListener('click',async e=>{e.preventDefault();e.stopPropagation();"
        // confirm() temporarily removed for diagnosis: Chrome silently
        // no-ops window.confirm() with zero console output if the user
        // previously ticked "Prevent this page from creating additional
        // dialogs" on an earlier alert/confirm -- that reads identically to
        // "the button does nothing," so ruling it out here.
        "try{await api('/api/v1/chats/'+encodeURIComponent(c.id)+'/delete','POST');"
        "if(q('#messageChat')&&q('#messageChat').value===c.id){location.href='/app';return;}"
        "await load();}"
        "catch(x){const s=q('#actionStatus');"
        "showSystemError('Delete failed: '+x.message);}});"
        "row.append(a,del);return row;}"
        "function renderChatList(chats){lastChats=chats;"
        "const list=q('#chatList');if(!list)return;"
        "list.replaceChildren();"
        "for(const c of chats.slice(0,RECENT_CHAT_LIMIT))list.append(chatLink(c));"
        "const older=chats.slice(RECENT_CHAT_LIMIT);"
        "const historySection=q('#chatHistorySection');"
        "const historyList=q('#chatHistoryList');"
        "if(historySection&&historyList){"
        "historySection.hidden=older.length===0;historyList.replaceChildren();"
        "for(const c of older)historyList.append(chatLink(c));}}"
        // Keeps remembered details inspectable and removable. The section is
        // collapsed by default in the sidebar, preserving the uncluttered
        // chat layout while making automatic captures transparent.
        "function renderMemories(memories){const list=q('#memoryList');if(!list)return;"
        "list.replaceChildren();if(!memories.length){"
        "const empty=document.createElement('div');empty.className='memoryEmpty';"
        "empty.textContent='No saved details.';list.append(empty);return;}"
        "for(const memory of memories){const row=document.createElement('div');"
        "row.className='memoryItem';const text=document.createElement('span');"
        "text.textContent=memory.content;text.title=memory.source==='auto'?"
        "'Remembered automatically':'Saved explicitly';row.append(text);"
        "if(q('#newMemory')){const del=document.createElement('button');"
        "del.type='button';del.className='memoryDeleteBtn';del.textContent='\u00d7';"
        "del.title='Forget this detail';del.setAttribute('aria-label','Forget detail');"
        "del.addEventListener('click',async()=>{try{await api('/api/v1/memories/'+"
        "encodeURIComponent(memory.id)+'/delete','POST');await refreshMemories();}"
        "catch(x){showSystemError('Could not forget detail: '+x.message);}});"
        "row.append(del);}list.append(row);}}"
        "async function refreshMemories(){const data=await api('/api/v1/memories');"
        "renderMemories(data.memories||[]);}"
        "async function addMemory(e){e.preventDefault();const input=q('#memoryContent');"
        "const content=input.value.trim();if(!content)return;try{"
        "await api('/api/v1/memories','POST',{content});input.value='';"
        "await refreshMemories();}catch(x){showSystemError('Could not save detail: '+x.message);}}"
        // Loads one chat's full message history (via the GET
        // /api/v1/chats/{id} route) into the chat panel.
        "async function openChat(id){const s=q('#actionStatus');"
        "try{const chat=await api('/api/v1/chats/'+encodeURIComponent(id));"
        "q('#messageChat').value=id;openedProjectId=chat.projectId;"
        "if(q('#chatEmpty'))q('#chatEmpty').hidden=true;"
        // The model can still be switched after the first message (see
        // changeChatModel()) -- only the project is fixed for the chat's
        // lifetime, since it's tied to the attachments/retrieval already
        // scoped against it.
        "const modelSelect=q('#chatModel');"
        // Setting .value to an id that isn't one of the picker's current
        // options (the model this chat was created with may not be in the
        // "ready" list right now -- unloaded, still downloading, etc.)
        // silently no-ops in every browser, leaving whatever option was
        // already first selected instead of the chat's actual model. Adding
        // a placeholder option for it first guarantees the assignment always
        // sticks, so restoring a chat always shows the model it really uses.
        "if(modelSelect){"
        "if(chat.modelId&&![...modelSelect.options].some(o=>o.value===chat.modelId)){"
        "const placeholder=document.createElement('option');"
        "placeholder.value=chat.modelId;"
        "placeholder.textContent=chat.modelId+' (not currently loaded)';"
        "modelSelect.append(placeholder);}"
        "modelSelect.value=chat.modelId;refreshModelSettingsPanel();}"
        // Unlike Effort/Thinking (per-model, localStorage), Tool execution
        // is per-chat and server-owned -- populate it straight from this
        // chat's own record rather than from refreshModelSettingsPanel()
        // above, which knows nothing about which chat is open.
        "if(q('#chatToolMode')){"
        "q('#chatToolMode').value=chat.toolExecutionMode||'auto';"
        "updateChatToolModeNote();}"
        "if(q('#chatProject'))q('#chatProject').disabled=true;"
        "const box=q('#chatMessages');box.replaceChildren();"
        "for(const message of chat.messages)"
        "appendMessage(box,message.role,message.content,message.tokenCount);"
        "box.scrollTop=box.scrollHeight;"
        // Warming used to be a side effect of the GET above (fired the
        // instant a chat was opened, mid-load -- before the browser had
        // actually painted this history or the models dropdown from the
        // request racing it). It's triggered explicitly now, and only
        // after two animation frames have actually elapsed, which
        // guarantees a real paint has happened -- so the full page
        // (sidebar, model list, this chat's history) is genuinely on
        // screen before a multi-gigabyte cold load starts competing with
        // it for CPU/disk. Fire-and-forget: a failure here just means the
        // pre-warm head start didn't happen, not a page error.
        "if(chat.modelId)requestAnimationFrame(()=>requestAnimationFrame(()=>"
        "api('/api/v1/runner/warm','POST',{modelId:chat.modelId}).catch(()=>{})));}"
        "catch(x){showSystemError('Failed to load chat: '+x.message);}}"
        // Bubble side and color already say who's speaking; only the system
        // role (rendered plain, centered) still needs a label. User/assistant
        // content renders through renderMarkdown() below (fenced code
        // blocks, bullet/numbered lists, inline code, bold) instead of raw
        // textContent, so multi-line replies read like a real chat client
        // rather than one run-on paragraph. The raw source is kept on the
        // element itself so streamed tokens can be re-rendered as they
        // arrive (see streamMessage()).
        // Writes the bubble's title row as 'Query (Tokens: N)' / 'Response
        // (Tokens: N)' -- the count is the runner's own figure for that
        // message (prompt tokens evaluated for a query, tokens generated
        // for a response). A count of 0 means "not recorded" (older
        // messages, or a turn that failed first) and shows no suffix.
        "function setMsgTokens(msgEl,role,tokens){"
        "const title=msgEl.querySelector('.chatMsg-responseTitle');"
        "if(!title)return;"
        "title.textContent=(role==='assistant'?'Response':'Query')+"
        "(tokens>0?' (Tokens: '+tokens+')':'');}"
        "function appendMessage(box,role,content,tokenCount){"
        "const p=document.createElement('div');"
        "p.className='chatMsg chatMsg-'+role;"
        "if(role==='system'){p.textContent='system: '+content;}"
        "else{p.dataset.raw=content;"
        // Assistant replies get a 'Response' title and user messages get a
        // 'Query' title, matching the error card's own title row -- the
        // copy button (added below) shares that row's top-right corner via
        // .msgCopyBtn's absolute positioning, same as it already does for
        // the error card and every other bubble.
        "if(role==='assistant'||role==='user'){const title=document.createElement('div');"
        "title.className='chatMsg-responseTitle';"
        "p.append(title);"
        // setMsgTokens() owns the title text so the '(Tokens: N)' suffix
        // renders identically here and when streamMessage() back-fills the
        // counts from the stream's 'complete' event.
        "setMsgTokens(p,role,tokenCount||0);}"
        // Rendered content lives in its own .msgBody child rather than
        // directly in p -- streamMessage() rewrites just that child's
        // innerHTML as tokens arrive, so the copy button appended below
        // survives every re-render instead of being wiped out along with
        // the old content each time.
        "const bodyEl=document.createElement('div');bodyEl.className='msgBody';"
        "bodyEl.innerHTML=renderMarkdown(content);p.append(bodyEl);"
        "addMessageCopyButton(p);addCodeCopyButtons(bodyEl);}"
        "box.append(p);return p;}"
        // Writes text to the clipboard and gives the clicked button
        // momentary 'Copied!' feedback so the click registers as having
        // worked, then restores its original label.
        "function copyToClipboard(text,btn){navigator.clipboard.writeText(text||'')"
        ".then(()=>{const old=btn.textContent;btn.textContent='Copied!';"
        "setTimeout(()=>{btn.textContent=old;},1500);});}"
        // One button per message bubble that copies the whole reply's raw
        // source (kept on p.dataset.raw, same field renderMarkdown()
        // re-renders from) -- not the HTML, so pasting elsewhere gets plain
        // text/Markdown rather than this page's markup.
        "function addMessageCopyButton(msgEl){const btn=document.createElement('button');"
        "btn.type='button';btn.className='msgCopyBtn';btn.textContent='Copy';"
        "btn.setAttribute('aria-label','Copy message');"
        "btn.addEventListener('click',()=>copyToClipboard(msgEl.dataset.raw,btn));"
        "msgEl.append(btn);}"
        // A chat reply has no location on disk of its own -- it's just
        // generated text -- so a model can never actually write a file for
        // it (that's what the [[TOOL_CALL]] write_file mechanism is for,
        // and only inside a project-bound chat, where the project root is a
        // known location). For every other chat, and for any snippet a user
        // wants saved somewhere other than the project, this button lets
        // the *user* pick a real save location instead. showSaveFilePicker
        // gives a native save dialog where supported (Chromium); the
        // Blob/anchor fallback below covers every other browser.
        "const LANG_EXT={javascript:'js',js:'js',jsx:'jsx',typescript:'ts',"
        "ts:'ts',tsx:'tsx',python:'py',py:'py',cpp:'cpp','c++':'cpp',"
        "c:'c',csharp:'cs','c#':'cs',java:'java',go:'go',golang:'go',"
        "rust:'rs',rs:'rs',ruby:'rb',rb:'rb',php:'php',html:'html',"
        "css:'css',scss:'scss',json:'json',yaml:'yaml',yml:'yaml',"
        "sql:'sql',bash:'sh',sh:'sh',shell:'sh',zsh:'sh',"
        "powershell:'ps1',ps1:'ps1',xml:'xml',markdown:'md',md:'md',"
        "kotlin:'kt',swift:'swift',dart:'dart',lua:'lua',"
        "dockerfile:'dockerfile',toml:'toml',ini:'ini'};"
        "async function saveCodeBlock(text,lang,btn){"
        "const ext=LANG_EXT[(lang||'').toLowerCase()]||'txt';"
        "const name='snippet.'+ext;"
        "if(window.showSaveFilePicker){try{"
        "const handle=await window.showSaveFilePicker({suggestedName:name});"
        "const writable=await handle.createWritable();"
        "await writable.write(text||'');await writable.close();"
        "const old=btn.textContent;btn.textContent='Saved!';"
        "setTimeout(()=>{btn.textContent=old;},1500);return;}"
        // AbortError is just the user cancelling the picker -- not a
        // failure worth falling back for. Any other error (e.g. the API
        // existing but being denied by policy) still falls through to the
        // anchor-download fallback below.
        "catch(e){if(e&&e.name==='AbortError')return;}}"
        "const blob=new Blob([text||''],{type:'text/plain'});"
        "const url=URL.createObjectURL(blob);"
        "const a=document.createElement('a');a.href=url;a.download=name;"
        "document.body.append(a);a.click();a.remove();"
        "setTimeout(()=>URL.revokeObjectURL(url),1000);"
        "const old=btn.textContent;btn.textContent='Saved!';"
        "setTimeout(()=>{btn.textContent=old;},1500);}"
        // One Copy + Save button pair per fenced code block (renderMarkdown()
        // emits <pre><code>) so a snippet can be copied or saved on its own
        // without the surrounding prose -- idempotent (skips a <pre> that
        // already has them) since streamed re-renders call this again on
        // every token.
        "function addCodeCopyButtons(container){"
        "container.querySelectorAll('pre').forEach(pre=>{"
        "if(pre.querySelector('.codeCopyBtn'))return;"
        "const btn=document.createElement('button');btn.type='button';"
        "btn.className='codeCopyBtn';btn.textContent='Copy';"
        "btn.setAttribute('aria-label','Copy code block');"
        "btn.addEventListener('click',e=>{e.stopPropagation();"
        "const code=pre.querySelector('code');"
        "copyToClipboard(code?code.textContent:pre.textContent,btn);});"
        "const saveBtn=document.createElement('button');saveBtn.type='button';"
        "saveBtn.className='codeSaveBtn';saveBtn.textContent='Save';"
        "saveBtn.setAttribute('aria-label','Save code block to a file');"
        "saveBtn.addEventListener('click',e=>{e.stopPropagation();"
        "const code=pre.querySelector('code');"
        "const lang=((code&&code.className)||'').replace('language-','');"
        "saveCodeBlock(code?code.textContent:pre.textContent,lang,saveBtn);});"
        "pre.append(saveBtn,btn);});}"
        // Best-effort LaTeX-to-HTML for the common constructs model replies
        // actually use (fractions, super/subscripts, Greek letters, common
        // operators) -- not a real TeX engine (this project takes no
        // third-party dependencies), just enough substitution that
        // \\frac{a}{b}, x_i, x^2, and \\theta read as intended instead of
        // showing their raw backslash source. Operates on already
        // HTML-escaped text (see renderInline below), so every replacement
        // only ever introduces the handful of safe tags below, never raw
        // user/model text as markup.
        "function renderMathExpr(t){"
        // Structural LaTeX first, before any symbol substitution: row
        // separators (\\) become line breaks, alignment tabs (a bare &,
        // which is &amp; by the time this runs on escaped text) vanish,
        // \begin{...}/\end{...} environment wrappers vanish, \text{}-style
        // upright-text commands keep just their contents, and \left/\right
        // delimiter-sizing prefixes vanish. Without these, an align*
        // environment leaked as literal 'beginalign*' / 'textradius' text.
        "t=t.replace(/\\\\\\\\\\\\\\\\/g,'<br>');"
        "t=t.replace(/&amp;/g,'');"
        "t=t.replace(/\\\\(?:begin|end)\\{[a-zA-Z]+\\*?\\}/g,'');"
        "t=t.replace(/\\\\(?:text|textbf|textit|mathrm|mathbf|mathit|operatorname)"
        "\\{([^{}]*)\\}/g,(m,a)=>a);"
        "t=t.replace(/\\\\(?:left|right)(?=[^a-zA-Z]|$)/g,'');"
        "t=t.replace(/\\\\frac\\{([^{}]*)\\}\\{([^{}]*)\\}/g,"
        "(m,a,b)=>'<span class=\"frac\"><span class=\"fracNum\">'+a+"
        "'</span><span class=\"fracDen\">'+b+'</span></span>');"
        "t=t.replace(/\\\\sqrt\\{([^{}]*)\\}/g,(m,a)=>'\\u221a('+a+')');"
        "t=t.replace(/\\^\\{([^{}]*)\\}/g,(m,a)=>'<sup>'+a+'</sup>');"
        "t=t.replace(/\\^(\\w)/g,(m,a)=>'<sup>'+a+'</sup>');"
        "t=t.replace(/_\\{([^{}]*)\\}/g,(m,a)=>'<sub>'+a+'</sub>');"
        "t=t.replace(/_(\\w)/g,(m,a)=>'<sub>'+a+'</sub>');"
        "const symbols={alpha:'\\u03b1',beta:'\\u03b2',gamma:'\\u03b3',"
        "delta:'\\u03b4',epsilon:'\\u03b5',zeta:'\\u03b6',eta:'\\u03b7',"
        "theta:'\\u03b8',lambda:'\\u03bb',mu:'\\u03bc',nu:'\\u03bd',"
        "pi:'\\u03c0',rho:'\\u03c1',sigma:'\\u03c3',tau:'\\u03c4',"
        "phi:'\\u03c6',chi:'\\u03c7',psi:'\\u03c8',omega:'\\u03c9',"
        "Delta:'\\u0394',Theta:'\\u0398',Lambda:'\\u039b',Pi:'\\u03a0',"
        "Sigma:'\\u03a3',Phi:'\\u03a6',Psi:'\\u03a8',Omega:'\\u03a9',"
        "cdot:'\\u00b7',times:'\\u00d7',pm:'\\u00b1',mp:'\\u2213',"
        "leq:'\\u2264',geq:'\\u2265',neq:'\\u2260',approx:'\\u2248',"
        "infty:'\\u221e',to:'\\u2192',rightarrow:'\\u2192',"
        "partial:'\\u2202',nabla:'\\u2207',in:'\\u2208',"
        "sum:'\\u2211',int:'\\u222b',prod:'\\u220f'};"
        "t=t.replace(/\\\\([a-zA-Z]+)/g,(m,name)=>"
        "symbols[name]!==undefined?symbols[name]:name);"
        "return t.replace(/[{}]/g,'');}"
        // Turns Markdown-style [label](url) links and bare http(s):// URLs
        // into real anchors. Runs on already-HTML-escaped text (same
        // convention as the rest of renderInline), so a URL/label can never
        // inject markup of its own -- the only tags this ever introduces are
        // the <a> wrapper below. A markdown link's own url is matched first
        // so the bare-URL pass afterward (which requires a preceding
        // whitespace/paren/line-start boundary) never re-wraps the url this
        // already placed inside an href=\"...\" attribute. Trailing
        // punctuation (a period ending the sentence, a closing bracket from
        // surrounding prose) is peeled off the bare-URL match so it reads
        // outside the link instead of becoming part of the href.
        "function linkify(t){"
        "t=t.replace(/\\[([^\\]<>]+)\\]\\((https?:\\/\\/[^\\s()<>]+)\\)/g,"
        "(m,label,url)=>'<a href=\"'+url+'\" target=\"_blank\" "
        "rel=\"noopener noreferrer\">'+label+'</a>');"
        "t=t.replace(/(^|[\\s(])(https?:\\/\\/[^\\s<>()]+)/g,(m,pre,url)=>{"
        "let trail='';while(url&&/[.,;:!?]$/.test(url)){"
        "trail=url.slice(-1)+trail;url=url.slice(0,-1);}"
        "if(!url)return m;"
        "return pre+'<a href=\"'+url+'\" target=\"_blank\" "
        "rel=\"noopener noreferrer\">'+url+'</a>'+trail;});"
        "return t;}"
        // Inline span-level formatting within a single line: linkify (see
        // above), backtick code spans, **bold**, and \\(...\\) inline math.
        // Operates on already-HTML-escaped text so the replacement groups
        // never need escaping themselves.
        "function renderInline(text){let t=esc(text);t=linkify(t);"
        "t=t.replace(/`([^`]+)`/g,(m,c)=>'<code>'+c+'</code>');"
        "t=t.replace(/\\*\\*([^*]+)\\*\\*/g,(m,b)=>'<strong>'+b+'</strong>');"
        "t=t.replace(/\\\\\\((.+?)\\\\\\)/g,"
        "(m,e)=>'<span class=\"math\">'+renderMathExpr(e)+'</span>');"
        "return t;}"
        // Small block-level Markdown renderer covering what model replies
        // actually use: fenced code blocks, bullet and numbered lists, and
        // paragraphs with soft line breaks. Deliberately not a full Markdown
        // parser -- just enough structure to read code and lists correctly
        // instead of everything collapsing into a single run-on line.
        "function renderMarkdownBlocks(raw){const lines=String(raw==null?'':raw)"
        ".replace(/\\r\\n/g,'\\n').split('\\n');let html='',i=0;"
        "while(i<lines.length){const line=lines[i];"
        // Leading whitespace is tolerated here (and stripped back out of
        // the code lines below) because models -- Granite in particular --
        // commonly indent a fenced block as a numbered-list continuation
        // (e.g. under \"1. Do X\"). The list handler bails out of the list
        // on that indented line and falls through to this parser, so an
        // indent-blind fence regex would leave the ``` markers rendered as
        // literal paragraph text instead of a copyable <pre><code> block.
        "const fence=/^(\\s*)```(\\w*)\\s*$/.exec(line);"
        "if(fence){const indent=fence[1];const lang=fence[2];i++;const code=[];"
        "const stripRe=indent?new RegExp('^'+indent.replace(/[.*+?^${}()|[\\]\\\\]/g,'\\\\$&')):null;"
        "while(i<lines.length&&!/^\\s*```\\s*$/.test(lines[i])){"
        "code.push(stripRe?lines[i].replace(stripRe,''):lines[i]);i++;}"
        "i++;const cls=lang?' class=\"language-'+esc(lang)+'\"':'';"
        "html+='<pre><code'+cls+'>'+esc(code.join('\\n'))+'</code></pre>';continue;}"
        // Block math: \\[ ... \\], possibly spanning multiple lines (each
        // line of a wrapped equation is common in model output). Collected
        // as raw (unescaped) source and rendered the same way inline math
        // is, then dropped into its own centered block.
        "if(/^\\s*\\\\\\[/.test(line)){const mathLines=[line];i++;"
        "while(i<lines.length&&!/\\\\\\]\\s*$/.test(mathLines[mathLines.length-1])){"
        "mathLines.push(lines[i]);i++;}"
        "const joined=mathLines.join(' ');"
        "const inner=joined.replace(/^\\s*\\\\\\[/,'').replace(/\\\\\\]\\s*$/,'');"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(inner))+'</div>';continue;}"
        // Bare \\begin{align*}...\\end{align*}-style environments with no
        // \\[ \\] wrapper (how QWEN typically emits display math) get the
        // same centered math-block treatment -- renderMathExpr() itself
        // strips the begin/end wrappers, alignment tabs, and \\text{}.
        "if(/^\\s*\\\\begin\\{/.test(line)){const mathLines=[line];i++;"
        "while(i<lines.length&&!/\\\\end\\{[^}]*\\}\\s*$/"
        ".test(mathLines[mathLines.length-1])){mathLines.push(lines[i]);i++;}"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(mathLines.join(' ')))+"
        "'</div>';continue;}"
        // $$ ... $$ display math (the other delimiter models commonly use),
        // either on one line or spanning several until the closing $$.
        "if(/^\\s*\\$\\$/.test(line)){"
        "const mathLines=[line.replace(/^\\s*\\$\\$/,'')];i++;"
        "while(i<lines.length&&!/\\$\\$\\s*$/"
        ".test(mathLines[mathLines.length-1])){mathLines.push(lines[i]);i++;}"
        "const dollarInner=mathLines.join(' ').replace(/\\$\\$\\s*$/,'');"
        "html+='<div class=\"mathBlock\">'+renderMathExpr(esc(dollarInner))+"
        "'</div>';continue;}"
        // A single blank line between two list items (common in model
        // output that puts a blank line after every bullet for readability)
        // must not end the list -- otherwise each item becomes its own
        // one-item <ol>/<ul>, which is what made every ordered item render
        // as \"1.\" instead of counting up. Only a non-list line (or two
        // blank lines in a row) actually ends the list.
        "if(/^\\s*[-*]\\s+/.test(line)){const items=[];"
        "while(i<lines.length){"
        "if(/^\\s*[-*]\\s+/.test(lines[i])){"
        "items.push(renderInline(lines[i].replace(/^\\s*[-*]\\s+/,'')));i++;}"
        "else if(lines[i].trim()===''&&i+1<lines.length&&"
        "/^\\s*[-*]\\s+/.test(lines[i+1])){i++;}"
        "else break;}"
        "html+='<ul>'+items.map(x=>'<li>'+x+'</li>').join('')+'</ul>';continue;}"
        "if(/^\\s*\\d+[.)]\\s+/.test(line)){const items=[];"
        "while(i<lines.length){"
        "const m=/^\\s*(\\d+)[.)]\\s+/.exec(lines[i]);"
        "if(m){items.push({n:parseInt(m[1],10),"
        "text:renderInline(lines[i].replace(/^\\s*\\d+[.)]\\s+/,''))});i++;}"
        "else if(lines[i].trim()===''&&i+1<lines.length&&"
        "/^\\s*\\d+[.)]\\s+/.test(lines[i+1])){i++;}"
        "else break;}"
        // The model's own leading numbers are preserved instead of letting
        // the browser silently renumber every list from 1 -- start=
        // anchors the first item and a per-item value= carries the rest, so
        // a list that begins mid-sequence (e.g. continuing after a code
        // block) or skips a number still renders exactly as written.
        "const start=items.length?items[0].n:1;"
        "html+='<ol start=\"'+start+'\">'+items.map(x=>"
        "'<li value=\"'+x.n+'\">'+x.text+'</li>').join('')+'</ol>';continue;}"
        "if(line.trim()===''){i++;continue;}"
        "const para=[];"
        // A math block starting mid-paragraph (\\[, \\begin{...}, or $$ on
        // its own line right after prose) must end the paragraph, or the
        // paragraph loop swallows it and it never reaches the block
        // handlers above.
        "while(i<lines.length&&lines[i].trim()!==''&&!/^\\s*```/.test(lines[i])&&"
        "!/^\\s*[-*]\\s+/.test(lines[i])&&!/^\\s*\\d+[.)]\\s+/.test(lines[i])&&"
        "!/^\\s*\\\\\\[/.test(lines[i])&&!/^\\s*\\\\begin\\{/.test(lines[i])&&"
        "!/^\\s*\\$\\$/.test(lines[i])){"
        "para.push(renderInline(lines[i]));i++;}"
        "html+='<p>'+para.join('<br>')+'</p>';}"
        "return html;}"
        // Reasoning-capable models with thinking mode on (see
        // apply_reasoning_directive() server-side, Qwen's own /think
        // convention) emit a leading <think>...</think> block ahead of the
        // real answer. Pulled out here -- not server-side -- so the exact
        // same raw text keeps working whether it's a live stream still
        // mid-thought (tag open, no closing tag yet) or a reply reloaded
        // whole from chat history; both paths call this one function.
        // Rendered as a collapsed <details>, matching claude.ai's extended
        // -thinking panel, so the final answer stays the visually primary
        // thing in the bubble.
        "function renderMarkdown(raw){const s=String(raw==null?'':raw);"
        "const closed=/^\\s*<think>([\\s\\S]*?)<\\/think>/i.exec(s);"
        "if(closed){const rest=s.slice(closed.index+closed[0].length);"
        "return '<details class=\"thinkBlock\"><summary>Thinking</summary>"
        "<div class=\"thinkBody\">'+renderMarkdownBlocks(closed[1])+"
        "'</div></details>'+renderMarkdownBlocks(rest);}"
        "const open=/^\\s*<think>([\\s\\S]*)$/i.exec(s);"
        "if(open){return '<details class=\"thinkBlock\" open>"
        "<summary>Thinking&hellip;</summary><div class=\"thinkBody\">'+"
        "renderMarkdownBlocks(open[1])+'</div></details>';}"
        "return renderMarkdownBlocks(s);}"
        // Streaming re-render helper: instead of wholesale innerHTML
        // replacement on every token (which destroyed and recreated every
        // element -- resetting each code block's scroll position to the
        // top and making its scrollbar flicker), diff the freshly rendered
        // blocks against the live DOM and touch only what changed. An
        // unchanged block keeps its exact DOM node (scroll state, copy
        // button and all); the one block still growing has its <code> text
        // swapped in place, keeps its horizontal scroll, and follows its
        // own bottom while the user hasn't scrolled up inside it.
        "function patchMsgBody(el,html){"
        "const tpl=document.createElement('template');tpl.innerHTML=html;"
        "const next=Array.from(tpl.content.children);"
        "for(let i=0;i<next.length;i++){"
        "const a=el.children[i],b=next[i];"
        "if(!a){el.append(b);continue;}"
        "if(a.tagName==='PRE'&&b.tagName==='PRE'){"
        "const ac=a.querySelector('code'),bc=b.querySelector('code');"
        "if(ac&&bc){"
        "if(ac.textContent!==bc.textContent||ac.className!==bc.className){"
        // 'At the bottom' is measured before the update so a user who
        // scrolled up to read stays put; anyone at (or within a few px of)
        // the bottom keeps following the incoming code.
        "const follow=a.scrollTop+a.clientHeight>=a.scrollHeight-8;"
        "const left=a.scrollLeft;"
        "ac.className=bc.className;ac.textContent=bc.textContent;"
        "a.scrollLeft=left;"
        "if(follow)a.scrollTop=a.scrollHeight;}"
        "continue;}}"
        "if(a.outerHTML!==b.outerHTML)a.replaceWith(b);}"
        "while(el.children.length>next.length)el.lastElementChild.remove();}"
        // Escapes text for safe insertion as HTML content elsewhere in this
        // file (table cells built from trusted-looking but user-supplied
        // strings such as display names and diagnostics).
        "function esc(s){const d=document.createElement('div');"
        "d.textContent=s==null?'':String(s);return d.innerHTML;}"
        // Shortens long id/hash/name values (dataset ids, SHA digests, etc.)
        // so a table column never forces the page wider than the window --
        // the full value is still available on hover via the title
        // attribute. Used in place of esc() for any field whose value is
        // not a short human-authored label.
        "function escTrim(s,n){const full=s==null?'':String(s);"
        "if(full.length<=n)return esc(full);"
        "return '<span title=\"'+esc(full)+'\">'+esc(full.slice(0,n-1))+"
        "'\\u2026</span>';}"
        // Small inline-SVG icon set (Bootstrap-Icons-style outline glyphs)
        // used by the row-action toolbar buttons below instead of shipping
        // an icon font -- this app has no CDN/network dependency and none
        // of these screens warrant one just for icons.
        "const ICONS={"
        "check:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M13.7 3.3a1 1 0 0 1 0 1.4l-7 7a1 "
        "1 0 0 1-1.4 0l-3.5-3.5a1 1 0 1 1 1.4-1.4L6 9.6l6.3-6.3a1 1 0 0 1 "
        "1.4 0z\"/></svg>',"
        "trash:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M5.5 1a.5.5 0 0 0-.5.5V2H2.5a.5.5 "
        "0 0 0 0 1H3v10.5A1.5 1.5 0 0 0 4.5 15h7a1.5 1.5 0 0 0 "
        "1.5-1.5V3h.5a.5.5 0 0 0 0-1H11v-.5a.5.5 0 0 0-.5-.5h-5zM4 3h8v10.5a"
        ".5.5 0 0 1-.5.5h-7a.5.5 0 0 1-.5-.5V3zm2 2a.5.5 0 0 1 .5.5v6a.5.5 "
        "0 0 1-1 0v-6A.5.5 0 0 1 6 5zm4 0a.5.5 0 0 1 .5.5v6a.5.5 0 0 "
        "1-1 0v-6a.5.5 0 0 1 .5-.5z\"/></svg>',"
        "x:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M4.3 4.3a1 1 0 0 1 1.4 0L8 6.6l"
        "2.3-2.3a1 1 0 1 1 1.4 1.4L9.4 8l2.3 2.3a1 1 0 0 1-1.4 1.4L8 9.4l"
        "-2.3 2.3a1 1 0 0 1-1.4-1.4L6.6 8 4.3 5.7a1 1 0 0 1 0-1.4z\"/>"
        "</svg>',"
        "play:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M4 2.5a.5.5 0 0 1 .77-.42l9 5.5a."
        "5.5 0 0 1 0 .84l-9 5.5A.5.5 0 0 1 4 13.5v-11z\"/></svg>',"
        "eye:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M16 8s-3-5.5-8-5.5S0 8 0 8s3 5.5 "
        "8 5.5S16 8 16 8zM1.173 8a13.13 13.13 0 0 1 1.66-2.043C4.12 4.668 "
        "5.88 3.5 8 3.5c2.12 0 3.879 1.168 5.168 2.457A13.13 13.13 0 0 1 "
        "14.828 8c-.058.087-.122.183-.195.288-.335.48-.83 1.12-1.465 "
        "1.755C11.879 11.332 10.119 12.5 8 12.5c-2.12 0-3.879-1.168-"
        "5.168-2.457A13.13 13.13 0 0 1 1.172 8z\"/><path d=\"M8 5.5a2.5 "
        "2.5 0 1 0 0 5 2.5 2.5 0 0 0 0-5zM4.5 8a3.5 3.5 0 1 1 7 0 3.5 3.5 "
        "0 0 1-7 0z\"/></svg>',"
        "gear:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M9.405 1.05c-.413-1.4-2.397-1.4-"
        "2.81 0l-.1.34a1.464 1.464 0 0 1-2.105.872l-.31-.17c-1.283-.698-"
        "2.686.705-1.987 1.987l.169.311c.446.82.023 1.841-.872 2.105l-.34."
        "1c-1.4.413-1.4 2.397 0 2.81l.34.1a1.464 1.464 0 0 1 .872 2.105l-"
        ".17.31c-.698 1.283.705 2.686 1.987 1.987l.311-.169a1.464 1.464 0 "
        "0 1 2.105.872l.1.34c.413 1.4 2.397 1.4 2.81 0l.1-.34a1.464 1.464 "
        "0 0 1 2.105-.872l.31.17c1.283.698 2.686-.705 1.987-1.987l-.169-"
        ".311a1.464 1.464 0 0 1 .872-2.105l.34-.1c1.4-.413 1.4-2.397 0-"
        "2.81l-.34-.1a1.464 1.464 0 0 1-.872-2.105l.17-.31c.698-1.283-"
        ".705-2.686-1.987-1.987l-.311.169a1.464 1.464 0 0 1-2.105-.872l-"
        ".1-.34zM8 10.93a2.929 2.929 0 1 1 0-5.858 2.929 2.929 0 0 1 0 "
        "5.858z\"/></svg>',"
        "pause:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M5 3.5A1.5 1.5 0 0 1 6.5 5v6a1.5 "
        "1.5 0 0 1-3 0V5A1.5 1.5 0 0 1 5 3.5zm6 0A1.5 1.5 0 0 1 12.5 5v6a"
        "1.5 1.5 0 0 1-3 0V5A1.5 1.5 0 0 1 11 3.5z\"/></svg>',"
        "stop:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M3.5 3.5A1 1 0 0 1 4.5 2.5h7a1 1 "
        "0 0 1 1 1v7a1 1 0 0 1-1 1h-7a1 1 0 0 1-1-1v-7z\"/></svg>',"
        "pencil:'<svg viewBox=\"0 0 16 16\" width=\"14\" height=\"14\" "
        "fill=\"currentColor\"><path d=\"M12.146.146a.5.5 0 0 1 .708 0l3 "
        "3a.5.5 0 0 1 0 .708l-10 10a.5.5 0 0 1-.168.11l-5 2a.5.5 0 0 1-"
        ".65-.65l2-5a.5.5 0 0 1 .11-.168l10-10zM11.207 2.5 13.5 4.793 "
        "14.793 3.5 12.5 1.207 11.207 2.5zm1.586 3L10.5 3.207 4 9.707V10h."
        "5a.5.5 0 0 1 .5.5v.5h.5a.5.5 0 0 1 .5.5v.5h.293l6.5-6.5z\"/></svg>'"
        "};"
        // Renders a single icon-only action button. `label` is both the
        // tooltip and the accessible name since the button carries no
        // visible text -- callers group several of these in a
        // '.rowToolbar' div that sits above the row's descriptive text
        // instead of stretching the row with full-width text buttons.
        // `extraAttr` is an optional raw HTML attribute (e.g. 'disabled').
        "function iconBtn(icon,label,attr,id,extraClass,extraAttr){"
        "return '<button type=\"button\" class=\"iconBtn'+"
        "(extraClass?' '+extraClass:'')+'\" data-'+attr+'=\"'+id+"
        "'\" title=\"'+esc(label)+'\" aria-label=\"'+esc(label)+'\"'+"
        "(extraAttr?' '+extraAttr:'')+'>'+"
        "ICONS[icon]+'</button>';}"
        "function applyBtn(attr,id){return iconBtn('check','Apply',attr,id,"
        "'iconBtn-apply');}"
        "function deleteBtn(attr,id){return iconBtn('trash','Delete',attr,"
        "id,'iconBtn-delete');}"
        "function approveBtn(attr,id){return iconBtn('check','Approve',"
        "attr,id,'iconBtn-apply');}"
        "function rejectBtn(attr,id){return iconBtn('x','Reject',attr,id,"
        "'iconBtn-delete');}"
        "function runBtn(attr,id,label,extraAttr){return iconBtn('play',"
        "label||'Run',attr,id,'',extraAttr);}"
        "function viewBtn(attr,id,label){return iconBtn('eye',"
        "label||'View',attr,id,'');}"
        "function configureBtn(attr,id){return iconBtn('gear','Configure',"
        "attr,id,'');}"
        "function toolbar(){return '<div class=\"rowToolbar\">'+"
        "Array.prototype.slice.call(arguments).join('')+'</div>';}"
        // Field hint bubbles (ML forms clarity pass): appended straight
        // after a <label>'s own visible text (see every call site below),
        // so the '?' badge sits inline with the field name it explains
        // rather than as a separate row. tabindex=0 lets a keyboard user
        // reach it (the CSS bubble also opens on :focus, not just :hover).
        // Hidden entirely via body.hintsOff -- see the CSS comment on
        // .mlHint and the settings checkbox wiring near applyHintsPref()
        // below -- rather than merely left empty, so a user who turns
        // hints off gets exactly the same layout as before this pass.
        "function hint(text){return '<span class=\"mlHint\" tabindex=\"0\">"
        "?<span class=\"mlHintBubble\">'+esc(text)+'</span></span>';}"
        // Multi-select control (ML forms clarity pass): renders a
        // scrollable checkbox list from [{value,label}] options -- the
        // dropdown-of-names equivalent for a field that must accept more
        // than one id/name at once (a plain <select> can only ever submit
        // one value per name without multi-select semantics real users
        // find awkward, e.g. ctrl-click). multiSelectValues() reads the
        // checked boxes back out as the same comma-separated string the
        // server already expects, so no server-side change was needed to
        // adopt this control on an existing field.
        "function multiSelect(id,options){"
        "return '<div class=\"multiSelect\" id=\"'+id+'\">'+"
        "options.map(o=>'<label><input type=\"checkbox\" value=\"'+"
        "esc(o.value)+'\"> '+esc(o.label)+'</label>').join('')+'</div>';}"
        "function multiSelectValues(id){const el=q('#'+id);if(!el)return'';"
        "return Array.prototype.slice.call("
        "el.querySelectorAll('input:checked')).map(i=>i.value).join(',');}"
        "function multiSelectSetChecked(id,csv){const el=q('#'+id);"
        "if(!el)return;const wanted=new Set((csv||'').split(',')"
        ".map(s=>s.trim()).filter(Boolean));"
        "el.querySelectorAll('input').forEach(i=>{"
        "i.checked=wanted.has(i.value);});}"
        // Reads the "Show field hints" preference (default on) from
        // localStorage -- a personal per-browser display preference, not
        // server configuration, so it needs no backend field and applies
        // instantly without a save round-trip. Called once on every page
        // load (see load() below) and again the moment the Machine
        // Learning Settings checkbox changes.
        "function applyHintsPref(){"
        "const off=localStorage.getItem('mlHintsOff')==='1';"
        "document.body.classList.toggle('hintsOff',off);"
        "const cb=q('#cfgHintsEnabled');if(cb)cb.checked=!off;}"
        // Builds a simple two-column-plus-actions HTML table from rows,
        // replacing the raw JSON dumps every list used to show verbatim --
        // this is a user-facing screen, not a debugging console.
        // Wrapped in a scrollable div so a wide table (many ML tables run
        // to 6-7 columns of status pickers and action buttons) scrolls
        // inside its own box instead of forcing the whole page wider.
        "function table(headers,rows){if(!rows.length)return'<p>Nothing here yet.</p>';"
        "let h='<div style=\"overflow-x:auto\"><table><thead><tr>';"
        "for(const header of headers)h+='<th>'+esc(header)+'</th>';"
        "h+='</tr></thead><tbody>';"
        "for(const row of rows){h+='<tr>';for(const cell of row)h+='<td>'+cell+'</td>';h+='</tr>';}"
        "return h+'</tbody></table></div>';}"
        "function renderProjects(projects){const el=q('#projectsList');if(!el)return;"
        "el.innerHTML=table(['Project','ID'],projects.map(x=>"
        "[esc(x.displayName),esc(x.id)]));}"
        "function renderModels(models){const el=q('#modelsList');if(!el)return;"
        "el.innerHTML=table(['Model','Category','Quantization','Status','Recommended RAM','Notes'],"
        "models.map(x=>[esc(x.displayName),esc(x.category),esc(x.quantization||'-'),"
        "'<span class=\"stateTag stateTag-'+esc(x.state)+'\">'+esc(x.state)+'</span>',"
        "x.recommendedRamMiB?Math.round(x.recommendedRamMiB/1024)+' GB':'-',"
        "esc(x.diagnostic)]));}"
        "function renderBenchmarks(benchmarks){const el=q('#benchmarksList');if(!el)return;"
        "el.innerHTML=table(['Model','Profile','Passed','Tokens/sec'],"
        "benchmarks.map(x=>[esc(x.modelId),esc(x.profile),"
        "x.passedCases+' / '+x.totalCases,Number(x.tokensPerSecond).toFixed(1)]));}"
        // Each row's toolbar lets an administrator remove a user from
        // authentication (UserRecord::enabled -- see set_user_status's own
        // comment in server.cpp) without deleting the account or its audit
        // history. The current user's own row never gets the button: the
        // server also refuses a self-disable, but hiding it here avoids a
        // click that can only ever come back as an error.
        "function renderUsers(users,meId){const el=q('#usersList');if(!el)return;"
        "el.innerHTML=users.length?table(['User','Display name','Role','Status',''],"
        "users.map(x=>[esc(x.username),esc(x.displayName),esc(x.role),"
        "'<span class=\"stateTag stateTag-'+(x.enabled?'approved':'rejected')+"
        "'\">'+(x.enabled?'enabled':'disabled')+'</span>',"
        "x.id===meId?'':toolbar(x.enabled?"
        "iconBtn('trash','Disable (remove from authentication)',"
        "'disable-user',x.id,'iconBtn-delete'):"
        "iconBtn('check','Enable (restore authentication)','enable-user',"
        "x.id,'iconBtn-apply'))])):"
        "'<p>No users visible, or administrator access is required.</p>';"
        "for(const btn of el.querySelectorAll('[data-disable-user],"
        "[data-enable-user]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.disableUser||btn.dataset.enableUser;"
        "const enabling=!!btn.dataset.enableUser;"
        "try{await api('/api/v1/users/'+encodeURIComponent(id)+'/status',"
        "'POST',{enabled:enabling});await load();}"
        "catch(x){showSystemError((enabling?'Enable':'Disable')+"
        "' user failed: '+x.message);}});}}"
        // Phase 30A system-configuration editor: reads/writes settings.json
        // fields by dotted path (matching each input's data-path attribute)
        // instead of one hand-written getter/setter pair per field --
        // cfgGet/cfgSet below are the only place that indirection lives.
        "function cfgGet(obj,path){return path.split('.').reduce("
        "(v,k)=>(v==null?v:v[k]),obj);}"
        "function cfgSet(obj,path,value){const keys=path.split('.');"
        "let node=obj;for(let i=0;i<keys.length-1;i++)node=node[keys[i]];"
        "node[keys[keys.length-1]]=value;}"
        // Keeps the full document last loaded from the server so submit can
        // overlay only the fields this form actually exposes back onto it --
        // fields the form never shows (host/port, TLS material, storage
        // roots, allow-lists, ...) are round-tripped unchanged instead of
        // being reconstructed (and possibly gotten wrong) client-side.
        "let systemConfigDocument=null;"
        "function renderSystemConfig(cfg){const form=q('#systemConfigForm');"
        "if(!form)return;if(!cfg){"
        "q('#systemConfigStatus').textContent="
        "'System configuration is unavailable (administrator access, or a "
        "running settings.json path, is required).';return;}"
        "systemConfigDocument=cfg;"
        "form.querySelectorAll('[data-path]').forEach(input=>{"
        "const value=cfgGet(cfg,input.dataset.path);if(value===undefined)return;"
        "if(input.dataset.type==='bool')input.checked=!!value;"
        "else if(input.dataset.type==='json')"
        "input.value=JSON.stringify(value,null,2);"
        "else input.value=value;});}"
        "async function submitSystemConfig(e){e.preventDefault();"
        "const status=q('#systemConfigStatus');status.textContent='Saving...';"
        "try{if(!systemConfigDocument)"
        "throw new Error('configuration was not loaded');"
        "const updated=JSON.parse(JSON.stringify(systemConfigDocument));"
        "q('#systemConfigForm').querySelectorAll('[data-path]').forEach("
        "input=>{const value=input.dataset.type==='bool'?input.checked:"
        "(input.dataset.type==='json'?JSON.parse(input.value||'{}'):"
        "(input.type==='number'?Number(input.value):input.value));"
        "cfgSet(updated,input.dataset.path,value);});"
        "const result=await api('/api/v1/admin/config','POST',updated);"
        "systemConfigDocument=result.configuration;"
        "status.textContent=result.restartRequired?"
        "'Saved. Some changed fields only take effect after MasterAI is "
        "restarted.':'Saved and applied.';}"
        "catch(x){showSystemError('Could not save configuration: '+x.message);"
        "status.textContent='';}}"
        // System Report (Report sidebar): renders the single consolidated
        // read from GET /api/v1/system/report. Early-returns when the
        // target element is absent (any non-administrator page load, and
        // any page other than the report itself) rather than assuming this
        // section is always present -- same convention as every other
        // render*() helper here.
        "function mib(x){return Number(x).toLocaleString()+' MiB';}"
        "function gib(mibValue){return (Number(mibValue)/1024).toFixed(1)+"
        "' GiB';}"
        "function bytesGib(x){return (Number(x)/1073741824).toFixed(2)+"
        "' GiB';}"
        "function renderSystemReport(report){const el=q('#systemReport');"
        "if(!el)return;if(!report){el.textContent="
        "'System Report is unavailable (administrator access is required).';"
        "return;}"
        "const h=report.hardware,p=report.process,mem=report.memory,"
        "spf=report.systemPageFile,pf=report.pageFile,f=report.features;"
        "const row=(label,value)=>'<tr><td class=\"reportLabel\">'+esc(label)+"
        "'</td><td class=\"reportValue\">'+value+'</td></tr>';"
        "const section=(title,rows)=>'<div class=\"reportSection\">"
        "<h3>'+esc(title)+'</h3><table><tbody>'+rows.join('')+"
        "'</tbody></table></div>';"
        "el.innerHTML="
        "section('Hardware',["
        "row('Platform / architecture',esc(h.platform)+' / '+esc(h.architecture)),"
        "row('CPUs',h.physicalCpus+' physical / '+h.logicalCpus+' logical, '+"
        "h.numaNodes+' NUMA node(s)'),"
        "row('System RAM',gib(h.totalRamMiB)+' total, '+gib(h.availableRamMiB)+"
        "' available'),"
        "row('GPU memory',h.gpuMemoryMiB?gib(h.gpuMemoryMiB):'none detected'),"
        "row('GPU backends',h.gpuBackends.length?h.gpuBackends.map(esc)."
        "join(', '):'none')"
        "])+"
        "section('System pagefile',["
        "row('Commit limit (RAM + system pagefile)',gib(spf.totalVirtualMemoryMiB)),"
        "row('Available',gib(spf.availableVirtualMemoryMiB))"
        "])+"
        "section('MasterAI PageFile (virtual pagefile substitute)',["
        "row('Location',esc(pf.location)+(pf.usingDefault?"
        "' (default -- set Storage &gt; PageFile location in System "
        "configuration to move it)':' (custom location)')),"
        // Spelled out as total/used/free rather than "capacity / free" --
        // that paired phrasing read as "used / free" at a glance, making an
        // almost-empty drive (e.g. 40 GiB total, 39.9 GiB free) look like it
        // had consumed nearly all of its own capacity.
        "row('Drive (total / used / free)',gib(pf.driveCapacityMiB)+' / '+"
        "gib(pf.driveCapacityMiB-pf.driveFreeMiB)+' / '+gib(pf.driveFreeMiB)),"
        "row('MasterAI usage (used / capacity)',bytesGib(pf.usedBytes)+' / '+"
        "bytesGib(pf.capacityBytes))"
        "])+"
        "section('Memory pressure',["
        "row('Pressure level',esc(mem.pressure||'')),"
        "row('Reserved',bytesGib(mem.reservedBytes||0))"
        "])+"
        "section('Process',["
        "row('Resident / private memory',bytesGib(p.residentMemoryBytes)+"
        "' / '+bytesGib(p.privateMemoryBytes)),"
        "row('Commit',bytesGib(p.commitBytes)),"
        "row('Page faults',Number(p.pageFaults).toLocaleString())"
        "])+"
        "section('Enabled features',["
        "row('Accelerator policy',esc(f.acceleratorPolicy)),"
        "row('Retrieval',f.retrievalEnabled?'enabled':'disabled'),"
        "row('Cache',f.cacheEnabled?'enabled':'disabled'),"
        "row('Session reuse',f.sessionReuseEnabled?'enabled':'disabled'),"
        "row('Automatic calibration',f.performanceAutoTune?'enabled':"
        "'disabled'),"
        "row('Project file watching',f.watchProjectFiles?'enabled':"
        "'disabled'),"
        "row('Local password accounts',f.allowLocalPasswordAccounts?"
        "'allowed':'not allowed'),"
        "row('OS-integrated sign-in',f.allowOsIdentityAccounts?'allowed':"
        "'not allowed'),"
        "row('Require sign-in',f.authenticationEnabled?'on (normal)':"
        "'off (no login required)'),"
        "row('TLS mode',esc(f.tlsMode))"
        "]);}"
        // Renders the Machine Learning foundation page: an acknowledgement
        // line, the real dashboard counts, and the interface roadmap with
        // each entry tagged available/planned -- see MachineLearningRegistry's
        // class comment in masterai.hpp. Phase 92 (consolidation): the five
        // real content pages that make up the core pipeline (and the
        // Dashboard's own self-listing) are now excluded from this table --
        // application_page()'s ml_pipeline_overview() above already shows
        // those five, as real links, in pipeline order; repeating them here
        // as a sixth/plain-text copy would just be the same information
        // twice. ML_DASHBOARD_ROUTE_BY_KEY turns every remaining interface's
        // key into that page's real URL so this table becomes a set of
        // links too, not just a status list.
        "const ML_DASHBOARD_ROUTE_BY_KEY={'model-registry':'/app/ml/models',"
        "'model-builder':'/app/ml/model-builder-configs',"
        "'subject-knowledge':'/app/ml/subjects',"
        "'data-labeling':'/app/ml/label-tasks',"
        "'data-preparation':'/app/ml/prep-jobs',"
        "'fine-tuning':'/app/ml/fine-tuning-jobs',"
        "'checkpoint-management':'/app/ml/checkpoints',"
        "'hyperparameter-optimization':'/app/ml/hyperparameter-searches',"
        "'ensemble-methods':'/app/ml/ensembles',"
        "'experiment-tracking':'/app/ml/experiments',"
        "'subject-examination':'/app/ml/subject-exams',"
        "'model-optimization':'/app/ml/model-optimizations',"
        "'prompt-instruction-training':'/app/ml/instruction-examples',"
        "'embeddings-vector-stores':'/app/ml/vector-stores',"
        "'retrieval-augmented-generation':'/app/ml/rag-configs',"
        "'synthetic-data':'/app/ml/synthetic-records',"
        "'model-comparison':'/app/ml/model-comparisons',"
        "'deployment-manager':'/app/ml/deployments',"
        "'inference-endpoints':'/app/ml/inference-endpoints',"
        "'hardware-compute':'/app/ml/compute-nodes',"
        "'automation-pipelines':'/app/ml/automation-pipelines',"
        "'safety-governance':'/app/ml/safety-governance',"
        "'monitoring-diagnostics':'/app/ml/monitoring',"
        "'audit-logs':'/app/ml/audit-logs',"
        "'ml-settings':'/app/ml/settings'};"
        "const ML_DASHBOARD_CORE_KEYS=['dashboard','projects',"
        "'dataset-manager','training-jobs','evaluation-lab'];"
        "function renderMlDashboard(ml){const ack=q('#mlAck');if(!ack)return;"
        "ack.textContent=ml.enabled?"
        "'Machine Learning module is enabled ('+ml.phase+' phase).':"
        "'Machine Learning module is unavailable.';"
        "q('#mlStats').innerHTML=table(['Metric','Count'],["
        "['Active projects','activeProjects'],"
        "['Models training','modelsTraining'],"
        "['Models awaiting evaluation','modelsAwaitingEvaluation'],"
        "['Models awaiting approval','modelsAwaitingApproval'],"
        "['Deployed models','deployedModels'],"
        "['Failed training jobs','failedTrainingJobs']]"
        ".map(([label,key])=>[label,String(ml[key])]));"
        "q('#mlInterfaces').innerHTML=table(['Interface','Status'],"
        "ml.interfaces.filter(x=>!ML_DASHBOARD_CORE_KEYS.includes(x.key))"
        ".map(x=>{const route=ML_DASHBOARD_ROUTE_BY_KEY[x.key];"
        "return[route?'<a href=\"'+route+'\">'+esc(x.label)+'</a>':esc(x.label),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+esc(x.status)+"
        "'</span>'];}));}"
        // Renders the ML Projects list with a Delete button per row -- the
        // only mutation this foundation phase supports beyond create, since
        // status transitions belong to the training/evaluation/deployment
        // phases that don't exist yet (see MLProjectStore's comment).
        "function renderMlProjects(projects){const el=q('#mlProjectsList');"
        "if(!el)return;"
        "if(!projects.length){el.innerHTML='<p>No Machine Learning projects "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Subject domain','Model task',"
        "'Administrators','Status',''],"
        "projects.map(x=>[esc(x.name),esc(x.subjectDomain),esc(x.modelTask),"
        "escTrim(x.administrators,24)||'-',"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+esc(x.status)+"
        "'</span>',"
        "toolbar(configureBtn('edit-ml-project-governance',x.id),"
        "deleteBtn('delete-ml-project',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-delete-ml-project]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/projects/'+"
        "encodeURIComponent(btn.dataset.deleteMlProject)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete Machine Learning project failed: '+"
        "x.message);}});}"
        // Phase 93: "Configure" loads the project's real governance fields
        // (not just id) into the Governance form below the table.
        "for(const btn of el.querySelectorAll("
        "'[data-edit-ml-project-governance]')){"
        "btn.addEventListener('click',()=>{"
        "const id=btn.dataset.editMlProjectGovernance;"
        "const p=projects.find(y=>y.id===id);if(!p)return;"
        "q('#mlProjectGovernanceId').value=id;"
        "q('#mlProjectAdministrators').value=p.administrators||'';"
        "q('#mlProjectApprovedDataSources').value=p.approvedDataSources||'';"
        "q('#mlProjectSecurityClassification').value="
        "p.securityClassification||'';"
        "q('#mlProjectTargetArchitecture').value=p.targetArchitecture||'';"
        "q('#mlProjectTargetDeploymentEnvironment').value="
        "p.targetDeploymentEnvironment||'';"
        "q('#mlProjectSuccessCriteria').value=p.successCriteria||'';"
        "q('#mlProjectEvaluationRequirements').value="
        "p.evaluationRequirements||'';"
        "q('#mlProjectSafetyRequirements').value=p.safetyRequirements||'';"
        "q('#mlProjectStorageAllocationMb').value="
        "p.storageAllocationMb||0;"
        "q('#mlProjectComputeAllocationNotes').value="
        "p.computeAllocationNotes||'';});}}"
        // Model Registry (docs/PLAN.md "Machine Learning Abilities" section
        // 7): each row carries its own lifecycle-state dropdown so an
        // administrator can move a model along its states one deliberate
        // step at a time, plus a Delete button. The dropdown always lists
        // every known state -- the server enforces the one hard rule
        // (production requires approved/staging first, see
        // ModelRegistryStore::set_state) and reports the reason back if it
        // rejects the change.
        "const MODEL_STATES=['imported','unverified','verified','training',"
        "'evaluation','rejected','approved','staging','production',"
        "'deprecated','archived','quarantined'];"
        "function renderMlModels(models){const el=q('#mlModelsList');"
        "if(!el)return;"
        "if(!models.length){el.innerHTML='<p>No models registered yet.</p>';"
        "return;}"
        "el.innerHTML=table(['Name','Version','Family','Task','Quantization','State','Set state'],"
        "models.map(x=>[esc(x.displayName||x.name),esc(x.version),"
        "esc(x.family),esc(x.task),esc(x.quantization||'unknown'),"
        "'<span class=\"stateTag stateTag-'+esc(x.state)+'\">'+esc(x.state)+"
        "'</span>',"
        "toolbar('<select data-state-for=\"'+x.id+'\">'+MODEL_STATES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.state?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',applyBtn('apply-state',x.id),"
        "deleteBtn('delete-ml-model',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-state]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyState;"
        "const state=el.querySelector('[data-state-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/models/'+encodeURIComponent(id)+'/state',"
        "'POST',{state});await load();}"
        "catch(x){showSystemError('Update model state failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-model]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/models/'+"
        "encodeURIComponent(btn.dataset.deleteMlModel)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete model failed: '+x.message);}});}}"
        // Dataset Manager (docs/PLAN.md "Machine Learning Abilities"
        // section 10): each row shows its approval status plus Approve,
        // Reject, and Delete actions.
        "function renderMlDatasets(datasets){const el=q('#mlDatasetsList');"
        "if(!el)return;"
        "if(!datasets.length){el.innerHTML='<p>No datasets registered yet.</p>';"
        "return;}"
        "el.innerHTML=table(['Name','Purpose','Subject area','Format','Content',"
        "'Version','Quality','Duplicates','Approval',''],"
        "datasets.map(x=>[esc(x.name),"
        "x.purpose==='instruction'?'Instruction / fine-tuning':'Tabular',"
        "esc(x.subjectArea),esc(x.dataFormat),"
        // ML forms clarity pass: makes the exact gap that used to only
        // surface as a raw ml_dataset_has_no_content error at training
        // time visible right here in the dataset list.
        "'<span class=\"stateTag stateTag-'+(x.hasContent?'ready':'missing')+"
        "'\">'+(x.hasContent?x.contentRows+' row(s)':'no content')+'</span>',"
        // Phase 94: real content-derived metrics, computed at upload time --
        // see Dataset::record_count's comment in masterai.hpp.
        "x.currentVersion?('v'+x.currentVersion):'-',"
        "x.recordCount?(Math.round(x.dataQualityScore*100)+'%'):'-',"
        "x.recordCount?(Math.round(x.duplicateRate*100)+'%'):'-',"
        "'<span class=\"stateTag stateTag-'+esc(x.approvalStatus)+'\">'+"
        "esc(x.approvalStatus)+'</span>',"
        "toolbar(approveBtn('approve-ml-dataset',x.id),"
        "rejectBtn('reject-ml-dataset',x.id),"
        "configureBtn('edit-ml-dataset-declaration',x.id),"
        "viewBtn('history-ml-dataset',x.id,'History'),"
        "deleteBtn('delete-ml-dataset',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-approve-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.approveMlDataset)+'/approve','POST',"
        "{status:'approved'});await load();}"
        "catch(x){showSystemError('Approve dataset failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-reject-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.rejectMlDataset)+'/approve','POST',"
        "{status:'rejected'});await load();}"
        "catch(x){showSystemError('Reject dataset failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(btn.dataset.deleteMlDataset)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete dataset failed: '+x.message);}});}"
        // Phase 94: "Configure" loads the dataset's declared fields
        // (sensitive-data status, split percentages) into the Declare
        // metadata form below the table.
        "for(const btn of el.querySelectorAll("
        "'[data-edit-ml-dataset-declaration]')){"
        "btn.addEventListener('click',()=>{"
        "const id=btn.dataset.editMlDatasetDeclaration;"
        "const d=datasets.find(y=>y.id===id);if(!d)return;"
        "q('#mlDatasetDeclareId').value=id;"
        "q('#mlDatasetSensitiveDataStatus').value=d.sensitiveDataStatus||'';"
        "q('#mlDatasetTrainSplitPercent').value=d.trainSplitPercent||0;"
        "q('#mlDatasetValidationSplitPercent').value="
        "d.validationSplitPercent||0;"
        "q('#mlDatasetTestSplitPercent').value=d.testSplitPercent||0;});}"
        // Phase 94: "History" fetches this dataset's real immutable version
        // list (docs/PLAN.md section 11) and reports it in the shared
        // action status line -- a full table felt like overkill for what is
        // usually a handful of uploads.
        "for(const btn of el.querySelectorAll('[data-history-ml-dataset]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.historyMlDataset;"
        "try{const r=await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(id)+'/versions','GET');"
        "const s=q('#actionStatus');"
        "const summary=r.versions.map(v=>'v'+v.version+' ('+"
        "v.recordCount+' rows, '+v.uploadedBy+')').join('; ');"
        "if(s)s.textContent=r.versions.length?"
        "('Version history: '+summary):'No content uploaded yet.';}"
        "catch(x){showSystemError('Load dataset history failed: '+"
        "x.message);}});}}"
        // Subject Knowledge Manager (docs/PLAN.md "Machine Learning
        // Abilities" section 12): each row carries its own review-status
        // dropdown, mirroring the Model Registry's set-state pattern above,
        // plus a Delete button.
        "const SUBJECT_REVIEW_STATUSES=['draft','in_review','approved',"
        "'needs_revision','retired'];"
        "function renderMlSubjects(subjects){const el=q('#mlSubjectsList');"
        "if(!el)return;"
        "if(!subjects.length){el.innerHTML='<p>No subject packages "
        "registered yet.</p>';return;}"
        "el.innerHTML=table(['Name','Scope','Target audience','Review "
        "status','Set status'],"
        "subjects.map(x=>[esc(x.name),esc(x.scope),esc(x.targetAudience),"
        "'<span class=\"stateTag stateTag-'+esc(x.reviewStatus)+'\">'+"
        "esc(x.reviewStatus)+'</span>',"
        "toolbar('<select data-review-status-for=\"'+x.id+'\">'+"
        "SUBJECT_REVIEW_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.reviewStatus?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-review-status',x.id),"
        "deleteBtn('delete-ml-subject',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-review-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyReviewStatus;"
        "const status=el.querySelector("
        "'[data-review-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/subjects/'+encodeURIComponent(id)+"
        "'/review-status','POST',{status});await load();}"
        "catch(x){showSystemError('Update subject review status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-subject]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/subjects/'+"
        "encodeURIComponent(btn.dataset.deleteMlSubject)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete subject failed: '+x.message);}});}}"
        // Phases 58-59: ingested source files and their measured index
        // footprint. SHA-256 is shortened only for display; the API retains
        // the complete digest.
        "function renderMlKnowledgeDocuments(documents){"
        "const el=q('#mlKnowledgeDocumentsList');if(!el)return;"
        "if(!documents.length){el.innerHTML='<p>No knowledge files ingested yet.</p>';"
        "return;}el.innerHTML=table(['File','Subject','Vector store','Bytes',"
        "'Chunks','SHA-256',''],documents.map(x=>[escTrim(x.fileName,28),"
        "escTrim(x.subjectId,16),escTrim(x.vectorStoreId,16),"
        "String(x.byteCount),String(x.chunkCount),"
        "'<code title=\"'+esc(x.sha256)+'\">'+esc(x.sha256.slice(0,12))+"
        "'...</code>',deleteBtn('delete-ml-knowledge',x.id)]));"
        "for(const btn of el.querySelectorAll('[data-delete-ml-knowledge]')){"
        "btn.addEventListener('click',async()=>{try{await api("
        "'/api/v1/ml/knowledge-documents/'+encodeURIComponent("
        "btn.dataset.deleteMlKnowledge)+'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete knowledge file failed: '+x.message);}});}}"
        // Data Labeling (docs/PLAN.md "Machine Learning Abilities" section
        // 14): each row carries its own status dropdown, mirroring the
        // Subject Knowledge Manager's review-status pattern above, plus a
        // Delete button.
        "const LABEL_TASK_STATUSES=['queued','in_progress','in_review',"
        "'completed'];"
        "function renderMlLabelTasks(tasks){const el=q('#mlLabelTasksList');"
        "if(!el)return;"
        "if(!tasks.length){el.innerHTML='<p>No labeling tasks created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Label mode','Assignee',"
        "'Status','Set status'],"
        "tasks.map(x=>[esc(x.name),escTrim(x.datasetId,16),esc(x.labelMode),"
        "escTrim(x.assigneeId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-label-task-status-for=\"'+x.id+'\">'+"
        "LABEL_TASK_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-label-task-status',x.id),"
        "deleteBtn('delete-ml-label-task',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-label-task-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyLabelTaskStatus;"
        "const status=el.querySelector("
        "'[data-label-task-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/label-tasks/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update labeling task status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-label-task]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/label-tasks/'+"
        "encodeURIComponent(btn.dataset.deleteMlLabelTask)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete labeling task failed: '+"
        "x.message);}});}}"
        // Data Preparation (docs/PLAN.md "Machine Learning Abilities"
        // section 15): same status-dropdown-plus-Delete pattern as Data
        // Labeling above.
        "const PREP_JOB_STATUSES=['pending','running','completed','failed'];"
        "function renderMlPrepJobs(jobs){const el=q('#mlPrepJobsList');"
        "if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No preparation jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Operation','Status',"
        "'Set status'],"
        "jobs.map(x=>[esc(x.name),escTrim(x.datasetId,16),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-prep-job-status-for=\"'+x.id+'\">'+"
        "PREP_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-prep-job-status',x.id),"
        "deleteBtn('delete-ml-prep-job',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-prep-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyPrepJobStatus;"
        "const status=el.querySelector("
        "'[data-prep-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/prep-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update preparation job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-prep-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/prep-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlPrepJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete preparation job failed: '+"
        "x.message);}});}}"
        // Training Jobs (docs/PLAN.md "Machine Learning Abilities" section
        // 16): same status-dropdown-plus-Delete pattern as Data Labeling/
        // Data Preparation above, with the status list matching section
        // 16's eleven lifecycle states.
        "const TRAINING_JOB_STATUSES=['draft','queued','preparing','running',"
        "'paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        // Phase 56: one shared formatter for real evaluation metrics so
        // Training Jobs, Evaluation Lab, and prediction results all report
        // numbers the same way.
        "function fmtMlMetrics(m){if(!m)return'';"
        // Phase 96: latency/throughput/memory/stability/robustness/bias --
        // real-measured, appended after whichever core metric line below
        // applies, so every existing caller (Training Jobs, Evaluation Lab,
        // Model Comparison, prediction results) shows them for free.
        "const extra=(m.latencyMs===undefined)?'':"
        "(' Latency '+Number(m.latencyMs).toFixed(1)+'ms, throughput '+"
        "Number(m.throughputPredictionsPerSec).toFixed(1)+'/s, stability '+"
        "Number(m.stabilityScore).toFixed(2)+', robustness '+"
        "Number(m.robustnessScore).toFixed(2)+"
        "(m.biasFairnessReport&&Object.keys(m.biasFairnessReport).length?"
        "(', bias breakdown: '+Object.entries(m.biasFairnessReport)."
        "map(([k,v])=>k+'='+Number(v).toFixed(3)).join(', ')):'')+'.');"
        "if(m.task==='classification'){return'accuracy '+"
        "(100*m.accuracy).toFixed(1)+'%, macro precision '+"
        "Number(m.macroPrecision).toFixed(3)+', macro recall '+"
        "Number(m.macroRecall).toFixed(3)+', macro F1 '+"
        "Number(m.macroF1).toFixed(3)+' over '+m.evaluatedRows+' rows.'+extra;}"
        "return'MSE '+Number(m.mse).toPrecision(4)+', MAE '+"
        "Number(m.mae).toPrecision(4)+', R\\u00b2 '+"
        "Number(m.rSquared).toFixed(3)+' over '+m.evaluatedRows+' rows.'+extra;}"
        "function renderMlTrainingJobs(jobs,datasets){"
        "const el=q('#mlTrainingJobsList');"
        "if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No training jobs created "
        "yet.</p>';return;}"
        // Guard rail (ML forms clarity pass): a job whose dataset has no
        // uploaded content can only ever fail with
        // ml_dataset_has_no_content, so Train Now is disabled up front
        // with a tooltip explaining exactly what to do, instead of the
        // user finding out from a raw server error after clicking it.
        "const contentByDataset=new Map((datasets||[]).map("
        "x=>[x.id,!!x.hasContent]));"
        "el.innerHTML=table(['Name','Project','Model','Dataset','Training "
        "type','Max runtime','Retry','Status','Set status'],"
        "jobs.map(x=>{const ready=contentByDataset.get(x.datasetId)===true;"
        "return[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),esc(x.trainingType),"
        // Phase 95: real enforced fields -- see execute_training_job()'s
        // comment in server.cpp for exactly how each is used.
        "x.maxRuntimeSeconds?(x.maxRuntimeSeconds+'s'):'no limit',"
        "esc(x.failureRecoveryStrategy||'none'),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-training-job-status-for=\"'+x.id+'\">'+"
        "TRAINING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-training-job-status',x.id),"
        "runBtn('run-ml-training-job',x.id,ready?'Train now':"
        "'Train now (upload this dataset\\u2019s content first)',"
        "ready?'':'disabled'),"
        "configureBtn('edit-ml-training-job-policy',x.id),"
        "deleteBtn('delete-ml-training-job',x.id))];}));"
        // Phase 56: "Train now" invokes the real training executor and
        // shows the genuine result (method, loss, held-out metrics) in the
        // panel below the table.
        "for(const btn of el.querySelectorAll('[data-run-ml-training-job]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlTrainingRunResult');"
        "if(out)out.textContent='Training...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/training-jobs/'+"
        "encodeURIComponent(btn.dataset.runMlTrainingJob)+'/run','POST',{});"
        "if(out)out.textContent='Trained '+r.method+' over '+r.epochs+"
        "' epochs on '+r.trainRows+' rows (final loss '+"
        "Number(r.finalLoss).toPrecision(4)+'). '+"
        "(r.evaluatedOnTest?'Held-out ('+r.testRows+' rows): '"
        ":'No held-out rows; metrics use training data: ')+"
        "fmtMlMetrics(r.metrics)+' Model ID: '+r.modelId;"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Training failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-training-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyTrainingJobStatus;"
        "const status=el.querySelector("
        "'[data-training-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/training-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update training job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-training-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/training-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlTrainingJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete training job failed: '+"
        "x.message);}});}"
        // Phase 95: "Configure" loads this job's real execution-policy
        // fields into the Execution policy form below the table.
        "for(const btn of el.querySelectorAll("
        "'[data-edit-ml-training-job-policy]')){"
        "btn.addEventListener('click',()=>{"
        "const id=btn.dataset.editMlTrainingJobPolicy;"
        "const j=jobs.find(y=>y.id===id);if(!j)return;"
        "q('#mlTrainingJobPolicyId').value=id;"
        "q('#mlTrainingJobMaxRuntimeSeconds').value=j.maxRuntimeSeconds||0;"
        "q('#mlTrainingJobFailureRecoveryStrategy').value="
        "j.failureRecoveryStrategy||'none';"
        "q('#mlTrainingJobCheckpointFrequencyEpochs').value="
        "j.checkpointFrequencyEpochs||0;"
        "q('#mlTrainingJobOutputDirectory').value=j.outputDirectory||'';"
        "q('#mlTrainingJobComputeTarget').value=j.computeTarget||'';"
        "q('#mlTrainingJobHardwareAllocation').value="
        "j.hardwareAllocation||'';"
        "q('#mlTrainingJobRuntimeEnvironment').value="
        "j.runtimeEnvironment||'';"
        "q('#mlTrainingJobContainerImage').value=j.containerImage||'';"
        "q('#mlTrainingJobEnvironmentVariables').value="
        "j.environmentVariables||'';"
        "q('#mlTrainingJobSecretsReferences').value="
        "j.secretsReferences||'';"
        "q('#mlTrainingJobLoggingPolicy').value=j.loggingPolicy||'';"
        "q('#mlTrainingJobNotificationPolicy').value="
        "j.notificationPolicy||'';"
        "q('#mlTrainingJobResourceCeilingNotes').value="
        "j.resourceCeilingNotes||'';"
        "q('#mlTrainingJobCostCeilingNotes').value="
        "j.costCeilingNotes||'';});}}"
        // Evaluation Lab (docs/PLAN.md "Machine Learning Abilities" section
        // 23): same status-dropdown-plus-Delete pattern as Training Jobs
        // above.
        "const EVALUATION_RUN_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        "function renderMlEvaluationRuns(runs){const el=q('#mlEvaluationRunsList');"
        "if(!el)return;"
        "if(!runs.length){el.innerHTML='<p>No evaluation runs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model','Dataset','Category','Status',"
        "'Set status'],"
        "runs.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "escTrim(x.datasetId,16),esc(x.category),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-evaluation-run-status-for=\"'+x.id+'\">'+"
        "EVALUATION_RUN_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-evaluation-run-status',x.id),"
        "runBtn('run-ml-evaluation',x.id,'Evaluate now'),"
        "viewBtn('view-ml-evaluation',x.id,'View result'),"
        "deleteBtn('delete-ml-evaluation-run',x.id))]));"
        // Phase 56: "Evaluate now" scores the run's trained model against
        // its dataset for real; "View result" recalls the stored metrics.
        "for(const btn of el.querySelectorAll('[data-run-ml-evaluation]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlEvaluationRunResult');"
        "if(out)out.textContent='Evaluating...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.runMlEvaluation)+'/run','POST',{});"
        "if(out)out.textContent='Evaluation complete: '+fmtMlMetrics(r.metrics);"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Evaluation failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-evaluation]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlEvaluationRunResult');"
        "try{const r=await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.viewMlEvaluation)+'/result');"
        "if(out)out.textContent='Stored result: '+fmtMlMetrics(r.metrics);}"
        "catch(x){showSystemError('No stored result for this run yet: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-evaluation-run-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyEvaluationRunStatus;"
        "const status=el.querySelector("
        "'[data-evaluation-run-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/evaluation-runs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update evaluation run status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-evaluation-run]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/evaluation-runs/'+"
        "encodeURIComponent(btn.dataset.deleteMlEvaluationRun)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete evaluation run failed: '+"
        "x.message);}});}}"
        // Experiment Tracking (docs/PLAN.md "Machine Learning Abilities"
        // section 25): same status-dropdown-plus-Delete pattern as
        // Evaluation Lab above.
        "const EXPERIMENT_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        "function renderMlExperiments(experiments){const el=q('#mlExperimentsList');"
        "if(!el)return;"
        "if(!experiments.length){el.innerHTML='<p>No experiments created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Model','Dataset','Status',"
        "'Set status'],"
        "experiments.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-experiment-status-for=\"'+x.id+'\">'+"
        "EXPERIMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-experiment-status',x.id),"
        "runBtn('run-ml-experiment',x.id,'Run now'),"
        "deleteBtn('delete-ml-experiment',x.id))]));"
        // Phase 80: "Run now" invokes the real experiment executor and
        // shows the genuine training/validation/evaluation metrics in the
        // panel below the table, the same pattern Training Jobs' "Train
        // now" already uses.
        "for(const btn of el.querySelectorAll('[data-run-ml-experiment]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlExperimentRunResult');"
        "if(out)out.textContent='Running...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/experiments/'+"
        "encodeURIComponent(btn.dataset.runMlExperiment)+'/run','POST',{});"
        "const m=r.result;"
        "if(out)out.textContent=m?('Trained '+m.trainingMetrics.epochs+"
        "' epochs (final loss '+Number(m.trainingMetrics.finalLoss)"
        ".toPrecision(4)+'). Validation: '+fmtMlMetrics(m.validationMetrics)+"
        "' Evaluation: '+fmtMlMetrics(m.evaluationMetrics)+"
        "' Runtime: '+m.runtimeMilliseconds+'ms. Checkpoints: '+"
        "m.checkpointIds.length+'.'):'Run complete.';"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Run experiment failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-experiment-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyExperimentStatus;"
        "const status=el.querySelector("
        "'[data-experiment-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/experiments/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update experiment status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-experiment]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/experiments/'+"
        "encodeURIComponent(btn.dataset.deleteMlExperiment)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete experiment failed: '+"
        "x.message);}});}}"
        // Fine-Tuning Interface (docs/PLAN.md "Machine Learning Abilities"
        // section 18): same status-dropdown-plus-Delete pattern as Training
        // Jobs above, reusing that section's eleven-state lifecycle since
        // fine-tuning is a training-job variant. Phase 70 adds "Fine-tune
        // now", the real executor: it warm-starts gradient descent from the
        // base model's already-learned weights (see execute_fine_tuning_job
        // in server.cpp) rather than training a fresh model from scratch.
        "const FINE_TUNING_JOB_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlFineTuningJobs(jobs){"
        "const el=q('#mlFineTuningJobsList');if(!el)return;"
        "if(!jobs.length){el.innerHTML='<p>No fine-tuning jobs created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Dataset',"
        "'Method','Status','Set status'],"
        "jobs.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.modelId,16),escTrim(x.datasetId,16),esc(x.method),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-fine-tuning-job-status-for=\"'+x.id+'\">'+"
        "FINE_TUNING_JOB_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-fine-tuning-job-status',x.id),"
        "runBtn('run-ml-fine-tuning-job',x.id,'Fine-tune now'),"
        "deleteBtn('delete-ml-fine-tuning-job',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-run-ml-fine-tuning-job]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlFineTuningRunResult');"
        "if(out)out.textContent='Fine-tuning...';btn.disabled=true;"
        "const jobId=btn.dataset.runMlFineTuningJob;"
        "try{const r=await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(jobId)+'/run','POST',{});"
        // A real LLM LoRA run (Method starting "llm:") answers 202
        // "queued" immediately and finishes on a detached background
        // thread -- poll GET .../llm-result until it reports completed or
        // failed instead of expecting the tabular path's synchronous report
        // shape (r.method/r.finalLoss/...).
        "if(r.status==='queued'){"
        "if(out)out.textContent='LLM fine-tuning running in the "
        "background (this can take a while)...';"
        "let tries=0;"
        "while(tries<600){"
        "await new Promise(res=>setTimeout(res,3000));tries++;"
        // Real progress, not a spinner: tails whichever llama.cpp tool's
        // own log file the background run is currently writing to (see
        // GET .../llm-progress's own comment in server.cpp) so the user
        // sees the actual write operation's live output.
        "try{const prog=await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(jobId)+'/llm-progress','GET');"
        "if(out){const lastLine=(prog.logTail||'').trim().split('\\n').pop()||"
        "'starting...';"
        "out.textContent='LLM fine-tuning: '+prog.stage+' -- '+lastLine;}}"
        "catch(e){}"
        "let poll;try{poll=await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(jobId)+'/llm-result','GET');}"
        "catch(e){continue;}"
        "if(poll.status==='completed'){"
        "if(out)out.textContent='LLM fine-tuning complete. Adapted model "
        "ID: '+poll.modelId+'.'+(poll.catalogModelId?"
        "' Ready to chat with as model \\''+poll.catalogModelId+'\\'.':"
        "(poll.catalogNote?' '+poll.catalogNote:''))+"
        "(poll.quantizationWarning?' \\u26a0 '+poll.quantizationWarning:'');"
        "await load();break;}"
        "if(poll.status==='failed'){"
        "if(out)out.textContent='';"
        "showSystemError('LLM fine-tuning failed: '+poll.error);break;}"
        "}"
        "}else{"
        "if(out)out.textContent='Fine-tuned '+r.method+' over '+r.epochs+"
        "' epochs on '+r.trainRows+' rows (final loss '+"
        "Number(r.finalLoss).toPrecision(4)+'). '+"
        "(r.evaluatedOnTest?'Held-out ('+r.testRows+' rows): '"
        ":'No held-out rows; metrics use training data: ')+"
        "fmtMlMetrics(r.metrics)+' Adapted model ID: '+r.modelId;"
        "await load();}}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Fine-tuning failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-fine-tuning-job-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyFineTuningJobStatus;"
        "const status=el.querySelector("
        "'[data-fine-tuning-job-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/fine-tuning-jobs/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update fine-tuning job status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-fine-tuning-job]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/fine-tuning-jobs/'+"
        "encodeURIComponent(btn.dataset.deleteMlFineTuningJob)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete fine-tuning job failed: '+"
        "x.message);}});}}"
        // Model Builder Interface (docs/PLAN.md "Machine Learning
        // Abilities" section 9): same status-dropdown-plus-Delete pattern
        // as Fine-Tuning above, with its own five-state design-time
        // lifecycle (draft/configuring/ready/submitted/archived) instead of
        // the eleven-state job lifecycle, since a builder configuration
        // never runs. Each row's Configure button loads that
        // configuration's full section 9 build settings into the
        // basic/advanced configure form (fillMlModelBuilderConfigureForm),
        // which saves through POST .../{id}/configure.
        "const MODEL_BUILDER_CONFIG_STATUSES=['draft','configuring','ready',"
        "'submitted','archived'];"
        "let mlModelBuilderConfigureTarget=null;"
        "function fillMlModelBuilderConfigureForm(x){"
        "mlModelBuilderConfigureTarget=x.id;"
        "q('#mlMbcTarget').textContent='Editing build settings for: '+x.name;"
        "const s=x.settings||{};"
        "q('#mlMbcMode').value=s.configurationMode||'basic';"
        "q('#mlMbcArchitecture').value=s.architecture||'';"
        "q('#mlMbcLossFunction').value=s.lossFunction||'';"
        "q('#mlMbcOptimiser').value=s.optimiser||'';"
        "q('#mlMbcBatchSize').value=s.batchSize||'';"
        "q('#mlMbcEpochCount').value=s.epochCount||'';"
        "q('#mlMbcSequenceLength').value=s.sequenceLength||'';"
        "q('#mlMbcLayerConfiguration').value=s.layerConfiguration||'';"
        "q('#mlMbcHiddenDimensions').value=s.hiddenDimensions||'';"
        "q('#mlMbcAttentionConfiguration').value="
        "s.attentionConfiguration||'';"
        "q('#mlMbcVocabularyTokenizer').value=s.vocabularyTokenizer||'';"
        "q('#mlMbcActivationFunctions').value=s.activationFunctions||'';"
        "q('#mlMbcDropout').value=s.dropout||'';"
        "q('#mlMbcInitialisationStrategy').value="
        "s.initialisationStrategy||'';"
        "q('#mlMbcLearningRateScheduler').value="
        "s.learningRateScheduler||'';"
        "q('#mlMbcGradientAccumulation').value=s.gradientAccumulation||'';"
        "q('#mlMbcGradientClipping').value=s.gradientClipping||'';"
        "q('#mlMbcL1Regularization').value=s.l1Regularization||'';"
        "q('#mlMbcL2Regularization').value=s.l2Regularization||'';"
        "q('#mlMbcMixedPrecision').checked=!!s.mixedPrecision;"
        "q('#mlMbcCheckpointFrequency').value=s.checkpointFrequency||'';"
        "q('#mlMbcValidationFrequency').value=s.validationFrequency||'';"
        "q('#mlMbcEarlyStopping').checked=!!s.earlyStopping;"
        "q('#mlMbcRandomSeed').value=s.randomSeed||'';"
        "q('#mlMbcReproducibilitySettings').value="
        "s.reproducibilitySettings||'';"
        "q('#mlMbcDistributedTrainingSettings').value="
        "s.distributedTrainingSettings||'';"
        "q('#mlMbcAdvanced').style.display="
        "q('#mlMbcMode').value==='advanced'?'':'none';}"
        "function renderMlModelBuilderConfigs(configs){"
        "const el=q('#mlModelBuilderConfigsList');if(!el)return;"
        "if(!configs.length){el.innerHTML='<p>No model builder "
        "configurations created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Base model','Source type',"
        "'Architecture','Mode','Status','Set status'],"
        "configs.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.baseModelId,16),esc(x.sourceType),"
        "esc((x.settings&&x.settings.architecture)||''),"
        "esc((x.settings&&x.settings.configurationMode)||'basic'),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-model-builder-config-status-for=\"'+x.id+"
        "'\">'+MODEL_BUILDER_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-model-builder-config-status',x.id),"
        "configureBtn('configure-ml-model-builder-config',x.id),"
        "deleteBtn('delete-ml-model-builder-config',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-configure-ml-model-builder-config]')){"
        "btn.addEventListener('click',()=>{const found=configs.find(c=>"
        "c.id===btn.dataset.configureMlModelBuilderConfig);"
        "if(found)fillMlModelBuilderConfigureForm(found);});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-model-builder-config-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyModelBuilderConfigStatus;"
        "const status=el.querySelector("
        "'[data-model-builder-config-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update model builder configuration "
        "status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-model-builder-config]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(btn.dataset.deleteMlModelBuilderConfig)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete model builder configuration "
        "failed: '+x.message);}});}}"
        // Prompt and Instruction Training (docs/PLAN.md "Machine Learning
        // Abilities" section 19): same status-dropdown-plus-Delete pattern
        // as Model Builder above, with its own five-state reviewer workflow
        // (draft/in_review/approved/rejected/archived) matching section 19's
        // "generated training examples must require approval before
        // entering an approved dataset."
        "const INSTRUCTION_EXAMPLE_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlInstructionExamples(examples){"
        "const el=q('#mlInstructionExamplesList');if(!el)return;"
        "if(!examples.length){el.innerHTML='<p>No instruction examples "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Subject classification',"
        "'Status','Set status'],"
        "examples.map(x=>[esc(x.name),escTrim(x.datasetId,16),"
        "esc(x.subjectClassification),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-instruction-example-status-for=\"'+x.id+"
        "'\">'+INSTRUCTION_EXAMPLE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-instruction-example-status',x.id),"
        "configureBtn('edit-ml-instruction-example-content',x.id),"
        "viewBtn('validate-ml-instruction-example',x.id,'Validate'),"
        "deleteBtn('delete-ml-instruction-example',x.id))]));"
        // Phase 81: "Validate" runs the real structured-output check
        // against the example's stored content.
        "for(const btn of el.querySelectorAll("
        "'[data-validate-ml-instruction-example]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.validateMlInstructionExample;"
        "const out=q('#mlInstructionExampleContentStatus');"
        "try{const r=await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(id)+'/validate','POST',{});"
        "if(out)out.textContent=(r.valid?'Valid. ':'Invalid. ')+"
        "(r.detail||'');}"
        "catch(x){showSystemError('Validate instruction example failed: '+"
        "x.message);}});}"
        // Phase 81: "Configure" loads the example's real content (if any)
        // into the Edit content form below the table, so an administrator
        // reviewing the list can jump straight to editing its full record.
        "for(const btn of el.querySelectorAll("
        "'[data-edit-ml-instruction-example-content]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.editMlInstructionExampleContent;"
        "q('#mlInstructionExampleContentId').value=id;"
        "const status=q('#mlInstructionExampleContentStatus');"
        "try{const c=await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(id)+'/content','GET');"
        "q('#mlInstructionExampleSystemInstruction').value="
        "c.systemInstruction||'';"
        "q('#mlInstructionExampleUserInstruction').value="
        "c.userInstruction||'';"
        "q('#mlInstructionExampleContext').value=c.context||'';"
        "q('#mlInstructionExampleExpectedResponse').value="
        "c.expectedResponse||'';"
        "q('#mlInstructionExampleRejectedResponse').value="
        "c.rejectedResponse||'';"
        "q('#mlInstructionExampleToolCallsJson').value=c.toolCallsJson||'';"
        "q('#mlInstructionExampleToolResultsJson').value="
        "c.toolResultsJson||'';"
        "q('#mlInstructionExampleRequiredOutputFormat').value="
        "c.requiredOutputFormat||'';"
        "q('#mlInstructionExampleDifficulty').value=c.difficulty||'';"
        "q('#mlInstructionExampleSafetyClassification').value="
        "c.safetyClassification||'';"
        "if(status)status.textContent='Loaded existing content.';}"
        "catch(x){for(const f of ['SystemInstruction','UserInstruction',"
        "'Context','ExpectedResponse','RejectedResponse','ToolCallsJson',"
        "'ToolResultsJson','RequiredOutputFormat','Difficulty',"
        "'SafetyClassification'])q('#mlInstructionExample'+f).value='';"
        "if(status)status.textContent="
        "'No content yet -- fill in the form and Save content.';}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-instruction-example-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyInstructionExampleStatus;"
        "const status=el.querySelector("
        "'[data-instruction-example-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update instruction example status "
        "failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-instruction-example]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(btn.dataset.deleteMlInstructionExample)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete instruction example failed: '+"
        "x.message);}});}}"
        // Synthetic Data Generation (docs/PLAN.md "Machine Learning
        // Abilities" section 20): same status-dropdown-plus-Delete pattern
        // as Prompt and Instruction Training above, with its own five-state
        // reviewer workflow (draft/in_review/approved/rejected/archived)
        // matching section 20's requirement that generated records "remain
        // distinguishable from human-created and real-world data" until
        // reviewed.
        "const SYNTHETIC_RECORD_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlSyntheticRecords(records){"
        "const el=q('#mlSyntheticRecordsList');if(!el)return;"
        "if(!records.length){el.innerHTML='<p>No synthetic records "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Dataset','Generation technique',"
        "'Status','Set status'],"
        "records.map(x=>[esc(x.name),escTrim(x.datasetId,16),"
        "esc(x.generationTechnique),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-synthetic-record-status-for=\"'+x.id+"
        "'\">'+SYNTHETIC_RECORD_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-synthetic-record-status',x.id),"
        "deleteBtn('delete-ml-synthetic-record',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-synthetic-record-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applySyntheticRecordStatus;"
        "const status=el.querySelector("
        "'[data-synthetic-record-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/synthetic-records/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update synthetic record status "
        "failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-synthetic-record]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/synthetic-records/'+"
        "encodeURIComponent(btn.dataset.deleteMlSyntheticRecord)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete synthetic record failed: '+"
        "x.message);}});}}"
        // Embeddings and Vector Stores (docs/PLAN.md "Machine Learning
        // Abilities" section 21): a vector store is a standalone registered
        // resource, not a target-scoped content record, so it reuses
        // Dataset's three-state pending/approved/rejected approval workflow
        // instead of the five-state reviewer workflow content records use.
        "const VECTOR_STORE_STATUSES=['pending','approved','rejected'];"
        "function renderMlVectorStores(stores){"
        "const el=q('#mlVectorStoresList');if(!el)return;"
        "if(!stores.length){el.innerHTML='<p>No vector stores registered "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Embedding model','Distance metric',"
        "'Status','Set status'],"
        "stores.map(x=>[esc(x.name),esc(x.embeddingModel),"
        "esc(x.distanceMetric),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-vector-store-status-for=\"'+x.id+'\">'+"
        "VECTOR_STORE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-vector-store-status',x.id),"
        "viewBtn('view-ml-vector-index',x.id,'View index'),"
        "deleteBtn('delete-ml-vector-store',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-vector-store-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyVectorStoreStatus;"
        "const status=el.querySelector("
        "'[data-vector-store-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/vector-stores/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update vector store status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-vector-index]')){"
        "btn.addEventListener('click',async()=>{try{const r=await api("
        "'/api/v1/ml/vector-stores/'+encodeURIComponent("
        "btn.dataset.viewMlVectorIndex)+'/index');const out=q('#mlVectorIndexResult');"
        "if(out)out.textContent=r.documentCount+' document(s), '+r.chunkCount+"
        "' chunk(s), '+r.dimensions+' dimensions, '+r.indexedTextBytes+"
        "' indexed text bytes ('+r.embeddingMethod+').';}catch(x){"
        "showSystemError('Read vector index failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-vector-store]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/vector-stores/'+"
        "encodeURIComponent(btn.dataset.deleteMlVectorStore)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete vector store failed: '+"
        "x.message);}});}}"
        // Retrieval-Augmented Generation (docs/PLAN.md "Machine Learning
        // Abilities" section 22): a RAG configuration is a standalone
        // registered resource, not a target-scoped content record, so it
        // reuses the same three-state pending/approved/rejected approval
        // workflow as Vector Stores above.
        "const RAG_CONFIG_STATUSES=['pending','approved','rejected'];"
        "function renderMlRagConfigs(configs){"
        "const el=q('#mlRagConfigsList');if(!el)return;"
        "fillMlSelect('#mlRagQueryConfigId',configs,'Choose an approved RAG configuration',"
        "x=>x.name+' ('+x.status+')');"
        "if(!configs.length){el.innerHTML='<p>No RAG configurations "
        "registered yet.</p>';return;}"
        "el.innerHTML=table(['Name','Search strategy','Vector store',"
        "'Status','Set status'],"
        "configs.map(x=>[esc(x.name),esc(x.searchStrategy),"
        "escTrim(x.vectorStoreId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-rag-config-status-for=\"'+x.id+'\">'+"
        "RAG_CONFIG_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-rag-config-status',x.id),"
        "deleteBtn('delete-ml-rag-config',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-rag-config-status]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "const id=btn.dataset.applyRagConfigStatus;"
        "const status=el.querySelector("
        "'[data-rag-config-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/rag-configs/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update RAG config status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-rag-config]')){"
        "btn.addEventListener('click',async()=>{const s=q('#actionStatus');"
        "try{await api('/api/v1/ml/rag-configs/'+"
        "encodeURIComponent(btn.dataset.deleteMlRagConfig)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete RAG config failed: '+"
        "x.message);}});}}"
        // Subject Examination System (docs/PLAN.md "Machine Learning
        // Abilities" section 24): an exam is authored content that a
        // reviewer approves before it may examine anything, so it uses the
        // same five-state reviewer workflow as Instruction Examples.
        "const SUBJECT_EXAM_STATUSES=['draft','in_review','approved',"
        "'rejected','archived'];"
        "function renderMlSubjectExams(exams){"
        "const el=q('#mlSubjectExamsList');if(!el)return;"
        "if(!exams.length){el.innerHTML='<p>No subject exams created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Subject ID','Question format',"
        "'Status','Set status'],"
        "exams.map(x=>[esc(x.name),escTrim(x.subjectId,16),"
        "esc(x.questionFormat),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-subject-exam-status-for=\"'+x.id+'\">'+"
        "SUBJECT_EXAM_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-subject-exam-status',x.id),"
        "deleteBtn('delete-ml-subject-exam',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-subject-exam-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applySubjectExamStatus;"
        "const status=el.querySelector("
        "'[data-subject-exam-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/subject-exams/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update subject exam status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-subject-exam]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/subject-exams/'+"
        "encodeURIComponent(btn.dataset.deleteMlSubjectExam)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete subject exam failed: '+"
        "x.message);}});}}"
        // Hyperparameter Optimization (docs/PLAN.md "Machine Learning
        // Abilities" section 26): a search queues, runs, and pauses like a
        // training job, so it uses the same eleven-state job lifecycle.
        "const HYPERPARAMETER_SEARCH_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlHyperparameterSearches(searches){"
        "const el=q('#mlHyperparameterSearchesList');if(!el)return;"
        "if(!searches.length){el.innerHTML='<p>No hyperparameter searches "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Training job ID','Strategy',"
        "'Status','Set status'],"
        "searches.map(x=>[esc(x.name),escTrim(x.trainingJobId,16),"
        "esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-hyperparameter-search-status-for=\"'+x.id+"
        "'\">'+HYPERPARAMETER_SEARCH_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-hyperparameter-search-status',x.id),"
        "runBtn('run-ml-hyperparameter-search',x.id,'Run now'),"
        "deleteBtn('delete-ml-hyperparameter-search',x.id))]));"
        // "Run now" invokes the real search executor (grid/random/
        // bayesian, per the search's own strategy) and shows the real
        // best trial's parameters/score below the table.
        "for(const btn of el.querySelectorAll("
        "'[data-run-ml-hyperparameter-search]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlHyperparameterSearchRunResult');"
        "if(out)out.textContent='Searching...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/hyperparameter-searches/'+"
        "encodeURIComponent(btn.dataset.runMlHyperparameterSearch)+"
        "'/run','POST',{});"
        "if(out)out.textContent=r.detail;await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Run hyperparameter search failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-hyperparameter-search-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyHyperparameterSearchStatus;"
        "const status=el.querySelector("
        "'[data-hyperparameter-search-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/hyperparameter-searches/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError("
        "'Update hyperparameter search status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-hyperparameter-search]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/hyperparameter-searches/'+"
        "encodeURIComponent(btn.dataset.deleteMlHyperparameterSearch)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete hyperparameter search failed: '+"
        "x.message);}});}}"
        // Ensemble Methods (2026-08-24): an ensemble run executes like any
        // other job, so it uses the same eleven-state job lifecycle.
        // "Run now" invokes the real executor and shows the real ensemble
        // score vs. single-model baseline score below the table.
        "const ENSEMBLE_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlEnsembles(ensembles){"
        "const el=q('#mlEnsemblesList');if(!el)return;"
        "if(!ensembles.length){el.innerHTML='<p>No ensembles created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Training job ID','Method',"
        "'Members','Result','Status','Set status'],"
        "ensembles.map(x=>[esc(x.name),escTrim(x.trainingJobId,16),"
        "esc(x.method),String(x.memberCount),"
        "x.membersTrained?('ensemble '+x.ensembleScore.toPrecision(4)+"
        "' vs. single model '+x.baselineScore.toPrecision(4)+' ('+"
        "x.membersTrained+' member(s))'):'not run yet',"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-ensemble-status-for=\"'+x.id+"
        "'\">'+ENSEMBLE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-ensemble-status',x.id),"
        "runBtn('run-ml-ensemble',x.id,'Run now'),"
        "deleteBtn('delete-ml-ensemble',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-run-ml-ensemble]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlEnsembleRunResult');"
        "if(out)out.textContent='Training and combining members...';"
        "btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/ensembles/'+"
        "encodeURIComponent(btn.dataset.runMlEnsemble)+'/run','POST',{});"
        "if(out)out.textContent=r.detail;await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Run ensemble failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-ensemble-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyEnsembleStatus;"
        "const status=el.querySelector("
        "'[data-ensemble-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/ensembles/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update ensemble status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-ensemble]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/ensembles/'+"
        "encodeURIComponent(btn.dataset.deleteMlEnsemble)+'/delete',"
        "'POST');await load();}"
        "catch(x){showSystemError('Delete ensemble failed: '+"
        "x.message);}});}}"
        // Model Optimization (docs/PLAN.md "Machine Learning Abilities"
        // section 28): an optimization run executes like a training job, so
        // it uses the same eleven-state job lifecycle.
        "const MODEL_OPTIMIZATION_STATUSES=['draft','queued','preparing',"
        "'running','paused','canceling','canceled','failed','completed',"
        "'awaiting_evaluation','archived'];"
        "function renderMlModelOptimizations(runs){"
        "const el=q('#mlModelOptimizationsList');if(!el)return;"
        "if(!runs.length){el.innerHTML='<p>No model optimization runs "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model ID','Operation',"
        "'Status','Set status'],"
        "runs.map(x=>[esc(x.name),escTrim(x.modelId,16),esc(x.operation),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-model-optimization-status-for=\"'+x.id+"
        "'\">'+MODEL_OPTIMIZATION_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-model-optimization-status',x.id),"
        "deleteBtn('delete-ml-model-optimization',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-model-optimization-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyModelOptimizationStatus;"
        "const status=el.querySelector("
        "'[data-model-optimization-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-optimizations/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError("
        "'Update model optimization status failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-model-optimization]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-optimizations/'+"
        "encodeURIComponent(btn.dataset.deleteMlModelOptimization)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete model optimization failed: '+"
        "x.message);}});}}"
        // Checkpoint Management (docs/PLAN.md "Machine Learning Abilities"
        // section 33): a checkpoint carries a retention lifecycle (active/
        // pinned/archived), not an approval workflow -- pinned is section
        // 33's own "protect" operation.
        "const CHECKPOINT_STATUSES=['active','pinned','archived'];"
        "function renderMlCheckpoints(checkpoints){"
        "const el=q('#mlCheckpointsList');if(!el)return;"
        "if(!checkpoints.length){el.innerHTML='<p>No checkpoints recorded "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Training job ID','Capture reason',"
        "'Epoch','Snapshot','Status','Set status'],"
        "checkpoints.map(x=>[esc(x.name),escTrim(x.trainingJobId,16),"
        "esc(x.captureReason),String(x.epoch),"
        "x.hasSnapshot?'Yes':'No',"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-checkpoint-status-for=\"'+x.id+'\">'+"
        "CHECKPOINT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-checkpoint-status',x.id),"
        "x.hasSnapshot?runBtn('resume-ml-checkpoint',x.id,"
        "'Resume training'):'',"
        "deleteBtn('delete-ml-checkpoint',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-checkpoint-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyCheckpointStatus;"
        "const status=el.querySelector("
        "'[data-checkpoint-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/checkpoints/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update checkpoint status failed: '+"
        "x.message);}});}"
        // Phase 79: "Resume training" continues gradient descent from this
        // checkpoint's real captured weights and shows the same real
        // method/loss/metrics summary the training-jobs "Train now" action
        // shows, in the panel's own result line.
        "for(const btn of el.querySelectorAll("
        "'[data-resume-ml-checkpoint]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlCheckpointResumeResult');"
        "if(out)out.textContent='Resuming...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/checkpoints/'+"
        "encodeURIComponent(btn.dataset.resumeMlCheckpoint)+"
        "'/resume','POST',{});"
        "if(out)out.textContent='Resumed '+r.method+' over '+r.epochs+"
        "' further epochs on '+r.trainRows+' rows (final loss '+"
        "Number(r.finalLoss).toPrecision(4)+'). '+"
        "(r.evaluatedOnTest?'Held-out ('+r.testRows+' rows): '"
        ":'No held-out rows; metrics use training data: ')+"
        "fmtMlMetrics(r.metrics)+' Model ID: '+r.modelId;"
        "await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Resume training failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-checkpoint]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/checkpoints/'+"
        "encodeURIComponent(btn.dataset.deleteMlCheckpoint)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete checkpoint failed: '+"
        "x.message);}});}}"
        // Deployment Manager (docs/PLAN.md "Machine Learning Abilities"
        // section 34): a deployment is a standalone registered resource
        // awaiting authorization, so it reuses the same three-state
        // pending/approved/rejected approval workflow as Vector Stores.
        // The Deployment Manager/Inference Endpoints/Synthetic Data
        // completion phase adds real "Deploy now"/"Rollback" actions
        // (POST .../deploy, .../rollback) alongside the manual status
        // dropdown -- "Deploy now" gates on an approved ModelCard and
        // reports a real health signal; "Rollback" only appears once a
        // deployment has superseded a prior one for the same environment.
        "const DEPLOYMENT_STATUSES=['pending','approved','rejected'];"
        "function renderMlDeployments(deployments){"
        "const el=q('#mlDeploymentsList');if(!el)return;"
        "if(!deployments.length){el.innerHTML='<p>No deployments recorded "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model ID','Environment','Strategy',"
        "'Status','Health','Deployed at','Set status'],"
        "deployments.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "esc(x.environment),esc(x.strategy),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "esc(x.healthStatus||'unchecked'),"
        "x.deployedAtEpochSeconds?"
        "new Date(x.deployedAtEpochSeconds*1000).toLocaleString():'Never',"
        "toolbar('<select data-deployment-status-for=\"'+x.id+'\">'+"
        "DEPLOYMENT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-deployment-status',x.id),"
        "runBtn('deploy-ml-deployment',x.id,'Deploy now'),"
        "x.previousDeploymentId?"
        "runBtn('rollback-ml-deployment',x.id,'Rollback'):'',"
        "deleteBtn('delete-ml-deployment',x.id))]));"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-deployment-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyDeploymentStatus;"
        "const status=el.querySelector("
        "'[data-deployment-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update deployment status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-deploy-ml-deployment]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(btn.dataset.deployMlDeployment)+"
        "'/deploy','POST',{});await load();}"
        "catch(x){showSystemError('Deploy failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-rollback-ml-deployment]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(btn.dataset.rollbackMlDeployment)+"
        "'/rollback','POST',{});await load();}"
        "catch(x){showSystemError('Rollback failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-deployment]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/deployments/'+"
        "encodeURIComponent(btn.dataset.deleteMlDeployment)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete deployment failed: '+"
        "x.message);}});}}"
        // Model Comparison (docs/PLAN.md "Machine Learning Abilities"
        // section 27): the same status-dropdown pattern as Evaluation Lab,
        // plus Phase 57's real "Compare now"/"View result" actions.
        "const MODEL_COMPARISON_STATUSES=['queued','running','completed',"
        "'failed','canceled'];"
        // One shared formatter for a stored comparison result so the run
        // and view actions describe the winner identically.
        "function fmtMlComparison(r){if(!r)return'';"
        "const verdict=r.winner==='tie'?'Tie on '+r.primaryMetric:"
        "(r.winner==='candidate'?'Candidate':'Baseline')+' wins on '+"
        "r.primaryMetric;"
        "return verdict+' (baseline '+Number(r.baselineValue).toPrecision(4)+"
        "', candidate '+Number(r.candidateValue).toPrecision(4)+"
        "', delta '+Number(r.delta).toPrecision(4)+'). '+"
        "'Baseline '+r.baseline.modelId+': '+fmtMlMetrics(r.baseline.metrics)+"
        "' Candidate '+r.candidate.modelId+': '+"
        "fmtMlMetrics(r.candidate.metrics);}"
        "function renderMlModelComparisons(comparisons){"
        "const el=q('#mlModelComparisonsList');if(!el)return;"
        "if(!comparisons.length){el.innerHTML='<p>No model comparisons "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Baseline model','Candidate model',"
        "'Benchmark dataset','Status','Set status'],"
        "comparisons.map(x=>[esc(x.name),escTrim(x.baselineModelId,16),"
        "escTrim(x.candidateModelId,16),escTrim(x.datasetId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-comparison-status-for=\"'+x.id+'\">'+"
        "MODEL_COMPARISON_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-comparison-status',x.id),"
        "runBtn('run-ml-comparison',x.id,'Compare now'),"
        "viewBtn('view-ml-comparison',x.id,'View result'),"
        "deleteBtn('delete-ml-comparison',x.id))]));"
        // Phase 57: "Compare now" scores both trained artifacts against the
        // shared benchmark for real; "View result" recalls the stored
        // verdict.
        "for(const btn of el.querySelectorAll('[data-run-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlModelComparisonResult');"
        "if(out)out.textContent='Comparing...';btn.disabled=true;"
        "try{const r=await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.runMlComparison)+'/run','POST',{});"
        "if(out)out.textContent='Comparison complete: '+"
        "fmtMlComparison(r.result);await load();}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Comparison failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "const out=q('#mlModelComparisonResult');"
        "try{const r=await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.viewMlComparison)+'/result');"
        "if(out)out.textContent='Stored result: '+fmtMlComparison(r.result);}"
        "catch(x){showSystemError('No stored result for this comparison "
        "yet: '+x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-apply-comparison-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyComparisonStatus;"
        "const status=el.querySelector("
        "'[data-comparison-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update comparison status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll("
        "'[data-delete-ml-comparison]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-comparisons/'+"
        "encodeURIComponent(btn.dataset.deleteMlComparison)+"
        "'/delete','POST');await load();}"
        "catch(x){showSystemError('Delete comparison failed: '+"
        "x.message);}});}}"
        // Phase 62 (docs/PLAN.md "Machine Learning Abilities" section 35):
        // same status-dropdown-plus-Delete pattern as every registry above.
        "const INFERENCE_ENDPOINT_STATUSES=['draft','active','disabled'];"
        "function renderMlInferenceEndpoints(endpoints){"
        "const el=q('#mlInferenceEndpointsList');if(!el)return;"
        "if(!endpoints.length){el.innerHTML='<p>No inference endpoints "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Model','Host','Port','Protocol',"
        "'Status','Set status'],"
        "endpoints.map(x=>[esc(x.name),escTrim(x.modelId,16),esc(x.host),"
        "esc(x.port),esc(x.protocol),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-endpoint-status-for=\"'+x.id+'\">'+"
        "INFERENCE_ENDPOINT_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-endpoint-status',x.id),"
        "deleteBtn('delete-ml-endpoint',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-endpoint-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyEndpointStatus;"
        "const status=el.querySelector("
        "'[data-endpoint-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/inference-endpoints/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update endpoint status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-endpoint]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/inference-endpoints/'+"
        "encodeURIComponent(btn.dataset.deleteMlEndpoint)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete endpoint failed: '+"
        "x.message);}});}}"
        // Phase 63 (docs/PLAN.md "Machine Learning Abilities" section 30):
        // same list/status/delete pattern as the other registries. Phase 67
        // adds a real "View live telemetry" action for whichever node is
        // flagged local -- see ComputeNode's class comment in masterai.hpp.
        "const COMPUTE_NODE_STATUSES=['available','reserved','draining',"
        "'disabled'];"
        "function fmtMlHardware(h){if(!h)return'';"
        "return h.physicalCpus+' physical / '+h.logicalCpus+' logical CPUs, '+"
        "h.availableRamMiB+' / '+h.totalRamMiB+' MiB RAM available, '+"
        "(h.gpuBackends.length?h.gpuBackends.join('/')+' GPU backend, '+"
        "h.gpuMemoryMiB+' MiB VRAM':'no GPU backend detected')+', '+"
        "h.storageFreeMiB+' MiB free disk ('+h.storageClass+').';}"
        "function renderMlComputeNodes(nodes){"
        "const el=q('#mlComputeNodesList');if(!el)return;"
        "if(!nodes.length){el.innerHTML='<p>No compute nodes registered "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Address','OS','CPU','GPU','Local',"
        "'Status','Set status'],"
        "nodes.map(x=>[esc(x.name),esc(x.address),esc(x.operatingSystem),"
        "escTrim(x.cpuDescription,20),escTrim(x.gpuDescription,20),"
        "x.isLocal?'Yes':'No',"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-node-status-for=\"'+x.id+'\">'+"
        "COMPUTE_NODE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-node-status',x.id),"
        "x.isLocal?viewBtn('view-ml-node-telemetry',x.id,"
        "'View live telemetry'):'',"
        "deleteBtn('delete-ml-node',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-node-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyNodeStatus;"
        "const status=el.querySelector("
        "'[data-node-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/compute-nodes/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update compute node status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-view-ml-node-telemetry]')"
        "){btn.addEventListener('click',async()=>{"
        "const out=q('#mlComputeNodeTelemetry');"
        "if(out)out.textContent='Probing...';"
        "try{const r=await api('/api/v1/ml/compute-nodes/'+"
        "encodeURIComponent(btn.dataset.viewMlNodeTelemetry)+'/telemetry');"
        "if(out)out.textContent='Live telemetry: '+"
        "fmtMlHardware(r.telemetry);}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Fetch telemetry failed: '+x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-node]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/compute-nodes/'+"
        "encodeURIComponent(btn.dataset.deleteMlNode)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete compute node failed: '+"
        "x.message);}});}}"
        // Phase 64/69/71 (docs/PLAN.md "Machine Learning Abilities" section
        // 37): 'Run' genuinely executes Train model/Evaluate model/Validate
        // data/Validate model/Safety tests/Request approval/Deploy staging/
        // Deploy production/Rollback/Monitor stages and honestly records
        // every other named stage as skipped -- see AutomationPipeline's
        // class comment in masterai.hpp. The run's per-stage outcome is
        // rendered into #mlPipelineRunDetail below. Phase 71: a run now
        // executes on a background thread and POST .../run returns
        // immediately in the `running` state, so renderMlPipelineRun()
        // shows a live <progress> bar (completedStageCount/totalStageCount)
        // and the currently-running stage's name while pollMlPipelineRun()
        // re-fetches GET .../runs once a second until the run reaches a
        // terminal status.
        "const AUTOMATION_PIPELINE_STATUSES=['draft','active','disabled'];"
        "function renderMlPipelineRun(run){"
        "const el=q('#mlPipelineRunDetail');if(!el)return;"
        "const stillRunning=run.status==='queued'||run.status==='running';"
        "const bar=stillRunning?'<progress value=\"'+run.completedStageCount+"
        "'\" max=\"'+(run.totalStageCount||1)+'\"></progress><p>'+"
        "run.completedStageCount+' / '+run.totalStageCount+' stage(s) complete'+"
        "(run.currentStage?' -- '+esc(run.currentStage):'')+'</p>':'';"
        "el.innerHTML='<h3>Run '+esc(run.id)+' -- '+"
        "'<span class=\"stateTag stateTag-'+esc(run.status)+'\">'+"
        "esc(run.status)+'</span></h3>'+bar+'<p>'+esc(run.outcomeNote)+'</p>'+"
        "table(['Stage','Status','Detail'],"
        "(run.stageResults||[]).map(s=>[esc(s.stage),"
        "'<span class=\"stateTag stateTag-'+esc(s.status)+'\">'+"
        "esc(s.status)+'</span>',esc(s.detail)]));}"
        // Re-fetches the run list (the run's own live progress fields, not
        // just its final result) once a second until the run this button
        // started reaches a terminal status, re-rendering the progress bar
        // and stage table on every tick so the operator can see how far
        // along a still-running pipeline is instead of staring at a
        // disabled button with no feedback.
        "const TERMINAL_PIPELINE_RUN_STATES=new Set("
        "['completed','failed','canceled']);"
        "async function pollMlPipelineRun(pipelineId,runId){"
        "for(;;){let run;"
        "try{const d=await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(pipelineId)+'/runs');"
        "run=(d.pipelineRuns||[]).find(r=>r.id===runId);}"
        "catch(x){console.warn('pipeline run poll tick failed, retrying "
        "next interval',x);}"
        "if(run)renderMlPipelineRun(run);"
        "if(run&&TERMINAL_PIPELINE_RUN_STATES.has(run.status))return run;"
        "await new Promise(r=>setTimeout(r,1000));}}"
        "function renderMlAutomationPipelines(pipelines){"
        "const el=q('#mlAutomationPipelinesList');if(!el)return;"
        "if(!pipelines.length){el.innerHTML='<p>No automation pipelines "
        "created yet.</p>';return;}"
        "el.innerHTML=table(['Name','Project','Stages','Dataset','Model',"
        "'Status','Set status'],"
        "pipelines.map(x=>[esc(x.name),escTrim(x.projectId,16),"
        "escTrim(x.stages,24),escTrim(x.datasetId,16),escTrim(x.modelId,16),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-pipeline-status-for=\"'+x.id+'\">'+"
        "AUTOMATION_PIPELINE_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-pipeline-status',x.id),"
        "runBtn('run-ml-pipeline',x.id,'Run pipeline'),"
        "deleteBtn('delete-ml-pipeline',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-run-ml-pipeline]')){"
        "btn.addEventListener('click',async()=>{btn.disabled=true;"
        "const pipelineId=btn.dataset.runMlPipeline;"
        "try{const run=await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(pipelineId)+'/run','POST',{});"
        "renderMlPipelineRun(run);"
        "await pollMlPipelineRun(pipelineId,run.id);await load();}"
        "catch(x){showSystemError('Run pipeline failed: '+x.message);}"
        "finally{btn.disabled=false;}});}"
        "for(const btn of el.querySelectorAll('[data-apply-pipeline-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyPipelineStatus;"
        "const status=el.querySelector("
        "'[data-pipeline-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(id)+'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update pipeline status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-pipeline]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/automation-pipelines/'+"
        "encodeURIComponent(btn.dataset.deleteMlPipeline)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete pipeline failed: '+"
        "x.message);}});}}"
        // Phase 65 (docs/PLAN.md "Machine Learning Abilities" section 40):
        // policies and model cards each get their own independent
        // pending/approved/rejected approval workflow.
        "const SAFETY_POLICY_STATUSES=['pending','approved','rejected'];"
        "function renderMlSafetyPolicies(policies){"
        "const el=q('#mlSafetyPoliciesList');if(!el)return;"
        "if(!policies.length){el.innerHTML='<p>No safety policies created "
        "yet.</p>';return;}"
        "el.innerHTML=table(['Name','Scope','Restricted categories',"
        "'Status','Set status'],"
        "policies.map(x=>[esc(x.name),esc(x.scope),"
        "escTrim(x.restrictedDataCategories,24),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-policy-status-for=\"'+x.id+'\">'+"
        "SAFETY_POLICY_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-policy-status',x.id),"
        "deleteBtn('delete-ml-policy',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-policy-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyPolicyStatus;"
        "const status=el.querySelector("
        "'[data-policy-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/safety-policies/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update policy status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-policy]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/safety-policies/'+"
        "encodeURIComponent(btn.dataset.deleteMlPolicy)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete policy failed: '+"
        "x.message);}});}}"
        "function renderMlModelCards(cards){"
        "const el=q('#mlModelCardsList');if(!el)return;"
        "if(!cards.length){el.innerHTML='<p>No model cards created yet."
        "</p>';return;}"
        "el.innerHTML=table(['Model','Purpose','Status','Set status'],"
        "cards.map(x=>[escTrim(x.modelId,20),escTrim(x.purpose,28),"
        "'<span class=\"stateTag stateTag-'+esc(x.status)+'\">'+"
        "esc(x.status)+'</span>',"
        "toolbar('<select data-card-status-for=\"'+x.id+'\">'+"
        "SAFETY_POLICY_STATUSES.map(s=>"
        "'<option value=\"'+s+'\"'+(s===x.status?' selected':'')+'>'+s+"
        "'</option>').join('')+'</select>',"
        "applyBtn('apply-card-status',x.id),"
        "deleteBtn('delete-ml-card',x.id))]));"
        "for(const btn of el.querySelectorAll('[data-apply-card-status]')){"
        "btn.addEventListener('click',async()=>{"
        "const id=btn.dataset.applyCardStatus;"
        "const status=el.querySelector("
        "'[data-card-status-for=\"'+id+'\"]').value;"
        "try{await api('/api/v1/ml/model-cards/'+encodeURIComponent(id)+"
        "'/status','POST',{status});await load();}"
        "catch(x){showSystemError('Update model card status failed: '+"
        "x.message);}});}"
        "for(const btn of el.querySelectorAll('[data-delete-ml-card]')){"
        "btn.addEventListener('click',async()=>{"
        "try{await api('/api/v1/ml/model-cards/'+"
        "encodeURIComponent(btn.dataset.deleteMlCard)+'/delete','POST');"
        "await load();}"
        "catch(x){showSystemError('Delete model card failed: '+"
        "x.message);}});}}"
        // Phase 66 (docs/PLAN.md "Machine Learning Abilities" section 43):
        // read-only -- no row actions, no form.
        "function renderMlAuditLogs(entries){"
        "const el=q('#mlAuditLogsList');if(!el)return;"
        "if(!entries.length){el.innerHTML='<p>No Machine Learning "
        "administrator actions recorded yet.</p>';return;}"
        "el.innerHTML=table(['When','Event','Actor','Outcome','Detail'],"
        "entries.map(x=>[esc(new Date(x.timestampEpochSeconds*1000)"
        ".toLocaleString()),esc(x.event),esc(x.actor),esc(x.outcome),"
        "esc(x.detail)]));}"
        // Phase 68 (docs/PLAN.md "Machine Learning Abilities" section 44):
        // a read-only aggregation over data other real phases already
        // measured -- see the /api/v1/ml/monitoring handler's own comment
        // in server.cpp for exactly what is real here and what remains
        // planned (per-step training curves, live per-request telemetry,
        // temperature/network sensors).
        "function renderMlMonitoring(data){"
        "const panel=q('#mlMonitoringPanel');if(!panel)return;"
        "const sys=q('#mlMonitoringSystem');"
        "if(sys)sys.textContent=data.systemResources?"
        "fmtMlHardware(data.systemResources):"
        "'No system resource snapshot available.';"
        "const jobs=q('#mlMonitoringTrainingJobs');"
        "if(jobs){const counts=data.trainingJobCounts||{};"
        "const keys=Object.keys(counts);"
        "jobs.innerHTML=keys.length?table(['Training job status','Count'],"
        "keys.map(k=>[esc(k),String(counts[k])])):"
        "'<p>No training jobs recorded yet.</p>';}"
        "const evals=q('#mlMonitoringEvaluations');"
        "if(evals){const rows=data.evaluationMetrics||[];"
        "evals.innerHTML=rows.length?table("
        "['Evaluation run','Model','Dataset','Measured result'],"
        "rows.map(x=>[esc(x.name),escTrim(x.modelId,16),"
        "escTrim(x.datasetId,16),fmtMlMetrics(x.metrics)])):"
        "'<p>No completed evaluation runs with stored metrics yet.</p>';}"
        "const benches=q('#mlMonitoringBenchmarks');"
        "if(benches){const rows=data.inferenceBenchmarks||[];"
        "benches.innerHTML=rows.length?table("
        "['Model','Benchmark profile','Prompt tokens/sec',"
        "'Generation tokens/sec','Passed cases'],"
        "rows.map(x=>[escTrim(x.modelId,16),esc(x.profile),"
        "Number(x.promptTokensPerSecond).toFixed(1),"
        "Number(x.generationTokensPerSecond).toFixed(1),"
        "x.passedCases+' / '+x.totalCases])):"
        "'<p>No benchmark runs recorded yet -- run one from the "
        "Dashboard to populate real inference throughput evidence "
        "here.</p>';}}"
        // Each row gets its own Start/resume, Pause, Stop, and Remove buttons
        // wired directly to that job's id -- nothing to hand-type, unlike the
        // old single manual 'Download job ID' field this replaces. The State
        // cell carries its own id (state-<id>) so the progress poll below can
        // update it live instead of leaving it frozen at whatever it showed
        // when this row was last rendered.
        "function renderDownloads(downloads){const el=q('#downloadsList');if(!el)return;"
        "if(!downloads.length){el.innerHTML='<p>No downloads queued yet.</p>';return;}"
        "const ACTIVE=new Set(['queued','transferring','paused','verifying']);"
        "el.innerHTML=table(['Model','Filename','State','Progress',''],"
        "downloads.map(x=>[esc(x.modelId),esc(x.filename),"
        "'<span id=\"state-'+x.id+'\">'+esc(x.state)+'</span>',"
        "'<span id=\"progress-'+x.id+'\">'+"
        "(x.completedBytes/1048576).toFixed(1)+' MiB</span>',"
        "toolbar("
        "iconBtn(x.state==='complete'?'check':'play',"
        "x.state==='complete'?'Complete':'Start / resume','run-id',x.id,"
        "'',x.state==='complete'?'disabled':''),"
        "(x.state==='transferring'?"
        "iconBtn('pause','Pause','pause-id',x.id):''),"
        "(ACTIVE.has(x.state)?"
        "iconBtn('stop','Stop','stop-id',x.id,'iconBtn-delete'):''),"
        "iconBtn('trash','Remove','remove-id',x.id,'iconBtn-delete'))]));"
        "for(const btn of el.querySelectorAll('[data-run-id]')){"
        "btn.addEventListener('click',()=>runDownloadJob(btn.dataset.runId));}"
        "for(const btn of el.querySelectorAll('[data-pause-id]')){"
        "btn.addEventListener('click',()=>pauseDownload(btn.dataset.pauseId));}"
        "for(const btn of el.querySelectorAll('[data-stop-id]')){"
        "btn.addEventListener('click',()=>stopDownload(btn.dataset.stopId));}"
        "for(const btn of el.querySelectorAll('[data-remove-id]')){"
        "btn.addEventListener('click',()=>removeDownload(btn.dataset.removeId));}"
        // A job already 'transferring' (started earlier in this session or
        // by a page that's since been reloaded) has no JS-side poll loop of
        // its own yet -- without this, its progress and state cells only
        // ever show the single snapshot captured at this render and never
        // move again until something else happens to reload the page.
        // Resuming the poll here (without re-POSTing /run, which is already
        // in flight server-side) is what keeps it live.
        "for(const x of downloads){if(x.state==='transferring'&&!activePolls[x.id]){"
        "activePolls[x.id]=pollDownloadProgress(x.id);}}}"
        // Each section (chat, projects, models/*, admin/*) is its own
        // server-rendered page at its own URL now, so switching sections is
        // a normal browser navigation -- there is no client-side router.
        // Curated, size-tiered GGUF suggestions. Every commit hash and SHA-256
        // below was read directly from the Hugging Face API so the server's
        // immutable-revision and digest checks pass without hand-editing.
        + model_catalog_presets_js() +
        // Rebuilds the suggestion dropdown for the selected RAM tier and
        // immediately applies the first match. Auto-applying here (rather
        // than requiring a separate button click) is what prevents the
        // stale-form trap: switching tiers can no longer leave the form
        // holding a previous tier's model while looking like it was updated.
        // Every preset's sourceUrl host says which of the Download source
        // options (huggingface/github/modelscope) it actually came from --
        // used below so the suggestion list only ever offers models that
        // match the source currently selected, instead of mixing in
        // suggestions whose URL wouldn't even match the visible source
        // fields. 'custom' matches nothing, since a hand-typed source has no
        // curated suggestions.
        "function presetSource(p){"
        "if(p.sourceUrl.includes('huggingface.co'))return 'huggingface';"
        "if(p.sourceUrl.includes('modelscope.cn'))return 'modelscope';"
        "if(p.sourceUrl.includes('github.com'))return 'github';"
        "return 'custom';}"
        "function refreshPresets(){const sel=q('#downloadPreset');if(!sel)return;"
        "sel.replaceChildren();const tier=q('#downloadTier').value;"
        "const type=q('#downloadSourceType').value;"
        "for(const p of PRESETS.filter(x=>x.tier===tier&&presetSource(x)===type)){"
        "const o=document.createElement('option');o.value=p.id;o.textContent=p.label;"
        "sel.append(o);}"
        "if(sel.options.length)applyPreset();}"
        // Copies the selected preset's verified fields into the raw form;
        // license acceptance is left to the operator, never auto-checked.
        // Not every preset carries manifest metadata (architecture,
        // quantization, an SPDX id this server's allow-list recognizes) --
        // those without it downloaded the file fine but can't reach Ready
        // without an administrator hand-authoring a compliant manifest, so
        // the status line says so instead of silently leaving blank fields.
        "function applyPreset(){const p=PRESETS.find(x=>x.id===q('#downloadPreset').value);"
        "if(!p)return;lastAppliedPreset=p;q('#downloadCategory').value=p.category;"
        "q('#downloadModelId').value=p.modelId;q('#downloadFilename').value=p.filename;"
        "q('#downloadSourceUrl').value=p.sourceUrl;q('#downloadRevision').value=p.revision;"
        "q('#downloadSha256').value=p.sha256;q('#downloadMinRam').value=p.minRam;"
        "q('#downloadRecRam').value=p.recRam;"
        "q('#downloadDisplayName').value=p.displayName||'';"
        "q('#downloadArchitecture').value=p.architecture||'';"
        "q('#downloadQuantization').value=p.quantization||'';"
        "q('#downloadLicenseSpdx').value=p.licenseSpdx||'';"
        "q('#downloadSizeBytes').value=p.sizeBytes;"
        "q('#actionStatus').textContent=p.licenseSpdx?"
        "'Loaded suggestion: '+p.label+"
        "'. Review its license on Hugging Face before accepting.':"
        "'Loaded suggestion: '+p.label+"
        "\". This model's license isn't in the recognized list -- fill in \"+"
        "'Display name/Architecture/Quantization/License yourself, using a '+"
        "'license this server accepts, before queueing.';}"
        // Queueing immediately starts the transfer too -- Active downloads
        // (now the bottom of this same page) is where progress and any
        // later resume happens, so there is nothing left to hand-type here.
        "async function queueDownload(e){e.preventDefault();const s=q('#actionStatus');"
        "try{const body={filename:q('#downloadFilename').value,"
        "category:q('#downloadCategory').value,modelId:q('#downloadModelId').value,"
        "sourceUrl:q('#downloadSourceUrl').value,immutableRevision:q('#downloadRevision').value,"
        "expectedSha256:q('#downloadSha256').value,licenseAccepted:q('#downloadLicense').checked,"
        "displayName:q('#downloadDisplayName').value,"
        "architecture:q('#downloadArchitecture').value,"
        "quantization:q('#downloadQuantization').value,"
        "licenseSpdx:q('#downloadLicenseSpdx').value,"
        "sizeBytes:Number(q('#downloadSizeBytes').value),"
        "minimumRamMiB:Number(q('#downloadMinRam').value),"
        "recommendedRamMiB:Number(q('#downloadRecRam').value)};"
        "const r=await api('/api/v1/model-downloads','POST',body);"
        // Only trust a preset's known total size for the progress bar when
        // the form still matches that preset (the operator may have edited
        // the source URL by hand after loading a suggestion).
        "downloadSizes[r.id]=lastAppliedPreset&&"
        "lastAppliedPreset.sourceUrl===body.sourceUrl?lastAppliedPreset.sizeBytes:null;"
        "s.textContent='Queued download '+r.id+' ('+r.hardwareRecommendation+"
        "'). Starting transfer...';await load();await runDownloadJob(r.id);}"
        "catch(x){showSystemError('Queue failed: '+x.message);}}"
        // Polls the job-list route (the /run response itself only arrives
        // once the whole transfer finishes) and updates that row's state and
        // progress cells from the polled job, using a known total size when
        // available or falling back to a plain byte counter. Both cells are
        // updated from the same poll so State never goes stale while
        // Progress keeps moving.
        "const TERMINAL_DOWNLOAD_STATES=new Set("
        "['complete','failed','quarantined','cancelled','paused']);"
        "async function pollDownloadProgress(id){const cell=q('#progress-'+id);"
        "const stateCell=q('#state-'+id);"
        "const total=downloadSizes[id];let handle;"
        "const tick=async()=>{try{const d=await api('/api/v1/model-downloads');"
        "const job=d.downloads.find(x=>x.id===id);"
        // A completed download is auto-removed from the list server-side
        // the moment it finishes, so no matching job ever comes back for
        // it -- without this, the old '!job return' behaviour left this
        // interval ticking every second forever with nothing to stop it.
        "if(!job){clearInterval(handle);delete activePolls[id];"
        "if(stateCell)stateCell.textContent='complete';await load();return;}"
        "if(stateCell)stateCell.textContent=job.state;"
        "const mib=(job.completedBytes/1048576).toFixed(1);"
        "if(cell){if(total){cell.textContent=mib+' / '+(total/1048576).toFixed(1)+"
        "' MiB ('+Math.min(100,Math.round(job.completedBytes/total*100))+'%)';}"
        "else cell.textContent=mib+' MiB';}"
        // Stops itself once the transfer is no longer running -- this same
        // function is also how renderDownloads auto-resumes progress for a
        // job left 'transferring' across a page reload, so without this it
        // would keep polling a finished (or paused) job forever. A paused
        // job still needs a full re-render (via load()) to swap its row's
        // buttons back to Start/resume, so this only stops the polling loop,
        // not the eventual refresh.
        "if(TERMINAL_DOWNLOAD_STATES.has(job.state)){clearInterval(handle);"
        "delete activePolls[id];}"
        "}catch(x){console.warn('download poll tick failed, retrying next "
        "interval',x);}};"
        "await tick();handle=setInterval(tick,1000);return handle;}"
        // The server runs the transfer synchronously and only responds once
        // the job finishes, so this request can legitimately stay pending
        // for a long time on a large model; the progress cell above is what
        // fills that gap. Called both right after queueing and from a
        // row's own Start/resume button for a job that stalled or paused.
        "async function runDownloadJob(id){const s=q('#actionStatus');"
        "s.textContent='Downloading '+id+'... this can take a long time for large "
        "models; the page will keep working while it runs.';"
        "const timer=await pollDownloadProgress(id);"
        "try{const r=await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/run','POST');"
        "s.textContent='Download '+id+' finished with state: '+r.state;}"
        "catch(x){showSystemError('Download failed: '+x.message);}"
        "finally{if(timer)clearInterval(timer);await load();}}"
        // Pause/Stop signal the in-flight run() call (still pending on the
        // /run request started by runDownloadJob) rather than perform the
        // state change themselves -- that request's own 'finally' above is
        // what refreshes this row once the transfer actually lands in its
        // new state, so both handlers here just poll once immediately after
        // to reflect the signal being accepted.
        "async function pauseDownload(id){const s=q('#actionStatus');"
        "try{await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/pause','POST');"
        "s.textContent='Pausing download '+id+'...';await load();}"
        "catch(x){showSystemError('Pause failed: '+x.message);}}"
        "async function stopDownload(id){const s=q('#actionStatus');"
        "try{await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/cancel','POST');"
        "s.textContent='Stopping download '+id+'...';await load();}"
        "catch(x){showSystemError('Stop failed: '+x.message);}}"
        // Removing an active transfer stops it first (mirroring Stop) and
        // waits for its own run() call to actually unwind -- the manager
        // rejects remove() while a job is still in flight -- before deleting
        // its record, then refreshes the list so the row disappears.
        "async function removeDownload(id){const s=q('#actionStatus');"
        "try{const before=(await api('/api/v1/model-downloads'))"
        ".downloads.find(x=>x.id===id);"
        "if(before&&before.state==='transferring'){"
        "await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/cancel','POST')"
        ".catch(()=>{});"
        "for(let attempt=0;attempt<20;attempt++){"
        "await new Promise(resolve=>setTimeout(resolve,150));"
        "const current=(await api('/api/v1/model-downloads'))"
        ".downloads.find(x=>x.id===id);"
        "if(!current||current.state!=='transferring')break;}}"
        "await api('/api/v1/model-downloads/'+encodeURIComponent(id)+'/remove','POST');"
        "if(activePolls[id]){clearInterval(activePolls[id]);delete activePolls[id];}"
        "s.textContent='Download '+id+' removed.';await load();}"
        "catch(x){showSystemError('Remove failed: '+x.message);}}"
        "async function createUser(e){e.preventDefault();const s=q('#actionStatus');"
        "try{await api('/api/v1/users','POST',{username:q('#newUserName').value,"
        "displayName:q('#newUserDisplay').value,role:q('#newUserRole').value,"
        "password:q('#newUserPassword').value});"
        "s.textContent='Local account created.';q('#newUser').reset();await load();}"
        "catch(x){showSystemError('Create account failed: '+x.message);}}"
        "function fill(sel,items,key,label){const e=q(sel);if(!e)return;e.replaceChildren();"
        "for(const x of items){const o=document.createElement('option');o.value=key(x);"
        "o.textContent=label(x);e.append(o);}}"
        // Reduces a manifest's free-text quantization string (e.g. 'Q4_K_M',
        // 'Q5_K_S', 'Q8_0', 'F16') down to a short bracketed tag ('[Q4]',
        // '[Q5]', '[Q8]', '[F16]') for display next to a model's name.
        // Falls back to the whole string bracketed, or '' if none is set.
        "function quantTag(q){if(!q)return '';"
        "const m=/^[Qq](\\d+)/.exec(q);if(m)return '[Q'+m[1]+'] ';"
        "return '['+q+'] ';}"
        // Machine Learning forms (any path under /api/v1/ml/) route their
        // completion through showFormSuccess()'s centered green banner
        // instead of this plain #actionStatus line -- see that function's
        // own comment for why. Every other form keeps the original inline
        // status text.
        "async function submit(e,path,body){e.preventDefault();const s=q('#actionStatus');"
        "try{const r=await api(path,'POST',body());const msg='Completed: '+"
        "JSON.stringify(r);"
        "if(path.indexOf('/api/v1/ml/')===0){showFormSuccess(msg);}"
        "else{s.textContent=msg;}"
        "await load();}catch(x){showSystemError('Action failed: '+x.message);}}"
        // Uploads one picked file as a project attachment (text only, per
        // AttachmentStore::add_text) and adds it to the pending list for the
        // message currently being composed; attachmentIds are read off that
        // list at send time in streamMessage() rather than a hidden field.
        "async function attachFile(file){const s=q('#actionStatus');"
        "try{const content=await file.text();"
        "const record=await api('/api/v1/attachments','POST',"
        "{projectId:currentProjectId(),filename:file.name,content});"
        "attachedFiles.push({id:record.id,filename:file.name});renderAttachChips();}"
        "catch(x){showSystemError('Attach \\''+file.name+'\\' failed: '+x.message);}}"
        "function renderAttachChips(){const box=q('#attachChips');if(!box)return;"
        "box.replaceChildren();"
        "attachedFiles.forEach((f,i)=>{const chip=document.createElement('span');"
        "chip.className='attachChip';"
        "const label=document.createElement('span');label.textContent=f.filename;"
        "const remove=document.createElement('button');remove.type='button';"
        "remove.textContent='\\u00d7';remove.title='Remove attachment';"
        "remove.addEventListener('click',()=>{attachedFiles.splice(i,1);"
        "renderAttachChips();});"
        "chip.append(label,remove);box.append(chip);});}"
        // Composer support commands: typed as the whole message (leading/
        // trailing whitespace ignored, case-insensitive) instead of being
        // sent to the model. /app is a full navigation -- the same URL the
        // "+ New chat" sidebar link already uses -- so it resets every piece
        // of client state (messages, attachments, pickers) the same way a
        // fresh page load does, rather than this list having to duplicate
        // that reset by hand.
        "const SLASH_COMMANDS={'/clear':()=>location.href='/app',"
        "'/new':()=>location.href='/app',"
        "'/help':()=>{q('#actionStatus').textContent="
        "'Commands: /clear or /new starts a fresh chat. /help shows this "
        "message. Auto-drive: short confirm/implement/proceed/go ahead/make "
        "the changes/execute/apply/phase/plan/strategy messages ask the AI "
        "to actually carry out its last reply -- using real read/write/"
        "search/list/run-command tools, each shown in the transcript as it "
        "happens -- across as many turns as it takes, pausing for your "
        "Approve/Deny on anything destructive, until it reports done or "
        "you press Escape/Stop.';}};"
        // Word-boundary match against confirm/implement/proceed-style
        // phrasing, restricted to short (<=10-word) messages so it can't
        // fire inside an ordinary long sentence that happens to contain
        // "plan" -- see apply_auto_drive_directive() in server.cpp for
        // what turning this on actually asks the model to do.
        "const AUTO_DRIVE_RE=/\\b(confirm(ed)?|implement(ed|ing)?|make "
        "(the )?changes?|change it|proceed|go ahead|execute|apply( it| "
        "that| the plan)?|do it|start phase|begin phase|phases?|the plan|"
        "strategy)\\b/i;"
        "function isTaskContinuationCommand(text){const t=(text||'').trim();"
        "if(!t||t.split(/\\s+/).length>10)return false;"
        "return AUTO_DRIVE_RE.test(t);}"
        // Set once Escape/Stop is pressed (see the listeners below) so a
        // continuation turn already queued up (autoDriveState:'continue',
        // or a tool-approval resume in flight) does not fire after the
        // user asked everything to stop -- generation.abort() alone only
        // ever stops the *current* fetch, not turns still to come.
        "let autoDriveAborted=false;let autoDriveTurns=0;"
        "const AUTO_DRIVE_MAX_TURNS=25;"
        // Live status text shown in place of the generic 'Thinking' spinner
        // once something concrete is actually happening (a tool running) --
        // only replaces the spinner while no real reply text has streamed
        // yet, so it never clobbers a reply already in progress.
        "function setLiveStatus(assistantEl,text){if(!assistantEl)return;"
        "const bodyEl=assistantEl.querySelector('.msgBody');if(!bodyEl)return;"
        "if(assistantEl.dataset.raw)return;"
        "bodyEl.innerHTML='<span class=\"chatThinking\">"
        "<span class=\"chatThinkingSpinner\"></span>'+text+'</span>';}"
        // Tool-call/result cards reuse appendModelCard's plain-notice style
        // for the title line, but a tool_result with real output (e.g.
        // run_command's stdout) gets that output rendered into its own
        // scrollable <pre> block underneath -- reusing the same code-block
        // styling/copy button as a markdown code fence -- rather than being
        // squashed into the one-line, 200-char-truncated title text. The
        // approval card is richer still (reason, arguments, Approve/Deny).
        "function appendToolCard(box,title,body){if(!box)return null;"
        "const card=document.createElement('div');"
        "card.className='chatMsg chatMsg-toolResult';"
        "const titleEl=document.createElement('div');"
        "titleEl.className='chatMsg-toolResultTitle';titleEl.textContent=title;"
        "card.append(titleEl);"
        "if(body){const pre=document.createElement('pre');"
        "const codeEl=document.createElement('code');codeEl.textContent=body;"
        "pre.append(codeEl);card.append(pre);addCodeCopyButtons(card);}"
        "box.append(card);box.scrollTop=box.scrollHeight;return card;}"
        "function summarizeToolArguments(args){try{"
        "const text=JSON.stringify(args);"
        "return text&&text.length>200?text.slice(0,200)+'...':text;}"
        "catch(e){return '';}}"
        // Tool results vary in shape by tool (run_command's {exitCode,output},
        // read_file's plain text, an {error:...} failure, ...) -- this picks
        // out whichever field actually holds human-readable output instead
        // of always falling back to a truncated JSON.stringify blob, and
        // gives it a much larger cap since the <pre> block scrolls instead
        // of clipping.
        "function formatToolResultBody(result){try{"
        "if(result==null)return '';"
        "if(typeof result==='string')return result;"
        "if(typeof result==='object'){"
        "if(typeof result.output==='string'){"
        "const prefix='exitCode' in result?'exit '+result.exitCode+'\\n':'';"
        "return prefix+result.output;}"
        "if(typeof result.error==='string')return result.error;"
        "if(typeof result.content==='string')return result.content;}"
        "const text=JSON.stringify(result,null,2);"
        "return text&&text.length>8000?text.slice(0,8000)+'\\n...(truncated)':"
        "(text||'');}catch(e){return '';}}"
        "function appendApprovalCard(box,chatId,event){"
        "const card=document.createElement('div');"
        "card.className='chatMsg chatMsg-assistant chatMsg-error';"
        "const title=document.createElement('div');"
        "title.className='chatMsg-errorTitle';"
        "title.textContent='Approval needed: '+event.tool;"
        "const body=document.createElement('div');"
        "body.textContent=event.reason+' Arguments: '+"
        "summarizeToolArguments(event.arguments);"
        "const actions=document.createElement('div');"
        "actions.style.marginTop='0.5em';"
        "const approveBtn=document.createElement('button');"
        "approveBtn.type='button';approveBtn.textContent='Approve';"
        "const denyBtn=document.createElement('button');"
        "denyBtn.type='button';denyBtn.textContent='Deny';"
        "denyBtn.style.marginLeft='0.5em';"
        "const resolve=async(decision)=>{"
        "approveBtn.disabled=true;denyBtn.disabled=true;"
        "await resumeToolApproval(chatId,event.approvalId,decision,box);};"
        "approveBtn.addEventListener('click',()=>resolve('approve'));"
        "denyBtn.addEventListener('click',()=>resolve('deny'));"
        "actions.append(approveBtn,denyBtn);"
        "card.append(title,body,actions);"
        "box.append(card);box.scrollTop=box.scrollHeight;}"
        // Reads one NDJSON tool_call/tool_result/tool_approval_required/
        // complete/error/token stream from an already-started fetch Response
        // and applies it to the transcript -- shared by runTurn() (the
        // ordinary POST .../messages path) and resumeToolApproval() (POST
        // .../tool-approvals/{id}, which streams the same event shapes).
        // Returns the last 'complete' event seen (or null on error/abort),
        // so the caller can decide whether to continue the auto-drive loop.
        // Re-parsing and re-patching the *entire* accumulated reply on every
        // single token (the old behaviour, via requestAnimationFrame
        // coalescing) is still O(reply length) per re-render -- coalescing
        // to once per painted frame only bounded *how often* that cost was
        // paid, not the cost itself, so a long streamed reply still made the
        // main thread miss frames once the model warmed up and the reply
        // grew past a few paragraphs, reading as the page stalling roughly
        // once a second. This appends each token's raw text directly to a
        // single Text node instead -- O(1) per token, no markdown parsing,
        // no DOM diffing -- and defers the real markdown render entirely
        // until the stream finishes (see the flush after the read loop
        // below). The trade-off is that formatting (code fences, bold,
        // lists, ...) only appears once the reply completes rather than
        // live; plain text still streams in immediately either way.
        // el.rafPending only coalesces the scroll-into-view, which is cheap
        // but still forces a layout read if done on every token.
        // Lazily builds the live "Thinking..." panel the first time a
        // stream is detected to have opened a <think> block -- same markup/
        // classes renderMarkdown() itself produces for a completed reply
        // (the ".thinkBlock"/".thinkBody" CSS classes), so the swap at
        // stream-completion (which still re-renders from the server's
        // authoritative text) never reads as a different element suddenly
        // appearing in its place.
        "function ensureThinkBlock(el){if(el.thinkDetails)return el.thinkDetails;"
        "const bodyEl=el.querySelector('.msgBody');"
        "const details=document.createElement('details');"
        "details.className='thinkBlock thinkBlockLive';details.open=true;"
        "const summary=document.createElement('summary');"
        "summary.textContent='Thinking\\u2026';"
        "const body=document.createElement('div');body.className='thinkBody';"
        "const textNode=document.createTextNode('');body.appendChild(textNode);"
        "details.append(summary,body);bodyEl.appendChild(details);"
        "el.thinkDetails=details;el.thinkSummary=summary;el.thinkTextNode=textNode;"
        "return details;}"
        // <details> has no native open/close transition -- toggling `open`
        // off snapped the whole panel away in a single frame, which read as
        // the jarring "jump" the rest of this pass is fixing. This measures
        // the body's current rendered height, then animates max-height/
        // opacity down to zero before actually closing it.
        "function collapseThinkBlock(el){const details=el.thinkDetails;"
        "if(!details)return;if(el.thinkSummary)el.thinkSummary.textContent='Thinking';"
        "const body=details.querySelector('.thinkBody');"
        "if(!body){details.open=false;return;}"
        "body.style.maxHeight=body.scrollHeight+'px';body.style.overflow='hidden';"
        "body.style.transition='max-height .22s ease,opacity .22s ease';"
        "requestAnimationFrame(()=>{body.style.maxHeight='0px';body.style.opacity='0';});"
        "setTimeout(()=>{details.open=false;body.style.maxHeight='';"
        "body.style.overflow='';body.style.transition='';body.style.opacity='';},240);}"
        "function ensureAnswerNode(el){if(el.answerTextNode)return el.answerTextNode;"
        "const bodyEl=el.querySelector('.msgBody');"
        "el.answerTextNode=document.createTextNode('');bodyEl.appendChild(el.answerTextNode);"
        "return el.answerTextNode;}"
        // Feeds one streamed chunk of confirmed thinking-block content into
        // the live panel above, watching for the closing "</think>" tag
        // arriving split across separate token events via a small tail
        // buffer (never the whole reply seen so far) -- O(this chunk) per
        // call, not O(reply length), matching appendStreamToken's own
        // per-token cost budget (see its class comment below).
        "function feedThinkingChunk(el,chunk){ensureThinkBlock(el);"
        "el.thinkTailBuffer=(el.thinkTailBuffer||'')+chunk;"
        "const closeIdx=el.thinkTailBuffer.toLowerCase().indexOf('</think>');"
        "if(closeIdx>=0){const before=el.thinkTailBuffer.slice(0,closeIdx);"
        "if(before)el.thinkTextNode.appendData(before);"
        "collapseThinkBlock(el);"
        "const after=el.thinkTailBuffer.slice(closeIdx+8);"
        "el.thinkTailBuffer='';el.streamPhase='answer';"
        "if(after)ensureAnswerNode(el).appendData(after);}"
        "else{const keep=Math.min(el.thinkTailBuffer.length,8);"
        "const flush=el.thinkTailBuffer.slice(0,el.thinkTailBuffer.length-keep);"
        "if(flush)el.thinkTextNode.appendData(flush);"
        "el.thinkTailBuffer=el.thinkTailBuffer.slice("
        "el.thinkTailBuffer.length-keep);}}"
        // Phase 88 follow-up: a reasoning-capable model's <think>...</think>
        // block used to stream in as flat, undifferentiated text -- exactly
        // like the rest of the reply -- and only became the styled
        // collapsible panel renderMarkdown() knows how to draw once the
        // *entire* reply finished and got its one full markdown re-parse
        // (see that comment below). The actual thinking, which can run for
        // a real amount of time on a reasoning-heavy model, looked like
        // nothing was happening beyond plain scrolling text. This now
        // classifies the stream into three phases per token -- 'unknown'
        // (still buffering up to a "<think>"-length prefix to decide),
        // 'thinking' (routed live into the panel above via
        // feedThinkingChunk), 'answer' (appended to its own plain text node,
        // exactly as before) -- so reasoning shows up in its own container
        // the moment it starts, not only in retrospect. Every path here
        // stays O(1) amortized per token: the prefix decision buffers at
        // most a few characters before committing, and thinking-phase
        // routing never rescans more than the small tail buffer above.
        "function appendStreamToken(el,box,content){"
        "const bodyEl=el.querySelector('.msgBody');if(!bodyEl)return;"
        "if(!el.streamingText){bodyEl.replaceChildren();"
        "bodyEl.style.whiteSpace='pre-wrap';el.streamingText=true;"
        "el.streamPhase='unknown';el.prefixBuffer='';}"
        "if(el.streamPhase==='unknown'){el.prefixBuffer+=content;"
        "const trimmed=el.prefixBuffer.replace(/^\\s+/,'');"
        "if(trimmed.length>=7){"
        "if(/^<think>/i.test(trimmed)){el.streamPhase='thinking';"
        "feedThinkingChunk(el,trimmed.slice(7));}"
        "else{el.streamPhase='answer';"
        "ensureAnswerNode(el).appendData(el.prefixBuffer);}"
        "el.prefixBuffer='';}}"
        "else if(el.streamPhase==='thinking'){feedThinkingChunk(el,content);}"
        "else{ensureAnswerNode(el).appendData(content);}"
        // scrollTo(...,{behavior:'smooth'}) rather than a hard scrollTop
        // jump -- still coalesced to once per animation frame below, but
        // now the box eases toward the new bottom each time instead of
        // snapping straight there, which is what actually read as "jaggy"
        // during a fast-streaming reply.
        "if(!el.rafPending){el.rafPending=true;"
        "requestAnimationFrame(()=>{el.rafPending=false;"
        "box.scrollTo({top:box.scrollHeight,behavior:'smooth'});});}}"
        "async function readTurnStream(r,box,chatId,userEl,assistantEl){"
        "const reader=r.body.getReader(),decoder=new TextDecoder();let pending='';"
        "let completeEvent=null;"
        "for(;;){const x=await reader.read();if(x.done)break;"
        "pending+=decoder.decode(x.value,{stream:true});let n;"
        "while((n=pending.indexOf('\\n'))>=0){const line=pending.slice(0,n);"
        "pending=pending.slice(n+1);"
        "if(!line)continue;const event=JSON.parse(line);"
        "if(event.type==='token'){"
        "if(!assistantEl){assistantEl=appendMessage(box,'assistant','');}"
        "assistantEl.dataset.raw=(assistantEl.dataset.raw||'')+event.content;"
        // Counted client-side, one per 'token' SSE event received, so the
        // titlebar updates live as the reply streams in instead of sitting
        // blank until the final 'complete' event backfills it (see that
        // handler below, which still overwrites this with the server's own
        // authoritative count once the turn finishes -- a tool-call turn
        // withholds some chunks from the stream entirely, so this running
        // count is an approximation of what's on screen, not a guarantee of
        // matching the model's real token count).
        "assistantEl.streamedTokens=(assistantEl.streamedTokens||0)+1;"
        "setMsgTokens(assistantEl,'assistant',assistantEl.streamedTokens);"
        "appendStreamToken(assistantEl,box,event.content);}"
        "if(event.type==='tool_call'){"
        "if(!assistantEl){assistantEl=appendMessage(box,'assistant','');}"
        "setLiveStatus(assistantEl,'Running '+event.tool+"
        "'('+summarizeToolArguments(event.arguments)+')...');"
        "appendToolCard(box,'Running tool: '+event.tool+' '+"
        "summarizeToolArguments(event.arguments));}"
        "if(event.type==='tool_result'){"
        "const outcome=event.succeeded?'Tool result':'Tool failed';"
        "appendToolCard(box,outcome+' ('+event.tool+')',"
        "formatToolResultBody(event.result));}"
        // The model attempted a tool call in a chat where tools aren't
        // actually available (no project bound, not auto-drive) -- the
        // server already stripped the raw attempt out of the reply text, so
        // without this the turn just trailed off with no explanation. Shown
        // as its own card, same style as a real tool result, so it's
        // obvious something was attempted rather than looking like the
        // model simply stopped mid-thought.
        "if(event.type==='tool_notice'){"
        "appendToolCard(box,event.message||"
        "('Tool unavailable: '+event.tool),"
        "summarizeToolArguments(event.arguments));}"
        "if(event.type==='tool_approval_required'){"
        "appendApprovalCard(box,chatId,event);}"
        "if(event.type==='complete'){completeEvent=event;"
        // The client's dataset.raw was built purely by appending each raw
        // 'token' chunk as it streamed in, *before* the server's own
        // detect_and_strip_tool_call() cleanup runs on the full reply (that
        // only happens once generation finishes). Without this, a stray/
        // hallucinated tool-call attempt the server successfully strips
        // from what it persists would still render raw -- stray commas,
        // brackets and all -- because the final markdown render below reads
        // from this locally-accumulated buffer, not from what the server
        // actually saved. 'content' carries the server's authoritative
        // post-cleanup text, so swap it in before that render happens.
        "if(typeof event.content==='string')assistantEl.dataset.raw="
        "event.content;"
        "if(userEl)setMsgTokens(userEl,'user',event.promptTokens||0);"
        "if(assistantEl)setMsgTokens(assistantEl,'assistant',"
        "event.generatedTokens||0);"
        "if(event.memorySaved)refreshMemories().catch(()=>{});}"
        "if(event.type==='error')throw new Error(event.error,"
        "{cause:event.detail});}}"
        // The stream is finished here; appendStreamToken() above only ever
        // appended plain text (either straight to a bare answer Text node,
        // or -- live -- into the lightweight thinking-panel preview built by
        // ensureThinkBlock()/feedThinkingChunk()) with no real markdown
        // parsing, so formatting stays snappy while tokens are arriving --
        // this is the one point that actually renders the real markdown,
        // from the full accumulated raw text. patchMsgBody()'s diff walks
        // bodyEl's *element* children, which the streaming Text node(s) and
        // the live thinking-panel preview aren't, so they're all cleared
        // first or they'd end up sitting duplicated alongside the freshly
        // rendered elements -- the preview panel was only ever a stand-in
        // for this authoritative render, not a second source of truth.
        "if(assistantEl){const bodyEl=assistantEl.querySelector('.msgBody');"
        "if(bodyEl){if(assistantEl.streamingText){bodyEl.replaceChildren();"
        "bodyEl.style.whiteSpace='';assistantEl.streamingText=false;}"
        "patchMsgBody(bodyEl,renderMarkdown(assistantEl.dataset.raw||''));"
        "addCodeCopyButtons(bodyEl);box.scrollTop=box.scrollHeight;}}"
        "return completeEvent;}"
        // Posts one turn's content (real user text on the very first turn
        // of an exchange, 'Continue.' on every automatic follow-up) and,
        // once it finishes, automatically posts the next turn itself when
        // the stream reported autoDriveState:'continue' -- this is the
        // client-side half of auto-drive mode (see docs/PLAN.md Phase 84
        // for why the loop lives here, one ordinary POST per turn, rather
        // than inside the server's own request handler). Bounded by
        // AUTO_DRIVE_MAX_TURNS and stopped immediately by autoDriveAborted
        // (Escape/Stop -- see the listeners below).
        "async function runTurn(chatId,content,attachmentIds,autoDrive,box,userEl){"
        "generation=new AbortController();"
        "const assistantEl=appendMessage(box,'assistant','');"
        // Shown until the first real event streams back -- reasoning models
        // in particular can take a real amount of time to produce anything,
        // and an empty bubble with no feedback reads as a hang.
        "setLiveStatus(assistantEl,'Thinking');box.scrollTop=box.scrollHeight;"
        "const r=await fetch('/api/v1/chats/'+encodeURIComponent(chatId)+'/messages',"
        "{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},"
        "body:JSON.stringify({content,attachmentIds:attachmentIds||[],"
        "autoDrive:!!autoDrive,"
        "effort:q('#modelEffort')?q('#modelEffort').value:'medium',"
        "thinking:q('#modelThinking')?q('#modelThinking').value:'off'}),"
        "signal:generation.signal});"
        "if(!r.ok)throw new Error(await r.text());"
        "const completeEvent=await readTurnStream(r,box,chatId,userEl,assistantEl);"
        "if(completeEvent&&completeEvent.autoDriveState==='continue'&&"
        "!autoDriveAborted&&autoDriveTurns<AUTO_DRIVE_MAX_TURNS){"
        "autoDriveTurns++;"
        "await runTurn(chatId,'Continue.',[],true,box,null);}}"
        // Resumes a paused high-risk tool call from its Approve/Deny card,
        // then keeps the auto-drive loop going the same way runTurn()'s own
        // tail call does.
        "async function resumeToolApproval(chatId,approvalId,decision,box){"
        "try{generation=new AbortController();"
        "const r=await fetch('/api/v1/chats/'+encodeURIComponent(chatId)+"
        "'/tool-approvals/'+encodeURIComponent(approvalId),"
        "{method:'POST',headers:{'Content-Type':'application/json','X-CSRF-Token':csrf},"
        "body:JSON.stringify({decision}),signal:generation.signal});"
        "if(!r.ok)throw new Error(await r.text());"
        "const completeEvent=await readTurnStream(r,box,chatId,null,null);"
        "if(completeEvent&&completeEvent.autoDriveState==='continue'&&"
        "!autoDriveAborted&&autoDriveTurns<AUTO_DRIVE_MAX_TURNS){"
        "autoDriveTurns++;await runTurn(chatId,'Continue.',[],true,box,null);}}"
        "catch(x){showSystemError('Failed to resume: '+x.message);}"
        "finally{generation=null;}}"
        // A brand-new chat (no project/model form of its own any more) is
        // created lazily on the first message: the composer's own project
        // and model pickers supply what /api/v1/chats needs, and the URL
        // updates to that chat's own address without a full reload. Tokens
        // stream directly into a new assistant bubble instead of the status
        // line, and the user's own message is appended immediately so the
        // conversation reads like a real chat, not a debug log.
        "async function streamMessage(e){e.preventDefault();const s=q('#actionStatus');"
        "const box=q('#chatMessages');const content=q('#messageContent').value;"
        "if(!content.trim())return;"
        "const command=SLASH_COMMANDS[content.trim().toLowerCase()];"
        "if(command){q('#messageContent').value='';command();return;}"
        "const autoDrive=isTaskContinuationCommand(content);"
        "autoDriveAborted=false;autoDriveTurns=0;"
        "try{let chatId=q('#messageChat').value;"
        "if(!chatId){const projectId=q('#chatProject').value,modelId=q('#chatModel').value;"
        "if(!projectId||!modelId){"
        "s.textContent='Choose a project and a downloaded model first.';return;}"
        // Whatever the composer's Tool execution selector is currently set
        // to (default 'auto' -- see the panel's markup) becomes this brand
        // new chat's persisted mode from creation onward.
        "const toolMode=q('#chatToolMode')?q('#chatToolMode').value:'auto';"
        "const created=await api('/api/v1/chats','POST',"
        "{projectId,modelId,toolExecutionMode:toolMode});"
        "chatId=created.id;openedProjectId=projectId;"
        // Render the new chat into the sidebar immediately from the
        // create response itself, instead of waiting on load()'s full
        // GET /api/v1/chats round trip below to be the only thing that
        // ever shows it -- a caller that navigates away, or where that
        // refetch is ever skipped, still leaves the new chat visible.
        "renderChatList([{id:chatId,title:'New chat'},...lastChats]);"
        // Sync the hidden chat-id field before reloading so load() takes the
        // openChat() branch (hides the empty-state greeting, clears/repopulates
        // #chatMessages, disables the now-fixed project/model pickers) instead
        // of re-running the brand-new-chat branch, which would leave the
        // greeting visible at the same time as the message about to be
        // appended below.
        "q('#messageChat').value=chatId;"
        "history.pushState(null,'','/app/chat/'+encodeURIComponent(chatId));"
        "await load();"
        // The dropdown itself doesn't fire 'change' for the model a new
        // chat was created with (only for a later switch, via
        // changeChatModel() above), so nothing would otherwise say which
        // model this chat is actually using -- confirm it here instead,
        // reading the label back off the now-reloaded #chatModel picker.
        "const chosen=q('#chatModel');"
        "const chosenOpt=chosen?chosen.options[chosen.selectedIndex]:null;"
        "appendModelCard(q('#chatMessages'),"
        "'Using model '+(chosenOpt?chosenOpt.textContent:modelId));}"
        "q('#messageContent').value='';"
        "const attachmentIds=attachedFiles.map(x=>x.id);"
        "attachedFiles=[];renderAttachChips();"
        // Kept so the stream's 'complete' event below can back-fill this
        // query bubble's title with the prompt-token count.
        "const userEl=appendMessage(box,'user',content);"
        "box.scrollTop=box.scrollHeight;"
        "await runTurn(chatId,content,attachmentIds,autoDrive,box,userEl);}"
        "catch(x){const cancelled=x.name==='AbortError';"
        // The server used to fold every generation-time exception (model not
        // loaded, runner crash, degraded model producing an empty reply,
        // etc.) into the single opaque 'generation_failed' code with no way
        // to tell them apart. It now also sends the real exception text as
        // `detail` (carried here via Error's `cause`) -- show that when
        // present instead of guessing, and fall back to the old generic
        // wording only if an older server/response omitted it.
        "if(x.message==='generation_failed')x.message=x.cause||"
        "'The model failed to generate a reply. It may still be loading, "
        "downloading, or unable to run on this machine -- check Model "
        "inventory and try again.';"
        "if(cancelled)s.textContent='Cancelled.';"
        // A cancelled turn already left whatever partial reply the user saw
        // on screen (the server persists it too, see streamed_text in
        // send_chat_message) -- overwriting it here would erase text the
        // user already read. A real failure, though, previously left an
        // empty bubble with the only explanation in the easy-to-miss status
        // line above the composer; showing it as its own card puts it where
        // a reply would have appeared.
        "if(!cancelled){const el=appendMessage(box,'assistant','');"
        "el.className='chatMsg chatMsg-assistant chatMsg-error';el.replaceChildren();"
        "const title=document.createElement('div');"
        "title.className='chatMsg-errorTitle';title.textContent='SYSTEM ERROR!';"
        "const body=document.createElement('div');body.textContent=x.message;"
        // Same reusable clipboard helper the reply/code copy buttons use --
        // dataset.raw is what addMessageCopyButton() would normally read,
        // set here since this card replaces the bubble's usual rendered
        // content instead of going through appendMessage()'s own path.
        "el.dataset.raw=x.message;"
        "el.append(title,body);addMessageCopyButton(el);"
        "box.scrollTop=box.scrollHeight;}}"
        "finally{generation=null;}}"
        // Fills the raw source fields from the Hugging Face helper inputs;
        // the operator still supplies model ID, RAM figures, SHA-256, and
        // license acceptance separately, so nothing about validation changes.
        "function applyHfSource(){const repo=q('#hfRepo').value.trim(),"
        "revision=q('#hfRevision').value.trim(),filename=q('#hfFilename').value.trim();"
        "if(!repo||!revision||!filename)return;"
        "q('#downloadSourceUrl').value='https://huggingface.co/'+repo+'/resolve/'+"
        "revision+'/'+filename;"
        "q('#downloadRevision').value=revision;q('#downloadFilename').value=filename;"
        "q('#actionStatus').textContent='Filled the source URL from Hugging Face fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        "function applyGithubSource(){const repo=q('#ghRepo').value.trim(),"
        "tag=q('#ghTag').value.trim(),asset=q('#ghAsset').value.trim();"
        "if(!repo||!tag||!asset)return;"
        "q('#downloadSourceUrl').value='https://github.com/'+repo+'/releases/download/'+"
        "tag+'/'+asset;"
        "q('#downloadRevision').value=tag;q('#downloadFilename').value=asset;"
        "q('#actionStatus').textContent='Filled the source URL from GitHub release fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        // ModelScope serves immutable-revision files at the same
        // /resolve/<revision>/<file> path shape as Hugging Face, so this
        // mirrors applyHfSource() rather than introducing a different shape.
        "function applyMsSource(){const repo=q('#msRepo').value.trim(),"
        "revision=q('#msRevision').value.trim(),filename=q('#msFilename').value.trim();"
        "if(!repo||!revision||!filename)return;"
        "q('#downloadSourceUrl').value='https://modelscope.cn/models/'+repo+'/resolve/'+"
        "revision+'/'+filename;"
        "q('#downloadRevision').value=revision;q('#downloadFilename').value=filename;"
        "q('#actionStatus').textContent='Filled the source URL from ModelScope fields. "
        "Still set model ID, RAM, SHA-256, and review the license before queueing.';}"
        // Confirms which model a chat is using inline, in the transcript
        // itself, rather than only in the easy-to-miss status line -- so
        // it's obvious which replies came from which model. Shared by the
        // very first model choice on a brand-new chat and by later
        // switches (changeChatModel() below) so the confirmation always
        // appears, not just on a subsequent change.
        "function appendModelCard(box,text){if(!box)return;"
        "const card=document.createElement('div');"
        "card.className='chatMsg chatMsg-modelChange';card.textContent=text;"
        "box.append(card);box.scrollTop=box.scrollHeight;}"
        // Picks the model a brand-new chat's picker starts on: whatever the
        // runner already has warm (Ready state) takes priority, since
        // reusing it avoids a fresh load/warm-up cycle; otherwise falls back
        // to the model the most recent chat used. Leaves the picker's
        // browser-default selection alone if neither candidate is in the
        // ready list (e.g. a fresh install with nothing warm and no chat
        // history yet).
        "async function selectDefaultChatModel(ready,chats){"
        "const picker=q('#chatModel');if(!picker||!ready.length)return;"
        "let defaultId=null;"
        "try{const st=await api('/api/v1/runner/status');"
        "if(st.warmState==='Ready'&&st.modelId&&"
        "ready.some(x=>x.id===st.modelId))defaultId=st.modelId;}catch(x){"
        "console.warn('selectDefaultChatModel: runner status check failed, "
        "falling back to last chat model',x);}"
        "if(!defaultId){"
        "const lastChat=chats.find(x=>x.modelId&&ready.some(r=>r.id===x.modelId));"
        "if(lastChat)defaultId=lastChat.modelId;}"
        "if(defaultId){picker.value=defaultId;refreshModelSettingsPanel();}}"
        // Polls GET /api/v1/runner/status (see server.cpp) so the chat page
        // can announce once, inline, the moment the backend actually
        // finishes loading -- not just the initial 'Using model X' choice.
        // warmPollBaselined swallows whatever warmState the very first poll
        // finds (a model that was already warm from an earlier chat isn't a
        // transition worth announcing); after that, lastAnnouncedWarmModelId
        // is compared against the polled modelId, so the card fires exactly
        // once per model becoming Ready and stays silent on every later poll
        // for that same model -- it only fires again once the id actually
        // changes (the model was switched away from and back, or a
        // different chat's model finished loading), matching "warmed" being
        // per-model state, not a one-time-ever flag.
        "let warmPollBaselined=false,lastAnnouncedWarmModelId=null;"
        "async function pollRunnerStatus(){try{const st=await api('/api/v1/runner/status');"
        "if(!warmPollBaselined){warmPollBaselined=true;"
        "if(st.warmState==='Ready')lastAnnouncedWarmModelId=st.modelId;return;}"
        "if(st.warmState==='Ready'&&st.modelId&&"
        "lastAnnouncedWarmModelId!==st.modelId){"
        "lastAnnouncedWarmModelId=st.modelId;"
        "const box=q('#chatMessages');if(!box)return;"
        "const select=q('#chatModel');"
        "const opt=select?[...select.options].find(o=>o.value===st.modelId):null;"
        "appendModelCard(box,'Model warmed: '+(opt?opt.textContent:st.modelId)+"
        "' is ready.');}}catch(x){console.warn('pollRunnerStatus: status "
        "poll failed, retrying next interval',x);}}"
        // Model Inventory page "Memory Status" widget. Lives here (not an
        // inline <script> on the page itself) because html_response()'s
        // CSP is script-src 'self' -- an inline script is blocked outright,
        // it never even runs. Every function below no-ops harmlessly on any
        // page that doesn't have #memoryStatusSection, exactly like
        // renderModels()/pollRunnerStatus() above already no-op via their
        // own q(...)==null guards.
        "function bytesMiB(b){return Math.round(b/1048576);}"
        "function reportRow(label,value){return "
        "'<tr><td class=\"reportLabel\">'+label+"
        "'</td><td class=\"reportValue\">'+value+'</td></tr>';}"
        "function renderMemoryStatusPanel(resources,mem,cacheStatus,"
        "scratchStatus){"
        "const el=q('#memoryStatusTable');if(!el)return;"
        "let cacheUsed=0,cacheCapacity=0;"
        "Object.values(cacheStatus.categories).forEach(v=>{"
        "cacheUsed+=v.usedBytes;cacheCapacity+=v.capacityBytes;});"
        "el.innerHTML='<table><tbody>'+"
        "reportRow('Memory pressure',mem.pressure)+"
        "reportRow('System RAM',resources.availableRamMiB+' MiB free of '+"
        "resources.totalRamMiB+' MiB')+"
        "reportRow('System virtual memory',resources.availableVirtualMemoryMiB+"
        "' MiB free of '+resources.totalVirtualMemoryMiB+' MiB')+"
        "reportRow('MasterAI cache usage',bytesMiB(cacheUsed)+' / '+"
        "bytesMiB(cacheCapacity)+' MiB')+"
        "reportRow('MasterAI temporary files',bytesMiB(scratchStatus."
        "globalReservedBytes)+' / '+bytesMiB(scratchStatus.globalQuotaBytes)+"
        "' MiB ('+scratchStatus.activeJobs.length+' active)')+"
        "'</tbody></table>';}"
        "async function refreshMemoryStatus(){"
        "if(!q('#memoryStatusSection'))return;"
        "try{const[resources,mem,cacheStatus,scratchStatus]="
        "await Promise.all([api('/api/v1/system/resources'),"
        "api('/api/v1/system/memory'),api('/api/v1/system/cache'),"
        "api('/api/v1/system/scratch')]);"
        "renderMemoryStatusPanel(resources,mem,cacheStatus,scratchStatus);}"
        // Any failure (permission, network, a bad response) shows the real
        // reason in place of the table instead of leaving it stuck on
        // "Loading..." forever with no way to tell why.
        "catch(e){const el=q('#memoryStatusTable');"
        "if(el)el.textContent='Could not load memory status: '+e.message;}}"
        "let cleanPollHandle=null;"
        "const MEMORY_CLEAN_STEP_LABELS={trimOtherProcessWorkingSets:"
        "'trim other applications\\' working sets',flushModifiedPageList:"
        "'flush the modified page list',purgeStandbyList:'purge the standby "
        "list',purgeLowPriorityStandbyPages:'purge low-priority standby "
        "pages',emptySystemAndServiceWorkingSets:'empty system and service "
        "working sets',clearSystemFileCache:'clear the system file cache'};"
        "function summarizeMemoryClean(r){const parts=[];"
        "if(r.cacheBytesFreed)parts.push(bytesMiB(r.cacheBytesFreed)+"
        "' MiB of cache freed');"
        "if(r.scratchOrphansRemoved)parts.push(r.scratchOrphansRemoved+"
        "' orphaned temporary file(s) removed');"
        "if(r.processWorkingSetBytesFreed)parts.push(bytesMiB("
        "r.processWorkingSetBytesFreed)+' MiB released back to the OS');"
        "if(r.modelUnloaded)parts.push('AI model unloaded');"
        "if(r.otherProcessesTrimmed)parts.push(r.otherProcessesTrimmed+"
        "' other application(s) trimmed');"
        "if(r.modifiedPageListFlushed)parts.push('modified page list flushed');"
        "if(r.standbyListPurged)parts.push('standby list purged');"
        "if(r.lowPriorityStandbyPurged)parts.push('low-priority standby "
        "pages purged');"
        "if(r.systemWorkingSetsEmptied)parts.push(r.systemWorkingSetsEmptied+"
        "' system/service working set(s) emptied');"
        "if(r.systemFileCacheCleared)parts.push('system file cache cleared');"
        "let summary=parts.length?'Done: '+parts.join(', ')+'.':"
        "'Done: nothing further to free right now.';"
        // Every requested-but-denied privileged step is named, not just
        // counted -- an administrator who ticked a box and saw it do
        // nothing needs to know it was refused, and why, not guess.
        "if(r.privilegeDeniedSteps&&r.privilegeDeniedSteps.length){"
        "summary+=' Skipped (requires MasterAI to run as Administrator): '+"
        "r.privilegeDeniedSteps.map(s=>MEMORY_CLEAN_STEP_LABELS[s]||s)"
        ".join(', ')+'.';}"
        "return summary;}"
        "async function pollCleanProgress(){"
        "try{const r=await api('/api/v1/system/memory/clean');"
        "const bar=q('#memCleanProgress');const status=q('#memCleanStatus');"
        "if(bar)bar.value=r.percent;"
        "if(status)status.textContent=r.state==='running'?"
        "(r.currentStep||'Cleaning memory...')+' ('+r.percent+'%)':"
        "(r.state==='failed'?'Failed: '+r.diagnostic:summarizeMemoryClean(r));"
        "if(r.state==='complete'||r.state==='failed'){"
        "clearInterval(cleanPollHandle);cleanPollHandle=null;"
        "const btn=q('#memCleanStart');if(btn)btn.disabled=false;"
        "if(bar)setTimeout(()=>{bar.hidden=true;},1500);"
        "await refreshMemoryStatus();"
        "try{const m=await api('/api/v1/models');"
        "if(typeof renderModels==='function')renderModels(m.models);}"
        "catch(e){}}"
        "}catch(e){}}"
        // Fires when the composer's model picker changes while a chat is
        // already open -- persists the new model against the chat so it's
        // still selected (and used) on the next message and after a reload.
        // A brand-new chat (no id yet) has nothing to persist against, so
        // it's left alone; the chosen model just gets used (and confirmed
        // via appendModelCard) when the chat is created on first send, see
        // streamMessage().
        "async function changeChatModel(){const chatId=q('#messageChat').value;"
        "if(!chatId)return;const s=q('#actionStatus');const select=q('#chatModel');"
        "const modelId=select.value;"
        "try{await api('/api/v1/chats/'+encodeURIComponent(chatId)+'/model','POST',"
        "{modelId});"
        "const opt=select.options[select.selectedIndex];"
        "appendModelCard(q('#chatMessages'),"
        "'Model switched to '+(opt?opt.textContent:modelId));}"
        "catch(x){showSystemError('Failed to switch model: '+x.message);}}"
        // Keeps the one-line explanation under the Tool execution selector
        // in sync with whatever option is currently chosen, so the setting
        // is self-explanatory without external docs (see the three modes'
        // own server-side comments in set_chat_tool_execution_mode()).
        "const TOOL_MODE_NOTES={"
        "auto:'Safe actions (read, search, list) run immediately; "
        "risky ones (write, delete, run a command) always pause for your "
        "approval first.',"
        "confirm_all:'Every tool action, safe or not, pauses and shows you "
        "an Approve/Deny card before it runs.',"
        "off:'The model has no tools in this chat at all -- it can only "
        "reply with text.'};"
        "function updateChatToolModeNote(){const select=q('#chatToolMode'),"
        "note=q('#chatToolModeNote');if(!select||!note)return;"
        "note.textContent=TOOL_MODE_NOTES[select.value]||'';}"
        // Fires when the composer's Tool execution selector changes while a
        // chat is already open -- persists it against the chat immediately
        // (matching changeChatModel() above) so it applies to this chat's
        // very next turn, on any device. A brand-new chat (no id yet) has
        // nothing to persist against yet; its chosen value is instead sent
        // with the chat's own creation request, see streamMessage().
        "async function changeChatToolMode(){updateChatToolModeNote();"
        "const chatId=q('#messageChat').value;if(!chatId)return;"
        "const mode=q('#chatToolMode').value;"
        "try{await api('/api/v1/chats/'+encodeURIComponent(chatId)+'/tool-mode',"
        "'POST',{toolExecutionMode:mode});}"
        "catch(x){showSystemError('Failed to change tool execution mode: '+"
        "x.message);}}"
        // Phase 84 follow-up: renders the run_command admin allow-list --
        // see the /app/settings/allowed-commands page comment for why this
        // page had to exist. Each row shows exactly what the model is
        // permitted to invoke (executable, description, OS, default risk,
        // any project restriction) with Edit/Revoke buttons, so approving
        // something is never a silent, unreviewable action.
        // allowedCommandsCache holds the last fetched list so the OS filter
        // can re-render instantly without a round trip, and
        // allowedCommandEditingId is non-null while the form below is
        // editing an existing row rather than creating a new one.
        "let allowedCommandsCache=[];let allowedCommandEditingId=null;"
        "function osLabel(os){return os==='windows'?'Windows':"
        "os==='linux'?'Linux':'Windows & Linux';}"
        "function renderAllowedCommands(commands){"
        "allowedCommandsCache=commands;"
        "const list=q('#allowedCommandsList');if(!list)return;"
        "const filter=q('#allowedCommandOsFilter');"
        "const filterValue=filter?filter.value:'all';"
        "const filtered=filterValue==='all'?commands:"
        "commands.filter(cmd=>cmd.os===filterValue);"
        "list.replaceChildren();if(!filtered.length){"
        "const empty=document.createElement('div');"
        "empty.textContent=commands.length?"
        "'No approved executables match this OS filter.':"
        "'No executables are approved -- run_command will refuse every call "
        "until one is added above.';list.append(empty);return;}"
        "for(const cmd of filtered){const row=document.createElement('div');"
        "row.className='allowedCommandItem'+"
        "(cmd.enabled?'':' allowedCommandItem-disabled');"
        "const info=document.createElement('span');"
        "info.textContent=cmd.executable+"
        "(cmd.description?' -- '+cmd.description:'')+"
        "' ['+osLabel(cmd.os)+', '+cmd.riskDefault+']'+"
        "(cmd.allowedProjectIds.length?"
        "' (projects: '+cmd.allowedProjectIds.join(', ')+')':"
        "' (all projects)')+"
        "(cmd.enabled?'':' (disabled)');"
        "row.append(info);"
        // Icon-only row buttons (ICONS/.iconBtn -- the same small
        // square-glyph convention every other row toolbar on this page
        // uses) instead of full-width text buttons, so a long executable
        // list stays compact.
        "const actions=document.createElement('span');"
        "actions.className='allowedCommandActions';"
        "const edit=document.createElement('button');edit.type='button';"
        "edit.className='iconBtn';edit.title='Edit';"
        "edit.setAttribute('aria-label','Edit');"
        "edit.innerHTML=ICONS.pencil;"
        "edit.addEventListener('click',()=>beginEditAllowedCommand(cmd));"
        "actions.append(edit);"
        "const del=document.createElement('button');del.type='button';"
        "del.className='iconBtn iconBtn-delete';del.title='Revoke';"
        "del.setAttribute('aria-label','Revoke');"
        "del.innerHTML=ICONS.trash;"
        "del.addEventListener('click',async()=>{"
        "try{await api('/api/v1/chat-tools/allowed-commands/remove','POST',"
        "{id:cmd.id});if(allowedCommandEditingId===cmd.id)"
        "cancelEditAllowedCommand();await refreshAllowedCommands();}"
        "catch(x){showSystemError('Could not revoke command: '+x.message);}});"
        "actions.append(del);row.append(actions);list.append(row);}}"
        "async function refreshAllowedCommands(){"
        "if(!q('#allowedCommandsList'))return;"
        "try{const data=await api('/api/v1/chat-tools/allowed-commands');"
        "renderAllowedCommands(data.commands||[]);}"
        "catch(x){showSystemError('Could not load the allow-list: '+x.message);}}"
        // Populates the form from an existing record and flips it into edit
        // mode; addAllowedCommand() below checks allowedCommandEditingId to
        // decide whether to PUT-style update this id or POST a new record.
        "function beginEditAllowedCommand(cmd){"
        "allowedCommandEditingId=cmd.id;"
        "q('#allowedCommandExecutable').value=cmd.executable;"
        "q('#allowedCommandDescription').value=cmd.description||'';"
        "q('#allowedCommandRisk').value=cmd.riskDefault;"
        "q('#allowedCommandOs').value=cmd.os;"
        "q('#allowedCommandProjects').value=cmd.allowedProjectIds.join(', ');"
        "q('#allowedCommandEnabled').checked=cmd.enabled;"
        "q('#allowedCommandFormTitle').textContent='Edit '+cmd.executable;"
        "q('#allowedCommandSubmit').textContent='Save changes';"
        "q('#allowedCommandCancelEdit').hidden=false;"
        "const status=q('#allowedCommandStatus');if(status)status.textContent='';"
        "q('#allowedCommandExecutable').focus();}"
        // Resets the form to add-mode without touching #allowedCommandStatus,
        // since addAllowedCommand() below calls this right after setting a
        // success message that must survive the reset.
        "function cancelEditAllowedCommand(){"
        "allowedCommandEditingId=null;q('#newAllowedCommand').reset();"
        "q('#allowedCommandFormTitle').textContent='Approve a new command';"
        "q('#allowedCommandSubmit').textContent='Approve command';"
        "q('#allowedCommandCancelEdit').hidden=true;}"
        "async function addAllowedCommand(e){e.preventDefault();"
        "const status=q('#allowedCommandStatus');"
        "const executable=q('#allowedCommandExecutable').value.trim();"
        "if(!executable)return;"
        "const allowedProjectIds=q('#allowedCommandProjects').value.split(',')"
        ".map(s=>s.trim()).filter(Boolean);"
        "const payload={executable,"
        "description:q('#allowedCommandDescription').value.trim(),"
        "riskDefault:q('#allowedCommandRisk').value,"
        "os:q('#allowedCommandOs').value,allowedProjectIds,"
        "enabled:q('#allowedCommandEnabled').checked};"
        "const editingId=allowedCommandEditingId;"
        "try{if(editingId){payload.id=editingId;"
        "await api('/api/v1/chat-tools/allowed-commands/update','POST',payload);"
        "if(status)status.textContent='Saved '+executable+'.';}"
        "else{await api('/api/v1/chat-tools/allowed-commands','POST',payload);"
        "if(status)status.textContent='Approved '+executable+'.';}"
        "cancelEditAllowedCommand();"
        "await refreshAllowedCommands();}"
        "catch(x){if(status)status.textContent='';"
        "showSystemError((editingId?'Could not save command: ':"
        "'Could not approve command: ')+x.message);}}"
        "function toggleSourceFields(){const type=q('#downloadSourceType').value;"
        "q('#hfFields').hidden=type!=='huggingface';"
        "q('#githubFields').hidden=type!=='github';"
        "q('#msFields').hidden=type!=='modelscope';}"
        "addEventListener('DOMContentLoaded',()=>{const f=q('#login');"
        "if(f){f.addEventListener('submit',login);initLogin();"
        "q('#setupForm').addEventListener('submit',setupLocalAdmin);}"
        // Every section below is its own page, so only one of these blocks'
        // elements exists on any given load -- each is independently guarded.
        "if(q('#sidebar')){load();"
        "setInterval(pollRunnerStatus,4000);"
        // Sidebar section collapse state persists across the full-page
        // navigations every sidebar click already does (see
        // sidebar_section() in web_ui.cpp): each <details> only overrides
        // its default-open state when localStorage actually has a saved
        // value, and a 'toggle' listener keeps that value current.
        "document.querySelectorAll('.sidebarSection[data-key]').forEach(d=>{"
        "const k='sidebarSection:'+d.dataset.key;const saved=localStorage.getItem(k);"
        "if(saved!==null)d.open=saved==='1';"
        "d.addEventListener('toggle',()=>localStorage.setItem(k,d.open?'1':'0'));});"
        // The sidebar's width and scroll position persist the same way the
        // section collapse state above does: every sidebar click is a
        // full-page navigation, so both are restored from localStorage on
        // each load. The width restore must run before the scroll restore
        // -- reflowing the sidebar after setting scrollTop would shift it.
        "const sb=q('#sidebar');"
        "const savedWidth=parseInt(localStorage.getItem('sidebarWidth'));"
        "if(savedWidth)sb.style.width=savedWidth+'px';"
        "const savedScroll=parseInt(localStorage.getItem('sidebarScroll'));"
        "if(savedScroll)sb.scrollTop=savedScroll;"
        "sb.addEventListener('scroll',()=>"
        "localStorage.setItem('sidebarScroll',Math.round(sb.scrollTop)));"
        // Pointer-capture drag on the gutter between sidebar and content:
        // width is clamped to keep both panes usable, and the final width
        // is saved once on release rather than on every move event.
        "const rz=q('#sidebarResizer');"
        "if(rz)rz.addEventListener('pointerdown',e=>{e.preventDefault();"
        "rz.setPointerCapture(e.pointerId);rz.classList.add('dragging');"
        "const move=ev=>{const w=Math.min(Math.max(ev.clientX,170),600);"
        "sb.style.width=w+'px';};"
        "const up=()=>{rz.classList.remove('dragging');"
        "rz.removeEventListener('pointermove',move);"
        "rz.removeEventListener('pointerup',up);"
        "localStorage.setItem('sidebarWidth',parseInt(sb.style.width));};"
        "rz.addEventListener('pointermove',move);"
        "rz.addEventListener('pointerup',up);});"
        "if(q('#newProject'))q('#newProject').addEventListener('submit',e=>submit(e,'/api/v1/projects',"
        "()=>({id:q('#projectId').value,displayName:q('#projectName').value})));"
        "if(q('#attachToggle'))q('#attachToggle').addEventListener('click',()=>{"
        "const s=q('#actionStatus');"
        "if(!currentProjectId()){s.textContent="
        "'Choose a project first -- attachments are stored against it.';return;}"
        "q('#attachFileInput').click();});"
        "if(q('#attachFileInput'))q('#attachFileInput').addEventListener('change',"
        "async e=>{const files=[...e.target.files];e.target.value='';"
        "for(const file of files)await attachFile(file);});"
        "if(q('#newMessage'))q('#newMessage').addEventListener('submit',streamMessage);"
        "if(q('#chatModel'))q('#chatModel').addEventListener('change',"
        "()=>{changeChatModel();openModelSettingsPanel();});"
        "if(q('#modelSettingsToggle'))q('#modelSettingsToggle')"
        ".addEventListener('click',openModelSettingsPanel);"
        "if(q('#modelSettingsClose'))q('#modelSettingsClose')"
        ".addEventListener('click',()=>{q('#modelSettingsPanel').hidden=true;});"
        "if(q('#modelEffort'))q('#modelEffort')"
        ".addEventListener('change',saveCurrentModelSettings);"
        "if(q('#modelThinking'))q('#modelThinking')"
        ".addEventListener('change',saveCurrentModelSettings);"
        "if(q('#chatToolMode')){updateChatToolModeNote();"
        "q('#chatToolMode').addEventListener('change',changeChatToolMode);}"
        "if(q('#newMemory'))q('#newMemory').addEventListener('submit',addMemory);"
        "if(q('#memoryStatusSection')){refreshMemoryStatus();"
        "setInterval(refreshMemoryStatus,5000);"
        "const startBtn=q('#memCleanStart');"
        "if(startBtn)startBtn.addEventListener('click',async()=>{"
        "startBtn.disabled=true;"
        "const bar=q('#memCleanProgress');const status=q('#memCleanStatus');"
        "if(bar){bar.hidden=false;bar.value=0;}"
        "if(status)status.textContent='Starting...';"
        // Polling has to start BEFORE the POST resolves, not after: the
        // server runs every reclaim step synchronously and only responds
        // once the whole job is finished, so awaiting it first would mean
        // polling only ever began after there was nothing left to observe.
        // setInterval's own initial delay (below) is what makes this safe
        // against reading a stale previous run's finished status -- by the
        // time the first tick fires the POST has always already reached
        // the server and called begin(), which is what actually resets
        // state to 'running' for this job.
        "cleanPollHandle=setInterval(pollCleanProgress,400);"
        "try{await api('/api/v1/system/memory/clean','POST',{"
        "trimCaches:q('#memCleanTrimCaches').checked,"
        "clearScratch:q('#memCleanScratch').checked,"
        "releaseWorkingSet:q('#memCleanWorkingSet').checked,"
        "unloadModel:q('#memCleanUnloadModel').checked,"
        "trimOtherProcessWorkingSets:q('#memCleanTrimOtherProcesses').checked,"
        "trimOtherProcessMinimumMib:"
        "Number(q('#memCleanTrimOtherProcessesMinMib').value)||0,"
        "protectForegroundApplication:q('#memCleanProtectForeground').checked,"
        "flushModifiedPageList:q('#memCleanFlushModifiedList').checked,"
        "purgeStandbyList:q('#memCleanPurgeStandby').checked,"
        "purgeLowPriorityStandbyPages:"
        "q('#memCleanPurgeLowPriorityStandby').checked,"
        "emptySystemAndServiceWorkingSets:"
        "q('#memCleanEmptySystemWorkingSets').checked,"
        "clearSystemFileCache:q('#memCleanClearFileCache').checked});"
        "await pollCleanProgress();}"
        "catch(err){clearInterval(cleanPollHandle);cleanPollHandle=null;"
        "startBtn.disabled=false;showSystemError(err.message);}"
        "});}"
        // Enter sends the message, mirroring every mainstream chat client;
        // Shift+Enter still inserts a newline (the textarea's own default),
        // so multi-line prompts remain possible.
        "if(q('#messageContent'))q('#messageContent').addEventListener('keydown',"
        "e=>{if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();"
        "q('#newMessage').requestSubmit();}});"
        // autoDriveAborted stops the *next* auto-drive turn/approval-resume
        // from ever starting -- generation.abort() alone only stops
        // whichever fetch is in flight right now, which would otherwise
        // still let runTurn()'s own tail call queue up one more turn.
        "if(q('#cancelMessage'))q('#cancelMessage').addEventListener('click',"
        "()=>{autoDriveAborted=true;if(generation)generation.abort();});"
        // Escape stops an in-flight reply (and any queued auto-drive
        // continuation) immediately, mirroring the cancel button --
        // listens on the document (not just the textarea) so it works even
        // while focus is elsewhere on the chat page, but only while a
        // generation is actually running so it doesn't swallow Escape for
        // anything else (closing a picker, blurring a field).
        "if(q('#newMessage'))document.addEventListener('keydown',"
        "e=>{if(e.key==='Escape'&&generation){autoDriveAborted=true;"
        "generation.abort();}});"
        "if(q('#downloadTier')){q('#downloadTier').addEventListener('change',refreshPresets);"
        "q('#downloadPreset').addEventListener('change',applyPreset);"
        "refreshPresets();q('#applyPreset').addEventListener('click',applyPreset);"
        "q('#newDownload').addEventListener('submit',queueDownload);"
        "q('#downloadSourceType').addEventListener('change',"
        "()=>{toggleSourceFields();refreshPresets();});"
        "toggleSourceFields();"
        "q('#applyHfSource').addEventListener('click',applyHfSource);"
        "q('#applyGithubSource').addEventListener('click',applyGithubSource);"
        "q('#applyMsSource').addEventListener('click',applyMsSource);}"
        "if(q('#newUser'))q('#newUser').addEventListener('submit',createUser);"
        "if(q('#newAllowedCommand')){"
        "q('#newAllowedCommand').addEventListener('submit',addAllowedCommand);"
        "q('#allowedCommandCancelEdit').addEventListener('click',()=>{"
        "cancelEditAllowedCommand();"
        "const status=q('#allowedCommandStatus');if(status)status.textContent='';});"
        "q('#allowedCommandOsFilter').addEventListener('change',"
        "()=>renderAllowedCommands(allowedCommandsCache));"
        "refreshAllowedCommands();}"
        "if(q('#systemConfigForm'))q('#systemConfigForm').addEventListener("
        "'submit',submitSystemConfig);"
        "if(q('#cfgHintsEnabled'))q('#cfgHintsEnabled').addEventListener("
        "'change',e=>{localStorage.setItem('mlHintsOff',e.target.checked?"
        "'0':'1');applyHintsPref();});"
        "if(q('#newMlProject'))q('#newMlProject').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/projects',()=>({name:q('#mlProjectName').value,"
        "description:q('#mlProjectDescription').value,"
        "objective:q('#mlProjectObjective').value,"
        "subjectDomain:q('#mlProjectSubjectDomain').value,"
        "modelTask:q('#mlProjectModelTask').value})));"
        // Phase 93: governance save -- PATCHes the section-5 fields onto
        // whichever project is currently loaded in the form (filled by
        // "Configure" above).
        "if(q('#mlProjectGovernanceForm'))"
        "q('#mlProjectGovernanceForm').addEventListener('submit',"
        "async e=>{e.preventDefault();"
        "const id=q('#mlProjectGovernanceId').value;"
        "if(!id){showSystemError('Choose a project first.');return;}"
        "try{await api('/api/v1/ml/projects/'+encodeURIComponent(id)+"
        "'/governance','POST',{"
        "administrators:q('#mlProjectAdministrators').value,"
        "approvedDataSources:q('#mlProjectApprovedDataSources').value,"
        "securityClassification:q('#mlProjectSecurityClassification').value,"
        "targetArchitecture:q('#mlProjectTargetArchitecture').value,"
        "targetDeploymentEnvironment:"
        "q('#mlProjectTargetDeploymentEnvironment').value,"
        "successCriteria:q('#mlProjectSuccessCriteria').value,"
        "evaluationRequirements:q('#mlProjectEvaluationRequirements').value,"
        "safetyRequirements:q('#mlProjectSafetyRequirements').value,"
        "storageAllocationMb:"
        "Number(q('#mlProjectStorageAllocationMb').value)||0,"
        "computeAllocationNotes:"
        "q('#mlProjectComputeAllocationNotes').value});"
        "showFormSuccess('Project governance saved.');await load();}"
        "catch(x){showSystemError('Save project governance failed: '+"
        "x.message);}});"
        // Phase 94: dataset declared-metadata save (sensitive-data status,
        // split percentages) -- fields content upload cannot derive itself.
        "if(q('#mlDatasetDeclareForm'))"
        "q('#mlDatasetDeclareForm').addEventListener('submit',"
        "async e=>{e.preventDefault();"
        "const id=q('#mlDatasetDeclareId').value;"
        "if(!id){showSystemError('Choose a dataset first.');return;}"
        "try{await api('/api/v1/ml/datasets/'+encodeURIComponent(id)+"
        "'/declare','POST',{"
        "sensitiveDataStatus:q('#mlDatasetSensitiveDataStatus').value,"
        "trainSplitPercent:Number(q('#mlDatasetTrainSplitPercent').value)||0,"
        "validationSplitPercent:"
        "Number(q('#mlDatasetValidationSplitPercent').value)||0,"
        "testSplitPercent:Number(q('#mlDatasetTestSplitPercent').value)||0});"
        "showFormSuccess('Dataset metadata saved.');await load();}"
        "catch(x){showSystemError('Save dataset metadata failed: '+"
        "x.message);}});"
        // Phase 95: training job execution-policy save.
        "if(q('#mlTrainingJobPolicyForm'))"
        "q('#mlTrainingJobPolicyForm').addEventListener('submit',"
        "async e=>{e.preventDefault();"
        "const id=q('#mlTrainingJobPolicyId').value;"
        "if(!id){showSystemError('Choose a training job first.');return;}"
        "try{await api('/api/v1/ml/training-jobs/'+encodeURIComponent(id)+"
        "'/execution-policy','POST',{"
        "maxRuntimeSeconds:"
        "Number(q('#mlTrainingJobMaxRuntimeSeconds').value)||0,"
        "failureRecoveryStrategy:"
        "q('#mlTrainingJobFailureRecoveryStrategy').value,"
        "checkpointFrequencyEpochs:"
        "Number(q('#mlTrainingJobCheckpointFrequencyEpochs').value)||0,"
        "outputDirectory:q('#mlTrainingJobOutputDirectory').value,"
        "computeTarget:q('#mlTrainingJobComputeTarget').value,"
        "hardwareAllocation:q('#mlTrainingJobHardwareAllocation').value,"
        "runtimeEnvironment:q('#mlTrainingJobRuntimeEnvironment').value,"
        "containerImage:q('#mlTrainingJobContainerImage').value,"
        "environmentVariables:"
        "q('#mlTrainingJobEnvironmentVariables').value,"
        "secretsReferences:q('#mlTrainingJobSecretsReferences').value,"
        "loggingPolicy:q('#mlTrainingJobLoggingPolicy').value,"
        "notificationPolicy:q('#mlTrainingJobNotificationPolicy').value,"
        "resourceCeilingNotes:"
        "q('#mlTrainingJobResourceCeilingNotes').value,"
        "costCeilingNotes:q('#mlTrainingJobCostCeilingNotes').value});"
        "showFormSuccess('Training job execution policy saved.');"
        "await load();}"
        "catch(x){showSystemError('Save execution policy failed: '+"
        "x.message);}});"
        "if(q('#newMlModel'))q('#newMlModel').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/models',()=>({name:q('#mlModelName').value,"
        "displayName:q('#mlModelDisplayName').value,"
        "version:q('#mlModelVersion').value,"
        "family:q('#mlModelFamily').value,"
        "task:q('#mlModelTask').value,"
        "format:q('#mlModelFormat').value,"
        "quantization:q('#mlModelQuantization').value,"
        "source:q('#mlModelSource').value,"
        "license:q('#mlModelLicense').value})));"
        "if(q('#newMlDataset'))q('#newMlDataset').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/datasets',()=>({name:q('#mlDatasetName').value,"
        "purpose:q('#mlDatasetPurpose').value,"
        "description:q('#mlDatasetDescription').value,"
        "subjectArea:q('#mlDatasetSubjectArea').value,"
        "source:q('#mlDatasetSource').value,"
        "license:q('#mlDatasetLicense').value,"
        "dataFormat:q('#mlDatasetFormat').value})));"
        // Dataset content format completion phase: the upload originally
        // accepted CSV only; the file's extension now also selects JSON (a
        // top-level array of flat objects), JSONL (one flat object per
        // line), or Parquet (base64-encoded the same way the Knowledge
        // ingestion Parquet upload already does -- see that handler just
        // below) -- server.cpp converts every format to CSV before storing,
        // so the returned profile shape is identical either way.
        "if(q('#newMlDatasetContent'))q('#newMlDatasetContent')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlDatasetContentResult');"
        "const file=q('#mlDatasetContentFile').files[0];"
        "if(!file){showSystemError('Choose a dataset file first.');return;}"
        "if(file.size>32*1024*1024){showSystemError('Dataset file exceeds the 32 MiB limit.');return;}"
        "out.textContent='Reading, uploading, and validating '+file.name+'...';"
        "try{const lower=file.name.toLowerCase();"
        "const format=lower.endsWith('.parquet')?'parquet':"
        "lower.endsWith('.jsonl')?'jsonl':lower.endsWith('.json')?'json':'csv';"
        "let content;"
        "if(format==='parquet'){"
        "const bytes=new Uint8Array(await file.arrayBuffer());"
        "let binary='';const chunkSize=0x8000;"
        "for(let i=0;i<bytes.length;i+=chunkSize){"
        "binary+=String.fromCharCode.apply(null,bytes.subarray(i,i+chunkSize));}"
        "content=btoa(binary);}"
        "else{content=new TextDecoder('utf-8',{fatal:true}).decode("
        "await file.arrayBuffer());}"
        "const r=await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(q('#mlDatasetContentId').value.trim())+'/content',"
        "'POST',{content,format,"
        "targetColumn:q('#mlDatasetContentTarget').value.trim()});"
        "out.textContent=r.task==='instruction'?"
        "('Stored '+r.rows+' row(s): instruction column \\''+"
        "r.instructionColumn+'\\', response column \\''+"
        "r.responseColumn+'\\' (fine-tuning text).'):"
        "('Stored '+r.rows+' rows: '+r.featureColumns.length+"
        "' feature column(s) ['+r.featureColumns.join(', ')+'], target \\''+"
        "r.targetColumn+'\\' ('+r.task+"
        "(r.task==='classification'?', classes: '+r.classes.join(', '):'')+"
        "').');showFormSuccess(out.textContent);await load();}"
        "catch(x){out.textContent='';"
        "showSystemError('Upload dataset content failed: '+x.message);}});"
        // Real data augmentation (2026-08-24): calls the new /augment route,
        // which appends genuine synthetic rows (word-level text operations
        // and/or numeric noise/minority-class oversampling, per the checked
        // options) to the dataset's content and reports real before/after
        // row counts -- never a fabricated outcome.
        "if(q('#newMlDatasetAugment'))q('#newMlDatasetAugment')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlDatasetAugmentResult');"
        "out.textContent='Augmenting...';"
        "try{const r=await api('/api/v1/ml/datasets/'+"
        "encodeURIComponent(q('#mlDatasetAugmentId').value.trim())+'/augment',"
        "'POST',{"
        "synonymReplacement:q('#mlDatasetAugmentSynonymReplacement').checked,"
        "randomInsertion:q('#mlDatasetAugmentRandomInsertion').checked,"
        "randomDeletion:q('#mlDatasetAugmentRandomDeletion').checked,"
        "randomSwap:q('#mlDatasetAugmentRandomSwap').checked,"
        "textAugmentationFraction:"
        "Number(q('#mlDatasetAugmentTextFraction').value)||0,"
        "gaussianNoise:q('#mlDatasetAugmentGaussianNoise').checked,"
        "noiseStddevFraction:"
        "Number(q('#mlDatasetAugmentNoiseFraction').value)||0,"
        "oversampleMinorityClasses:"
        "q('#mlDatasetAugmentOversampleMinority').checked,"
        "targetMinorityRatio:"
        "Number(q('#mlDatasetAugmentTargetRatio').value)||0});"
        "out.textContent=r.rowsBefore+' row(s) before, '+r.rowsAfter+"
        "' after ('+r.syntheticRowsAdded+' synthetic row(s) added).';"
        "showFormSuccess(out.textContent);await load();}"
        "catch(x){out.textContent='';"
        "showSystemError('Augment dataset failed: '+x.message);}});"
        // Phase 56: live prediction form -- parses the feature JSON locally
        // for a clear error, then calls the real prediction endpoint.
        "if(q('#mlPredictForm'))q('#mlPredictForm')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlPredictResult');"
        "let features;"
        "try{features=JSON.parse(q('#mlPredictFeatures').value);}"
        "catch(x){showSystemError('Feature values must be a valid JSON "
        "object.');return;}"
        "out.textContent='Predicting...';"
        "try{const r=await api('/api/v1/ml/models/'+"
        "encodeURIComponent(q('#mlPredictModelId').value.trim())+'/predict',"
        "'POST',{features});"
        "if(r.task==='classification'){"
        "out.textContent='Predicted class: '+r.label+' ('+r.classes.map("
        "(c,i)=>c+' '+(100*r.probabilities[i]).toFixed(1)+'%').join(', ')+')';}"
        "else{out.textContent='Predicted value: '+"
        "Number(r.value).toPrecision(6);}"
        "showFormSuccess(out.textContent);}"
        "catch(x){out.textContent='';"
        "showSystemError('Prediction failed: '+x.message);}});"
        "if(q('#newMlSubject'))q('#newMlSubject').addEventListener('submit',"
        "e=>submit(e,'/api/v1/ml/subjects',()=>({name:q('#mlSubjectName').value,"
        "description:q('#mlSubjectDescription').value,"
        "scope:q('#mlSubjectScope').value,"
        "targetAudience:q('#mlSubjectTargetAudience').value})));"
        "if(q('#newMlKnowledgeDocument'))q('#newMlKnowledgeDocument')"
        ".addEventListener('submit',async e=>{e.preventDefault();"
        "const file=q('#mlKnowledgeFile').files[0];"
        "if(!file){showSystemError('Choose a knowledge file first.');return;}"
        "const out=q('#mlKnowledgeUploadResult');"
        "out.textContent='Reading and indexing '+file.name+'...';"
        "try{const lower=file.name.toLowerCase();"
        "const isParquet=lower.endsWith('.parquet');"
        "let mediaType=file.type;let content;"
        "if(isParquet){mediaType='application/vnd.apache.parquet';"
        "const bytes=new Uint8Array(await file.arrayBuffer());"
        "let binary='';const chunkSize=0x8000;"
        "for(let i=0;i<bytes.length;i+=chunkSize){"
        "binary+=String.fromCharCode.apply(null,bytes.subarray(i,i+chunkSize));}"
        "content=btoa(binary);}"
        "else{content=new TextDecoder('utf-8',{fatal:true}).decode("
        "await file.arrayBuffer());"
        "if(!mediaType){mediaType=lower.endsWith('.md')?"
        "'text/markdown':lower.endsWith('.csv')?'text/csv':"
        "lower.endsWith('.json')?'application/json':"
        "lower.endsWith('.jsonl')?'application/x-ndjson':'text/plain';}}"
        "const r=await api('/api/v1/ml/knowledge-documents','POST',{"
        "subjectId:q('#mlKnowledgeSubjectId').value,"
        "vectorStoreId:q('#mlKnowledgeVectorStoreId').value,"
        "fileName:file.name,mediaType,content});"
        "out.textContent='Indexed '+r.fileName+': '+r.chunkCount+' chunk(s), SHA-256 '+r.sha256+'.';"
        "showFormSuccess(out.textContent);await load();}catch(x){out.textContent='';"
        "showSystemError('Knowledge ingestion failed: '+x.message);}});"
        "if(q('#newMlLabelTask'))q('#newMlLabelTask').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/label-tasks',"
        "()=>({datasetId:q('#mlLabelTaskDatasetId').value,"
        "name:q('#mlLabelTaskName').value,"
        "description:q('#mlLabelTaskDescription').value,"
        "labelMode:q('#mlLabelTaskLabelMode').value,"
        "assigneeId:q('#mlLabelTaskAssigneeId').value})));"
        "if(q('#newMlPrepJob'))q('#newMlPrepJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/prep-jobs',"
        "()=>({datasetId:q('#mlPrepJobDatasetId').value,"
        "name:q('#mlPrepJobName').value,"
        "description:q('#mlPrepJobDescription').value,"
        "operation:q('#mlPrepJobOperation').value})));"
        "if(q('#newMlTrainingJob'))q('#newMlTrainingJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/training-jobs',"
        "()=>({projectId:q('#mlTrainingJobProjectId').value,"
        "modelId:q('#mlTrainingJobModelId').value,"
        "datasetId:q('#mlTrainingJobDatasetId').value,"
        "name:q('#mlTrainingJobName').value,"
        "description:q('#mlTrainingJobDescription').value,"
        "trainingType:q('#mlTrainingJobTrainingType').value})));"
        "if(q('#newMlEvaluationRun'))q('#newMlEvaluationRun').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/evaluation-runs',"
        "()=>({modelId:q('#mlEvaluationRunModelId').value,"
        "datasetId:q('#mlEvaluationRunDatasetId').value,"
        "name:q('#mlEvaluationRunName').value,"
        "description:q('#mlEvaluationRunDescription').value,"
        "category:q('#mlEvaluationRunCategory').value,"
        "sensitiveFeatureName:"
        "q('#mlEvaluationRunSensitiveFeatureName').value})));"
        "if(q('#newMlExperiment'))q('#newMlExperiment').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/experiments',"
        "()=>({projectId:q('#mlExperimentProjectId').value,"
        "modelId:q('#mlExperimentModelId').value,"
        "datasetId:q('#mlExperimentDatasetId').value,"
        "name:q('#mlExperimentName').value,"
        "description:q('#mlExperimentDescription').value,"
        "hyperparametersJson:q('#mlExperimentHyperparameters').value,"
        "randomSeed:Number(q('#mlExperimentRandomSeed').value||0),"
        "sourceCodeVersion:q('#mlExperimentSourceCodeVersion').value,"
        "configurationVersion:q('#mlExperimentConfigurationVersion').value,"
        "containerVersion:q('#mlExperimentContainerVersion').value,"
        "tags:q('#mlExperimentTags').value,"
        "notes:q('#mlExperimentNotes').value})));"
        "if(q('#compareMlExperiments'))q('#compareMlExperiments')."
        "addEventListener('submit',async e=>{e.preventDefault();"
        "const out=q('#mlExperimentCompareResult');"
        // ML forms clarity pass: baseline is its own required <select> (so
        // "baseline first" is a structural guarantee, not something an
        // administrator has to remember to type in the right comma
        // position), and every checked candidate follows it in whatever
        // order fillMultiSelect listed them in -- comparisons run against
        // the baseline regardless of candidate order, so only the
        // baseline's own position in this array actually matters.
        "const baselineId=q('#mlExperimentCompareBaselineId').value;"
        "const candidateIds=multiSelectValues('mlExperimentCompareCandidateIds')"
        ".split(',').map(s=>s.trim()).filter(s=>s&&s!==baselineId);"
        "const ids=baselineId?[baselineId,...candidateIds]:candidateIds;"
        "try{const r=await api('/api/v1/ml/experiments/compare','POST',"
        "{experimentIds:ids});"
        "if(out)out.innerHTML=table(['Experiment','Status','Has result',"
        "'Primary metric','Value'],r.experiments.map(x=>[esc(x.name),"
        "esc(x.status),x.hasResult?'yes':'no',esc(x.primaryMetric||''),"
        "x.hasResult?Number(x.primaryValue).toPrecision(4):'']))+"
        "'<h3>Comparisons vs. baseline</h3>'+table(['Experiment ID',"
        "'Metric delta','Regression','Runtime delta (ms)'],"
        "r.comparisons.map(c=>[escTrim(c.experimentId,16),"
        "c.metricDelta!==undefined?Number(c.metricDelta).toPrecision(4):"
        "(c.metricComparison||''),c.regression?'yes':'no',"
        "c.runtimeDeltaMilliseconds!==undefined?"
        "String(c.runtimeDeltaMilliseconds):'']));"
        "showFormSuccess('Compared '+r.experiments.length+' experiment(s) "
        "against the baseline.');}"
        "catch(x){showSystemError('Compare experiments failed: '+"
        "x.message);}});"
        "if(q('#newMlFineTuningJob'))q('#newMlFineTuningJob').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/fine-tuning-jobs',"
        "()=>({modelId:q('#mlFineTuningJobModelId').value,"
        "datasetId:q('#mlFineTuningJobDatasetId').value,"
        "projectId:q('#mlFineTuningJobProjectId').value,"
        "name:q('#mlFineTuningJobName').value,"
        "description:q('#mlFineTuningJobDescription').value,"
        "method:q('#mlFineTuningJobMethod').value})));"
        "if(q('#newMlModelBuilderConfig'))"
        "q('#newMlModelBuilderConfig').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-builder-configs',"
        "()=>({sourceType:q('#mlModelBuilderConfigSourceType').value,"
        "baseModelId:q('#mlModelBuilderConfigBaseModelId').value,"
        "projectId:q('#mlModelBuilderConfigProjectId').value,"
        "name:q('#mlModelBuilderConfigName').value,"
        "description:q('#mlModelBuilderConfigDescription').value})));"
        // Section 9's basic/advanced configuration modes: the mode select
        // toggles the advanced field group's visibility, and Save always
        // posts every field (basic mode keeps the advanced values the
        // Configure button loaded, so hiding them loses nothing).
        "if(q('#configureMlModelBuilderConfig')){"
        "q('#mlMbcMode').addEventListener('change',()=>{"
        "q('#mlMbcAdvanced').style.display="
        "q('#mlMbcMode').value==='advanced'?'':'none';});"
        "q('#configureMlModelBuilderConfig').addEventListener('submit',e=>{"
        "if(!mlModelBuilderConfigureTarget){e.preventDefault();"
        "showSystemError('Press Configure on a configuration in the list "
        "first to choose which one to edit.');return;}"
        "submit(e,'/api/v1/ml/model-builder-configs/'+"
        "encodeURIComponent(mlModelBuilderConfigureTarget)+'/configure',"
        "()=>({configurationMode:q('#mlMbcMode').value,"
        "architecture:q('#mlMbcArchitecture').value,"
        "layerConfiguration:q('#mlMbcLayerConfiguration').value,"
        "hiddenDimensions:Number(q('#mlMbcHiddenDimensions').value)||0,"
        "attentionConfiguration:q('#mlMbcAttentionConfiguration').value,"
        "vocabularyTokenizer:q('#mlMbcVocabularyTokenizer').value,"
        "sequenceLength:Number(q('#mlMbcSequenceLength').value)||0,"
        "activationFunctions:q('#mlMbcActivationFunctions').value,"
        "dropout:Number(q('#mlMbcDropout').value)||0,"
        "initialisationStrategy:q('#mlMbcInitialisationStrategy').value,"
        "lossFunction:q('#mlMbcLossFunction').value,"
        "optimiser:q('#mlMbcOptimiser').value,"
        "learningRateScheduler:q('#mlMbcLearningRateScheduler').value,"
        "batchSize:Number(q('#mlMbcBatchSize').value)||0,"
        "epochCount:Number(q('#mlMbcEpochCount').value)||0,"
        "gradientAccumulation:"
        "Number(q('#mlMbcGradientAccumulation').value)||0,"
        "gradientClipping:Number(q('#mlMbcGradientClipping').value)||0,"
        "l1Regularization:"
        "Number(q('#mlMbcL1Regularization').value)||0,"
        "l2Regularization:"
        "Number(q('#mlMbcL2Regularization').value)||0,"
        "mixedPrecision:q('#mlMbcMixedPrecision').checked,"
        "checkpointFrequency:"
        "Number(q('#mlMbcCheckpointFrequency').value)||0,"
        "validationFrequency:"
        "Number(q('#mlMbcValidationFrequency').value)||0,"
        "earlyStopping:q('#mlMbcEarlyStopping').checked,"
        "randomSeed:Number(q('#mlMbcRandomSeed').value)||0,"
        "reproducibilitySettings:"
        "q('#mlMbcReproducibilitySettings').value,"
        "distributedTrainingSettings:"
        "q('#mlMbcDistributedTrainingSettings').value}));});}"
        "if(q('#newMlInstructionExample'))"
        "q('#newMlInstructionExample').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/instruction-examples',"
        "()=>({datasetId:q('#mlInstructionExampleDatasetId').value,"
        "name:q('#mlInstructionExampleName').value,"
        "description:q('#mlInstructionExampleDescription').value,"
        "subjectClassification:"
        "q('#mlInstructionExampleSubjectClassification').value})));"
        // Continual Learning (2026-08-24): calls the real collection
        // executor and shows its real, honest accounting (scanned/
        // considered/created/rejected counts) -- then the new drafts are
        // visible in the Instruction examples table above like any other,
        // ready for the normal review flow.
        "if(q('#newMlContinualLearningCollect'))"
        "q('#newMlContinualLearningCollect').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const out=q('#mlContinualLearningResult');"
        "out.textContent='Scanning conversations...';"
        "try{const r=await api('/api/v1/ml/continual-learning/collect',"
        "'POST',{targetDatasetId:q('#mlContinualLearningDatasetId').value,"
        "qualityFloor:Number(q('#mlContinualLearningQualityFloor').value)||0.4});"
        "out.textContent='Scanned '+r.chatsScanned+' chat(s), '+"
        "r.turnsConsidered+' turn(s) considered: '+r.candidatesCreated+"
        "' draft(s) created, '+r.rejectedLowQuality+' rejected (low "
        "quality), '+r.rejectedUnsafe+' rejected (safety scan), '+"
        "r.piiRedactionsApplied+' PII redaction(s) applied. Review the new "
        "draft(s) in the table above.';"
        "showFormSuccess(out.textContent);await load();}"
        "catch(x){out.textContent='';"
        "showSystemError('Collect candidates failed: '+x.message);}});"
        // Phase 81: content save -- upserts the real record body for the
        // example id currently in the form (filled by "Configure" above,
        // or pasted directly).
        "if(q('#mlInstructionExampleContentForm'))"
        "q('#mlInstructionExampleContentForm').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const id=q('#mlInstructionExampleContentId').value;"
        "const status=q('#mlInstructionExampleContentStatus');"
        "try{await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(id)+'/content','POST',{"
        "systemInstruction:q('#mlInstructionExampleSystemInstruction').value,"
        "userInstruction:q('#mlInstructionExampleUserInstruction').value,"
        "context:q('#mlInstructionExampleContext').value,"
        "expectedResponse:q('#mlInstructionExampleExpectedResponse').value,"
        "rejectedResponse:q('#mlInstructionExampleRejectedResponse').value,"
        "toolCallsJson:q('#mlInstructionExampleToolCallsJson').value,"
        "toolResultsJson:q('#mlInstructionExampleToolResultsJson').value,"
        "requiredOutputFormat:"
        "q('#mlInstructionExampleRequiredOutputFormat').value,"
        "difficulty:q('#mlInstructionExampleDifficulty').value,"
        "safetyClassification:"
        "q('#mlInstructionExampleSafetyClassification').value});"
        "if(status)status.textContent='Content saved.';"
        "showFormSuccess('Instruction example content saved.');}"
        "catch(x){showSystemError('Save instruction example content "
        "failed: '+x.message);}});"
        // Phase 81: generate -- calls execute_rag_generation for real and
        // reloads the list so the new draft example appears.
        "if(q('#mlInstructionExampleGenerateForm'))"
        "q('#mlInstructionExampleGenerateForm').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const status=q('#mlInstructionExampleGenerateStatus');"
        "if(status)status.textContent='Generating...';"
        "try{const r=await api('/api/v1/ml/instruction-examples/generate',"
        "'POST',{datasetId:q('#mlInstructionExampleGenerateDatasetId').value,"
        "modelId:q('#mlInstructionExampleGenerateModelId').value,"
        "name:q('#mlInstructionExampleGenerateName').value,"
        "systemInstruction:"
        "q('#mlInstructionExampleGenerateSystemInstruction').value,"
        "userInstruction:"
        "q('#mlInstructionExampleGenerateUserInstruction').value,"
        "context:q('#mlInstructionExampleGenerateContext').value});"
        "if(status)status.textContent='Generated draft \"'+r.example.name+"
        "'\" (status: draft, needs review before approval).';"
        "showFormSuccess(status?status.textContent:"
        "'Generated draft \"'+r.example.name+'\".');"
        "await load();}"
        "catch(x){if(status)status.textContent='';"
        "showSystemError('Generate instruction example failed: '+"
        "x.message);}});"
        // Phase 81: test against models -- a real fan-out probe, not saved.
        "if(q('#mlInstructionExampleTestForm'))"
        "q('#mlInstructionExampleTestForm').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const out=q('#mlInstructionExampleTestResult');"
        "if(out)out.textContent='Testing...';"
        "const modelIds=multiSelectValues('mlInstructionExampleTestModelIds')."
        "split(',').map(s=>s.trim()).filter(Boolean);"
        "try{const r=await api('/api/v1/ml/instruction-examples/'+"
        "encodeURIComponent(q('#mlInstructionExampleTestId').value)+"
        "'/test','POST',{modelIds});"
        "if(out)out.innerHTML=table(['Model','Response','Elapsed (\\u00b5s)'],"
        "r.results.map(x=>[escTrim(x.modelId,16),"
        "esc(x.error?('error: '+x.error):x.response),"
        "x.elapsedMicroseconds!==undefined?String(x.elapsedMicroseconds):''"
        "]));"
        "showFormSuccess('Tested against '+r.results.length+' model(s).');}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Test instruction example failed: '+x.message);}});"
        // Phase 81: duplicate/contradiction detection over one dataset.
        "if(q('#mlInstructionExampleCheckForm'))"
        "q('#mlInstructionExampleCheckForm').addEventListener("
        "'click',async e=>{"
        "const kind=e.target&&e.target.dataset&&e.target.dataset.check;"
        "if(!kind)return;e.preventDefault();"
        "const out=q('#mlInstructionExampleCheckResult');"
        "if(out)out.textContent='Checking...';"
        "try{const r=await api('/api/v1/ml/instruction-examples/'+kind,"
        "'POST',{datasetId:q('#mlInstructionExampleCheckDatasetId').value});"
        "const pairs=r[kind]||[];"
        "if(out)out.innerHTML=pairs.length?table(['First ID','Second ID'],"
        "pairs.map(p=>[escTrim(p.firstId,20),escTrim(p.secondId,20)])):"
        "'<p>No '+kind+' found.</p>';"
        "showFormSuccess('Checked for '+kind+': '+pairs.length+' found.');}"
        "catch(x){if(out)out.textContent='';"
        "showSystemError('Check '+kind+' failed: '+x.message);}});"
        "if(q('#newMlSyntheticRecord'))"
        "q('#newMlSyntheticRecord').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/synthetic-records',"
        "()=>({datasetId:q('#mlSyntheticRecordDatasetId').value,"
        "name:q('#mlSyntheticRecordName').value,"
        "description:q('#mlSyntheticRecordDescription').value,"
        "generationTechnique:"
        "q('#mlSyntheticRecordGenerationTechnique').value})));"
        // Deployment Manager/Inference Endpoints/Synthetic Data completion
        // phase: generate -- calls execute_rag_generation for real via
        // POST .../synthetic-records/generate and reloads the list so the
        // new draft record appears.
        "if(q('#mlSyntheticRecordGenerateForm'))"
        "q('#mlSyntheticRecordGenerateForm').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const status=q('#mlSyntheticRecordGenerateStatus');"
        "if(status)status.textContent='Generating...';"
        "try{const r=await api('/api/v1/ml/synthetic-records/generate',"
        "'POST',{datasetId:q('#mlSyntheticRecordGenerateDatasetId').value,"
        "modelId:q('#mlSyntheticRecordGenerateModelId').value,"
        "name:q('#mlSyntheticRecordGenerateName').value,"
        "description:q('#mlSyntheticRecordGenerateDescription').value,"
        "generationTechnique:"
        "q('#mlSyntheticRecordGenerateTechnique').value,"
        "sourceText:q('#mlSyntheticRecordGenerateSourceText').value});"
        "if(status)status.textContent='Generated \"'+r.record.name+"
        "'\" (status: draft, needs review before approval). Output: '+"
        "r.content.generatedText;"
        "showFormSuccess('Generated draft \"'+r.record.name+'\".');"
        "await load();}"
        "catch(x){if(status)status.textContent='';"
        "showSystemError('Generate synthetic record failed: '+"
        "x.message);}});"
        "if(q('#newMlVectorStore'))"
        "q('#newMlVectorStore').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/vector-stores',"
        "()=>({name:q('#mlVectorStoreName').value,"
        "description:q('#mlVectorStoreDescription').value,"
        "embeddingModel:q('#mlVectorStoreEmbeddingModel').value,"
        "distanceMetric:q('#mlVectorStoreDistanceMetric').value})));"
        "if(q('#newMlRagConfig'))"
        "q('#newMlRagConfig').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/rag-configs',"
        "()=>({name:q('#mlRagConfigName').value,"
        "description:q('#mlRagConfigDescription').value,"
        "searchStrategy:q('#mlRagConfigSearchStrategy').value,"
        "vectorStoreId:q('#mlRagConfigVectorStoreId').value})));"
        "if(q('#mlRagQueryForm'))q('#mlRagQueryForm').addEventListener("
        "'submit',async e=>{e.preventDefault();const out=q('#mlRagQueryResult');"
        "out.textContent='Retrieving indexed evidence...';try{const r=await api("
        "'/api/v1/ml/rag-configs/'+encodeURIComponent("
        "q('#mlRagQueryConfigId').value)+'/query','POST',{"
        "query:q('#mlRagQueryText').value,topK:Number(q('#mlRagQueryTopK').value)});"
        "const result=r.result;out.textContent='Retrieved '+result.retrievedCount+"
        "' chunk(s) with '+result.searchStrategy+' search.\\n\\n'+"
        "result.chunks.map((x,i)=>(i+1)+'. '+x.citation+' score '+"
        "Number(x.score).toFixed(4)+'\\n'+x.text).join('\\n\\n')+"
        "'\\n\\nContext package:\\n'+result.context;"
        "showFormSuccess('Retrieved '+result.retrievedCount+' chunk(s) with '+"
        "result.searchStrategy+' search.');}catch(x){out.textContent='';"
        "showSystemError('RAG retrieval failed: '+x.message);}});"
        "if(q('#newMlSubjectExam'))"
        "q('#newMlSubjectExam').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/subject-exams',"
        "()=>({subjectId:q('#mlSubjectExamSubjectId').value,"
        "name:q('#mlSubjectExamName').value,"
        "description:q('#mlSubjectExamDescription').value,"
        "questionFormat:q('#mlSubjectExamQuestionFormat').value})));"
        "if(q('#newMlHyperparameterSearch'))"
        "q('#newMlHyperparameterSearch').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/hyperparameter-searches',"
        "()=>({trainingJobId:q('#mlHyperparameterSearchTrainingJobId').value,"
        "name:q('#mlHyperparameterSearchName').value,"
        "description:q('#mlHyperparameterSearchDescription').value,"
        "strategy:q('#mlHyperparameterSearchStrategy').value})));"
        "if(q('#newMlEnsemble'))"
        "q('#newMlEnsemble').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/ensembles',"
        "()=>({trainingJobId:q('#mlEnsembleTrainingJobId').value,"
        "name:q('#mlEnsembleName').value,"
        "description:q('#mlEnsembleDescription').value,"
        "method:q('#mlEnsembleMethod').value,"
        "memberCount:Number(q('#mlEnsembleMemberCount').value)||5})));"
        "if(q('#newMlModelOptimization'))"
        "q('#newMlModelOptimization').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-optimizations',"
        "()=>({modelId:q('#mlModelOptimizationModelId').value,"
        "name:q('#mlModelOptimizationName').value,"
        "description:q('#mlModelOptimizationDescription').value,"
        "operation:q('#mlModelOptimizationOperation').value})));"
        "if(q('#newMlCheckpoint'))"
        "q('#newMlCheckpoint').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/checkpoints',"
        "()=>({trainingJobId:q('#mlCheckpointTrainingJobId').value,"
        "name:q('#mlCheckpointName').value,"
        "description:q('#mlCheckpointDescription').value,"
        "captureReason:q('#mlCheckpointCaptureReason').value})));"
        "if(q('#newMlDeployment'))"
        "q('#newMlDeployment').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/deployments',"
        "()=>({modelId:q('#mlDeploymentModelId').value,"
        "name:q('#mlDeploymentName').value,"
        "description:q('#mlDeploymentDescription').value,"
        "environment:q('#mlDeploymentEnvironment').value,"
        "strategy:q('#mlDeploymentStrategy').value})));"
        "if(q('#newMlModelComparison'))"
        "q('#newMlModelComparison').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-comparisons',"
        "()=>({baselineModelId:q('#mlModelComparisonBaselineModelId').value,"
        "candidateModelId:q('#mlModelComparisonCandidateModelId').value,"
        "datasetId:q('#mlModelComparisonDatasetId').value,"
        "name:q('#mlModelComparisonName').value,"
        "description:q('#mlModelComparisonDescription').value})));"
        "if(q('#newMlInferenceEndpoint'))"
        "q('#newMlInferenceEndpoint').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/inference-endpoints',"
        "()=>({modelId:q('#mlEndpointModelId').value,"
        "name:q('#mlEndpointName').value,"
        "runtime:q('#mlEndpointRuntime').value,"
        "host:q('#mlEndpointHost').value,"
        "port:Number(q('#mlEndpointPort').value)||0,"
        "protocol:q('#mlEndpointProtocol').value,"
        "authenticationMethod:q('#mlEndpointAuthenticationMethod').value,"
        "authToken:q('#mlEndpointAuthToken').value,"
        "rateLimitPerMinute:Number(q('#mlEndpointRateLimit').value)||0})));"
        // Deployment Manager/Inference Endpoints/Synthetic Data completion
        // phase: exposes the previously API-only POST .../{id}/policy
        // route in the UI.
        "if(q('#mlEndpointPolicyForm'))"
        "q('#mlEndpointPolicyForm').addEventListener("
        "'submit',async e=>{e.preventDefault();"
        "const status=q('#mlEndpointPolicyStatus');"
        "if(status)status.textContent='Saving...';"
        "try{await api('/api/v1/ml/inference-endpoints/'+"
        "encodeURIComponent(q('#mlEndpointPolicyId').value)+'/policy',"
        "'POST',{contentScanEnabled:"
        "q('#mlEndpointPolicyContentScanEnabled').checked,"
        "blockOnScanFinding:q('#mlEndpointPolicyBlockOnScanFinding').checked,"
        "blockAnswerOnScanFinding:"
        "q('#mlEndpointPolicyBlockAnswerOnScanFinding').checked,"
        "safetyPolicyId:q('#mlEndpointPolicySafetyPolicyId').value,"
        "modelClassifierEnabled:"
        "q('#mlEndpointPolicyModelClassifierEnabled').checked,"
        "modelClassifierConfidenceFloor:"
        "Number(q('#mlEndpointPolicyConfidenceFloor').value)||0.5});"
        "if(status)status.textContent='Policy saved. Takes effect on the "
        "endpoint\\'s next request, no restart required.';"
        "showFormSuccess('Endpoint policy saved. Takes effect on the "
        "endpoint\\'s next request, no restart required.');}"
        "catch(x){if(status)status.textContent='';"
        "showSystemError('Save endpoint policy failed: '+x.message);}});"
        "if(q('#newMlComputeNode'))"
        "q('#newMlComputeNode').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/compute-nodes',"
        "()=>({name:q('#mlComputeNodeName').value,"
        "address:q('#mlComputeNodeAddress').value,"
        "operatingSystem:q('#mlComputeNodeOperatingSystem').value,"
        "cpuDescription:q('#mlComputeNodeCpuDescription').value,"
        "gpuDescription:q('#mlComputeNodeGpuDescription').value,"
        "memoryMib:Number(q('#mlComputeNodeMemoryMib').value)||0,"
        "isLocal:q('#mlComputeNodeIsLocal').checked})));"
        "if(q('#newMlAutomationPipeline'))"
        "q('#newMlAutomationPipeline').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/automation-pipelines',"
        "()=>({name:q('#mlPipelineName').value,"
        "projectId:q('#mlPipelineProjectId').value,"
        "description:q('#mlPipelineDescription').value,"
        // Stages run in the fixed order the checkboxes are listed in (see
        // that field's hint), not click order -- querySelectorAll already
        // returns them in document order, so no extra sorting is needed.
        "stages:Array.prototype.slice.call(document.querySelectorAll("
        "'.mlPipelineStage:checked')).map(i=>i.value).join(','),"
        "datasetId:q('#mlPipelineDatasetId').value,"
        "modelId:q('#mlPipelineModelId').value})));"
        "if(q('#newMlSafetyPolicy'))"
        "q('#newMlSafetyPolicy').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/safety-policies',"
        "()=>({name:q('#mlSafetyPolicyName').value,"
        "scope:q('#mlSafetyPolicyScope').value,"
        "restrictedDataCategories:"
        "q('#mlSafetyPolicyRestrictedDataCategories').value})));"
        "if(q('#newMlModelCard'))"
        "q('#newMlModelCard').addEventListener("
        "'submit',e=>submit(e,'/api/v1/ml/model-cards',"
        "()=>({modelId:q('#mlModelCardModelId').value,"
        "purpose:q('#mlModelCardPurpose').value,"
        "intendedUse:q('#mlModelCardIntendedUse').value,"
        "prohibitedUse:q('#mlModelCardProhibitedUse').value,"
        "trainingDataReference:"
        "q('#mlModelCardTrainingDataReference').value,"
        "evaluationResults:q('#mlModelCardEvaluationResults').value,"
        "knownLimitations:q('#mlModelCardKnownLimitations').value,"
        "license:q('#mlModelCardLicense').value})));}});";
}

// Presents the native OS account sign-in form without embedding credentials.
std::string login_page() {
    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Login</title><style>" DARK_THEME_CSS
        "body{max-width:34rem;margin:8vh auto}"
        "h1{font-size:2.1rem}p{color:var(--muted)}</style></head>"
        "<body><h1>MasterAI</h1>"
        // Shown only before the first administrator exists (toggled by JS
        // after reading /health/ready's setupRequired flag). Only present
        // when the server was configured with allowLocalPasswordAccounts;
        // otherwise the server rejects POST /api/v1/setup/local anyway.
        "<section id=\"setupSection\" hidden><h2>First-time setup</h2>"
        "<p>Create the local administrator account. The one-time setup token "
        "was printed to the server's log/console when it started.</p>"
        "<form id=\"setupForm\"><label for=\"setupToken\">Setup token</label>"
        "<input id=\"setupToken\" required>"
        "<label for=\"setupUsername\">Username</label>"
        "<input id=\"setupUsername\" required pattern=\"[A-Za-z0-9_.-]+\">"
        "<label for=\"setupDisplay\">Display name</label>"
        "<input id=\"setupDisplay\" required>"
        "<label for=\"setupPassword\">Password (minimum 8 characters)</label>"
        "<input id=\"setupPassword\" type=\"password\" minlength=\"8\" required>"
        "<button title=\"Create administrator\">" ICON_PLUS_SVG " Create administrator</button></form></section>"
        "<section id=\"loginSection\"><p>Sign in with your MasterAI or "
        "operating-system account.</p><form id=\"login\"><label for=\"username\">"
        "User name</label><input id=\"username\" autocomplete=\"username\" required>"
        "<label for=\"password\">Password</label><input id=\"password\" type=\"password\" "
        "autocomplete=\"current-password\" required><button>Sign in</button></form>"
        "</section>"
        "<p id=\"status\" role=\"status\"></p><script src=\"/assets/app.js\"></script>"
        "</body></html>");
}

// Renders one sidebar link. href is a real URL (a normal page navigation),
// never a client-side hash -- "active" is decided server-side from the
// section actually being rendered on this response.
std::string nav_link(const std::string& href, const std::string& label,
                     bool active) {
    return "<a class=\"navButton" + std::string(active ? " active" : "") +
           "\" href=\"" + href + "\">" + label + "</a>";
}

// ML forms clarity pass: a "?" badge placed immediately after a <label>'s
// own visible text, revealing `text` in a floating bubble on hover/focus --
// see the .mlHint/.mlHintBubble CSS and body.hintsOff's client-side
// toggle (applyHintsPref() in application_script()) for how a user turns
// this off from Machine Learning Settings. `text` is always a literal
// string this function's own callers author (never live request/API data),
// so it is written to avoid '<', '>', and '&' rather than carrying a full
// HTML-escaper for content that is never actually untrusted.
std::string field_hint(const std::string& text) {
    return "<span class=\"mlHint\" tabindex=\"0\">?"
           "<span class=\"mlHintBubble\">" + text + "</span></span>";
}

// ML forms clarity pass: a numbered "1, 2, 3, ..." banner placed once at
// the top of a panel that is one (or, for Dataset Manager, two adjacent)
// stage(s) of a real multi-panel pipeline (Dataset -> Training Job ->
// Evaluation, for example), so a user without an ML background sees where
// the panel they landed on fits before reading a single form field.
// `active_from`/`active_to` (1-based, inclusive) are the step(s) this
// panel represents -- equal for every panel except Dataset Manager, which
// hosts both "register" and "upload content" and highlights both rather
// than forcing two separate banners (one of which would otherwise sit in
// a form column too narrow to lay out five boxes, which is exactly the
// squeezed, broken-looking duplicate this replaced). Every other step
// just shows its title/one-line description as context for what comes
// before/after. This intentionally does not track live completion
// (whether step 1 has actually been done yet) -- that would need
// per-panel data the banner has no access to at server-render time -- it
// only orients the user in the sequence. Rendered as a CSS grid (see
// .mlStepFlow) that reflows its own row count rather than a fixed-width
// row, so it never needs to be placed more than once per panel to stay
// readable at any browser size. `steps` entries are always literal
// strings this function's own callers author, matching field_hint's own
// escaping rule above.
std::string ml_step_flow(
    const int active_from, const int active_to,
    const std::vector<std::pair<std::string, std::string>>& steps) {
    std::string html = "<div class=\"mlStepFlow\">";
    for (std::size_t i = 0; i < steps.size(); ++i) {
        const int step_number = static_cast<int>(i) + 1;
        const bool active = step_number >= active_from && step_number <= active_to;
        html += "<div class=\"mlStepBox";
        if (active) html += " mlStepActive";
        html += "\"><span class=\"mlStepNum\">" + std::to_string(step_number) +
                "</span><div><strong>" + steps[i].first + "</strong>";
        if (!steps[i].second.empty()) {
            html += "<span class=\"mlStepDesc\">" + steps[i].second + "</span>";
        }
        html += "</div></div>";
    }
    return html + "</div>";
}
std::string ml_step_flow(
    const int active_index,
    const std::vector<std::pair<std::string, std::string>>& steps) {
    return ml_step_flow(active_index, active_index, steps);
}

// Machine Learning Dashboard consolidation pass: the same five-box grid
// ml_step_flow() renders on each individual pipeline page, but every box on
// the Dashboard is a real link (a plain full-page navigation, matching
// nav_link()'s own convention -- no client-side router involved) to that
// step's own page, so landing on the Dashboard first teaches the whole path
// before the user picks a page to start on. `hrefs` is a parallel array to
// `steps`, one URL per step in the same order; a step whose page also hosts
// the next step (Dataset Manager hosts both "register" and "upload
// content") simply repeats that URL, exactly as kMlPipelineSteps' own two
// dataset entries do. No step is rendered "active" here -- there is no
// single current step on an overview page, all five are equally reachable.
std::string ml_pipeline_overview(
    const std::vector<std::pair<std::string, std::string>>& steps,
    const std::vector<std::string>& hrefs) {
    std::string html = "<div class=\"mlStepFlow mlPipelineOverview\">";
    for (std::size_t i = 0; i < steps.size(); ++i) {
        html += "<a class=\"mlStepBox\" href=\"" + hrefs[i] +
                "\"><span class=\"mlStepNum\">" + std::to_string(i + 1) +
                "</span><div><strong>" + steps[i].first + "</strong>";
        if (!steps[i].second.empty()) {
            html += "<span class=\"mlStepDesc\">" + steps[i].second + "</span>";
        }
        html += "</div></a>";
    }
    return html + "</div>";
}

// Wraps one titled group of sidebar links (Workspace, Settings, Machine
// Learning, Admin, ...) in a collapsible <details> instead of a plain <h3>,
// so a caller with many reachable sections can shrink the sidebar down to
// just the section titles that matter to them. `key` is a short stable
// identifier persisted client-side (see the sidebar-collapse script in
// application_page) so the collapsed/expanded state survives the full-page
// navigation every sidebar click already does -- <details> alone resets to
// `open` on every fresh page load otherwise. Defaults to expanded so
// nothing already reachable becomes newly hidden by this change.
std::string sidebar_section(const std::string& key, const std::string& title,
                            const std::string& body) {
    return "<details open class=\"sidebarSection\" data-key=\"" + key +
          "\"><summary>" + title + "</summary><div>" + body +
          "</div></details>";
}

// Picks a random empty-state greeting for a brand new chat, using the
// signed-in user's display name when one is on record. Bodies are stored
// lowercase-start ("what's ...") so they read naturally both standalone
// (capitalized here) and after a name salutation (left lowercase); this
// avoids maintaining two parallel 64-entry message lists just to get
// name-aware and anonymous phrasing out of the same pool.
// last_chat_title, when non-empty, is the signed-in user's most recent
// chat's real (model-derived, not "New chat") title -- see the caller in
// server.cpp, which looks that up before this page renders. About a third
// of the time this pulls from kResumeBodies instead of kBodies, so a
// returning user is sometimes grounded back into their last real topic
// before they type anything, rather than only ever getting generic small
// talk that ignores a chat history the server already has.
std::string chat_welcome_message(const std::string& display_name,
                                 const std::string& last_chat_title) {
    static const char* const kBodies[] = {
        "what's on the agenda today?", "where should we start?",
        "what are we building today?", "what's the plan for today?",
        "what can I help you tackle?", "what's next on your list?",
        "ready when you are -- what's up?", "what should we dig into?",
        "what's on your mind today?", "what are we working on?",
        "what would you like to get done?", "what's the mission today?",
        "what should we get moving on?", "what's today's focus?",
        "what can I take off your plate?", "what's the first thing to tackle?",
        "what's brewing today?", "what shall we work on?",
        "what's the task at hand?", "what do you need help with?",
        "what's on deck today?", "where do you want to begin?",
        "what's today looking like?", "what should we knock out first?",
        "what's the goal for this session?", "what are we solving today?",
        "what's up -- what do you need?", "what's the priority today?",
        "what can we get done together?", "what's the next thing to build?",
        "what's calling for attention today?", "what should we chase down?",
        "what's the challenge today?", "what's worth tackling first?",
        "what's on the table today?", "what should we make progress on?",
        "what's today's project?", "what's the next step?",
        "what are we digging into today?", "what's the plan of attack?",
        "what's on your desk today?", "what can I lend a hand with?",
        "what's the task for today?", "what should we get into?",
        "what's the focus for now?", "what's up next?",
        "what should we start with?", "what's today's puzzle?",
        "what's the job today?", "what's cooking today?",
        "what can we knock out today?", "what's the target for today?",
        "what should we sort out?", "what's the next build?",
        "what's the itinerary today?", "what's the order of business?",
        "what should we look at first?", "what's the agenda?",
        "what's the game plan today?", "what can I get started on?",
        "what's today's mission?", "what's the next problem to solve?",
        "what's worth diving into?", "what's the first move today?",
        "what's today's headline?", "what's the story today?",
        "what's up first?", "what's calling?", "what's the play today?",
        "what should we crack on with?", "what's on the runway today?",
        "what's the lead item today?", "what's queued up?",
        "what's the docket today?", "what's the opening move?",
        "what's the target today?", "what should we chip away at?",
        "what's front of mind today?", "what's the thread to pull today?",
        "what's the next milestone?", "what's the sprint today?",
        "what's the win we're chasing today?", "what's the ask today?",
        "what's the assignment today?", "what's the itch to scratch today?",
        "what's the objective right now?", "what's the opener today?",
        "what's up on your end?", "what's the shape of today?",
        "what's the lineup today?", "what should we set in motion?",
        "what's the pressing item today?", "what's the next chapter?",
        "what's the direction today?", "what's the move?",
        "what's begging for attention?", "what's the starting point today?",
        "what's the theme for today?", "what should we make happen?",
        "what's the ball to get rolling?", "what's the loose end to tie up?",
        "what's the itinerary look like?", "what's the checklist today?",
        "what's the standing item today?", "what's the next win?",
        "what's the thing to unblock?", "what's the fresh start today?",
    };
    // Referenced when a returning user's most recent chat had a real,
    // model-derived title (see ChatRecord::title's own comment) -- this
    // pool grounds the greeting in that actual prior topic instead of only
    // ever showing generic small talk, so a returning user is reminded
    // what they were last doing before they type a single word. "{topic}"
    // is replaced with that title, quoted, by the caller below.
    static const char* const kResumeBodies[] = {
        "welcome back -- last time we were on {topic}. Pick that back up, "
        "or start something new?",
        "last session was about {topic}. Want to continue there, or dig "
        "into something else today?",
        "picking up where we left off? Last time was {topic}.",
        "still thinking about {topic} from last time, or ready for "
        "something new?",
        "your last chat here was {topic} -- carry on with that, or fresh "
        "start today?",
        "welcome back -- {topic} was the last thing on the table. Back to "
        "that, or something new?",
    };
    // Time-neutral salutations are always eligible. "Morning"/"Afternoon"/
    // "Evening" are only mixed in when the server's actual local clock is in
    // that window -- otherwise a night-owl session would get greeted
    // "Morning" at 2am, which is just wrong.
    static const char* const kNeutralSalutations[] = {
        "Hey {name} -- ", "Welcome back, {name} -- ", "{name}, ",
        "Good to see you, {name} -- ", "Alright {name}, ", "",
    };
    static const char* const kMorningSalutations[] = {
        "Morning, {name} -- ", "Good morning, {name} -- ",
    };
    static const char* const kAfternoonSalutations[] = {
        "Afternoon, {name} -- ", "Good afternoon, {name} -- ",
    };
    static const char* const kEveningSalutations[] = {
        "Evening, {name} -- ", "Good evening, {name} -- ",
    };
    static thread_local std::mt19937 rng{std::random_device{}()};

    std::string body;
    if (!last_chat_title.empty() &&
        std::uniform_int_distribution<int>(0, 2)(rng) == 0) {
        body = kResumeBodies[std::uniform_int_distribution<size_t>(
            0, std::size(kResumeBodies) - 1)(rng)];
        const size_t topic_token = body.find("{topic}");
        if (topic_token != std::string::npos) {
            body.replace(topic_token, 7, "\"" + last_chat_title + "\"");
        }
    } else {
        body = kBodies[std::uniform_int_distribution<size_t>(
            0, std::size(kBodies) - 1)(rng)];
    }

    if (display_name.empty()) {
        std::string sentence(body);
        sentence[0] = static_cast<char>(std::toupper(sentence[0]));
        return sentence;
    }

    std::vector<const char*> candidates(std::begin(kNeutralSalutations),
                                        std::end(kNeutralSalutations));
    const std::time_t now_time =
        std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local_tm{};
#if defined(_WIN32)
    localtime_s(&local_tm, &now_time);
#else
    localtime_r(&now_time, &local_tm);
#endif
    const int hour = local_tm.tm_hour;
    if (hour >= 5 && hour < 12) {
        candidates.insert(candidates.end(), std::begin(kMorningSalutations),
                          std::end(kMorningSalutations));
    } else if (hour >= 12 && hour < 17) {
        candidates.insert(candidates.end(), std::begin(kAfternoonSalutations),
                          std::end(kAfternoonSalutations));
    } else if (hour >= 17 && hour < 22) {
        candidates.insert(candidates.end(), std::begin(kEveningSalutations),
                          std::end(kEveningSalutations));
    }
    // 22:00-04:59 gets only the time-neutral pool -- there's no tasteful way
    // to say "good 3am" to someone.

    const char* salutation = candidates[std::uniform_int_distribution<size_t>(
        0, candidates.size() - 1)(rng)];
    std::string prefix(salutation);
    const size_t token = prefix.find("{name}");
    if (token != std::string::npos) {
        prefix.replace(token, 6, display_name);
    }
    if (prefix.empty()) {
        std::string sentence(body);
        sentence[0] = static_cast<char>(std::toupper(sentence[0]));
        return sentence;
    }
    return prefix + body;
}

// Presents one section (chat, projects, models/*, admin/*) of the workspace
// as its own full document at its own URL (see server.cpp's /app/* routes),
// with a sidebar of real links to every other section the caller's role can
// reach. Switching sections is therefore a normal full-page navigation, not
// a client-side panel swap -- there is no hash router involved.
std::string application_page(const UserRecord& user, const std::string& section,
                             const std::string& chat_id,
                             const std::string& last_chat_title) {
    // The Admin nav entries are omitted entirely for non-administrators, and
    // the Settings entries for plain viewers. This is presentation only --
    // every admin-only or settings-only route (users.manage,
    // downloads.manage) still enforces its own permission check per request
    // regardless of what this page renders or which URL is typed directly.
    const bool is_administrator = user.role == UserRole::administrator;
    const bool is_developer = user.role == UserRole::developer;
    const bool can_manage_settings = is_administrator || is_developer;
    const bool can_write_chat = role_allows(user.role, "chats.write");
    const std::string role_attr =
        is_administrator ? "administrator" : (is_developer ? "developer" : "viewer");

    // ML forms clarity pass: the real end-to-end path from nothing to a
    // trained model spans five separate panels with no other on-page cue
    // that they are one sequence (see ml_step_flow()'s own comment). Every
    // panel that is a stage of this pipeline prepends this same banner
    // with its own stage highlighted, so a user landing on any one of them
    // sees the whole path before reading a single form field.
    static const std::vector<std::pair<std::string, std::string>>
        kMlPipelineSteps = {
            {"Create a project",
             "Groups the datasets, models, and training jobs below under "
             "one goal."},
            {"Register a dataset",
             "Describes the data (name, source, license). This alone "
             "stores no rows yet."},
            {"Upload dataset content",
             "Adds the actual CSV/JSON/JSONL/Parquet rows. A training job "
             "cannot run without this step."},
            {"Create &amp; run a training job",
             "Picks a project and a dataset, then Train Now fits a real "
             "model on the uploaded rows."},
            {"Evaluate the result",
             "Checks accuracy on held-out data, or tracks the run as a "
             "comparable experiment."},
        };

    std::string body;
    if (section == "chat") {
        // Mirrors a familiar chat layout: a centered empty-state greeting
        // when nothing is selected yet, growing message history once a
        // conversation exists, and a single pill-shaped composer pinned to
        // the bottom that carries the model picker itself -- there is no
        // separate "Start chat" form to fill in first. Project and model
        // are only choosable before the first message; once the chat
        // exists both are fixed to it (see openChat() disabling them) since
        // the model is set at chat-creation time server-side.
        const bool is_new_chat = chat_id.empty();
        body =
            "<div id=\"chatShell\">"
            "<div id=\"chatTopBar\"><label>Project"
            "<select id=\"chatProject\"></select></label></div>"
            "<div id=\"chatEmpty\"" + std::string(is_new_chat ? "" : " hidden") +
            "><h1>" + html_escape(chat_welcome_message(user.display_name,
                                                       last_chat_title)) +
            "</h1></div>"
            "<div id=\"chatMessages\"></div>"
            "<div id=\"attachChips\" class=\"attachChips\"></div>"
            "<form id=\"newMessage\" class=\"composer\">"
            "<input type=\"hidden\" id=\"messageChat\" value=\"" +
            html_escape(chat_id) +
            "\">"
            // Text files only, matching AttachmentStore::add_text -- there is
            // no server-side path for binary content, so the picker doesn't
            // offer to select any.
            "<input type=\"file\" id=\"attachFileInput\" class=\"visuallyHidden\" "
            "multiple accept=\".txt,.md,.markdown,.c,.h,.cc,.cpp,.hpp,.hh,.py,.js,"
            ".jsx,.ts,.tsx,.json,.yaml,.yml,.toml,.ini,.cfg,.conf,.csv,.log,.rs,.go,"
            ".java,.rb,.sh,.sql,text/plain\">"
            "<button type=\"button\" id=\"attachToggle\" class=\"composerIconBtn\" "
            "title=\"Attach files\">+</button>"
            "<div class=\"composerInputWrap\">"
            "<textarea id=\"messageContent\" class=\"composerInput\" rows=\"1\" "
            "placeholder=\"Message MasterAI...\" required></textarea>"
            "</div>"
            "<select id=\"chatModel\" class=\"composerModelPicker\"></select>"
            // Effort/thinking-level panel: opens automatically whenever the
            // model picker changes (see openModelSettingsPanel()) and can be
            // reopened afterward with this gear button. Its settings persist
            // per model id in localStorage (see loadModelSettings()/
            // saveModelSettings()) and are read back the moment that same
            // model is chosen again, in this chat or a future one.
            "<button type=\"button\" id=\"modelSettingsToggle\" "
            "class=\"composerIconBtn\" title=\"Model settings (effort / "
            "thinking)\" aria-label=\"Model settings\">&#9881;</button>"
            "<div id=\"modelSettingsPanel\" class=\"modelSettingsPanel\" hidden>"
            "<div class=\"modelSettingsPanelHeader\">Model settings"
            "<button type=\"button\" id=\"modelSettingsClose\" "
            "class=\"modelSettingsClose\" aria-label=\"Close model settings\">"
            "&#215;</button></div>"
            "<label>Effort<select id=\"modelEffort\">"
            "<option value=\"low\">Low</option>"
            "<option value=\"medium\" selected>Medium</option>"
            "<option value=\"high\">High</option></select></label>"
            "<label>Thinking<select id=\"modelThinking\">"
            "<option value=\"off\" selected>Off</option>"
            "<option value=\"on\">On</option></select></label>"
            "<p id=\"modelSettingsNote\" class=\"modelSettingsNote\"></p>"
            // Phase 84 follow-up: unlike Effort/Thinking above (per-model,
            // localStorage-only), this is a per-chat, server-persisted
            // setting -- see loadChatToolMode()/saveChatToolMode() in the
            // shared client script -- so it stays correct across devices
            // and controls what actually gets sent/executed server-side,
            // not just a client-side sampling hint.
            "<label>Tool execution<select id=\"chatToolMode\">"
            "<option value=\"auto\" selected>Auto</option>"
            "<option value=\"confirm_all\">Confirm every action</option>"
            "<option value=\"off\">Off</option></select></label>"
            "<p id=\"chatToolModeNote\" class=\"modelSettingsNote\"></p>"
            "</div>"
            "<button type=\"submit\" class=\"composerSendBtn\" title=\"Send\">"
            "&#8593;</button>"
            "<button id=\"cancelMessage\" type=\"button\" class=\"composerIconBtn\" "
            "title=\"Cancel\">&times;</button>"
            "</form></div>";
    } else if (section == "projects") {
        body =
            "<section id=\"panel-projects\" class=\"panel\"><div>"
            "<h2>Projects</h2>"
            "<form id=\"newProject\"><label>Project ID<input id=\"projectId\" "
            "required pattern=\"[A-Za-z0-9_.-]+\"></label><label>Name"
            "<input id=\"projectName\" required></label>"
            "<button title=\"Create project\">" ICON_PLUS_SVG " Create project</button></form>"
            "<div id=\"projectsList\">Loading...</div></div></section>";
    } else if (section == "models-inventory") {
        // Memory Status: an administrator-only widget (its live figures come
        // from /api/v1/system/cache and /api/v1/system/scratch, both already
        // gated to administrators -- see server.cpp) showing real-time
        // system and MasterAI memory usage underneath the inventory table,
        // plus a "Clean Memory" action so freed RAM is reflected immediately
        // without a page reload. Empty string for every other role, exactly
        // like the newMemory form below is gated by can_write_chat.
        const std::string memory_status_section =
            is_administrator
                ? "<section id=\"memoryStatusSection\">"
                  "<h3>Memory Status</h3>"
                  "<p class=\"memoryStatusHint\">Live system and MasterAI "
                  "memory usage, refreshed every 5 seconds.</p>"
                  "<div id=\"memoryStatusTable\">Loading...</div>"
                  "<h4>Clean Memory options</h4>"
                  "<div class=\"memoryCleanOptions\">"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanTrimCaches\" checked> Trim MasterAI's bounded "
                  "caches</label>"
                  "<span class=\"memoryStatusHint\">Evicts cache entries down "
                  "to their configured size limit.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanScratch\" checked> Remove orphaned temporary "
                  "files</label>"
                  "<span class=\"memoryStatusHint\">Clears leftover scratch/"
                  "working files left behind by interrupted jobs.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanWorkingSet\" checked> Release freed memory "
                  "back to the OS</label>"
                  "<span class=\"memoryStatusHint\">Trims MasterAI's own "
                  "process memory footprint.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanUnloadModel\"> Unload the currently loaded AI "
                  "model</label>"
                  "<span class=\"memoryStatusHint\">Frees the most RAM, but "
                  "the next chat message will need to reload it.</span></div>"
                  "</div>"
                  // Windows system-memory options: real OS-level memory-
                  // manager primitives (standby list, modified page list,
                  // system file cache, other processes' working sets), not
                  // anything scoped to MasterAI's own process. Every one of
                  // these genuinely requires MasterAI itself to be running
                  // elevated (Administrator) -- the hint text says so
                  // upfront rather than letting a checked box silently do
                  // nothing; the result summary also reports exactly which
                  // steps were skipped and why (see summarizeMemoryClean()).
                  "<h4>Windows system memory options</h4>"
                  "<p class=\"memoryStatusHint\">These act on the whole "
                  "system, not just MasterAI, and only take effect if "
                  "MasterAI itself is running as Administrator.</p>"
                  "<div class=\"memoryCleanOptions memoryCleanOptionsGrid\">"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanTrimOtherProcesses\"> Trim other applications' "
                  "working sets</label>"
                  "<span class=\"memoryStatusHint\">Only applications using"
                  "<input type=\"number\" id=\"memCleanTrimOtherProcessesMinMib\" "
                  "min=\"0\" step=\"10\" value=\"100\" class=\"memoryStatusInlineNumber\">"
                  "MB or more.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanProtectForeground\" checked> Protect the "
                  "foreground application</label>"
                  "<span class=\"memoryStatusHint\">Skips whichever "
                  "application currently has focus, so it won't stutter."
                  "</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanFlushModifiedList\"> Flush the modified page "
                  "list</label>"
                  "<span class=\"memoryStatusHint\">Writes modified pages to "
                  "disk so they can be discarded from RAM.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanPurgeStandby\"> Purge the standby list</label>"
                  "<span class=\"memoryStatusHint\">Discards cached file "
                  "pages Windows was keeping around speculatively."
                  "</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanPurgeLowPriorityStandby\"> Purge low-priority "
                  "standby pages</label>"
                  "<span class=\"memoryStatusHint\">A lighter version of the "
                  "above: only the lowest-priority cached pages.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanEmptySystemWorkingSets\"> Empty system and "
                  "service working sets</label>"
                  "<span class=\"memoryStatusHint\">Also trims SYSTEM and "
                  "service processes, not just user applications.</span></div>"
                  "<div class=\"memoryCleanOption\">"
                  "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
                  "id=\"memCleanClearFileCache\"> Clear the system file "
                  "cache</label>"
                  "<span class=\"memoryStatusHint\">Forces Windows' own file "
                  "cache back down after briefly capping it.</span></div>"
                  "</div>"
                  "<button type=\"button\" id=\"memCleanStart\" "
                  "title=\"Clean memory\">Clean Memory</button>"
                  "<progress id=\"memCleanProgress\" value=\"0\" max=\"100\" "
                  "hidden></progress>"
                  "<p id=\"memCleanStatus\" class=\"memoryStatusHint\"></p>"
                  "</section>"
                : "";
        // The widget's logic lives in application_script() (served from
        // /assets/app.js), not an inline <script> here -- html_response()'s
        // CSP is script-src 'self', which blocks inline scripts outright.
        // See the DOMContentLoaded block's #memoryStatusSection guard.
        body =
            "<section id=\"panel-models-inventory\" class=\"panel\"><div>"
            "<h2>Model inventory</h2><div id=\"modelsList\">Loading...</div>" +
            memory_status_section +
            "</div></section>";
    } else if (section == "models-download") {
        body =
            "<section id=\"panel-models-download\" class=\"panel\"><div>"
            "<h2>Download a model</h2>"
            // Source selector: assembles a source URL/revision for the
            // operator instead of requiring one be hand-typed, without
            // changing what's actually submitted or validated -- the
            // assembled fields still go through the same required raw
            // sourceUrl/revision/sha256 fields and the same server-side
            // revision-substring and post-download SHA-256 checks as before.
            "<label>Download source<select id=\"downloadSourceType\">"
            "<option value=\"huggingface\">Hugging Face</option>"
            "<option value=\"github\">GitHub Releases</option>"
            "<option value=\"modelscope\">ModelScope</option>"
            "<option value=\"custom\">Custom URL</option>"
            "</select></label>"
            "<div id=\"hfFields\">"
            "<label>Org/repo (e.g. Qwen/Qwen2.5-Coder-7B-Instruct-GGUF)"
            "<input id=\"hfRepo\"></label>"
            "<label>Commit/revision<input id=\"hfRevision\"></label>"
            "<label>Filename<input id=\"hfFilename\"></label>"
            "<button type=\"button\" id=\"applyHfSource\">Fill source from "
            "these fields</button></div>"
            "<div id=\"githubFields\" hidden>"
            "<label>Owner/repo<input id=\"ghRepo\"></label>"
            "<label>Release tag<input id=\"ghTag\"></label>"
            "<label>Asset filename<input id=\"ghAsset\"></label>"
            "<button type=\"button\" id=\"applyGithubSource\">Fill source from "
            "these fields</button></div>"
            "<div id=\"msFields\" hidden>"
            "<label>Org/repo (e.g. second-state/StarCoder2-3B-GGUF)"
            "<input id=\"msRepo\"></label>"
            "<label>Commit/revision<input id=\"msRevision\"></label>"
            "<label>Filename<input id=\"msFilename\"></label>"
            "<button type=\"button\" id=\"applyMsSource\">Fill source from "
            "these fields</button></div>"
            // Suggestion picker: fills the raw form below from a verified,
            // hard-coded preset so the operator never has to hand-type a
            // commit hash or a 64-character digest to get a working model.
            "<h3>Or pick a curated suggestion</h3>"
            "<label>Filter suggestions by available RAM<select id=\"downloadTier\">"
            "<option value=\"test\">Tiny test model (~1 MB) - any hardware</option>"
            "<option value=\"1\">1 GB</option>"
            "<option value=\"2\">2 GB</option>"
            "<option value=\"3\">3 GB</option>"
            "<option value=\"4\">4 GB</option>"
            "<option value=\"5\">5 GB</option>"
            "<option value=\"6\">6 GB</option>"
            "<option value=\"7\">7 GB</option>"
            "<option value=\"8\">8 GB</option><option value=\"16\">16 GB</option>"
            "<option value=\"24\">24 GB</option><option value=\"32\">32 GB</option>"
            "<option value=\"64\">64 GB or more</option></select></label>"
            "<label>Suggested model<select id=\"downloadPreset\"></select></label>"
            "<button type=\"button\" id=\"applyPreset\">Reset form to suggestion"
            "</button>"
            "<form id=\"newDownload\">"
            "<label>Category<select id=\"downloadCategory\">"
            "<option value=\"general-programming\">general-programming</option>"
            "<option value=\"code-completion\">code-completion</option>"
            "<option value=\"code-review\">code-review</option>"
            "<option value=\"debugging\">debugging</option>"
            "<option value=\"documentation\">documentation</option>"
            "<option value=\"embeddings-code-search\">embeddings-code-search"
            "</option><option value=\"conversation\">conversation</option>"
            "</select></label>"
            "<label>Model ID<input id=\"downloadModelId\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Filename<input id=\"downloadFilename\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Source URL (Hugging Face, GitHub release, or ModelScope resolve URL)"
            "<input id=\"downloadSourceUrl\" type=\"url\" required></label>"
            "<label>Immutable revision (commit hash or release tag)"
            "<input id=\"downloadRevision\" required></label>"
            "<label>Expected SHA-256<input id=\"downloadSha256\" required "
            "pattern=\"[0-9a-fA-F]{64}\"></label>"
            "<label>File size (bytes)<input id=\"downloadSizeBytes\" "
            "type=\"number\" min=\"1\" required></label>"
            "<label>Minimum RAM (MiB)<input id=\"downloadMinRam\" type=\"number\" "
            "min=\"1\" required></label>"
            "<label>Recommended RAM (MiB)<input id=\"downloadRecRam\" "
            "type=\"number\" min=\"1\" required></label>"
            // These four exist so a manifest.json can be written the
            // moment the file lands -- without one ModelRegistry never
            // recognizes the download as a model at all, and it could
            // never reach Ready or be chatted with.
            "<label>Display name<input id=\"downloadDisplayName\" required "
            "maxlength=\"160\"></label>"
            "<label>Architecture (e.g. qwen2, llama, gemma, phi3)"
            "<input id=\"downloadArchitecture\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Quantization (e.g. Q4_K_M)<input id=\"downloadQuantization\" "
            "required pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>License<select id=\"downloadLicenseSpdx\" required>"
            "<option value=\"\">Choose the model's actual license</option>"
            "<option value=\"Apache-2.0\">Apache-2.0</option>"
            "<option value=\"MIT\">MIT</option>"
            "<option value=\"BSD-2-Clause\">BSD-2-Clause</option>"
            "<option value=\"BSD-3-Clause\">BSD-3-Clause</option>"
            "<option value=\"CC-BY-4.0\">CC-BY-4.0</option>"
            "<option value=\"Llama-3.1\">Llama-3.1</option>"
            "<option value=\"Llama-3.2\">Llama-3.2</option>"
            "<option value=\"Gemma\">Gemma</option></select>"
            "<p>If the model's real license isn't listed, it cannot be "
            "downloaded through this form.</p></label>"
            "<label class=\"checkboxLabel\"><input id=\"downloadLicense\" "
            "type=\"checkbox\" required> "
            "I have reviewed and accept this model's license</label>"
            "<button title=\"Queue download\">" ICON_DOWNLOAD_SVG " Queue download</button></form>"
            // Active downloads lives at the bottom of this same page --
            // queueing a download and watching it run are one continuous
            // task, not two separate destinations.
            "<h3>Active downloads</h3>"
            "<div id=\"downloadsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "models-benchmarks") {
        body =
            "<section id=\"panel-models-benchmarks\" class=\"panel\"><div>"
            "<h2>Benchmarks</h2><div id=\"benchmarksList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-dashboard") {
        // Foundation-phase acknowledgement page: confirms the Machine
        // Learning module is enabled and shows its real counts alongside the
        // full roadmap of interfaces from docs/PLAN.md "Machine Learning
        // Abilities" section 2, each marked available or planned -- see
        // MachineLearningRegistry's class comment in masterai.hpp for why
        // nothing here is fabricated.
        //
        // Phase 92 (consolidation): this landing page used to show only the
        // acknowledgement line, the stat counts, and one flat, unordered
        // table of all ~25 interfaces -- a newcomer had no way to tell from
        // this page which five pages are the real, working, end-to-end path
        // (create a project, register a dataset, fill it with rows, train,
        // evaluate) versus which are optional support tooling around that
        // path. The pipeline overview below is that path, in order, each
        // box a real link; the Interfaces table underneath deliberately
        // excludes those same five real content pages (see
        // renderMlDashboard()) so the two lists never repeat each other.
        body =
            "<section id=\"panel-ml-dashboard\" class=\"panel\"><div>"
            "<h2>Machine Learning</h2>"
            "<p id=\"mlAck\">Loading...</p>"
            "<div id=\"mlStats\"></div>"
            "</div><div>"
            "<h2>The five steps, in order</h2>"
            "<p>Every one of these five pages does something real: creating "
            "a project, registering a dataset, filling it with rows, "
            "training a genuine model on those rows, and measuring how "
            "accurate the result is. They connect in a straight line -- "
            "the project you create in Step 1 is what you pick again in "
            "Step 4; the dataset you register in Step 2 is empty until "
            "Step 3 gives it real rows; Step 4 cannot run without a "
            "dataset that has those rows; Step 5 only makes sense once "
            "Step 4 has produced a trained model to measure. Click any box "
            "below to go straight to that step.</p>" +
            ml_pipeline_overview(
                kMlPipelineSteps,
                {"/app/ml/projects", "/app/ml/datasets", "/app/ml/datasets",
                 "/app/ml/training-jobs", "/app/ml/evaluation-runs"}) +
            "</div><div>"
            "<h2>Additional tools</h2>"
            "<p>Everything below supports the five steps above -- "
            "registering an existing model instead of letting Step 4 "
            "create one, fine-tuning, generating synthetic or "
            "instruction-formatted data, retrieval, safety review, "
            "deployment, and more -- but none of it is required to "
            "complete the real end-to-end path above.</p>"
            "<div id=\"mlInterfaces\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-projects") {
        // Phase 38 (docs/PLAN.md "Machine Learning Abilities" section 5):
        // create and list ML projects. Phase 93 added the section's
        // remaining governance/target fields (see MLProject's comment in
        // masterai.hpp) -- collected here at creation, and editable
        // afterwards via the Governance form below (POST
        // .../projects/{id}/governance).
        body =
            ml_step_flow(1, kMlPipelineSteps) +
            "<section id=\"panel-ml-projects\" class=\"panel\"><div>"
            "<h2>Step 1 &mdash; New Machine Learning project</h2>"
            "<form id=\"newMlProject\"><label>Name" +
            field_hint("A short, memorable name for this effort, e.g. "
                       "&quot;Support ticket triage&quot;. You will pick "
                       "this project again when creating a training job.") +
            "<input id=\"mlProjectName\" required maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlProjectDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Objective<textarea id=\"mlProjectObjective\" "
            "rows=\"2\"></textarea></label>"
            "<label>Subject domain<input id=\"mlProjectSubjectDomain\"></label>"
            "<label>Model task<input id=\"mlProjectModelTask\"></label>"
            "<button title=\"Create project\">" ICON_PLUS_SVG " Create project</button></form>"
            "</div><div>"
            "<h2>Projects</h2><div id=\"mlProjectsList\">Loading...</div>"
            "</div><div>"
            "<h2>Governance &amp; targets</h2>"
            "<form id=\"mlProjectGovernanceForm\">"
            "<label>Project" +
            field_hint("Which project this form edits. Clicking the "
                      "Configure (gear) button on a row in the Projects "
                      "table above fills this in automatically.") +
            "<select id=\"mlProjectGovernanceId\" required>"
            "<option value=\"\">Choose a project</option></select></label>"
            "<label>Administrators" +
            field_hint("Comma-separated user ids, in addition to the "
                      "owner, who are shown as co-administering this "
                      "project. Each id must be a real account or saving "
                      "is rejected.") +
            "<input id=\"mlProjectAdministrators\" "
            "placeholder=\"user-id-1, user-id-2\"></label>"
            "<label>Approved data sources<input "
            "id=\"mlProjectApprovedDataSources\" "
            "placeholder=\"comma-separated\"></label>"
            "<label>Security classification<input "
            "id=\"mlProjectSecurityClassification\"></label>"
            "<label>Target architecture<input "
            "id=\"mlProjectTargetArchitecture\"></label>"
            "<label>Target deployment environment<input "
            "id=\"mlProjectTargetDeploymentEnvironment\"></label>"
            "<label>Success criteria<textarea "
            "id=\"mlProjectSuccessCriteria\" rows=\"2\"></textarea></label>"
            "<label>Evaluation requirements<textarea "
            "id=\"mlProjectEvaluationRequirements\" rows=\"2\">"
            "</textarea></label>"
            "<label>Safety requirements<textarea "
            "id=\"mlProjectSafetyRequirements\" rows=\"2\"></textarea></label>"
            "<label>Storage allocation (MB)" +
            field_hint("A declared ceiling recorded for operator "
                      "reference -- this build does not yet enforce a "
                      "per-project disk quota against it.") +
            "<input id=\"mlProjectStorageAllocationMb\" type=\"number\" "
            "min=\"0\" step=\"1\"></label>"
            "<label>Compute allocation notes<textarea "
            "id=\"mlProjectComputeAllocationNotes\" rows=\"2\">"
            "</textarea></label>"
            "<button title=\"Save governance\">" ICON_PLUS_SVG " Save governance</button></form>"
            "</div></section>";
    } else if (section == "ml-models") {
        // Phase 39 (docs/PLAN.md "Machine Learning Abilities" section 7):
        // register and list Model Registry entries. Only the
        // identity/provenance/lifecycle fields ModelRegistryStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the fields deferred to later phases.
        body =
            "<section id=\"panel-ml-models\" class=\"panel\"><div>"
            "<p class=\"mlPipelineNote\">Optional. A training job that "
            "names no model here still works &mdash; it creates one "
            "automatically when it finishes. Register a model here only "
            "if you want to track its identity/provenance before you "
            "start training.</p>"
            "<h2>Register a model</h2>"
            "<form id=\"newMlModel\">"
            "<label>Internal name (unique identifier)" +
            field_hint("A short machine-friendly id with no spaces, e.g. "
                       "&quot;support-triage-v1&quot;. Used to reference "
                       "this exact model elsewhere.") +
            "<input id=\"mlModelName\" required maxlength=\"160\"></label>"
            "<label>Display name<input id=\"mlModelDisplayName\"></label>"
            "<label>Version<input id=\"mlModelVersion\" "
            "placeholder=\"e.g. 1.0.0\"></label>"
            "<label>Model family<input id=\"mlModelFamily\" "
            "placeholder=\"e.g. Llama, Mistral\"></label>"
            "<label>Model task<input id=\"mlModelTask\" "
            "placeholder=\"e.g. text-classification\"></label>"
            "<label>Model format<input id=\"mlModelFormat\" "
            "placeholder=\"e.g. GGUF, ONNX, safetensors\"></label>"
            "<label>Quantization" +
            field_hint("The GGUF's quantization level, e.g. &quot;F16&quot;, "
                       "&quot;Q8_0&quot;, or &quot;Q4_K_M&quot;. Matters most "
                       "if this model will be used as a Fine-Tuning base: "
                       "LoRA fine-tuning is only reliable against a "
                       "full/near-full precision base (F32, F16, BF16, or "
                       "Q8_0) -- a more heavily quantized base still runs "
                       "but the Fine-Tuning result will carry a quality "
                       "warning. Leave blank if unknown.") +
            "<input id=\"mlModelQuantization\" "
            "placeholder=\"e.g. F16, Q8_0, Q4_K_M\"></label>"
            "<label>Source<input id=\"mlModelSource\" "
            "placeholder=\"where this model came from\"></label>"
            "<label>License<input id=\"mlModelLicense\"></label>"
            "<button title=\"Register model\">" ICON_PLUS_SVG " Register model</button></form>"
            // Phase 56: live prediction against a trained model's persisted
            // weights. Feature values are entered as JSON keyed by column
            // name so the caller never has to know the internal ordering.
            "<h2>Predict with a trained model</h2>"
            "<form id=\"mlPredictForm\">"
            "<label>Model ID (a model produced by a training run)"
            "<select id=\"mlPredictModelId\" required><option value=\"\">"
            "Choose a trained model</option></select></label>"
            "<label>Feature values (JSON object, e.g. "
            "{&quot;sepal_length&quot;:5.1,&quot;sepal_width&quot;:3.5})"
            "<textarea id=\"mlPredictFeatures\" rows=\"3\" required>"
            "</textarea></label>"
            "<button title=\"Predict\">" ICON_PLAY_SVG " Predict</button></form>"
            "<p id=\"mlPredictResult\"></p>"
            "</div><div>"
            "<h2>Registered models</h2>"
            "<div id=\"mlModelsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-datasets") {
        // Phase 39 (docs/PLAN.md "Machine Learning Abilities" section 10):
        // register and list datasets, and approve or reject them. Phase 94
        // added the section's remaining content-derived metrics (computed
        // for real at upload time -- see compute_dataset_content_metrics()
        // in ml_engine.cpp) and Dataset Versioning (section 11), shown in
        // the table's Version/Quality/Duplicates columns and the History
        // button below, plus the administrator-declared fields (sensitive-
        // data status, split percentages) in the Declare metadata form.
        body =
            ml_step_flow(2, 3, kMlPipelineSteps) +
            "<section id=\"panel-ml-datasets\" class=\"panel\"><div>"
            "<h2>Step 2 &mdash; Register a dataset</h2>"
            "<p class=\"mlPipelineNote\">This only records where the data "
            "comes from. It stores no rows yet &mdash; that is the "
            "separate step below.</p>"
            "<form id=\"newMlDataset\">"
            "<label>Name<input id=\"mlDatasetName\" required "
            "maxlength=\"160\"></label>"
            "<label>What kind of data is this?" +
            field_hint("Tabular picks rows of numbers/categories with one "
                       "column to predict (classification or regression), "
                       "e.g. spreadsheet-style data. Instruction / "
                       "fine-tuning text picks rows of question-and-answer "
                       "or prompt-and-response text used to fine-tune an "
                       "LLM &mdash; no column to predict is needed, so a "
                       "\"target column\" does not apply.") +
            "<select id=\"mlDatasetPurpose\">"
            "<option value=\"tabular\">Tabular data (classification / "
            "regression)</option>"
            "<option value=\"instruction\">Instruction / fine-tuning text "
            "(LLM training)</option>"
            "</select></label>"
            "<label>Description<textarea id=\"mlDatasetDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Subject area<input id=\"mlDatasetSubjectArea\" "
            "placeholder=\"e.g. customer support transcripts\"></label>"
            "<label>Source<input id=\"mlDatasetSource\" "
            "placeholder=\"where this data came from\"></label>"
            "<label>License<input id=\"mlDatasetLicense\"></label>"
            "<label>Data format<input id=\"mlDatasetFormat\" "
            "placeholder=\"e.g. JSONL, CSV\"></label>"
            "<button title=\"Register dataset\">" ICON_PLUS_SVG " Register dataset</button></form>"
            // Phase 56: real content upload. The uploaded file is fully
            // parsed and validated server-side before anything is stored,
            // and the returned profile (rows, columns, task) is shown below
            // the form. Dataset content format completion phase: CSV, JSON
            // (a top-level array of flat objects), JSONL (one flat object
            // per line), and Parquet (converted via the same DuckDB helper
            // the Knowledge ingestion page uses) are all accepted -- the
            // file's own extension picks the format, so nothing else on
            // this form changes between them.
            "<h2>Step 3 &mdash; Upload dataset content</h2>"
            "<p class=\"mlPipelineWarning\">Required before training. A "
            "dataset registered above but never given content here will "
            "fail training with &quot;upload CSV content to the job's "
            "dataset first&quot;.</p>"
            "<form id=\"newMlDatasetContent\">"
            "<label>Dataset" +
            field_hint("Pick the dataset you registered in Step 2. Its "
                       "row shows &#9888; no content uploaded yet until "
                       "you finish this step.") +
            "<select id=\"mlDatasetContentId\" required>"
            "<option value=\"\">Choose a registered dataset</option>"
            "</select></label>"
            "<label>Target column (the column to predict; blank uses the "
            "last column; ignored for an Instruction / fine-tuning text "
            "dataset)" +
            field_hint("The column of values the model should learn to "
                       "predict from every other column, e.g. "
                       "&quot;price&quot; or &quot;label&quot;. Leave "
                       "blank to use the file's last column. Ignored "
                       "entirely for a dataset registered as Instruction / "
                       "fine-tuning text -- that data needs "
                       "&quot;instruction&quot;/&quot;prompt&quot; and "
                       "&quot;response&quot;/&quot;output&quot;/"
                       "&quot;completion&quot; columns instead.") +
            "<input id=\"mlDatasetContentTarget\"></label>"
            "<label>Dataset file -- .csv, .json, .jsonl, or .parquet" +
            field_hint("A CSV/JSONL row or JSON array entry is one "
                       "training example. A feature column may be a "
                       "number or a category (e.g. text like "
                       "&quot;document&quot;/&quot;image&quot;) &mdash; a "
                       "category column is encoded automatically, no "
                       "manual conversion needed. Parquet needs an "
                       "administrator-configured DuckDB helper, see "
                       "Machine Learning Settings.") +
            "<input id=\"mlDatasetContentFile\" type=\"file\" "
            "accept=\".csv,text/csv,.json,application/json,.jsonl,"
            "application/x-ndjson,.parquet\" required></label>"
            "<button title=\"Upload content\">" ICON_UPLOAD_SVG " Upload content</button></form>"
            "<p id=\"mlDatasetContentResult\"></p>"
            "</div><div>"
            "<h2>Registered datasets</h2>"
            "<div id=\"mlDatasetsList\">Loading...</div>"
            "</div><div>"
            "<h2>Augment dataset (optional)</h2>"
            "<p>Adds real synthetic rows to help the model generalize -- "
            "your original rows are never changed or removed, only added "
            "to. Skip this step entirely if you don't need it.</p>"
            "<form id=\"newMlDatasetAugment\">"
            "<label>Dataset" +
            field_hint("The dataset to augment. Must already have content "
                       "uploaded in Step 3.") +
            "<select id=\"mlDatasetAugmentId\" required>"
            "<option value=\"\">Choose a registered dataset</option>"
            "</select></label>"
            "<label>Synonym replacement" +
            field_hint("For text columns: replaces some words with a "
                       "similar-meaning word from a small built-in list "
                       "(e.g. &quot;good&quot; &rarr; &quot;great&quot;).") +
            "<input id=\"mlDatasetAugmentSynonymReplacement\" "
            "type=\"checkbox\"></label>"
            "<label>Random insertion" +
            field_hint("For text columns: inserts an extra similar-meaning "
                       "word at a random position.") +
            "<input id=\"mlDatasetAugmentRandomInsertion\" "
            "type=\"checkbox\"></label>"
            "<label>Random deletion" +
            field_hint("For text columns: randomly drops a few words.") +
            "<input id=\"mlDatasetAugmentRandomDeletion\" "
            "type=\"checkbox\"></label>"
            "<label>Random swap" +
            field_hint("For text columns: randomly swaps the position of "
                       "two words.") +
            "<input id=\"mlDatasetAugmentRandomSwap\" "
            "type=\"checkbox\"></label>"
            "<label>Text change amount (0.0 to 1.0)" +
            field_hint("Roughly what fraction of the words in a text cell "
                       "each enabled text option above touches. 0.1 = "
                       "about one word in ten.") +
            "<input id=\"mlDatasetAugmentTextFraction\" type=\"number\" "
            "min=\"0\" max=\"1\" step=\"0.05\" value=\"0.1\"></label>"
            "<label>Add numeric noise" +
            field_hint("For number columns: adds a synthetic copy of each "
                       "row with a small random amount added to every "
                       "number column, scaled to that column's own typical "
                       "spread.") +
            "<input id=\"mlDatasetAugmentGaussianNoise\" "
            "type=\"checkbox\"></label>"
            "<label>Noise amount (fraction of each column's spread)"
            "<input id=\"mlDatasetAugmentNoiseFraction\" type=\"number\" "
            "min=\"0\" step=\"0.01\" value=\"0.05\"></label>"
            "<label>Balance rare classes" +
            field_hint("Classification datasets only: duplicates rows "
                       "from under-represented classes so every class has "
                       "closer to the same number of rows. Skipped "
                       "automatically for a regression dataset or one with "
                       "too many distinct classes to make sense of as "
                       "categories.") +
            "<input id=\"mlDatasetAugmentOversampleMinority\" "
            "type=\"checkbox\"></label>"
            "<label>Target balance ratio (0.0 to 1.0)" +
            field_hint("How close to the largest class every other class "
                       "should be brought, e.g. 0.5 = at least half as "
                       "many rows as the largest class. 1.0 = fully "
                       "balanced.") +
            "<input id=\"mlDatasetAugmentTargetRatio\" type=\"number\" "
            "min=\"0\" max=\"1\" step=\"0.05\" value=\"0.5\"></label>"
            "<button title=\"Augment dataset\">" ICON_PLUS_SVG " Augment dataset</button></form>"
            "<p id=\"mlDatasetAugmentResult\"></p>"
            "</div><div>"
            "<h2>Declare metadata</h2>"
            "<form id=\"mlDatasetDeclareForm\">"
            "<label>Dataset" +
            field_hint("Which dataset this form edits. Clicking the "
                      "Configure (gear) button on a row above fills this "
                      "in automatically.") +
            "<select id=\"mlDatasetDeclareId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Sensitive data status" +
            field_hint("Free text you declare yourself, e.g. &quot;contains "
                      "customer email addresses&quot; or &quot;none "
                      "known&quot;. This build does not scan content for "
                      "PII automatically.") +
            "<input id=\"mlDatasetSensitiveDataStatus\"></label>"
            "<label>Train split %<input id=\"mlDatasetTrainSplitPercent\" "
            "type=\"number\" min=\"0\" max=\"100\" step=\"1\"></label>"
            "<label>Validation split %<input "
            "id=\"mlDatasetValidationSplitPercent\" type=\"number\" "
            "min=\"0\" max=\"100\" step=\"1\"></label>"
            "<label>Test split %" +
            field_hint("The three splits together must not exceed 100%. "
                      "Leave all at 0 to keep using training/evaluation's "
                      "own default split.") +
            "<input id=\"mlDatasetTestSplitPercent\" type=\"number\" "
            "min=\"0\" max=\"100\" step=\"1\"></label>"
            "<button title=\"Save declaration\">" ICON_PLUS_SVG " Save declaration</button></form>"
            "</div></section>";
    } else if (section == "ml-subjects") {
        // Phase 40 (docs/PLAN.md "Machine Learning Abilities" section 12):
        // register and list subject packages, and move them through review.
        // Only the identity/scope/ownership/review-status fields
        // SubjectPackageStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the fields deferred to
        // the later Knowledge Ingestion Pipeline phase.
        body =
            "<section id=\"panel-ml-subjects\" class=\"panel\"><div>"
            "<h2>New subject package</h2>"
            "<p class=\"mlPipelineNote\">A subject package groups related "
            "knowledge files under one topic, e.g. &quot;Product "
            "documentation&quot;, so they can be ingested and searched "
            "together below.</p>"
            "<form id=\"newMlSubject\"><label>Name"
            "<input id=\"mlSubjectName\" required maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlSubjectDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Scope<textarea id=\"mlSubjectScope\" rows=\"2\" "
            "placeholder=\"what this subject does and does not cover\">"
            "</textarea></label>"
            "<label>Target audience<input id=\"mlSubjectTargetAudience\"></label>"
            "<button title=\"Create subject package\">" ICON_PLUS_SVG " Create subject package</button></form>"
            "<h2>Ingest a knowledge file</h2>"
            "<p>Select a real local text source. MasterAI hashes it, creates "
            "overlapping chunks, generates local deterministic vectors, and "
            "persists the populated index.</p>"
            "<form id=\"newMlKnowledgeDocument\">"
            "<label>Subject package<select id=\"mlKnowledgeSubjectId\" required>"
            "<option value=\"\">Choose a subject</option></select></label>"
            "<label>Vector store" +
            field_hint("Create this first on the Embeddings and Vector "
                       "Stores page if none exists yet -- this file's "
                       "chunks are indexed into whichever store you pick "
                       "here.") +
            "<select id=\"mlKnowledgeVectorStoreId\" required>"
            "<option value=\"\">Choose a vector store</option></select></label>"
            "<label>Knowledge file<input id=\"mlKnowledgeFile\" type=\"file\" "
            "accept=\".txt,.md,.markdown,.csv,.json,.jsonl,.parquet,text/plain,text/markdown,text/csv,application/json\" "
            "required></label><p class=\"fieldHint\">Size limit is set by the "
            "administrator. Text, Markdown, CSV, JSON, JSONL, and Parquet are "
            "supported.</p>"
            "<button title=\"Ingest and index file\">" ICON_UPLOAD_SVG " Ingest and index file</button></form>"
            "<p id=\"mlKnowledgeUploadResult\"></p>"
            "</div><div>"
            "<h2>Subject packages</h2>"
            "<div id=\"mlSubjectsList\">Loading...</div>"
            "<h2>Ingested knowledge files</h2>"
            "<div id=\"mlKnowledgeDocumentsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-label-tasks") {
        // Phase 41 (docs/PLAN.md "Machine Learning Abilities" section 14):
        // create and list labeling tasks against a registered dataset, and
        // move them through a lifecycle status. Only the identity/target-
        // dataset/label-mode/assignment/status fields LabelTaskStore
        // actually persists are collected here -- see that class's comment
        // in masterai.hpp for the label-record features deferred to a later
        // phase.
        body =
            "<section id=\"panel-ml-label-tasks\" class=\"panel\"><div>"
            "<p class=\"mlPipelineNote\">This tracks a human labeling "
            "task's status -- there is no automated labeler here. Moving a "
            "task to &quot;completed&quot; is what lets Automation "
            "Pipelines' &quot;Label data&quot; stage recognize this "
            "dataset as labeled.</p>"
            "<h2>New labeling task</h2>"
            "<form id=\"newMlLabelTask\">"
            "<label>Dataset<select id=\"mlLabelTaskDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlLabelTaskName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlLabelTaskDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Label mode" +
            field_hint("Free text describing what kind of label this task "
                       "collects, e.g. text_category for a single-class "
                       "label per row.") +
            "<input id=\"mlLabelTaskLabelMode\" "
            "placeholder=\"e.g. text_category, entity_span, bounding_box\">"
            "</label>"
            "<label>Assignee (optional)<select id=\"mlLabelTaskAssigneeId\">"
            "<option value=\"\">Unassigned</option></select></label>"
            "<button title=\"Create labeling task\">" ICON_PLUS_SVG " Create labeling task</button></form>"
            "</div><div>"
            "<h2>Labeling tasks</h2>"
            "<div id=\"mlLabelTasksList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-prep-jobs") {
        // Phase 41 (docs/PLAN.md "Machine Learning Abilities" section 15):
        // create and list data-preparation jobs against a registered
        // dataset, and move them through a lifecycle status. Only the
        // identity/target-dataset/operation/status fields
        // DataPreparationJobStore actually persists are collected here --
        // see that class's comment in masterai.hpp for the pipeline-step
        // composition, logging, and reproducibility record deferred to the
        // phase that actually executes a pipeline.
        body =
            "<section id=\"panel-ml-prep-jobs\" class=\"panel\"><div>"
            "<p class=\"mlPipelineNote\">This records a data-preparation "
            "step and moves it through a status lifecycle -- it does not "
            "yet actually transform your dataset's rows. Use it to track "
            "what cleanup work a dataset needs and its progress.</p>"
            "<h2>New data preparation job</h2>"
            "<form id=\"newMlPrepJob\">"
            "<label>Dataset<select id=\"mlPrepJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlPrepJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlPrepJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Operation" +
            field_hint("Free text naming the intended cleanup step, e.g. "
                       "remove_duplicates. Recorded for tracking only -- "
                       "moving this job to &quot;completed&quot; does not "
                       "run the operation against the dataset.") +
            "<input id=\"mlPrepJobOperation\" required "
            "placeholder=\"e.g. remove_duplicates, redact_pii, split_dataset\">"
            "</label>"
            "<button title=\"Create preparation job\">" ICON_PLUS_SVG " Create preparation job</button></form>"
            "</div><div>"
            "<h2>Preparation jobs</h2>"
            "<div id=\"mlPrepJobsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-training-jobs") {
        // Phase 42 (docs/PLAN.md "Machine Learning Abilities" section 16):
        // create and list training jobs against a registered project and
        // dataset, and move them through a lifecycle status. Phase 95 added
        // the section's remaining execution-policy fields -- see
        // TrainingJob's comment in masterai.hpp for which are genuinely
        // enforced by execute_training_job() (max runtime, retry,
        // checkpoint cadence) vs. recorded for operator reference only
        // (compute target, hardware, container, ...), collected in the
        // Execution policy form below.
        body =
            ml_step_flow(4, kMlPipelineSteps) +
            "<section id=\"panel-ml-training-jobs\" class=\"panel\"><div>"
            "<h2>Step 4 &mdash; New training job</h2>"
            "<form id=\"newMlTrainingJob\">"
            "<label>Project<select id=\"mlTrainingJobProjectId\" required>"
            "<option value=\"\">Choose a project</option></select></label>"
            "<label>Existing model (optional)" +
            field_hint("Leave as &quot;Create a new model&quot; unless "
                       "you registered a model on the Model Registry "
                       "page and specifically want this run to update "
                       "it.") +
            "<select id=\"mlTrainingJobModelId\">"
            "<option value=\"\">Create a new model</option></select></label>"
            "<label>Dataset" +
            field_hint("Must show a row count, e.g. &quot;120 row(s) "
                       "ready&quot;. A dataset marked &#9888; no content "
                       "uploaded yet has nothing to train on &mdash; go "
                       "back to Step 3 first.") +
            "<select id=\"mlTrainingJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlTrainingJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlTrainingJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Training type<input id=\"mlTrainingJobTrainingType\" "
            "placeholder=\"e.g. fine_tuning, transfer_learning, lora\">"
            "</label>"
            "<button title=\"Create training job\">" ICON_PLUS_SVG " Create training job</button></form>"
            "</div><div>"
            "<h2>Training jobs</h2>"
            "<div id=\"mlTrainingJobsList\">Loading...</div>"
            // Phase 56: the real training executor reports its genuine
            // result (method, final loss, held-out metrics) here after a
            // "Train now" click.
            "<h2>Last training result</h2>"
            "<p id=\"mlTrainingRunResult\">No training run in this session "
            "yet. Click \"Train now\" on a job whose dataset has uploaded "
            "CSV content.</p>"
            "</div><div>"
            "<h2>Execution policy</h2>"
            "<form id=\"mlTrainingJobPolicyForm\">"
            "<label>Training job" +
            field_hint("Which job this form edits. Clicking the Configure "
                      "(gear) button on a row above fills this in "
                      "automatically.") +
            "<select id=\"mlTrainingJobPolicyId\" required>"
            "<option value=\"\">Choose a training job</option></select>"
            "</label>"
            "<label>Max runtime (seconds, 0 = no limit)" +
            field_hint("Genuinely enforced: a run that overshoots this is "
                      "aborted and the job marked failed with reason "
                      "&quot;timeout&quot;.") +
            "<input id=\"mlTrainingJobMaxRuntimeSeconds\" type=\"number\" "
            "min=\"0\" step=\"1\"></label>"
            "<label>Failure recovery" +
            field_hint("Genuinely enforced: retry once actually re-runs "
                      "training a second time before giving up if the "
                      "first attempt fails.") +
            "<select id=\"mlTrainingJobFailureRecoveryStrategy\">"
            "<option value=\"none\">None</option>"
            "<option value=\"retry_once\">Retry once</option></select></label>"
            "<label>Checkpoint frequency (epochs, 0 = use the job's own "
            "default)<input id=\"mlTrainingJobCheckpointFrequencyEpochs\" "
            "type=\"number\" min=\"0\" step=\"1\"></label>"
            "<label>Output directory<input "
            "id=\"mlTrainingJobOutputDirectory\"></label>"
            "<label>Compute target" +
            field_hint("Recorded for operator reference -- this build "
                      "always trains in-process on the host running "
                      "MasterAI; there is no container/cluster scheduler "
                      "for this field to actually target.") +
            "<input id=\"mlTrainingJobComputeTarget\"></label>"
            "<label>Hardware allocation<input "
            "id=\"mlTrainingJobHardwareAllocation\"></label>"
            "<label>Runtime environment<input "
            "id=\"mlTrainingJobRuntimeEnvironment\"></label>"
            "<label>Container image<input "
            "id=\"mlTrainingJobContainerImage\"></label>"
            "<label>Environment variables<textarea "
            "id=\"mlTrainingJobEnvironmentVariables\" rows=\"2\">"
            "</textarea></label>"
            "<label>Secrets references<textarea "
            "id=\"mlTrainingJobSecretsReferences\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Logging policy<input "
            "id=\"mlTrainingJobLoggingPolicy\"></label>"
            "<label>Notification policy<input "
            "id=\"mlTrainingJobNotificationPolicy\"></label>"
            "<label>Resource ceiling notes<input "
            "id=\"mlTrainingJobResourceCeilingNotes\"></label>"
            "<label>Cost ceiling notes<input "
            "id=\"mlTrainingJobCostCeilingNotes\"></label>"
            "<button title=\"Save execution policy\">" ICON_PLUS_SVG " Save execution policy</button></form>"
            "</div></section>";
    } else if (section == "ml-evaluation-runs") {
        // Phase 43 (docs/PLAN.md "Machine Learning Abilities" section 23):
        // create and list evaluation runs against a registered model and
        // benchmark dataset, and move them through a lifecycle status.
        // Phase 96 added a real, measured metric set beyond accuracy/F1/
        // MSE (latency, throughput, memory, stability, robustness, and an
        // optional bias/fairness breakdown by a named feature column) --
        // see TabularEvaluationMetrics's comment in masterai.hpp for
        // exactly how each is computed, and for the honest boundary on the
        // generative/LLM-only categories (hallucination rate, perplexity,
        // ...) this tabular evaluator does not compute.
        body =
            ml_step_flow(5, kMlPipelineSteps) +
            "<section id=\"panel-ml-evaluation-runs\" class=\"panel\"><div>"
            "<h2>Step 5 &mdash; New evaluation run</h2>"
            "<p class=\"mlPipelineNote\">Measures a trained model's real "
            "accuracy on a chosen dataset. Needs a model produced by "
            "Step 4's Train Now (or Model Registry &gt; Register a "
            "model).</p>"
            "<form id=\"newMlEvaluationRun\">"
            "<label>Model<select id=\"mlEvaluationRunModelId\" required>"
            "<option value=\"\">Choose a trained model</option></select></label>"
            "<label>Benchmark dataset<select id=\"mlEvaluationRunDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlEvaluationRunName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlEvaluationRunDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Category<input id=\"mlEvaluationRunCategory\" "
            "placeholder=\"e.g. accuracy, f1_score, hallucination_rate\">"
            "</label>"
            "<label>Sensitive feature for bias/fairness breakdown "
            "(optional)" +
            field_hint("A feature column name from this dataset, e.g. "
                      "&quot;region&quot; or a one-hot slot like "
                      "&quot;region=west&quot;. When set, the run's result "
                      "reports accuracy/fit separately for each value of "
                      "this column. Leave blank to skip -- this codebase "
                      "never guesses which column, if any, is sensitive.") +
            "<input id=\"mlEvaluationRunSensitiveFeatureName\"></label>"
            "<button title=\"Create evaluation run\">" ICON_PLUS_SVG " Create evaluation run</button></form>"
            "</div><div>"
            "<h2>Evaluation runs</h2>"
            "<div id=\"mlEvaluationRunsList\">Loading...</div>"
            // Phase 56: the real evaluation harness reports its genuine
            // metrics here after an "Evaluate now" or "View result" click.
            "<h2>Last evaluation result</h2>"
            "<p id=\"mlEvaluationRunResult\">No evaluation shown yet. Click "
            "\"Evaluate now\" on a run whose model has been trained.</p>"
            "</div></section>";
    } else if (section == "ml-experiments") {
        // Phase 44/80 (docs/PLAN.md "Machine Learning Abilities" section
        // 25): create and list experiments tying a registered project and
        // model (and optionally a dataset) together, edit the definition-
        // time fields section 25 lists (hyperparameters, seed, source/
        // configuration/container version, tags, notes), run them for real
        // (genuine training/validation/evaluation metrics -- see
        // execute_experiment_run in server.cpp), and compare two or more
        // side by side.
        body =
            ml_step_flow(5, kMlPipelineSteps) +
            "<section id=\"panel-ml-experiments\" class=\"panel\"><div>"
            "<h2>Step 5 (alternative) &mdash; New experiment</h2>"
            "<p class=\"mlPipelineNote\">A tracked, comparable alternative "
            "to a plain evaluation run &mdash; records the exact "
            "hyperparameters/seed/versions used so two runs can be "
            "compared side by side later.</p>"
            "<form id=\"newMlExperiment\">"
            "<label>Project<select id=\"mlExperimentProjectId\" required>"
            "<option value=\"\">Choose a project</option></select></label>"
            "<label>Model<select id=\"mlExperimentModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Dataset (optional)<select id=\"mlExperimentDatasetId\">"
            "<option value=\"\">No dataset</option></select></label>"
            "<label>Name<input id=\"mlExperimentName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlExperimentDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Hyperparameters (JSON, optional)"
            "<textarea id=\"mlExperimentHyperparameters\" rows=\"2\" "
            "placeholder=\"{&quot;epochs&quot;:200}\"></textarea></label>"
            "<label>Random seed (optional)<input type=\"number\" min=\"0\" "
            "id=\"mlExperimentRandomSeed\"></label>"
            "<label>Source-code version (optional)"
            "<input id=\"mlExperimentSourceCodeVersion\"></label>"
            "<label>Configuration version (optional)"
            "<input id=\"mlExperimentConfigurationVersion\"></label>"
            "<label>Container version (optional)"
            "<input id=\"mlExperimentContainerVersion\"></label>"
            "<label>Tags (comma-separated, optional)"
            "<input id=\"mlExperimentTags\"></label>"
            "<label>Notes (optional)<textarea id=\"mlExperimentNotes\" "
            "rows=\"2\"></textarea></label>"
            "<button title=\"Create experiment\">" ICON_PLUS_SVG " Create experiment</button></form>"
            "</div><div>"
            "<h2>Experiments</h2>"
            "<div id=\"mlExperimentsList\">Loading...</div>"
            "<h2>Last run result</h2>"
            "<p id=\"mlExperimentRunResult\">No run shown yet. Click \"Run "
            "now\" on an experiment whose dataset has uploaded content.</p>"
            "<h2>Compare experiments</h2>"
            "<form id=\"compareMlExperiments\">"
            "<label>Baseline experiment" +
            field_hint("Every other experiment's metric delta and "
                      "regression flag in the result below are computed "
                      "against this one -- it is always included in the "
                      "comparison even if not also checked as a "
                      "candidate.") +
            "<select id=\"mlExperimentCompareBaselineId\" required>"
            "<option value=\"\">Choose the baseline experiment</option>"
            "</select></label>"
            "<label>Candidate experiments" +
            field_hint("Every checked experiment is compared against the "
                      "baseline above in one run. An experiment needs a "
                      "completed \"Run now\" (real metrics captured) to "
                      "show a meaningful comparison -- one without a "
                      "result still appears in the table with \"Has "
                      "result: no\".") +
            "<div class=\"multiSelect\" id=\"mlExperimentCompareCandidateIds\">"
            "</div></label>"
            "<button title=\"Compare experiments\">Compare</button></form>"
            "<div id=\"mlExperimentCompareResult\"></div>"
            "</div></section>";
    } else if (section == "ml-fine-tuning-jobs") {
        // Phase 45 (docs/PLAN.md "Machine Learning Abilities" section 18):
        // create and list fine-tuning jobs against a registered base model
        // and fine-tuning dataset, and move them through a lifecycle
        // status. Only the identity/target-project/target-model/target-
        // dataset/method/status fields FineTuningJobStore actually
        // persists are collected here -- see that class's comment in
        // masterai.hpp for the adapter-method/hyperparameter/checkpoint/
        // output-model fields deferred to the phase that actually executes
        // a fine-tuning run.
        body =
            "<section id=\"panel-ml-fine-tuning-jobs\" class=\"panel\"><div>"
            "<h2>New fine-tuning job</h2>"
            "<form id=\"newMlFineTuningJob\">"
            "<label>Base model<select id=\"mlFineTuningJobModelId\" required>"
            "<option value=\"\">Choose a base model</option></select></label>"
            "<label>Fine-tuning dataset" +
            field_hint("Must show a row count in the Dataset Manager list "
                       "(content uploaded there first) -- a dataset with "
                       "no content has nothing to fine-tune on.") +
            "<select id=\"mlFineTuningJobDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Project (optional)<select id=\"mlFineTuningJobProjectId\">"
            "<option value=\"\">No project</option></select></label>"
            "<label>Name<input id=\"mlFineTuningJobName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea id=\"mlFineTuningJobDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Method" +
            field_hint("Free text describing the intent of this run, e.g. "
                       "subject_specialisation. Informational only -- it "
                       "does not select which fine-tuning technique "
                       "actually runs.") +
            "<input id=\"mlFineTuningJobMethod\" "
            "placeholder=\"e.g. subject_specialisation, code_assistant, "
            "safety_alignment\"></label>"
            "<button title=\"Create fine-tuning job\">" ICON_PLUS_SVG " Create fine-tuning job</button></form>"
            "</div><div>"
            "<h2>Fine-tuning jobs</h2>"
            "<div id=\"mlFineTuningJobsList\">Loading...</div>"
            // Phase 70: the real fine-tuning executor reports its genuine
            // result (method, final loss, held-out metrics, adapted model
            // id) here after a "Fine-tune now" click.
            "<h2>Last fine-tuning result</h2>"
            "<p id=\"mlFineTuningRunResult\">No fine-tuning run in this "
            "session yet. Click \"Fine-tune now\" on a job whose base model "
            "has been trained and whose dataset has uploaded CSV "
            "content.</p>"
            "</div></section>";
    } else if (section == "ml-model-builder-configs") {
        // Phase 46 (docs/PLAN.md "Machine Learning Abilities" section 9) at
        // full surface: create and list model builder configurations
        // against an optional project and optional base model, move them
        // through a design-time lifecycle status, and edit every build
        // setting from section 9's configuration list through the
        // "Configure build settings" form. That form provides the basic and
        // advanced configuration modes section 9 requires: basic shows the
        // six everyday fields, advanced additionally reveals the full
        // architecture/regularisation/scheduling/reproducibility surface.
        body =
            "<section id=\"panel-ml-model-builder-configs\" class=\"panel\">"
            "<div>"
            "<h2>New model builder configuration</h2>"
            "<form id=\"newMlModelBuilderConfig\">"
            "<label>Source type<input id=\"mlModelBuilderConfigSourceType\" "
            "required placeholder=\"e.g. template, imported_base_model, "
            "embedding_model\"></label>"
            "<label>Base model (optional)<select "
            "id=\"mlModelBuilderConfigBaseModelId\"><option value=\"\">"
            "No base model</option></select></label>"
            "<label>Project (optional)<select id=\"mlModelBuilderConfigProjectId\">"
            "<option value=\"\">No project</option></select></label>"
            "<label>Name<input id=\"mlModelBuilderConfigName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlModelBuilderConfigDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button title=\"Create configuration\">" ICON_PLUS_SVG " Create configuration</button></form>"
            "</div><div>"
            "<h2>Configure build settings</h2>"
            "<p id=\"mlMbcTarget\">Press Configure on a configuration in "
            "the list below to edit its build settings here.</p>"
            "<form id=\"configureMlModelBuilderConfig\">"
            "<p class=\"mlPipelineNote\">This build trains a real, "
            "from-scratch multi-layer-perceptron (MLP) on tabular rows -- "
            "not a transformer/sequence model. Fields below marked "
            "&quot;not used by this trainer&quot; are recorded for your "
            "own notes but do not change how training actually runs.</p>"
            "<label>Configuration mode<select id=\"mlMbcMode\">"
            "<option value=\"basic\">Basic</option>"
            "<option value=\"advanced\">Advanced</option></select></label>"
            "<label>Model architecture" +
            field_hint("A free-text label for your own reference, e.g. "
                       "&quot;tabular_mlp&quot;. Does not select the "
                       "network shape -- Layer configuration/Hidden "
                       "dimensions in Advanced mode do that.") +
            "<input id=\"mlMbcArchitecture\" "
            "placeholder=\"e.g. tabular_mlp\"></label>"
            "<label>Loss function" +
            field_hint("A free-text label for your own reference. The "
                       "trainer always picks its own loss automatically "
                       "from the dataset's target column -- this field "
                       "does not choose it.") +
            "<input id=\"mlMbcLossFunction\" "
            "placeholder=\"e.g. cross_entropy, mse\"></label>"
            "<label>Optimiser<input id=\"mlMbcOptimiser\" "
            "placeholder=\"e.g. adamw, sgd\"></label>"
            "<label>Batch size<input id=\"mlMbcBatchSize\" type=\"number\" "
            "min=\"0\" placeholder=\"0 = executor default\"></label>"
            "<label>Epoch count<input id=\"mlMbcEpochCount\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = executor default\">"
            "</label>"
            "<div id=\"mlMbcAdvanced\" style=\"display:none\">"
            "<label>Layer configuration" +
            field_hint("The real hidden-layer sizes, as any numbers found "
                       "in this text, e.g. &quot;128, 64&quot; or &quot;2 "
                       "hidden layers of 128 and 64&quot;. This genuinely "
                       "shapes the trained network.") +
            "<textarea "
            "id=\"mlMbcLayerConfiguration\" rows=\"2\" placeholder=\"e.g. "
            "128, 64\"></textarea></label>"
            "<label>Hidden dimensions" +
            field_hint("A single hidden-layer size, used only when Layer "
                       "configuration above is left blank. Also genuinely "
                       "shapes the trained network.") +
            "<input id=\"mlMbcHiddenDimensions\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = executor default\">"
            "</label>"
            "<label>Sequence length" +
            field_hint("Not used by this trainer -- sequence length is a "
                       "transformer/text-model concept and this build "
                       "trains a tabular MLP, one fixed-width row at a "
                       "time.") +
            "<input id=\"mlMbcSequenceLength\" "
            "type=\"number\" min=\"0\" placeholder=\"not used by this "
            "trainer\"></label>"
            "<label>Attention configuration" +
            field_hint("Not used by this trainer -- attention is a "
                       "transformer concept and this build trains a "
                       "tabular MLP, which has no attention layers.") +
            "<input "
            "id=\"mlMbcAttentionConfiguration\" placeholder=\"not used by "
            "this trainer\"></label>"
            "<label>Vocabulary and tokenizer" +
            field_hint("Not used by this trainer -- tokenization applies "
                       "to text/sequence models, not the fixed numeric/"
                       "categorical feature columns this build trains on.") +
            "<input "
            "id=\"mlMbcVocabularyTokenizer\" placeholder=\"not used by "
            "this trainer\"></label>"
            "<label>Activation functions<input "
            "id=\"mlMbcActivationFunctions\" placeholder=\"e.g. silu, "
            "gelu\"></label>"
            "<label>Dropout (0.0 to 1.0)<input id=\"mlMbcDropout\" "
            "type=\"number\" min=\"0\" max=\"1\" step=\"0.01\" "
            "placeholder=\"0 = disabled\"></label>"
            "<label>Initialisation strategy<input "
            "id=\"mlMbcInitialisationStrategy\" placeholder=\"e.g. xavier, "
            "kaiming\"></label>"
            "<label>Learning-rate scheduler<input "
            "id=\"mlMbcLearningRateScheduler\" placeholder=\"e.g. cosine "
            "with warmup\"></label>"
            "<label>Gradient accumulation steps<input "
            "id=\"mlMbcGradientAccumulation\" type=\"number\" min=\"0\" "
            "placeholder=\"0 = disabled\"></label>"
            "<label>Gradient clipping (max norm)<input "
            "id=\"mlMbcGradientClipping\" type=\"number\" min=\"0\" "
            "step=\"0.1\" placeholder=\"0 = disabled\"></label>"
            "<label>L1 regularization (lasso strength)" +
            field_hint("Penalizes large weights so the model prefers "
                       "smaller ones, driving some all the way to zero -- "
                       "useful when many input columns are irrelevant and "
                       "you want the model to effectively ignore them. "
                       "Never applied to bias terms.") +
            "<input id=\"mlMbcL1Regularization\" type=\"number\" min=\"0\" "
            "step=\"0.0001\" placeholder=\"0 = disabled\"></label>"
            "<label>L2 regularization (ridge strength)" +
            field_hint("Also penalizes large weights, but shrinks them "
                       "smoothly toward zero instead of forcing them to "
                       "exactly zero -- the usual first choice against "
                       "overfitting when training accuracy is much higher "
                       "than validation accuracy. Never applied to bias "
                       "terms.") +
            "<input id=\"mlMbcL2Regularization\" type=\"number\" min=\"0\" "
            "step=\"0.0001\" placeholder=\"0 = disabled\"></label>"
            "<label>Mixed precision" +
            field_hint("Not used by this trainer -- mixed-precision tensor "
                       "cores are a GPU/transformer-training concept; this "
                       "build's tabular MLP trainer always runs in plain "
                       "precision.") +
            "<input id=\"mlMbcMixedPrecision\" "
            "type=\"checkbox\"></label>"
            "<label>Checkpoint frequency (steps)<input "
            "id=\"mlMbcCheckpointFrequency\" type=\"number\" min=\"0\" "
            "placeholder=\"0 = executor default\"></label>"
            "<label>Validation frequency (steps)" +
            field_hint("Not used by this trainer -- it always validates "
                       "on a held-out split once per epoch, not on a "
                       "configurable step interval.") +
            "<input id=\"mlMbcValidationFrequency\" type=\"number\" min=\"0\" "
            "placeholder=\"not used by this trainer\"></label>"
            "<label>Early stopping<input id=\"mlMbcEarlyStopping\" "
            "type=\"checkbox\"></label>"
            "<label>Random seed<input id=\"mlMbcRandomSeed\" "
            "type=\"number\" min=\"0\" placeholder=\"0 = not fixed\">"
            "</label>"
            "<label>Reproducibility settings" +
            field_hint("Free-text notes only, e.g. &quot;pinned library "
                       "versions&quot; -- nothing reads this back to "
                       "change how training runs. Random seed above is "
                       "what actually makes a run reproducible.") +
            "<textarea "
            "id=\"mlMbcReproducibilitySettings\" rows=\"2\" "
            "placeholder=\"e.g. deterministic kernels, pinned library "
            "versions\"></textarea></label>"
            "<label>Distributed-training settings" +
            field_hint("Free-text notes only -- this build trains on a "
                       "single process, so there is nothing to actually "
                       "configure here yet.") +
            "<textarea "
            "id=\"mlMbcDistributedTrainingSettings\" rows=\"2\" "
            "placeholder=\"e.g. 2-node data parallel\"></textarea></label>"
            "</div>"
            "<button title=\"Save build settings\">" ICON_SAVE_SVG " Save build settings</button></form>"
            "</div><div>"
            "<h2>Model builder configurations</h2>"
            "<p class=\"mlPipelineNote\">To build (or rebuild) a real "
            "model from a configuration: set its status to "
            "&quot;submitted&quot; in the table below and press Apply. "
            "That creates a real training job from Layer configuration/"
            "Hidden dimensions/Optimiser/etc above, which then trains like "
            "any Training Jobs page entry -- open Training Jobs and click "
            "&quot;Train now&quot; on it to actually run it.</p>"
            "<div id=\"mlModelBuilderConfigsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-instruction-examples") {
        // Phase 47/81 (docs/PLAN.md "Machine Learning Abilities" section
        // 19): create and list instruction examples against a registered
        // dataset, edit each one's full section-19 content record (system/
        // user instruction, context, expected/rejected response, tool
        // calls/results, output format, difficulty, safety
        // classification), generate a real draft via a model, test an
        // example's content against multiple models for real, check for
        // duplicates/contradictions, and validate structured output --
        // see execute_rag_generation/validate_structured_output/
        // detect_duplicate_instruction_examples/
        // detect_contradictory_instruction_examples for the real logic
        // behind each of these.
        body =
            "<section id=\"panel-ml-instruction-examples\" class=\"panel\">"
            "<div>"
            "<h2>New instruction example</h2>"
            "<form id=\"newMlInstructionExample\">"
            "<label>Dataset<select id=\"mlInstructionExampleDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlInstructionExampleName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlInstructionExampleDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Subject classification<input "
            "id=\"mlInstructionExampleSubjectClassification\" "
            "placeholder=\"e.g. cpp_code_review, customer_support\"></label>"
            "<button title=\"Create instruction example\">" ICON_PLUS_SVG " Create instruction example</button></form>"
            "</div><div>"
            "<h2>Instruction examples</h2>"
            "<div id=\"mlInstructionExamplesList\">Loading...</div>"
            "</div><div>"
            "<h2>Collect from conversations (Continual Learning)</h2>"
            "<p>Scans your own real chat conversations and turns qualifying "
            "turns into draft instruction examples below, for you to review "
            "the normal way -- nothing is ever added to an approved dataset "
            "or used to train a model automatically.</p>"
            "<form id=\"newMlContinualLearningCollect\">"
            "<label>Target dataset" +
            field_hint("Which dataset the collected drafts are registered "
                       "against. They still start as drafts either way -- "
                       "this only decides where an approved one would "
                       "eventually be added.") +
            "<select id=\"mlContinualLearningDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Minimum quality score to keep (0.0 to 1.0)" +
            field_hint("Turns scoring below this are skipped automatically "
                       "-- for example an empty, very short, or refusal-"
                       "shaped reply. 0.4 is a reasonable default.") +
            "<input id=\"mlContinualLearningQualityFloor\" type=\"number\" "
            "min=\"0\" max=\"1\" step=\"0.05\" value=\"0.4\"></label>"
            "<button title=\"Collect candidates\">" ICON_PLAY_SVG " Collect candidates</button></form>"
            "<p id=\"mlContinualLearningResult\"></p>"
            "</div><div>"
            "<h2>Edit content</h2>"
            "<form id=\"mlInstructionExampleContentForm\">"
            "<label>Instruction example" +
            field_hint("Which instruction example's full content (system/"
                      "user instruction, expected response, ...) this form "
                      "edits. Clicking the Configure (gear) button on a "
                      "row in the Instruction examples table below fills "
                      "this in automatically -- you rarely need to pick it "
                      "here by hand.") +
            "<select id=\"mlInstructionExampleContentId\" required>"
            "<option value=\"\">Choose an instruction example</option>"
            "</select></label>"
            "<label>System instruction<textarea "
            "id=\"mlInstructionExampleSystemInstruction\" rows=\"2\">"
            "</textarea></label>"
            "<label>User instruction<textarea "
            "id=\"mlInstructionExampleUserInstruction\" rows=\"2\">"
            "</textarea></label>"
            "<label>Context<textarea id=\"mlInstructionExampleContext\" "
            "rows=\"2\"></textarea></label>"
            "<label>Expected response<textarea "
            "id=\"mlInstructionExampleExpectedResponse\" rows=\"3\">"
            "</textarea></label>"
            "<label>Rejected response<textarea "
            "id=\"mlInstructionExampleRejectedResponse\" rows=\"3\">"
            "</textarea></label>"
            "<label>Tool calls (JSON)" +
            field_hint("Optional. The tool/function calls the model is "
                      "expected to make while producing this example's "
                      "response, as a JSON array -- e.g. "
                      "[{\"name\":\"search\",\"arguments\":{\"query\":"
                      "\"...\"}}]. Leave blank for an example that involves "
                      "no tool use.") +
            "<textarea "
            "id=\"mlInstructionExampleToolCallsJson\" rows=\"2\">"
            "</textarea></label>"
            "<label>Tool results (JSON)" +
            field_hint("Optional. The results those tool calls returned, "
                      "as a JSON array, paired positionally with Tool "
                      "calls above -- what the model actually saw before "
                      "producing Expected response.") +
            "<textarea "
            "id=\"mlInstructionExampleToolResultsJson\" rows=\"2\">"
            "</textarea></label>"
            "<label>Required output format" +
            field_hint("Free text naming the response shape a grader "
                      "should check for, e.g. json, markdown, plain, or a "
                      "specific schema name. Only informational here -- "
                      "the actual JSON-shape check runs from the \"Test "
                      "against models\" and \"Validate\" actions below, "
                      "not automatically from this field.") +
            "<input "
            "id=\"mlInstructionExampleRequiredOutputFormat\" "
            "placeholder=\"e.g. json, markdown, plain\"></label>"
            "<label>Difficulty" +
            field_hint("Free text, e.g. easy, medium, hard, or expert. "
                      "Used only to help a human reviewer prioritize and "
                      "filter examples -- no automated behavior reads "
                      "this field.") +
            "<input id=\"mlInstructionExampleDifficulty\">"
            "</label>"
            "<label>Safety classification" +
            field_hint("Free text describing this example's safety "
                      "sensitivity, e.g. benign, sensitive_topic, or "
                      "refusal_required. Recorded for review and audit "
                      "purposes -- it does not feed the Safety and "
                      "Governance content scanner, which runs its own "
                      "independent pattern/model-classifier checks.") +
            "<input "
            "id=\"mlInstructionExampleSafetyClassification\"></label>"
            "<button title=\"Save content\">Save content</button></form>"
            "<p id=\"mlInstructionExampleContentStatus\"></p>"
            "</div><div>"
            "<h2>Generate draft</h2>"
            "<form id=\"mlInstructionExampleGenerateForm\">"
            "<label>Dataset<select id=\"mlInstructionExampleGenerateDatasetId\" "
            "required><option value=\"\">Choose a dataset</option></select>"
            "</label>"
            "<label>Model<select id=\"mlInstructionExampleGenerateModelId\" "
            "required><option value=\"\">Choose a model</option></select>"
            "</label>"
            "<label>Name (optional)<input "
            "id=\"mlInstructionExampleGenerateName\" maxlength=\"160\"></label>"
            "<label>System instruction<textarea "
            "id=\"mlInstructionExampleGenerateSystemInstruction\" rows=\"2\">"
            "</textarea></label>"
            "<label>User instruction<textarea "
            "id=\"mlInstructionExampleGenerateUserInstruction\" rows=\"2\">"
            "</textarea></label>"
            "<label>Context<textarea "
            "id=\"mlInstructionExampleGenerateContext\" rows=\"2\">"
            "</textarea></label>"
            "<button title=\"Generate draft\">" ICON_PLUS_SVG " Generate draft</button></form>"
            "<p id=\"mlInstructionExampleGenerateStatus\">Generated drafts "
            "start as \"draft\" and need review before approval.</p>"
            "</div><div>"
            "<h2>Test against models</h2>"
            "<form id=\"mlInstructionExampleTestForm\">"
            "<label>Instruction example" +
            field_hint("Which example's system/user instruction and "
                      "context get sent to each chosen model below; its "
                      "expected response is what the models' real replies "
                      "get compared against.") +
            "<select id=\"mlInstructionExampleTestId\" required>"
            "<option value=\"\">Choose an instruction example</option>"
            "</select></label>"
            "<label>Models to test against" +
            field_hint("Every checked model gets a real generation call "
                      "with this example's content, and the result is "
                      "compared against Expected response. Check as many "
                      "as you want to compare side by side in one run.") +
            "<div class=\"multiSelect\" id=\"mlInstructionExampleTestModelIds\">"
            "</div></label>"
            "<button title=\"Test against models\">Test</button></form>"
            "<div id=\"mlInstructionExampleTestResult\"></div>"
            "</div><div>"
            "<h2>Duplicates and contradictions</h2>"
            "<form id=\"mlInstructionExampleCheckForm\">"
            "<label>Dataset" +
            field_hint("Scans every instruction example belonging to this "
                      "dataset for near-duplicate content and for pairs "
                      "whose instructions look the same but expect "
                      "contradictory responses -- both are real heuristic "
                      "checks, not exact-text matching only.") +
            "<select id=\"mlInstructionExampleCheckDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<button data-check=\"duplicates\" "
            "title=\"Check duplicates\">Check duplicates</button> "
            "<button data-check=\"contradictions\" "
            "title=\"Check contradictions\">Check contradictions</button>"
            "</form>"
            "<div id=\"mlInstructionExampleCheckResult\"></div>"
            "</div></section>";
    } else if (section == "ml-synthetic-records") {
        // Phase 48 (docs/PLAN.md "Machine Learning Abilities" section 20):
        // create and list synthetic records, and move them through the
        // same reviewer-approval lifecycle status Prompt and Instruction
        // Training uses. The Deployment Manager/Inference Endpoints/
        // Synthetic Data completion phase adds the "Generate" form below,
        // a real generation executor (POST .../synthetic-records/generate)
        // that invokes a model and stores generator model/version/prompt/
        // settings/generated text/confidence score via
        // SyntheticRecordContentStore -- see that class's comment in
        // masterai.hpp.
        body =
            "<section id=\"panel-ml-synthetic-records\" class=\"panel\">"
            "<div>"
            "<h2>New synthetic record</h2>"
            "<p class=\"mlPipelineNote\">This creates an empty record with "
            "no generated text yet -- use Generate synthetic record below "
            "instead if you want MasterAI to actually write the content "
            "with a real model.</p>"
            "<form id=\"newMlSyntheticRecord\">"
            "<label>Dataset<select id=\"mlSyntheticRecordDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Name<input id=\"mlSyntheticRecordName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlSyntheticRecordDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Generation technique<input "
            "id=\"mlSyntheticRecordGenerationTechnique\" "
            "placeholder=\"e.g. paraphrase, edge_case, counterexample\">"
            "</label>"
            "<button title=\"Create synthetic record\">" ICON_PLUS_SVG " Create synthetic record</button></form>"
            "</div><div>"
            "<h2>Synthetic records</h2>"
            "<div id=\"mlSyntheticRecordsList\">Loading...</div>"
            "</div><div>"
            "<h2>Generate synthetic record</h2>"
            "<form id=\"mlSyntheticRecordGenerateForm\">"
            "<label>Dataset<select id=\"mlSyntheticRecordGenerateDatasetId\" "
            "required><option value=\"\">Choose a dataset</option></select>"
            "</label>"
            "<label>Model<select id=\"mlSyntheticRecordGenerateModelId\" "
            "required><option value=\"\">Choose a model</option></select>"
            "</label>"
            "<label>Name (optional)<input "
            "id=\"mlSyntheticRecordGenerateName\" maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlSyntheticRecordGenerateDescription\" rows=\"2\">"
            "</textarea></label>"
            "<label>Generation technique<select "
            "id=\"mlSyntheticRecordGenerateTechnique\">"
            "<option value=\"alternative_questions\">Alternative questions</option>"
            "<option value=\"paraphrase\">Paraphrase</option>"
            "<option value=\"example\">Example</option>"
            "<option value=\"counterexample\">Counterexample</option>"
            "<option value=\"difficult_case\">Difficult case</option>"
            "<option value=\"malformed_input\">Malformed input</option>"
            "<option value=\"edge_case\">Edge case</option>"
            "<option value=\"balanced_class_sample\">Balanced-class sample</option>"
            "<option value=\"code_sample\">Code sample</option>"
            "<option value=\"unit_test_case\">Unit-test case</option>"
            "<option value=\"simulated_conversation\">Simulated conversation</option>"
            "<option value=\"image_variation\">Image variation</option>"
            "<option value=\"tabular_record\">Tabular record</option>"
            "</select></label>"
            "<label>Source text<textarea "
            "id=\"mlSyntheticRecordGenerateSourceText\" rows=\"3\">"
            "</textarea></label>"
            "<button title=\"Generate synthetic record\">" ICON_PLUS_SVG " Generate</button></form>"
            "<p id=\"mlSyntheticRecordGenerateStatus\">Generated records "
            "start as \"draft\" and need review before approval.</p>"
            "</div></section>";
    } else if (section == "ml-vector-stores") {
        // Phases 49/61 (Machine Learning Abilities section 21):
        // register and list vector stores, and move them through the same
        // three-state pending/approved/rejected approval workflow Dataset
        // Manager uses, since a vector store is a standalone registered
        // resource rather than a target-scoped content record. Only the
        // identity/embedding-model/distance-metric/status fields
        // VectorStoreStore actually persists are collected here -- see that
        // class's comment in masterai.hpp for the document-import/chunking/
        // index profile is execution evidence from Phases 59/61; the form
        // now offers the authored method plus verified ready embedding GGUFs.
        body =
            "<section id=\"panel-ml-vector-stores\" class=\"panel\">"
            "<div>"
            "<h2>New vector store</h2>"
            "<p class=\"mlPipelineNote\">A vector store is a searchable "
            "index you fill by ingesting knowledge files (Subject "
            "Knowledge Manager) or dataset rows -- creating one here just "
            "registers it, empty, ready to receive content.</p>"
            "<form id=\"newMlVectorStore\">"
            "<label>Name<input id=\"mlVectorStoreName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlVectorStoreDescription\" rows=\"2\"></textarea></label>"
            "<label>Embedding method<select id=\"mlVectorStoreEmbeddingModel\">"
            "<option value=\"authored_hashing_vectorizer_v1\">MasterAI "
            "authored hashing vectorizer v1 (128 dimensions)</option>"
            "</select></label>"
            "<label>Distance metric<select id=\"mlVectorStoreDistanceMetric\">"
            "<option value=\"cosine\">Cosine similarity</option>"
            "</select></label>"
            "<button title=\"Create vector store\">" ICON_PLUS_SVG " Create vector store</button></form>"
            "</div><div>"
            "<h2>Vector stores</h2>"
            "<div id=\"mlVectorStoresList\">Loading...</div>"
            "<h3>Index profile</h3><p id=\"mlVectorIndexResult\">Select "
            "View index on a vector store to inspect its populated index.</p>"
            "</div></section>";
    } else if (section == "ml-rag-configs") {
        // Phase 50 (docs/PLAN.md "Machine Learning Abilities" section 22):
        // register and list RAG configurations, and move them through the
        // same three-state pending/approved/rejected approval workflow
        // Vector Stores use, since a RAG configuration is a standalone
        // registered resource rather than a target-scoped content record.
        // Only the identity/search-strategy/vector-store-reference/status
        // fields RagConfigStore actually persists are collected here -- see
        // that class's comment in masterai.hpp for the retrieval-testing/
        // reranking/citation fields deferred to the phase that actually
        // generates retrieval results.
        body =
            "<section id=\"panel-ml-rag-configs\" class=\"panel\">"
            "<div>"
            "<h2>New RAG configuration</h2>"
            "<p class=\"mlPipelineNote\">A new configuration starts "
            "&quot;pending&quot; and must be approved (status dropdown "
            "below) before Test retrieval will accept it.</p>"
            "<form id=\"newMlRagConfig\">"
            "<label>Name<input id=\"mlRagConfigName\" required "
            "maxlength=\"160\"></label>"
            "<label>Description<textarea "
            "id=\"mlRagConfigDescription\" rows=\"2\"></textarea></label>"
            "<label>Search strategy<select id=\"mlRagConfigSearchStrategy\">"
            "<option value=\"hybrid\">Hybrid vector + keyword</option>"
            "<option value=\"vector\">Vector only</option>"
            "<option value=\"keyword\">Keyword only</option>"
            "</select></label>"
            "<label>Vector store<select id=\"mlRagConfigVectorStoreId\" required>"
            "<option value=\"\">Choose a vector store</option></select></label>"
            "<button title=\"Create RAG configuration\">" ICON_PLUS_SVG " Create RAG configuration</button></form>"
            "<h2>Test retrieval</h2>"
            "<form id=\"mlRagQueryForm\">"
            "<label>Approved RAG configuration<select id=\"mlRagQueryConfigId\" required>"
            "<option value=\"\">Choose an approved RAG configuration</option>"
            "</select></label>"
            "<label>Question<textarea id=\"mlRagQueryText\" rows=\"3\" "
            "required maxlength=\"8192\"></textarea></label>"
            "<label>Retrieved chunks<input id=\"mlRagQueryTopK\" type=\"number\" "
            "min=\"1\" max=\"20\" value=\"5\" required></label>"
            "<button title=\"Retrieve grounded context\">" ICON_PLAY_SVG " Retrieve grounded context</button></form>"
            "</div><div>"
            "<h2>RAG configurations</h2>"
            "<div id=\"mlRagConfigsList\">Loading...</div>"
            "<h3>Retrieval evidence</h3><pre id=\"mlRagQueryResult\">No query "
            "executed yet.</pre>"
            "</div></section>";
    } else if (section == "ml-subject-exams") {
        // Phase 51 (docs/PLAN.md "Machine Learning Abilities" section 24):
        // create and list subject exams and move them through the same
        // five-state reviewer-approval workflow Instruction Examples use,
        // since an exam is authored content a reviewer approves before it
        // may examine anything. Only the identity/subject-reference/
        // question-format/status fields SubjectExamStore actually persists
        // are collected here -- see that class's comment in masterai.hpp
        // for the question-bank/scoring fields deferred to the phase that
        // actually administers exams.
        body =
            "<section id=\"panel-ml-subject-exams\" class=\"panel\">"
            "<div>"
            "<h2>New subject exam</h2>"
            "<p class=\"mlPipelineNote\">Running an exam grades each "
            "answer with a real model acting as judge, falling back to a "
            "plain text-overlap check only if the judge call fails. Each "
            "result records which of the two graded it, and only an "
            "overall pass/fail score is produced today.</p>"
            "<form id=\"newMlSubjectExam\">"
            "<label>Name<input id=\"mlSubjectExamName\" required "
            "maxlength=\"160\"></label>"
            "<label>Subject package<select id=\"mlSubjectExamSubjectId\" required>"
            "<option value=\"\">Choose a subject</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlSubjectExamDescription\" rows=\"2\"></textarea></label>"
            "<label>Question format<input "
            "id=\"mlSubjectExamQuestionFormat\" "
            "placeholder=\"e.g. multiple_choice, short_answer, code_task\">"
            "</label>"
            "<button title=\"Create subject exam\">" ICON_PLUS_SVG " Create subject exam</button></form>"
            "</div><div>"
            "<h2>Subject exams</h2>"
            "<div id=\"mlSubjectExamsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-hyperparameter-searches") {
        // Phase 52 (docs/PLAN.md "Machine Learning Abilities" section 26):
        // create and list hyperparameter searches and move them through the
        // same eleven-state job lifecycle Training Jobs use, since a search
        // queues, runs, and pauses like any other job. Only the identity/
        // training-job-reference/strategy/status fields
        // HyperparameterSearchStore actually persists are collected here --
        // see that class's comment in masterai.hpp for the search-space/
        // trial-history fields deferred to the phase that actually runs
        // searches.
        body =
            "<section id=\"panel-ml-hyperparameter-searches\" "
            "class=\"panel\">"
            "<div>"
            "<h2>New hyperparameter search</h2>"
            "<form id=\"newMlHyperparameterSearch\">"
            "<label>Name<input id=\"mlHyperparameterSearchName\" required "
            "maxlength=\"160\"></label>"
            "<label>Training job" +
            field_hint("The search reuses this job's dataset and, when "
                       "it has a real model-builder MLP architecture, "
                       "also sweeps its batch size/dropout/optimiser -- "
                       "otherwise only learning rate and epoch count are "
                       "swept.") +
            "<select id=\"mlHyperparameterSearchTrainingJobId\" required>"
            "<option value=\"\">Choose a training job</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlHyperparameterSearchDescription\" rows=\"2\">"
            "</textarea></label>"
            "<label>Search strategy" +
            field_hint("All three run a real search over learning rate/"
                       "epochs (plus batch size/dropout/optimiser for MLP "
                       "jobs), capped at 20 trials. Grid: evenly spaced "
                       "values across the whole range -- simple and "
                       "predictable. Random: independent random draws -- "
                       "often finds a good setting faster than grid when "
                       "you only have a small trial budget. Bayesian: "
                       "learns from each trial's result and picks the next "
                       "one it expects to do best -- usually the most "
                       "sample-efficient choice, but each trial takes a "
                       "little longer to choose.") +
            "<select id=\"mlHyperparameterSearchStrategy\">"
            "<option value=\"grid\">Grid (evenly spaced)</option>"
            "<option value=\"random\">Random (independent draws)</option>"
            "<option value=\"bayesian\">Bayesian (learns as it goes)"
            "</option></select>"
            "</label>"
            "<button title=\"Create hyperparameter search\">" ICON_PLUS_SVG " Create hyperparameter search</button></form>"
            "</div><div>"
            "<h2>Hyperparameter searches</h2>"
            "<div id=\"mlHyperparameterSearchesList\">Loading...</div>"
            "<p id=\"mlHyperparameterSearchRunResult\"></p>"
            "</div></section>";
    } else if (section == "ml-ensembles") {
        // Ensemble Methods (2026-08-24): create and list ensembles and
        // move them through the same eleven-state job lifecycle every
        // job-like entity in this family uses. "Run now" invokes the real
        // executor (run_ensemble(), server.cpp): it genuinely trains and
        // combines real members, scores the combination against a genuine
        // single-model baseline on the same held-out split, and shows the
        // real result below the table.
        body =
            "<section id=\"panel-ml-ensembles\" class=\"panel\">"
            "<div>"
            "<h2>New ensemble</h2>"
            "<form id=\"newMlEnsemble\">"
            "<label>Name<input id=\"mlEnsembleName\" required "
            "maxlength=\"160\"></label>"
            "<label>Training job" +
            field_hint("The ensemble reuses this job's dataset and "
                       "architecture -- each member is trained the same "
                       "way a single run of this job would be.") +
            "<select id=\"mlEnsembleTrainingJobId\" required>"
            "<option value=\"\">Choose a training job</option></select></label>"
            "<label>Description<textarea id=\"mlEnsembleDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Method" +
            field_hint("Bagging: trains several models on different "
                       "random resamples of the data and averages their "
                       "predictions -- reduces variance, a safe default. "
                       "Boosting: trains models one after another, each "
                       "one correcting the previous ones' mistakes -- can "
                       "reach higher accuracy but is more prone to "
                       "overfitting a small dataset. Stacking: trains "
                       "several different models, then trains a small "
                       "final model that learns how to best combine "
                       "their predictions.") +
            "<select id=\"mlEnsembleMethod\">"
            "<option value=\"bagging\">Bagging</option>"
            "<option value=\"boosting\">Boosting</option>"
            "<option value=\"stacking\">Stacking</option>"
            "</select></label>"
            "<label>Member count (1 to 20)" +
            field_hint("How many models to train and combine. More "
                       "members can improve the result but takes longer "
                       "to run -- 5 is a reasonable starting point.") +
            "<input id=\"mlEnsembleMemberCount\" type=\"number\" min=\"1\" "
            "max=\"20\" value=\"5\"></label>"
            "<button title=\"Create ensemble\">" ICON_PLUS_SVG " Create ensemble</button></form>"
            "</div><div>"
            "<h2>Ensembles</h2>"
            "<div id=\"mlEnsemblesList\">Loading...</div>"
            "<p id=\"mlEnsembleRunResult\"></p>"
            "</div></section>";
    } else if (section == "ml-model-optimizations") {
        // Phase 53 (docs/PLAN.md "Machine Learning Abilities" section 28):
        // create and list model optimization runs and move them through the
        // same eleven-state job lifecycle Training Jobs use, since an
        // optimization run executes like any other job. Only the identity/
        // model-reference/operation/status fields ModelOptimizationStore
        // actually persists are collected here -- see that class's comment
        // in masterai.hpp for the before/after-comparison fields deferred
        // to the phase that actually optimizes models.
        body =
            "<section id=\"panel-ml-model-optimizations\" class=\"panel\">"
            "<div>"
            "<h2>New model optimization</h2>"
            "<form id=\"newMlModelOptimization\">"
            "<label>Name<input id=\"mlModelOptimizationName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlModelOptimizationModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlModelOptimizationDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Operation" +
            field_hint("Only &quot;pruning&quot; actually runs a real "
                       "optimization pass on the model's weights. "
                       "quantization/distillation/graph_optimization and "
                       "other values are recorded but have no executor "
                       "yet, so \"Run now\" will fail with &quot;has no "
                       "real executor yet&quot;.") +
            "<input id=\"mlModelOptimizationOperation\" "
            "placeholder=\"pruning (the only operation that actually runs)\">"
            "</label>"
            "<button title=\"Create model optimization\">" ICON_PLUS_SVG " Create model optimization</button></form>"
            "</div><div>"
            "<h2>Model optimization runs</h2>"
            "<div id=\"mlModelOptimizationsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-checkpoints") {
        // Phase 54 (docs/PLAN.md "Machine Learning Abilities" section 33):
        // record and list training checkpoints and move them through a
        // bespoke three-state retention lifecycle (active/pinned/archived)
        // -- pinned is section 33's own "protect" operation, exempting a
        // checkpoint from retention deletion. Phase 79 closed the step/
        // epoch/resume gap this class comment used to name: a checkpoint
        // captured by a real training or fine-tuning run now carries its
        // real epoch and a real learned-weight snapshot ("Snapshot" column
        // below), and "Resume training" continues gradient descent from it.
        // A manually-created checkpoint record (the form below) has neither,
        // since there is no weight state to attach to an administrator's
        // own note.
        body =
            "<section id=\"panel-ml-checkpoints\" class=\"panel\">"
            "<div>"
            "<h2>New checkpoint record</h2>"
            "<p class=\"mlPipelineNote\">A checkpoint created here manually "
            "is a note only, with no saved weights to resume from. A real, "
            "resumable checkpoint (with actual learned weights) is instead "
            "captured automatically by a real training or fine-tuning "
            "run -- look for one with a Snapshot in the list below, and "
            "use its &quot;Resume training&quot; action to continue "
            "gradient descent from it.</p>"
            "<form id=\"newMlCheckpoint\">"
            "<label>Name<input id=\"mlCheckpointName\" required "
            "maxlength=\"160\"></label>"
            "<label>Training job<select id=\"mlCheckpointTrainingJobId\" required>"
            "<option value=\"\">Choose a training job</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlCheckpointDescription\" rows=\"2\"></textarea></label>"
            "<label>Capture reason<input id=\"mlCheckpointCaptureReason\" "
            "placeholder=\"e.g. epoch_end, best_metric, manual\"></label>"
            "<button title=\"Create checkpoint record\">" ICON_PLUS_SVG " Create checkpoint record</button></form>"
            "</div><div>"
            "<h2>Checkpoints</h2>"
            "<div id=\"mlCheckpointsList\">Loading...</div>"
            "<p id=\"mlCheckpointResumeResult\"></p>"
            "</div></section>";
    } else if (section == "ml-deployments") {
        // Phase 55 (docs/PLAN.md "Machine Learning Abilities" section 34):
        // record and list deployments and move them through the same
        // three-state pending/approved/rejected approval workflow Vector
        // Stores use, since section 34 explicitly names approval as part of
        // the deployment record. The Deployment Manager/Inference
        // Endpoints/Synthetic Data completion phase adds the real "Deploy
        // now"/"Rollback" row actions (gated on an approved ModelCard, with
        // a real health signal and supersede/rollback tracking) -- see
        // DeploymentStore's class comment in masterai.hpp and
        // renderMlDeployments above.
        body =
            "<section id=\"panel-ml-deployments\" class=\"panel\">"
            "<div>"
            "<h2>New deployment</h2>"
            "<form id=\"newMlDeployment\">"
            "<label>Name<input id=\"mlDeploymentName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlDeploymentModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlDeploymentDescription\" rows=\"2\"></textarea></label>"
            "<label>Environment" +
            field_hint("A deployment supersedes the previous active "
                       "deployment for this same environment name -- "
                       "matching text here, e.g. &quot;production&quot;, "
                       "is what makes Rollback find its predecessor.") +
            "<input id=\"mlDeploymentEnvironment\" "
            "placeholder=\"e.g. development, staging, production\"></label>"
            "<label>Strategy" +
            field_hint("Free text for your own record-keeping -- every "
                       "deployment goes live the same way regardless of "
                       "this value; there is no separate blue-green/canary "
                       "rollout executor yet.") +
            "<input id=\"mlDeploymentStrategy\" "
            "placeholder=\"e.g. direct, blue_green, canary\"></label>"
            "<button title=\"Create deployment\">" ICON_PLUS_SVG " Create deployment</button></form>"
            "</div><div>"
            "<h2>Deployments</h2>"
            "<p>\"Deploy now\" requires an approved model card for the "
            "deployment's model (Safety and Governance). \"Rollback\" "
            "appears once a deployment has superseded an earlier one for "
            "the same environment.</p>"
            "<div id=\"mlDeploymentsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-model-comparisons") {
        // Phase 57 (docs/PLAN.md "Machine Learning Abilities" section 27):
        // a REAL executor page like Training Jobs' "Train now" -- "Compare
        // now" evaluates both trained artifacts against the shared
        // benchmark dataset and reports the measured winner. Every field is
        // labeled with what it must reference so the form stays
        // self-explanatory.
        body =
            "<section id=\"panel-ml-model-comparisons\" class=\"panel\">"
            "<div>"
            "<h2>New model comparison</h2>"
            "<form id=\"newMlModelComparison\">"
            "<label>Name<input id=\"mlModelComparisonName\" required "
            "maxlength=\"160\"></label>"
            "<label>Baseline model<select id=\"mlModelComparisonBaselineModelId\" required>"
            "<option value=\"\">Choose the baseline model</option></select></label>"
            "<label>Candidate model<select id=\"mlModelComparisonCandidateModelId\" required>"
            "<option value=\"\">Choose the candidate model</option></select></label>"
            "<label>Benchmark dataset<select id=\"mlModelComparisonDatasetId\" required>"
            "<option value=\"\">Choose a dataset</option></select></label>"
            "<label>Description<textarea "
            "id=\"mlModelComparisonDescription\" rows=\"2\"></textarea>"
            "</label>"
            "<button title=\"Create model comparison\">" ICON_PLUS_SVG " Create model comparison</button></form>"
            "</div><div>"
            "<h2>Model comparisons</h2>"
            "<div id=\"mlModelComparisonsList\">Loading...</div>"
            // Phase 57: the real comparison verdict renders here after a
            // "Compare now" or "View result" click.
            "<h3>Comparison result</h3>"
            "<p id=\"mlModelComparisonResult\">No comparison shown yet. "
            "Click \"Compare now\" on a comparison whose two models have "
            "both been trained.</p>"
            "</div></section>";
    } else if (section == "ml-inference-endpoints") {
        // Phase 62/77 (docs/PLAN.md "Machine Learning Abilities" section
        // 35): a real network listener opens while an endpoint is `active`
        // (run_inference_endpoint in server.cpp), enforcing real Bearer
        // auth, a real per-minute rate limit, and live per-request safety
        // policy -- see InferenceEndpoint's class comment in masterai.hpp.
        // The Deployment Manager/Inference Endpoints/Synthetic Data
        // completion phase adds the "Bearer token" field below (the API
        // already accepted authToken; the form was simply missing it) and
        // the policy-editing form, both previously API-only.
        body =
            "<section id=\"panel-ml-inference-endpoints\" class=\"panel\">"
            "<div>"
            "<h2>New inference endpoint</h2>"
            "<form id=\"newMlInferenceEndpoint\">"
            "<label>Name<input id=\"mlEndpointName\" required "
            "maxlength=\"160\"></label>"
            "<label>Model<select id=\"mlEndpointModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Runtime" +
            field_hint("Free text naming the inference backend this "
                      "endpoint's listener runs against, e.g. llama.cpp. "
                      "Informational -- MasterAI's own listener "
                      "(run_inference_endpoint) is what actually serves "
                      "requests once this endpoint is active.") +
            "<input id=\"mlEndpointRuntime\" "
            "placeholder=\"e.g. llama.cpp\"></label>"
            "<label>Host" +
            field_hint("The network interface the real listener binds to "
                      "when this endpoint is set active, e.g. 127.0.0.1 "
                      "for loopback-only, or 0.0.0.0 to accept "
                      "connections from other machines.") +
            "<input id=\"mlEndpointHost\" "
            "placeholder=\"e.g. 127.0.0.1\"></label>"
            "<label>Port" +
            field_hint("The TCP port the real listener binds to. Must be "
                      "free on the host and not already used by another "
                      "active endpoint or by MasterAI's own web server.") +
            "<input id=\"mlEndpointPort\" type=\"number\" min=\"0\" "
            "max=\"65535\"></label>"
            "<label>Protocol" +
            field_hint("Free text naming the wire protocol clients use "
                      "against this endpoint, e.g. rest, websocket, mcp. "
                      "Informational -- the real listener always speaks "
                      "POST /v1/completions regardless of this value.") +
            "<input id=\"mlEndpointProtocol\" "
            "placeholder=\"e.g. rest, websocket, mcp\"></label>"
            "<label>Authentication method" +
            field_hint("Free text naming how callers authenticate, e.g. "
                      "api_token. Leave blank or set to \"none\" only if "
                      "you deliberately want this endpoint to accept "
                      "unauthenticated requests -- otherwise Bearer token "
                      "below is required and actually enforced.") +
            "<input "
            "id=\"mlEndpointAuthenticationMethod\" "
            "placeholder=\"e.g. api_token\"></label>"
            "<label>Bearer token (required unless authentication method is "
            "blank or \"none\")" +
            field_hint("The exact value a caller must send as "
                      "\"Authorization: Bearer <token>\" on every request "
                      "to this endpoint's real listener. Stored and "
                      "checked server-side -- there is no way to view it "
                      "again after saving, so keep a copy somewhere safe.") +
            "<input id=\"mlEndpointAuthToken\" "
            "type=\"password\" autocomplete=\"new-password\"></label>"
            "<label>Rate limit, requests per minute" +
            field_hint("The real per-minute cap the listener enforces "
                      "once this endpoint is active; a caller exceeding it "
                      "gets a real 429 response. 0 or blank means no "
                      "limit.") +
            "<input "
            "id=\"mlEndpointRateLimit\" type=\"number\" min=\"0\"></label>"
            "<button title=\"Create inference endpoint\">" ICON_PLUS_SVG " Create inference endpoint</button></form>"
            "</div><div>"
            "<h2>Inference endpoints</h2>"
            "<p>Only an <code>active</code> endpoint's listener is running. "
            "Requests are served at <code>POST /v1/completions</code> "
            "against the endpoint's host/port.</p>"
            "<div id=\"mlInferenceEndpointsList\">Loading...</div>"
            "</div><div>"
            "<h2>Endpoint policy</h2>"
            "<form id=\"mlEndpointPolicyForm\">"
            "<label>Inference endpoint" +
            field_hint("Which endpoint this policy applies to. Re-read "
                      "live on every request the endpoint's real listener "
                      "serves -- saving a change here takes effect on that "
                      "endpoint's very next request, no restart required.") +
            "<select id=\"mlEndpointPolicyId\" required>"
            "<option value=\"\">Choose an inference endpoint</option>"
            "</select></label>"
            "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
            "id=\"mlEndpointPolicyContentScanEnabled\" checked> Content "
            "scan enabled" +
            field_hint("Runs the real heuristic content scanner (secret-"
                      "shaped tokens, prompt-injection phrasing, and this "
                      "policy's own restricted data categories) over every "
                      "prompt and answer this endpoint handles.") +
            "</label>"
            "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
            "id=\"mlEndpointPolicyBlockOnScanFinding\" checked> Block "
            "request on scan finding" +
            field_hint("If the content scan above finds something in the "
                      "incoming prompt, refuse the request outright "
                      "instead of letting it reach the model.") +
            "</label>"
            "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
            "id=\"mlEndpointPolicyBlockAnswerOnScanFinding\"> Block answer "
            "on scan finding" +
            field_hint("If the content scan finds something in the "
                      "model's generated answer, withhold that answer from "
                      "the caller instead of returning it.") +
            "</label>"
            "<label>Safety policy (optional)" +
            field_hint("Attaches a Safety and Governance policy so this "
                      "endpoint's content scan also checks that policy's "
                      "own restricted data categories, not just the "
                      "built-in secret/prompt-injection patterns. Leave "
                      "unset to scan with only the built-in patterns.") +
            "<select id=\"mlEndpointPolicySafetyPolicyId\">"
            "<option value=\"\">None / choose a safety policy</option>"
            "</select></label>"
            "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
            "id=\"mlEndpointPolicyModelClassifierEnabled\"> Model "
            "classifier enabled" +
            field_hint("Also runs a real LLM-as-judge classifier (the "
                      "locally loaded model rating text for bias, "
                      "hallucination risk, and harmful content) alongside "
                      "the heuristic scan above. Slower, since it makes an "
                      "extra generation call per request; a failed/"
                      "unavailable classifier never blocks a request the "
                      "heuristic scan already passed.") +
            "</label>"
            "<label>Model classifier confidence floor" +
            field_hint("Only classifier findings at or above this "
                      "confidence (0 to 1, as reported by the judge model) "
                      "count as a finding. Higher values mean fewer, "
                      "more-confident flags; lower values catch more but "
                      "risk more false positives.") +
            "<input "
            "id=\"mlEndpointPolicyConfidenceFloor\" type=\"number\" min=\"0\" "
            "max=\"1\" step=\"0.05\" value=\"0.5\"></label>"
            "<button title=\"Save policy\">Save policy</button></form>"
            "<p id=\"mlEndpointPolicyStatus\"></p>"
            "</div></section>";
    } else if (section == "ml-compute-nodes") {
        // Phase 63 (docs/PLAN.md "Machine Learning Abilities" section 30):
        // a static compute-node registry. Phase 67 adds real live telemetry
        // for whichever single node is flagged as this MasterAI process's
        // own host -- see ComputeNode's class comment in masterai.hpp for
        // why a remote node still cannot be polled.
        body =
            "<section id=\"panel-ml-compute-nodes\" class=\"panel\">"
            "<div>"
            "<h2>New compute node</h2>"
            "<form id=\"newMlComputeNode\">"
            "<label>Name<input id=\"mlComputeNodeName\" required "
            "maxlength=\"160\"></label>"
            "<label>Address<input id=\"mlComputeNodeAddress\" "
            "placeholder=\"e.g. 127.0.0.1 or hostname\"></label>"
            "<label>Operating system<input "
            "id=\"mlComputeNodeOperatingSystem\"></label>"
            "<label>CPU description<input id=\"mlComputeNodeCpuDescription\">"
            "</label>"
            "<label>GPU description<input id=\"mlComputeNodeGpuDescription\">"
            "</label>"
            "<label>System memory, MiB<input id=\"mlComputeNodeMemoryMib\" "
            "type=\"number\" min=\"0\"></label>"
            "<label class=\"checkboxLabel\">"
            "<input type=\"checkbox\" id=\"mlComputeNodeIsLocal\">"
            " This is the local node MasterAI is running on (enables live "
            "CPU/RAM/GPU telemetry)</label>"
            "<button title=\"Create compute node\">" ICON_PLUS_SVG " Create compute node</button></form>"
            "</div><div>"
            "<h2>Compute nodes</h2>"
            "<div id=\"mlComputeNodesList\">Loading...</div>"
            "<div id=\"mlComputeNodeTelemetry\"></div>"
            "</div></section>";
    } else if (section == "ml-automation-pipelines") {
        // Phase 64/69/71 (docs/PLAN.md "Machine Learning Abilities" section
        // 37): a pipeline names an ordered subset of the section's sixteen
        // lifecycle stages. "Run" genuinely executes "Train model",
        // "Evaluate model", "Validate data", "Validate model", "Safety
        // tests", "Request approval", "Deploy staging", "Deploy
        // production", "Rollback", and "Monitor" (matched case-
        // insensitively); every other named stage is honestly recorded as
        // skipped -- see AutomationPipeline's class comment in
        // masterai.hpp. A run executes on a background thread and reports
        // live per-stage progress while it works.
        body =
            "<section id=\"panel-ml-automation-pipelines\" class=\"panel\">"
            "<div>"
            "<h2>New automation pipeline</h2>"
            "<form id=\"newMlAutomationPipeline\">"
            "<label>Name<input id=\"mlPipelineName\" required "
            "maxlength=\"160\"></label>"
            "<label>Project<select id=\"mlPipelineProjectId\">"
            "<option value=\"\">None / choose a project</option></select>"
            "</label>"
            "<label>Description<textarea id=\"mlPipelineDescription\" "
            "rows=\"2\"></textarea></label>"
            "<label>Stages" +
            field_hint("Every checked stage runs, in the fixed order "
                      "listed here (top to bottom), on Run -- this "
                      "codebase does not support reordering them "
                      "independently of this list. Each one is a real "
                      "executor, not a placeholder: Train model/Evaluate "
                      "model run the same real tabular engine as Training "
                      "Jobs/Evaluation Lab, Safety tests checks for an "
                      "approved Model Card, Deploy staging/Deploy "
                      "production/Rollback create and approve/revoke a "
                      "real Deployment record, and so on -- see the "
                      "Automation Pipelines page notes for the full list. "
                      "Label data reports itself skipped unless a "
                      "completed labeling task already exists for the "
                      "dataset, since this codebase has no automated "
                      "labeler.") +
            "<div class=\"multiSelect\">" +
            [] {
                static const char* const stages[] = {
                    "Import data", "Validate data", "Clean data",
                    "Label data", "Split data", "Train model",
                    "Validate model", "Evaluate model", "Safety tests",
                    "Optimize", "Request approval", "Deploy staging",
                    "Staging tests", "Deploy production", "Monitor",
                    "Rollback"};
                std::string html;
                for (const char* stage : stages) {
                    html += "<label><input type=\"checkbox\" "
                            "class=\"mlPipelineStage\" value=\"";
                    html += stage;
                    html += "\"> ";
                    html += stage;
                    html += "</label>";
                }
                return html;
            }() +
            "</div></label>"
            "<label>Training/evaluation dataset (for Train model / Evaluate "
            "model / Validate data stages)<select id=\"mlPipelineDatasetId\">"
            "<option value=\"\">None / choose a dataset</option></select>"
            "</label>"
            "<label>Starting model, optional (for stages that need a model "
            "with no prior Train model stage)<select id=\"mlPipelineModelId\">"
            "<option value=\"\">None / choose a model</option></select>"
            "</label>"
            "<button title=\"Create automation pipeline\">" ICON_PLUS_SVG " Create automation pipeline</button></form>"
            "</div><div>"
            "<h2>Automation pipelines</h2>"
            "<div id=\"mlAutomationPipelinesList\">Loading...</div>"
            "<div id=\"mlPipelineRunDetail\"></div>"
            "</div></section>";
    } else if (section == "ml-safety-governance") {
        // Phase 65 (docs/PLAN.md "Machine Learning Abilities" section 40):
        // a governance policy plus per-model disclosure cards -- see
        // SafetyPolicy/ModelCard's class comment in masterai.hpp for the
        // content-scanning fields this scoped-down registry defers.
        body =
            "<section id=\"panel-ml-safety-governance\" class=\"panel\">"
            "<div>"
            "<h2>New safety policy</h2>"
            "<form id=\"newMlSafetyPolicy\">"
            "<label>Name<input id=\"mlSafetyPolicyName\" required "
            "maxlength=\"160\"></label>"
            "<label>Scope<input id=\"mlSafetyPolicyScope\" "
            "placeholder=\"e.g. project name or 'global'\"></label>"
            "<label>Restricted data categories" +
            field_hint("Real, in effect: attach this policy to an "
                       "Inference Endpoint (Endpoint policy, on the "
                       "Inference Endpoints page) and its content scanner "
                       "checks every prompt/answer for these categories, "
                       "on top of its built-in secret/prompt-injection "
                       "patterns.") +
            "<textarea "
            "id=\"mlSafetyPolicyRestrictedDataCategories\" rows=\"2\" "
            "placeholder=\"e.g. personal information, credentials, "
            "copyrighted text\"></textarea></label>"
            "<button title=\"Create safety policy\">" ICON_PLUS_SVG " Create safety policy</button></form>"
            "</div><div>"
            "<h2>Safety policies</h2>"
            "<div id=\"mlSafetyPoliciesList\">Loading...</div>"
            "</div>"
            "<div>"
            "<h2>New model card</h2>"
            "<p class=\"mlPipelineNote\">Real, in effect: a model needs an "
            "approved model card here before Deployment Manager's "
            "&quot;Deploy now&quot; will accept it.</p>"
            "<form id=\"newMlModelCard\">"
            "<label>Model<select id=\"mlModelCardModelId\" required>"
            "<option value=\"\">Choose a model</option></select></label>"
            "<label>Purpose<textarea id=\"mlModelCardPurpose\" rows=\"2\" "
            "required></textarea></label>"
            "<label>Intended use<textarea id=\"mlModelCardIntendedUse\" "
            "rows=\"2\"></textarea></label>"
            "<label>Prohibited use<textarea id=\"mlModelCardProhibitedUse\" "
            "rows=\"2\"></textarea></label>"
            "<label>Training data reference<input "
            "id=\"mlModelCardTrainingDataReference\"></label>"
            "<label>Evaluation results<textarea "
            "id=\"mlModelCardEvaluationResults\" rows=\"2\"></textarea>"
            "</label>"
            "<label>Known limitations<textarea "
            "id=\"mlModelCardKnownLimitations\" rows=\"2\"></textarea>"
            "</label>"
            "<label>License<input id=\"mlModelCardLicense\"></label>"
            "<button title=\"Create model card\">" ICON_PLUS_SVG " Create model card</button></form>"
            "</div><div>"
            "<h2>Model cards</h2>"
            "<div id=\"mlModelCardsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-monitoring") {
        // Phase 68 (docs/PLAN.md "Machine Learning Abilities" section 44):
        // a read-only aggregation over data other real phases already
        // measured -- no new store, no fabricated numbers. See the
        // /api/v1/ml/monitoring handler's own comment in server.cpp for the
        // exact boundary between what is real here and what remains
        // planned.
        body =
            "<section id=\"panel-ml-monitoring\" class=\"panel\">"
            "<div id=\"mlMonitoringPanel\">"
            "<h2>System resources</h2>"
            "<p>Live CPU/RAM/GPU/disk for the host this MasterAI process is "
            "running on, probed fresh on every page load.</p>"
            "<p id=\"mlMonitoringSystem\">Loading...</p>"
            "<h2>Training job status</h2>"
            "<p>Real status counts from every registered training job.</p>"
            "<div id=\"mlMonitoringTrainingJobs\">Loading...</div>"
            "<h2>Evaluation results</h2>"
            "<p>Genuinely measured metrics from completed Evaluation Lab "
            "runs.</p>"
            "<div id=\"mlMonitoringEvaluations\">Loading...</div>"
            "<h2>Inference throughput (benchmarks)</h2>"
            "<p>Real prompt/generation tokens-per-second from actual "
            "Benchmark runs -- the closest measured inference-performance "
            "evidence this build has. Not live production request "
            "telemetry: requests-per-second, latency percentiles, queue "
            "depth, cache-hit rate, safety-filter rate, tool-call success, "
            "retrieval latency, and model-loading time remain planned, "
            "since no request path in this codebase is currently "
            "instrumented to measure them.</p>"
            "<div id=\"mlMonitoringBenchmarks\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-audit-logs") {
        // Phase 66 (docs/PLAN.md "Machine Learning Abilities" section 43):
        // read-only over the AuditLog every ml.* mutation already writes to
        // -- there is no create/edit form here by design.
        body =
            "<section id=\"panel-ml-audit-logs\" class=\"panel\">"
            "<div>"
            "<h2>Machine Learning audit log</h2>"
            "<p>The most recent 200 recorded Machine Learning administrator "
            "actions, newest first. Every ml.* action across every "
            "interface on this page is recorded here automatically -- "
            "there is nothing to configure.</p>"
            "<div id=\"mlAuditLogsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "ml-settings") {
        // Machine Learning Settings (docs/PLAN.md "Machine Learning
        // Abilities" section 49): reuses the exact #systemConfigForm/
        // #systemConfigStatus machinery the general System Configuration
        // page (settings-config, above) uses -- renderSystemConfig()/
        // submitSystemConfig() read and write by data-path regardless of
        // which subset of fields a given page shows, so this page can
        // scope to only the ML-relevant fields without new JS. Fields not
        // shown here are preserved unchanged on save.
        body =
            "<section id=\"panel-ml-settings\" class=\"panel\">"
            "<div><h2>Machine Learning settings</h2>"
            "<p>The Machine Learning subset of the System Configuration "
            "page. See Settings &gt; System Configuration for every other "
            "server setting.</p>"
            // Field hint bubbles preference (ML forms clarity pass): a
            // personal per-browser display toggle, not server
            // configuration, so it deliberately lives outside
            // #systemConfigForm and its Save button -- unchecking it
            // applies immediately via its own 'change' listener (see
            // applyHintsPref() and this checkbox's wiring near the bottom
            // of the shared JS), with no save round-trip and no effect on
            // any other browser or administrator.
            "<label class=\"checkboxLabel\"><input type=\"checkbox\" "
            "id=\"cfgHintsEnabled\" checked> Show field hints (the '?' "
            "bubbles next to form fields across every Machine Learning "
            "page) -- unchecking this is saved to this browser only"
            "</label>"
            "<form id=\"systemConfigForm\">"
            "<label>Tabular dataset upload limit, bytes &mdash; the "
            "Dataset Manager's CSV content-upload cap"
            "<input id=\"cfgTabularDatasetMaximumCsvBytes\" type=\"number\" "
            "min=\"1\" "
            "data-path=\"machineLearning.tabularDatasetMaximumCsvBytes\">"
            "</label>"
            "<label>Knowledge document upload limit, bytes &mdash; the "
            "Subject Knowledge Manager's ingestion cap"
            "<input id=\"cfgKnowledgeMaximumDocumentBytes\" type=\"number\" "
            "min=\"1\" data-path=\"knowledge.maximumDocumentBytes\"></label>"
            "<label>Parquet helper executable (restart required) &mdash; "
            "the DuckDB CLI path used to convert Parquet knowledge and "
            "dataset uploads; leave empty to disable Parquet ingestion"
            "<input id=\"cfgParquetHelperExecutable\" type=\"text\" "
            "data-path=\"knowledge.parquetHelperExecutable\"></label>"
            "<button title=\"Save Machine Learning settings\">" ICON_SAVE_SVG " Save Machine Learning settings</button>"
            "</form>"
            "<p id=\"systemConfigStatus\" role=\"status\"></p>"
            "</div></section>";
    } else if (section == "settings-allowed-commands") {
        // Phase 84 follow-up: admin UI for AllowedCommandStore (tool_exec.cpp)
        // -- backed by GET/POST /api/v1/chat-tools/allowed-commands and POST
        // .../remove (server.cpp). Every registered, enabled entry here is an
        // executable the chat/MCP run_command tool is permitted to invoke,
        // so this list is the single point of control for what an AI model
        // can ever run on this machine -- destructive commands still always
        // pause for a separate per-call Approve/Deny click regardless of
        // what's approved here (see classify_tool_call_risk()); this only
        // controls which executables are reachable at all.
        // The Windows/Linux/cross-platform command catalog itself is seeded
        // server-side (AllowedCommandStore::ensure_default_catalog(),
        // tool_exec.cpp) enabled by default -- this form only needs to
        // cover add and edit for a single record, plus an OS filter since
        // the seeded catalog makes the list long.
        body =
            "<section id=\"panel-settings-allowed-commands\" class=\"panel\">"
            "<div><h2>Run-command tool allow-list</h2>"
            "<p>Executables the chat/MCP <code>run_command</code> tool may "
            "invoke. Nothing not listed here can ever be run by a model, no "
            "matter what it asks for. A destructive command (delete, "
            "format, a forced git reset, ...) still always pauses for your "
            "explicit Approve/Deny before it runs, even if its executable "
            "is approved below. A built-in catalog of common Windows and "
            "Linux commands is pre-loaded below and enabled by default -- "
            "use Edit to disable anything you don't want the model to be "
            "able to run.</p>"
            "<form id=\"newAllowedCommand\">"
            "<h3 id=\"allowedCommandFormTitle\">Approve a new command</h3>"
            "<label>Executable &mdash; full path or name resolved on PATH, "
            "matched exactly (no wildcards)"
            "<input id=\"allowedCommandExecutable\" type=\"text\" "
            "maxlength=\"4096\" required></label>"
            "<label>Description"
            "<input id=\"allowedCommandDescription\" type=\"text\" "
            "maxlength=\"256\"></label>"
            "<label>Operating system"
            "<select id=\"allowedCommandOs\">"
            "<option value=\"both\">Windows &amp; Linux</option>"
            "<option value=\"windows\">Windows</option>"
            "<option value=\"linux\">Linux</option>"
            "</select></label>"
            "<label>Default risk"
            "<select id=\"allowedCommandRisk\">"
            "<option value=\"safe\">safe (runs immediately unless a "
            "destructive pattern is detected)</option>"
            "<option value=\"high_risk\">high_risk (always pauses for "
            "approval)</option>"
            "</select></label>"
            "<label>Restrict to project IDs (optional, comma-separated -- "
            "leave empty to allow every project)"
            "<input id=\"allowedCommandProjects\" type=\"text\" "
            "maxlength=\"4096\"></label>"
            "<label class=\"checkboxLabel\"><input id=\"allowedCommandEnabled\" "
            "type=\"checkbox\" checked> Enabled -- run_command may invoke "
            "this executable immediately</label>"
            "<div class=\"allowedCommandFormButtons\">"
            "<button id=\"allowedCommandSubmit\" title=\"Approve command\">"
            ICON_SAVE_SVG " Approve command</button>"
            "<button id=\"allowedCommandCancelEdit\" type=\"button\" "
            "hidden>Cancel edit</button>"
            "</div>"
            "</form>"
            "<p id=\"allowedCommandStatus\" role=\"status\"></p>"
            "<label class=\"allowedCommandFilter\">Filter by OS"
            "<select id=\"allowedCommandOsFilter\">"
            "<option value=\"all\">All</option>"
            "<option value=\"windows\">Windows</option>"
            "<option value=\"linux\">Linux</option>"
            "<option value=\"both\">Windows &amp; Linux</option>"
            "</select></label>"
            "<div id=\"allowedCommandsList\">Loading...</div>"
            "</div></section>";
    } else if (section == "settings-config") {
        // Phase 30A: administrator-only local-configuration editor backed by
        // GET/POST /api/v1/admin/config (server.cpp's admin_config_get()/
        // admin_config_put()) -- the same settings.json `masterai configure`
        // writes. Every input carries data-path="section.field" matching
        // that JSON document's own shape; application_script()'s
        // renderSystemConfig()/submitSystemConfig() read/write by that path
        // instead of one hand-written line per field. Fields not shown here
        // (host/port, TLS material, storage roots, allow-lists, ...) are
        // preserved unchanged -- the submit handler overlays only the
        // visible fields onto the full document it originally loaded.
        body =
            "<section id=\"panel-settings-config\" class=\"panel\">"
            "<div><h2>System configuration</h2>"
            "<p>Local server configuration. Changes are written to "
            "settings.json immediately; fields marked (restart required) "
            "only take effect the next time MasterAI is started.</p>"
            "<form id=\"systemConfigForm\">"
            "<h3>Hardware</h3>"
            "<label>Accelerator policy"
            "<select id=\"cfgAcceleratorPolicy\" "
            "data-path=\"hardware.acceleratorPolicy\">"
            "<option value=\"auto\">auto (use GPU when available)</option>"
            "<option value=\"cpu_only\">cpu_only (no GPU allocation)</option>"
            "<option value=\"gpu_allowed\">gpu_allowed</option>"
            "</select></label>"
            "<h3>Memory (restart required)</h3>"
            "<label>Resource profile"
            "<select id=\"cfgResourceProfile\" data-path=\"memory.profile\">"
            "<option value=\"minimal\">minimal (one request, smallest "
            "footprint)</option>"
            "<option value=\"balanced\">balanced</option>"
            "<option value=\"performance\">performance</option>"
            "</select></label>"
            "<label>Hard memory limit, MiB (0 = automatic)"
            "<input id=\"cfgMemoryHardLimitMiB\" type=\"number\" min=\"0\" "
            "max=\"1048576\" data-path=\"memory.hardLimitMiB\"></label>"
            "<label>Minimum free RAM percent"
            "<input id=\"cfgMinimumFreePercent\" type=\"number\" min=\"0\" "
            "max=\"50\" data-path=\"memory.minimumFreePercent\"></label>"
            "<label>Critical pressure percent"
            "<input id=\"cfgCriticalPercent\" type=\"number\" min=\"80\" "
            "max=\"99\" data-path=\"memory.criticalPressurePercent\"></label>"
            "<h3>Inference</h3>"
            "<label>Chat context length, tokens" +
            field_hint("The server rejects anything above 1,048,576 "
                      "tokens -- this is a hard policy ceiling, not a "
                      "model-specific limit.") +
            "<input id=\"cfgChatContextLength\" type=\"number\" min=\"1\" "
            "max=\"1048576\" data-path=\"inference.chatContextLength\">"
            "</label>"
            "<label>Chat maximum reply tokens" +
            field_hint("The server rejects anything above 32,768 tokens "
                      "-- this is a hard policy ceiling, not a model-"
                      "specific limit.") +
            "<input id=\"cfgChatMaxReplyTokens\" type=\"number\" min=\"1\" "
            "max=\"32768\" data-path=\"inference.chatMaxReplyTokens\">"
            "</label>"
            "<label>Runner startup timeout, seconds (restart required)"
            "<input id=\"cfgStartupTimeout\" type=\"number\" min=\"1\" "
            "max=\"3600\" data-path=\"inference.startupTimeoutSeconds\">"
            "</label>"
            "<label>Runner stall timeout, seconds"
            "<input id=\"cfgStallTimeout\" type=\"number\" min=\"1\" "
            "max=\"3600\" data-path=\"inference.stallTimeoutSeconds\">"
            "</label>"
            "<h3>Storage</h3>"
            "<label>PageFile location (restart required) &mdash; a folder "
            "MasterAI uses instead of the system pagefile/temp area for its "
            "own disk-backed cache and model data; leave empty to use the "
            "default cache folder"
            "<input id=\"cfgPageFileRoot\" type=\"text\" "
            "placeholder=\"(default cache folder)\" "
            "data-path=\"storage.pageFileRoot\"></label>"
            "<h3>Topology</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgNumaPlacement\" "
            "type=\"checkbox\" data-path=\"topology.numaLocalPlacementEnabled\" "
            "data-type=\"bool\"> Pin worker threads to the nearest NUMA "
            "node</label>" +
            field_hint("On multi-socket/multi-NUMA-node hardware, keeps "
                      "each connection's worker thread on the same memory "
                      "node as the model it is serving, reducing cross-node "
                      "memory latency. Has no effect on single-node "
                      "hardware. Also requires the \"NUMA affinity\" "
                      "advanced optimization to be admitted under "
                      "Performance &rarr; Benchmarks &amp; Regression first "
                      "-- this checkbox alone does not enable pinning "
                      "without real per-host benchmark evidence.") +
            "<h3>Model tiering (routing)</h3>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgModelRoutingEnabled\" type=\"checkbox\" "
            "data-path=\"modelRouting.enabled\" data-type=\"bool\"> "
            "Tiered auto-routing enabled</label>" +
            field_hint("When on and at least one tier below has a model "
                      "assigned, chats can select an \"Auto (Tiered)\" "
                      "model option that routes each message to the "
                      "cheapest tier that can answer well, escalating to a "
                      "larger tier only when the smaller one's answer looks "
                      "unreliable. Existing chats pinned to a specific "
                      "model are never affected by this setting.") +
            "<label>Tier assignments (JSON: tier name &rarr; list of model "
            "IDs)" +
            field_hint("Valid tier names: deterministic_processing, "
                      "compact_router, small_fast, medium_general, "
                      "large_specialist. Each is a list of downloaded model "
                      "IDs (see Models &rarr; Inventory) that may serve that "
                      "tier, cheapest/fastest tier first. A tier left out "
                      "or empty is simply never selected -- routing always "
                      "falls back to a chat's own pinned model when no tier "
                      "can satisfy a request.") +
            "<textarea id=\"cfgModelTierAssignments\" rows=\"6\" "
            "data-path=\"modelRouting.tiers\" data-type=\"json\" "
            "placeholder=\"{&quot;small_fast&quot;:[&quot;...&quot;],"
            "&quot;large_specialist&quot;:[&quot;...&quot;]}\"></textarea>"
            "</label>"
            "<h3>Retrieval</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgRetrievalEnabled\" "
            "type=\"checkbox\" data-path=\"retrieval.enabled\" "
            "data-type=\"bool\"> Retrieval enabled</label>"
            "<label>Retrieval deadline, milliseconds"
            "<input id=\"cfgRetrievalDeadline\" type=\"number\" min=\"1\" "
            "data-path=\"retrieval.deadlineMilliseconds\"></label>"
            "<label>Retrieval maximum context, bytes"
            "<input id=\"cfgRetrievalMaxContext\" type=\"number\" min=\"1\" "
            "data-path=\"retrieval.maximumContextBytes\"></label>"
            "<label>Retrieval maximum chunks per source"
            "<input id=\"cfgRetrievalMaxChunksPerSource\" type=\"number\" "
            "min=\"1\" data-path=\"retrieval.maximumChunksPerSource\"></label>"
            "<label>Retrieval maximum total chunks"
            "<input id=\"cfgRetrievalMaxTotalChunks\" type=\"number\" "
            "min=\"1\" data-path=\"retrieval.maximumTotalChunks\"></label>"
            "<label class=\"checkboxLabel\"><input id=\"cfgRetrievalMcpResource\" "
            "type=\"checkbox\" data-path=\"retrieval.mcpResourceEnabled\" "
            "data-type=\"bool\"> Use connected MCP tools/agents to help "
            "answer questions" +
            field_hint("When on, retrieval can call out to any outbound "
                       "MCP server this MasterAI instance is already "
                       "connected to (Settings &gt; Allowed Commands/"
                       "Outbound Connections) as one more evidence source "
                       "alongside the local project index -- effectively "
                       "letting another connected AI agent or tool help "
                       "find the answer. Off by default is safer if you "
                       "don't want outbound calls during retrieval; on by "
                       "default otherwise, matching every prior "
                       "installation's existing behavior.") +
            "</label>"
            "<label class=\"checkboxLabel\"><input id=\"cfgRetrievalSemanticEmbedding\" "
            "type=\"checkbox\" data-path=\"retrieval.semanticEmbeddingEnabled\" "
            "data-type=\"bool\"> Semantic embedding search enabled</label>"
            "<label class=\"checkboxLabel\"><input id=\"cfgRetrievalGitDiff\" "
            "type=\"checkbox\" data-path=\"retrieval.gitDiffEnabled\" "
            "data-type=\"bool\"> Git diff evidence enabled</label>"
            "<h3>Cache</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgCacheEnabled\" "
            "type=\"checkbox\" data-path=\"cache.enabled\" "
            "data-type=\"bool\"> Retrieval/prompt cache enabled</label>"
            "<h3>Session reuse</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgSessionEnabled\" "
            "type=\"checkbox\" data-path=\"session.enabled\" "
            "data-type=\"bool\"> Prompt-prefix/KV session reuse enabled"
            "</label>"
            "<label>Maximum reusable slots (restart required)"
            "<input id=\"cfgSessionMaxSlots\" type=\"number\" min=\"1\" "
            "data-path=\"session.maxSlots\"></label>"
            "<label>Idle retention, seconds (restart required)"
            "<input id=\"cfgSessionIdleRetention\" type=\"number\" min=\"1\" "
            "data-path=\"session.idleRetentionSeconds\"></label>"
            "<h3>Performance</h3>"
            "<label class=\"checkboxLabel\"><input id=\"cfgAutoTune\" "
            "type=\"checkbox\" data-path=\"performance.autoTune\" "
            "data-type=\"bool\"> Automatic calibration enabled</label>"
            "<h3>Sign-in</h3>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgAuthenticationEnabled\" type=\"checkbox\" "
            "data-path=\"auth.enabled\" data-type=\"bool\"> Require sign-in "
            "(on = normal, off = no login required)" +
            field_hint("Turning this off lets any request use this "
                      "MasterAI instance without signing in at all -- no "
                      "username, password, or API token. Only allowed "
                      "while the server is bound to loopback (127.0.0.1 / "
                      "localhost); MasterAI refuses to start with this off "
                      "on any other host, so it can never expose an "
                      "unauthenticated instance to the network.") +
            "</label>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgAllowLocalPasswordAccounts\" type=\"checkbox\" "
            "data-path=\"auth.allowLocalPasswordAccounts\" "
            "data-type=\"bool\"> Allow locally stored password accounts"
            "</label>"
            "<label class=\"checkboxLabel\"><input "
            "id=\"cfgAllowOsIdentityAccounts\" type=\"checkbox\" "
            "data-path=\"auth.allowOsIdentityAccounts\" data-type=\"bool\"> "
            "Allow OS-integrated sign-in</label>"
            "<h3>Server</h3>"
            "<label>Rate limit, requests per minute"
            "<input id=\"cfgRateLimit\" type=\"number\" min=\"1\" "
            "data-path=\"server.rateLimitPerMinute\"></label>"
            "<label>Maximum request size, bytes"
            "<input id=\"cfgMaxRequestBytes\" type=\"number\" min=\"1024\" "
            "data-path=\"server.maxRequestBytes\"></label>"
            "<button title=\"Save configuration\">" ICON_SAVE_SVG " Save configuration</button>"
            "</form>"
            "<p id=\"systemConfigStatus\" role=\"status\"></p>"
            "</div></section>";
    } else if (section == "report-system") {
        body =
            "<section id=\"panel-report-system\" class=\"panel\">"
            "<div><h2>System Report</h2>"
            "<p>A consolidated, point-in-time read of this MasterAI "
            "instance: hardware, memory pressure, the system pagefile, the "
            "MasterAI-scoped virtual PageFile, and which features are "
            "currently enabled.</p>"
            "<div id=\"systemReport\">Loading...</div>"
            "</div></section>";
    } else if (section == "performance") {
        body =
            "<section id=\"panel-performance\" class=\"panel\">"
            "<div><h2>Performance</h2>"
            "<p>Live visibility and manual override for the local runner "
            "pool (Phase 33), the intranet worker pool (Phase 33), the "
            "adaptive performance controller (Phase 34), memory (Phase 14), "
            "caches (Phase 17), storage tiers and tier migrations (Phase 31), "
            "the request scheduler (Phase 13), advanced optimizations "
            "(Phase 20), and calibration profiles (Phase 19). Every figure "
            "below comes directly from the same routes those phases expose "
            "-- nothing here is a separately maintained display-only value. "
            "Query Traces, Runner Configuration, Model Comparison, "
            "Benchmarks, and Regression History are not shown here yet -- no "
            "dedicated telemetry route exists for those.</p>"
            "<div class=\"reportSection\"><h3>Adaptive controller</h3>"
            "<div id=\"perfAdaptive\">Loading...</div>"
            "<form id=\"perfModeForm\"><label>Mode<select id=\"perfMode\">"
            "<option value=\"automatic\">Automatic</option>"
            "<option value=\"balanced\">Balanced</option>"
            "<option value=\"minimal_memory\">Minimal Memory</option>"
            "<option value=\"lowest_latency\">Lowest Latency</option>"
            "<option value=\"maximum_throughput\">Maximum Throughput</option>"
            "<option value=\"battery_saver\">Battery Saver</option>"
            "<option value=\"quiet_thermal_conservative\">"
            "Quiet/Thermal Conservative</option>"
            "<option value=\"administrator_custom\">Administrator Custom"
            "</option></select></label>"
            "<button type=\"submit\" title=\"Apply performance mode\">" ICON_SAVE_SVG
            " Apply mode</button>"
            "<button type=\"button\" id=\"perfRollback\" "
            "title=\"Revert the most recent automatic change\">Rollback last "
            "change</button></form>"
            "<details><summary>Ceilings (advanced)</summary>"
            "<form id=\"perfCeilingsForm\">"
            "<label>Maximum simultaneous generations" +
            field_hint("The most inference requests the adaptive controller "
                      "may ever let run at once, however much spare memory "
                      "there is.") +
            "<input id=\"ceilMaxInferenceConcurrency\" type=\"number\" "
            "min=\"1\"></label>"
            "<label>Maximum queued generations" +
            field_hint("How many more requests may wait in line once every "
                      "concurrency slot above is busy, before new requests "
                      "are rejected instead of queued.") +
            "<input id=\"ceilMaxQueuedInference\" type=\"number\" min=\"0\">"
            "</label>"
            "<label>Maximum background index workers" +
            field_hint("The most project-indexing worker threads the "
                      "controller may run at once.") +
            "<input id=\"ceilMaxIndexWorkers\" type=\"number\" min=\"1\">"
            "</label>"
            "<label>Maximum context tokens" +
            field_hint("The controller never shrinks a model's context "
                      "window below normal, but this caps how far it may "
                      "recover back up to after a prior shrink.") +
            "<input id=\"ceilMaxContextTokens\" type=\"number\" min=\"512\">"
            "</label>"
            "<label>Minimum idle-unload delay, seconds" +
            field_hint("The shortest time an idle model may sit warm before "
                      "the controller is allowed to unload it under memory "
                      "pressure.") +
            "<input id=\"ceilMinIdleUnloadSeconds\" type=\"number\" min=\"1\">"
            "</label>"
            "<label>Maximum idle-unload delay, seconds" +
            field_hint("The longest an idle model may stay warm once "
                      "pressure eases back to normal.") +
            "<input id=\"ceilMaxIdleUnloadSeconds\" type=\"number\" min=\"1\">"
            "</label>"
            "<label>Maximum step size, percent" +
            field_hint("How large a single automatic change to any one "
                      "number may be, as a percentage of its current value "
                      "-- keeps one adjustment from swinging a setting too "
                      "far at once.") +
            "<input id=\"ceilMaxStepPercent\" type=\"number\" min=\"1\" "
            "max=\"100\"></label>"
            "<label>Maximum changes per interval" +
            field_hint("The most automatic adjustments allowed within one "
                      "interval (see below), across every knob combined.") +
            "<input id=\"ceilMaxChangesPerInterval\" type=\"number\" min=\"1\">"
            "</label>"
            "<label>Interval length, seconds" +
            field_hint("The rolling window the \"maximum changes per "
                      "interval\" limit above is measured over.") +
            "<input id=\"ceilIntervalSeconds\" type=\"number\" min=\"1\">"
            "</label>"
            "<label>Minimum dwell time, seconds" +
            field_hint("How long the controller must wait after making one "
                      "change before it is allowed to make another -- "
                      "prevents rapid back-and-forth flapping.") +
            "<input id=\"ceilMinimumDwellSeconds\" type=\"number\" min=\"1\">"
            "</label>"
            "<button type=\"submit\" title=\"Save ceilings\">" ICON_SAVE_SVG
            " Save ceilings</button>"
            "</form></details>"
            "</div>"
            "<div class=\"reportSection\"><h3>Local runner pool</h3>"
            "<div id=\"perfRunnerPool\">Loading...</div></div>"
            "<div class=\"reportSection\"><h3>Intranet worker pool</h3>"
            "<div id=\"perfWorkerPool\">Loading...</div></div>"
            // Phase 35 (this pass): the remaining named Performance pages
            // (Memory, Caches, Storage, Scheduling, Advanced Optimizations,
            // Calibration) added as further real sections of this one
            // consolidated page rather than separate routes -- every figure
            // below still comes directly from the same
            // /api/v1/system/*//api/v1/performance/* routes the phases that
            // built them already expose (Phase 14 memory, Phase 17 cache,
            // Phase 31 storage/scratch/migration manifest, the Phase 13
            // scheduler, Phase 20 advanced optimizations, Phase 19
            // calibration). Query Traces, Runner Configuration, Model
            // Comparison, Benchmarks, and Regression History remain
            // deferred -- no dedicated telemetry route exists yet for
            // those, so this stays honest rather than fabricating one.
            "<div class=\"reportSection\"><h3>Memory</h3>"
            "<div id=\"perfMemory\">Loading...</div></div>"
            "<div class=\"reportSection\"><h3>Caches</h3>"
            "<div id=\"perfCaches\">Loading...</div>"
            "<button type=\"button\" id=\"perfCacheTrim\">Trim caches</button> "
            "<button type=\"button\" id=\"perfCacheClear\">Clear caches</button>"
            "</div>"
            "<div class=\"reportSection\"><h3>Storage</h3>"
            "<div id=\"perfStorage\">Loading...</div>"
            "<div id=\"perfScratch\"></div>"
            "<button type=\"button\" id=\"perfScratchCleanup\">Clean up "
            "orphaned scratch</button>"
            "<h4>Tier-migration manifest</h4>"
            "<div id=\"perfStorageManifest\"></div></div>"
            "<div class=\"reportSection\"><h3>Scheduling</h3>"
            "<div id=\"perfScheduling\">Loading...</div></div>"
            "<div class=\"reportSection\"><h3>Advanced optimizations</h3>"
            "<div id=\"perfAdvancedOptimizations\">Loading...</div></div>"
            "<div class=\"reportSection\"><h3>Calibration profiles</h3>"
            "<div id=\"perfCalibration\">Loading...</div></div>"
            "</div></section>"
            // Self-contained: fetched and rendered independently of the
            // page's shared load() pipeline (see fetchFor()'s own comment
            // on why that shared Promise.all only fetches elements present
            // on the current page) -- this page's placeholders above are
            // only ever present when section=='performance', so a plain,
            // unconditional api() call here never fires on any other page.
            "<script>(function(){"
            "function row(label,value){return '<tr><td class=\"reportLabel\">'+"
            "label+'</td><td class=\"reportValue\">'+value+'</td></tr>';}"
            "function renderAdaptive(r){const el=q('#perfAdaptive');if(!el)return;"
            "let html='<table><tbody>'+"
            "row('Active mode',r.activeMode)+row('Selected mode',r.selectedMode)+"
            "row('Applied this cycle',r.applied.length)+"
            "row('Proposed (not yet applied)',r.proposedNotYetApplied.length)+"
            "'</tbody></table>';"
            "if(r.applied.length){html+='<h4>Applied</h4><ul>'+r.applied.map(a=>"
            "'<li>'+a.parameter+': '+a.previousValue+' \\u2192 '+a.proposedValue+"
            "' ('+a.reason+')</li>').join('')+'</ul>';}"
            "if(r.proposedNotYetApplied.length){html+='<h4>Recommended</h4><ul>'+"
            "r.proposedNotYetApplied.map(a=>'<li>'+a.parameter+': '+"
            "a.previousValue+' \\u2192 '+a.proposedValue+' ('+a.reason+"
            "', confidence '+Math.round(a.confidence*100)+'%)</li>').join('')+"
            "'</ul>';}"
            "el.innerHTML=html;q('#perfMode').value=r.activeMode;"
            "const c=r.ceilings;if(c){"
            "q('#ceilMaxInferenceConcurrency').value=c.maxInferenceConcurrency;"
            "q('#ceilMaxQueuedInference').value=c.maxQueuedInference;"
            "q('#ceilMaxIndexWorkers').value=c.maxIndexWorkers;"
            "q('#ceilMaxContextTokens').value=c.maxContextTokens;"
            "q('#ceilMinIdleUnloadSeconds').value=c.minIdleUnloadSeconds;"
            "q('#ceilMaxIdleUnloadSeconds').value=c.maxIdleUnloadSeconds;"
            "q('#ceilMaxStepPercent').value=c.maxStepPercent;"
            "q('#ceilMaxChangesPerInterval').value=c.maxChangesPerInterval;"
            "q('#ceilIntervalSeconds').value=c.intervalSeconds;"
            "q('#ceilMinimumDwellSeconds').value=c.minimumDwellSeconds;}}"
            "function renderPool(elementId,list,noun){const el=q(elementId);"
            "if(!el)return;if(!list.length){el.textContent='No '+noun+' configured.';"
            "return;}"
            "el.innerHTML='<table><thead><tr><th>Id</th><th>State</th>"
            "<th>Model</th><th>Healthy</th><th>Failures</th></tr></thead><tbody>'+"
            "list.map(x=>'<tr><td>'+x.id+'</td><td>'+x.state+'</td><td>'+"
            "(x.modelId||'')+'</td><td>'+(x.healthy?'yes':'no')+'</td><td>'+"
            "x.consecutiveFailures+'</td></tr>').join('')+'</tbody></table>';}"
            "function bytesMiB(b){return Math.round(b/1048576);}"
            "function renderMemory(m){const el=q('#perfMemory');if(!el)return;"
            "let html='<table><tbody>'+"
            "row('Pressure',m.pressure)+"
            "row('Hard limit',bytesMiB(m.hardLimitBytes)+' MiB')+"
            "row('Reserved',bytesMiB(m.reservedBytes)+' MiB')+"
            "row('Observed process',bytesMiB(m.observedProcessBytes)+' MiB')+"
            "row('Available physical',bytesMiB(m.availablePhysicalBytes)+' MiB')+"
            "row('Runner idle unload',m.runnerIdleUnloadSeconds+'s')+"
            "'</tbody></table>';"
            "html+='<table><thead><tr><th>Category</th><th>Bytes (MiB)</th>"
            "</tr></thead><tbody>'+Object.entries(m.categories).map(([k,v])=>"
            "'<tr><td>'+k+'</td><td>'+bytesMiB(v)+'</td></tr>').join('')+"
            "'</tbody></table>';"
            "if(m.activePressureActions.length){html+='<p>Active pressure "
            "actions: '+m.activePressureActions.join(', ')+'</p>';}"
            "el.innerHTML=html;}"
            "function renderCaches(c){const el=q('#perfCaches');if(!el)return;"
            "el.innerHTML='<table><thead><tr><th>Category</th><th>Used/Capacity "
            "(MiB)</th><th>Entries</th><th>Hits</th><th>Misses</th>"
            "<th>Evictions</th><th>Rejections</th></tr></thead><tbody>'+"
            "Object.entries(c.categories).map(([k,v])=>'<tr><td>'+k+'</td><td>'+"
            "bytesMiB(v.usedBytes)+'/'+bytesMiB(v.capacityBytes)+'</td><td>'+"
            "v.entries+'</td><td>'+v.hits+'</td><td>'+v.misses+'</td><td>'+"
            "v.evictions+'</td><td>'+v.admissionRejections+'</td></tr>')"
            ".join('')+'</tbody></table>';}"
            "function renderStorage(s){const el=q('#perfStorage');if(!el)return;"
            "el.innerHTML='<table><tbody>'+"
            "row('Models root tier',s.modelsRoot.tier)+"
            "row('Storage class',s.modelsRoot.storageClass)+"
            "row('Measured read latency',s.modelsRoot.measuredReadLatencyMicroseconds+"
            "'\\u00b5s')+"
            "row('Acceptable for active model storage',"
            "s.modelsRoot.acceptableForActiveModelStorage?'yes':'no')+"
            "'</tbody></table>'+(s.modelsRoot.concerns.length?"
            "'<p>Concerns: '+s.modelsRoot.concerns.join(', ')+'</p>':'');}"
            "function renderScratch(s){const el=q('#perfScratch');if(!el)return;"
            "el.innerHTML='<table><tbody>'+"
            "row('Root',s.root)+row('Preferred tier',s.preferredTier)+"
            "row('Quota',bytesMiB(s.globalQuotaBytes)+' MiB')+"
            "row('Reserved',bytesMiB(s.globalReservedBytes)+' MiB')+"
            "row('Active jobs',s.activeJobs.length)+'</tbody></table>';}"
            "function renderStorageManifest(m){const el="
            "q('#perfStorageManifest');if(!el)return;"
            "if(!m.migrations.length){el.textContent='No tier migrations "
            "recorded.';return;}"
            "el.innerHTML='<table><thead><tr><th>Source</th><th>Destination"
            "</th><th>Data class</th><th>SHA-256</th></tr></thead><tbody>'+"
            "m.migrations.map(x=>'<tr><td>'+x.sourcePath+'</td><td>'+"
            "x.destinationPath+'</td><td>'+x.dataClass+'</td><td>'+"
            "x.sha256.slice(0,12)+'...</td></tr>').join('')+'</tbody></table>';}"
            "function renderScheduling(s){const el=q('#perfScheduling');"
            "if(!el)return;"
            "el.innerHTML='<table><thead><tr><th>Class</th><th>Queued</th>"
            "<th>Running</th><th>Admitted</th><th>Rejected</th><th>Expired</th>"
            "<th>Preempted</th></tr></thead><tbody>'+"
            "Object.entries(s.classes).map(([k,v])=>'<tr><td>'+k+'</td><td>'+"
            "v.queued+'</td><td>'+v.running+'</td><td>'+v.admittedTotal+"
            "'</td><td>'+v.rejectedTotal+'</td><td>'+v.expiredTotal+'</td><td>'+"
            "v.preemptedTotal+'</td></tr>').join('')+'</tbody></table>';}"
            "function renderAdvancedOptimizations(r){const el="
            "q('#perfAdvancedOptimizations');if(!el)return;"
            "el.innerHTML='<table><thead><tr><th>Feature</th><th>Enabled</th>"
            "<th>Implementation available</th><th>Has evidence</th>"
            "</tr></thead><tbody>'+r.features.map(f=>'<tr><td title=\"'+"
            "f.description+'\">'+f.name+'</td><td>'+(f.enabled?'yes':'no')+"
            "'</td><td>'+(f.implementationAvailable?'yes':'no')+'</td><td>'+"
            "(f.hasEvidence?'yes':'no')+'</td></tr>').join('')+'</tbody></table>';}"
            "function renderCalibration(r){const el=q('#perfCalibration');"
            "if(!el)return;"
            "if(!r.profiles.length){el.textContent='No calibration profiles "
            "recorded yet.';return;}"
            "el.innerHTML='<table><thead><tr><th>Profile</th><th>Model</th>"
            "<th>Context</th><th>GPU layers</th><th>Parallel slots</th>"
            "<th>Cold load (ms)</th><th>Gen (tok/\\u00b5s)</th></tr></thead>"
            "<tbody>'+r.profiles.map(p=>'<tr><td>'+p.profileName+'</td><td>'+"
            "p.modelSha256.slice(0,12)+'...</td><td>'+"
            "p.recommendedContextLength+'</td><td>'+p.recommendedGpuLayers+"
            "'</td><td>'+p.recommendedParallelSlots+'</td><td>'+"
            "Math.round(p.coldLoadMicroseconds/1000)+'</td><td>'+"
            "p.generatedTokensMeasured+'</td></tr>').join('')+'</tbody></table>';}"
            "async function refresh(){"
            "try{renderAdaptive(await api('/api/v1/performance/adaptive'));}"
            "catch(e){}"
            "try{const p=await api('/api/v1/runner/pool');"
            "renderPool('#perfRunnerPool',p.runners,'local runners');}catch(e){}"
            "try{const w=await api('/api/v1/worker/pool');"
            "renderPool('#perfWorkerPool',w.workers,'intranet workers');}catch(e){}"
            "try{renderMemory(await api('/api/v1/system/memory'));}catch(e){}"
            "try{renderCaches(await api('/api/v1/system/cache'));}catch(e){}"
            "try{renderStorage(await api('/api/v1/system/storage'));}catch(e){}"
            "try{renderScratch(await api('/api/v1/system/scratch'));}catch(e){}"
            "try{renderStorageManifest("
            "await api('/api/v1/system/storage/manifest'));}catch(e){}"
            "try{renderScheduling(await api('/api/v1/system/scheduler'));}"
            "catch(e){}"
            "try{renderAdvancedOptimizations("
            "await api('/api/v1/performance/advanced-optimizations'));}catch(e){}"
            "try{renderCalibration("
            "await api('/api/v1/performance/recommendations'));}catch(e){}"
            "}"
            "if(q('#panel-performance')){refresh();"
            "const form=q('#perfModeForm');"
            "if(form)form.addEventListener('submit',async(e)=>{e.preventDefault();"
            "try{await api('/api/v1/performance/adaptive/mode','POST',"
            "{mode:q('#perfMode').value});await refresh();}"
            "catch(err){showSystemError(err.message);}});"
            "const rollback=q('#perfRollback');"
            "if(rollback)rollback.addEventListener('click',async()=>{"
            "try{await api('/api/v1/performance/adaptive/rollback','POST',{});"
            "await refresh();}catch(err){showSystemError(err.message);}});"
            "const ceilingsForm=q('#perfCeilingsForm');"
            "if(ceilingsForm)ceilingsForm.addEventListener('submit',"
            "async(e)=>{e.preventDefault();try{await api("
            "'/api/v1/performance/adaptive/ceilings','POST',{"
            "maxInferenceConcurrency:Number("
            "q('#ceilMaxInferenceConcurrency').value),"
            "maxQueuedInference:Number(q('#ceilMaxQueuedInference').value),"
            "maxIndexWorkers:Number(q('#ceilMaxIndexWorkers').value),"
            "maxContextTokens:Number(q('#ceilMaxContextTokens').value),"
            "minIdleUnloadSeconds:Number(q('#ceilMinIdleUnloadSeconds').value),"
            "maxIdleUnloadSeconds:Number(q('#ceilMaxIdleUnloadSeconds').value),"
            "maxStepPercent:Number(q('#ceilMaxStepPercent').value),"
            "maxChangesPerInterval:Number("
            "q('#ceilMaxChangesPerInterval').value),"
            "intervalSeconds:Number(q('#ceilIntervalSeconds').value),"
            "minimumDwellSeconds:Number(q('#ceilMinimumDwellSeconds').value)"
            "});await refresh();}"
            "catch(err){showSystemError(err.message);}});"
            "const cacheTrim=q('#perfCacheTrim');"
            "if(cacheTrim)cacheTrim.addEventListener('click',async()=>{"
            "try{await api('/api/v1/system/cache/trim','POST',{});"
            "await refresh();}catch(err){showSystemError(err.message);}});"
            "const cacheClear=q('#perfCacheClear');"
            "if(cacheClear)cacheClear.addEventListener('click',async()=>{"
            "try{await api('/api/v1/system/cache/clear','POST',{});"
            "await refresh();}catch(err){showSystemError(err.message);}});"
            "const scratchCleanup=q('#perfScratchCleanup');"
            "if(scratchCleanup)scratchCleanup.addEventListener('click',"
            "async()=>{try{await api('/api/v1/system/scratch/cleanup','POST',{});"
            "await refresh();}catch(err){showSystemError(err.message);}});}"
            "})();</script>";
    } else if (section == "performance-benchmarks") {
        body =
            "<section id=\"panel-performance-benchmarks\" class=\"panel\">"
            "<div><h2>Benchmarks &amp; Regression</h2>"
            "<p>The Phase 36 full performance benchmark matrix and "
            "regression gate. Every run below is a real quality-benchmark "
            "pass plus five regression check groups (runner attribution, "
            "low memory, prompt cache, calibration, model routing) against "
            "this codebase's own live decision logic. A run is only ever "
            "compared against a previous <em>accepted</em> run that shares "
            "its exact fingerprint (model, backend, hardware, settings, "
            "prompt suite, cache state, and profile) -- mismatched "
            "environments are never presented as a direct comparison. "
            "Honest scope note: this control plane runs every dimension it "
            "can control in software on this one host (cache cold/warm, "
            "sequential concurrency, prompt/context size); it cannot "
            "manufacture multiple physical storage media or GPU hardware "
            "on demand, so the plan's full cross-device matrix still "
            "depends on an administrator running this page on each real "
            "target host.</p>"
            "<div class=\"reportSection\"><h3>Run a certification</h3>"
            "<form id=\"certRunForm\">"
            "<label>Model id" +
            field_hint("The model id exactly as it appears on the Model "
                      "Inventory page.") +
            "<input id=\"certModelId\" required></label>"
            "<label>Backend version" +
            field_hint("The llama.cpp backend build identifier this run is "
                      "measuring, so results are never compared across a "
                      "silent backend upgrade.") +
            "<input id=\"certBackendVersion\" required></label>"
            "<label>Build id" +
            field_hint("This MasterAI build's own version/commit "
                      "identifier.") +
            "<input id=\"certBuildId\" required></label>"
            "<label>Hardware id" +
            field_hint("An identifier for the physical host running this "
                      "benchmark, so results are never compared across "
                      "different machines.") +
            "<input id=\"certHardwareId\" required></label>"
            "<label>Profile" +
            field_hint("Quick runs 2 cases, standard 5, extended the full "
                      "suite -- larger profiles take longer but carry "
                      "more evidence.") +
            "<select id=\"certProfile\">"
            "<option value=\"quick\">Quick</option>"
            "<option value=\"standard\">Standard</option>"
            "<option value=\"extended\">Extended</option></select></label>"
            "<label>Cache state" +
            field_hint("Cold trims every bounded cache first so the run "
                      "measures a genuinely cold cache; warm leaves "
                      "caches as they are.") +
            "<select id=\"certCacheState\">"
            "<option value=\"warm\">Warm</option>"
            "<option value=\"cold\">Cold</option></select></label>"
            "<label>Concurrency" +
            field_hint("How many times the suite repeats; each repetition "
                      "is a real generate() call run one after another on "
                      "this single-process host, not a literally "
                      "simultaneous load.") +
            "<input id=\"certConcurrency\" type=\"number\" min=\"1\" "
            "value=\"1\"></label>"
            "<button type=\"submit\" title=\"Run certification\">" ICON_SAVE_SVG
            " Run certification</button></form>"
            "<p id=\"certRunStatus\" role=\"status\"></p></div>"
            "<div class=\"reportSection\"><h3>Regression thresholds</h3>"
            "<form id=\"certThresholdsForm\">"
            "<label>Max TTFT regression %" +
            field_hint("How much slower the first response is allowed to "
                      "get before the gate rejects the run.") +
            "<input id=\"certMaxTtft\" type=\"number\" step=\"0.1\"></label>"
            "<label>Max memory increase %" +
            field_hint("How much higher peak resident memory is allowed "
                      "to get before the gate rejects the run.") +
            "<input id=\"certMaxMemory\" type=\"number\" step=\"0.1\">"
            "</label>"
            "<label>Min throughput %" +
            field_hint("How much generation throughput is allowed to "
                      "drop (negative) or must improve (positive) to "
                      "pass.") +
            "<input id=\"certMinThroughput\" type=\"number\" step=\"0.1\">"
            "</label>"
            "<label>Max quality regression %" +
            field_hint("How much lower the benchmark pass rate is allowed "
                      "to get before the gate rejects the run.") +
            "<input id=\"certMaxQuality\" type=\"number\" step=\"0.1\">"
            "</label>"
            "<label>Max CPU increase %" +
            field_hint("How much higher average CPU utilization is "
                      "allowed to get before the gate rejects the run.") +
            "<input id=\"certMaxCpu\" type=\"number\" step=\"0.1\"></label>"
            "<label>Max queue-wait increase %" +
            field_hint("How much longer a request is allowed to wait in "
                      "the scheduler's queue before the gate rejects the "
                      "run.") +
            "<input id=\"certMaxQueueWait\" type=\"number\" step=\"0.1\">"
            "</label>"
            "<label>Max storage amplification %" +
            field_hint("How much higher disk bytes read per generated "
                      "token is allowed to get before the gate rejects "
                      "the run.") +
            "<input id=\"certMaxStorage\" type=\"number\" step=\"0.1\">"
            "</label>"
            "<button type=\"submit\" title=\"Save thresholds\">" ICON_SAVE_SVG
            " Save thresholds</button></form></div>"
            "<div class=\"reportSection\"><h3>Regression history</h3>"
            "<div id=\"certHistory\">Loading...</div></div>"
            "</div></section>"
            "<script>(function(){"
            "function certRow(label,value){return '<tr><td class="
            "\"reportLabel\">'+label+'</td><td class=\"reportValue\">'+"
            "value+'</td></tr>';}"
            "function renderThresholds(t){"
            "q('#certMaxTtft').value=t.maxTtftRegressionPercent;"
            "q('#certMaxMemory').value=t.maxMemoryIncreasePercent;"
            "q('#certMinThroughput').value=t.minThroughputPercent;"
            "q('#certMaxQuality').value=t.maxQualityRegressionPercent;"
            "q('#certMaxCpu').value=t.maxCpuIncreasePercent;"
            "q('#certMaxQueueWait').value=t.maxQueueWaitIncreasePercent;"
            "q('#certMaxStorage').value=t.maxStorageAmplificationPercent;}"
            "function renderHistory(list){const el=q('#certHistory');"
            "if(!el)return;if(!list.length){el.textContent='No "
            "certification runs recorded yet.';return;}"
            "el.innerHTML='<table><thead><tr><th>When</th><th>Model</th>"
            "<th>Profile</th><th>Cache</th><th>Accelerator</th>"
            "<th>Quality</th><th>Accepted</th><th>Reason</th>"
            "<th>Resources</th></tr></thead>"
            "<tbody>'+list.map(c=>'<tr><td>'+"
            "new Date(c.createdEpochSeconds*1000).toLocaleString()+"
            "'</td><td>'+c.modelId+'</td><td>'+c.profile+'</td><td>'+"
            "c.cacheState+'</td><td>'+c.acceleratorMode+'</td><td>'+"
            "Math.round(c.qualityScore*100)+'%</td><td>'+"
            "(c.accepted?'yes':'no')+'</td><td>'+(c.rejectionReason||'')+"
            // Phase 36 benchmark-gap pass: commit/page-fault/storage-
            // operation-count -- real measured numbers (see
            // PerformanceCertificationRecord's field comments in
            // masterai.hpp), shown as a compact tooltip rather than four
            // more table columns, matching this page's existing
            // uncluttered layout.
            "'</td><td title=\"Commit '+"
            "(c.commitBytes/1048576).toFixed(1)+' MB, '+c.pageFaults+"
            "' page faults, '+c.storageReadOperations+' storage read op(s), '+"
            "c.storageWriteOperations+' storage write op(s)\">'+"
            "(c.commitBytes/1048576).toFixed(0)+' MB, '+c.pageFaults+"
            "' faults</td></tr>').join('')+'</tbody></table>';}"
            "async function refresh(){"
            "try{renderThresholds(await api("
            "'/api/v1/performance/certification/thresholds'));}catch(e){}"
            "try{const r=await api('/api/v1/performance/certification');"
            "renderHistory(r.certifications);}catch(e){}}"
            "if(q('#panel-performance-benchmarks')){refresh();"
            "const runForm=q('#certRunForm');"
            "if(runForm)runForm.addEventListener('submit',async(e)=>{"
            "e.preventDefault();q('#certRunStatus').textContent="
            "'Running...';"
            "try{await api('/api/v1/performance/certification','POST',{"
            "modelId:q('#certModelId').value,"
            "backendVersion:q('#certBackendVersion').value,"
            "buildId:q('#certBuildId').value,"
            "hardwareId:q('#certHardwareId').value,"
            "profile:q('#certProfile').value,"
            "cacheState:q('#certCacheState').value,"
            "concurrency:parseInt(q('#certConcurrency').value,10)});"
            "q('#certRunStatus').textContent='Certification complete.';"
            "await refresh();}catch(err){"
            "q('#certRunStatus').textContent='';"
            "showSystemError(err.message);}});"
            "const thresholdsForm=q('#certThresholdsForm');"
            "if(thresholdsForm)thresholdsForm.addEventListener('submit',"
            "async(e)=>{e.preventDefault();"
            "try{await api("
            "'/api/v1/performance/certification/thresholds','POST',{"
            "maxTtftRegressionPercent:parseFloat(q('#certMaxTtft').value),"
            "maxMemoryIncreasePercent:parseFloat(q('#certMaxMemory').value),"
            "minThroughputPercent:parseFloat(q('#certMinThroughput').value),"
            "maxQualityRegressionPercent:parseFloat("
            "q('#certMaxQuality').value),"
            "maxCpuIncreasePercent:parseFloat(q('#certMaxCpu').value),"
            "maxQueueWaitIncreasePercent:parseFloat("
            "q('#certMaxQueueWait').value),"
            "maxStorageAmplificationPercent:parseFloat("
            "q('#certMaxStorage').value)});"
            "await refresh();}catch(err){showSystemError(err.message);}"
            "});}"
            "})();</script>";
    } else if (section == "settings-api-reference") {
        body =
            "<section id=\"panel-settings-api-reference\" class=\"panel\">"
            "<div><h2>API Reference</h2>"
            "<p>Every HTTP and MCP endpoint an external system, script, or "
            "another local application can call against this MasterAI "
            "instance -- what it is for, what it needs, and how to "
            "authenticate. This page documents integration surface; it "
            "never calls anything itself.</p>"
            "<div class=\"reportSection\"><h3>Authenticating without an "
            "interactive login</h3>"
            "<p>A local system or external service that has no human user "
            "signing in through this web UI should never try to reuse the "
            "browser's cookie session -- that session is tied to one "
            "signed-in person. Instead, sign in once through the web UI, "
            "then call <code>POST /api/v1/tokens</code> with a JSON body "
            "of <code>{\"scopes\":[...],\"expiresMinutes\":N,"
            "\"projects\":[...]}</code> (scopes and project bindings can "
            "never exceed what your own signed-in role already holds; "
            "expiry is capped at 43200 minutes/30 days) to mint a durable "
            "API token, and call every endpoint below with "
            "<code>Authorization: Bearer &lt;token&gt;</code>. Call "
            "<code>POST /api/v1/tokens/revoke</code> with "
            "<code>{\"token\":\"...\"}</code> to revoke one immediately. "
            "This is the recommended path for chat access with no "
            "interactive login: scope the token to only "
            "<code>chats.read</code>/<code>chats.write</code> if the "
            "integration only needs to send and read messages. Every "
            "token is independently scope-limited and revocable -- it "
            "never carries any person's own password.</p>"
            "<pre><code>curl -s -X POST http://127.0.0.1:8080/api/v1/tokens "
            "\\\n  -H \"Content-Type: application/json\" "
            "-H \"Cookie: $SESSION_COOKIE\" \\\n  -d "
            "'{\"scopes\":[\"chats.read\",\"chats.write\"],"
            "\"expiresMinutes\":43200,\"projects\":[]}'\n\n"
            "curl -s http://127.0.0.1:8080/api/v1/chats \\\n"
            "  -H \"Authorization: Bearer $TOKEN\"</code></pre>"
            "<table><thead><tr><th>Scope</th><th>Grants</th></tr></thead>"
            "<tbody>"
            "<tr><td>chats.read / chats.write</td><td>Read chat history, "
            "or create chats and send messages.</td></tr>"
            "<tr><td>models.read / models.load</td><td>Read the model "
            "inventory, or load/unload a model into the runner.</td></tr>"
            "<tr><td>projects.read / projects.write</td><td>Read a "
            "project's index status, or create projects and manage their "
            "index.</td></tr>"
            "<tr><td>attachments.write</td><td>Upload a chat "
            "attachment.</td></tr>"
            "<tr><td>benchmarks.read / benchmarks.run</td><td>Read stored "
            "benchmark results, or run a new benchmark.</td></tr>"
            "<tr><td>tokens.create</td><td>Mint further API tokens "
            "(never above your own held scopes/projects).</td></tr>"
            "<tr><td>ide.connect</td><td>Call the IDE endpoints "
            "(diagnostics, diff preview, capabilities).</td></tr>"
            "<tr><td>mcp.connect</td><td>Open an inbound MCP session "
            "against <code>POST /mcp</code>.</td></tr>"
            "<tr><td>mcp.tools.invoke</td><td>List or invoke registered "
            "outbound MCP servers.</td></tr>"
            "<tr><td>settings.manage / users.manage</td><td>Administrator-"
            "only: system configuration, user accounts, outbound MCP "
            "server registration.</td></tr>"
            "</tbody></table></div>"
            "<div class=\"reportSection\"><h3>Chats, memories &amp; "
            "attachments</h3>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>POST /api/v1/chats</td><td>Create a new chat.</td>"
            "</tr>"
            "<tr><td>GET /api/v1/chats</td><td>List chats visible to the "
            "authenticated user/token.</td></tr>"
            "<tr><td>GET /api/v1/chats/{id}</td><td>Read one chat's full "
            "message history.</td></tr>"
            "<tr><td>POST /api/v1/chats/{id}/messages</td><td>Send a new "
            "message and receive the model's reply (streamed).</td></tr>"
            "<tr><td>POST /api/v1/chats/{id}/model</td><td>Change which "
            "model future messages in this chat use.</td></tr>"
            "<tr><td>POST /api/v1/chats/{id}/tool-approvals/{approvalId}"
            "</td><td>Approve or reject a paused high-risk tool call and "
            "resume the turn.</td></tr>"
            "<tr><td>POST /api/v1/chats/{id}/delete</td><td>Delete a chat "
            "and its full message history.</td></tr>"
            "<tr><td>POST /api/v1/runner/warm</td><td>Pre-warm the "
            "currently selected model before the first message is "
            "sent.</td></tr>"
            "<tr><td>GET /api/v1/memories</td><td>List saved user-memory "
            "details recalled into every chat's prompt.</td></tr>"
            "<tr><td>POST /api/v1/memories</td><td>Save a new memory "
            "detail.</td></tr>"
            "<tr><td>POST /api/v1/memories/{id}/delete</td><td>Delete a "
            "saved memory detail.</td></tr>"
            "<tr><td>POST /api/v1/attachments</td><td>Upload a file to "
            "attach to a chat message.</td></tr>"
            "<tr><td>POST /v1/completions</td><td>OpenAI-request-shaped "
            "single-turn completion, but only ever reachable on a "
            "per-model deployed inference endpoint's own listener port "
            "(Machine Learning &gt; Inference Endpoints), not this "
            "administrative server -- see that page for a given model's "
            "port and bearer token. This is this codebase's own surface, "
            "not a full OpenAI-compatible API.</td></tr>"
            "</tbody></table></div>"
            "<div class=\"reportSection\"><h3>Projects</h3>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>GET /api/v1/projects</td><td>List registered "
            "projects visible to the authenticated user/token.</td></tr>"
            "<tr><td>POST /api/v1/projects</td><td>Register a new project "
            "root directory.</td></tr>"
            "<tr><td>GET /api/v1/projects/{id}/index</td><td>Read the "
            "project's file-index build status.</td></tr>"
            "<tr><td>POST /api/v1/projects/{id}/index/rebuild</td><td>"
            "Rebuild the project's file index.</td></tr>"
            "<tr><td>POST /api/v1/projects/{id}/index/cancel</td><td>"
            "Cancel an in-progress index rebuild.</td></tr>"
            "<tr><td>POST /api/v1/projects/{id}/index/notify</td><td>"
            "Tell MasterAI a file inside the project changed on disk.</td>"
            "</tr></tbody></table></div>"
            "<div class=\"reportSection\"><h3>Models, downloads &amp; "
            "benchmarks</h3>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>GET /api/v1/models</td><td>The verified model "
            "inventory: id, display name, category, architecture, and "
            "readiness state.</td></tr>"
            "<tr><td>POST /api/v1/models/{id}/load</td><td>Load a model "
            "into the runner.</td></tr>"
            "<tr><td>POST /api/v1/models/{id}/unload</td><td>Unload the "
            "currently loaded model.</td></tr>"
            "<tr><td>GET /api/v1/models/usage-signals</td><td>Recent "
            "per-model usage counters used to recommend a default "
            "model.</td></tr>"
            "<tr><td>GET/POST /api/v1/model-downloads</td><td>List, or "
            "queue a new, model download.</td></tr>"
            "<tr><td>POST /api/v1/model-downloads/{id}/run, /pause, "
            "/cancel, /remove</td><td>Control one queued or in-progress "
            "download.</td></tr>"
            "<tr><td>GET /api/v1/benchmarks</td><td>List stored benchmark "
            "results.</td></tr>"
            "<tr><td>POST /api/v1/benchmarks</td><td>Run a new benchmark "
            "against a loaded model.</td></tr>"
            "<tr><td>POST /api/v1/benchmarks/recommend</td><td>Recommend "
            "a benchmark profile for a given model/hardware pairing.</td>"
            "</tr></tbody></table></div>"
            "<div class=\"reportSection\"><h3>System, performance &amp; "
            "health</h3>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>GET /api/v1/system/resources</td><td>Live hardware "
            "snapshot: CPU, RAM, GPU, storage class.</td></tr>"
            "<tr><td>GET /api/v1/system/memory</td><td>MasterAI's own "
            "memory budget, pressure, and category breakdown.</td></tr>"
            "<tr><td>GET /api/v1/system/storage</td><td>Storage tier "
            "usage and migration status.</td></tr>"
            "<tr><td>GET /api/v1/system/scheduler</td><td>Administrator-"
            "only: the request scheduler's live queue state.</td></tr>"
            "<tr><td>GET /api/v1/runner/status</td><td>The currently "
            "loaded model's runner state, requested-vs-actual accelerator "
            "settings.</td></tr>"
            "<tr><td>GET /api/v1/performance/adaptive</td><td>The active "
            "performance mode and any applied/recommended tuning "
            "changes.</td></tr>"
            "<tr><td>GET /api/v1/performance/certification</td><td>"
            "Regression-gate history for benchmarked builds.</td></tr>"
            "<tr><td>GET /health/live, /health/ready</td><td>Unauthenticated "
            "liveness/readiness probes for a supervising process or load "
            "balancer.</td></tr>"
            "</tbody></table></div>"
            "<div class=\"reportSection\"><h3>Machine Learning</h3>"
            "<p>Every Machine Learning resource type below follows the "
            "same shape: <code>GET /api/v1/ml/{resource}</code> lists, "
            "<code>POST /api/v1/ml/{resource}</code> creates, and "
            "<code>/api/v1/ml/{resource}/{id}</code> reads/updates one "
            "record, with a handful of resource-specific action routes "
            "(e.g. starting a training run) alongside it -- all gated to "
            "the administrator role today. See "
            "<code>docs/HowToUse-MachineLearning.md</code> for a full, "
            "phase-by-phase walkthrough of every endpoint and what each "
            "field means.</p>"
            "<table><thead><tr><th>Base path</th><th>Covers</th></tr>"
            "</thead><tbody>"
            "<tr><td>/api/v1/ml/projects, /models, /datasets</td><td>ML "
            "project workspaces, imported model records, dataset "
            "records.</td></tr>"
            "<tr><td>/api/v1/ml/subjects, /label-tasks, /subject-exams"
            "</td><td>Subject knowledge review, data labeling, and "
            "subject examination.</td></tr>"
            "<tr><td>/api/v1/ml/prep-jobs, /instruction-examples, "
            "/synthetic-records</td><td>Dataset preparation, curated "
            "instruction examples, synthetic data generation.</td></tr>"
            "<tr><td>/api/v1/ml/training-jobs, /fine-tuning-jobs, "
            "/hyperparameter-searches, /ensembles</td><td>Training runs, "
            "fine-tuning jobs, hyperparameter search, bagging/boosting/"
            "stacking ensembles.</td></tr>"
            "<tr><td>/api/v1/ml/evaluation-runs, /experiments, "
            "/model-comparisons</td><td>Evaluation runs, experiment "
            "tracking, side-by-side model comparison.</td></tr>"
            "<tr><td>/api/v1/ml/vector-stores, /rag-configs, "
            "/knowledge-documents</td><td>Embeddings/vector stores, "
            "RAG configuration, ingested knowledge sources.</td></tr>"
            "<tr><td>/api/v1/ml/model-builder-configs, "
            "/model-optimizations, /checkpoints</td><td>GGUF packaging, "
            "post-training optimization, checkpoint management.</td></tr>"
            "<tr><td>/api/v1/ml/deployments, /inference-endpoints, "
            "/compute-nodes</td><td>Model deployment, the per-model "
            "OpenAI-shaped inference listener, and compute node "
            "inventory.</td></tr>"
            "<tr><td>/api/v1/ml/automation-pipelines, /safety-policies, "
            "/model-cards</td><td>Automation pipelines, safety/governance "
            "policy, published model cards.</td></tr>"
            "<tr><td>/api/v1/ml/audit-logs, /monitoring, /dashboard"
            "</td><td>Read-only audit trail, health monitoring, and the "
            "ML dashboard summary.</td></tr>"
            "</tbody></table></div>"
            "<div class=\"reportSection\"><h3>MCP (Model Context "
            "Protocol) -- MasterAI as a server</h3>"
            "<p>An IDE or agent host connects to MasterAI as an MCP "
            "server so it can read authorized project files and search "
            "them from inside the host's own tool-call loop. Two "
            "transports reach the same tool catalogue:</p>"
            "<table><thead><tr><th>Transport</th><th>How to connect</th>"
            "</tr></thead><tbody>"
            "<tr><td>Streamable HTTP</td><td><code>POST /mcp</code> with "
            "<code>Authorization: Bearer &lt;token&gt;</code> (scope "
            "<code>mcp.connect</code>), header <code>MCP-Protocol-"
            "Version</code> matching this build's protocol version, and "
            "<code>Content-Type: application/json</code>. Use this for "
            "any MCP client that is not a supported IDE host.</td></tr>"
            "<tr><td>stdio (VS Code/Agent-Coder, Visual Studio)</td><td>"
            "Run <code>masterai mcp-stdio &lt;settings-file&gt; "
            "&lt;vscode|visual-studio&gt;</code> as the child process the "
            "IDE host launches. It speaks newline-delimited JSON-RPC on "
            "stdin/stdout and needs a stored IDE token first -- see setup "
            "below.</td></tr></tbody></table>"
            "<p>One-time stdio setup for an IDE host:</p>"
            "<pre><code>"
            "1. masterai ide-token-store &lt;settings-file&gt; vscode\n"
            "   (paste a token minted from POST /api/v1/tokens above,\n"
            "    scoped to at least mcp.connect and bound to the\n"
            "    project(s) the IDE should be able to read)\n"
            "2. masterai ide-profile &lt;settings-file&gt; vscode\n"
            "   (prints the JSON connection profile -- command,\n"
            "    arguments, apiBase, mcpEndpoint -- for the IDE\n"
            "    extension/host to consume directly)\n"
            "3. Point the IDE host's MCP client config at the printed\n"
            "   command: masterai mcp-stdio &lt;settings-file&gt; vscode"
            "</code></pre>"
            "<p>Replace <code>vscode</code> with <code>visual-studio</code> "
            "for a Visual Studio host. The token is read from secure OS "
            "storage, never from a plaintext file, and stdio emits only "
            "protocol JSON on stdout -- diagnostics go to stderr.</p>"
            "<p>Once connected, the host's <code>tools/list</code> call "
            "returns this fixed catalogue. The first four tools only need "
            "the token's existing <code>projects.read</code>/"
            "<code>models.read</code> scopes; the last three -- the same "
            "write/delete/run tools chat's own agentic tool use (Phase 84) "
            "calls, sharing its identical dispatch -- additionally need "
            "<code>projects.write</code>. A call any of those three would "
            "classify as high-risk (destructive) is refused outright, not "
            "executed unattended: MCP's request/response shape has no "
            "Approve/Deny UI of its own yet, so completing that specific "
            "action means using the MasterAI web chat instead, where it "
            "pauses for a human decision.</p>"
            "<table><thead><tr><th>Tool</th><th>Does</th></tr></thead>"
            "<tbody>"
            "<tr><td>masterai.projects.list</td><td>List only the "
            "projects explicitly bound to this token.</td></tr>"
            "<tr><td>masterai.project.read_file</td><td>Read one bounded "
            "UTF-8 source file from an authorized project.</td></tr>"
            "<tr><td>masterai.project.search</td><td>Find literal text in "
            "bounded source files inside an authorized project.</td></tr>"
            "<tr><td>masterai.project.list_directory</td><td>List one "
            "bounded directory's immediate entries in an authorized "
            "project.</td></tr>"
            "<tr><td>masterai.project.write_file</td><td>Create or "
            "overwrite one bounded UTF-8 file (requires "
            "<code>projects.write</code>; refused if it would blank an "
            "existing file).</td></tr>"
            "<tr><td>masterai.project.delete_file</td><td>Delete one file "
            "(requires <code>projects.write</code>; always refused through "
            "MCP -- always high-risk).</td></tr>"
            "<tr><td>masterai.project.run_command</td><td>Run one "
            "admin-allow-listed executable (requires "
            "<code>projects.write</code>; refused if the command matches "
            "the destructive-pattern table).</td></tr>"
            "<tr><td>masterai.models.list</td><td>List the verified local "
            "model inventory.</td></tr>"
            "</tbody></table>"
            "<p>An IDE host also has three plain REST endpoints alongside "
            "MCP, all requiring <code>ide.connect</code>:</p>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>GET /api/v1/ide/capabilities</td><td>What this build "
            "supports, so the host can adapt its own UI.</td></tr>"
            "<tr><td>POST /api/v1/ide/diagnostics</td><td>Deterministic "
            "read-only diagnostics for one authorized project file.</td>"
            "</tr>"
            "<tr><td>POST /api/v1/ide/diff-preview</td><td>Preview a "
            "bounded unified diff without modifying project files.</td>"
            "</tr></tbody></table></div>"
            "<div class=\"reportSection\"><h3>MCP -- MasterAI as a "
            "client</h3>"
            "<p>MasterAI can also call out to external MCP servers you "
            "register, so chat/agent flows can invoke their tools during "
            "a conversation. Registration is administrator-only "
            "(<code>settings.manage</code>); invocation needs "
            "<code>mcp.tools.invoke</code>.</p>"
            "<table><thead><tr><th>Endpoint</th><th>Purpose</th></tr>"
            "</thead><tbody>"
            "<tr><td>GET /api/v1/mcp/outbound/servers</td><td>List "
            "registered outbound MCP servers (credentials never included)."
            "</td></tr>"
            "<tr><td>POST /api/v1/mcp/outbound/servers</td><td>Register "
            "an outbound server: transport (<code>stdio</code> or "
            "<code>streamable-http</code>), pinned executable/endpoint, "
            "allowed tools, allowed projects, credential secret name, and "
            "timeout/output/memory limits.</td></tr>"
            "<tr><td>POST /api/v1/mcp/outbound/servers/remove</td><td>"
            "Remove a registration; its tool authority ends "
            "immediately.</td></tr>"
            "<tr><td>POST /api/v1/mcp/outbound/calls</td><td>Invoke one "
            "explicitly user-approved tool on a registered server, bounded "
            "to the caller's own scopes and project bindings.</td></tr>"
            "</tbody></table>"
            "<pre><code>curl -s -X POST "
            "http://127.0.0.1:8080/api/v1/mcp/outbound/servers \\\n"
            "  -H \"Authorization: Bearer $TOKEN\" "
            "-H \"Content-Type: application/json\" \\\n  -d '{\n"
            "    \"id\":\"docs-server\",\"transport\":\"stdio\",\n"
            "    \"executable\":\"C:\\\\tools\\\\docs-mcp.exe\","
            "\"arguments\":[],\n    \"workingDirectory\":\"\","
            "\"endpoint\":\"\",\n    \"allowedTools\":[\"docs.search\"],"
            "\"allowedProjects\":[],\n    \"credentialSecretName\":\"\","
            "\"timeoutSeconds\":30,\n    \"maximumOutputBytes\":1048576,"
            "\"memoryLimitMiB\":512\n  }'</code></pre>"
            "</div>"
            "<div class=\"reportSection\"><h3>Choosing a path for a new "
            "integration</h3>"
            "<p>Decide which direction the call needs to go:</p>"
            "<table><thead><tr><th>You want to...</th><th>Use</th></tr>"
            "</thead><tbody>"
            "<tr><td>Let a script or another local app read/send chats or "
            "inspect models</td><td>Mint an API token (Authenticating, "
            "above) and call the REST endpoints directly.</td></tr>"
            "<tr><td>Let an IDE host (VS Code/Agent-Coder, Visual Studio) "
            "read project files as part of its own agent loop</td><td>"
            "Set up stdio MCP (MCP as a server, above).</td></tr>"
            "<tr><td>Let any other MCP client connect over the "
            "network</td><td>Mint a token scoped to "
            "<code>mcp.connect</code> and call <code>POST /mcp</code> "
            "directly.</td></tr>"
            "<tr><td>Let MasterAI's own chat/agent flows call an external "
            "tool</td><td>Register it as an outbound MCP server (MCP as a "
            "client, above).</td></tr>"
            "</tbody></table></div>"
            "</div></section>";
    } else if (section == "admin-create") {
        body =
            "<section id=\"panel-admin-create\" class=\"panel\">"
            "<div><h2>Create user</h2>"
            "<form id=\"newUser\"><label>Username"
            "<input id=\"newUserName\" required "
            "pattern=\"[A-Za-z0-9_.-]+\"></label>"
            "<label>Display name<input id=\"newUserDisplay\" required></label>"
            "<label>Role<select id=\"newUserRole\">"
            "<option value=\"administrator\">administrator</option>"
            "<option value=\"developer\">developer</option>"
            "<option value=\"viewer\">viewer</option></select></label>"
            "<label>Password (minimum 8 characters)"
            "<input id=\"newUserPassword\" type=\"password\" "
            "minlength=\"8\" required></label>"
            "<button title=\"Create local account\">" ICON_PLUS_SVG " Create local account</button></form>"
            "</div></section>";
    } else if (section == "admin-users") {
        body =
            "<section id=\"panel-admin-users\" class=\"panel\">"
            "<div><h2>User list</h2>"
            "<div id=\"usersList\">Loading...</div></div></section>";
    }

    const std::string memory_controls =
        "<details id=\"chatMemorySection\"><summary>Memory</summary>"
        "<p class=\"memoryHelp\">Saved details are available to every model. "
        "Type <code>save to memory: your detail</code> in chat, or manage them "
        "here.</p>" +
        std::string(can_write_chat
                        ? "<form id=\"newMemory\"><label for=\"memoryContent\">"
                          "Detail to remember</label><textarea id=\"memoryContent\" "
                          "rows=\"2\" maxlength=\"512\" required></textarea>"
                          "<button title=\"Save detail\">" ICON_SAVE_SVG " Save detail</button></form>"
                        : "") +
        "<div id=\"memoryList\">Loading...</div></details>";

    std::string sidebar_links =
        nav_link("/app", "+ New chat", section == "chat" && chat_id.empty()) +
        sidebar_section(
            "chats", "Chats",
            "<div id=\"chatList\"></div>"
            // Older conversations (anything past the most recent 20) render
            // here instead, so the primary list above stays a fixed,
            // scannable size. Hidden by default; renderChatList() reveals
            // it once there is anything to show.
            "<details id=\"chatHistorySection\" hidden><summary>History</summary>"
            "<div id=\"chatHistoryList\"></div></details>" + memory_controls);
    if (can_manage_settings) {
        // Administrator-only entries (system configuration, user
        // management) are appended to this same Settings group instead of
        // their own separate "Admin" sidebar section -- Settings is already
        // gated to developer+administrator, so it needed a permission
        // boundary inside the group either way, and one settings-shaped
        // destination is easier for an administrator to find than two
        // sibling groups that both hold configuration. Every entry still
        // enforces its own server-side permission check regardless of what
        // this sidebar renders (see the route handlers), so a developer who
        // guesses one of these URLs directly still gets redirected/403'd.
        std::string settings_links =
            nav_link("/app/models/inventory", "Model inventory",
                     section == "models-inventory") +
            nav_link("/app/models/download", "Download a model",
                     section == "models-download") +
            nav_link("/app/models/benchmarks", "Benchmarks",
                     section == "models-benchmarks") +
            nav_link("/app/settings/api-reference", "API Reference",
                     section == "settings-api-reference");
        if (is_administrator) {
            settings_links +=
                nav_link("/app/settings/config", "System configuration",
                         section == "settings-config") +
                nav_link("/app/settings/allowed-commands",
                         "Run-command allow-list",
                         section == "settings-allowed-commands") +
                nav_link("/app/admin/create", "Create user",
                         section == "admin-create") +
                nav_link("/app/admin/users", "User list",
                         section == "admin-users");
        }
        sidebar_links +=
            sidebar_section("workspace", "Workspace",
                            nav_link("/app/projects", "Projects",
                                     section == "projects")) +
            sidebar_section("settings", "Settings", settings_links);
    }
    if (is_administrator) {
        sidebar_links += sidebar_section(
            "report", "Report",
            nav_link("/app/report/system", "System Report",
                     section == "report-system"));
        // Phase 35: Performance administration -- overview, local runner
        // pool (Phase 33), intranet worker pool (Phase 33), and the
        // adaptive controller (Phase 34), each backed by the real routes
        // those phases already exposed.
        sidebar_links += sidebar_section(
            "performance", "Performance",
            nav_link("/app/performance", "Overview", section == "performance") +
                // Phase 36: full performance benchmark matrix and
                // regression gate -- the "Benchmarks" and "Regression
                // History" pages the Phase 35 status note named as
                // deferred (no dedicated telemetry route existed for them
                // yet); GET/POST /api/v1/performance/certification now
                // gives this page real data to render.
                nav_link("/app/performance/benchmarks",
                         "Benchmarks & Regression",
                         section == "performance-benchmarks"));
        // Ordered to match the chronological order of actually building a
        // model -- an administrator works top to bottom, project through
        // deployment/monitoring, with the two packaging/inspection steps
        // that only make sense once a model exists ("Model Builder", which
        // produces the finished GGUF, and "Model Registry", which reports
        // on it) placed last rather than grouped near the top.
        sidebar_links += sidebar_section(
            "ml", "Machine Learning",
            nav_link("/app/ml", "Dashboard", section == "ml-dashboard") +
                nav_link("/app/ml/projects", "Projects",
                         section == "ml-projects") +
                nav_link("/app/ml/datasets", "Dataset Manager",
                         section == "ml-datasets") +
                // Moved out of "Machine Learning Logs and Settings" (where
                // it was mislabeled "Model Registry (Statistics)") -- it is
                // one of the two prerequisite entities (with Dataset
                // Manager, just above) a training job references, and its
                // panel is a real "Register a model" / "Predict with a
                // trained model" form, not a read-only statistics page.
                nav_link("/app/ml/models", "Model Registry",
                         section == "ml-models") +
                nav_link("/app/ml/subjects", "Subject Knowledge Manager",
                         section == "ml-subjects") +
                nav_link("/app/ml/label-tasks", "Data Labeling",
                         section == "ml-label-tasks") +
                nav_link("/app/ml/prep-jobs", "Data Preparation",
                         section == "ml-prep-jobs") +
                nav_link("/app/ml/synthetic-records",
                         "Synthetic Data Generation",
                         section == "ml-synthetic-records") +
                nav_link("/app/ml/instruction-examples",
                         "Prompt and Instruction Training",
                         section == "ml-instruction-examples") +
                nav_link("/app/ml/vector-stores",
                         "Embeddings and Vector Stores",
                         section == "ml-vector-stores") +
                nav_link("/app/ml/rag-configs",
                         "Retrieval-Augmented Generation",
                         section == "ml-rag-configs") +
                nav_link("/app/ml/training-jobs", "Training Jobs",
                         section == "ml-training-jobs") +
                nav_link("/app/ml/fine-tuning-jobs", "Fine-Tuning",
                         section == "ml-fine-tuning-jobs") +
                nav_link("/app/ml/checkpoints",
                         "Checkpoint Management",
                         section == "ml-checkpoints") +
                // Moved after Training Jobs/Fine-Tuning/Checkpoints (was
                // previously listed before Training Jobs even existed one
                // to search over) -- a hyperparameter search always picks
                // an existing training job, so it only makes sense once
                // one exists.
                nav_link("/app/ml/hyperparameter-searches",
                         "Hyperparameter Optimization",
                         section == "ml-hyperparameter-searches") +
                // Ensemble Methods (2026-08-24): same reasoning as
                // Hyperparameter Optimization directly above -- an
                // ensemble always references an existing training job, so
                // it is listed right after it.
                nav_link("/app/ml/ensembles", "Ensemble Methods",
                         section == "ml-ensembles") +
                nav_link("/app/ml/evaluation-runs", "Evaluation Lab",
                         section == "ml-evaluation-runs") +
                nav_link("/app/ml/experiments", "Experiment Tracking",
                         section == "ml-experiments") +
                nav_link("/app/ml/subject-exams",
                         "Subject Examination",
                         section == "ml-subject-exams") +
                nav_link("/app/ml/model-optimizations",
                         "Model Optimization",
                         section == "ml-model-optimizations") +
                nav_link("/app/ml/model-comparisons",
                         "Model Comparison",
                         section == "ml-model-comparisons") +
                nav_link("/app/ml/safety-governance",
                         "Safety and Governance",
                         section == "ml-safety-governance") +
                nav_link("/app/ml/deployments",
                         "Deployment Manager",
                         section == "ml-deployments") +
                nav_link("/app/ml/inference-endpoints",
                         "Inference Endpoints",
                         section == "ml-inference-endpoints") +
                nav_link("/app/ml/compute-nodes",
                         "Hardware and Compute",
                         section == "ml-compute-nodes") +
                nav_link("/app/ml/automation-pipelines",
                         "Automation Pipelines",
                         section == "ml-automation-pipelines") +
                nav_link("/app/ml/model-builder-configs", "Model Builder",
                         section == "ml-model-builder-configs"));
        // Logs, settings, and statistics aren't build-pipeline steps -- an
        // administrator visits them to check on or configure the system,
        // not as part of working through a model build in order -- so they
        // get their own tree instead of trailing the pipeline above.
        sidebar_links += sidebar_section(
            "ml-admin", "Machine Learning Logs and Settings",
            nav_link("/app/ml/monitoring", "Monitoring and Diagnostics",
                     section == "ml-monitoring") +
                nav_link("/app/ml/audit-logs", "Audit Logs",
                         section == "ml-audit-logs") +
                nav_link("/app/ml/settings", "Machine Learning Settings",
                         section == "ml-settings"));
    }

    return html_response(
        "<!doctype html><html><head><meta charset=\"utf-8\">"
        "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
        "<title>MasterAI Workspace</title><style>" DARK_THEME_CSS
        "body{max-width:none;padding:0}"
        // #shell is pinned to exactly one viewport height (not just a
        // min-height) so #content's flex children -- #actionStatus plus
        // whichever section body follows it -- have a real bounded box to
        // divide up. Without this, #chatShell's own flex:1 below has no
        // fixed ancestor height to size against, and the composer ends up
        // pushed below the fold instead of pinned to the bottom of the
        // visible window.
        "#shell{display:flex;height:100vh}"
        "#sidebar{width:16rem;flex:none;padding:1.25rem 1rem;"
        "border-right:1px solid var(--panel-border);"
        "display:flex;flex-direction:column;gap:.5rem;overflow-y:auto}"
        // A thin draggable gutter between the sidebar and the content pane
        // -- the sidebar script persists the chosen width (and the
        // sidebar's scroll position) in localStorage so both survive the
        // full-page navigation every sidebar click performs.
        "#sidebarResizer{flex:none;width:6px;cursor:col-resize;"
        "background:transparent;touch-action:none}"
        "#sidebarResizer:hover,#sidebarResizer.dragging{"
        "background:var(--panel-border)}"
        "#sidebar h1{font-size:1.3rem}"
        "#sidebar h3{margin-top:1rem}"
        // Collapsible sidebar section groups (Chats/Workspace/Settings/
        // Machine Learning/Admin) replace the old plain <h3> group
        // headings -- the marker plus summary text takes the same visual
        // role the h3 used to, but a click now shrinks the whole group away
        // instead of just being a label.
        ".sidebarSection{margin-top:1rem}"
        ".sidebarSection summary{font-weight:600;cursor:pointer;"
        "list-style-position:outside;padding:.15rem 0;user-select:none}"
        ".sidebarSection summary:hover{color:var(--text)}"
        ".sidebarSection>div{margin-top:.15rem}"
        // The content pane spans the full remaining browser width (no fixed
        // max-width cap), so every panel stretches consistently to the
        // window at any browser size -- the .panel grid below re-flows its
        // cards to fill whatever width this yields, and the chat page keeps
        // its own comfortable reading cap via #chatShell's max-width.
        // min-width:0 overrides the flex-item default of min-width:auto,
        // which sizes to the widest child's intrinsic (unwrapped) width --
        // without this, a wide ML table pushed #content (and #shell with
        // it) past the viewport instead of scrolling inside its own box.
        "#content{flex:1;min-width:0;padding:1.5rem;overflow-y:auto;"
        "display:flex;flex-direction:column;min-height:0}"
        "#actionStatus{margin-bottom:1rem;flex:none}"
        // An empty status line still reserved a full line of height (see the
        // global #actionStatus,#status min-height rule) even though it had
        // nothing to show -- on the chat page that shrank the message area
        // for no visible reason, so give the space back when there's no
        // status text.
        "#actionStatus:empty{margin-bottom:0;min-height:0}"
        ".navButton{display:block;background:transparent;color:var(--text);"
        "border:1px solid transparent;text-align:left;margin-top:.15rem;"
        "padding:.5rem .6rem;text-decoration:none}"
        ".navButton:hover{background:var(--panel)}"
        ".navButton.active{background:var(--panel);border-color:var(--panel-border)}"
        ".chatListItem{display:flex;align-items:center;gap:.15rem}"
        ".chatListItem .navButton{flex:1;min-width:0;margin-top:0;overflow:hidden;"
        "text-overflow:ellipsis;white-space:nowrap}"
        ".chatDeleteBtn{flex:none;width:1.8rem;height:1.8rem;padding:0;"
        "margin-top:0;display:flex;align-items:center;justify-content:center;"
        "background:transparent;border:1px solid transparent;border-radius:.4rem;"
        "color:var(--muted);line-height:1;cursor:pointer}"
        // Same fix as .iconBtn svg below: the global "button svg" rule (see
        // DARK_THEME_CSS) adds margin-right and a vertical-align offset to
        // space an icon from label text on full-width buttons -- this
        // button has no label text, so that same rule was shoving its SVG
        // off-center both horizontally and vertically. Zero it out.
        ".chatDeleteBtn svg{margin-right:0;vertical-align:middle}"
        ".chatDeleteBtn:hover{background:var(--panel);border-color:var(--panel-border);"
        "color:#e5657a}"
        // Compact icon-button toolbar used across every Machine Learning
        // list (status pickers, approve/reject/delete actions) in place of
        // full-width text buttons -- keeps each row a single tidy line
        // instead of stretching table cells and wrapping under the window.
        // nowrap keeps every row's action buttons on one line -- wrap let
        // a cramped column fold the second/third button onto its own line,
        // which is the "toolbar buttons need to be side by side" bug;
        // table() already wraps the whole table in an overflow-x:auto div,
        // so a genuinely too-narrow row scrolls instead of stacking.
        ".rowToolbar{display:flex;align-items:center;gap:.3rem;flex-wrap:nowrap}"
        ".rowToolbar select{max-width:11rem}"
        // margin-top:0 overrides the generic button{margin-top:.75rem}
        // rule (see DARK_THEME_CSS) that otherwise pushes every icon
        // button down out of vertical center with the select/text next to
        // it in the same .rowToolbar row.
        ".iconBtn{flex:none;width:1.8rem;height:1.8rem;padding:0;margin-top:0;"
        "display:inline-flex;align-items:center;justify-content:center;"
        "background:transparent;border:1px solid var(--panel-border);"
        "border-radius:.4rem;color:var(--muted);line-height:1;cursor:pointer}"
        // The global "button svg{margin-right:.4rem}" rule (see
        // DARK_THEME_CSS) exists to space an icon from the visible label
        // text that follows it on full-width form buttons -- iconBtn
        // buttons carry no label text, so that same margin just shoves the
        // icon off-center inside the square button. Zero it here.
        ".iconBtn svg{margin-right:0}"
        ".iconBtn:hover{background:var(--panel);color:var(--text)}"
        ".iconBtn-apply:hover{color:#4caf6a;border-color:#4caf6a}"
        ".iconBtn-delete:hover{color:#e5657a;border-color:#e5657a}"
        // Guard rail (ML forms clarity pass): a button disabled because a
        // prerequisite step is missing (e.g. Train Now before dataset
        // content is uploaded) must look obviously inert -- dimmed, no
        // hover reaction -- rather than looking identical to every
        // clickable button, which is what an un-styled [disabled] button
        // otherwise renders as.
        ".iconBtn:disabled{opacity:.35;cursor:not-allowed}"
        ".iconBtn:disabled:hover{background:transparent;color:var(--muted)}"
        // Table cells hold either short controls or long free-text/id
        // values; cap width and ellipsize the latter (escTrim already
        // shortens the text itself, this is the belt-and-suspenders CSS
        // backstop for values that arrive unexpectedly long) so a wide
        // table never forces the page past the window boundary -- the
        // surrounding div.style=overflow-x:auto in table() still scrolls
        // as a last resort, but should rarely be needed now.
        "table td,table th{max-width:16rem;overflow:hidden;"
        "text-overflow:ellipsis}"
        "table td:has(.rowToolbar),table td:has(select),table td:has(button)"
        "{overflow:visible;white-space:normal}"
        "#chatList{display:flex;flex-direction:column;gap:.15rem;max-height:14rem;"
        "overflow-y:auto}"
        "#chatHistorySection{margin-top:1rem}"
        "#chatHistorySection summary{color:var(--muted);font-size:.85rem;"
        "text-transform:uppercase;letter-spacing:.06em;cursor:pointer}"
        "#chatHistoryList{display:flex;flex-direction:column;gap:.15rem;"
        "max-height:14rem;overflow-y:auto;margin-top:.4rem}"
        "#chatMemorySection{margin-top:1rem}"
        "#chatMemorySection summary{color:var(--muted);font-size:.85rem;"
        "text-transform:uppercase;letter-spacing:.06em;cursor:pointer}"
        ".memoryHelp,.memoryEmpty{color:var(--muted);font-size:.75rem;"
        "line-height:1.4;margin:.45rem 0}"
        "#newMemory label{margin-top:.4rem}"
        "#newMemory textarea{font-size:.78rem;resize:vertical}"
        "#newMemory button{font-size:.78rem;padding:.4rem;margin-top:.4rem}"
        "#memoryList{display:flex;flex-direction:column;gap:.3rem;"
        "max-height:12rem;overflow-y:auto;margin-top:.55rem}"
        ".memoryItem{display:flex;align-items:flex-start;gap:.3rem;"
        "padding:.4rem;border:1px solid var(--panel-border);border-radius:.4rem;"
        "font-size:.75rem;line-height:1.35}"
        ".memoryItem span{flex:1;min-width:0;overflow-wrap:anywhere}"
        // Matches .chatDeleteBtn's centering fix above: a button's default
        // text layout doesn't reliably center a glyph both ways inside a
        // fixed square, so force it explicitly instead of relying on UA
        // text-align/line-height defaults.
        ".memoryDeleteBtn{flex:none;width:1.4rem;height:1.4rem;margin:0;padding:0;"
        "display:flex;align-items:center;justify-content:center;"
        "background:transparent;color:var(--muted);line-height:1}"
        ".memoryDeleteBtn:hover{background:#3a0a0a;color:#ffd54a}"
        "#allowedCommandsList{display:flex;flex-direction:column;gap:.4rem;"
        "margin-top:.75rem}"
        ".allowedCommandItem{display:flex;align-items:center;"
        "justify-content:space-between;gap:.6rem;padding:.5rem .6rem;"
        "border:1px solid var(--panel-border);border-radius:.4rem;"
        "font-size:.82rem}"
        ".allowedCommandItem span{flex:1;min-width:0;overflow-wrap:anywhere}"
        ".allowedCommandItem .allowedCommandActions{flex:none;display:flex;"
        "gap:.4rem}"
        // Disabled entries (including most of the seeded catalog once an
        // admin turns individual ones off) get a visually distinct dark
        // maroon/yellow treatment so a disabled row reads as "off" at a
        // glance rather than only via the "(disabled)" text suffix.
        ".allowedCommandItem-disabled{background:#3a0a12;"
        "border-color:#c9a227}"
        ".allowedCommandFormButtons{display:flex;gap:.5rem;align-items:center}"
        ".allowedCommandFilter{max-width:16rem;margin-top:.75rem}"
        // Model Inventory page: a self-contained "container" (background,
        // border, radius) matching the look #systemReport's own
        // .reportSection cards use, but scoped to its own id rather than
        // that page's id since this widget lives on a different page.
        "#memoryStatusSection{background:var(--panel);"
        "border:1px solid var(--panel-border);border-radius:.6rem;"
        "padding:.9rem 1.1rem;margin-top:1.25rem}"
        "#memoryStatusSection h3{margin:0 0 .3rem;font-size:.75rem;"
        "font-weight:600;text-transform:uppercase;letter-spacing:.05em;"
        "color:var(--muted)}"
        "#memoryStatusSection h4{margin:1rem 0 .2rem;font-size:.7rem;"
        "font-weight:600;text-transform:uppercase;letter-spacing:.05em;"
        "color:var(--muted)}"
        "#memoryStatusSection table{table-layout:fixed}"
        "#memoryStatusSection td{border-bottom:1px solid var(--panel-border);"
        "vertical-align:top}"
        "#memoryStatusSection tr:last-child td{border-bottom:none}"
        ".memoryCleanOption{margin-top:.6rem}"
        ".memoryCleanOption .checkboxLabel{font-size:.85rem;color:var(--text)}"
        // Two-column layout for the longer "Windows system memory options"
        // group, matching a familiar memory-cleaner tool's own checkbox
        // grid rather than a single long list.
        ".memoryCleanOptionsGrid{display:grid;"
        "grid-template-columns:repeat(auto-fit,minmax(220px,1fr));"
        "column-gap:1.5rem}"
        ".memoryStatusInlineNumber{width:4rem;display:inline-block;"
        "margin:0 .3rem;padding:.15rem .3rem}"
        ".memoryStatusHint{display:block;color:var(--muted);font-size:.72rem;"
        "line-height:1.35;margin:.15rem 0 0}"
        "#memCleanProgress{margin-top:.6rem}"
        ".panel{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));"
        "gap:1.25rem}"
        // Same min-width:auto default problem one level down: a grid track
        // otherwise still grows to fit a wide table inside it instead of
        // letting the table's own overflow-x scrollbar (see table()) do
        // the job, so a single wide ML card could blow out the whole row.
        ".panel>div{min-width:0}"
        // Every Machine Learning panel that pairs a form with a list
        // (:has(>div:nth-child(2)) excludes the handful of ML panels that
        // are just a single list/settings card, e.g. ml-audit-logs,
        // ml-settings) was giving the narrow form column the same 1fr
        // share as the list column, so a list's table -- often the widest
        // thing on the page -- got squeezed into half the panel width.
        // Capping the form column keeps it comfortably readable while
        // handing the rest of the row to the list.
        "section[id^=\"panel-ml-\"].panel:has(>div:nth-child(2)){"
        "grid-template-columns:minmax(260px,22rem) minmax(0,1fr)}"
        "@media (max-width:900px){"
        "section[id^=\"panel-ml-\"].panel:has(>div:nth-child(2)){"
        "grid-template-columns:1fr}}"
        // Chat gets its own full-height flex column (centered empty-state
        // greeting, growing history, a pill composer pinned to the bottom)
        // instead of the generic multi-card grid every settings page uses,
        // so it reads like a normal chat client rather than a forms screen.
        "#chatShell{display:flex;flex-direction:column;flex:1;min-height:0;"
        "max-width:64rem;margin:0 auto;width:100%}"
        "#chatTopBar{margin-bottom:.5rem}"
        "#chatTopBar label{display:inline-flex;align-items:center;gap:.5rem;"
        "margin:0;color:var(--muted);font-size:.8rem;width:auto}"
        "#chatTopBar select{width:auto;padding:.3rem .5rem}"
        // The [hidden] attribute alone can't win here: this ID selector's own
        // display:flex outranks the UA stylesheet's [hidden]{display:none},
        // so the greeting stayed visible even after openChat() hid it. The
        // explicit :not([hidden]) rule below is what actually applies the
        // flex layout, leaving [hidden] free to hide it as normal.
        "#chatEmpty:not([hidden]){display:flex;align-items:center;"
        "justify-content:center;text-align:center}"
        "#chatEmpty{flex:1}"
        "#chatEmpty h1{font-size:1.8rem}"
        "#chatMessages{flex:1;overflow-y:auto;padding:.25rem .6rem;"
        "display:flex;flex-direction:column;gap:.35rem}"
        "#chatMessages:empty{flex:0}"
        ".chatMsg{position:relative;max-width:80%;margin:0;padding:.45rem .8rem;"
        "line-height:1.45;border-radius:.9rem;overflow-wrap:anywhere}"
        // Copy button sits in the bubble's own corner and stays out of the
        // way until hovered/focused -- matches the rest of the chat UI
        // staying uncluttered while every message and code block still
        // gets one.
        ".msgCopyBtn{position:absolute;top:.35rem;right:.5rem;margin:0;"
        "width:auto;padding:.15rem .5rem;font-size:.7rem;font-weight:600;"
        "border-radius:.4rem;border:1px solid var(--panel-border);"
        "background:rgba(0,0,0,.25);color:inherit;opacity:0;"
        "transition:opacity .15s}"
        ".chatMsg:hover>.msgCopyBtn,.msgCopyBtn:focus{opacity:1}"
        ".chatMsg-user>.msgCopyBtn{background:rgba(0,0,0,.25);color:#fff}"
        ".chatMsg-user{margin-left:auto;margin-right:.6rem;background:#3a3a3e;"
        "border:1px solid #505055;color:#fff}"
        ".chatMsg-assistant{margin-right:auto;background:var(--panel);"
        "border:1px solid var(--panel-border);color:#8fe6c9}"
        ".chatMsg-responseTitle{font-weight:800;letter-spacing:.03em;"
        "margin-bottom:.25rem;opacity:.85}"
        ".chatMsg-system{margin:0 auto;color:var(--muted);font-style:italic;"
        "background:none;white-space:pre-wrap}"
        ".chatMsg-error{background:#3a0a0a;border:1px solid #ffd54a;color:#ffd54a}"
        ".chatMsg-errorTitle{font-weight:800;letter-spacing:.03em;"
        "margin-bottom:.25rem}"
        ".chatMsg-modelChange{margin:0 auto;background:rgba(40,167,69,.12);"
        "border:1px solid rgba(40,167,69,.5);color:#4ade80;font-size:.82rem;"
        "text-align:center}"
        // Tool-call/tool-result cards: unlike .chatMsg-modelChange's short
        // centered one-liners, these can carry a real output block (a
        // run_command's stdout, a file's contents, ...) so they're
        // left-aligned and allowed the full bubble width rather than
        // being squeezed to 80% and centered.
        ".chatMsg-toolResult{max-width:100%;background:rgba(40,167,69,.08);"
        "border:1px solid rgba(40,167,69,.4);color:#4ade80;font-size:.82rem;"
        "text-align:left}"
        ".chatMsg-toolResultTitle{font-weight:700}"
        ".chatMsg-toolResult pre{margin-top:.4rem}"
        // Rendered Markdown structure inside a bubble: paragraphs/lists need
        // their own spacing since the bubble itself no longer relies on
        // white-space:pre-wrap for line breaks (renderMarkdown() emits real
        // <p>/<br>/<ul> elements instead).
        ".chatMsg p{margin:0 0 .5rem}.chatMsg>*:last-child{margin-bottom:0}"
        ".chatMsg ul,.chatMsg ol{margin:0 0 .5rem;padding-left:1.3rem}"
        ".chatMsg li{margin:.15rem 0}"
        ".chatMsg code{background:rgba(0,0,0,.25);border-radius:.25rem;"
        "padding:.1rem .3rem;font-size:.85em;font-family:ui-monospace,"
        "SFMono-Regular,Consolas,monospace}"
        // overflow-x/overflow-y are both set explicitly (rather than relying
        // on the shorthand from the generic pre{} rule above cascading in
        // for the axis this rule doesn't mention) so a long reply's code
        // block is unambiguously scrollable on both axes instead of being
        // clipped. overscroll-behavior stops the scroll from chaining into
        // the outer #chatMessages once the block's own scroll hits its end,
        // which otherwise makes it feel like the block itself won't scroll.
        ".chatMsg pre{position:relative;margin:0 0 .5rem;background:#0e0e11;"
        "border:1px solid var(--panel-border);border-radius:.5rem;"
        "padding:.6rem .75rem;overflow-x:auto;overflow-y:auto;max-height:24rem;"
        "overscroll-behavior:contain;scrollbar-width:thin}"
        ".chatMsg pre::-webkit-scrollbar{width:.5rem;height:.5rem}"
        ".chatMsg pre::-webkit-scrollbar-thumb{background:var(--panel-border);"
        "border-radius:.5rem}"
        ".chatMsg pre code{background:none;padding:0;color:#c8c8d4;"
        "white-space:pre}"
        // Same corner-button treatment as .msgCopyBtn, but scoped to the
        // individual code block so a snippet can be copied without the
        // surrounding reply -- visible on hovering the block itself,
        // independent of whether the whole bubble is hovered.
        ".codeCopyBtn{position:absolute;top:.4rem;right:.5rem;margin:0;"
        "width:auto;padding:.15rem .5rem;font-size:.7rem;font-weight:600;"
        "border-radius:.4rem;border:1px solid var(--panel-border);"
        "background:rgba(255,255,255,.08);color:#c8c8d4;opacity:0;"
        "transition:opacity .15s}"
        ".chatMsg pre:hover>.codeCopyBtn,.codeCopyBtn:focus{opacity:1}"
        // Same treatment as .codeCopyBtn, sat immediately to its left so the
        // two read as one action pair in the block's top-right corner.
        ".codeSaveBtn{position:absolute;top:.4rem;right:3.6rem;margin:0;"
        "width:auto;padding:.15rem .5rem;font-size:.7rem;font-weight:600;"
        "border-radius:.4rem;border:1px solid var(--panel-border);"
        "background:rgba(255,255,255,.08);color:#c8c8d4;opacity:0;"
        "transition:opacity .15s}"
        ".chatMsg pre:hover>.codeSaveBtn,.codeSaveBtn:focus{opacity:1}"
        // Best-effort inline/block math (see renderMathExpr()): a plain
        // serif-leaning span for inline expressions, a centered block for
        // \\[...\\] with a little breathing room above/below.
        ".math{font-family:'Cambria Math',ui-serif,Georgia,serif}"
        ".mathBlock{font-family:'Cambria Math',ui-serif,Georgia,serif;"
        "text-align:center;margin:.5rem 0;padding:.3rem 0}"
        // A hand-rolled fraction: numerator over denominator with a rule
        // between them, since there is no TeX engine here to lay one out.
        ".frac{display:inline-flex;flex-direction:column;vertical-align:middle;"
        "text-align:center;margin:0 .15em;line-height:1.1}"
        ".fracNum,.fracDen{display:block;padding:0 .2em}"
        ".fracNum{border-bottom:1px solid currentColor}"
        // The "Thinking..." placeholder shown in the assistant bubble from
        // the moment a message is sent until the first token streams back
        // (see streamMessage()) -- a spinner plus label instead of an
        // empty-looking bubble, since reasoning-heavy models can take a
        // real amount of time before their first visible token.
        ".chatThinking{display:inline-flex;align-items:center;gap:.5rem;"
        "color:var(--muted)}"
        ".chatThinkingSpinner{width:.9rem;height:.9rem;flex:none;"
        "border-radius:50%;border:2px solid currentColor;"
        "border-top-color:transparent;animation:chatThinkingSpin .7s linear infinite;"
        // will-change promotes the spinner to its own compositor layer so its
        // rotation keeps animating smoothly off the main thread even while
        // token/tool events are mutating the surrounding DOM (scrollTop,
        // sibling renders) -- without it, the browser can fold the spin into
        // the same paint as those mutations and it reads as jerky/stepped.
        "will-change:transform}"
        "@keyframes chatThinkingSpin{to{transform:rotate(360deg)}}"
        // The model's own <think>...</think> reasoning (see renderMarkdown()),
        // shown as a collapsed-by-default panel above the final answer --
        // same idea as claude.ai's extended-thinking section: present, but
        // visually secondary to the actual response. Stays open while the
        // block is still streaming in (no closing tag yet) so reasoning is
        // visible as it's produced, then collapses once the answer starts.
        ".thinkBlock{margin:0 0 .6rem;border:1px solid var(--panel-border);"
        "border-radius:.5rem;background:rgba(255,255,255,.03)}"
        ".thinkBlock>summary{cursor:pointer;list-style:none;padding:.35rem .6rem;"
        "font-size:.8rem;font-weight:600;color:var(--muted)}"
        ".thinkBlock>summary::-webkit-details-marker{display:none}"
        ".thinkBlock>summary::before{content:'\\25b8';display:inline-block;"
        "margin-right:.4rem;transition:transform .15s}"
        ".thinkBlock[open]>summary::before{transform:rotate(90deg)}"
        ".thinkBlock .thinkBody{padding:0 .6rem .6rem;font-size:.85rem;"
        "color:var(--muted);font-style:italic}"
        ".thinkBlock .thinkBody p{margin:0 0 .4rem}"
        // The live thinking panel (built token-by-token by ensureThinkBlock()/
        // feedThinkingChunk() in the script above, before the reply is
        // complete) eases in rather than popping into existence the instant
        // reasoning starts, and its summary label breathes gently while open
        // so the panel visibly reads as "still working" the same way the
        // Thinking spinner above the bubble already does -- without a full
        // per-token markdown re-render, this is the UI's only other signal
        // that generation is actively producing something.
        ".thinkBlockLive{animation:chatBlockFadeIn .25s ease}"
        "@keyframes chatBlockFadeIn{from{opacity:0;transform:translateY(-3px)}"
        "to{opacity:1;transform:translateY(0)}}"
        ".thinkBlockLive[open]>summary{animation:chatThinkPulse 1.6s ease-in-out "
        "infinite}"
        "@keyframes chatThinkPulse{0%,100%{opacity:.6}50%{opacity:1}}"
        // The composer: a single rounded pill carrying the attach toggle,
        // the message box, the model picker, and send/cancel -- no separate
        // "start chat" form above it.
        ".composer{position:relative;display:flex;align-items:center;gap:.4rem;"
        "background:var(--panel);border:1px solid var(--panel-border);"
        "border-radius:1.5rem;padding:.4rem .5rem .4rem 1rem}"
        // Floats above the composer pill (anchored to its bottom-right
        // corner via the parent's position:relative above) instead of
        // pushing the message list around when it opens.
        ".modelSettingsPanel{position:absolute;bottom:100%;right:0;"
        "margin-bottom:.5rem;width:14rem;background:var(--panel);"
        "border:1px solid var(--panel-border);border-radius:.75rem;"
        "padding:.75rem .9rem;box-shadow:0 8px 24px rgba(0,0,0,.4);"
        "display:flex;flex-direction:column;gap:.4rem;z-index:20}"
        // display:flex above beats the browser's default display:none for
        // [hidden] (author styles win over the UA stylesheet), so without
        // this the panel showed regardless of its hidden attribute --
        // including on page load and after the close button set it.
        ".modelSettingsPanel[hidden]{display:none}"
        ".modelSettingsPanel label{margin-top:.3rem}"
        ".modelSettingsPanelHeader{display:flex;align-items:center;"
        "justify-content:space-between;font-size:.75rem;color:var(--muted);"
        "text-transform:uppercase;letter-spacing:.05em}"
        ".modelSettingsClose{width:auto;margin:0;padding:0 .3rem;"
        "background:transparent;color:var(--muted);font-weight:700;"
        "border:none;cursor:pointer}"
        ".modelSettingsNote{margin:.2rem 0 0;font-size:.75rem;"
        "color:var(--muted)}"
        // Gives the prompt text box its own layout container, distinct from
        // the attach/model-picker/settings/send controls it sits beside in
        // the shared .composer pill -- those stay flex:none siblings; this
        // wrapper alone claims the remaining row width, so the textarea
        // inside it is never sharing a flex slot with anything else.
        ".composerInputWrap{flex:1;min-width:0;display:flex}"
        ".composerInput{flex:1;min-width:0;border:none;background:transparent;"
        "resize:none;max-height:8rem;padding:.6rem 0;margin:0;box-shadow:none}"
        ".composerInput:focus{box-shadow:none}"
        ".composerModelPicker{width:auto;max-width:11rem;border:none;"
        "background:transparent;color:var(--muted);font-size:.8rem;margin:0;"
        "padding:.3rem .4rem}"
        ".composerIconBtn,.composerSendBtn{width:2.25rem;height:2.25rem;flex:none;"
        "border-radius:50%;padding:0;margin:0;font-size:1.15rem;line-height:1;"
        "display:flex;align-items:center;justify-content:center}"
        ".composerIconBtn{background:transparent;color:var(--muted);"
        "border:1px solid var(--panel-border)}"
        ".composerIconBtn:hover{background:var(--bg)}"
        ".visuallyHidden{position:absolute;width:1px;height:1px;padding:0;"
        "margin:-1px;overflow:hidden;clip:rect(0,0,0,0);border:0}"
        // Pending attachments render as removable chips between the message
        // history and the composer, mirroring the mainstream chat clients
        // this is meant to match -- the file itself has already been
        // uploaded by the time its chip appears (see attachFile()), so
        // removing a chip only drops it from this message, not the store.
        ".attachChips{display:flex;flex-wrap:wrap;gap:.4rem}"
        ".attachChips:empty{display:none}"
        ".attachChip{display:inline-flex;align-items:center;gap:.4rem;"
        "background:var(--panel);border:1px solid var(--panel-border);"
        "border-radius:1rem;padding:.25rem .4rem .25rem .75rem;"
        "font-size:.8rem;max-width:16rem}"
        ".attachChip span{overflow:hidden;text-overflow:ellipsis;"
        "white-space:nowrap}"
        ".attachChip button{width:1.3rem;height:1.3rem;flex:none;padding:0;"
        "margin:0;border-radius:50%;border:none;background:transparent;"
        "color:var(--muted);line-height:1;font-size:1rem}"
        ".attachChip button:hover{background:var(--bg)}"
        // Tables replace the raw JSON dumps every list page used to show --
        // this is a user-facing screen, not a debugging console.
        "table{width:100%;border-collapse:collapse;font-size:.85rem}"
        "th,td{text-align:left;padding:.5rem .6rem;border-bottom:1px solid "
        "var(--panel-border)}"
        "th{color:var(--muted);font-weight:600;text-transform:uppercase;"
        "font-size:.7rem;letter-spacing:.05em}"
        ".stateTag{padding:.15rem .5rem;border-radius:1rem;font-size:.75rem}"
        ".stateTag-ready,.stateTag-approved,.stateTag-production,"
        ".stateTag-verified{background:#0d3321;color:#5fe3a4}"
        ".stateTag-invalid,.stateTag-failed,.stateTag-quarantined,"
        ".stateTag-rejected,.stateTag-missing{background:#3a1414;color:#f299a0}"
        ".stateTag-downloading,.stateTag-unverified,.stateTag-training,"
        ".stateTag-evaluation,.stateTag-pending,.stateTag-imported,"
        ".stateTag-staging{background:#3a2f0d;color:#f2c96d}"
        "#hfFields,#githubFields{border:1px solid var(--panel-border);"
        "border-radius:.5rem;padding:.5rem .75rem;margin-top:.5rem}"
        // System Report: each section is its own bordered card with a fixed
        // label-column width shared across every card, so values line up
        // in a single straight column instead of each section's table
        // sizing its own first column independently off whatever labels
        // happen to be longest in that section.
        "#systemReport{display:flex;flex-direction:column;gap:1rem}"
        // Card styling for .reportSection used to be scoped to #systemReport
        // alone, so every other page reusing the same class (Performance,
        // Benchmarks, API Reference) rendered as unstyled text with no
        // card background/border -- promoted to a bare class selector so
        // the card look is consistent everywhere the class is used, with
        // a matching vertical rhythm between stacked cards on those pages.
        ".reportSection{background:var(--panel);"
        "border:1px solid var(--panel-border);border-radius:.6rem;"
        "padding:.9rem 1.1rem;margin-bottom:1rem}"
        ".reportSection h3{margin:0 0 .4rem;font-size:.75rem;"
        "font-weight:600;text-transform:uppercase;letter-spacing:.05em;"
        "color:var(--muted)}"
        ".reportSection p{font-size:.85rem;line-height:1.5}"
        ".reportSection:last-child{margin-bottom:0}"
        // #systemReport already spaces its stacked cards with its own
        // flex gap, so the base class's margin-bottom would double up --
        // cancelled here to keep that page's existing rhythm unchanged.
        "#systemReport .reportSection{margin-bottom:0}"
        "#systemReport table{table-layout:fixed}"
        "#systemReport td{border-bottom:1px solid var(--panel-border);"
        "vertical-align:top}"
        "#systemReport tr:last-child td{border-bottom:none}"
        "#systemReport td.reportLabel{width:18rem;color:var(--muted)}"
        "#systemReport td.reportValue{font-variant-numeric:tabular-nums}"
        "@media (max-width:640px){"
        "#systemReport td.reportLabel{width:9rem}}"
        "#memoryStatusSection td.reportLabel{width:12rem;color:var(--muted)}"
        "#memoryStatusSection td.reportValue{font-variant-numeric:tabular-nums}"
        "@media (max-width:640px){"
        "#memoryStatusSection td.reportLabel{width:8rem}}"
        // The sidebar's user-resized width (persisted in localStorage, see
        // the sidebar script below) can otherwise still eat most of a
        // narrow window's space -- these two breakpoints claw it back
        // before content is squeezed to nothing.
        "@media (max-width:900px){#sidebar{width:12rem!important}}"
        "@media (max-width:640px){#shell{flex-direction:column;height:auto;"
        "min-height:100vh}"
        "#sidebar{width:auto!important;max-height:40vh;"
        "border-right:none;border-bottom:1px solid var(--panel-border)}"
        "#sidebarResizer{display:none}}"
        "</style></head><body>"
        "<div id=\"shell\" data-role=\"" + role_attr + "\"><nav id=\"sidebar\">"
        // The sidebar's width and scroll position both persist client-side
        // (see the sidebar script in application_script()): a drag handle
        // (#sidebarResizer below) lets the user widen or narrow the
        // sidebar, and both settings survive the full-page navigation every
        // sidebar click performs.
        "<h1>MasterAI</h1><p id=\"who\">" +
        html_escape(user.display_name) +
        "</p>" +
        sidebar_links +
        "<p id=\"sidebarCredit\" style=\"margin-top:auto;padding-top:1rem;"
        "font-size:.65rem;color:var(--muted);\">"
        "Developed by Daniel J. Hobson, Australia, 2026</p>"
        "</nav><div id=\"sidebarResizer\" "
        "title=\"Drag to resize the sidebar\"></div><main id=\"content\">"
        "<p id=\"actionStatus\" role=\"status\"></p>" +
        body +
        "</main></div><script src=\"/assets/app.js\"></script></body></html>");
}

}  // namespace masterai::server_internal
