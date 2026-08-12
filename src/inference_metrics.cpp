// Phase 78: real per-request inference telemetry -- see InferenceMetricsStore's
// class comment in masterai.hpp for which Monitoring and Diagnostics gap this
// closes and which call sites feed it.
#include "masterai.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace masterai {
namespace {

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Nearest-rank percentile over already-sorted `values`. Empty input returns
// 0 rather than dividing by zero -- an idle server has no latency to report,
// which is a genuine (not fabricated) state.
double percentile(const std::vector<std::uint64_t>& sorted_values,
                  const double fraction) {
    if (sorted_values.empty()) return 0.0;
    const auto index = static_cast<std::size_t>(
        std::ceil(fraction * static_cast<double>(sorted_values.size())) - 1.0);
    const auto bounded = std::min(index, sorted_values.size() - 1U);
    return static_cast<double>(sorted_values[bounded]) / 1000.0;  // us -> ms
}

}  // namespace

void InferenceMetricsStore::begin_request() {
    std::lock_guard<std::mutex> lock(mutex_);
    ++queue_depth_;
}

void InferenceMetricsStore::end_request(const std::uint64_t latency_microseconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (queue_depth_ > 0) --queue_depth_;
    samples_.emplace_back(epoch_seconds(), latency_microseconds);
    // Prune anything older than 15 minutes so a long-running server's
    // sample window stays bounded without needing a separate sweep thread
    // -- every begin_request()/end_request() call already takes this lock.
    const auto cutoff = epoch_seconds() > 900U ? epoch_seconds() - 900U : 0U;
    while (!samples_.empty() && samples_.front().first < cutoff) {
        samples_.pop_front();
    }
}

InferenceMetricsStore::Snapshot InferenceMetricsStore::snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_);
    Snapshot result;
    result.current_queue_depth = queue_depth_;
    const auto now = epoch_seconds();
    const auto minute_cutoff = now > 60U ? now - 60U : 0U;
    std::vector<std::uint64_t> recent_latencies;
    recent_latencies.reserve(samples_.size());
    for (const auto& sample : samples_) {
        if (sample.first >= minute_cutoff) {
            ++result.requests_last_minute;
            recent_latencies.push_back(sample.second);
        }
    }
    std::sort(recent_latencies.begin(), recent_latencies.end());
    result.p50_latency_ms = percentile(recent_latencies, 0.50);
    result.p95_latency_ms = percentile(recent_latencies, 0.95);
    result.p99_latency_ms = percentile(recent_latencies, 0.99);
    return result;
}

std::string inference_metrics_json(const InferenceMetricsStore::Snapshot& snapshot) {
    char p50[32];
    char p95[32];
    char p99[32];
    std::snprintf(p50, sizeof(p50), "%.3f", snapshot.p50_latency_ms);
    std::snprintf(p95, sizeof(p95), "%.3f", snapshot.p95_latency_ms);
    std::snprintf(p99, sizeof(p99), "%.3f", snapshot.p99_latency_ms);
    return "{\"currentQueueDepth\":" +
           std::to_string(snapshot.current_queue_depth) +
           ",\"requestsLastMinute\":" +
           std::to_string(snapshot.requests_last_minute) +
           ",\"p50LatencyMs\":" + p50 + ",\"p95LatencyMs\":" + p95 +
           ",\"p99LatencyMs\":" + p99 + "}";
}

}  // namespace masterai
