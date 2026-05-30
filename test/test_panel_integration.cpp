// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>

#include <QApplication>
#include <QLineEdit>
#include <QTreeWidget>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <gtest/gtest.h>
#include <rclcpp/rclcpp.hpp>

#include "rviz2_diagnostics_monitor/diagnostics_monitor_panel.hpp"

using namespace std::chrono_literals;

namespace {

diagnostic_msgs::msg::DiagnosticArray makeMessage(uint8_t level,
                                                  const std::string &message) {
  diagnostic_msgs::msg::DiagnosticArray array;
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = "Sensors/Lidar/Front";
  status.hardware_id = "lidar_front";
  status.level = level;
  status.message = message;
  diagnostic_msgs::msg::KeyValue value;
  value.key = "scan_rate";
  value.value = "10 Hz";
  status.values.push_back(value);
  array.status.push_back(status);
  return array;
}

} // namespace

TEST(PanelIntegration, ReceivesDiagnosticsAndTracksStateChanges) {
  auto panel_node =
      std::make_shared<rclcpp::Node>("diagnostics_monitor_panel_test");
  auto publisher_node =
      std::make_shared<rclcpp::Node>("diagnostics_monitor_publisher_test");
  auto publisher =
      publisher_node->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
          "/diagnostics_monitor_test", 10);

  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  panel.setTopicForTest("/diagnostics_monitor_test");
  panel.setStaleTimeoutForTest(300ms);
  panel.initializeForTest(panel_node);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(panel_node);
  executor.add_node(publisher_node);

  publisher->publish(makeMessage(diagnostic_msgs::msg::DiagnosticStatus::OK,
                                 "Nominal"));

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(20ms);
    QApplication::processEvents();
    if (panel.currentCountsForTest().ok == 1) {
      break;
    }
  }

  auto counts = panel.currentCountsForTest();
  EXPECT_EQ(counts.ok, 1);
  EXPECT_EQ(counts.error, 0);
  ASSERT_EQ(panel.snapshotsForTest().size(), 1u);
  EXPECT_EQ(panel.snapshotsForTest().front().name, "Sensors/Lidar/Front");
  EXPECT_EQ(panel.eventsForTest().size(), 1u);

  publisher->publish(makeMessage(diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                                 "Packet loss"));
  const auto error_deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < error_deadline) {
    executor.spin_some(20ms);
    QApplication::processEvents();
    if (panel.currentCountsForTest().error == 1) {
      break;
    }
  }

  counts = panel.currentCountsForTest();
  EXPECT_EQ(counts.error, 1);
  EXPECT_GE(panel.eventsForTest().size(), 2u);

  const auto stale_deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < stale_deadline) {
    executor.spin_some(20ms);
    QApplication::processEvents();
    if (panel.currentCountsForTest().stale == 1) {
      break;
    }
  }
  EXPECT_EQ(panel.currentCountsForTest().stale, 1);
}

TEST(PanelIntegration, RefreshKeepsCollapsedTreeItemsCollapsed) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  panel.refreshForTest();

  QTreeWidgetItem *all_devices = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == "All Devices") {
      all_devices = tree->topLevelItem(i);
      break;
    }
  }
  ASSERT_NE(all_devices, nullptr);
  ASSERT_TRUE(all_devices->isExpanded());

  all_devices->setExpanded(false);
  panel.refreshForTest();

  QTreeWidgetItem *refreshed_all_devices = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == "All Devices") {
      refreshed_all_devices = tree->topLevelItem(i);
      break;
    }
  }
  ASSERT_NE(refreshed_all_devices, nullptr);
  EXPECT_FALSE(refreshed_all_devices->isExpanded());
}

TEST(PanelIntegration, OverviewFilterAppliesToAllDevicesTree) {
  auto panel_node =
      std::make_shared<rclcpp::Node>("diagnostics_monitor_filter_panel_test");
  auto publisher_node =
      std::make_shared<rclcpp::Node>("diagnostics_monitor_filter_publisher_test");
  auto publisher =
      publisher_node->create_publisher<diagnostic_msgs::msg::DiagnosticArray>(
          "/diagnostics_monitor_filter_test", 10);

  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  panel.setTopicForTest("/diagnostics_monitor_filter_test");
  panel.initializeForTest(panel_node);

  rclcpp::executors::SingleThreadedExecutor executor;
  executor.add_node(panel_node);
  executor.add_node(publisher_node);

  diagnostic_msgs::msg::DiagnosticArray message;
  auto lidar = makeMessage(diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal")
                   .status.front();
  diagnostic_msgs::msg::DiagnosticStatus battery;
  battery.name = "Power/Battery";
  battery.hardware_id = "battery_pack";
  battery.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
  battery.message = "Battery low";
  message.status = {lidar, battery};
  publisher->publish(message);

  const auto deadline = std::chrono::steady_clock::now() + 2s;
  while (std::chrono::steady_clock::now() < deadline) {
    executor.spin_some(20ms);
    QApplication::processEvents();
    if (panel.currentCountsForTest().total() == 2) {
      break;
    }
  }
  ASSERT_EQ(panel.currentCountsForTest().total(), 2);

  panel.overviewSearchForTest()->setText("battery");
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  const auto battery_items =
      tree->findItems("Battery", Qt::MatchExactly | Qt::MatchRecursive, 0);
  const auto lidar_items =
      tree->findItems("Lidar", Qt::MatchExactly | Qt::MatchRecursive, 0);
  EXPECT_FALSE(battery_items.empty());
  EXPECT_TRUE(lidar_items.empty());
}

int main(int argc, char **argv) {
  setenv("QT_QPA_PLATFORM", "offscreen", 0);
  setenv("ROS_LOG_DIR", "/tmp/rviz2_diagnostics_monitor_test_logs", 0);
  rclcpp::init(argc, argv);
  testing::InitGoogleTest(&argc, argv);
  QApplication app(argc, argv);
  const int result = RUN_ALL_TESTS();
  rclcpp::shutdown();
  return result;
}
