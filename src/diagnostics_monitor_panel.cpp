// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include "rviz2_diagnostics_monitor/diagnostics_monitor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QPushButton>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QSplitter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <pluginlib/class_list_macros.hpp>
#include <rviz_common/config.hpp>
#include <rviz_common/display_context.hpp>

namespace rviz2_diagnostics_monitor {
namespace {

constexpr const char *kDefaultTopic = "/diagnostics_agg";

QString qstr(const std::string &text) {
  return QString::fromStdString(text);
}

std::string str(const QString &text) {
  return text.toStdString();
}

} // namespace

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
  setMinimumHeight(26);
}

void TimelineWidget::setSamples(std::vector<HistorySample> samples) {
  samples_ = std::move(samples);
  update();
}

void TimelineWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor("#f3f4f6"));
  if (samples_.empty()) {
    painter.setPen(QColor("#6b7280"));
    painter.drawText(rect(), Qt::AlignCenter, "No history");
    return;
  }

  const int segment_width = std::max(1, width() / static_cast<int>(samples_.size()));
  for (int i = 0; i < static_cast<int>(samples_.size()); ++i) {
    painter.fillRect(i * segment_width, 0,
                     i == static_cast<int>(samples_.size()) - 1
                         ? width() - i * segment_width
                         : segment_width,
                     height(), DiagnosticsMonitorPanel::colorFor(samples_[i].level));
  }
}

DiagnosticsMonitorPanel::DiagnosticsMonitorPanel(QWidget *parent)
    : rviz_common::Panel(parent) {
  buildUi();
  applySettingsFromControls();

  auto *timer = new QTimer(this);
  QObject::connect(timer, &QTimer::timeout, this, [this]() { refreshUi(); });
  timer->start(500);
}

void DiagnosticsMonitorPanel::onInitialize() {
  const auto abstraction = getDisplayContext()->getRosNodeAbstraction().lock();
  if (!abstraction) {
    return;
  }
  initializeForTest(abstraction->get_raw_node());
}

void DiagnosticsMonitorPanel::load(const rviz_common::Config &config) {
  rviz_common::Panel::load(config);

  QString string_value;
  int int_value = 0;
  bool bool_value = true;

  if (config.mapGetString("Diagnostics Topic", &string_value)) {
    topic_edit_->setText(string_value);
  }
  if (config.mapGetInt("Stale Timeout Ms", &int_value)) {
    stale_timeout_spin_->setValue(int_value);
  }
  if (config.mapGetInt("History Window Sec", &int_value)) {
    history_window_spin_->setValue(int_value);
  }
  if (config.mapGetInt("Max Event Rows", &int_value)) {
    max_events_spin_->setValue(int_value);
  }
  if (config.mapGetBool("Show OK Events", &bool_value)) {
    event_ok_->setChecked(bool_value);
  }
  if (config.mapGetBool("Show WARN Events", &bool_value)) {
    event_warn_->setChecked(bool_value);
  }
  if (config.mapGetBool("Show ERROR Events", &bool_value)) {
    event_error_->setChecked(bool_value);
  }
  if (config.mapGetBool("Show STALE Events", &bool_value)) {
    event_stale_->setChecked(bool_value);
  }

  applySettingsFromControls();
  rebuildSubscriptionIfReady();
}

void DiagnosticsMonitorPanel::save(rviz_common::Config config) const {
  rviz_common::Panel::save(config);
  config.mapSetValue("Diagnostics Topic", topic_edit_->text());
  config.mapSetValue("Stale Timeout Ms", stale_timeout_spin_->value());
  config.mapSetValue("History Window Sec", history_window_spin_->value());
  config.mapSetValue("Max Event Rows", max_events_spin_->value());
  config.mapSetValue("Show OK Events", event_ok_->isChecked());
  config.mapSetValue("Show WARN Events", event_warn_->isChecked());
  config.mapSetValue("Show ERROR Events", event_error_->isChecked());
  config.mapSetValue("Show STALE Events", event_stale_->isChecked());
}

void DiagnosticsMonitorPanel::initializeForTest(
    const rclcpp::Node::SharedPtr &node) {
  node_ = node;
  subscribe();
}

void DiagnosticsMonitorPanel::setTopicForTest(const std::string &topic) {
  topic_edit_->setText(qstr(topic));
  rebuildSubscriptionIfReady();
}

