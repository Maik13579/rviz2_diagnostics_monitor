// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <mutex>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include <QPointer>
#include <QWidget>
#include <diagnostic_msgs/msg/diagnostic_array.hpp>
#include <rclcpp/rclcpp.hpp>
#include <rviz_common/panel.hpp>

#include "rviz2_diagnostics_monitor/diagnostic_model.hpp"

class QCheckBox;
class QDialog;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace rviz2_diagnostics_monitor {

class DiagnosticDetailDialog;
class DiagnosticGroupDialog;

class TimelineWidget : public QWidget {
public:
  explicit TimelineWidget(QWidget *parent = nullptr);

  void setSamples(std::vector<HistorySample> samples,
                  std::chrono::steady_clock::time_point now,
                  std::chrono::milliseconds window);
  void setSelectedStamp(
      std::optional<std::chrono::steady_clock::time_point> stamp);
  void setClickCallback(
      std::function<void(std::chrono::steady_clock::time_point)> callback);

protected:
  void mousePressEvent(QMouseEvent *event) override;
  void paintEvent(QPaintEvent *event) override;

private:
  std::vector<HistorySample> samples_;
  std::chrono::steady_clock::time_point now_{};
  std::chrono::milliseconds window_{std::chrono::minutes(10)};
  std::optional<std::chrono::steady_clock::time_point> selected_stamp_;
  std::function<void(std::chrono::steady_clock::time_point)> click_callback_;
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
  QTreeWidget *overviewTreeForTest(const QString &tab) const;
  QTabWidget *overviewTabsForTest() const;
  QLineEdit *overviewSearchForTest() const;
  QDialog *detailDialogForTest(const std::string &id) const;
  QDialog *groupDialogForTest(const QString &path) const;
  void openGroupDialogForTest(const QString &path);
  int detailDialogCountForTest() const;
  QListWidget *eventFeedListForTest() const;
  std::vector<HistorySample> groupHistoryForTest(const QString &path);

  static QColor colorFor(Severity severity);
  static QString ageText(std::chrono::steady_clock::time_point last_seen,
                         std::chrono::steady_clock::time_point now);

private:
  void buildUi();
  void applySettingsFromControls();
  void subscribe();
  void refreshUi();
  void refreshOverview(const std::vector<DiagnosticSnapshot> &snapshots);
  void refreshEvents();
  void refreshDetailDialogs();
  void openDetailDialog(const std::string &id);
  void openGroupDialog(const QString &path);
  void addTreeNode(QTreeWidget *tree, QTreeWidgetItem *parent,
                   const TreeNode &node);
  void configureOverviewTree(QTreeWidget *tree);
  void connectOverviewTree(QTreeWidget *tree);
  void setItemSeverity(QTreeWidgetItem *item, Severity severity);
  void rebuildSubscriptionIfReady();
  TreeNode treeForSnapshots(const std::vector<DiagnosticSnapshot> &snapshots) const;
  std::map<std::string, std::set<QString>> expandedItemPaths() const;
  void restoreExpandedItemPaths(
      const std::map<std::string, std::set<QString>> &expanded_paths);
  QString itemPath(const QTreeWidgetItem *item) const;
  std::vector<DiagnosticSnapshot> snapshotsForGroupPath(const QString &path);
  std::vector<HistorySample> historyForGroupPath(const QString &path);
  static std::vector<DiagnosticSnapshot> aggregateOverviewSnapshots(
      const std::vector<DiagnosticSnapshot> &snapshots);

  rclcpp::Node::SharedPtr node_;
  rclcpp::Subscription<diagnostic_msgs::msg::DiagnosticArray>::SharedPtr
      subscription_;

  mutable std::mutex mutex_;
  DiagnosticModel model_;
  std::map<std::string, QPointer<DiagnosticDetailDialog>> detail_dialogs_;
  std::map<QString, QPointer<DiagnosticGroupDialog>> group_dialogs_;

  QLineEdit *topic_edit_{nullptr};
  QSpinBox *stale_timeout_spin_{nullptr};
  QSpinBox *history_window_spin_{nullptr};
  QLineEdit *overview_search_{nullptr};
  QLabel *summary_label_{nullptr};
  TimelineWidget *overall_timeline_{nullptr};
  QTabWidget *overview_tabs_{nullptr};
  std::map<std::string, QTreeWidget *> overview_trees_;
  QTabWidget *tabs_{nullptr};
  QCheckBox *event_ok_{nullptr};
  QCheckBox *event_warn_{nullptr};
  QCheckBox *event_error_{nullptr};
  QCheckBox *event_stale_{nullptr};
  QCheckBox *event_wrap_{nullptr};
  QPushButton *event_pause_button_{nullptr};
  QLineEdit *event_search_{nullptr};
  QListWidget *event_list_{nullptr};
  bool event_feed_paused_{false};
  std::map<std::string, std::vector<std::string>> overview_tree_signatures_;
  std::vector<std::string> event_row_signatures_;
};

} // namespace rviz2_diagnostics_monitor
