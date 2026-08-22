// Phase 34 (2026-08-13): adaptive performance controller. See the class
// comment on AdaptiveController in masterai.hpp for this file's honest
// scope -- which named knobs are actually applied live via
// MemoryBudgetManager::set_policy() versus only ever computed and
// disclosed as a recommendation.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <cmath>

namespace masterai {

std::string to_string(const PerformanceMode mode) {
    switch (mode) {
        case PerformanceMode::minimal_memory: return "minimal_memory";
        case PerformanceMode::balanced: return "balanced";
        case PerformanceMode::lowest_latency: return "lowest_latency";
        case PerformanceMode::maximum_throughput: return "maximum_throughput";
        case PerformanceMode::battery_saver: return "battery_saver";
        case PerformanceMode::quiet_thermal_conservative:
            return "quiet_thermal_conservative";
        case PerformanceMode::administrator_custom: return "administrator_custom";
        case PerformanceMode::automatic: return "automatic";
    }
    return "automatic";
}

namespace {

// Bounds `step` to at most `max_step_percent` of `current` (never zero when
// current is nonzero and a change is due at all) -- the plan's "bounded
// step size" stability control.
std::uint32_t bounded_step(const std::uint32_t current,
                           const unsigned int max_step_percent) {
    const auto step = static_cast<std::uint32_t>(
        std::max<std::uint64_t>(
            1ULL, static_cast<std::uint64_t>(current) * max_step_percent / 100ULL));
    return step;
}

// Picks the best-fit named mode from live signals when the administrator
// has left mode selection on `automatic`. Ordered by severity: memory
// pressure and thermal/battery constraints take priority over throughput-
// seeking, since backing off is always the safe default direction.
PerformanceMode select_automatic_mode(const AdaptiveSignalSnapshot& signals) {
    if (signals.memory.pressure == MemoryPressure::critical) {
        return PerformanceMode::minimal_memory;
    }
    if (signals.on_battery_power) {
        return PerformanceMode::battery_saver;
    }
    if (signals.thermal_headroom_percent.has_value() &&
        *signals.thermal_headroom_percent < 15.0) {
        return PerformanceMode::quiet_thermal_conservative;
    }
    if (signals.memory.pressure == MemoryPressure::high) {
        return PerformanceMode::minimal_memory;
    }
    std::uint32_t queued_total = 0U;
    for (const auto& [klass, status] : signals.scheduler) {
        (void)klass;
        queued_total += static_cast<std::uint32_t>(status.queued);
    }
    if (signals.memory.pressure == MemoryPressure::normal && queued_total >= 4U) {
        return PerformanceMode::maximum_throughput;
    }
    if (signals.average_ttft_ms.has_value() && *signals.average_ttft_ms > 2000.0) {
        return PerformanceMode::lowest_latency;
    }
    return PerformanceMode::balanced;
}

// Mode bias applied on top of the pressure-driven direction below: how many
// extra/fewer percentage points of headroom this mode wants before growing
// or shrinking concurrency, relative to `balanced`'s neutral 0.
int mode_bias_percent(const PerformanceMode mode) {
    switch (mode) {
        case PerformanceMode::minimal_memory: return -30;
        case PerformanceMode::battery_saver: return -20;
        case PerformanceMode::quiet_thermal_conservative: return -20;
        case PerformanceMode::maximum_throughput: return 20;
        case PerformanceMode::lowest_latency: return 10;
        default: return 0;
    }
}

}  // namespace

AdaptiveController::AdaptiveController(PerformanceCeilings ceilings)
    : ceilings_(ceilings) {}

void AdaptiveController::set_mode(const PerformanceMode mode) {
    std::lock_guard<std::mutex> lock(mutex_);
    mode_ = mode;
}

PerformanceMode AdaptiveController::mode() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return mode_;
}

void AdaptiveController::set_ceilings(PerformanceCeilings ceilings) {
    std::lock_guard<std::mutex> lock(mutex_);
    ceilings_ = ceilings;
}

