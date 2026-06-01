// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include <chrono>
#include <cstdlib>

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QLayout>
#include <QMetaObject>
#include <QPushButton>
#include <QScrollBar>
#include <QTabWidget>
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

diagnostic_msgs::msg::DiagnosticStatus makeStatus(
    const std::string &name, const std::string &hardware_id, uint8_t level,
    const std::string &message) {
  diagnostic_msgs::msg::DiagnosticStatus status;
  status.name = name;
  status.hardware_id = hardware_id;
  status.level = level;
  status.message = message;
  return status;
}

QTreeWidgetItem *topLevelItem(QTreeWidget *tree, const QString &text) {
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    auto *item = tree->topLevelItem(i);
    if (item->text(0) == text) {
      return item;
    }
  }
  return nullptr;
}

int childCountWithName(const QTreeWidgetItem *parent, const QString &name) {
  int count = 0;
  for (int i = 0; i < parent->childCount(); ++i) {
    if (parent->child(i)->text(0) == name) {
      ++count;
    }
  }
  return count;
}

QString eventLabelText(QListWidget *list, int row, const QString &object_name) {
  auto *item = list->item(row);
  if (item == nullptr) {
    return {};
  }
  auto *widget = list->itemWidget(item);
  if (widget == nullptr) {
    if (object_name == "event_severity") {
      return item->data(Qt::UserRole + 10).toString();
    }
    if (object_name == "event_age") {
      return item->data(Qt::UserRole + 11).toString();
    }
    if (object_name == "event_name") {
      return item->data(Qt::UserRole + 12).toString();
    }
    if (object_name == "event_hardware") {
      return item->data(Qt::UserRole + 13).toString();
    }
    if (object_name == "event_message") {
      return item->data(Qt::UserRole + 14).toString();
    }
    if (object_name == "event_values") {
      return item->data(Qt::UserRole + 15).toString();
    }
    return {};
  }
  auto *label = widget->findChild<QLabel *>(object_name);
  return label == nullptr ? QString{} : label->text();
}

QLineEdit *lineEditWithPlaceholder(QWidget *root, const QString &placeholder) {
  const auto edits = root->findChildren<QLineEdit *>();
  for (auto *edit : edits) {
    if (edit->placeholderText() == placeholder) {
      return edit;
    }
  }
  return nullptr;
}

QCheckBox *checkBoxWithText(QWidget *root, const QString &text) {
  const auto checks = root->findChildren<QCheckBox *>();
  for (auto *check : checks) {
    if (check->text() == text) {
      return check;
    }
  }
  return nullptr;
}

void addValue(diagnostic_msgs::msg::DiagnosticStatus &status,
              const std::string &key, const std::string &value) {
  diagnostic_msgs::msg::KeyValue diagnostic_value;
  diagnostic_value.key = key;
  diagnostic_value.value = value;
  status.values.push_back(diagnostic_value);
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

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor/Left", "left_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal"),
  };
  panel.ingestForTest(message);

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  panel.refreshForTest();

  QTreeWidgetItem *drive = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == "Drive") {
      drive = tree->topLevelItem(i);
      break;
    }
  }
  ASSERT_NE(drive, nullptr);
  ASSERT_TRUE(drive->isExpanded());

  drive->setExpanded(false);
  panel.refreshForTest();

  QTreeWidgetItem *refreshed_drive = nullptr;
  for (int i = 0; i < tree->topLevelItemCount(); ++i) {
    if (tree->topLevelItem(i)->text(0) == "Drive") {
      refreshed_drive = tree->topLevelItem(i);
      break;
    }
  }
  ASSERT_NE(refreshed_drive, nullptr);
  EXPECT_FALSE(refreshed_drive->isExpanded());
}

