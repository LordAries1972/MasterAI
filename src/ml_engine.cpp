// Phase 56: the Machine Learning module's first real execution layer --
// see the block comment above TabularDataset in masterai.hpp. This file
// contains actual computation: CSV parsing into numeric matrices,
// full-batch gradient-descent training (linear regression for numeric
// targets, softmax/logistic classification for categorical targets),
// held-out evaluation with genuine metrics, weight-artifact persistence,
// and live prediction. Nothing in here fabricates a number: every metric
// is computed from real data and every loss value comes from a real
// optimization step.
#include "masterai.hpp"
#include "json.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <numeric>
#include <random>
#include <set>
#include <stdexcept>

namespace masterai {
namespace {

std::uint64_t epoch_seconds() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::now().time_since_epoch())
            .count());
}

// Same length-prefixed field packing ml.cpp's stores use, so artifacts can
// hold arbitrary text (feature names from CSV headers) without escaping.
std::string pack(const std::vector<std::string>& fields) {
    std::string result;
    for (const auto& field : fields) {
        result += std::to_string(field.size()) + ":" + field;
    }
    return result;
}

std::vector<std::string> unpack(const std::string& value) {
    std::vector<std::string> fields;
    std::size_t position = 0U;
    while (position < value.size()) {
        const auto colon = value.find(':', position);
        if (colon == std::string::npos || colon == position) {
            throw std::runtime_error("persisted ML artifact record is malformed");
        }
        const auto size = std::stoull(value.substr(position, colon - position));
        position = colon + 1U;
        if (size > value.size() - position) {
            throw std::runtime_error("persisted ML artifact record is truncated");
        }
        fields.push_back(value.substr(position, static_cast<std::size_t>(size)));
        position += static_cast<std::size_t>(size);
    }
    return fields;
}

std::string json_escape(const std::string& value) {
    std::string result;
    result.reserve(value.size() + 16U);
    for (const unsigned char character : value) {
        switch (character) {
            case '"': result += "\\\""; break;
            case '\\': result += "\\\\"; break;
            case '\b': result += "\\b"; break;
            case '\f': result += "\\f"; break;
            case '\n': result += "\\n"; break;
            case '\r': result += "\\r"; break;
            case '\t': result += "\\t"; break;
            default:
                if (character < 0x20U) {
                    static constexpr char hex_digits[] = "0123456789abcdef";
                    result += "\\u00";
                    result += hex_digits[(character >> 4U) & 0x0fU];
                    result += hex_digits[character & 0x0fU];
                } else {
                    result += static_cast<char>(character);
                }
        }
    }
    return result;
}

// JSON has no NaN/Infinity; a degenerate metric (e.g. R-squared on a
// constant target) serializes as 0 rather than emitting invalid JSON.
std::string json_number(const double value) {
    if (!std::isfinite(value)) return "0";
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    return buffer;
}

// Full-precision round-trip form for persisted weights, where %.9g would
// silently degrade the model between server restarts.
std::string number_field(const double value) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.17g", std::isfinite(value) ? value : 0.0);
    return buffer;
}

double parse_number_field(const std::string& text) {
    try {
        return std::stod(text);
    } catch (const std::exception&) {
        throw std::runtime_error("persisted ML artifact number is malformed");
    }
}

// One CSV line -> fields, honoring double-quoted fields with "" escapes.
// Deliberately line-scoped: multi-line quoted fields are rejected upstream
// because a tabular training cell is always a single number or label.
std::vector<std::string> split_csv_line(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1U < line.size() && line[index + 1U] == '"') {
                    field += '"';
                    ++index;
                } else {
                    quoted = false;
                }
            } else {
                field += character;
            }
        } else if (character == '"' && field.empty()) {
            quoted = true;
        } else if (character == ',') {
            fields.push_back(field);
            field.clear();
        } else {
            field += character;
        }
    }
    fields.push_back(field);
    return fields;
}

std::string trim(const std::string& value) {
    std::size_t begin = 0U;
    std::size_t end = value.size();
    while (begin < end && (value[begin] == ' ' || value[begin] == '\t' ||
                           value[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (value[end - 1U] == ' ' || value[end - 1U] == '\t' ||
                           value[end - 1U] == '\r')) {
        --end;
    }
    return value.substr(begin, end - begin);
}

bool parse_double(const std::string& text, double& out) {
    if (text.empty()) return false;
    char* end = nullptr;
    out = std::strtod(text.c_str(), &end);
    return end == text.c_str() + text.size() && std::isfinite(out);
}

// Softmax over one logit row, numerically stabilized by max subtraction.
std::vector<double> softmax(const std::vector<double>& logits) {
    const double peak = *std::max_element(logits.begin(), logits.end());
    std::vector<double> probabilities(logits.size());
    double total = 0.0;
    for (std::size_t index = 0; index < logits.size(); ++index) {
        probabilities[index] = std::exp(logits[index] - peak);
        total += probabilities[index];
    }
    for (auto& probability : probabilities) probability /= total;
    return probabilities;
}

// Standardized-feature linear score for one weight row: bias is the last
// weight, mirroring the layout train_tabular_model writes.
double score_row(const std::vector<double>& weights,
                 const std::vector<double>& standardized) {
    double result = weights.back();
    for (std::size_t index = 0; index < standardized.size(); ++index) {
        result += weights[index] * standardized[index];
    }
    return result;
}

std::vector<double> standardize(const TrainedTabularModel& model,
                                const std::vector<double>& features) {
    std::vector<double> standardized(features.size());
    for (std::size_t index = 0; index < features.size(); ++index) {
        standardized[index] =
            (features[index] - model.feature_means[index]) /
            model.feature_stddevs[index];
    }
    return standardized;
}

// Shared by training and standalone evaluation so both report identical
// metric definitions. `standardized` holds pre-standardized feature rows.
TabularEvaluationMetrics compute_metrics(
    const TrainedTabularModel& model,
    const std::vector<std::vector<double>>& standardized,
    const std::vector<double>& targets) {
    TabularEvaluationMetrics metrics;
    metrics.classification = model.classification;
    metrics.evaluated_rows = standardized.size();
    if (standardized.empty()) return metrics;
    if (model.classification) {
        const std::size_t class_count = model.class_labels.size();
        metrics.confusion.assign(class_count,
                                 std::vector<std::size_t>(class_count, 0U));
        std::size_t correct = 0U;
        for (std::size_t row = 0; row < standardized.size(); ++row) {
            std::vector<double> logits(class_count);
            for (std::size_t klass = 0; klass < class_count; ++klass) {
                logits[klass] = score_row(model.weights[klass], standardized[row]);
            }
            const auto predicted = static_cast<std::size_t>(
                std::max_element(logits.begin(), logits.end()) - logits.begin());
            const auto actual = static_cast<std::size_t>(targets[row]);
            metrics.confusion[actual][predicted] += 1U;
            if (predicted == actual) ++correct;
        }
        metrics.accuracy =
            static_cast<double>(correct) / static_cast<double>(standardized.size());
        // Macro averages treat every class equally regardless of support;
        // a class absent from this split contributes zero to its average,
        // the standard "0 when undefined" convention.
        double precision_sum = 0.0;
        double recall_sum = 0.0;
        double f1_sum = 0.0;
        for (std::size_t klass = 0; klass < class_count; ++klass) {
            std::size_t true_positive = metrics.confusion[klass][klass];
            std::size_t predicted_total = 0U;
            std::size_t actual_total = 0U;
            for (std::size_t other = 0; other < class_count; ++other) {
                predicted_total += metrics.confusion[other][klass];
                actual_total += metrics.confusion[klass][other];
            }
            const double precision =
                predicted_total > 0U
                    ? static_cast<double>(true_positive) / predicted_total : 0.0;
            const double recall =
                actual_total > 0U
                    ? static_cast<double>(true_positive) / actual_total : 0.0;
            precision_sum += precision;
            recall_sum += recall;
            f1_sum += (precision + recall) > 0.0
                          ? 2.0 * precision * recall / (precision + recall) : 0.0;
        }
        metrics.macro_precision = precision_sum / static_cast<double>(class_count);
        metrics.macro_recall = recall_sum / static_cast<double>(class_count);
        metrics.macro_f1 = f1_sum / static_cast<double>(class_count);
    } else {
        double squared_error = 0.0;
        double absolute_error = 0.0;
        double target_sum = 0.0;
        for (std::size_t row = 0; row < standardized.size(); ++row) {
            const double predicted = score_row(model.weights[0], standardized[row]);
            const double error = predicted - targets[row];
            squared_error += error * error;
            absolute_error += std::abs(error);
            target_sum += targets[row];
        }
        const auto count = static_cast<double>(standardized.size());
        metrics.mse = squared_error / count;
        metrics.mae = absolute_error / count;
        const double mean = target_sum / count;
        double total_variance = 0.0;
        for (std::size_t row = 0; row < standardized.size(); ++row) {
            total_variance += (targets[row] - mean) * (targets[row] - mean);
        }
        metrics.r_squared =
            total_variance > 0.0 ? 1.0 - squared_error / total_variance : 0.0;
    }
    return metrics;
}

std::string confusion_json(const std::vector<std::vector<std::size_t>>& confusion) {
    std::string result = "[";
    for (std::size_t row = 0; row < confusion.size(); ++row) {
        if (row > 0U) result += ",";
        result += "[";
        for (std::size_t column = 0; column < confusion[row].size(); ++column) {
            if (column > 0U) result += ",";
            result += std::to_string(confusion[row][column]);
        }
        result += "]";
    }
    return result + "]";
}

std::string string_array_json(const std::vector<std::string>& values) {
    std::string result = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0U) result += ",";
        result += "\"" + json_escape(values[index]) + "\"";
    }
    return result + "]";
}

