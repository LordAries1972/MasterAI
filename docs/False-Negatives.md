# SlopGuard Report Audit — 2026-08-06

Audit of `docs/slopguard-report.md` (generated 2026-08-05T22:15:00Z from
`.agent-coder/slop-guard/findings.json`, 1166 total findings: 829 defect +
337 review-signal). Every finding was checked against the actual file at the
cited path. Result: **9 real defects, all now fixed**, and **820 defect
findings (99% of the "defect" total) that are noise** — they point at a
captured third-party Microsoft Edge browser profile, not MasterAI source.
This file exists to feed that noise-source back into Agent-Coder/SlopGuard so
it can exclude the offending path from future scans.

## Summary

| Bucket | Count | Disposition |
|---|---|---|
| Real defects in MasterAI source (`src/*.cpp`) | 9 | **Fixed** (this session) |
| Noise: defects inside `runtime/validation/**` (captured Edge browser profile) | 820 | Not fixed — not project source. See "Noise source" below. |
| Review signals in MasterAI source (duplication, nesting, complexity, file size) | 163 | Not defects; informational only, left as-is |
| Review signals inside `runtime/validation/**` | 174 | Same noise source as above |
| **Total** | **1166** | |

## 1. Real defects — confirmed and fixed

All 9 of the report's "defect"-classified findings that point at actual
MasterAI source were the same rule, `quality-noEmptyExceptionHandlers`
("Empty exception handler swallows the error silently"). All 9 were
verified against the live file content and were genuinely empty
`catch` blocks. Each has been fixed by logging the swallowed exception
(`std::cerr` in C++, `console.warn` in the embedded web-UI JavaScript)
while preserving the original best-effort behavior — none of these paths
should abort or rethrow, they just weren't leaving a trace when they failed.

| # | File | Line | Original code | Fix |
|---|---|---|---|---|
| 1 | `src/server.cpp` | 4557 | `warm_model_async()` background thread: `catch (...) { }` around `ensure_model_loaded()` | Logs the caught exception (or "unknown exception" for non-`std::exception` throws) to `std::cerr` before clearing `model_warm_in_progress`. |
| 2 | `src/server.cpp` | 5529 | Chat-generation failure handler: `catch (const std::exception&) { }` around `queries.finish(query_id, QueryStatus::failed, ...)` | Logs the secondary failure to `std::cerr`; still doesn't rethrow, so the original `failure_reason` remains what's surfaced to the client. |
| 3 | `src/server.cpp` | 5540 | Same handler: `catch (const std::exception&) { }` around `chats->append(chat_id, ChatRole::assistant, streamed_text)` | Logs the append failure to `std::cerr`. |
| 4 | `src/downloads.cpp` | 392 | Post-download best-effort verification cache write: `catch (const std::exception&) { }` around `record_verified_model(...)` | Logs the failure to `std::cerr`, explicitly noting the model still downloaded successfully and will just show as unverified until the next `verify-models` run. |
| 5 | `src/web_ui.cpp` | 106 | Embedded JS `saveModelSettings()`: `catch(x){}` around `JSON.parse(localStorage.getItem('modelSettings')...)` | `console.warn('saveModelSettings: corrupt modelSettings, resetting', x)`. |
| 6 | `src/web_ui.cpp` | 232 (was 233 pre-edit) | Embedded JS `initLogin()`: `catch(x){}` around the `/health/ready` fetch | `console.warn('initLogin: /health/ready check failed, defaulting to login form', x)`. |
| 7 | `src/web_ui.cpp` | 2498 (was 2499) | Embedded JS download-progress poll `tick()`: `catch(x){}` | `console.warn('download poll tick failed, retrying next interval', x)`. |
| 8 | `src/web_ui.cpp` | 2766 (was 2767) | Embedded JS `selectDefaultChatModel()`: `catch(x){}` around the runner-status fetch | `console.warn('selectDefaultChatModel: runner status check failed, falling back to last chat model', x)`. |
| 9 | `src/web_ui.cpp` | 2794 (was 2795) | Embedded JS `pollRunnerStatus()`: `catch(x){}` | `console.warn('pollRunnerStatus: status poll failed, retrying next interval', x)`. |

Verified after the fix: `grep` for empty-catch patterns
(`catch(x){}`, `catch (const std::exception&) { }`, `catch (...) { }`)
across `src/` now returns zero matches.

## 2. Noise source — captured Edge browser profile, not project code

**820 of the 829 "defect" findings (99%)** — and 174 of the 337
review-signal findings — point at files under:

```
runtime/validation/phase5-browser-20260805-2/edge-profile/**
```

This directory is a **captured Microsoft Edge user-data profile** from a
Phase 5 browser-automation validation run (per
`.agent-coder/slop-guard/findings.json`, findings were `detectedAt:
2026-08-05T22:14:...Z`, matching a validation run, not a code-authoring
session). It contains ~1765 files total: Edge's own bundled extensions
(Edge Shopping, Edge Wallet, Edge Travel, Speech Recognition, SafetyTips,
etc.), a third-party AI-assistant extension ("Monica",
`fhimbbbmdjiifimnepkibjfjbppnjble`), a download-manager integration
extension (`ahmpjcflkgiildlgicmcieglgoilbfdp`), hyphenation dictionaries,
fonts, DLLs, and other Chromium component data. None of it is MasterAI
source or something this project maintains or can fix — it's a frozen
snapshot of Microsoft's and third parties' shipped, minified/bundled JS.

SlopGuard scanned it as if it were project source and flagged every empty
`catch` block and one `execCommand` call inside Microsoft's own minified
bundles as MasterAI defects, which is what dragged **Error Handling &
Recovery down to 0/100** in the report.