TEST(PanelIntegration, OverviewUsesInnerTabsAndCompactHeaders) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  panel.refreshForTest();

  auto *tabs = panel.overviewTabsForTest();
  ASSERT_NE(tabs, nullptr);
  ASSERT_EQ(tabs->count(), 4);
  EXPECT_EQ(tabs->tabText(0), "All");
  EXPECT_EQ(tabs->tabText(1), "Errors");
  EXPECT_EQ(tabs->tabText(2), "Warnings");
  EXPECT_EQ(tabs->tabText(3), "Stale");

  auto *tree = panel.overviewTreeForTest("All");
  ASSERT_NE(tree, nullptr);
  ASSERT_EQ(tree->columnCount(), 2);
  EXPECT_EQ(tree->headerItem()->text(0), "Device");
  EXPECT_EQ(tree->headerItem()->text(1), "Hardware ID");
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

TEST(PanelIntegration, RefreshKeepsOverviewSelectionWhenContentIsUnchanged) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor/Left", "left_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"),
  };
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest("All");
  ASSERT_NE(tree, nullptr);
  const auto left_items =
      tree->findItems("Left", Qt::MatchExactly | Qt::MatchRecursive, 0);
  ASSERT_EQ(left_items.size(), 1);
  tree->setCurrentItem(left_items.front());
  ASSERT_EQ(tree->currentItem(), left_items.front());

  panel.refreshForTest();

  ASSERT_NE(tree->currentItem(), nullptr);
  EXPECT_EQ(tree->currentItem()->text(0), "Left");
}