void DiagnosticsMonitorPanel::setStaleTimeoutForTest(
    std::chrono::milliseconds timeout) {
  stale_timeout_spin_->setValue(static_cast<int>(timeout.count()));
  applySettingsFromControls();
}

SummaryCounts DiagnosticsMonitorPanel::currentCountsForTest() {
  std::lock_guard<std::mutex> lock(mutex_);
  return model_.counts(std::chrono::steady_clock::now());
}

std::vector<DiagnosticEvent> DiagnosticsMonitorPanel::eventsForTest() const {
  std::lock_guard<std::mutex> lock(mutex_);
  return model_.events();
}

std::vector<DiagnosticSnapshot> DiagnosticsMonitorPanel::snapshotsForTest() {
  std::lock_guard<std::mutex> lock(mutex_);
  return model_.snapshots(std::chrono::steady_clock::now());
}

void DiagnosticsMonitorPanel::refreshForTest() { refreshUi(); }

QTreeWidget *DiagnosticsMonitorPanel::overviewTreeForTest() const {
  return overview_tree_;
}

void DiagnosticsMonitorPanel::buildUi() {
  setMinimumWidth(260);
  setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);

  auto *root = new QVBoxLayout(this);
  root->setContentsMargins(4, 4, 4, 4);
  root->setSpacing(4);

  topic_edit_ = new QLineEdit(kDefaultTopic, this);
  topic_edit_->setMinimumWidth(0);
  stale_timeout_spin_ = new QSpinBox(this);
  stale_timeout_spin_->setRange(100, 600000);
  stale_timeout_spin_->setValue(3000);
  stale_timeout_spin_->setSuffix(" ms");
  history_window_spin_ = new QSpinBox(this);
  history_window_spin_->setRange(1, 86400);
  history_window_spin_->setValue(600);
  history_window_spin_->setSuffix(" s");
  max_events_spin_ = new QSpinBox(this);
  max_events_spin_->setRange(1, 100000);
  max_events_spin_->setValue(5000);

  tabs_ = new QTabWidget(this);
  root->addWidget(tabs_, 1);

  auto *overview = new QWidget(tabs_);
  auto *overview_layout = new QVBoxLayout(overview);
  overview_layout->setContentsMargins(4, 4, 4, 4);
  overview_layout->setSpacing(4);
  overview_search_ = new QLineEdit(overview);
  overview_search_->setPlaceholderText("Filter diagnostics");
  overview_search_->setMinimumWidth(0);
  summary_label_ = new QLabel("Overall: STALE  OK 0  WARN 0  ERROR 0  STALE 0", overview);
  summary_label_->setWordWrap(true);
  overall_timeline_ = new TimelineWidget(overview);
  overview_tree_ = new QTreeWidget(overview);
  overview_tree_->setHeaderLabels({"Device", "Level", "Message", "Hardware ID"});
  overview_tree_->setMinimumWidth(0);
  overview_tree_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  overview_tree_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  overview_tree_->header()->setStretchLastSection(false);
  overview_tree_->header()->setSectionResizeMode(QHeaderView::Interactive);
  overview_tree_->setColumnWidth(0, 170);
  overview_tree_->setColumnWidth(1, 58);
  overview_tree_->setColumnWidth(2, 160);
  overview_tree_->setColumnWidth(3, 110);
  overview_tree_->setAlternatingRowColors(true);

  auto *splitter = new QSplitter(Qt::Vertical, overview);
  auto *detail = new QWidget(splitter);
  auto *detail_layout = new QVBoxLayout(detail);
  detail_layout->setContentsMargins(0, 0, 0, 0);
  detail_label_ = new QLabel("Select a diagnostic for details", detail);
  detail_label_->setWordWrap(true);
  selected_timeline_ = new TimelineWidget(detail);
  detail_values_ = new QTableWidget(0, 2, detail);
  detail_values_->setHorizontalHeaderLabels({"Key", "Value"});
  detail_values_->setMinimumWidth(0);
  detail_values_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  detail_values_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
  detail_layout->addWidget(detail_label_);
  detail_layout->addWidget(selected_timeline_);
  detail_layout->addWidget(detail_values_);

  auto *tree_holder = new QWidget(splitter);
  auto *tree_layout = new QVBoxLayout(tree_holder);
  tree_layout->setContentsMargins(0, 0, 0, 0);
  tree_layout->addWidget(summary_label_);
  tree_layout->addWidget(overall_timeline_);
  tree_layout->addWidget(overview_tree_);
  splitter->addWidget(tree_holder);
  splitter->addWidget(detail);
  splitter->setStretchFactor(0, 3);
  splitter->setStretchFactor(1, 2);
  overview_layout->addWidget(overview_search_);
  overview_layout->addWidget(splitter, 1);
  tabs_->addTab(overview, "Overview");

  QObject::connect(overview_search_, &QLineEdit::textChanged, this,
                   [this]() { refreshUi(); });
  QObject::connect(overview_tree_, &QTreeWidget::itemSelectionChanged, this,
                   [this]() {
                     const auto items = overview_tree_->selectedItems();
                     if (items.empty()) {
                       return;
                     }
                     selected_id_ = str(items.front()->data(0, Qt::UserRole).toString());
                     showDetails(selected_id_);
                   });

  auto *events = new QWidget(tabs_);
  auto *events_layout = new QVBoxLayout(events);
  events_layout->setContentsMargins(4, 4, 4, 4);
  events_layout->setSpacing(4);
  auto *event_filters = new QWidget(events);
  auto *event_filter_layout = new QVBoxLayout(event_filters);
  event_filter_layout->setContentsMargins(0, 0, 0, 0);
  event_filter_layout->setSpacing(4);
  auto *event_severity_row = new QWidget(event_filters);
  auto *event_severity_layout = new QHBoxLayout(event_severity_row);
  event_severity_layout->setContentsMargins(0, 0, 0, 0);
  event_severity_layout->setSpacing(6);
  event_ok_ = new QCheckBox("OK", event_filters);
  event_warn_ = new QCheckBox("WARN", event_filters);
  event_error_ = new QCheckBox("ERROR", event_filters);
  event_stale_ = new QCheckBox("STALE", event_filters);
  for (auto *check : {event_ok_, event_warn_, event_error_, event_stale_}) {
    check->setChecked(true);
    event_severity_layout->addWidget(check);
    QObject::connect(check, &QCheckBox::toggled, this,
                     [this]() { refreshEvents(); });
  }
  event_severity_layout->addStretch(1);
  event_filter_layout->addWidget(event_severity_row);
  event_hardware_filter_ = new QLineEdit(event_filters);
  event_hardware_filter_->setPlaceholderText("Hardware ID");
  event_hardware_filter_->setMinimumWidth(0);
  event_search_ = new QLineEdit(event_filters);
  event_search_->setPlaceholderText("Search events");
  event_search_->setMinimumWidth(0);
  event_filter_layout->addWidget(event_hardware_filter_);
  event_filter_layout->addWidget(event_search_);
  events_layout->addWidget(event_filters);
  event_table_ = new QTableWidget(0, 6, events);
  event_table_->setHorizontalHeaderLabels(
      {"Age", "Level", "Name", "Message", "Hardware ID", "Values"});
  event_table_->setMinimumWidth(0);
  event_table_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  event_table_->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  event_table_->horizontalHeader()->setStretchLastSection(false);
  event_table_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
  event_table_->setColumnWidth(0, 80);
  event_table_->setColumnWidth(1, 58);
  event_table_->setColumnWidth(2, 160);
  event_table_->setColumnWidth(3, 160);
  event_table_->setColumnWidth(4, 100);
  event_table_->setColumnWidth(5, 180);
  event_table_->setAlternatingRowColors(true);
  events_layout->addWidget(event_table_, 1);
  tabs_->addTab(events, "Event Feed");

  QObject::connect(event_hardware_filter_, &QLineEdit::textChanged, this,
                   [this]() { refreshEvents(); });
  QObject::connect(event_search_, &QLineEdit::textChanged, this,
                   [this]() { refreshEvents(); });

  auto *settings_page = new QWidget(tabs_);
  auto *settings_layout = new QVBoxLayout(settings_page);
  settings_layout->setContentsMargins(8, 8, 8, 8);
  settings_layout->setSpacing(8);

  auto *source_group = new QGroupBox("Source", settings_page);
  auto *source_layout = new QFormLayout(source_group);
  auto *topic_row = new QWidget(source_group);
  auto *topic_layout = new QHBoxLayout(topic_row);
  topic_layout->setContentsMargins(0, 0, 0, 0);
  topic_layout->setSpacing(4);
  auto *apply_topic = new QPushButton("Apply", topic_row);
  apply_topic->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  topic_layout->addWidget(topic_edit_, 1);
  topic_layout->addWidget(apply_topic);
  source_layout->addRow("Topic", topic_row);
  settings_layout->addWidget(source_group);

  auto *history_group = new QGroupBox("Retention", settings_page);
  auto *history_layout = new QFormLayout(history_group);
  history_layout->addRow("Stale timeout", stale_timeout_spin_);
  history_layout->addRow("History window", history_window_spin_);
  history_layout->addRow("Max event rows", max_events_spin_);
  settings_layout->addWidget(history_group);
  settings_layout->addStretch(1);
  tabs_->addTab(settings_page, "Settings");

  QObject::connect(apply_topic, &QPushButton::clicked, this,
                   [this]() { rebuildSubscriptionIfReady(); });
  for (auto *spin : {stale_timeout_spin_, history_window_spin_,
                     max_events_spin_}) {
    QObject::connect(spin, qOverload<int>(&QSpinBox::valueChanged), this,
                     [this]() {
                       applySettingsFromControls();
                       refreshUi();
                     });
  }
}