std::string number_array_json(const std::vector<double>& values) {
    std::string result = "[";
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (index > 0U) result += ",";
        result += json_number(values[index]);
    }
    return result + "]";
}

}  // namespace

TabularDataset parse_tabular_csv(const std::string& csv,
                                 const std::string& target_column,
                                 const std::uint64_t maximum_csv_bytes) {
    if (csv.size() > maximum_csv_bytes) {
        throw std::runtime_error(
            "dataset content exceeds the configured " +
            std::to_string(maximum_csv_bytes / (1024ULL * 1024ULL)) +
            " MiB limit");
    }
    // Split into non-empty lines; \r is trimmed per cell so both LF and
    // CRLF files parse identically.
    std::vector<std::string> lines;
    std::size_t position = 0U;
    while (position <= csv.size()) {
        const auto newline = csv.find('\n', position);
        const auto end = newline == std::string::npos ? csv.size() : newline;
        std::string line = csv.substr(position, end - position);
        if (!trim(line).empty()) lines.push_back(std::move(line));
        if (newline == std::string::npos) break;
        position = newline + 1U;
    }
    if (lines.size() < 3U) {
        throw std::runtime_error(
            "dataset needs a header row and at least two data rows");
    }
    auto header = split_csv_line(lines[0]);
    for (auto& name : header) name = trim(name);
    if (header.size() < 2U) {
        throw std::runtime_error(
            "dataset needs at least one feature column and one target column");
    }
    // Resolve the target column: by name when given, last column otherwise.
    std::size_t target_index = header.size() - 1U;
    if (!target_column.empty()) {
        const auto found = std::find(header.begin(), header.end(), target_column);
        if (found == header.end()) {
            throw std::runtime_error("target column \"" + target_column +
                                     "\" is not in the CSV header");
        }
        target_index = static_cast<std::size_t>(found - header.begin());
    }
    TabularDataset data;
    data.target_name = header[target_index];
    for (std::size_t column = 0; column < header.size(); ++column) {
        if (column != target_index) data.feature_names.push_back(header[column]);
    }
    // First pass: collect raw cells and decide the task from the target
    // column (all-numeric -> regression, anything else -> classification).
    std::vector<std::string> raw_targets;
    raw_targets.reserve(lines.size() - 1U);
    bool numeric_target = true;
    for (std::size_t row = 1; row < lines.size(); ++row) {
        auto cells = split_csv_line(lines[row]);
        if (cells.size() != header.size()) {
            throw std::runtime_error(
                "row " + std::to_string(row + 1U) + " has " +
                std::to_string(cells.size()) + " cells but the header has " +
                std::to_string(header.size()));
        }
        std::vector<double> features;
        features.reserve(data.feature_names.size());
        for (std::size_t column = 0; column < cells.size(); ++column) {
            const auto cell = trim(cells[column]);
            if (column == target_index) {
                double numeric = 0.0;
                if (!parse_double(cell, numeric)) numeric_target = false;
                raw_targets.push_back(cell);
                continue;
            }
            double value = 0.0;
            if (!parse_double(cell, value)) {
                throw std::runtime_error(
                    "feature column \"" + header[column] + "\" has non-numeric value \"" +
                    cell + "\" in row " + std::to_string(row + 1U) +
                    "; every feature column must be numeric");
            }
            features.push_back(value);
        }
        data.features.push_back(std::move(features));
    }
    data.classification = !numeric_target;
    if (data.classification) {
        // Sorted-unique labels give a deterministic class order regardless
        // of row order, so artifacts and confusion matrices stay stable.
        const std::set<std::string> unique(raw_targets.begin(), raw_targets.end());
        data.class_labels.assign(unique.begin(), unique.end());
        if (data.class_labels.size() < 2U) {
            throw std::runtime_error(
                "classification target has only one distinct label");
        }
        if (data.class_labels.size() > 64U) {
            throw std::runtime_error(
                "classification target has more than 64 distinct labels; "
                "check that the target column is really categorical");
        }
        for (const auto& raw : raw_targets) {
            const auto found = std::lower_bound(data.class_labels.begin(),
                                                data.class_labels.end(), raw);
            data.targets.push_back(
                static_cast<double>(found - data.class_labels.begin()));
        }
    } else {
        for (const auto& raw : raw_targets) {
            double value = 0.0;
            parse_double(raw, value);
            data.targets.push_back(value);
        }
    }
    return data;
}

