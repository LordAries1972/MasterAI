// Phase 23: compiled chat-template execution plans, segmented prompt
// assembly over shared immutable buffers, and a small restricted intern
// table.
//
// This file deliberately does not depend on server.cpp or HttpServer::State
// -- everything here operates on the plain masterai.hpp types
// (ChatWrapTemplate/ChatMessage/PromptSegment/BufferView/SharedBuffer) so it
// can be exercised directly by tests without spinning up a full server.
#include "masterai.hpp"

#include <map>
#include <mutex>
#include <set>
#include <stdexcept>

namespace masterai {
namespace {

// Guards the process-lifetime compiled-template cache below. Contention is
// irrelevant here: lookups are a small map find, and the map only ever
// grows to the handful of distinct architectures actually seen.
std::mutex g_template_plan_mutex;
std::map<std::string, ChatTemplatePlan> g_template_plans;

// Copies one literal string into its own immutable SharedBuffer and wraps
// it as a whole-buffer BufferView. Used only for the small, fixed set of
// per-architecture template literals, each copied exactly once (at
// compile-plan build time) rather than once per message the way the old
// std::string += approach implicitly did.
BufferView literal_view(const std::string& text) {
    return BufferView(SharedBuffer::copy_from(text.data(), text.size()), 0U,
                      text.size(), "utf-8");
}

}  // namespace

const ChatTemplatePlan& compiled_chat_template(const std::string& architecture,
                                               const ChatWrapTemplate& tmpl) {
    std::lock_guard<std::mutex> lock(g_template_plan_mutex);
    const auto found = g_template_plans.find(architecture);
    if (found != g_template_plans.end()) return found->second;
    ChatTemplatePlan plan;
    plan.system_prefix = literal_view(tmpl.system_prefix);
    plan.system_suffix = literal_view(tmpl.system_suffix);
    plan.user_prefix = literal_view(tmpl.user_prefix);
    plan.user_suffix = literal_view(tmpl.user_suffix);
    plan.assistant_prefix = literal_view(tmpl.assistant_prefix);
    plan.assistant_suffix = literal_view(tmpl.assistant_suffix);
    plan.generation_prompt = literal_view(tmpl.generation_prompt);
    plan.stop_sequence = tmpl.stop_sequence;
    const auto inserted = g_template_plans.emplace(architecture, std::move(plan));
    return inserted.first->second;
}

std::vector<PromptSegment> assemble_chat_prompt_segments(
    const ChatTemplatePlan& plan, const std::vector<ChatMessage>& history,
    const std::string& latest_user_content) {
    std::vector<PromptSegment> segments;
    // Three segments (prefix/content/suffix) per history message, plus the
    // same three for the latest turn, plus the generation-prompt marker.
    segments.reserve(history.size() * 3U + 4U);
    // Wraps one message's own content bytes in a fresh SharedBuffer -- see
    // the header comment on assemble_chat_prompt_segments() for why this
    // one case still copies (the source std::string does not outlive this
    // call, unlike the plan's literal buffers).
    const auto push_variable = [&](const std::string& content) {
        segments.push_back(
            {BufferView(SharedBuffer::copy_from(content.data(), content.size()),
                       0U, content.size(), "utf-8"),
             false});
    };
    for (const auto& message : history) {
        switch (message.role) {
            case ChatRole::system:
                segments.push_back({plan.system_prefix, true});
                push_variable(message.content);
                segments.push_back({plan.system_suffix, true});
                break;
            case ChatRole::user:
                segments.push_back({plan.user_prefix, true});
                push_variable(message.content);
                segments.push_back({plan.user_suffix, true});
                break;
            case ChatRole::assistant:
                segments.push_back({plan.assistant_prefix, true});
                push_variable(message.content);
                segments.push_back({plan.assistant_suffix, true});
                break;
        }
    }
    segments.push_back({plan.user_prefix, true});
    push_variable(latest_user_content);
    segments.push_back({plan.user_suffix, true});
    segments.push_back({plan.generation_prompt, true});
    return segments;
}

std::string materialize_prompt(const std::vector<PromptSegment>& segments) {
    std::size_t total = 0U;
    for (const auto& segment : segments) total += segment.view.size();
    std::string result;
    result.reserve(total);
    for (const auto& segment : segments) {
        const auto* data = segment.view.data();
        if (data == nullptr || segment.view.size() == 0U) continue;
        result.append(reinterpret_cast<const char*>(data), segment.view.size());
    }
    return result;
}

namespace {

// Backing storage for intern_identifier(). std::set's iterators/elements
// never move once inserted (node-based container), so a reference into it
// stays valid across further insertions -- exactly what a "stable
// reference for the rest of the process" contract needs.
std::mutex g_intern_mutex;
std::set<std::string> g_intern_pool;

}  // namespace

const std::string& intern_identifier(const std::string& text) {
    if (text.size() > kMaxInternedLength) {
        // Structural enforcement (not just documentation) of "role names /
        // route names / repeated JSON keys ONLY" -- arbitrary user content
        // or file text is essentially guaranteed to exceed this length
        // sooner or later, so callers attempting to intern it get a hard
        // failure instead of silently growing an unbounded process-
        // lifetime cache of arbitrary strings.
        throw std::invalid_argument(
            "intern_identifier: text exceeds the interning length limit; "
            "this table is for short, high-repetition identifiers only");
    }
    std::lock_guard<std::mutex> lock(g_intern_mutex);
    return *g_intern_pool.insert(text).first;
}

}  // namespace masterai