void DiagnosticsMonitorPanel::applySettingsFromControls() {
  DiagnosticModelConfig config;
  config.stale_timeout = std::chrono::milliseconds(stale_timeout_spin_->value());
  config.history_window = std::chrono::seconds(history_window_spin_->value());
  config.max_event_rows = static_cast<std::size_t>(max_events_spin_->value());
  std::lock_guard<std::mutex> lock(mutex_);
  model_.setConfig(config);
}

void DiagnosticsMonitorPanel::subscribe() {
  if (!node_) {
    return;
  }
  applySettingsFromControls();
  const auto topic = str(topic_edit_->text());
  subscription_ = node_->create_subscription<diagnostic_msgs::msg::DiagnosticArray>(
      topic, rclcpp::QoS(10),
      [this](diagnostic_msgs::msg::DiagnosticArray::ConstSharedPtr message) {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          model_.ingest(*message, std::chrono::steady_clock::now());
        }
        QMetaObject::invokeMethod(this, [this]() { refreshUi(); },
                                  Qt::QueuedConnection);
      });
}

void DiagnosticsMonitorPanel::refreshUi() {
  std::vector<DiagnosticSnapshot> snapshots;
  std::vector<HistorySample> overall_history;
  SummaryCounts counts;
  Severity overall = Severity::Ok;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    const auto now = std::chrono::steady_clock::now();
    snapshots = model_.snapshots(now);
    counts = model_.counts(now);
    overall = model_.overallSeverity(now);
    overall_history = model_.overallHistory();
  }

  summary_label_->setText(
      QString("Overall: %1  OK %2  WARN %3  ERROR %4  STALE %5")
          .arg(qstr(DiagnosticModel::severityLabel(overall)))
          .arg(counts.ok)
          .arg(counts.warn)
          .arg(counts.error)
          .arg(counts.stale));
  summary_label_->setStyleSheet(
      QString("font-weight: 600; color: %1").arg(colorFor(overall).name()));
  overall_timeline_->setSamples(std::move(overall_history));
  refreshOverview(snapshots);
  refreshEvents();
  if (!selected_id_.empty()) {
    showDetails(selected_id_);
  }
}