TabularCleanReport clean_tabular_csv(const std::string& csv) {
    // Split into raw lines without dropping blanks yet (unlike
    // parse_tabular_csv's line split) so blank rows can be counted rather
    // than silently disappearing.
    std::vector<std::string> raw_lines;
    std::size_t position = 0U;
    while (position <= csv.size()) {
        const auto newline = csv.find('\n', position);
        const auto end = newline == std::string::npos ? csv.size() : newline;
        raw_lines.push_back(csv.substr(position, end - position));
        if (newline == std::string::npos) break;
        position = newline + 1U;
    }
    // A trailing newline produces one final empty "line" that is a
    // formatting artifact, not a data row -- drop it before counting.
    if (raw_lines.size() > 1U && trim(raw_lines.back()).empty()) {
        raw_lines.pop_back();
    }
    if (raw_lines.empty()) {
        throw std::runtime_error("dataset has no content to clean");
    }
    TabularCleanReport report;
    const std::string header = trim(raw_lines.front());
    report.rows_before = raw_lines.size() - 1U;
    std::vector<std::string> cleaned;
    std::set<std::string> seen;
    for (std::size_t index = 1; index < raw_lines.size(); ++index) {
        const auto trimmed = trim(raw_lines[index]);
        if (trimmed.empty()) {
            ++report.blank_rows_removed;
            continue;
        }
        if (!seen.insert(trimmed).second) {
            ++report.duplicate_rows_removed;
            continue;
        }
        cleaned.push_back(trimmed);
    }
    report.rows_after = cleaned.size();
    std::string rebuilt = header;
    for (const auto& row : cleaned) {
        rebuilt += "\n";
        rebuilt += row;
    }
    report.csv = std::move(rebuilt);
    return report;
}

namespace {

// Quotes a rebuilt CSV field only when it actually needs it (contains a
// comma, quote, or newline), doubling any embedded quotes -- the inverse of
// split_csv_line() above, so a row this function writes always re-parses
// back to the same fields.
std::string join_csv_field(const std::string& field) {
    const bool needs_quoting =
        field.find(',') != std::string::npos || field.find('"') != std::string::npos ||
        field.find('\n') != std::string::npos;
    if (!needs_quoting) return field;
    std::string quoted = "\"";
    for (const char character : field) {
        if (character == '"') quoted += "\"\"";
        else quoted += character;
    }
    quoted += "\"";
    return quoted;
}

std::string join_csv_row(const std::vector<std::string>& fields) {
    std::string row;
    for (std::size_t index = 0; index < fields.size(); ++index) {
        if (index > 0U) row += ",";
        row += join_csv_field(fields[index]);
    }
    return row;
}

}  // namespace

TabularAutoLabelReport auto_label_tabular_dataset(const std::string& csv,
                                                  const std::string& target_column) {
    std::vector<std::string> raw_lines;
    std::size_t position = 0U;
    while (position <= csv.size()) {
        const auto newline = csv.find('\n', position);
        const auto end = newline == std::string::npos ? csv.size() : newline;
        raw_lines.push_back(csv.substr(position, end - position));
        if (newline == std::string::npos) break;
        position = newline + 1U;
    }
    if (raw_lines.size() > 1U && trim(raw_lines.back()).empty()) raw_lines.pop_back();
    if (raw_lines.size() < 2U) {
        throw std::runtime_error("dataset needs a header row and at least one data row");
    }
    auto header = split_csv_line(trim(raw_lines.front()));
    for (auto& name : header) name = trim(name);
    if (header.empty()) throw std::runtime_error("dataset header is empty");
    std::size_t target_index = header.size() - 1U;
    if (!target_column.empty()) {
        const auto found = std::find(header.begin(), header.end(), target_column);
        if (found == header.end()) {
            throw std::runtime_error("target column \"" + target_column +
                                     "\" is not in the CSV header");
        }
        target_index = static_cast<std::size_t>(found - header.begin());
    }
    std::vector<std::vector<std::string>> rows;
    rows.reserve(raw_lines.size() - 1U);
    for (std::size_t index = 1; index < raw_lines.size(); ++index) {
        if (trim(raw_lines[index]).empty()) continue;
        rows.push_back(split_csv_line(raw_lines[index]));
    }
    if (rows.empty()) throw std::runtime_error("dataset has no data rows to label");

    TabularAutoLabelReport report;
    report.rows_total = rows.size();
    std::vector<bool> missing(rows.size(), false);
    for (std::size_t row = 0; row < rows.size(); ++row) {
        const auto value = target_index < rows[row].size() ? trim(rows[row][target_index])
                                                            : std::string{};
        if (value.empty()) missing[row] = true; else ++report.rows_already_labeled;
    }
    if (report.rows_already_labeled == rows.size()) {
        report.method = "existing_labels_validated";
        std::string rebuilt = trim(raw_lines.front());
        for (const auto& row : rows) {
            rebuilt += "\n";
            rebuilt += join_csv_row(row);
        }
        report.csv = std::move(rebuilt);
        return report;
    }

    // Find the first column, other than the target, whose value parses as a
    // real number on every data row -- the deterministic binning source.
    std::size_t source_index = header.size();
    std::vector<double> source_values;
    for (std::size_t column = 0; column < header.size(); ++column) {
        if (column == target_index) continue;
        std::vector<double> candidate_values;
        candidate_values.reserve(rows.size());
        bool all_numeric = true;
        for (const auto& row : rows) {
            double value = 0.0;
            const auto text = column < row.size() ? trim(row[column]) : std::string{};
            if (!parse_double(text, value)) { all_numeric = false; break; }
            candidate_values.push_back(value);
        }
        if (all_numeric) {
            source_index = column;
            source_values = std::move(candidate_values);
            break;
        }
    }
    if (source_index == header.size()) {
        throw std::runtime_error(
            "no fully-numeric column is available to derive labels from");
    }

    auto sorted_values = source_values;
    std::sort(sorted_values.begin(), sorted_values.end());
    const auto percentile = [&sorted_values](const double fraction) {
        const auto index = static_cast<std::size_t>(
            fraction * static_cast<double>(sorted_values.size() - 1U));
        return sorted_values[index];
    };
    report.method = "quantile_binning";
    report.source_column = header[source_index];
    report.low_medium_threshold = percentile(1.0 / 3.0);
    report.medium_high_threshold = percentile(2.0 / 3.0);

    for (std::size_t row = 0; row < rows.size(); ++row) {
        if (!missing[row]) continue;
        const double value = source_values[row];
        std::string label;
        if (value <= report.low_medium_threshold) label = "low";
        else if (value <= report.medium_high_threshold) label = "medium";
        else label = "high";
        if (target_index >= rows[row].size()) rows[row].resize(target_index + 1U);
        rows[row][target_index] = label;
        ++report.rows_labeled;
    }

    std::string rebuilt = trim(raw_lines.front());
    for (const auto& row : rows) {
        rebuilt += "\n";
        rebuilt += join_csv_row(row);
    }
    report.csv = std::move(rebuilt);
    return report;
}

void TrainingProgressTracker::begin(const std::string& job_id,
                                    const std::uint32_t total_epochs) {
    std::lock_guard<std::mutex> lock(mutex_);
    TrainingProgressSnapshot snapshot;
    snapshot.running = true;
    snapshot.total_epochs = total_epochs;
    snapshot.updated_at_epoch_seconds = epoch_seconds();
    progress_[job_id] = snapshot;
}

void TrainingProgressTracker::update(const std::string& job_id,
                                     const std::uint32_t epoch, const double loss) {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = progress_.find(job_id);
    if (found == progress_.end()) return;  // end()/never begin()-ed: nothing to update.
    // epoch is 0-based in the training loop; reported as 1-based "epochs
    // completed so far" so a poller reading current_epoch == total_epochs
    // sees a run that has finished its last epoch, not one epoch short.
    found->second.current_epoch = epoch + 1U;
    found->second.current_loss = loss;
    found->second.updated_at_epoch_seconds = epoch_seconds();
}

void TrainingProgressTracker::end(const std::string& job_id) noexcept {
    std::lock_guard<std::mutex> lock(mutex_);
    progress_.erase(job_id);
}