TEST(PanelIntegration, SeverityTabsPreserveDiagnosticHierarchy) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor/Left", "left_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
      makeStatus("Drive/Motor/Right", "right_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"),
      makeStatus("Power/Battery", "battery",
                 diagnostic_msgs::msg::DiagnosticStatus::STALE, "No data"),
  };

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *errors = panel.overviewTreeForTest("Errors");
  ASSERT_NE(errors, nullptr);
  EXPECT_FALSE(
      errors->findItems("Drive", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      errors->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      errors->findItems("Left", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_TRUE(
      errors->findItems("Right", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());

  auto *warnings = panel.overviewTreeForTest("Warnings");
  ASSERT_NE(warnings, nullptr);
  EXPECT_FALSE(
      warnings->findItems("Drive", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      warnings->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      warnings->findItems("Right", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_TRUE(
      warnings->findItems("Left", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());

  auto *stale = panel.overviewTreeForTest("Stale");
  ASSERT_NE(stale, nullptr);
  EXPECT_FALSE(
      stale->findItems("Power", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      stale->findItems("Battery", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
}

TEST(PanelIntegration, OverviewAggregatesSameNameDiagnostics) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor", "",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
      makeStatus("Drive/Motor", "motor_controller",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
  };

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest("Errors");
  ASSERT_NE(tree, nullptr);
  auto *drive = topLevelItem(tree, "Drive");
  ASSERT_NE(drive, nullptr);
  EXPECT_EQ(drive->text(0), "Drive");
  EXPECT_EQ(childCountWithName(drive, "Motor"), 1);

  const auto motor_items =
      tree->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0);
  EXPECT_EQ(motor_items.size(), 1);
  EXPECT_EQ(motor_items.front()->text(1), "motor_controller");
}

TEST(PanelIntegration, OverviewSameNameSeverityUsesWorstLevel) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor", "motor_controller",
                 diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal"),
      makeStatus("Drive/Motor", "",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Over current"),
  };

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest();
  ASSERT_NE(tree, nullptr);
  const auto motor_items =
      tree->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0);
  ASSERT_EQ(motor_items.size(), 1);

  EXPECT_EQ(motor_items.front()->foreground(0).color(),
            rviz2_diagnostics_monitor::DiagnosticsMonitorPanel::colorFor(
                rviz2_diagnostics_monitor::Severity::Error));
  EXPECT_EQ(motor_items.front()->foreground(1).color(),
            rviz2_diagnostics_monitor::DiagnosticsMonitorPanel::colorFor(
                rviz2_diagnostics_monitor::Severity::Error));
}

TEST(PanelIntegration, OverviewStaleDuplicateUsesWorstCurrentSeverity) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor", "",
                 diagnostic_msgs::msg::DiagnosticStatus::STALE, "No data"),
      makeStatus("Drive/Motor", "motor_controller",
                 diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal"),
  };

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest("Stale");
  ASSERT_NE(tree, nullptr);
  auto *drive = topLevelItem(tree, "Drive");
  ASSERT_NE(drive, nullptr);
  EXPECT_EQ(childCountWithName(drive, "Motor"), 1);

  const auto motor_items =
      tree->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0);
  ASSERT_EQ(motor_items.size(), 1);
  EXPECT_EQ(motor_items.front()->foreground(0).color(),
            rviz2_diagnostics_monitor::DiagnosticsMonitorPanel::colorFor(
                rviz2_diagnostics_monitor::Severity::Stale));
  EXPECT_EQ(motor_items.front()->text(1), "motor_controller");
}

TEST(PanelIntegration, SearchFilteringAppliesAcrossOverviewTrees) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
      makeStatus("Power/Battery", "battery",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Low"),
  };

  panel.ingestForTest(message);
  panel.refreshForTest();
  panel.overviewSearchForTest()->setText("battery");
  panel.refreshForTest();

  auto *all = panel.overviewTreeForTest("All");
  auto *warnings = panel.overviewTreeForTest("Warnings");
  auto *errors = panel.overviewTreeForTest("Errors");
  ASSERT_NE(all, nullptr);
  ASSERT_NE(warnings, nullptr);
  ASSERT_NE(errors, nullptr);

  EXPECT_FALSE(
      all->findItems("Battery", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_FALSE(
      warnings->findItems("Battery", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
  EXPECT_TRUE(
      errors->findItems("Motor", Qt::MatchExactly | Qt::MatchRecursive, 0)
          .empty());
}

TEST(PanelIntegration, DoubleClickingDiagnosticOpensAndReusesDetailDialog) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  auto status =
      makeMessage(diagnostic_msgs::msg::DiagnosticStatus::WARN, "Scan jitter")
          .status.front();
  message.status = {status};

  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *tree = panel.overviewTreeForTest("All");
  ASSERT_NE(tree, nullptr);
  const auto front_items =
      tree->findItems("Front", Qt::MatchExactly | Qt::MatchRecursive, 0);
  ASSERT_EQ(front_items.size(), 1);

  const std::string id = "Sensors/Lidar/Front\nlidar_front";
  ASSERT_TRUE(QMetaObject::invokeMethod(
      tree, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QTreeWidgetItem *, front_items.front()), Q_ARG(int, 0)));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  EXPECT_EQ(panel.detailDialogCountForTest(), 1);
  EXPECT_TRUE(dialog->windowTitle().contains("Sensors/Lidar/Front [lidar_front]"));
  ASSERT_NE(dialog->findChild<QTableWidget *>("diagnostic_detail_values"), nullptr);
  ASSERT_NE(dialog->findChild<QListWidget *>("diagnostic_detail_events"), nullptr);

  ASSERT_TRUE(QMetaObject::invokeMethod(
      tree, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QTreeWidgetItem *, front_items.front()), Q_ARG(int, 0)));
  QApplication::processEvents();
  EXPECT_EQ(panel.detailDialogForTest(id), dialog);
  EXPECT_EQ(panel.detailDialogCountForTest(), 1);
  dialog->hide();
  QApplication::processEvents();
}

TEST(PanelIntegration, DetailPopupUsesCompactSelectableEventFeed) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray initial;
  auto warm = makeStatus("Drive/Motor", "motor",
                         diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm");
  addValue(warm, "temperature", "72 C");
  initial.status = {warm};
  panel.ingestForTest(initial);

  diagnostic_msgs::msg::DiagnosticArray updated;
  auto fault = makeStatus("Drive/Motor", "motor",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault");
  addValue(fault, "temperature", "91 C");
  updated.status = {fault};
  panel.ingestForTest(updated);
  panel.refreshForTest();

  const std::string id = "Drive/Motor\nmotor";
  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  auto *header = dialog->findChild<QLabel *>("diagnostic_detail_header");
  ASSERT_NE(header, nullptr);
  EXPECT_FALSE(header->text().contains("Drive/Motor"));
  EXPECT_FALSE(header->text().contains("motor"));

  auto *severity = dialog->findChild<QLabel *>("diagnostic_detail_severity");
  ASSERT_NE(severity, nullptr);
  EXPECT_EQ(severity->text(), "ERROR");
  EXPECT_TRUE(severity->styleSheet().contains("border"));

  auto *detail_events = dialog->findChild<QListWidget *>("diagnostic_detail_events");
  ASSERT_NE(detail_events, nullptr);
  ASSERT_GE(detail_events->count(), 2);
  EXPECT_EQ(eventLabelText(detail_events, 0, "event_severity"), "ERROR");
  EXPECT_TRUE(eventLabelText(detail_events, 0, "event_age").contains("ago"));
  EXPECT_TRUE(detail_events->item(0)->text().contains("Fault"));
  auto *row_widget = detail_events->itemWidget(detail_events->item(0));
  EXPECT_EQ(row_widget, nullptr);
  EXPECT_FALSE(detail_events->item(0)->text().contains("temperature=91 C"));

  auto *message = dialog->findChild<QLabel *>("diagnostic_detail_message");
  auto *values = dialog->findChild<QTableWidget *>("diagnostic_detail_values");
  ASSERT_NE(message, nullptr);
  ASSERT_NE(values, nullptr);
  EXPECT_EQ(detail_events->currentRow(), 0);
  EXPECT_EQ(message->text(), "Fault");
  ASSERT_EQ(values->rowCount(), 1);
  EXPECT_EQ(values->item(0, 0)->text(), "temperature");
  EXPECT_EQ(values->item(0, 1)->text(), "91 C");
  EXPECT_EQ(values->columnCount(), 3);

  detail_events->setCurrentRow(1);
  EXPECT_EQ(message->text(), "Warm");
  ASSERT_EQ(values->rowCount(), 1);
  EXPECT_EQ(values->item(0, 1)->text(), "72 C");
  dialog->hide();
}

TEST(PanelIntegration, DetailPopupShowsPlotButtonOnlyForNumericValues) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  auto status = makeStatus("Drive/Motor", "motor",
                           diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm");
  addValue(status, "temperature", "72 C");
  addValue(status, "state", "warm");
  message.status = {status};
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest("Drive/Motor\nmotor");
  ASSERT_NE(dialog, nullptr);
  auto *values = dialog->findChild<QTableWidget *>("diagnostic_detail_values");
  ASSERT_NE(values, nullptr);
  ASSERT_EQ(values->rowCount(), 2);
  ASSERT_EQ(values->columnCount(), 3);
  EXPECT_NE(values->cellWidget(0, 2), nullptr);
  EXPECT_EQ(values->cellWidget(1, 2), nullptr);
  dialog->hide();
}

TEST(PanelIntegration, PlotButtonOpensReusableValuePlotDialogAndRefreshes) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray initial;
  auto warm = makeStatus("Drive/Motor", "motor",
                         diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm");
  addValue(warm, "temperature", "72 C");
  initial.status = {warm};
  panel.ingestForTest(initial);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  const std::string id = "Drive/Motor\nmotor";
  auto *detail = panel.detailDialogForTest(id);
  ASSERT_NE(detail, nullptr);
  auto *values = detail->findChild<QTableWidget *>("diagnostic_detail_values");
  ASSERT_NE(values, nullptr);
  auto *button = qobject_cast<QPushButton *>(values->cellWidget(0, 2));
  ASSERT_NE(button, nullptr);
  button->click();
  QApplication::processEvents();

  auto *plot = panel.valuePlotDialogForTest(id, "temperature");
  ASSERT_NE(plot, nullptr);
  EXPECT_TRUE(plot->windowTitle().contains("Drive/Motor / temperature"));
  auto *header = plot->findChild<QLabel *>("diagnostic_value_plot_header");
  ASSERT_NE(header, nullptr);
  EXPECT_TRUE(header->text().contains("Samples: 1"));

  button->click();
  QApplication::processEvents();
  EXPECT_EQ(panel.valuePlotDialogForTest(id, "temperature"), plot);

  diagnostic_msgs::msg::DiagnosticArray updated;
  auto fault = makeStatus("Drive/Motor", "motor",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault");
  addValue(fault, "temperature", "91 C");
  updated.status = {fault};
  panel.ingestForTest(updated);
  panel.refreshForTest();

  EXPECT_TRUE(header->text().contains("Current: 91 C"));
  EXPECT_TRUE(header->text().contains("Samples: 2"));
  plot->hide();
  detail->hide();
}

TEST(PanelIntegration, ValuePlotAutoScaleControlsToggleManualRange) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  auto status = makeStatus("Drive/Motor", "motor",
                           diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm");
  addValue(status, "temperature", "72 C");
  message.status = {status};
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  const std::string id = "Drive/Motor\nmotor";
  auto *detail = panel.detailDialogForTest(id);
  ASSERT_NE(detail, nullptr);
  auto *values = detail->findChild<QTableWidget *>("diagnostic_detail_values");
  ASSERT_NE(values, nullptr);
  auto *button = qobject_cast<QPushButton *>(values->cellWidget(0, 2));
  ASSERT_NE(button, nullptr);
  button->click();
  QApplication::processEvents();

  auto *plot = panel.valuePlotDialogForTest(id, "temperature");
  ASSERT_NE(plot, nullptr);
  auto *auto_scale =
      plot->findChild<QCheckBox *>("diagnostic_value_plot_auto_scale");
  auto *min_spin = plot->findChild<QDoubleSpinBox *>("diagnostic_value_plot_min");
  auto *max_spin = plot->findChild<QDoubleSpinBox *>("diagnostic_value_plot_max");
  ASSERT_NE(auto_scale, nullptr);
  ASSERT_NE(min_spin, nullptr);
  ASSERT_NE(max_spin, nullptr);

  EXPECT_TRUE(auto_scale->isChecked());
  EXPECT_FALSE(min_spin->isEnabled());
  EXPECT_FALSE(max_spin->isEnabled());
  auto_scale->setChecked(false);
  EXPECT_TRUE(min_spin->isEnabled());
  EXPECT_TRUE(max_spin->isEnabled());
  plot->hide();
  detail->hide();
}

TEST(PanelIntegration, DetailPopupPauseFreezesEventFeedAndContinuesToNewest) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray initial;
  auto warn = makeStatus("Drive/Motor", "motor",
                         diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm");
  addValue(warn, "state", "warm");
  initial.status = {warn};
  panel.ingestForTest(initial);

  diagnostic_msgs::msg::DiagnosticArray error_message;
  auto error = makeStatus("Drive/Motor", "motor",
                          diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault");
  addValue(error, "state", "fault");
  error_message.status = {error};
  panel.ingestForTest(error_message);
  panel.refreshForTest();

  const std::string id = "Drive/Motor\nmotor";
  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();
  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  auto *detail_events = dialog->findChild<QListWidget *>("diagnostic_detail_events");
  auto *message = dialog->findChild<QLabel *>("diagnostic_detail_message");
  auto *pause = dialog->findChild<QPushButton *>("diagnostic_detail_pause_button");
  ASSERT_NE(detail_events, nullptr);
  ASSERT_NE(message, nullptr);
  ASSERT_NE(pause, nullptr);
  ASSERT_EQ(eventLabelText(detail_events, 0, "event_severity"), "ERROR");
  EXPECT_EQ(message->text(), "Fault");

  pause->click();
  EXPECT_EQ(pause->text(), "Continue Feed");

  diagnostic_msgs::msg::DiagnosticArray recovered;
  auto ok = makeStatus("Drive/Motor", "motor",
                       diagnostic_msgs::msg::DiagnosticStatus::OK, "Recovered");
  addValue(ok, "state", "ok");
  recovered.status = {ok};
  panel.ingestForTest(recovered);
  panel.refreshForTest();

  EXPECT_EQ(eventLabelText(detail_events, 0, "event_severity"), "ERROR");
  EXPECT_EQ(message->text(), "Fault");

  pause->click();
  EXPECT_EQ(pause->text(), "Pause Feed");
  ASSERT_GE(detail_events->count(), 3);
  EXPECT_EQ(eventLabelText(detail_events, 0, "event_severity"), "OK");
  EXPECT_EQ(message->text(), "Recovered");
  dialog->hide();
}

TEST(PanelIntegration, DetailPopupAutoLatestIsOptIn) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray initial;
  initial.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"),
  };
  panel.ingestForTest(initial);

  diagnostic_msgs::msg::DiagnosticArray fault;
  fault.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
  };
  panel.ingestForTest(fault);
  panel.refreshForTest();

  const std::string id = "Drive/Motor\nmotor";
  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  auto *detail_events = dialog->findChild<QListWidget *>("diagnostic_detail_events");
  auto *message = dialog->findChild<QLabel *>("diagnostic_detail_message");
  auto *auto_latest = dialog->findChild<QCheckBox *>("diagnostic_detail_auto_latest");
  ASSERT_NE(detail_events, nullptr);
  ASSERT_NE(message, nullptr);
  ASSERT_NE(auto_latest, nullptr);
  EXPECT_FALSE(auto_latest->isChecked());

  detail_events->setCurrentRow(1);
  EXPECT_EQ(message->text(), "Warm");

  diagnostic_msgs::msg::DiagnosticArray recovered;
  recovered.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::OK, "Recovered"),
  };
  panel.ingestForTest(recovered);
  panel.refreshForTest();

  ASSERT_GE(detail_events->count(), 3);
  EXPECT_EQ(message->text(), "Warm");

  auto_latest->setChecked(true);
  EXPECT_EQ(detail_events->currentRow(), 0);
  EXPECT_EQ(message->text(), "Recovered");
  dialog->hide();
}

TEST(PanelIntegration, DetailPopupPlacesHistoryAfterEventDetails) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {makeStatus("Drive/Motor", "motor",
                               diagnostic_msgs::msg::DiagnosticStatus::WARN,
                               "Warm")};
  panel.ingestForTest(message);
  panel.refreshForTest();

  const std::string id = "Drive/Motor\nmotor";
  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  auto *detail_events = dialog->findChild<QListWidget *>("diagnostic_detail_events");
  auto *values = dialog->findChild<QTableWidget *>("diagnostic_detail_values");
  auto *timeline = dialog->findChild<QWidget *>("diagnostic_detail_timeline");
  ASSERT_NE(detail_events, nullptr);
  ASSERT_NE(values, nullptr);
  ASSERT_NE(timeline, nullptr);
  auto *layout = dialog->layout();
  ASSERT_NE(layout, nullptr);
  EXPECT_GT(layout->indexOf(timeline), layout->indexOf(detail_events));
  EXPECT_GT(layout->indexOf(timeline), layout->indexOf(values));
  dialog->hide();
}

