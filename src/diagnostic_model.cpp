// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include "rviz2_diagnostics_monitor/diagnostic_model.hpp"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <regex>
#include <sstream>
#include <unordered_set>

namespace rviz2_diagnostics_monitor {
namespace {

bool enabledFor(Severity severity, const EventFilter &filter) {
  switch (severity) {
  case Severity::Ok:
    return filter.show_ok;
  case Severity::Warn:
    return filter.show_warn;
  case Severity::Error:
    return filter.show_error;
  case Severity::Stale:
    return filter.show_stale;
  }
  return false;
}

} // namespace

void pruneHistory(std::vector<HistorySample> &history,
                  std::chrono::steady_clock::time_point cutoff) {
  const auto first_in_window =
      std::lower_bound(history.begin(), history.end(), cutoff,
                       [](const auto &sample, const auto &time) {
                         return sample.stamp < time;
                       });
  if (first_in_window == history.begin()) {
    return;
  }
  if (first_in_window == history.end()) {
    if (!history.empty()) {
      history.erase(history.begin(), history.end() - 1);
    }
    return;
  }
  history.erase(history.begin(), first_in_window - 1);
}

void pruneValueHistory(std::vector<ValueHistorySample> &history,
                       std::chrono::steady_clock::time_point cutoff) {
  const auto first_in_window =
      std::lower_bound(history.begin(), history.end(), cutoff,
                       [](const auto &sample, const auto &time) {
                         return sample.stamp < time;
                       });
  history.erase(history.begin(), first_in_window);
}

DiagnosticModel::DiagnosticModel() = default;

void DiagnosticModel::setConfig(const DiagnosticModelConfig &config) {
  config_ = config;
}

void DiagnosticModel::clear() {
  entries_.clear();
  events_.clear();
  overall_history_.clear();
  last_message_seen_.reset();
  next_event_sequence_ = 1;
}

void DiagnosticModel::ingest(const diagnostic_msgs::msg::DiagnosticArray &message,
                             std::chrono::steady_clock::time_point now) {
  const bool was_stream_stale = isMessageStreamStale(now);
  std::unordered_set<std::string> incoming_ids;
  incoming_ids.reserve(message.status.size());
  for (const auto &status : message.status) {
    incoming_ids.insert(idFor(status));
  }

  last_message_seen_ = now;
  if (was_stream_stale) {
    for (auto &[id, entry] : entries_) {
      entry.stale_event_emitted = false;
      if (incoming_ids.find(id) == incoming_ids.end()) {
        appendHistory(entry, entry.current.level, now);
      }
    }
  }

  for (const auto &status : message.status) {
    DiagnosticSnapshot snapshot;
    snapshot.id = idFor(status);
    snapshot.name = status.name;
    snapshot.hardware_id = status.hardware_id;
    snapshot.level = normalizeLevel(status.level);
    snapshot.message = status.message;
    snapshot.last_seen = now;
    snapshot.locally_stale = false;
    snapshot.values.reserve(status.values.size());
    for (const auto &value : status.values) {
      snapshot.values.push_back({value.key, value.value});
    }

    auto &entry = entries_[snapshot.id];
    const auto signature = signatureFor(snapshot);
    const bool changed = entry.state_signature.empty() ||
                         entry.state_signature != signature ||
                         entry.stale_event_emitted;
    entry.current = snapshot;
    entry.state_signature = signature;
    entry.stale_event_emitted = snapshot.level == Severity::Stale;
    appendHistory(entry, snapshot.level, now);
    appendValueHistory(entry, snapshot, now);
    if (changed) {
      appendEvent(snapshot, now);
    }
  }
  appendOverallHistory(now);
  prune(now);
}

void DiagnosticModel::applyStaleTimeout(
    std::chrono::steady_clock::time_point now) {
  const bool stream_stale = isMessageStreamStale(now);
  for (auto &[_, entry] : entries_) {
    if (!stream_stale || entry.current.level == Severity::Stale) {
      entry.stale_event_emitted = entry.current.level == Severity::Stale;
      continue;
    }

    appendHistory(entry, Severity::Stale, now);
    if (!entry.stale_event_emitted) {
      appendEvent(effectiveSnapshot(entry, now), now);
      entry.stale_event_emitted = true;
    }
  }
  appendOverallHistory(now);
  prune(now);
}

std::vector<DiagnosticSnapshot> DiagnosticModel::snapshots(
    std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  std::vector<DiagnosticSnapshot> result;
  result.reserve(entries_.size());
  for (const auto &[_, entry] : entries_) {
    result.push_back(effectiveSnapshot(entry, now));
  }
  std::sort(result.begin(), result.end(),
            [](const auto &left, const auto &right) {
              if (left.level != right.level) {
                return static_cast<int>(left.level) >
                       static_cast<int>(right.level);
              }
              return left.name < right.name;
            });
  return result;
}

std::optional<DiagnosticSnapshot> DiagnosticModel::snapshot(
    const std::string &id, std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return std::nullopt;
  }
  return effectiveSnapshot(it->second, now);
}

