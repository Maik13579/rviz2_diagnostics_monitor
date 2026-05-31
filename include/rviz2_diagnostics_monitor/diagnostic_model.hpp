// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <diagnostic_msgs/msg/diagnostic_array.hpp>

namespace rviz2_diagnostics_monitor {

enum class Severity : uint8_t {
  Ok = diagnostic_msgs::msg::DiagnosticStatus::OK,
  Warn = diagnostic_msgs::msg::DiagnosticStatus::WARN,
  Error = diagnostic_msgs::msg::DiagnosticStatus::ERROR,
  Stale = diagnostic_msgs::msg::DiagnosticStatus::STALE,
};

struct DiagnosticValue {
  std::string key;
  std::string value;
};

struct DiagnosticSnapshot {
  std::string id;
  std::string name;
  std::string hardware_id;
  Severity level{Severity::Stale};
  std::string message;
  std::vector<DiagnosticValue> values;
  std::chrono::steady_clock::time_point last_seen{};
  bool locally_stale{false};
};

struct HistorySample {
  std::chrono::steady_clock::time_point stamp{};
  Severity level{Severity::Stale};
};

struct DiagnosticEvent {
  std::chrono::steady_clock::time_point stamp{};
  DiagnosticSnapshot snapshot;
};

struct SummaryCounts {
  int ok{0};
  int warn{0};
  int error{0};
  int stale{0};

  int total() const { return ok + warn + error + stale; }
};

struct TreeNode {
  std::string label;
  std::string diagnostic_id;
  Severity severity{Severity::Ok};
  std::map<std::string, TreeNode> children;
};

struct DiagnosticModelConfig {
  std::chrono::milliseconds stale_timeout{3000};
  std::chrono::milliseconds history_window{std::chrono::minutes(10)};
  std::size_t max_event_rows{5000};
};

struct EventFilter {
  bool show_ok{true};
  bool show_warn{true};
  bool show_error{true};
  bool show_stale{true};
  std::string hardware_id;
  std::string search;
};

class DiagnosticModel {
public:
  DiagnosticModel();

  void setConfig(const DiagnosticModelConfig &config);
  const DiagnosticModelConfig &config() const { return config_; }

  void ingest(const diagnostic_msgs::msg::DiagnosticArray &message,
              std::chrono::steady_clock::time_point now);
  void applyStaleTimeout(std::chrono::steady_clock::time_point now);
  void clear();

  std::vector<DiagnosticSnapshot> snapshots(
      std::chrono::steady_clock::time_point now);
  std::optional<DiagnosticSnapshot> snapshot(
      const std::string &id, std::chrono::steady_clock::time_point now);
  SummaryCounts counts(std::chrono::steady_clock::time_point now);
  Severity overallSeverity(std::chrono::steady_clock::time_point now);
  TreeNode tree(std::chrono::steady_clock::time_point now);

  const std::vector<DiagnosticEvent> &events() const { return events_; }
  std::vector<DiagnosticEvent> filteredEvents(const EventFilter &filter) const;

  std::vector<HistorySample> historyFor(const std::string &id) const;
  const std::vector<HistorySample> &overallHistory() const {
    return overall_history_;
  }

  static std::vector<std::string> splitDiagnosticName(const std::string &name);
  static bool matchesSearch(const DiagnosticSnapshot &snapshot,
                            const std::string &search);
  static std::string severityLabel(Severity severity);
  static Severity worst(Severity left, Severity right);

private:
  struct Entry {
    DiagnosticSnapshot current;
    std::string state_signature;
    std::vector<HistorySample> history;
    bool stale_event_emitted{false};
  };

  static std::string idFor(const diagnostic_msgs::msg::DiagnosticStatus &status);
  static Severity normalizeLevel(uint8_t level);
  static std::string signatureFor(const DiagnosticSnapshot &snapshot);
  static std::string lower(std::string text);

  bool isMessageStreamStale(std::chrono::steady_clock::time_point now) const;
  DiagnosticSnapshot effectiveSnapshot(
      const Entry &entry, std::chrono::steady_clock::time_point now) const;
  void appendEvent(const DiagnosticSnapshot &snapshot,
                   std::chrono::steady_clock::time_point now);
  void appendHistory(Entry &entry, Severity level,
                     std::chrono::steady_clock::time_point now);
  void appendOverallHistory(std::chrono::steady_clock::time_point now);
  void prune(std::chrono::steady_clock::time_point now);

  DiagnosticModelConfig config_;
  std::unordered_map<std::string, Entry> entries_;
  std::vector<DiagnosticEvent> events_;
  std::vector<HistorySample> overall_history_;
  std::optional<std::chrono::steady_clock::time_point> last_message_seen_;
};

} // namespace rviz2_diagnostics_monitor