TEST(PanelIntegration, GroupPopupShowsHistoryAndStoresDiagnosticIds) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor/Left", "left_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"),
      makeStatus("Drive/Motor/Right", "right_motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
  };
  panel.ingestForTest(message);
  panel.refreshForTest();

  panel.openGroupDialogForTest("Drive/Motor");
  QApplication::processEvents();

  auto *dialog = panel.groupDialogForTest("Drive/Motor");
  ASSERT_NE(dialog, nullptr);
  auto *severity = dialog->findChild<QLabel *>("diagnostic_group_severity");
  ASSERT_NE(severity, nullptr);
  EXPECT_EQ(severity->text(), "ERROR");
  ASSERT_NE(dialog->findChild<QWidget *>("diagnostic_group_timeline"), nullptr);

  auto *diagnostics = dialog->findChild<QTreeWidget *>("diagnostic_group_values");
  ASSERT_NE(diagnostics, nullptr);
  ASSERT_EQ(diagnostics->topLevelItemCount(), 2);
  auto *first = diagnostics->topLevelItem(0);
  ASSERT_NE(first, nullptr);
  const auto id = first->data(0, Qt::UserRole).toString();
  EXPECT_FALSE(id.isEmpty());

  delete dialog;
  QApplication::processEvents();
}