### Breakdown (820 defect findings)

| Rule | Count |
|---|---|
| `quality-noEmptyExceptionHandlers` | 818 |
| `quality-noDangerousFunctions` (`execCommand` in `content.js:2170`, `content.js:5081`) | 2 |

### Affected files (30 unique files, all under the same captured profile)

| Findings | File |
|---|---|
| 207 | `.../Default/Extensions/fhimbbbmdjiifimnepkibjfjbppnjble/8.0.1_0/content.js` (Monica extension) |
| 83 | `.../Edge Shopping/2.1.108.0/auto_open_controller.js` |
| 72 | `.../Edge Shopping/2.1.108.0/product_page.js` |
| 71 | `.../Edge Shopping/2.1.108.0/edge_driver.js` |
| 70 | `.../Edge Shopping/2.1.108.0/edge_checkout_page_validator.js` |
| 70 | `.../Edge Shopping/2.1.108.0/edge_confirmation_page_validator.js` |
| 44 | `.../Edge Wallet/128.18367.18366.1/vendor.bundle.js` |
| 43 | `.../Edge Wallet/128.18367.18366.1/Wallet-Checkout/wallet-drawer.bundle.js` |
| 22 | `.../Default/Extensions/fhimbbbmdjiifimnepkibjfjbppnjble/8.0.1_0/monicaPopup.js` |
| 21 | `.../Default/Extensions/fhimbbbmdjiifimnepkibjfjbppnjble/8.0.1_0/background.js` |
| 16 | `.../Edge Wallet/128.18367.18366.1/Notification/notification.bundle.js` |
| 16 | `.../Edge Wallet/128.18367.18366.1/bnpl/bnpl.bundle.js` |
| 12 | `.../Edge Shopping/2.1.108.0/edge_tracking_page_validator.js` |
| 11 | `.../Edge Wallet/128.18367.18366.1/Tokenized-Card/tokenized-card.bundle.js` |
| 9 | `.../Edge Wallet/128.18367.18366.1/wallet.bundle.js` |
| 8 | `.../Edge Wallet/128.18367.18366.1/edge_driver.js` |
| 7 | `.../Edge Shopping/2.1.108.0/shopping_iframe_driver.js` |
| 6 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/fdmcontextmenumgr.js` |
| 5 | `.../Edge Wallet/128.18367.18366.1/Wallet-Checkout/load-ec-deps.bundle.js` |
| 5 | `.../Edge Wallet/128.18367.18366.1/Wallet-Checkout/load-ec-i18n.bundle.js` |
| 4 | `.../Edge Wallet/128.18367.18366.1/Mini-Wallet/miniwallet.bundle.js` |
| 4 | `.../Edge Wallet/128.18367.18366.1/Notification/notification_fast.bundle.js` |
| 4 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/contextmenumgr.js` |
| 2 | `.../Edge Wallet/128.18367.18366.1/bnpl_driver.js` |
| 2 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/nativehostmgr.js` |
| 2 | `.../Edge Wallet/128.18367.18366.1/wallet_donation_driver.js` |
| 1 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/netwrkmon.js` |
| 1 | `.../Edge Wallet/128.18367.18366.1/buynow_driver.js` |
| 1 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/misc.js` |
| 1 | `.../Default/Extensions/ahmpjcflkgiildlgicmcieglgoilbfdp/3.3.1_0/src/js/RequestsManager.js` |

(All paths above are relative to
`runtime/validation/phase5-browser-20260805-2/edge-profile/`.)

### Recommended fix in Agent-Coder / SlopGuard

`.agent-coder/slop-guard.json` currently has no path-exclusion mechanism
exercised (`pathOverrides: []`, no ignore/exclude list at all):

```json
{
  "enabled": true,
  "blockCriticalFindings": true,
  "maximumFileLines": 1500,
  "maximumNestingDepth": 6,
  "maximumDuplicateBlockLines": 6,
  "suppressedRuleIds": [],
  "pathOverrides": [],
  "routingFeedbackEnabled": true,
  "authorshipCalibrationEnabled": true,
  "refinementLoopEnabled": true
}
```

The scanner should exclude `runtime/` (or at minimum
`runtime/validation/**`) the same way it presumably already excludes
`node_modules/` or build output — this directory holds captured
browser-automation artifacts (profiles, extension bundles, binary data),
not authored or vendored project dependencies. Until that exclusion exists,
every future browser-validation run will keep re-flagging Microsoft's own
extension bundles as MasterAI "Error Handling & Recovery" defects and
sinking that score to 0/100 regardless of the actual project's code health.

## 3. Review signals in MasterAI source (not defects, left as-is)

The remaining 163 review-signal findings against real `src/*.cpp` files
are informational structural signals, not confirmed defects (the report
itself labels them "not scored" and "review signal(s) ... not defects
unless later semantic evidence validates them"):

| Rule | Count | What it measures |
|---|---|---|
| `duplicate-code-block` | 86 | Repeated multi-line blocks (mostly in `src/ml.cpp`, `src/server.cpp`, `test/tests.cpp` — largely declarative/symmetric branch code) |
| `quality-maxFunctionComplexity` | 45 | File-wide decision-point count over the configured limit (heuristic, not per-function) |
| `quality-maxNestingDepth` | 27 | Brace nesting depth over the configured limit of 5 |
| `file-size-growth` | 5 | Files over the 1500-line threshold (`src/server.cpp`, `src/ml.cpp`, `src/masterai.hpp`, `src/web_ui.cpp`, `test/tests.cpp`) |

None of these were treated as confirmed defects per the report's own
methodology, so none were changed in this pass.