TrainingProgressSnapshot TrainingProgressTracker::snapshot(
    const std::string& job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto found = progress_.find(job_id);
    return found != progress_.end() ? found->second : TrainingProgressSnapshot{};
}

std::vector<std::string> TrainingProgressTracker::active_job_ids() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(progress_.size());
    for (const auto& entry : progress_) ids.push_back(entry.first);
    return ids;
}

std::string training_progress_snapshot_json(const TrainingProgressSnapshot& snapshot) {
    return "{\"running\":" + std::string(snapshot.running ? "true" : "false") +
          ",\"currentEpoch\":" + std::to_string(snapshot.current_epoch) +
          ",\"totalEpochs\":" + std::to_string(snapshot.total_epochs) +
          ",\"currentLoss\":" + json_number(snapshot.current_loss) +
          ",\"updatedAtEpochSeconds\":" +
          std::to_string(snapshot.updated_at_epoch_seconds) + "}";
}

TabularSplitReport split_tabular_csv(const std::string& csv,
                                     const std::string& target_column,
                                     const double holdout_fraction) {
    // Real parse first, so a malformed dataset fails the split the same
    // honest way "Validate data" fails validation.
    const auto data = parse_tabular_csv(csv, target_column);
    if (holdout_fraction < 0.0 || holdout_fraction > 0.9) {
        throw std::runtime_error("holdout fraction must be in [0, 0.9]");
    }
    const std::size_t row_count = data.features.size();
    // Identical formula to train_tabular_model's internal split, so the
    // reported counts are exactly what a subsequent Train model stage will
    // actually use, not a second, possibly-inconsistent calculation.
    auto holdout_count = static_cast<std::size_t>(holdout_fraction *
                                                   static_cast<double>(row_count));
    if (holdout_count >= row_count) holdout_count = row_count - 1U;
    TabularSplitReport report;
    report.total_rows = row_count;
    report.holdout_rows = holdout_count;
    report.train_rows = row_count - holdout_count;
    return report;
}

TabularPruneReport prune_tabular_model(TrainedTabularModel& model,
                                       const double threshold) {
    TabularPruneReport report;
    for (auto& row : model.weights) {
        for (auto& weight : row) {
            ++report.weights_total;
            if (std::abs(weight) < threshold) {
                weight = 0.0;
            }
            if (weight == 0.0) ++report.weights_pruned;
        }
    }
    return report;
}

DatasetContentStore::DatasetContentStore(RecordStore& records)
    : records_(&records) {}

void DatasetContentStore::put(const std::string& dataset_id,
                              const std::string& csv,
                              const std::string& target_column) {
    records_->put("ml_dataset_content", dataset_id, pack({csv, target_column}));
}

std::optional<DatasetContentStore::Content> DatasetContentStore::find(
    const std::string& dataset_id) const {
    const auto value = records_->get("ml_dataset_content", dataset_id);
    if (!value) return std::nullopt;
    auto fields = unpack(*value);
    if (fields.size() != 2U) {
        throw std::runtime_error("persisted dataset content is malformed");
    }
    return Content{std::move(fields[0]), std::move(fields[1])};
}

bool DatasetContentStore::remove(const std::string& dataset_id) {
    if (!records_->get("ml_dataset_content", dataset_id)) return false;
    records_->erase("ml_dataset_content", dataset_id);
    return true;
}

std::string tabular_dataset_profile_json(const std::string& dataset_id,
                                         const TabularDataset& data) {
    return "{\"datasetId\":\"" + json_escape(dataset_id) +
           "\",\"rows\":" + std::to_string(data.features.size()) +
           ",\"featureColumns\":" + string_array_json(data.feature_names) +
           ",\"targetColumn\":\"" + json_escape(data.target_name) +
           "\",\"task\":\"" +
           (data.classification ? "classification" : "regression") +
           "\",\"classes\":" + string_array_json(data.class_labels) + "}";
}

TabularTrainingReport train_tabular_model(
    const TabularDataset& data, const TabularTrainingOptions& options,
    TrainedTabularModel& model, const TrainedTabularModel* warm_start,
    const std::function<void(std::uint32_t, double, const TrainedTabularModel&)>&
        on_epoch) {
    if (data.features.size() < 2U) {
        throw std::runtime_error("dataset has fewer than two rows");
    }
    if (options.epochs == 0U || options.epochs > 10000U) {
        throw std::runtime_error("epochs must be between 1 and 10000");
    }
    if (!(options.learning_rate > 0.0) || options.learning_rate > 10.0) {
        throw std::runtime_error("learning rate must be in (0, 10]");
    }
    if (options.test_fraction < 0.0 || options.test_fraction > 0.9) {
        throw std::runtime_error("test fraction must be in [0, 0.9]");
    }
    if (warm_start != nullptr) {
        if (warm_start->feature_names != data.feature_names) {
            throw std::runtime_error(
                "fine-tuning dataset feature columns do not match the base model");
        }
        if (warm_start->classification != data.classification) {
            throw std::runtime_error(
                "fine-tuning dataset task does not match the base model");
        }
        if (data.classification && warm_start->class_labels != data.class_labels) {
            throw std::runtime_error(
                "fine-tuning dataset labels do not match the base model");
        }
    }
    const std::size_t row_count = data.features.size();
    const std::size_t feature_count = data.feature_names.size();

    // Deterministic shuffled split: same seed, same split, reproducible run.
    std::vector<std::size_t> order(row_count);
    std::iota(order.begin(), order.end(), 0U);
    std::mt19937 generator(options.seed);
    std::shuffle(order.begin(), order.end(), generator);
    auto test_count = static_cast<std::size_t>(
        options.test_fraction * static_cast<double>(row_count));
    if (test_count >= row_count) test_count = row_count - 1U;
    const std::size_t train_count = row_count - test_count;

    // Standardization statistics come from the training split only, so the
    // held-out metrics never leak test-set information into the model.
    model.feature_means.assign(feature_count, 0.0);
    model.feature_stddevs.assign(feature_count, 1.0);
    for (std::size_t column = 0; column < feature_count; ++column) {
        double sum = 0.0;
        for (std::size_t index = 0; index < train_count; ++index) {
            sum += data.features[order[index]][column];
        }
        const double mean = sum / static_cast<double>(train_count);
        double variance = 0.0;
        for (std::size_t index = 0; index < train_count; ++index) {
            const double delta = data.features[order[index]][column] - mean;
            variance += delta * delta;
        }
        variance /= static_cast<double>(train_count);
        model.feature_means[column] = mean;
        // A constant column standardizes to zero everywhere instead of
        // dividing by zero; its weight simply never moves.
        model.feature_stddevs[column] = variance > 0.0 ? std::sqrt(variance) : 1.0;
    }
    const auto standardize_row = [&](const std::size_t row) {
        std::vector<double> standardized(feature_count);
        for (std::size_t column = 0; column < feature_count; ++column) {
            standardized[column] =
                (data.features[row][column] - model.feature_means[column]) /
                model.feature_stddevs[column];
        }
        return standardized;
    };
    std::vector<std::vector<double>> train_rows(train_count);
    std::vector<double> train_targets(train_count);
    for (std::size_t index = 0; index < train_count; ++index) {
        train_rows[index] = standardize_row(order[index]);
        train_targets[index] = data.targets[order[index]];
    }
    std::vector<std::vector<double>> test_rows(test_count);
    std::vector<double> test_targets(test_count);
    for (std::size_t index = 0; index < test_count; ++index) {
        test_rows[index] = standardize_row(order[train_count + index]);
        test_targets[index] = data.targets[order[train_count + index]];
    }

    model.classification = data.classification;
    model.feature_names = data.feature_names;
    model.target_name = data.target_name;
    model.class_labels = data.class_labels;
    const std::size_t output_count =
        data.classification ? data.class_labels.size() : 1U;
    model.method = !data.classification ? "linear_regression"
                   : output_count == 2U ? "logistic_regression"
                                        : "softmax_regression";
    model.weights.assign(output_count, std::vector<double>(feature_count + 1U, 0.0));
    // Warm start: continue gradient descent from the base model's learned
    // weights instead of zero, so this run genuinely fine-tunes it rather
    // than training a fresh model that happens to reuse the same code path.
    if (warm_start != nullptr) model.weights = warm_start->weights;

    TabularTrainingReport report;
    report.train_rows = train_count;
    report.test_rows = test_count;
    report.loss_history.reserve(options.epochs);
    const double count = static_cast<double>(train_count);

    // Full-batch gradient descent. Classification minimizes mean
    // cross-entropy via softmax (2-class softmax is exactly logistic
    // regression); regression minimizes mean squared error. Gradients are
    // accumulated per epoch and applied once -- deterministic and stable
    // for the tabular sizes this endpoint accepts.
    for (std::uint32_t epoch = 0; epoch < options.epochs; ++epoch) {
        std::vector<std::vector<double>> gradients(
            output_count, std::vector<double>(feature_count + 1U, 0.0));
        double loss = 0.0;
        for (std::size_t row = 0; row < train_count; ++row) {
            if (data.classification) {
                std::vector<double> logits(output_count);
                for (std::size_t klass = 0; klass < output_count; ++klass) {
                    logits[klass] = score_row(model.weights[klass], train_rows[row]);
                }
                const auto probabilities = softmax(logits);
                const auto actual = static_cast<std::size_t>(train_targets[row]);
                loss += -std::log(std::max(probabilities[actual], 1e-12));
                for (std::size_t klass = 0; klass < output_count; ++klass) {
                    const double error =
                        probabilities[klass] - (klass == actual ? 1.0 : 0.0);
                    for (std::size_t column = 0; column < feature_count; ++column) {
                        gradients[klass][column] += error * train_rows[row][column];
                    }
                    gradients[klass][feature_count] += error;
                }
            } else {
                const double predicted = score_row(model.weights[0], train_rows[row]);
                const double error = predicted - train_targets[row];
                loss += error * error;
                for (std::size_t column = 0; column < feature_count; ++column) {
                    gradients[0][column] += error * train_rows[row][column];
                }
                gradients[0][feature_count] += error;
            }
        }
        loss /= count;
        for (std::size_t klass = 0; klass < output_count; ++klass) {
            for (std::size_t column = 0; column <= feature_count; ++column) {
                model.weights[klass][column] -=
                    options.learning_rate * gradients[klass][column] / count;
            }
        }
        if (!std::isfinite(loss)) {
            throw std::runtime_error(
                "training diverged (non-finite loss); lower the learning rate");
        }
        report.loss_history.push_back(loss);
        if (on_epoch) on_epoch(epoch, loss, model);
    }
    report.final_loss = report.loss_history.back();
    report.evaluated_on_test = test_count > 0U;
    report.metrics = compute_metrics(model,
                                     report.evaluated_on_test ? test_rows : train_rows,
                                     report.evaluated_on_test ? test_targets
                                                              : train_targets);
    model.trained_at_epoch_seconds = epoch_seconds();
    return report;
}

