// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <set>
#include <string>

#include <QWidget>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rviz_common/panel.hpp>

#include "rviz2_diagnostics_monitor/diagnostic_model.hpp"

class QCheckBox;
class QLabel;
class QLineEdit;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace rviz2_diagnostics_monitor {

class TimelineWidget : public QWidget {
public:
  explicit TimelineWidget(QWidget *parent = nullptr);

  void setSamples(std::vector<HistorySample> samples);

protected:
  void paintEvent(QPaintEvent *event) override;

private:
  std::vector<HistorySample> samples_;
};

class DiagnosticsMonitorPanel : public rviz_common::Panel {
public:
  explicit DiagnosticsMonitorPanel(QWidget *parent = nullptr);

  void onInitialize() override;
  void load(const rviz_common::Config &config) override;
  void save(rviz_common::Config config) const override;

  void initializeForTest(const rclcpp::Node::SharedPtr &node);
  void setTopicForTest(const std::string &topic);
  void setStaleTimeoutForTest(std::chrono::milliseconds timeout);
  SummaryCounts currentCountsForTest();
  std::vector<DiagnosticEvent> eventsForTest() const;
  std::vector<DiagnosticSnapshot> snapshotsForTest();
  void ingestForTest(const diagnostic_msgs::msg::DiagnosticArray &message);
  void refreshForTest();
  QTreeWidget *overviewTreeForTest() const;
  QLineEdit *overviewSearchForTest() const;

  static QColor colorFor(Severity severity);

private:
  void buildUi();
  void applySettingsFromControls();
  void subscribe();
  void refreshUi();
  void refreshOverview(const std::vector<DiagnosticSnapshot> &snapshots);
  void refreshEvents();
  void showDetails(const std::string &id);
  void addSection(QTreeWidgetItem *root, const QString &title,
                  const std::vector<DiagnosticSnapshot> &snapshots,
                  Severity severity);
  void addTreeNode(QTreeWidgetItem *parent, const TreeNode &node);
  void setItemSeverity(QTreeWidgetItem *item, Severity severity);
  void setRowSeverity(QTableWidget *table, int row, Severity severity);
  void rebuildSubscriptionIfReady();
  TreeNode treeForSnapshots(const std::vector<DiagnosticSnapshot> &snapshots) const;
  std::set<QString> expandedItemPaths() const;
  void restoreExpandedItemPaths(const std::set<QString> &expanded_paths);
  QString itemPath(const QTreeWidgetItem *item) const;

  static QString ageText(std::chrono::steady_clock::time_point last_seen,
                         std::chrono::steady_clock::time_point now);

  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      subscription_;

  mutable std::mutex mutex_;
  DiagnosticModel model_;
  std::string selected_id_;

  QLineEdit *topic_edit_{nullptr};
  QSpinBox *stale_timeout_spin_{nullptr};
  QSpinBox *history_window_spin_{nullptr};
  QSpinBox *max_events_spin_{nullptr};
  QLineEdit *overview_search_{nullptr};
  QLabel *summary_label_{nullptr};
  QTreeWidget *overview_tree_{nullptr};
  QLabel *detail_label_{nullptr};
  QTableWidget *detail_values_{nullptr};
  TimelineWidget *overall_timeline_{nullptr};
  TimelineWidget *selected_timeline_{nullptr};
  QTabWidget *tabs_{nullptr};
  QCheckBox *event_ok_{nullptr};
  QCheckBox *event_warn_{nullptr};
  QCheckBox *event_error_{nullptr};
  QCheckBox *event_stale_{nullptr};
  QLineEdit *event_hardware_filter_{nullptr};
  QLineEdit *event_search_{nullptr};
  QTableWidget *event_table_{nullptr};
};

} // namespace rviz2_diagnostics_monitor
