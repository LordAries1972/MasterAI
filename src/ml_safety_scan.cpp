// Phase 74: real, local, heuristic content scanning for Safety and
// Governance (see the ContentScanReport/scan_content_for_risks block
// comment in masterai.hpp). Deliberately hand-rolled pattern matching, not
// std::regex (this codebase does not use <regex> anywhere else, favoring
// explicit character scanning the same way parse_tabular_csv's CSV splitter
// does) and not an ML classifier -- every finding here is a reproducible
// match against the actual text scanned, never a fabricated risk score.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <cctype>

namespace masterai {
namespace {

std::string lower(const std::string& value) {
    std::string result = value;
    for (char& character : result) {
        character =
            static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return result;
}

bool is_token_char(const char character) {
    return std::isalnum(static_cast<unsigned char>(character)) != 0 ||
           character == '_' || character == '-';
}

bool starts_with(const std::string& value, std::size_t offset,
                 const std::string& prefix) {
    if (offset + prefix.size() > value.size()) return false;
    return value.compare(offset, prefix.size(), prefix) == 0;
}

bool has_digit_and_letter(const std::string& value) {
    bool digit = false;
    bool letter = false;
    for (const char character : value) {
        if (std::isdigit(static_cast<unsigned char>(character))) digit = true;
        if (std::isalpha(static_cast<unsigned char>(character))) letter = true;
    }
    return digit && letter;
}

// Scans every maximal run of token characters (alnum/-/_) in `text` for
// secret-shaped patterns: a handful of well-known provider key prefixes
// (AWS AKIA, OpenAI-style sk-, GitHub ghp_) checked by prefix+length, plus
// a generic "looks like a random token" fallback (32+ characters, mixed
// letters and digits) that catches unlabeled API keys/JWTs/etc. without
// claiming to know their provider.
void scan_secret_tokens(const std::string& text,
                        std::vector<ContentScanFinding>& findings) {
    std::size_t index = 0U;
    while (index < text.size()) {
        if (!is_token_char(text[index])) {
            ++index;
            continue;
        }
        std::size_t end = index;
        while (end < text.size() && is_token_char(text[end])) ++end;
        const std::string token = text.substr(index, end - index);
        if (token.size() >= 20U && starts_with(token, 0U, "AKIA")) {
            findings.push_back(
                {"secret", "text contains an AWS-access-key-shaped token "
                           "(starts with AKIA, " +
                               std::to_string(token.size()) + " characters)"});
        } else if (token.size() >= 23U && starts_with(token, 0U, "sk-")) {
            findings.push_back(
                {"secret", "text contains an API-key-shaped token (starts "
                           "with sk-, " +
                               std::to_string(token.size()) + " characters)"});
        } else if (token.size() == 40U && starts_with(token, 0U, "ghp_")) {
            findings.push_back(
                {"secret", "text contains a GitHub-personal-access-token-"
                           "shaped token (starts with ghp_)"});
        } else if (token.size() >= 32U && has_digit_and_letter(token)) {
            findings.push_back(
                {"secret", "text contains a " + std::to_string(token.size()) +
                               "-character token that looks like a random "
                               "secret/API key (mixed letters and digits, no "
                               "spaces)"});
        }
        index = end;
    }
    if (text.find("-----BEGIN") != std::string::npos) {
        findings.push_back(
            {"secret", "text contains a PEM key/certificate block "
                       "(\"-----BEGIN\")"});
    }
}

// Case-insensitive substring search for a small, documented set of
// instruction-override/role-override phrasings. This is a heuristic
// approximation of prompt-injection detection, not a classifier: it will
// miss paraphrased or non-English injection attempts, and it will
// occasionally flag legitimate text that happens to use one of these
// phrases (e.g. a dataset about prompt-injection itself). Both are stated
// honestly rather than silently over-claiming coverage.
void scan_prompt_injection(const std::string& text,
                           std::vector<ContentScanFinding>& findings) {
    static const std::vector<std::string> phrases{
        "ignore previous instructions",
        "ignore all previous instructions",
        "ignore the above instructions",
        "disregard previous instructions",
        "disregard the system prompt",
        "disregard your instructions",
        "forget your instructions",
        "forget previous instructions",
        "override your instructions",
        "you are now",
        "act as if you have no restrictions",
        "pretend you have no restrictions",
        "you have no restrictions",
        "reveal your system prompt",
        "print your system prompt",
        "this is a new system prompt",
    };
    const auto haystack = lower(text);
    for (const auto& phrase : phrases) {
        if (haystack.find(phrase) != std::string::npos) {
            findings.push_back(
                {"prompt_injection",
                "text contains the phrase \"" + phrase + "\""});
        }
    }
}

// Splits a SafetyPolicy's comma-separated restricted_data_categories and
// searches for each term verbatim (case-insensitive) in `text`. Callers
// must check policy.enforcement_enabled before calling this -- it is not
// checked here so this function stays a pure term search.
void scan_restricted_terms(const std::string& text, const SafetyPolicy& policy,
                           std::vector<ContentScanFinding>& findings) {
    const auto haystack = lower(text);
    std::size_t start = 0U;
    while (start <= policy.restricted_data_categories.size()) {
        const auto comma = policy.restricted_data_categories.find(',', start);
        const auto end = comma == std::string::npos
                             ? policy.restricted_data_categories.size()
                             : comma;
        std::string term = policy.restricted_data_categories.substr(start, end - start);
        std::size_t begin = 0U;
        while (begin < term.size() && std::isspace(static_cast<unsigned char>(term[begin]))) ++begin;
        std::size_t stop = term.size();
        while (stop > begin && std::isspace(static_cast<unsigned char>(term[stop - 1U]))) --stop;
        term = term.substr(begin, stop - begin);
        if (!term.empty() && haystack.find(lower(term)) != std::string::npos) {
            findings.push_back(
                {"restricted_term",
                "text contains the policy-restricted term \"" + term + "\""});
        }
        if (comma == std::string::npos) break;
        start = comma + 1U;
    }
}

}  // namespace

ContentScanReport scan_content_for_risks(const std::string& text,
                                         const SafetyPolicy* policy) {
    ContentScanReport report;
    // Secret-token and prompt-injection-phrase scanning are the baseline
    // and always run, regardless of any policy's enforcement_enabled flag
    // -- only a policy's own configured restricted terms can be disabled.
    scan_secret_tokens(text, report.findings);
    scan_prompt_injection(text, report.findings);
    if (policy != nullptr && policy->enforcement_enabled) {
        scan_restricted_terms(text, *policy, report.findings);
    }
    return report;
}

std::string content_scan_report_json(const ContentScanReport& report) {
    std::string body = "{\"clean\":" + std::string(report.clean() ? "true" : "false") +
                       ",\"findings\":[";
    for (std::size_t index = 0; index < report.findings.size(); ++index) {
        if (index > 0U) body += ",";
        const auto& finding = report.findings[index];
        std::string escaped_detail;
        escaped_detail.reserve(finding.detail.size());
        for (const char character : finding.detail) {
            if (character == '"' || character == '\\') escaped_detail += '\\';
            escaped_detail += character;
        }
        body += "{\"category\":\"" + finding.category + "\",\"detail\":\"" +
               escaped_detail + "\"}";
    }
    return body + "]}";
}

namespace {

// Judge models occasionally wrap valid JSON in prose ("Here is the
// analysis:\n{...}\nLet me know..."); extracting the outermost {...} span
// before parsing tolerates that without trying to fully constrain the
// model's output format.
std::string extract_json_object(const std::string& text) {
    const auto open = text.find('{');
    const auto close = text.rfind('}');
    if (open == std::string::npos || close == std::string::npos || close < open) {
        return {};
    }
    return text.substr(open, close - open + 1U);
}

void append_classifier_category(const JsonValue& root, const char* key,
                                const char* category, const double confidence_floor,
                                std::vector<ModelClassifierFinding>& findings) {
    const auto* entry = root.optional(key);
    if (entry == nullptr) return;
    const auto* confidence_value = entry->optional("confidence");
    if (confidence_value == nullptr) return;
    const double confidence = confidence_value->as_double();
    if (confidence < confidence_floor) return;
    const auto* rationale_value = entry->optional("rationale");
    findings.push_back(ModelClassifierFinding{
        category, confidence, rationale_value ? rationale_value->as_string() : std::string{}});
}

}  // namespace

ModelClassifierReport scan_content_with_model_classifier(
    const std::string& text,
    const std::function<std::string(const std::string&)>& generate,
    const double confidence_floor) {
    ModelClassifierReport report;
    // Bounded so a large document cannot blow out the judge prompt's own
    // context budget; the judge is scoring risk signal, not proofreading
    // every byte, so a representative excerpt is sufficient.
    constexpr std::size_t kMaxJudgedCharacters = 4000U;
    const auto excerpt = text.size() > kMaxJudgedCharacters
                             ? text.substr(0U, kMaxJudgedCharacters) + " [truncated]"
                             : text;
    const std::string prompt =
        "You are a strict content-safety classifier. Analyze the TEXT below "
        "for three categories: bias (unfair skew toward/against a group or "
        "viewpoint), hallucination_risk (confident claims that read as "
        "unverifiable or fabricated), and harmful_content (content that is "
        "harmful in a subtler way than an explicit slur or threat, e.g. "
        "dangerous advice framed as helpful). Reply with ONLY one JSON "
        "object, no other text, in exactly this shape: "
        "{\"bias\":{\"confidence\":0.0,\"rationale\":\"\"},"
        "\"hallucination_risk\":{\"confidence\":0.0,\"rationale\":\"\"},"
        "\"harmful_content\":{\"confidence\":0.0,\"rationale\":\"\"}} "
        "where each confidence is a number from 0.0 (not present) to 1.0 "
        "(certainly present) and each rationale is one short sentence.\n\n"
        "TEXT:\n" + excerpt;
    std::string reply;
    try {
        reply = generate(prompt);
    } catch (const std::exception& error) {
        report.available = false;
        report.diagnostic = std::string("classifier generation failed: ") + error.what();
        return report;
    }
    const auto json_text = extract_json_object(reply);
    if (json_text.empty()) {
        report.available = false;
        report.diagnostic = "classifier reply did not contain a JSON object";
        return report;
    }
    try {
        const auto root = parse_json(json_text);
        append_classifier_category(root, "bias", "bias", confidence_floor, report.findings);
        append_classifier_category(root, "hallucination_risk", "hallucination_risk",
                                   confidence_floor, report.findings);
        append_classifier_category(root, "harmful_content", "harmful_content",
                                   confidence_floor, report.findings);
        report.available = true;
    } catch (const std::exception& error) {
        report.available = false;
        report.diagnostic = std::string("classifier reply JSON was malformed: ") + error.what();
    }
    return report;
}

std::string model_classifier_report_json(const ModelClassifierReport& report) {
    std::string findings_json = "[";
    for (std::size_t index = 0U; index < report.findings.size(); ++index) {
        if (index != 0U) findings_json += ",";
        const auto& finding = report.findings[index];
        findings_json += "{\"category\":" + json_string(finding.category) +
                         ",\"confidence\":" + std::to_string(finding.confidence) +
                         ",\"rationale\":" + json_string(finding.rationale) + "}";
    }
    findings_json += "]";
    return "{\"available\":" + std::string(report.available ? "true" : "false") +
          ",\"diagnostic\":" + json_string(report.diagnostic) +
          ",\"findings\":" + findings_json + "}";
}

}  // namespace masterai