TabularEvaluationMetrics evaluate_tabular_model(const TrainedTabularModel& model,
                                                const TabularDataset& data) {
    if (data.feature_names != model.feature_names) {
        throw std::runtime_error(
            "evaluation dataset feature columns do not match the trained model");
    }
    if (model.classification != data.classification) {
        throw std::runtime_error(
            "evaluation dataset task does not match the trained model");
    }
    std::vector<std::vector<double>> standardized(data.features.size());
    std::vector<double> targets(data.targets.size());
    for (std::size_t row = 0; row < data.features.size(); ++row) {
        standardized[row] = standardize(model, data.features[row]);
        if (model.classification) {
            // Remap this dataset's class index onto the model's label
            // order so a differently-shuffled benchmark still scores
            // correctly; an unseen label is a schema mismatch.
            const auto& label =
                data.class_labels[static_cast<std::size_t>(data.targets[row])];
            const auto found = std::find(model.class_labels.begin(),
                                         model.class_labels.end(), label);
            if (found == model.class_labels.end()) {
                throw std::runtime_error("evaluation dataset label \"" + label +
                                         "\" is unknown to the trained model");
            }
            targets[row] =
                static_cast<double>(found - model.class_labels.begin());
        } else {
            targets[row] = data.targets[row];
        }
    }
    return compute_metrics(model, standardized, targets);
}

TabularPrediction predict_tabular(const TrainedTabularModel& model,
                                  const std::vector<double>& features) {
    if (features.size() != model.feature_names.size()) {
        throw std::runtime_error(
            "prediction needs " + std::to_string(model.feature_names.size()) +
            " feature values but got " + std::to_string(features.size()));
    }
    const auto standardized = standardize(model, features);
    TabularPrediction prediction;
    if (model.classification) {
        std::vector<double> logits(model.weights.size());
        for (std::size_t klass = 0; klass < model.weights.size(); ++klass) {
            logits[klass] = score_row(model.weights[klass], standardized);
        }
        prediction.class_probabilities = softmax(logits);
        const auto winner = static_cast<std::size_t>(
            std::max_element(prediction.class_probabilities.begin(),
                             prediction.class_probabilities.end()) -
            prediction.class_probabilities.begin());
        prediction.value = static_cast<double>(winner);
        prediction.label = model.class_labels[winner];
    } else {
        prediction.value = score_row(model.weights[0], standardized);
    }
    return prediction;
}

TrainedModelStore::TrainedModelStore(RecordStore& records)
    : records_(&records) {}

void TrainedModelStore::put(const TrainedTabularModel& model) {
    // Flat field list: fixed header fields, then counted variable sections
    // (feature names, class labels, means, stddevs, weight rows).
    std::vector<std::string> fields{
        model.model_id,
        model.training_job_id,
        model.method,
        model.classification ? "1" : "0",
        model.target_name,
        std::to_string(model.trained_at_epoch_seconds),
        std::to_string(model.feature_names.size()),
        std::to_string(model.class_labels.size()),
        std::to_string(model.weights.size())};
    for (const auto& name : model.feature_names) fields.push_back(name);
    for (const auto& label : model.class_labels) fields.push_back(label);
    for (const auto mean : model.feature_means) fields.push_back(number_field(mean));
    for (const auto stddev : model.feature_stddevs) {
        fields.push_back(number_field(stddev));
    }
    for (const auto& row : model.weights) {
        for (const auto weight : row) fields.push_back(number_field(weight));
    }
    records_->put("ml_trained_models", model.model_id, pack(fields));
}