void DiagnosticsMonitorPanel::refreshOverview(
    const std::vector<DiagnosticSnapshot> &snapshots) {
  const auto search = str(overview_search_->text());
  const auto expanded_paths = expandedItemPaths();
  const auto selected_id = selected_id_;
  std::vector<DiagnosticSnapshot> filtered;
  std::copy_if(snapshots.begin(), snapshots.end(), std::back_inserter(filtered),
               [&search](const auto &snapshot) {
                 return DiagnosticModel::matchesSearch(snapshot, search);
               });

  overview_tree_->clear();
  addSection(nullptr, "Error Devices", filtered, Severity::Error);
  addSection(nullptr, "Warned Devices", filtered, Severity::Warn);
  addSection(nullptr, "Stale Devices", filtered, Severity::Stale);

  TreeNode tree;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    tree = model_.tree(std::chrono::steady_clock::now());
  }
  auto *all = new QTreeWidgetItem(overview_tree_, {"All Devices"});
  setItemSeverity(all, tree.severity);
  for (const auto &[_, child] : tree.children) {
    addTreeNode(all, child);
  }
  restoreExpandedItemPaths(expanded_paths);
  if (expanded_paths.empty()) {
    overview_tree_->expandToDepth(1);
  }

  if (!selected_id.empty()) {
    for (int i = 0; i < overview_tree_->topLevelItemCount(); ++i) {
      QList<QTreeWidgetItem *> stack;
      stack.push_back(overview_tree_->topLevelItem(i));
      while (!stack.empty()) {
        auto *item = stack.takeLast();
        if (str(item->data(0, Qt::UserRole).toString()) == selected_id) {
          overview_tree_->setCurrentItem(item);
          return;
        }
        for (int child = 0; child < item->childCount(); ++child) {
          stack.push_back(item->child(child));
        }
      }
    }
  }
}

