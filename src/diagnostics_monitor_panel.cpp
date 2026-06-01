// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include "rviz2_diagnostics_monitor/diagnostics_monitor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <map>
#include <optional>
#include <sstream>

#include <QApplication>
#include <QBrush>
#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMetaObject>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
#include <QFontDatabase>
#include <QFontInfo>
#include <QSyntaxHighlighter>
#include <QTabWidget>
#include <QTableWidget>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTimer>
#include <QTreeWidget>
#include <QVBoxLayout>

#include <pluginlib/class_list_macros.hpp>
#include <rviz_common/config.hpp>
#include <rviz_common/display_context.hpp>

namespace rviz2_diagnostics_monitor {
namespace {

constexpr const char *kDefaultTopic = "/diagnostics";

QString qstr(const std::string &text) {
  return QString::fromStdString(text);
}

std::string str(const QString &text) {
  return text.toStdString();
}

QString valuesText(const std::vector<DiagnosticValue> &values) {
  std::stringstream stream;
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i > 0) {
      stream << "; ";
    }
    stream << values[i].key << "=" << values[i].value;
  }
  return qstr(stream.str());
}

QString padded(const QString &text, const int width) {
  return text.leftJustified(width, ' ', true);
}

QFont logFont() {
  QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
  if (!QFontInfo(font).fixedPitch()) {
    const QStringList candidates = {
        "DejaVu Sans Mono", "Liberation Mono", "Ubuntu Mono", "Monospace"};
    for (const auto &family : candidates) {
      QFont candidate(family);
      candidate.setStyleHint(QFont::Monospace);
      candidate.setFixedPitch(true);
      if (QFontInfo(candidate).fixedPitch()) {
        font = candidate;
        break;
      }
    }
  }
  font.setStyleHint(QFont::Monospace);
  font.setFixedPitch(true);
  return font;
}

class EventFeedHighlighter : public QSyntaxHighlighter {
public:
  explicit EventFeedHighlighter(QTextDocument *parent) : QSyntaxHighlighter(parent) {}

protected:
  void highlightBlock(const QString &text) override {
    QTextCharFormat format;
    if (text.contains("ERROR")) {
      format.setForeground(DiagnosticsMonitorPanel::colorFor(Severity::Error));
    } else if (text.contains("WARN ")) {
      format.setForeground(DiagnosticsMonitorPanel::colorFor(Severity::Warn));
    } else if (text.contains("STALE")) {
      format.setForeground(DiagnosticsMonitorPanel::colorFor(Severity::Stale));
    } else {
      format.setForeground(DiagnosticsMonitorPanel::colorFor(Severity::Ok));
    }
    setFormat(0, text.size(), format);
  }
};

} // namespace