TEST(PanelIntegration, EventFeedUsesTimelineRowsAndOpensDetails) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  auto status = makeStatus("Drive/Motor", "motor",
                           diagnostic_msgs::msg::DiagnosticStatus::ERROR,
                           "Over temperature");
  diagnostic_msgs::msg::KeyValue value;
  value.key = "temperature";
  value.value = "91 C";
  status.values.push_back(value);
  message.status = {status};
  panel.ingestForTest(message);
  panel.refreshForTest();

  EXPECT_EQ(panel.findChild<QTableWidget *>("diagnostic_event_feed"), nullptr);
  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_EQ(event_list->count(), 1);
  EXPECT_EQ(event_list->itemWidget(event_list->item(0)), nullptr);
  EXPECT_TRUE(event_list->item(0)->text().contains("["));
  EXPECT_TRUE(event_list->item(0)->text().contains("Drive/Motor (motor)"));
  EXPECT_TRUE(event_list->item(0)->text().contains("Over temperature"));
  EXPECT_FALSE(event_list->item(0)->text().contains("temperature=91 C"));

  const std::string id = "Drive/Motor\nmotor";
  ASSERT_TRUE(QMetaObject::invokeMethod(
      event_list, "itemDoubleClicked", Qt::DirectConnection,
      Q_ARG(QListWidgetItem *, event_list->item(0))));
  QApplication::processEvents();

  auto *dialog = panel.detailDialogForTest(id);
  ASSERT_NE(dialog, nullptr);
  EXPECT_TRUE(dialog->windowTitle().contains("Drive/Motor [motor]"));
  dialog->hide();
  QApplication::processEvents();
}

