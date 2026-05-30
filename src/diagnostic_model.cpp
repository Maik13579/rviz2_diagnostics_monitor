// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include "rviz2_diagnostics_monitor/diagnostic_model.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

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

DiagnosticModel::DiagnosticModel() = default;

void DiagnosticModel::setConfig(const DiagnosticModelConfig &config) {
  config_ = config;
  if (config_.max_event_rows == 0) {
    config_.max_event_rows = 1;
  }
}

void DiagnosticModel::clear() {
  entries_.clear();
  events_.clear();
  overall_history_.clear();
}

void DiagnosticModel::ingest(const diagnostic_msgs::msg::DiagnosticArray &message,
                             std::chrono::steady_clock::time_point now) {
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
                         entry.current.locally_stale;
    entry.current = snapshot;
    entry.state_signature = signature;
    entry.stale_event_emitted = snapshot.level == Severity::Stale;
    appendHistory(entry, snapshot.level, now);
    if (changed) {
      appendEvent(snapshot, now);
    }
  }
  appendOverallHistory(now);
  prune(now);
}

void DiagnosticModel::applyStaleTimeout(
    std::chrono::steady_clock::time_point now) {
  for (auto &[_, entry] : entries_) {
    if (entry.current.level == Severity::Stale) {
      continue;
    }
    if (now - entry.current.last_seen <= config_.stale_timeout) {
      continue;
    }

    entry.current.level = Severity::Stale;
    entry.current.locally_stale = true;
    entry.current.message = "No fresh diagnostic data";
    entry.state_signature = signatureFor(entry.current);
    appendHistory(entry, Severity::Stale, now);
    if (!entry.stale_event_emitted) {
      appendEvent(entry.current, now);
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
    result.push_back(entry.current);
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
  return it->second.current;
}

SummaryCounts DiagnosticModel::counts(std::chrono::steady_clock::time_point now) {
  applyStaleTimeout(now);
  SummaryCounts counts;
  for (const auto &[_, entry] : entries_) {
    switch (entry.current.level) {
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
    severity = worst(severity, entry.current.level);
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
    node->severity = worst(node->severity, entry.current.level);
    for (const auto &part : splitDiagnosticName(entry.current.name)) {
      auto [child, _] = node->children.emplace(part, TreeNode{});
      child->second.label = part;
      child->second.severity = worst(child->second.severity,
                                     entry.current.level);
      node = &child->second;
    }
    node->diagnostic_id = id;
    node->severity = worst(node->severity, entry.current.level);
  }
  return root;
}

std::vector<DiagnosticEvent>
DiagnosticModel::filteredEvents(const EventFilter &filter) const {
  std::vector<DiagnosticEvent> result;
  for (auto it = events_.rbegin(); it != events_.rend(); ++it) {
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

std::vector<HistorySample> DiagnosticModel::historyFor(
    const std::string &id) const {
  const auto it = entries_.find(id);
  if (it == entries_.end()) {
    return {};
  }
  return it->second.history;
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
  events_.push_back({now, snapshot});
}

void DiagnosticModel::appendHistory(Entry &entry, Severity level,
                                    std::chrono::steady_clock::time_point now) {
  if (!entry.history.empty() && entry.history.back().level == level) {
    entry.history.back().stamp = now;
    return;
  }
  entry.history.push_back({now, level});
}

void DiagnosticModel::appendOverallHistory(
    std::chrono::steady_clock::time_point now) {
  Severity severity = Severity::Ok;
  for (const auto &[_, entry] : entries_) {
    severity = worst(severity, entry.current.level);
  }
  if (!overall_history_.empty() && overall_history_.back().level == severity) {
    overall_history_.back().stamp = now;
    return;
  }
  overall_history_.push_back({now, severity});
}

void DiagnosticModel::prune(std::chrono::steady_clock::time_point now) {
  const auto cutoff = now - config_.history_window;
  for (auto &[_, entry] : entries_) {
    entry.history.erase(
        std::remove_if(entry.history.begin(), entry.history.end(),
                       [cutoff](const auto &sample) {
                         return sample.stamp < cutoff;
                       }),
        entry.history.end());
  }
  overall_history_.erase(
      std::remove_if(overall_history_.begin(), overall_history_.end(),
                     [cutoff](const auto &sample) {
                       return sample.stamp < cutoff;
                     }),
      overall_history_.end());
  if (events_.size() > config_.max_event_rows) {
    events_.erase(events_.begin(),
                  events_.begin() +
                      static_cast<std::ptrdiff_t>(events_.size() -
                                                  config_.max_event_rows));
  }
}

} // namespace rviz2_diagnostics_monitor
