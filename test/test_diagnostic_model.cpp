// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include <chrono>

#include <gtest/gtest.h>

#include "rviz2_diagnostics_monitor/diagnostic_model.hpp"

using namespace std::chrono_literals;

namespace rviz2_diagnostics_monitor {
namespace {

diagnostic_msgs::msg::DiagnosticStatus status(
    const std::string &name, const std::string &hardware_id, uint8_t level,
    const std::string &message,
    std::vector<std::pair<std::string, std::string>> values = {}) {
  diagnostic_msgs::msg::DiagnosticStatus result;
  result.name = name;
  result.hardware_id = hardware_id;
  result.level = level;
  result.message = message;
  for (const auto &[key, value] : values) {
    diagnostic_msgs::msg::KeyValue kv;
    kv.key = key;
    kv.value = value;
    result.values.push_back(kv);
  }
  return result;
}

diagnostic_msgs::msg::DiagnosticArray array(
    std::initializer_list<diagnostic_msgs::msg::DiagnosticStatus> statuses) {
  diagnostic_msgs::msg::DiagnosticArray result;
  result.status.assign(statuses.begin(), statuses.end());
  return result;
}

} // namespace

TEST(DiagnosticModel, SplitsDiagnosticNamesIntoTreeParts) {
  EXPECT_EQ(DiagnosticModel::splitDiagnosticName("/Sensors/Lidar/Front"),
            (std::vector<std::string>{"Sensors", "Lidar", "Front"}));
  EXPECT_EQ(DiagnosticModel::splitDiagnosticName("Power/Battery"),
            (std::vector<std::string>{"Power", "Battery"}));
}

TEST(DiagnosticModel, ParentSeverityFollowsWorstChild) {
  DiagnosticModel model;
  const auto now = std::chrono::steady_clock::now();
  model.ingest(array({
                   status("Sensors/Lidar/Front", "lidar",
                          diagnostic_msgs::msg::DiagnosticStatus::OK, "OK"),
                   status("Sensors/Camera/Rear", "camera",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                          "Offline"),
               }),
               now);

  const auto tree = model.tree(now);
  ASSERT_EQ(tree.children.at("Sensors").severity, Severity::Error);
  EXPECT_EQ(tree.children.at("Sensors").children.at("Lidar").severity,
            Severity::Ok);
  EXPECT_EQ(tree.children.at("Sensors").children.at("Camera").severity,
            Severity::Error);
}

TEST(DiagnosticModel, CountsIncludeAllSeverities) {
  DiagnosticModel model;
  const auto now = std::chrono::steady_clock::now();
  model.ingest(array({
                   status("OK", "a", diagnostic_msgs::msg::DiagnosticStatus::OK,
                          "OK"),
                   status("WARN", "b",
                          diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warn"),
                   status("ERROR", "c",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Error"),
                   status("STALE", "d",
                          diagnostic_msgs::msg::DiagnosticStatus::STALE, "Stale"),
               }),
               now);

  const auto counts = model.counts(now);
  EXPECT_EQ(counts.ok, 1);
  EXPECT_EQ(counts.warn, 1);
  EXPECT_EQ(counts.error, 1);
  EXPECT_EQ(counts.stale, 1);
}

TEST(DiagnosticModel, SearchMatchesNameMessageHardwareAndValues) {
  DiagnosticSnapshot snapshot;
  snapshot.name = "Drive/Left Motor";
  snapshot.message = "Temperature elevated";
  snapshot.hardware_id = "motor_left";
  snapshot.values = {{"temperature", "63 C"}, {"fault_code", "0x42"}};

  EXPECT_TRUE(DiagnosticModel::matchesSearch(snapshot, "left motor"));
  EXPECT_TRUE(DiagnosticModel::matchesSearch(snapshot, "elevated"));
  EXPECT_TRUE(DiagnosticModel::matchesSearch(snapshot, "motor_left"));
  EXPECT_TRUE(DiagnosticModel::matchesSearch(snapshot, "0x42"));
  EXPECT_FALSE(DiagnosticModel::matchesSearch(snapshot, "battery"));
}

TEST(DiagnosticModel, EventFeedAppendsOnlyOnStateChanges) {
  DiagnosticModel model;
  const auto now = std::chrono::steady_clock::now();
  const auto ok =
      status("Power/Battery", "battery",
             diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal",
             {{"percent", "80"}});
  model.ingest(array({ok}), now);
  model.ingest(array({ok}), now + 1s);
  EXPECT_EQ(model.events().size(), 1u);

  auto warn = ok;
  warn.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
  warn.message = "Low";
  model.ingest(array({warn}), now + 2s);
  EXPECT_EQ(model.events().size(), 2u);

  auto value_change = warn;
  value_change.values.front().value = "20";
  model.ingest(array({value_change}), now + 3s);
  EXPECT_EQ(model.events().size(), 3u);
}

TEST(DiagnosticModel, HistoryPruningRespectsWindow) {
  DiagnosticModel model;
  DiagnosticModelConfig config;
  config.history_window = 10min;
  model.setConfig(config);
  const auto now = std::chrono::steady_clock::now();

  model.ingest(array({status("CPU", "ipc", diagnostic_msgs::msg::DiagnosticStatus::OK,
                             "OK")}),
               now);
  model.ingest(array({status("CPU", "ipc",
                             diagnostic_msgs::msg::DiagnosticStatus::WARN,
                             "High")}),
               now + 11min);

  const auto history = model.historyFor("CPU\nipc");
  ASSERT_EQ(history.size(), 1u);
  EXPECT_EQ(history.front().level, Severity::Warn);
}

TEST(DiagnosticModel, LocalStaleTimeoutConvertsMissingUpdates) {
  DiagnosticModel model;
  DiagnosticModelConfig config;
  config.stale_timeout = 1000ms;
  model.setConfig(config);
  const auto now = std::chrono::steady_clock::now();
  model.ingest(array({status("Lidar", "front",
                             diagnostic_msgs::msg::DiagnosticStatus::OK, "OK")}),
               now);

  const auto counts = model.counts(now + 1500ms);
  EXPECT_EQ(counts.ok, 0);
  EXPECT_EQ(counts.stale, 1);
  ASSERT_EQ(model.events().size(), 2u);
  EXPECT_EQ(model.events().back().snapshot.level, Severity::Stale);
}

TEST(DiagnosticModel, EventFilteringUsesSeverityHardwareAndSearch) {
  DiagnosticModel model;
  const auto now = std::chrono::steady_clock::now();
  model.ingest(array({
                   status("Power/Battery", "battery",
                          diagnostic_msgs::msg::DiagnosticStatus::OK, "OK"),
                   status("Drive/Motor", "motor",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                          "Over temperature"),
               }),
               now);

  EventFilter filter;
  filter.show_ok = false;
  filter.hardware_id = "motor";
  filter.search = "temperature";
  const auto events = model.filteredEvents(filter);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events.front().snapshot.name, "Drive/Motor");
}

} // namespace rviz2_diagnostics_monitor