TEST(PanelIntegration, EventFeedHasWrapToggle) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  auto *event_list = panel.eventFeedListForTest();
  auto *wrap = panel.findChild<QCheckBox *>("event_feed_wrap");
  ASSERT_NE(event_list, nullptr);
  ASSERT_NE(wrap, nullptr);

  EXPECT_TRUE(wrap->isChecked());
  EXPECT_TRUE(event_list->wordWrap());
  wrap->setChecked(false);
  EXPECT_FALSE(event_list->wordWrap());
  wrap->setChecked(true);
  EXPECT_TRUE(event_list->wordWrap());
}

TEST(PanelIntegration, EventFeedPreservesSelectionAndScrollWhenUnchanged) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;
  panel.resize(420, 260);
  panel.show();

  diagnostic_msgs::msg::DiagnosticArray message;
  for (int i = 0; i < 80; ++i) {
    message.status.push_back(
        makeStatus("Synthetic/Event " + std::to_string(i),
                   "synthetic_" + std::to_string(i),
                   diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"));
  }
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  const auto tabs = panel.findChildren<QTabWidget *>();
  for (auto *tabs_widget : tabs) {
    if (tabs_widget->count() > 1 && tabs_widget->tabText(1) == "Event Feed") {
      tabs_widget->setCurrentIndex(1);
    }
  }
  event_list->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
  event_list->setFixedHeight(140);
  QApplication::processEvents();
  ASSERT_EQ(event_list->count(), 80);
  ASSERT_GT(event_list->verticalScrollBar()->maximum(), 0);

  event_list->setCurrentRow(10);
  const auto selected_id = event_list->currentItem()->data(Qt::UserRole).toString();
  const auto expected_scroll = event_list->verticalScrollBar()->maximum() / 2;
  event_list->verticalScrollBar()->setValue(expected_scroll);

  panel.refreshForTest();

  ASSERT_NE(event_list->currentItem(), nullptr);
  EXPECT_EQ(event_list->currentItem()->data(Qt::UserRole).toString(), selected_id);
  EXPECT_EQ(event_list->verticalScrollBar()->value(), expected_scroll);
}

TEST(PanelIntegration, EventFeedFiltersVisibleTimelineRows) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray message;
  message.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
      makeStatus("Power/Battery", "battery",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Low"),
  };
  panel.ingestForTest(message);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_EQ(event_list->count(), 2);

  auto *event_search = lineEditWithPlaceholder(&panel, "Filter events");
  ASSERT_NE(event_search, nullptr);
  event_search->setText("battery");
  panel.refreshForTest();
  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Power/Battery (battery)"));
  event_search->clear();

  event_search->setText("fault");
  panel.refreshForTest();
  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Drive/Motor (motor)"));
  event_search->clear();

  auto *error = checkBoxWithText(&panel, "ERROR");
  ASSERT_NE(error, nullptr);
  error->setChecked(false);
  panel.refreshForTest();
  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Power/Battery (battery)"));
}