class DiagnosticDetailDialog : public QDialog {
public:
  explicit DiagnosticDetailDialog(const std::string &id, QWidget *parent = nullptr)
      : QDialog(parent), id_(id) {
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowModality(Qt::NonModal);
    resize(620, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    header_ = new QLabel(this);
    header_->setWordWrap(true);
    timeline_ = new TimelineWidget(this);
    values_ = new QTableWidget(0, 2, this);
    values_->setObjectName("diagnostic_detail_values");
    values_->setHorizontalHeaderLabels({"Key", "Value"});
    values_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    values_->setAlternatingRowColors(true);

    events_ = new QTableWidget(0, 4, this);
    events_->setObjectName("diagnostic_detail_events");
    events_->setHorizontalHeaderLabels({"Age", "Level", "Message", "Values"});
    events_->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    events_->horizontalHeader()->setStretchLastSection(true);
    events_->setColumnWidth(0, 80);
    events_->setColumnWidth(1, 58);
    events_->setColumnWidth(2, 180);
    events_->setAlternatingRowColors(true);

    layout->addWidget(header_);
    layout->addWidget(timeline_);
    layout->addWidget(values_, 1);
    layout->addWidget(events_, 1);
  }

  const std::string &id() const { return id_; }

  void refresh(const std::optional<DiagnosticSnapshot> &snapshot,
               std::vector<HistorySample> history,
               const std::vector<DiagnosticEvent> &events,
               std::chrono::steady_clock::time_point now,
               std::chrono::milliseconds window) {
    if (!snapshot) {
      setWindowTitle("Diagnostic unavailable [-]");
      header_->setText("Diagnostic is no longer available");
      values_->setRowCount(0);
      timeline_->setSamples({}, now, window);
    } else {
      const auto hardware =
          snapshot->hardware_id.empty() ? std::string("-") : snapshot->hardware_id;
      setWindowTitle(qstr(snapshot->name + " [" + hardware + "]"));
      header_->setText(
          QString("%1\nHardware: %2   Severity: %3\nMessage: %4\nLast update: %5%6")
              .arg(qstr(snapshot->name))
              .arg(qstr(hardware))
              .arg(qstr(DiagnosticModel::severityLabel(snapshot->level)))
              .arg(qstr(snapshot->message))
              .arg(DiagnosticsMonitorPanel::ageText(snapshot->last_seen, now))
              .arg(snapshot->locally_stale ? "   Locally stale" : ""));
      header_->setStyleSheet(
          QString("font-weight: 600; color: %1")
              .arg(DiagnosticsMonitorPanel::colorFor(snapshot->level).name()));
      values_->setRowCount(static_cast<int>(snapshot->values.size()));
      for (int row = 0; row < static_cast<int>(snapshot->values.size()); ++row) {
        values_->setItem(row, 0,
                         new QTableWidgetItem(qstr(snapshot->values[row].key)));
        values_->setItem(row, 1,
                         new QTableWidgetItem(qstr(snapshot->values[row].value)));
      }
      timeline_->setSamples(std::move(history), now, window);
    }

    events_->setRowCount(static_cast<int>(events.size()));
    for (int row = 0; row < static_cast<int>(events.size()); ++row) {
      const auto &event = events[row];
      const QStringList columns = {
          DiagnosticsMonitorPanel::ageText(event.stamp, now),
          qstr(DiagnosticModel::severityLabel(event.snapshot.level)),
          qstr(event.snapshot.message),
          valuesText(event.snapshot.values),
      };
      for (int col = 0; col < columns.size(); ++col) {
        auto *item = new QTableWidgetItem(columns[col]);
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setForeground(
            QBrush(DiagnosticsMonitorPanel::colorFor(event.snapshot.level)));
        events_->setItem(row, col, item);
      }
    }
  }

private:
  std::string id_;
  QLabel *header_{nullptr};
  TimelineWidget *timeline_{nullptr};
  QTableWidget *values_{nullptr};
  QTableWidget *events_{nullptr};
};

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
  setMinimumHeight(26);
}