PerformanceCeilings AdaptiveController::ceilings() const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return ceilings_;
}

AdaptiveLaunchRecommendation AdaptiveController::launch_recommendation()
    const noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    return launch_recommendation_;
}

AdaptiveControllerReport AdaptiveController::evaluate(
    const AdaptiveSignalSnapshot& signals, MemoryBudgetManager& memory,
    const std::uint64_t now_epoch_seconds, CacheManager* cache) {
    std::lock_guard<std::mutex> lock(mutex_);
    AdaptiveControllerReport report;
    report.active_mode = mode_;
    report.evaluation_epoch_seconds = now_epoch_seconds;
    report.ceilings = ceilings_;

    // Rolling measurement window (the plan's "rolling measurement"
    // stability control): the last 5 evaluate() calls' pressure reading.
    pressure_history_.push_back(signals.memory.pressure);
    while (pressure_history_.size() > 5U) pressure_history_.pop_front();

    report.selected_mode = mode_ == PerformanceMode::automatic
                               ? select_automatic_mode(signals)
                               : mode_;

    if (mode_ == PerformanceMode::administrator_custom) {
        // The plan's exact deliverable: administrator_custom applies the
        // configured ceilings with no automatic tuning at all -- evaluate()
        // still reports the mode but proposes nothing.
        return report;
    }

    // Interval bookkeeping (the plan's "max changes per interval" control):
    // a fresh interval starts changes_this_interval_ over from zero.
    if (now_epoch_seconds >=
        current_interval_epoch_seconds_ + ceilings_.interval_seconds) {
        current_interval_epoch_seconds_ = now_epoch_seconds;
        changes_this_interval_ = 0U;
    }

    const bool dwell_elapsed =
        now_epoch_seconds >=
        last_change_epoch_seconds_ + ceilings_.minimum_dwell_seconds;
    const bool interval_has_room =
        changes_this_interval_ < ceilings_.max_changes_per_interval;

    // Confidence (the plan's "confidence requirement" stability control):
    // the fraction of the rolling window whose pressure reading is at or
    // above the current one -- a single noisy sample can never dominate.
    const auto elevated_count = static_cast<double>(std::count_if(
        pressure_history_.begin(), pressure_history_.end(),
        [&](const MemoryPressure sample) { return sample >= signals.memory.pressure; }));
    const double confidence = pressure_history_.empty()
                                  ? 0.0
                                  : elevated_count /
                                        static_cast<double>(pressure_history_.size());

    const auto current_policy = memory.policy();
    auto proposed_policy = current_policy;
    bool any_memory_field_changed = false;
    const auto bias = mode_bias_percent(report.selected_mode);

    auto propose_and_maybe_apply =
        [&](const std::string& parameter, const std::string& reason,
            const double previous, const double proposed_value,
            bool* changed_flag, auto assign_into_policy) {
            AdaptiveAdjustment adjustment;
            adjustment.parameter = parameter;
            adjustment.reason = reason;
            adjustment.previous_value = previous;
            adjustment.proposed_value = proposed_value;
            adjustment.confidence = confidence;
            const bool value_changed =
                std::llround(previous) != std::llround(proposed_value);
            const bool eligible = value_changed && dwell_elapsed &&
                                  interval_has_room && confidence >= 0.6;
            if (eligible) {
                assign_into_policy(proposed_policy);
                *changed_flag = true;
                report.applied.push_back(adjustment);
            } else if (value_changed) {
                report.proposed_not_yet_applied.push_back(adjustment);
            }
        };

    // Inference concurrency: back off under pressure, grow only when
    // normal and the mode/queue backlog signal wants throughput -- either
    // direction is bounded to at most max_step_percent of the current
    // value and clamped to [1, ceilings_.max_inference_concurrency].
    {
        std::uint32_t queued_total = 0U;
        for (const auto& [klass, status] : signals.scheduler) {
            (void)klass;
            queued_total += static_cast<std::uint32_t>(status.queued);
        }
        std::int64_t direction = 0;
        if (signals.memory.pressure >= MemoryPressure::high || bias < 0) {
            direction = -1;
        } else if (signals.memory.pressure == MemoryPressure::normal &&
                  (queued_total >= 4U || bias > 0)) {
            direction = 1;
        }
        if (direction != 0) {
            const auto step = bounded_step(current_policy.maximum_active_inference,
                                           ceilings_.max_step_percent);
            const std::int64_t raw =
                static_cast<std::int64_t>(current_policy.maximum_active_inference) +
                direction * static_cast<std::int64_t>(step);
            const auto clamped = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
                raw, 1, static_cast<std::int64_t>(ceilings_.max_inference_concurrency)));
            propose_and_maybe_apply(
                "maximum_active_inference",
                direction < 0
                    ? "memory pressure elevated or mode favors lower footprint"
                    : "normal pressure with queued backlog or mode favors throughput",
                static_cast<double>(current_policy.maximum_active_inference),
                static_cast<double>(clamped), &any_memory_field_changed,
                [clamped](MemoryPolicy& policy) {
                    policy.maximum_active_inference = clamped;
                });
        }
    }

    // Index worker count: same direction logic as inference concurrency
    // (background indexing is exactly the kind of work Phase 14's own
    // pressure model already pauses first), bounded to
    // ceilings_.max_index_workers.
    if (signals.memory.pressure >= MemoryPressure::high) {
        const auto step = bounded_step(current_policy.maximum_index_workers,
                                       ceilings_.max_step_percent);
        const auto proposed = static_cast<std::uint32_t>(std::max<std::int64_t>(
            1, static_cast<std::int64_t>(current_policy.maximum_index_workers) -
                   static_cast<std::int64_t>(step)));
        propose_and_maybe_apply(
            "maximum_index_workers", "memory pressure elevated",
            static_cast<double>(current_policy.maximum_index_workers),
            static_cast<double>(proposed), &any_memory_field_changed,
            [proposed](MemoryPolicy& policy) {
                policy.maximum_index_workers = proposed;
            });
    } else if (signals.memory.pressure == MemoryPressure::normal &&
              current_policy.maximum_index_workers < ceilings_.max_index_workers) {
        const auto step = bounded_step(current_policy.maximum_index_workers,
                                       ceilings_.max_step_percent);
        const auto proposed = std::min(
            ceilings_.max_index_workers,
            current_policy.maximum_index_workers + step);
        propose_and_maybe_apply(
            "maximum_index_workers", "normal pressure with ceiling headroom",
            static_cast<double>(current_policy.maximum_index_workers),
            static_cast<double>(proposed), &any_memory_field_changed,
            [proposed](MemoryPolicy& policy) {
                policy.maximum_index_workers = proposed;
            });
    }

    // Default context tokens: reduced only under sustained critical
    // pressure (this is the most disruptive knob -- shrinking context
    // affects answer quality -- so it is gated on the strictest signal),
    // never grown automatically past what the administrator configured as
    // the ceiling in the first place.
    if (signals.memory.pressure == MemoryPressure::critical) {
        const auto step = bounded_step(current_policy.default_context_tokens,
                                       ceilings_.max_step_percent);
        const auto proposed = static_cast<std::uint32_t>(std::max<std::int64_t>(
            512, static_cast<std::int64_t>(current_policy.default_context_tokens) -
                     static_cast<std::int64_t>(step)));
        propose_and_maybe_apply(
            "default_context_tokens", "sustained critical memory pressure",
            static_cast<double>(current_policy.default_context_tokens),
            static_cast<double>(proposed), &any_memory_field_changed,
            [proposed](MemoryPolicy& policy) {
                policy.default_context_tokens = proposed;
            });
    }

    // idle_unload_seconds (full completion pass): a normal 4th
    // MemoryBudgetManager-applied knob now that MemoryPolicy carries this
    // field for real (see its own class comment) -- same two-direction,
    // bounded-step shape as maximum_index_workers above: shrink toward the
    // administrator's configured floor under pressure, grow back toward the
    // configured ceiling once pressure is normal again with headroom.
    if (signals.memory.pressure >= MemoryPressure::high) {
        const auto step = bounded_step(current_policy.idle_unload_seconds,
                                       ceilings_.max_step_percent);
        const auto proposed = static_cast<std::uint32_t>(std::max<std::int64_t>(
            static_cast<std::int64_t>(ceilings_.min_idle_unload_seconds),
            static_cast<std::int64_t>(current_policy.idle_unload_seconds) -
                static_cast<std::int64_t>(step)));
        propose_and_maybe_apply(
            "idle_unload_seconds", "memory pressure elevated -- unload idle models sooner",
            static_cast<double>(current_policy.idle_unload_seconds),
            static_cast<double>(proposed), &any_memory_field_changed,
            [proposed](MemoryPolicy& policy) {
                policy.idle_unload_seconds = proposed;
            });
    } else if (signals.memory.pressure == MemoryPressure::normal &&
              current_policy.idle_unload_seconds <
                  ceilings_.max_idle_unload_seconds) {
        const auto step = bounded_step(current_policy.idle_unload_seconds,
                                       ceilings_.max_step_percent);
        const auto proposed = std::min(
            ceilings_.max_idle_unload_seconds,
            current_policy.idle_unload_seconds + step);
        propose_and_maybe_apply(
            "idle_unload_seconds", "normal pressure with ceiling headroom",
            static_cast<double>(current_policy.idle_unload_seconds),
            static_cast<double>(proposed), &any_memory_field_changed,
            [proposed](MemoryPolicy& policy) {
                policy.idle_unload_seconds = proposed;
            });
    }

    if (any_memory_field_changed) {
        last_applied_ = AppliedChange{now_epoch_seconds, current_policy};
        memory.set_policy(proposed_policy);
        last_change_epoch_seconds_ = now_epoch_seconds;
        changes_this_interval_ += static_cast<std::uint32_t>(report.applied.size());
    }

    // Cache quota (full completion pass): same shape as idle_unload_seconds
    // just above, applied to CacheManager::set_policy() when a caller
    // supplies one -- absent `cache` leaves this knob untouched exactly as
    // every pre-existing caller that doesn't pass one experiences today.
    if (cache != nullptr) {
        const auto current_cache_policy = cache->policy();
        constexpr std::uint64_t kMinimumCacheBytesPerCategory =
            8ULL * 1024ULL * 1024ULL;
        bool cache_changed = false;
        auto proposed_cache_policy = current_cache_policy;
        if (signals.memory.pressure >= MemoryPressure::high) {
            const auto step = std::max<std::uint64_t>(
                1ULL, current_cache_policy.maximum_bytes_per_category *
                          ceilings_.max_step_percent / 100ULL);
            const auto proposed = std::max(
                kMinimumCacheBytesPerCategory,
                current_cache_policy.maximum_bytes_per_category - step);
            AdaptiveAdjustment adjustment;
            adjustment.parameter = "cache_maximum_bytes_per_category";
            adjustment.reason = "memory pressure elevated -- shrink cache quota";
            adjustment.previous_value = static_cast<double>(
                current_cache_policy.maximum_bytes_per_category);
            adjustment.proposed_value = static_cast<double>(proposed);
            adjustment.confidence = confidence;
            const bool value_changed =
                proposed != current_cache_policy.maximum_bytes_per_category;
            if (value_changed && dwell_elapsed && interval_has_room &&
                confidence >= 0.6) {
                proposed_cache_policy.maximum_bytes_per_category = proposed;
                cache_changed = true;
                report.applied.push_back(adjustment);
            } else if (value_changed) {
                report.proposed_not_yet_applied.push_back(adjustment);
            }
        }
        if (cache_changed) {
            cache->set_policy(proposed_cache_policy);
            last_change_epoch_seconds_ = now_epoch_seconds;
            changes_this_interval_ += 1U;
        }
    }

    // Thread count / GPU offload / NUMA placement (full completion pass):
    // real bounded-reduction targets computed from whatever real signals a
    // caller supplied (host_thread_count/calibrated_gpu_layers) -- these are
    // launch-time-only arguments to a separate llama.cpp process (see
    // AdaptiveLaunchRecommendation's class comment), so "applied" here means
    // stored for ensure_model_loaded() to consume at the next natural load,
    // never a forced live reload of an already-running model. Silent (no
    // recommendation emitted) when a caller has not supplied the relevant
    // signal -- there is nothing real to compute a target from, and this
    // pass would rather disclose nothing than a fabricated placeholder.
    const bool reduce_pressure_knobs =
        signals.on_battery_power || signals.memory.pressure >= MemoryPressure::high;
    AdaptiveLaunchRecommendation proposed_launch = launch_recommendation_;
    bool launch_changed = false;
    if (reduce_pressure_knobs && signals.host_thread_count.has_value()) {
        const auto host_threads = *signals.host_thread_count;
        const auto step = bounded_step(host_threads, ceilings_.max_step_percent);
        const auto proposed = static_cast<unsigned int>(std::max<std::int64_t>(
            1, static_cast<std::int64_t>(host_threads) -
                   static_cast<std::int64_t>(step)));
        AdaptiveAdjustment adjustment;
        adjustment.parameter = "thread_count";
        adjustment.reason = signals.on_battery_power
            ? "on battery power -- reduce worker thread count for power draw"
            : "memory pressure elevated -- reduce worker thread count";
        adjustment.previous_value = static_cast<double>(host_threads);
        adjustment.proposed_value = static_cast<double>(proposed);
        adjustment.confidence = confidence;
        if (dwell_elapsed && interval_has_room && confidence >= 0.6) {
            proposed_launch.thread_count = proposed;
            launch_changed = true;
            report.applied.push_back(adjustment);
        } else {
            report.proposed_not_yet_applied.push_back(adjustment);
        }
    }
    if (reduce_pressure_knobs && signals.calibrated_gpu_layers.has_value()) {
        const auto calibrated_layers = *signals.calibrated_gpu_layers;
        const auto step =
            bounded_step(calibrated_layers, ceilings_.max_step_percent);
        const auto proposed = static_cast<unsigned int>(std::max<std::int64_t>(
            0, static_cast<std::int64_t>(calibrated_layers) -
                   static_cast<std::int64_t>(step)));
        AdaptiveAdjustment adjustment;
        adjustment.parameter = "gpu_offload";
        adjustment.reason = signals.on_battery_power
            ? "on battery power -- reduce GPU layer offload"
            : "memory pressure elevated -- reduce GPU layer offload";
        adjustment.previous_value = static_cast<double>(calibrated_layers);
        adjustment.proposed_value = static_cast<double>(proposed);
        adjustment.confidence = confidence;
        if (dwell_elapsed && interval_has_room && confidence >= 0.6) {
            proposed_launch.gpu_offload_layers = proposed;
            launch_changed = true;
            report.applied.push_back(adjustment);
        } else {
            report.proposed_not_yet_applied.push_back(adjustment);
        }
    }
    if (reduce_pressure_knobs && signals.calibrated_batch_tokens.has_value()) {
        const auto calibrated_batch = *signals.calibrated_batch_tokens;
        const auto step = bounded_step(calibrated_batch, ceilings_.max_step_percent);
        const auto proposed = static_cast<unsigned int>(std::max<std::int64_t>(
            32, static_cast<std::int64_t>(calibrated_batch) -
                    static_cast<std::int64_t>(step)));
        AdaptiveAdjustment adjustment;
        adjustment.parameter = "batch_tokens";
        adjustment.reason = signals.on_battery_power
            ? "on battery power -- reduce prompt-processing batch size"
            : "memory pressure elevated -- reduce prompt-processing batch size";
        adjustment.previous_value = static_cast<double>(calibrated_batch);
        adjustment.proposed_value = static_cast<double>(proposed);
        adjustment.confidence = confidence;
        if (dwell_elapsed && interval_has_room && confidence >= 0.6) {
            proposed_launch.batch_tokens = proposed;
            launch_changed = true;
            report.applied.push_back(adjustment);
        } else {
            report.proposed_not_yet_applied.push_back(adjustment);
        }
    }
    // NUMA placement: battery power is the one live signal this pass has
    // that plausibly justifies giving up cross-node pinning (the complexity
    // isn't worth it when the host is trying to conserve power) -- normal
    // conditions never turn it back on by themselves, since that is an
    // administrator's own topology.numaLocalPlacementEnabled setting to make.
    if (signals.on_battery_power &&
        launch_recommendation_.numa_local_placement != std::optional<bool>(false)) {
        AdaptiveAdjustment adjustment;
        adjustment.parameter = "numa_local_placement";
        adjustment.reason = "on battery power -- cross-node pinning is not "
                            "worth the complexity while conserving power";
        adjustment.previous_value = 1.0;
        adjustment.proposed_value = 0.0;
        adjustment.confidence = confidence;
        if (dwell_elapsed && interval_has_room && confidence >= 0.6) {
            proposed_launch.numa_local_placement = false;
            launch_changed = true;
            report.applied.push_back(adjustment);
        } else {
            report.proposed_not_yet_applied.push_back(adjustment);
        }
    }
    if (launch_changed) {
        launch_recommendation_ = proposed_launch;
        last_change_epoch_seconds_ = now_epoch_seconds;
        changes_this_interval_ += 1U;
    }

    return report;
}