TEST(PanelIntegration, EventFeedFiltersAfterRetainingWindowEvents) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray warning;
  warning.status = {
      makeStatus("Power/Battery", "battery",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Low"),
  };
  panel.ingestForTest(warning);

  for (int i = 0; i < 30; ++i) {
    diagnostic_msgs::msg::DiagnosticArray ok_message;
    ok_message.status = {
        makeStatus("Synthetic/Ok " + std::to_string(i),
                   "ok_" + std::to_string(i),
                   diagnostic_msgs::msg::DiagnosticStatus::OK, "Nominal"),
    };
    panel.ingestForTest(ok_message);
  }
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_EQ(event_list->count(), 31);

  auto *ok = checkBoxWithText(&panel, "OK");
  auto *error = checkBoxWithText(&panel, "ERROR");
  auto *stale = checkBoxWithText(&panel, "STALE");
  ASSERT_NE(ok, nullptr);
  ASSERT_NE(error, nullptr);
  ASSERT_NE(stale, nullptr);
  ok->setChecked(false);
  error->setChecked(false);
  stale->setChecked(false);
  panel.refreshForTest();

  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Power/Battery (battery)"));
}

TEST(PanelIntegration, EventFeedPauseFreezesAndContinueRefreshesRows) {
  rviz2_diagnostics_monitor::DiagnosticsMonitorPanel panel;

  diagnostic_msgs::msg::DiagnosticArray initial;
  initial.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::WARN, "Warm"),
  };
  panel.ingestForTest(initial);
  panel.refreshForTest();

  auto *event_list = panel.eventFeedListForTest();
  ASSERT_NE(event_list, nullptr);
  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Warm"));

  auto *pause = panel.findChild<QPushButton *>("event_feed_pause_button");
  ASSERT_NE(pause, nullptr);
  pause->click();
  EXPECT_EQ(pause->text(), "Continue Feed");

  diagnostic_msgs::msg::DiagnosticArray updated;
  updated.status = {
      makeStatus("Drive/Motor", "motor",
                 diagnostic_msgs::msg::DiagnosticStatus::ERROR, "Fault"),
  };
  panel.ingestForTest(updated);
  panel.refreshForTest();

  ASSERT_EQ(event_list->count(), 1);
  EXPECT_TRUE(event_list->item(0)->text().contains("Warm"));
  EXPECT_FALSE(event_list->item(0)->text().contains("Fault"));

  pause->click();
  EXPECT_EQ(pause->text(), "Pause Feed");
  ASSERT_GE(event_list->count(), 2);
  EXPECT_TRUE(event_list->item(0)->text().contains("Fault"));
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