SummaryCounts DiagnosticModel::counts(std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  SummaryCounts counts;
  for (const auto &[_, entry] : entries_) {
    switch (effectiveSnapshot(entry, now).level) {
    case Severity::Ok:
      ++counts.ok;
      break;
    case Severity::Warn:
      ++counts.warn;
      break;
    case Severity::Error:
      ++counts.error;
      break;
    case Severity::Stale:
      ++counts.stale;
      break;
    }
  }
  return counts;
}

Severity DiagnosticModel::overallSeverity(
    std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  Severity severity = Severity::Ok;
  for (const auto &[_, entry] : entries_) {
    severity = worst(severity, effectiveSnapshot(entry, now).level);
  }
  return severity;
}

TreeNode DiagnosticModel::tree(std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  TreeNode root;
  root.label = "All Devices";
  root.severity = Severity::Ok;

  for (const auto &[id, entry] : entries_) {
    auto *node = &root;
    const auto snapshot = effectiveSnapshot(entry, now);
    node->severity = worst(node->severity, snapshot.level);
    for (const auto &part : splitDiagnosticName(snapshot.name)) {
      auto [child, _] = node->children.emplace(part, TreeNode{});
      child->second.label = part;
      child->second.severity = worst(child->second.severity,
                                     snapshot.level);
      node = &child->second;
    }
    node->diagnostic_id = id;
    node->hardware_id = snapshot.hardware_id;
    node->severity = worst(node->severity, snapshot.level);
  }
  return root;
}

std::vector<DiagnosticEvent>
DiagnosticModel::filteredEvents(const EventFilter &filter) const {
  std::vector<DiagnosticEvent> result;
  for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
    if (!filter.diagnostic_id.empty() &&
        it->snapshot.id != filter.diagnostic_id) {
      continue;
    }
    if (!enabledFor(it->snapshot.level, filter)) {
      continue;
    }
    if (!filter.hardware_id.empty() &&
        lower(it->snapshot.hardware_id).find(lower(filter.hardware_id)) ==
            std::string::npos) {
      continue;
    }
    if (!matchesSearch(it->snapshot, filter.search)) {
      continue;
    }
    result.push_back(*it);
  }
  return result;
}

std::vector<DiagnosticEvent>
DiagnosticModel::eventsForDiagnostic(const std::string &id) const {
  EventFilter filter;
  filter.diagnostic_id = id;
  return filteredEvents(filter);
}

std::vector<HistorySample> DiagnosticModel::historyFor(
    const std::string &id) const {
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return {};
  }
  return it->second.history;
}

std::vector<ValueHistorySample> DiagnosticModel::valueHistoryFor(
    const std::string &id, const std::string &key) const {
  const auto entry_it = entries_.find(id);
  if (entry_it == entries_.end()) {
    return {};
  }
  const auto history_it = entry_it->second.value_histories.find(key);
  if (history_it == entry_it->second.value_histories.end()) {
    return {};
  }
  return history_it->second;
}

std::optional<double>
DiagnosticModel::parseNumericValue(const std::string &value) {
  static const std::regex numeric_prefix(
      R"(^\s*[+-]?(?:(?:\d+\.?\d*)|(?:\.\d+))(?:[eE][+-]?\d+)?)");
  std::smatch match;
  if (!std::regex_search(value, match, numeric_prefix)) {
    return std::nullopt;
  }

  const auto text = match.str();
  const auto next = value.begin() + static_cast<std::ptrdiff_t>(match.length());
  const auto trimmed_start = std::find_if_not(
      value.begin(), value.end(), [](unsigned char c) { return std::isspace(c); });
  if (next != value.end() && (*next == 'x' || *next == 'X') &&
      trimmed_start != value.end() && *trimmed_start == '0') {
    return std::nullopt;
  }

  char *end = nullptr;
  const double parsed = std::strtod(text.c_str(), &end);
  if (end == text.c_str()) {
    return std::nullopt;
  }
  return parsed;
}

std::vector<std::string>
DiagnosticModel::splitDiagnosticName(const std::string &name) {
  std::vector<std::string> parts;
  std::stringstream stream(name);
  std::string part;
  while (std::getline(stream, part, '/')) {
    if (!part.empty()) {
      parts.push_back(part);
    }
  }
  if (parts.empty() && !name.empty()) {
    parts.push_back(name);
  }
  return parts;
}