void AdaptiveController::rollback(MemoryBudgetManager& memory) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!last_applied_.has_value()) return;
    memory.set_policy(last_applied_->policy_before);
    last_applied_.reset();
}

std::string AdaptiveController::to_json(const AdaptiveControllerReport& report) {
    const auto adjustments_json =
        [](const std::vector<AdaptiveAdjustment>& adjustments) {
            std::string body = "[";
            bool first = true;
            for (const auto& adjustment : adjustments) {
                if (!first) body += ",";
                first = false;
                body += "{\"parameter\":" + json_string(adjustment.parameter) +
                       ",\"reason\":" + json_string(adjustment.reason) +
                       ",\"previousValue\":" +
                       std::to_string(adjustment.previous_value) +
                       ",\"proposedValue\":" +
                       std::to_string(adjustment.proposed_value) +
                       ",\"confidence\":" + std::to_string(adjustment.confidence) +
                       "}";
            }
            return body + "]";
        };
    const auto& c = report.ceilings;
    return "{\"activeMode\":" + json_string(to_string(report.active_mode)) +
          ",\"selectedMode\":" + json_string(to_string(report.selected_mode)) +
          ",\"applied\":" + adjustments_json(report.applied) +
          ",\"proposedNotYetApplied\":" +
          adjustments_json(report.proposed_not_yet_applied) +
          ",\"evaluationEpochSeconds\":" +
          std::to_string(report.evaluation_epoch_seconds) +
          ",\"ceilings\":{"
          "\"maxInferenceConcurrency\":" +
          std::to_string(c.max_inference_concurrency) +
          ",\"maxQueuedInference\":" + std::to_string(c.max_queued_inference) +
          ",\"maxIndexWorkers\":" + std::to_string(c.max_index_workers) +
          ",\"maxContextTokens\":" + std::to_string(c.max_context_tokens) +
          ",\"minIdleUnloadSeconds\":" +
          std::to_string(c.min_idle_unload_seconds) +
          ",\"maxIdleUnloadSeconds\":" +
          std::to_string(c.max_idle_unload_seconds) +
          ",\"maxStepPercent\":" + std::to_string(c.max_step_percent) +
          ",\"maxChangesPerInterval\":" +
          std::to_string(c.max_changes_per_interval) +
          ",\"intervalSeconds\":" + std::to_string(c.interval_seconds) +
          ",\"minimumDwellSeconds\":" +
          std::to_string(c.minimum_dwell_seconds) + "}}";
}

}  // namespace masterai