void DiagnosticsMonitorPanel::refreshEvents() {
  EventFilter filter;
  filter.show_ok = event_ok_->isChecked();
  filter.show_warn = event_warn_->isChecked();
  filter.show_error = event_error_->isChecked();
  filter.show_stale = event_stale_->isChecked();
  filter.hardware_id = str(event_hardware_filter_->text());
  filter.search = str(event_search_->text());

  std::vector<DiagnosticEvent> events;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    events = model_.filteredEvents(filter);
  }

  event_table_->setRowCount(static_cast<int>(events.size()));
  const auto now = std::chrono::steady_clock::now();
  for (int row = 0; row < static_cast<int>(events.size()); ++row) {
    const auto &event = events[row];
    std::stringstream values;
    for (std::size_t i = 0; i < event.snapshot.values.size(); ++i) {
      if (i > 0) {
        values << "; ";
      }
      values << event.snapshot.values[i].key << "="
             << event.snapshot.values[i].value;
    }
    const QStringList columns = {
        ageText(event.stamp, now),
        qstr(DiagnosticModel::severityLabel(event.snapshot.level)),
        qstr(event.snapshot.name),
        qstr(event.snapshot.message),
        qstr(event.snapshot.hardware_id),
        qstr(values.str()),
    };
    for (int col = 0; col < columns.size(); ++col) {
      auto *item = new QTableWidgetItem(columns[col]);
      item->setFlags(item->flags() & ~Qt::ItemIsEditable);
      event_table_->setItem(row, col, item);
    }
    setRowSeverity(event_table_, row, event.snapshot.level);
  }
}

void DiagnosticsMonitorPanel::showDetails(const std::string &id) {
  std::optional<DiagnosticSnapshot> snapshot;
  std::vector<HistorySample> history;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = model_.snapshot(id, std::chrono::steady_clock::now());
    history = model_.historyFor(id);
  }
  if (!snapshot) {
    detail_label_->setText("Select a diagnostic for details");
    detail_values_->setRowCount(0);
    selected_timeline_->setSamples({});
    return;
  }

  const auto now = std::chrono::steady_clock::now();
  detail_label_->setText(
      QString("%1\nLevel: %2   Message: %3\nHardware: %4   Last update: %5")
          .arg(qstr(snapshot->name))
          .arg(qstr(DiagnosticModel::severityLabel(snapshot->level)))
          .arg(qstr(snapshot->message))
          .arg(qstr(snapshot->hardware_id.empty() ? "-" : snapshot->hardware_id))
          .arg(ageText(snapshot->last_seen, now)));
  detail_values_->setRowCount(static_cast<int>(snapshot->values.size()));
  for (int row = 0; row < static_cast<int>(snapshot->values.size()); ++row) {
    detail_values_->setItem(row, 0,
                            new QTableWidgetItem(qstr(snapshot->values[row].key)));
    detail_values_->setItem(row, 1,
                            new QTableWidgetItem(qstr(snapshot->values[row].value)));
  }
  selected_timeline_->setSamples(std::move(history));
}