std::optional<TrainedTabularModel> TrainedModelStore::find(
    const std::string& model_id) const {
    const auto value = records_->get("ml_trained_models", model_id);
    if (!value) return std::nullopt;
    const auto fields = unpack(*value);
    if (fields.size() < 9U) {
        throw std::runtime_error("persisted trained model is malformed");
    }
    TrainedTabularModel model;
    model.model_id = fields[0];
    model.training_job_id = fields[1];
    model.method = fields[2];
    model.classification = fields[3] == "1";
    model.target_name = fields[4];
    model.trained_at_epoch_seconds = std::stoull(fields[5]);
    const auto feature_count = static_cast<std::size_t>(std::stoull(fields[6]));
    const auto class_count = static_cast<std::size_t>(std::stoull(fields[7]));
    const auto output_count = static_cast<std::size_t>(std::stoull(fields[8]));
    const std::size_t expected = 9U + feature_count + class_count +
                                 2U * feature_count +
                                 output_count * (feature_count + 1U);
    if (fields.size() != expected) {
        throw std::runtime_error("persisted trained model is truncated");
    }
    std::size_t cursor = 9U;
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_names.push_back(fields[cursor++]);
    }
    for (std::size_t index = 0; index < class_count; ++index) {
        model.class_labels.push_back(fields[cursor++]);
    }
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_means.push_back(parse_number_field(fields[cursor++]));
    }
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_stddevs.push_back(parse_number_field(fields[cursor++]));
    }
    for (std::size_t output = 0; output < output_count; ++output) {
        std::vector<double> row;
        for (std::size_t column = 0; column <= feature_count; ++column) {
            row.push_back(parse_number_field(fields[cursor++]));
        }
        model.weights.push_back(std::move(row));
    }
    return model;
}

bool TrainedModelStore::remove(const std::string& model_id) {
    if (!records_->get("ml_trained_models", model_id)) return false;
    records_->erase("ml_trained_models", model_id);
    return true;
}

// Phase 79: identical flat-field pack/unpack shape to TrainedModelStore
// above, on its own table and keyed by checkpoint id instead of model id --
// see the class comment in masterai.hpp for why the snapshot's own
// `model_id` field is repurposed to carry the checkpoint id.
CheckpointModelStore::CheckpointModelStore(RecordStore& records)
    : records_(&records) {}

void CheckpointModelStore::put(const TrainedTabularModel& model) {
    std::vector<std::string> fields{
        model.model_id,
        model.training_job_id,
        model.method,
        model.classification ? "1" : "0",
        model.target_name,
        std::to_string(model.trained_at_epoch_seconds),
        std::to_string(model.feature_names.size()),
        std::to_string(model.class_labels.size()),
        std::to_string(model.weights.size())};
    for (const auto& name : model.feature_names) fields.push_back(name);
    for (const auto& label : model.class_labels) fields.push_back(label);
    for (const auto mean : model.feature_means) fields.push_back(number_field(mean));
    for (const auto stddev : model.feature_stddevs) {
        fields.push_back(number_field(stddev));
    }
    for (const auto& row : model.weights) {
        for (const auto weight : row) fields.push_back(number_field(weight));
    }
    records_->put("ml_checkpoint_models", model.model_id, pack(fields));
}

std::optional<TrainedTabularModel> CheckpointModelStore::find(
    const std::string& checkpoint_id) const {
    const auto value = records_->get("ml_checkpoint_models", checkpoint_id);
    if (!value) return std::nullopt;
    const auto fields = unpack(*value);
    if (fields.size() < 9U) {
        throw std::runtime_error("persisted checkpoint model is malformed");
    }
    TrainedTabularModel model;
    model.model_id = fields[0];
    model.training_job_id = fields[1];
    model.method = fields[2];
    model.classification = fields[3] == "1";
    model.target_name = fields[4];
    model.trained_at_epoch_seconds = std::stoull(fields[5]);
    const auto feature_count = static_cast<std::size_t>(std::stoull(fields[6]));
    const auto class_count = static_cast<std::size_t>(std::stoull(fields[7]));
    const auto output_count = static_cast<std::size_t>(std::stoull(fields[8]));
    const std::size_t expected = 9U + feature_count + class_count +
                                 2U * feature_count +
                                 output_count * (feature_count + 1U);
    if (fields.size() != expected) {
        throw std::runtime_error("persisted checkpoint model is truncated");
    }
    std::size_t cursor = 9U;
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_names.push_back(fields[cursor++]);
    }
    for (std::size_t index = 0; index < class_count; ++index) {
        model.class_labels.push_back(fields[cursor++]);
    }
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_means.push_back(parse_number_field(fields[cursor++]));
    }
    for (std::size_t index = 0; index < feature_count; ++index) {
        model.feature_stddevs.push_back(parse_number_field(fields[cursor++]));
    }
    for (std::size_t output = 0; output < output_count; ++output) {
        std::vector<double> row;
        for (std::size_t column = 0; column <= feature_count; ++column) {
            row.push_back(parse_number_field(fields[cursor++]));
        }
        model.weights.push_back(std::move(row));
    }
    return model;
}

bool CheckpointModelStore::remove(const std::string& checkpoint_id) {
    if (!records_->get("ml_checkpoint_models", checkpoint_id)) return false;
    records_->erase("ml_checkpoint_models", checkpoint_id);
    return true;
}

EvaluationResultStore::EvaluationResultStore(RecordStore& records)
    : records_(&records) {}

void EvaluationResultStore::put(const std::string& run_id,
                                const std::string& metrics_json) {
    records_->put("ml_evaluation_results", run_id, metrics_json);
}

std::optional<std::string> EvaluationResultStore::find(
    const std::string& run_id) const {
    return records_->get("ml_evaluation_results", run_id);
}

bool EvaluationResultStore::remove(const std::string& run_id) {
    if (!records_->get("ml_evaluation_results", run_id)) return false;
    records_->erase("ml_evaluation_results", run_id);
    return true;
}

std::string tabular_evaluation_metrics_json(
    const TabularEvaluationMetrics& metrics) {
    std::string result =
        "{\"task\":\"" +
        std::string(metrics.classification ? "classification" : "regression") +
        "\",\"evaluatedRows\":" + std::to_string(metrics.evaluated_rows);
    if (metrics.classification) {
        result += ",\"accuracy\":" + json_number(metrics.accuracy) +
                  ",\"macroPrecision\":" + json_number(metrics.macro_precision) +
                  ",\"macroRecall\":" + json_number(metrics.macro_recall) +
                  ",\"macroF1\":" + json_number(metrics.macro_f1) +
                  ",\"confusionMatrix\":" + confusion_json(metrics.confusion);
    } else {
        result += ",\"mse\":" + json_number(metrics.mse) +
                  ",\"mae\":" + json_number(metrics.mae) +
                  ",\"rSquared\":" + json_number(metrics.r_squared);
    }
    return result + "}";
}

std::string tabular_training_report_json(const TabularTrainingReport& report,
                                         const TrainedTabularModel& model) {
    // The loss curve is downsampled to at most 100 points for the response
    // (first/last always kept); the full curve only matters at plot
    // resolution and a 10000-epoch run should not emit a 10000-element array.
    std::vector<double> sampled;
    const std::size_t total = report.loss_history.size();
    const std::size_t stride = total > 100U ? total / 100U : 1U;
    for (std::size_t index = 0; index < total; index += stride) {
        sampled.push_back(report.loss_history[index]);
    }
    if (!report.loss_history.empty() &&
        (sampled.empty() || sampled.back() != report.loss_history.back())) {
        sampled.push_back(report.loss_history.back());
    }
    return "{\"modelId\":\"" + json_escape(model.model_id) +
           "\",\"trainingJobId\":\"" + json_escape(model.training_job_id) +
           "\",\"method\":\"" + json_escape(model.method) +
           "\",\"epochs\":" + std::to_string(report.loss_history.size()) +
           ",\"finalLoss\":" + json_number(report.final_loss) +
           ",\"trainRows\":" + std::to_string(report.train_rows) +
           ",\"testRows\":" + std::to_string(report.test_rows) +
           ",\"evaluatedOnTest\":" +
           (report.evaluated_on_test ? "true" : "false") +
           ",\"lossCurve\":" + number_array_json(sampled) +
           ",\"metrics\":" + tabular_evaluation_metrics_json(report.metrics) + "}";
}

