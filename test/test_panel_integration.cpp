// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>

#include <QApplication>
#include <QLineEdit>
#include <QScrollBar>
#include <QTableWidget>
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

TEST(PanelIntegration, RefreshKeepsTreeScrollPosition) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  for (int i = 0; i < 80; ++i) {
    diagnostic_msgs::msg::DiagnosticStatus status;
    status.name = "Synthetic/Device " + std::to_string(i);
    status.hardware_id = "synthetic_" + std::to_string(i);
    status.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
    status.message = "Nominal";
    message.status.push_back(status);
  }
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  tree->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  tree->expandAll();
  QApplication::processEvents();

  const auto max_scroll = tree->verticalScrollBar()->maximum();
  ASSERT_GT(max_scroll, 0);
  const auto expected_scroll = std::max(1, max_scroll / 2);
  tree->verticalScrollBar()->setValue(expected_scroll);

  panel.refreshForTest();

  EXPECT_EQ(tree->verticalScrollBar()->value(), expected_scroll);
}

TEST(PanelIntegration, SelectingGroupShowsMemberDiagnostics) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  auto front_lidar =
      makeMessage(diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal")
          .status.front();
  diagnostic_msgs::msg::DiagnosticStatus rear_lidar;
  rear_lidar.name = "Sensors/Lidar/Rear";
  rear_lidar.hardware_id = "lidar_rear";
  rear_lidar.level = diagnostic_msgs::msg::DiagnosticStatus::WARN;
  rear_lidar.message = "Scan jitter";
  diagnostic_msgs::msg::DiagnosticStatus battery;
  battery.name = "Power/Battery";
  battery.hardware_id = "battery";
  battery.level = diagnostic_msgs::msg::DiagnosticStatus::OK;
  battery.message = "Nominal";
  message.status = {front_lidar, rear_lidar, battery};

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  const auto lidar_items =
      tree->findItems("Lidar", Qt::MatchExactly | Qt::MatchRecursive, 0);
  ASSERT_FALSE(lidar_items.empty());
  tree->setCurrentItem(lidar_items.front());
  QApplication::processEvents();

  auto *details = panel.detailValuesForTest();
  ASSERT_NE(details, nullptr);
  EXPECT_EQ(details->rowCount(), 2);

  QStringList names;
  for (int row = 0; row < details->rowCount(); ++row) {
    ASSERT_NE(details->item(row, 1), nullptr);
    names.push_back(details->item(row, 1)->text());
  }
  EXPECT_TRUE(names.contains("Sensors/Lidar/Front"));
  EXPECT_TRUE(names.contains("Sensors/Lidar/Rear"));
  EXPECT_FALSE(names.contains("Power/Battery"));
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