void TimelineWidget::setSamples(std::vector<HistorySample> samples,
                                std::chrono::steady_clock::time_point now,
                                std::chrono::milliseconds window) {
  samples_ = std::move(samples);
  now_ = now;
  window_ = window;
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

  const auto window = std::max(window_, std::chrono::milliseconds(1));
  const auto start = now_ - window;
  std::size_t sample_index = 0;
  for (int x = 0; x < width(); ++x) {
    const auto offset = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        window * x / std::max(1, width() - 1));
    const auto bucket_time = start + offset;
    while (sample_index + 1 < samples_.size() &&
           samples_[sample_index + 1].stamp <= bucket_time) {
      ++sample_index;
    }
    const auto color = samples_[sample_index].stamp <= bucket_time
                           ? DiagnosticsMonitorPanel::colorFor(samples_[sample_index].level)
                           : QColor("#9ca3af");
    painter.setPen(color);
    painter.drawLine(x, 0, x, height());
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
  if (config.mapGetBool("Wrap Event Feed", &bool_value)) {
    event_wrap_->setChecked(bool_value);
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
  config.mapSetValue("Wrap Event Feed", event_wrap_->isChecked());
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

void DiagnosticsMonitorPanel::ingestForTest(
    const diagnostic_msgs::msg::DiagnosticArray &message) {
  std::lock_guard<std::mutex> lock(mutex_);
  model_.ingest(message, std::chrono::steady_clock::now());
}

void DiagnosticsMonitorPanel::refreshForTest() { refreshUi(); }

QTreeWidget *DiagnosticsMonitorPanel::overviewTreeForTest() const {
  return overviewTreeForTest("All");
}

QTreeWidget *DiagnosticsMonitorPanel::overviewTreeForTest(const QString &tab) const {
  const auto it = overview_trees_.find(str(tab));
  return it == overview_trees_.end() ? nullptr : it->second;
}

QTabWidget *DiagnosticsMonitorPanel::overviewTabsForTest() const {
  return overview_tabs_;
}

QLineEdit *DiagnosticsMonitorPanel::overviewSearchForTest() const {
  return overview_search_;
}

QDialog *DiagnosticsMonitorPanel::detailDialogForTest(const std::string &id) const {
  const auto it = detail_dialogs_.find(id);
  if (it == detail_dialogs_.end()) {
    return nullptr;
  }
  return it->second.data();
}

int DiagnosticsMonitorPanel::detailDialogCountForTest() const {
  int count = 0;
  for (const auto &[_, dialog] : detail_dialogs_) {
    if (!dialog.isNull()) {
      ++count;
    }
  }
  return count;
}

QPlainTextEdit *DiagnosticsMonitorPanel::eventFeedViewForTest() const {
  return event_view_;
}

std::vector<HistorySample>
DiagnosticsMonitorPanel::groupHistoryForTest(const QString &path) {
  return historyForGroupPath(path);
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
  overview_tabs_ = new QTabWidget(overview);
  for (const auto &name : {"All", "Errors", "Warnings", "Stale"}) {
    auto *tree = new QTreeWidget(overview_tabs_);
    configureOverviewTree(tree);
    connectOverviewTree(tree);
    overview_trees_.emplace(name, tree);
    overview_tabs_->addTab(tree, name);
  }

  overview_layout->addWidget(overview_search_);
  overview_layout->addWidget(summary_label_);
  overview_layout->addWidget(overall_timeline_);
  overview_layout->addWidget(overview_tabs_, 1);
  tabs_->addTab(overview, "Overview");

  QObject::connect(overview_search_, &QLineEdit::textChanged, this,
                   [this]() { refreshUi(); });

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
  auto *event_display_row = new QWidget(event_filters);
  auto *event_display_layout = new QHBoxLayout(event_display_row);
  event_display_layout->setContentsMargins(0, 0, 0, 0);
  event_display_layout->setSpacing(6);
  event_wrap_ = new QCheckBox("Wrap", event_display_row);
  event_wrap_->setChecked(true);
  event_display_layout->addWidget(event_wrap_);
  event_display_layout->addStretch(1);
  event_filter_layout->addWidget(event_display_row);
  events_layout->addWidget(event_filters);
  event_view_ = new QPlainTextEdit(events);
  event_view_->setObjectName("diagnostic_event_feed");
  event_view_->setReadOnly(true);
  const QFont fixed_font = logFont();
  event_view_->setFont(fixed_font);
  event_view_->document()->setDefaultFont(fixed_font);
  event_view_->setStyleSheet(
      "QPlainTextEdit { font-family: 'DejaVu Sans Mono', 'Liberation Mono', "
      "'Ubuntu Mono', monospace; }");
  event_view_->setLineWrapMode(QPlainTextEdit::WidgetWidth);
  new EventFeedHighlighter(event_view_->document());
  events_layout->addWidget(event_view_, 1);
  tabs_->addTab(events, "Event Feed");

  QObject::connect(event_hardware_filter_, &QLineEdit::textChanged, this,
                   [this]() { refreshEvents(); });
  QObject::connect(event_search_, &QLineEdit::textChanged, this,
                   [this]() { refreshEvents(); });
  QObject::connect(event_wrap_, &QCheckBox::toggled, this, [this](bool checked) {
    event_view_->setLineWrapMode(checked ? QPlainTextEdit::WidgetWidth
                                         : QPlainTextEdit::NoWrap);
  });

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
  const auto timeline_now = std::chrono::steady_clock::now();
  const auto timeline_window = std::chrono::seconds(history_window_spin_->value());
  overall_timeline_->setSamples(
      std::move(overall_history), timeline_now, timeline_window);
  refreshOverview(snapshots);
  refreshEvents();
  refreshDetailDialogs();
}

void DiagnosticsMonitorPanel::refreshOverview(
    const std::vector<DiagnosticSnapshot> &snapshots) {
  const auto search = str(overview_search_->text());
  const auto expanded_paths = expandedItemPaths();
  std::map<std::string, int> scroll_positions;
  for (const auto &[name, tree] : overview_trees_) {
    scroll_positions[name] = tree->verticalScrollBar()->value();
  }
  const auto overview_snapshots = aggregateOverviewSnapshots(snapshots);
  std::vector<DiagnosticSnapshot> filtered;
  std::copy_if(
      overview_snapshots.begin(), overview_snapshots.end(),
      std::back_inserter(filtered), [&search](const auto &snapshot) {
        return DiagnosticModel::matchesSearch(snapshot, search);
      });

  const std::map<std::string, std::optional<Severity>> tab_filters = {
      {"All", std::nullopt},
      {"Errors", Severity::Error},
      {"Warnings", Severity::Warn},
      {"Stale", Severity::Stale},
  };

  for (const auto &[name, severity] : tab_filters) {
    auto *widget = overview_trees_.at(name);
    std::vector<DiagnosticSnapshot> tab_snapshots;
    std::copy_if(filtered.begin(), filtered.end(), std::back_inserter(tab_snapshots),
                 [severity](const auto &snapshot) {
                   return !severity.has_value() || snapshot.level == *severity;
                 });

    widget->clear();
    const auto tree = treeForSnapshots(tab_snapshots);
    auto *root = new QTreeWidgetItem(widget, {qstr(tree.label)});
    setItemSeverity(root, tree.severity);
    for (const auto &[_, child] : tree.children) {
      addTreeNode(root, child);
    }
  }

  restoreExpandedItemPaths(expanded_paths);
  const bool had_expanded_items =
      std::any_of(expanded_paths.begin(), expanded_paths.end(),
                  [](const auto &entry) { return !entry.second.empty(); });
  if (!had_expanded_items) {
    for (const auto &[_, tree] : overview_trees_) {
      tree->expandToDepth(1);
    }
  }

  for (const auto &[name, tree] : overview_trees_) {
    tree->verticalScrollBar()->setValue(scroll_positions[name]);
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

  QString text;
  const auto now = std::chrono::steady_clock::now();
  bool inserted_any = false;
  for (const auto &event : events) {
    const QString prefix =
        padded(ageText(event.stamp, now), 12) + "  " +
        padded(qstr(DiagnosticModel::severityLabel(event.snapshot.level)), 5) +
        "  " + padded(qstr(event.snapshot.hardware_id.empty()
                               ? "-"
                               : event.snapshot.hardware_id),
                       18) +
        "  " + padded(qstr(event.snapshot.name), 34) + "  ";
    QString message = qstr(event.snapshot.message);
    message.replace('\t', "  ");
    message.replace('\n', "\n" + QString(prefix.size(), ' '));
    text += prefix + message;
    const auto values = valuesText(event.snapshot.values);
    if (!values.isEmpty()) {
      text += "\n" + QString(prefix.size(), ' ') + values;
    }
    text += "\n";
    inserted_any = true;
  }

  if (!inserted_any) {
    text = "No diagnostic events match the current filters.";
  }

  event_view_->setUpdatesEnabled(false);
  event_view_->setPlainText(text.trimmed());
  event_view_->moveCursor(QTextCursor::End);
  event_view_->setUpdatesEnabled(true);
}

void DiagnosticsMonitorPanel::refreshDetailDialogs() {
  const auto now = std::chrono::steady_clock::now();
  const auto window = std::chrono::seconds(history_window_spin_->value());
  for (auto it = detail_dialogs_.begin(); it != detail_dialogs_.end();) {
    auto *dialog = it->second.data();
    if (dialog == nullptr) {
      it = detail_dialogs_.erase(it);
      continue;
    }

    std::optional<DiagnosticSnapshot> snapshot;
    std::vector<HistorySample> history;
    std::vector<DiagnosticEvent> events;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      snapshot = model_.snapshot(it->first, now);
      history = model_.historyFor(it->first);
      events = model_.eventsForDiagnostic(it->first);
    }
    dialog->refresh(snapshot, std::move(history), events, now, window);
    ++it;
  }
}

void DiagnosticsMonitorPanel::openDetailDialog(const std::string &id) {
  auto &dialog = detail_dialogs_[id];
  if (dialog.isNull()) {
    dialog = new DiagnosticDetailDialog(id, this);
    QObject::connect(dialog.data(), &QObject::destroyed, this, [this, id]() {
      detail_dialogs_.erase(id);
    });
  }
  refreshDetailDialogs();
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void DiagnosticsMonitorPanel::addTreeNode(QTreeWidgetItem *parent,
                                          const TreeNode &node) {
  auto *item = new QTreeWidgetItem(
      parent, {qstr(node.label), qstr(node.hardware_id)});
  item->setData(0, Qt::UserRole, qstr(node.diagnostic_id));
  setItemSeverity(item, node.severity);
  for (const auto &[_, child] : node.children) {
    addTreeNode(item, child);
  }
}

void DiagnosticsMonitorPanel::configureOverviewTree(QTreeWidget *tree) {
  tree->setHeaderLabels({"Device", "Hardware ID"});
  tree->setMinimumWidth(0);
  tree->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
  tree->header()->setStretchLastSection(false);
  tree->header()->setSectionResizeMode(QHeaderView::Interactive);
  tree->setColumnWidth(0, 230);
  tree->setColumnWidth(1, 130);
  tree->setAlternatingRowColors(true);
}

void DiagnosticsMonitorPanel::connectOverviewTree(QTreeWidget *tree) {
  QObject::connect(tree, &QTreeWidget::itemDoubleClicked, this,
                   [this](QTreeWidgetItem *item, int) {
                     const auto id = str(item->data(0, Qt::UserRole).toString());
                     if (!id.empty()) {
                       openDetailDialog(id);
                     }
                   });
}

void DiagnosticsMonitorPanel::setItemSeverity(QTreeWidgetItem *item,
                                              Severity severity) {
  const auto color = colorFor(severity);
  for (int col = 0; col < item->columnCount(); ++col) {
    item->setForeground(col, QBrush(color));
  }
}

void DiagnosticsMonitorPanel::rebuildSubscriptionIfReady() {
  applySettingsFromControls();
  if (!node_) {
    return;
  }
  subscribe();
}

TreeNode DiagnosticsMonitorPanel::treeForSnapshots(
    const std::vector<DiagnosticSnapshot> &snapshots) const {
  TreeNode root;
  root.label = "All Devices";
  root.severity = Severity::Ok;

  for (const auto &snapshot : snapshots) {
    auto *node = &root;
    node->severity = DiagnosticModel::worst(node->severity, snapshot.level);
    for (const auto &part : DiagnosticModel::splitDiagnosticName(snapshot.name)) {
      auto [child, _] = node->children.emplace(part, TreeNode{});
      child->second.label = part;
      child->second.severity =
          DiagnosticModel::worst(child->second.severity, snapshot.level);
      node = &child->second;
    }
    node->diagnostic_id = snapshot.id;
    node->hardware_id = snapshot.hardware_id;
    node->severity = DiagnosticModel::worst(node->severity, snapshot.level);
  }

  return root;
}

std::map<std::string, std::set<QString>>
DiagnosticsMonitorPanel::expandedItemPaths() const {
  std::map<std::string, std::set<QString>> paths_by_tab;
  for (const auto &[name, tree] : overview_trees_) {
    auto &paths = paths_by_tab[name];
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
      const auto *root = tree->topLevelItem(i);
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
  }
  return paths_by_tab;
}

void DiagnosticsMonitorPanel::restoreExpandedItemPaths(
    const std::map<std::string, std::set<QString>> &expanded_paths) {
  for (const auto &[name, tree] : overview_trees_) {
    const auto paths_it = expanded_paths.find(name);
    const std::set<QString> empty_paths;
    const auto &paths =
        paths_it == expanded_paths.end() ? empty_paths : paths_it->second;
    for (int i = 0; i < tree->topLevelItemCount(); ++i) {
      auto *root = tree->topLevelItem(i);
      QList<QTreeWidgetItem *> stack;
      stack.push_back(root);
      while (!stack.empty()) {
        auto *item = stack.takeLast();
        item->setExpanded(paths.count(itemPath(item)) > 0);
        for (int child = 0; child < item->childCount(); ++child) {
          stack.push_back(item->child(child));
        }
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

std::vector<DiagnosticSnapshot>
DiagnosticsMonitorPanel::snapshotsForGroupPath(const QString &path) {
  std::vector<DiagnosticSnapshot> snapshots;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshots = model_.snapshots(std::chrono::steady_clock::now());
  }

  QString prefix = path;
  if (prefix.startsWith("All Devices/")) {
    prefix.remove(0, QString("All Devices/").size());
  } else if (prefix == "All Devices") {
    prefix.clear();
  }

  std::vector<DiagnosticSnapshot> result;
  for (const auto &snapshot : snapshots) {
    const auto name = qstr(snapshot.name);
    if (prefix.isEmpty() || name == prefix || name.startsWith(prefix + "/")) {
      result.push_back(snapshot);
    }
  }
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    if (left.level != right.level) {
      return static_cast<int>(left.level) > static_cast<int>(right.level);
    }
    return left.name < right.name;
  });
  return result;
}

std::vector<HistorySample>
DiagnosticsMonitorPanel::historyForGroupPath(const QString &path) {
  const auto snapshots = snapshotsForGroupPath(path);
  if (snapshots.empty()) {
    return {};
  }

  struct MemberSample {
    std::string id;
    HistorySample sample;
  };

  std::vector<MemberSample> samples;
  for (const auto &snapshot : snapshots) {
    std::vector<HistorySample> history;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      history = model_.historyFor(snapshot.id);
    }
    for (const auto &sample : history) {
      samples.push_back({snapshot.id, sample});
    }
  }
  std::sort(samples.begin(), samples.end(), [](const auto &left, const auto &right) {
    return left.sample.stamp < right.sample.stamp;
  });

  std::map<std::string, Severity> current_levels;
  std::vector<HistorySample> merged;
  for (const auto &member_sample : samples) {
    current_levels[member_sample.id] = member_sample.sample.level;

    Severity group_level = Severity::Ok;
    for (const auto &[_, level] : current_levels) {
      group_level = DiagnosticModel::worst(group_level, level);
    }

    if (!merged.empty() && merged.back().stamp == member_sample.sample.stamp) {
      merged.back().level = DiagnosticModel::worst(merged.back().level, group_level);
      continue;
    }
    merged.push_back({member_sample.sample.stamp, group_level});
  }
  return merged;
}

std::vector<DiagnosticSnapshot>
DiagnosticsMonitorPanel::aggregateOverviewSnapshots(
    const std::vector<DiagnosticSnapshot> &snapshots) {
  std::map<std::string, DiagnosticSnapshot> aggregated;

  for (const auto &snapshot : snapshots) {
    auto [it, inserted] = aggregated.emplace(snapshot.name, snapshot);
    if (inserted) {
      if (it->second.hardware_id.empty()) {
        it->second.hardware_id = "-";
      }
      continue;
    }

    auto &representative = it->second;
    const bool snapshot_is_worse =
        DiagnosticModel::worst(representative.level, snapshot.level) ==
            snapshot.level &&
        representative.level != snapshot.level;

    if (snapshot_is_worse) {
      const auto display_hardware_id =
          snapshot.hardware_id.empty() ? representative.hardware_id
                                       : snapshot.hardware_id;
      representative = snapshot;
      representative.hardware_id = display_hardware_id.empty() ? "-"
                                                               : display_hardware_id;
      continue;
    }

    if (representative.hardware_id == "-" && !snapshot.hardware_id.empty()) {
      representative.hardware_id = snapshot.hardware_id;
    }
  }

  std::vector<DiagnosticSnapshot> result;
  result.reserve(aggregated.size());
  for (auto &[_, snapshot] : aggregated) {
    result.push_back(std::move(snapshot));
  }
  std::sort(result.begin(), result.end(), [](const auto &left, const auto &right) {
    if (left.level != right.level) {
      return static_cast<int>(left.level) > static_cast<int>(right.level);
    }
    return left.name < right.name;
  });
  return result;
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