std::string trained_tabular_model_summary_json(const TrainedTabularModel& model) {
    return "{\"modelId\":\"" + json_escape(model.model_id) +
           "\",\"trainingJobId\":\"" + json_escape(model.training_job_id) +
           "\",\"method\":\"" + json_escape(model.method) +
           "\",\"task\":\"" +
           (model.classification ? "classification" : "regression") +
           "\",\"featureColumns\":" + string_array_json(model.feature_names) +
           ",\"targetColumn\":\"" + json_escape(model.target_name) +
           "\",\"classes\":" + string_array_json(model.class_labels) +
           ",\"trainedAtEpochSeconds\":" +
           std::to_string(model.trained_at_epoch_seconds) + "}";
}

// Phase 57: docs/PLAN.md "Machine Learning Abilities" section 27 (Model
// Comparison) -- the real comparison executor. Both metric sets arrive
// already computed by evaluate_tabular_model against the same benchmark
// dataset, so every number below is measured, not estimated. The primary
// metric decides the winner: macro F1 for classification (it balances
// precision and recall across every class, so a majority-class cheat
// can't win), MSE for regression (lower is better). The delta is always
// candidate minus baseline on that primary metric.
ComparisonResultStore::ComparisonResultStore(RecordStore& records)
    : records_(&records) {}

void ComparisonResultStore::put(const std::string& comparison_id,
                                const std::string& result_json) {
    records_->put("ml_comparison_results", comparison_id, result_json);
}

std::optional<std::string> ComparisonResultStore::find(
    const std::string& comparison_id) const {
    return records_->get("ml_comparison_results", comparison_id);
}

bool ComparisonResultStore::remove(const std::string& comparison_id) {
    if (!records_->get("ml_comparison_results", comparison_id)) return false;
    records_->erase("ml_comparison_results", comparison_id);
    return true;
}

std::string tabular_model_comparison_json(
    const TrainedTabularModel& baseline,
    const TabularEvaluationMetrics& baseline_metrics,
    const TrainedTabularModel& candidate,
    const TabularEvaluationMetrics& candidate_metrics) {
    if (baseline_metrics.classification != candidate_metrics.classification) {
        throw std::runtime_error(
            "cannot compare a classification model against a regression model");
    }
    const bool classification = baseline_metrics.classification;
    const char* primary_metric = classification ? "macroF1" : "mse";
    const double baseline_primary =
        classification ? baseline_metrics.macro_f1 : baseline_metrics.mse;
    const double candidate_primary =
        classification ? candidate_metrics.macro_f1 : candidate_metrics.mse;
    const double delta = candidate_primary - baseline_primary;
    // Higher macro F1 wins; lower MSE wins. An exact tie (identical
    // artifacts, or two models converged to the same optimum) is reported
    // honestly instead of picking a side.
    const char* winner = "tie";
    if (candidate_primary != baseline_primary) {
        const bool candidate_wins = classification
                                        ? candidate_primary > baseline_primary
                                        : candidate_primary < baseline_primary;
        winner = candidate_wins ? "candidate" : "baseline";
    }
    const auto side = [](const TrainedTabularModel& model,
                         const TabularEvaluationMetrics& metrics) {
        return "{\"modelId\":\"" + json_escape(model.model_id) +
               "\",\"method\":\"" + json_escape(model.method) +
               "\",\"metrics\":" + tabular_evaluation_metrics_json(metrics) +
               "}";
    };
    return "{\"task\":\"" +
           std::string(classification ? "classification" : "regression") +
           "\",\"baseline\":" + side(baseline, baseline_metrics) +
           ",\"candidate\":" + side(candidate, candidate_metrics) +
           ",\"primaryMetric\":\"" + primary_metric +
           "\",\"baselineValue\":" + json_number(baseline_primary) +
           ",\"candidateValue\":" + json_number(candidate_primary) +
           ",\"delta\":" + json_number(delta) + ",\"winner\":\"" + winner +
           "\"}";
}

ExperimentResultStore::ExperimentResultStore(RecordStore& records)
    : records_(&records) {}

void ExperimentResultStore::put(const std::string& experiment_id,
                                const std::string& result_json) {
    records_->put("ml_experiment_results", experiment_id, result_json);
}

std::optional<std::string> ExperimentResultStore::find(
    const std::string& experiment_id) const {
    return records_->get("ml_experiment_results", experiment_id);
}

bool ExperimentResultStore::remove(const std::string& experiment_id) {
    if (!records_->get("ml_experiment_results", experiment_id)) return false;
    records_->erase("ml_experiment_results", experiment_id);
    return true;
}

std::string experiment_result_json(
    const TabularTrainingReport& report,
    const TabularEvaluationMetrics& evaluation_metrics,
    const HardwareInfo& hardware, const std::uint64_t runtime_milliseconds,
    const std::vector<std::string>& checkpoint_ids,
    const std::string& log_text, const std::string& artifact_model_id) {
    // Training metrics come straight from the real gradient-descent loss
    // curve; validation metrics are the held-out split train_tabular_model
    // computed as part of the same run; evaluation metrics are an
    // independent evaluate_tabular_model pass against the full dataset --
    // three genuinely distinct numbers, matching section 25's three
    // separate metric fields.
    const std::string training_metrics =
        "{\"epochs\":" + std::to_string(report.loss_history.size()) +
        ",\"finalLoss\":" + json_number(report.final_loss) + "}";
    return "{\"trainingMetrics\":" + training_metrics +
           ",\"validationMetrics\":" +
           tabular_evaluation_metrics_json(report.metrics) +
           ",\"evaluationMetrics\":" +
           tabular_evaluation_metrics_json(evaluation_metrics) +
           ",\"hardware\":" + hardware_info_json(hardware) +
           ",\"runtimeMilliseconds\":" +
           std::to_string(runtime_milliseconds) +
           ",\"checkpointIds\":" + string_array_json(checkpoint_ids) +
           ",\"logs\":\"" + json_escape(log_text) +
           "\",\"artifacts\":{\"trainedModelId\":\"" +
           json_escape(artifact_model_id) + "\"}}";
}