void DiagnosticsMonitorPanel::addSection(
    QTreeWidgetItem *, const QString &title,
    const std::vector<DiagnosticSnapshot> &snapshots, Severity severity) {
  auto *section = new QTreeWidgetItem(overview_tree_, {title});
  setItemSeverity(section, severity);
  for (const auto &snapshot : snapshots) {
    if (snapshot.level != severity) {
      continue;
    }
    auto *item = new QTreeWidgetItem(
        section, {qstr(snapshot.name),
                  qstr(DiagnosticModel::severityLabel(snapshot.level)),
                  qstr(snapshot.message), qstr(snapshot.hardware_id)});
    item->setData(0, Qt::UserRole, qstr(snapshot.id));
    setItemSeverity(item, snapshot.level);
  }
}

void DiagnosticsMonitorPanel::addTreeNode(QTreeWidgetItem *parent,
                                          const TreeNode &node) {
  auto *item = new QTreeWidgetItem(
      parent, {qstr(node.label), qstr(DiagnosticModel::severityLabel(node.severity))});
  item->setData(0, Qt::UserRole, qstr(node.diagnostic_id));
  setItemSeverity(item, node.severity);
  for (const auto &[_, child] : node.children) {
    addTreeNode(item, child);
  }
}

void DiagnosticsMonitorPanel::setItemSeverity(QTreeWidgetItem *item,
                                              Severity severity) {
  const auto color = colorFor(severity);
  for (int col = 0; col < item->columnCount(); ++col) {
    item->setForeground(col, QBrush(color));
  }
}

void DiagnosticsMonitorPanel::setRowSeverity(QTableWidget *table, int row,
                                             Severity severity) {
  const auto color = colorFor(severity);
  for (int col = 0; col < table->columnCount(); ++col) {
    if (auto *item = table->item(row, col)) {
      item->setForeground(QBrush(color));
    }
  }
}

void DiagnosticsMonitorPanel::rebuildSubscriptionIfReady() {
  applySettingsFromControls();
  if (!node_) {
    return;
  }
  subscribe();
}

std::set<QString> DiagnosticsMonitorPanel::expandedItemPaths() const {
  std::set<QString> paths;
  for (int i = 0; i < overview_tree_->topLevelItemCount(); ++i) {
    const auto *root = overview_tree_->topLevelItem(i);
    QList<const QTreeWidgetItem *> stack;
    stack.push_back(root);
    while (!stack.empty()) {
      const auto *item = stack.takeLast();
      if (item->isExpanded()) {
        paths.insert(itemPath(item));
      }
      for (int child = 0; child < item->childCount(); ++child) {
        stack.push_back(item->child(child));
      }
    }
  }
  return paths;
}

void DiagnosticsMonitorPanel::restoreExpandedItemPaths(
    const std::set<QString> &expanded_paths) {
  for (int i = 0; i < overview_tree_->topLevelItemCount(); ++i) {
    auto *root = overview_tree_->topLevelItem(i);
    QList<QTreeWidgetItem *> stack;
    stack.push_back(root);
    while (!stack.empty()) {
      auto *item = stack.takeLast();
      item->setExpanded(expanded_paths.count(itemPath(item)) > 0);
      for (int child = 0; child < item->childCount(); ++child) {
        stack.push_back(item->child(child));
      }
    }
  }
}

QString DiagnosticsMonitorPanel::itemPath(const QTreeWidgetItem *item) const {
  QStringList parts;
  const auto *current = item;
  while (current != nullptr) {
    parts.prepend(current->text(0));
    current = current->parent();
  }
  return parts.join("/");
}

QColor DiagnosticsMonitorPanel::colorFor(Severity severity) {
  switch (severity) {
  case Severity::Ok:
    return QColor("#15803d");
  case Severity::Warn:
    return QColor("#b45309");
  case Severity::Error:
    return QColor("#dc2626");
  case Severity::Stale:
    return QColor("#6b7280");
  }
  return QColor("#6b7280");
}

QString DiagnosticsMonitorPanel::ageText(
    std::chrono::steady_clock::time_point stamp,
    std::chrono::steady_clock::time_point now) {
  const auto age_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(now - stamp).count();
  if (age_ms < 1000) {
    return QString("%1 ms ago").arg(age_ms);
  }
  return QString("%1 s ago").arg(age_ms / 1000);
}

} // namespace rviz2_diagnostics_monitor

PLUGINLIB_EXPORT_CLASS(rviz2_diagnostics_monitor::DiagnosticsMonitorPanel,
                       rviz_common::Panel)