bool DiagnosticModel::matchesSearch(const DiagnosticSnapshot &snapshot,
                                    const std::string &search) {
  const auto needle = lower(search);
  if (needle.empty()) {
    return true;
  }
  std::string haystack = snapshot.name + " " + snapshot.message + " " +
                         snapshot.hardware_id + " " +
                         severityLabel(snapshot.level);
  for (const auto &value : snapshot.values) {
    haystack += " " + value.key + " " + value.value;
  }
  return lower(haystack).find(needle) != std::string::npos;
}

std::string DiagnosticModel::severityLabel(Severity severity) {
  switch (severity) {
  case Severity::Ok:
    return "OK";
  case Severity::Warn:
    return "WARN";
  case Severity::Error:
    return "ERROR";
  case Severity::Stale:
    return "STALE";
  }
  return "STALE";
}

Severity DiagnosticModel::worst(Severity left, Severity right) {
  return static_cast<int>(left) >= static_cast<int>(right) ? left : right;
}

std::string
DiagnosticModel::idFor(const diagnostic_msgs::msg::DiagnosticStatus &status) {
  return status.name + "\n" + status.hardware_id;
}

Severity DiagnosticModel::normalizeLevel(uint8_t level) {
  if (level == diagnostic_msgs::msg::DiagnosticStatus::OK) {
    return Severity::Ok;
  }
  if (level == diagnostic_msgs::msg::DiagnosticStatus::WARN) {
    return Severity::Warn;
  }
  if (level == diagnostic_msgs::msg::DiagnosticStatus::ERROR) {
    return Severity::Error;
  }
  return Severity::Stale;
}

std::string DiagnosticModel::signatureFor(const DiagnosticSnapshot &snapshot) {
  std::string signature = std::to_string(static_cast<int>(snapshot.level)) +
                          "|" + snapshot.message;
  for (const auto &value : snapshot.values) {
    signature += "|" + value.key + "=" + value.value;
  }
  return signature;
}

std::string DiagnosticModel::lower(std::string text) {
  std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return text;
}

void DiagnosticModel::appendEvent(const DiagnosticSnapshot &snapshot,
                                  std::chrono::steady_clock::time_point now) {
  events_.push_back({next_event_sequence_++, now, snapshot});
}

void DiagnosticModel::appendHistory(Entry &entry, Severity level,
                                    std::chrono::steady_clock::time_point now) {
  if (!entry.history.empty() && entry.history.back().level == level) {
    return;
  }
  entry.history.push_back({now, level});
}

void DiagnosticModel::appendValueHistory(
    Entry &entry, const DiagnosticSnapshot &snapshot,
    std::chrono::steady_clock::time_point now) {
  for (const auto &diagnostic_value : snapshot.values) {
    const auto parsed = parseNumericValue(diagnostic_value.value);
    if (!parsed) {
      continue;
    }
    entry.value_histories[diagnostic_value.key].push_back(
        {now, *parsed, snapshot.level, diagnostic_value.value});
  }
}

void DiagnosticModel::appendOverallHistory(
    std::chrono::steady_clock::time_point now) {
  Severity severity = Severity::Ok;
  for (const auto &[_, entry] : entries_) {
    severity = worst(severity, effectiveSnapshot(entry, now).level);
  }
  if (!overall_history_.empty() && overall_history_.back().level == severity) {
    return;
  }
  overall_history_.push_back({now, severity});
}

void DiagnosticModel::prune(std::chrono::steady_clock::time_point now) {
  const auto cutoff = now - config_.history_window;
  for (auto &[_, entry] : entries_) {
    pruneHistory(entry.history, cutoff);
    for (auto history_it = entry.value_histories.begin();
         history_it != entry.value_histories.end();) {
      pruneValueHistory(history_it->second, cutoff);
      if (history_it->second.empty()) {
        history_it = entry.value_histories.erase(history_it);
      } else {
        ++history_it;
      }
    }
  }
  pruneHistory(overall_history_, cutoff);
  const auto first_event_in_window =
      std::lower_bound(events_.begin(), events_.end(), cutoff,
                       [](const auto &event, const auto &time) {
                         return event.stamp < time;
                       });
  events_.erase(events_.begin(), first_event_in_window);
}

bool DiagnosticModel::isMessageStreamStale(
    std::chrono::steady_clock::time_point now) const {
  return config_.stale_timeout > std::chrono::milliseconds::zero() &&
         last_message_seen_.has_value() &&
         now - *last_message_seen_ > config_.stale_timeout;
}

DiagnosticSnapshot DiagnosticModel::effectiveSnapshot(
    const Entry &entry, std::chrono::steady_clock::time_point now) const {
  auto snapshot = entry.current;
  if (snapshot.level != Severity::Stale && isMessageStreamStale(now)) {
    snapshot.level = Severity::Stale;
    snapshot.locally_stale = true;
    snapshot.message = "No fresh diagnostic data";
  }
  return snapshot;
}

} // namespace rviz2_diagnostics_monitor