namespace {

// Pulls the primary-metric name/value and runtime out of one experiment's
// already-built result_json (produced by experiment_result_json above),
// following tabular_model_comparison_json's own primary-metric choice:
// macro F1 for classification, MSE for regression. Returns nullopt if the
// experiment has no result yet or its result has no evaluation metrics.
struct ExperimentResultSummary {
    std::string primary_metric_name;
    double primary_metric_value{0.0};
    std::uint64_t runtime_milliseconds{0};
    std::string storage_class;
    unsigned int logical_cpu_count{0};
    std::uint64_t gpu_memory_mib{0};
};

std::optional<ExperimentResultSummary> summarize_experiment_result(
    const std::string& result_json) {
    if (result_json.empty()) return std::nullopt;
    try {
        const auto root = parse_json(result_json, 4U * 1024U * 1024U);
        const auto& metrics = root.required("evaluationMetrics");
        const bool classification =
            metrics.required("task").as_string() == "classification";
        ExperimentResultSummary summary;
        summary.primary_metric_name = classification ? "macroF1" : "mse";
        summary.primary_metric_value =
            classification ? metrics.required("macroF1").as_double()
                           : metrics.required("mse").as_double();
        summary.runtime_milliseconds = static_cast<std::uint64_t>(
            root.required("runtimeMilliseconds").as_integer());
        const auto& hardware = root.required("hardware");
        summary.storage_class = hardware.required("storageClass").as_string();
        summary.logical_cpu_count = static_cast<unsigned int>(
            hardware.required("logicalCpus").as_integer());
        summary.gpu_memory_mib = static_cast<std::uint64_t>(
            hardware.required("gpuMemoryMiB").as_integer());
        return summary;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string experiment_compare_block_json(
    const Experiment& experiment,
    const std::optional<ExperimentResultSummary>& summary) {
    std::string block =
        "{\"id\":\"" + json_escape(experiment.id) + "\",\"name\":\"" +
        json_escape(experiment.name) + "\",\"datasetId\":\"" +
        json_escape(experiment.dataset_id) + "\",\"hyperparametersJson\":\"" +
        json_escape(experiment.hyperparameters_json) +
        "\",\"randomSeed\":" + std::to_string(experiment.random_seed) +
        ",\"sourceCodeVersion\":\"" +
        json_escape(experiment.source_code_version) +
        "\",\"configurationVersion\":\"" +
        json_escape(experiment.configuration_version) +
        "\",\"containerVersion\":\"" +
        json_escape(experiment.container_version) + "\",\"status\":\"" +
        experiment_status_name(experiment.status) + "\",\"hasResult\":" +
        (summary ? "true" : "false");
    if (summary) {
        block += ",\"primaryMetric\":\"" + summary->primary_metric_name +
                 "\",\"primaryValue\":" +
                 json_number(summary->primary_metric_value) +
                 ",\"runtimeMilliseconds\":" +
                 std::to_string(summary->runtime_milliseconds) +
                 ",\"storageClass\":\"" +
                 json_escape(summary->storage_class) +
                 "\",\"logicalCpuCount\":" +
                 std::to_string(summary->logical_cpu_count) +
                 ",\"gpuMemoryMib\":" +
                 std::to_string(summary->gpu_memory_mib);
    }
    return block + "}";
}

}  // namespace

std::string experiments_comparison_json(
    const std::vector<std::string>& experiment_ids,
    const std::function<std::optional<Experiment>(const std::string&)>&
        find_experiment,
    const std::function<std::optional<std::string>(const std::string&)>&
        find_result_json) {
    if (experiment_ids.size() < 2U) {
        throw std::invalid_argument(
            "comparing experiments requires at least two ids");
    }
    std::vector<Experiment> experiments;
    std::vector<std::optional<ExperimentResultSummary>> summaries;
    experiments.reserve(experiment_ids.size());
    summaries.reserve(experiment_ids.size());
    for (const auto& id : experiment_ids) {
        const auto experiment = find_experiment(id);
        if (!experiment) {
            throw std::invalid_argument("experiment \"" + id + "\" was not found");
        }
        experiments.push_back(*experiment);
        const auto result_json = find_result_json(id);
        summaries.push_back(result_json ? summarize_experiment_result(*result_json)
                                        : std::nullopt);
    }
    std::string blocks = "[";
    for (std::size_t index = 0; index < experiments.size(); ++index) {
        if (index != 0U) blocks += ",";
        blocks += experiment_compare_block_json(experiments[index],
                                                 summaries[index]);
    }
    blocks += "]";
    // Diffs are relative to the first id, the same "baseline" convention
    // tabular_model_comparison_json uses for two-model comparisons, widened
    // here to however many experiments were passed.
    const auto& baseline = experiments.front();
    const auto& baseline_summary = summaries.front();
    std::string diffs = "[";
    for (std::size_t index = 1U; index < experiments.size(); ++index) {
        if (index != 1U) diffs += ",";
        const auto& candidate = experiments[index];
        const auto& candidate_summary = summaries[index];
        diffs += "{\"experimentId\":\"" + json_escape(candidate.id) +
                 "\",\"datasetDiffers\":" +
                 (candidate.dataset_id != baseline.dataset_id ? "true"
                                                              : "false") +
                 ",\"hyperparametersDiffer\":" +
                 (candidate.hyperparameters_json !=
                          baseline.hyperparameters_json
                      ? "true"
                      : "false") +
                 ",\"seedDiffers\":" +
                 (candidate.random_seed != baseline.random_seed ? "true"
                                                                 : "false");
        if (baseline_summary && candidate_summary &&
            baseline_summary->primary_metric_name ==
                candidate_summary->primary_metric_name) {
            const double delta = candidate_summary->primary_metric_value -
                                 baseline_summary->primary_metric_value;
            // Matches tabular_model_comparison_json's winner convention:
            // higher macro F1 wins, lower MSE wins; a regression is the
            // candidate losing on that same primary metric.
            const bool is_f1 =
                candidate_summary->primary_metric_name == "macroF1";
            const bool regression =
                delta != 0.0 &&
                (is_f1 ? delta < 0.0 : delta > 0.0);
            diffs += ",\"primaryMetric\":\"" +
                     candidate_summary->primary_metric_name +
                     "\",\"baselineValue\":" +
                     json_number(baseline_summary->primary_metric_value) +
                     ",\"candidateValue\":" +
                     json_number(candidate_summary->primary_metric_value) +
                     ",\"metricDelta\":" + json_number(delta) +
                     ",\"regression\":" + (regression ? "true" : "false") +
                     ",\"runtimeDeltaMilliseconds\":" +
                     std::to_string(
                         candidate_summary->runtime_milliseconds >=
                                 baseline_summary->runtime_milliseconds
                             ? candidate_summary->runtime_milliseconds -
                                   baseline_summary->runtime_milliseconds
                             : 0ULL) +
                     ",\"hardwareDiffers\":" +
                     (candidate_summary->storage_class !=
                              baseline_summary->storage_class ||
                      candidate_summary->logical_cpu_count !=
                              baseline_summary->logical_cpu_count ||
                      candidate_summary->gpu_memory_mib !=
                              baseline_summary->gpu_memory_mib
                          ? "true"
                          : "false");
        } else {
            diffs += ",\"metricComparison\":\"unavailable -- one or both "
                     "experiments have not been run yet, or used "
                     "incompatible tasks\"";
        }
        // No safety-scoring executor exists for this tabular engine yet --
        // reported honestly rather than fabricated, the same convention
        // ModelComparison's class comment already sets.
        diffs += ",\"safetyChanges\":null}";
    }
    diffs += "]";
    return "{\"experiments\":" + blocks + ",\"baselineId\":\"" +
           json_escape(baseline.id) + "\",\"comparisons\":" + diffs + "}";
}

std::string tabular_prediction_json(const TabularPrediction& prediction,
                                    const TrainedTabularModel& model) {
    std::string result = "{\"task\":\"" +
                         std::string(model.classification ? "classification"
                                                          : "regression") +
                         "\",\"value\":" + json_number(prediction.value);
    if (model.classification) {
        result += ",\"label\":\"" + json_escape(prediction.label) +
                  "\",\"classes\":" + string_array_json(model.class_labels) +
                  ",\"probabilities\":" +
                  number_array_json(prediction.class_probabilities);
    }
    return result + "}";
}

}  // namespace masterai
