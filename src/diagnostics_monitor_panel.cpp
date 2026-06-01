// Copyright 2026 Maik Knof
// SPDX-License-Identifier: Apache-2.0

#include "rviz2_diagnostics_monitor/diagnostics_monitor_panel.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sstream>

#include <QApplication>
#include <QAbstractItemView>
#include <QBrush>
#include <QCheckBox>
#include <QDialog>
#include <QFormLayout>
#include <QFrame>
#include <QGroupBox>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QItemSelectionModel>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QScrollBar>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStringList>
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

constexpr const char *kDefaultTopic = "/diagnostics";
constexpr int kEventSeverityRole = Qt::UserRole + 10;
constexpr int kEventAgeRole = Qt::UserRole + 11;
constexpr int kEventNameRole = Qt::UserRole + 12;
constexpr int kEventHardwareRole = Qt::UserRole + 13;
constexpr int kEventMessageRole = Qt::UserRole + 14;
constexpr int kEventValuesRole = Qt::UserRole + 15;
constexpr int kEventSequenceRole = Qt::UserRole + 16;

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

QColor blend(const QColor &base, const QColor &accent, double accent_weight) {
  const auto base_weight = 1.0 - accent_weight;
  return QColor(
      static_cast<int>(base.red() * base_weight + accent.red() * accent_weight),
      static_cast<int>(base.green() * base_weight + accent.green() * accent_weight),
      static_cast<int>(base.blue() * base_weight + accent.blue() * accent_weight));
}

QColor secondaryTextFor(const QPalette &palette) {
  return blend(palette.color(QPalette::Text), palette.color(QPalette::Base),
               0.38);
}

QLabel *eventLabel(const QString &text, const QString &object_name,
                   QWidget *parent) {
  auto *label = new QLabel(text, parent);
  label->setObjectName(object_name);
  label->setTextInteractionFlags(Qt::TextSelectableByMouse);
  label->setWordWrap(true);
  label->setMinimumWidth(0);
  return label;
}

QLabel *severityBadge(Severity severity, const QString &object_name,
                      QWidget *parent) {
  const auto severity_color = DiagnosticsMonitorPanel::colorFor(severity);
  auto *severity_label = eventLabel(
      qstr(DiagnosticModel::severityLabel(severity)), object_name, parent);
  severity_label->setWordWrap(false);
  severity_label->setAlignment(Qt::AlignCenter);
  severity_label->setStyleSheet(
      QString("font-weight: 700; color: %1; border: 1px solid %1; "
              "border-radius: 3px; padding: 1px 5px;")
          .arg(severity_color.name()));
  severity_label->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  return severity_label;
}

QString mainEventItemText(const DiagnosticEvent &event,
                          std::chrono::steady_clock::time_point now) {
  const auto hardware =
      event.snapshot.hardware_id.empty() ? std::string("-") : event.snapshot.hardware_id;
  return QString("[%1] %2 (%3) %4")
      .arg(DiagnosticsMonitorPanel::ageText(event.stamp, now))
      .arg(qstr(event.snapshot.name))
      .arg(qstr(hardware))
      .arg(qstr(event.snapshot.message));
}

QString detailEventItemText(const DiagnosticEvent &event,
                            std::chrono::steady_clock::time_point now) {
  return QString("%1   %2   %3")
      .arg(qstr(DiagnosticModel::severityLabel(event.snapshot.level)))
      .arg(DiagnosticsMonitorPanel::ageText(event.stamp, now))
      .arg(qstr(event.snapshot.message));
}

void setEventItemRoles(QListWidgetItem *item, const DiagnosticEvent &event,
                       std::chrono::steady_clock::time_point now) {
  item->setData(kEventSeverityRole,
                qstr(DiagnosticModel::severityLabel(event.snapshot.level)));
  item->setData(kEventAgeRole, DiagnosticsMonitorPanel::ageText(event.stamp, now));
  item->setData(kEventNameRole, qstr(event.snapshot.name));
  item->setData(
      kEventHardwareRole,
      qstr(event.snapshot.hardware_id.empty() ? std::string("-")
                                              : event.snapshot.hardware_id));
  item->setData(kEventMessageRole, qstr(event.snapshot.message));
  item->setData(kEventValuesRole, valuesText(event.snapshot.values));
  item->setData(kEventSequenceRole,
                QVariant::fromValue(static_cast<qulonglong>(event.sequence)));
}

void updateEventRowAge(QListWidgetItem *item,
                       std::chrono::steady_clock::time_point now) {
  if (item == nullptr || item->listWidget() == nullptr) {
    return;
  }
  auto *row = item->listWidget()->itemWidget(item);
  if (row == nullptr) {
    return;
  }
  auto *age = row->findChild<QLabel *>("event_age");
  if (age == nullptr) {
    const auto stamp =
        std::chrono::steady_clock::time_point(
            std::chrono::steady_clock::duration(
                item->data(Qt::UserRole + 1).toLongLong()));
    const auto age_text = DiagnosticsMonitorPanel::ageText(stamp, now);
    item->setData(kEventAgeRole, age_text);
    const auto mode = item->data(Qt::UserRole + 3).toString();
    if (mode == "main") {
      item->setText(QString("[%1] %2 (%3) %4")
                        .arg(age_text)
                        .arg(item->data(kEventNameRole).toString())
                        .arg(item->data(kEventHardwareRole).toString())
                        .arg(item->data(kEventMessageRole).toString()));
    } else if (mode == "detail") {
      item->setText(QString("%1   %2   %3")
                        .arg(item->data(kEventSeverityRole).toString())
                        .arg(age_text)
                        .arg(item->data(kEventMessageRole).toString()));
    }
    return;
  }
  const auto stamp =
      std::chrono::steady_clock::time_point(
          std::chrono::steady_clock::duration(
              item->data(Qt::UserRole + 1).toLongLong()));
  age->setText(DiagnosticsMonitorPanel::ageText(stamp, now));
}

void updateVisibleEventRowAges(QListWidget *list,
                               std::chrono::steady_clock::time_point now) {
  if (list == nullptr || list->count() == 0 || !list->isVisible()) {
    return;
  }

  const int top = std::max(0, list->indexAt(QPoint(0, 0)).row());
  const int bottom_index =
      list->indexAt(QPoint(0, std::max(0, list->viewport()->height() - 1))).row();
  const int bottom = bottom_index < 0 ? list->count() - 1 : bottom_index;
  for (int row = top; row <= bottom && row < list->count(); ++row) {
    updateEventRowAge(list->item(row), now);
  }
}

std::vector<DiagnosticEvent> newestRenderedEvents(
    const std::vector<DiagnosticEvent> &events) {
  return events;
}

} // namespace

class DiagnosticDetailDialog : public QDialog {
public:
  explicit DiagnosticDetailDialog(const std::string &id, QWidget *parent = nullptr)
      : QDialog(parent), id_(id) {
    setWindowModality(Qt::NonModal);
    resize(620, 520);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    timeline_ = new TimelineWidget(this);
    timeline_->setObjectName("diagnostic_detail_timeline");

    auto *header_row = new QWidget(this);
    auto *header_layout = new QHBoxLayout(header_row);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(6);
    severity_badge_ = severityBadge(Severity::Stale, "diagnostic_detail_severity",
                                    header_row);
    header_ = new QLabel(header_row);
    header_->setObjectName("diagnostic_detail_header");
    header_->setWordWrap(true);
    pause_button_ = new QPushButton("Pause Feed", header_row);
    pause_button_->setObjectName("diagnostic_detail_pause_button");
    pause_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    header_layout->addWidget(severity_badge_);
    header_layout->addWidget(header_, 1);
    header_layout->addWidget(pause_button_);

    events_ = new QListWidget(this);
    events_->setObjectName("diagnostic_detail_events");
    events_->setMinimumWidth(0);
    events_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
    events_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    events_->setSelectionMode(QAbstractItemView::SingleSelection);
    events_->setUniformItemSizes(false);
    events_->setSpacing(4);
    events_->setFrameShape(QFrame::NoFrame);
    events_->setStyleSheet(
        "QListWidget#diagnostic_detail_events::item { margin: 0; padding: 0; }");

    message_ = new QLabel(this);
    message_->setObjectName("diagnostic_detail_message");
    message_->setWordWrap(true);
    message_->setTextInteractionFlags(Qt::TextSelectableByMouse);

    values_ = new QTableWidget(0, 2, this);
    values_->setObjectName("diagnostic_detail_values");
    values_->setHorizontalHeaderLabels({"Key", "Value"});
    values_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    values_->setAlternatingRowColors(true);

    layout->addWidget(header_row);
    layout->addWidget(events_, 1);
    layout->addWidget(message_);
    layout->addWidget(values_, 1);
    layout->addWidget(timeline_);

    QObject::connect(events_, &QListWidget::currentRowChanged, this,
                     [this](int row) { showEventDetail(row); });
    timeline_->setClickCallback([this](auto stamp) {
      selectClosestEvent(stamp);
    });
    QObject::connect(pause_button_, &QPushButton::clicked, this, [this]() {
      paused_ = !paused_;
      pause_button_->setText(paused_ ? "Continue Feed" : "Pause Feed");
      if (!paused_) {
        event_row_signatures_.clear();
        events_->setCurrentRow(-1);
        refresh(last_snapshot_, last_history_, last_events_, last_now_,
                last_window_);
      }
    });
  }

  const std::string &id() const { return id_; }

  void refresh(const std::optional<DiagnosticSnapshot> &snapshot,
               std::vector<HistorySample> history,
               const std::vector<DiagnosticEvent> &events,
               std::chrono::steady_clock::time_point now,
               std::chrono::milliseconds window) {
    last_snapshot_ = snapshot;
    last_history_ = history;
    last_events_ = events;
    last_now_ = now;
    last_window_ = window;

    if (!snapshot) {
      setWindowTitle("Diagnostic unavailable [-]");
      header_->setText("Diagnostic is no longer available");
      severity_badge_->setText(qstr(DiagnosticModel::severityLabel(Severity::Stale)));
      severity_badge_->setStyleSheet(
          QString("font-weight: 700; color: %1; border: 1px solid %1; "
                  "border-radius: 3px; padding: 1px 5px;")
              .arg(DiagnosticsMonitorPanel::colorFor(Severity::Stale).name()));
      values_->setRowCount(0);
      message_->clear();
      timeline_->setSamples({}, now, window);
    } else {
      const auto hardware =
          snapshot->hardware_id.empty() ? std::string("-") : snapshot->hardware_id;
      setWindowTitle(qstr(snapshot->name + " [" + hardware + "]"));
      header_->setText(QString("Last update: %1%2")
                           .arg(DiagnosticsMonitorPanel::ageText(
                               snapshot->last_seen, now))
                           .arg(snapshot->locally_stale ? "   Locally stale" : ""));
      header_->setStyleSheet(
          QString("font-weight: 600; color: %1")
              .arg(palette().color(QPalette::Text).name()));
      updateSeverityBadge(snapshot->level);
      timeline_->setSamples(std::move(history), now, window);
    }

    if (paused_) {
      return;
    }

    const auto rendered_events = newestRenderedEvents(events);
    std::vector<std::string> signatures;
    signatures.reserve(rendered_events.size());
    for (const auto &event : rendered_events) {
      signatures.push_back(std::to_string(event.sequence) + "|" +
                           event.snapshot.id + "|" +
                           std::to_string(static_cast<int>(event.snapshot.level)) +
                           "|" + event.snapshot.message + "|" +
                           str(valuesText(event.snapshot.values)));
    }
    if (signatures == event_row_signatures_) {
      updateVisibleEventRowAges(events_, now);
      return;
    }
    event_row_signatures_ = std::move(signatures);

    const auto *selected_item = events_->currentItem();
    const QString selected_signature =
        selected_item == nullptr
            ? QString{}
            : selected_item->data(Qt::UserRole + 2).toString();
    const auto selected_sequence =
        selected_item == nullptr
            ? std::optional<std::uint64_t>{}
            : std::optional<std::uint64_t>{
                  selected_item->data(kEventSequenceRole).toULongLong()};
    const auto selected_stamp = selected_item == nullptr
                                    ? std::optional<std::chrono::steady_clock::time_point>{}
                                    : std::optional<std::chrono::steady_clock::time_point>{
                                          std::chrono::steady_clock::time_point(
                                              std::chrono::steady_clock::duration(
                                                  selected_item->data(Qt::UserRole + 1)
                                                      .toLongLong()))};
    const auto scroll_position = events_->verticalScrollBar()->value();

    events_->setUpdatesEnabled(false);
    events_->clear();
    int selected_row = -1;
    for (int row = 0; row < static_cast<int>(rendered_events.size()); ++row) {
      const auto &event = rendered_events[row];
      auto *item = new QListWidgetItem;
      const QString row_signature =
          QString("%1|%2|%3|%4")
              .arg(static_cast<qulonglong>(event.sequence))
              .arg(qstr(event.snapshot.id))
              .arg(static_cast<int>(event.snapshot.level))
              .arg(qstr(event.snapshot.message));
      item->setData(Qt::UserRole, row);
      item->setData(
          Qt::UserRole + 1,
          static_cast<qlonglong>(event.stamp.time_since_epoch().count()));
      item->setData(Qt::UserRole + 2, row_signature);
      item->setData(Qt::UserRole + 3, "detail");
      setEventItemRoles(item, event, now);
      item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) &
                     ~Qt::ItemIsEditable);
      events_->addItem(item);
      item->setText(detailEventItemText(event, now));
      item->setForeground(QBrush(DiagnosticsMonitorPanel::colorFor(
          event.snapshot.level)));
      if (!selected_signature.isEmpty() && selected_signature == row_signature) {
        selected_row = row;
      }
      if (selected_sequence && event.sequence == *selected_sequence) {
        selected_row = row;
      }
    }
    if (selected_row < 0 && events_->count() > 0) {
      if (selected_stamp) {
        selected_row = closestEventRow(rendered_events, *selected_stamp);
      } else {
        selected_row = 0;
      }
    }
    if (selected_row >= 0) {
      displayed_events_ = rendered_events;
      events_->setCurrentRow(selected_row);
      showEventDetail(selected_row);
      if (pending_timeline_selection_) {
        applyTimelineSelection(*pending_timeline_selection_);
        pending_timeline_selection_.reset();
      }
    } else {
      displayed_events_.clear();
      showEventDetail(-1);
    }
    events_->verticalScrollBar()->setValue(scroll_position);
    events_->setUpdatesEnabled(true);
  }

private:
  void updateSeverityBadge(Severity severity) {
    const auto severity_color = DiagnosticsMonitorPanel::colorFor(severity);
    severity_badge_->setText(qstr(DiagnosticModel::severityLabel(severity)));
    severity_badge_->setStyleSheet(
        QString("font-weight: 700; color: %1; border: 1px solid %1; "
                "border-radius: 3px; padding: 1px 5px;")
            .arg(severity_color.name()));
  }

  void showEventDetail(int row) {
    if (row < 0 || row >= static_cast<int>(displayed_events_.size())) {
      message_->clear();
      values_->setRowCount(0);
      timeline_->setSelectedStamp(std::nullopt);
      return;
    }
    const auto &event = displayed_events_[row];
    timeline_->setSelectedStamp(event.stamp);
    message_->setText(qstr(event.snapshot.message));
    values_->setRowCount(static_cast<int>(event.snapshot.values.size()));
    for (int value_row = 0;
         value_row < static_cast<int>(event.snapshot.values.size());
         ++value_row) {
      values_->setItem(value_row, 0,
                       new QTableWidgetItem(
                           qstr(event.snapshot.values[value_row].key)));
      values_->setItem(value_row, 1,
                       new QTableWidgetItem(
                           qstr(event.snapshot.values[value_row].value)));
    }
  }

  void selectClosestEvent(std::chrono::steady_clock::time_point stamp) {
    pending_timeline_selection_ = stamp;
    if (displayed_events_.empty()) {
      return;
    }
    applyTimelineSelection(stamp);
  }

  void applyTimelineSelection(std::chrono::steady_clock::time_point stamp) {
    const auto newest_stamp = displayed_events_.front().stamp;
    const auto oldest_stamp = displayed_events_.back().stamp;
    if (stamp > newest_stamp || stamp < oldest_stamp) {
      return;
    }

    const int closest_row = closestEventRow(displayed_events_, stamp);
    events_->setCurrentRow(closest_row);
    events_->scrollToItem(events_->item(closest_row),
                          QAbstractItemView::PositionAtCenter);
  }

  static int closestEventRow(
      const std::vector<DiagnosticEvent> &events,
      std::chrono::steady_clock::time_point stamp) {
    int closest_row = 0;
    auto closest_distance = events.front().stamp > stamp
                                ? events.front().stamp - stamp
                                : stamp - events.front().stamp;
    for (int row = 1; row < static_cast<int>(events.size()); ++row) {
      const auto distance = events[row].stamp > stamp
                                ? events[row].stamp - stamp
                                : stamp - events[row].stamp;
      if (distance < closest_distance) {
        closest_distance = distance;
        closest_row = row;
      }
    }
    return closest_row;
  }

  std::string id_;
  QLabel *header_{nullptr};
  QLabel *severity_badge_{nullptr};
  QPushButton *pause_button_{nullptr};
  TimelineWidget *timeline_{nullptr};
  QListWidget *events_{nullptr};
  QLabel *message_{nullptr};
  QTableWidget *values_{nullptr};
  bool paused_{false};
  std::optional<DiagnosticSnapshot> last_snapshot_;
  std::vector<HistorySample> last_history_;
  std::vector<DiagnosticEvent> last_events_;
  std::vector<DiagnosticEvent> displayed_events_;
  std::chrono::steady_clock::time_point last_now_{};
  std::chrono::milliseconds last_window_{std::chrono::minutes(10)};
  std::vector<std::string> event_row_signatures_;
  std::optional<std::chrono::steady_clock::time_point> pending_timeline_selection_;
};

class DiagnosticGroupDialog : public QDialog {
public:
  explicit DiagnosticGroupDialog(
      const QString &path, std::function<void(const std::string &)> open_detail,
      QWidget *parent = nullptr)
      : QDialog(parent), path_(path), open_detail_(std::move(open_detail)) {
    setWindowModality(Qt::NonModal);
    resize(760, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    auto *header_row = new QWidget(this);
    auto *header_layout = new QHBoxLayout(header_row);
    header_layout->setContentsMargins(0, 0, 0, 0);
    header_layout->setSpacing(6);
    severity_badge_ =
        severityBadge(Severity::Stale, "diagnostic_group_severity", header_row);
    header_ = new QLabel(header_row);
    header_->setObjectName("diagnostic_group_header");
    header_->setWordWrap(true);
    header_layout->addWidget(severity_badge_);
    header_layout->addWidget(header_, 1);

    diagnostics_ = new QTreeWidget(this);
    diagnostics_->setObjectName("diagnostic_group_values");
    diagnostics_->setHeaderLabels(
        {"Level", "Name", "Message", "Hardware ID", "Values"});
    diagnostics_->header()->setSectionResizeMode(QHeaderView::Interactive);
    diagnostics_->header()->setStretchLastSection(true);
    diagnostics_->setColumnWidth(0, 58);
    diagnostics_->setColumnWidth(1, 180);
    diagnostics_->setColumnWidth(2, 180);
    diagnostics_->setColumnWidth(3, 120);
    diagnostics_->setAlternatingRowColors(true);

    timeline_ = new TimelineWidget(this);
    timeline_->setObjectName("diagnostic_group_timeline");

    layout->addWidget(header_row);
    layout->addWidget(diagnostics_, 1);
    layout->addWidget(timeline_);

    QObject::connect(diagnostics_, &QTreeWidget::itemDoubleClicked, this,
                     [this](QTreeWidgetItem *item, int) {
      if (item == nullptr || !open_detail_) {
        return;
      }
      const auto id = str(item->data(0, Qt::UserRole).toString());
      if (!id.empty()) {
        QMetaObject::invokeMethod(parentWidget(), [callback = open_detail_, id]() {
          if (callback) {
            callback(id);
          }
        }, Qt::QueuedConnection);
      }
    });
  }

  const QString &path() const { return path_; }

  void refresh(const std::vector<DiagnosticSnapshot> &snapshots,
               std::vector<HistorySample> history,
               std::chrono::steady_clock::time_point now,
               std::chrono::milliseconds window) {
    setWindowTitle(QString("%1 [%2 diagnostics]")
                       .arg(path_)
                       .arg(static_cast<int>(snapshots.size())));
    if (snapshots.empty()) {
      header_->setText(
          QString("%1 diagnostics   No diagnostics currently match this group.")
              .arg(0));
      updateSeverityBadge(Severity::Stale);
      diagnostics_->clear();
      timeline_->setSamples(std::move(history), now, window);
      return;
    }

    Severity severity = Severity::Ok;
    for (const auto &snapshot : snapshots) {
      severity = DiagnosticModel::worst(severity, snapshot.level);
    }
    header_->setText(QString("%1 diagnostics")
                         .arg(static_cast<int>(snapshots.size())));
    header_->setStyleSheet(QString("font-weight: 600; color: %1")
                               .arg(palette().color(QPalette::Text).name()));
    updateSeverityBadge(severity);

    diagnostics_->clear();
    for (const auto &snapshot : snapshots) {
      const QStringList columns = {
          qstr(DiagnosticModel::severityLabel(snapshot.level)),
          qstr(snapshot.name),
          qstr(snapshot.message),
          qstr(snapshot.hardware_id.empty() ? "-" : snapshot.hardware_id),
          valuesText(snapshot.values),
      };
      auto *item = new QTreeWidgetItem(diagnostics_, columns);
      item->setData(0, Qt::UserRole, qstr(snapshot.id));
      for (int col = 0; col < columns.size(); ++col) {
        item->setFlags(item->flags() & ~Qt::ItemIsEditable);
        item->setForeground(
            col, QBrush(DiagnosticsMonitorPanel::colorFor(snapshot.level)));
      }
    }
    timeline_->setSamples(std::move(history), now, window);
  }

private:
  void updateSeverityBadge(Severity severity) {
    const auto severity_color = DiagnosticsMonitorPanel::colorFor(severity);
    severity_badge_->setText(qstr(DiagnosticModel::severityLabel(severity)));
    severity_badge_->setStyleSheet(
        QString("font-weight: 700; color: %1; border: 1px solid %1; "
                "border-radius: 3px; padding: 1px 5px;")
            .arg(severity_color.name()));
  }

  QString path_;
  std::function<void(const std::string &)> open_detail_;
  QLabel *header_{nullptr};
  QLabel *severity_badge_{nullptr};
  QTreeWidget *diagnostics_{nullptr};
  TimelineWidget *timeline_{nullptr};
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

void TimelineWidget::setSelectedStamp(
    std::optional<std::chrono::steady_clock::time_point> stamp) {
  selected_stamp_ = stamp;
  update();
}

void TimelineWidget::setClickCallback(
    std::function<void(std::chrono::steady_clock::time_point)> callback) {
  click_callback_ = std::move(callback);
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
  if (!click_callback_ || width() <= 1) {
    return;
  }

  const auto clamped_x = std::clamp(event->pos().x(), 0, width() - 1);
  const auto window = std::max(window_, std::chrono::milliseconds(1));
  const auto offset =
      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
          window * clamped_x / std::max(1, width() - 1));
  click_callback_(now_ - window + offset);
}

void TimelineWidget::paintEvent(QPaintEvent *) {
  QPainter painter(this);
  const auto widget_palette = palette();
  painter.fillRect(rect(), widget_palette.color(QPalette::Base));
  if (samples_.empty()) {
    painter.setPen(secondaryTextFor(widget_palette));
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
                           : secondaryTextFor(widget_palette);
    painter.setPen(color);
    painter.drawLine(x, 0, x, height());
  }

  if (selected_stamp_) {
    const auto start = now_ - window;
    if (*selected_stamp_ >= start && *selected_stamp_ <= now_) {
      const auto elapsed =
          std::chrono::duration_cast<std::chrono::milliseconds>(
              *selected_stamp_ - start);
      const auto x = static_cast<int>(
          elapsed.count() * std::max(1, width() - 1) /
          std::max<std::int64_t>(1, window.count()));
      QPen pen(QColor("#2563eb"));
      pen.setWidth(2);
      painter.setPen(pen);
      painter.drawLine(x, 0, x, height());
    }
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

QDialog *DiagnosticsMonitorPanel::groupDialogForTest(const QString &path) const {
  const auto it = group_dialogs_.find(path);
  if (it == group_dialogs_.end()) {
    return nullptr;
  }
  return it->second.data();
}

void DiagnosticsMonitorPanel::openGroupDialogForTest(const QString &path) {
  openGroupDialog(path);
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

QListWidget *DiagnosticsMonitorPanel::eventFeedListForTest() const {
  return event_list_;
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

  auto *event_control_row = new QWidget(event_filters);
  auto *event_control_layout = new QHBoxLayout(event_control_row);
  event_control_layout->setContentsMargins(0, 0, 0, 0);
  event_control_layout->setSpacing(6);
  event_pause_button_ = new QPushButton("Pause Feed", event_filters);
  event_pause_button_->setObjectName("event_feed_pause_button");
  event_pause_button_->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
  event_wrap_ = new QCheckBox("Wrap", event_filters);
  event_wrap_->setObjectName("event_feed_wrap");
  event_wrap_->setChecked(true);
  event_control_layout->addWidget(event_pause_button_);
  event_control_layout->addWidget(event_wrap_);
  event_control_layout->addStretch(1);
  event_filter_layout->addWidget(event_control_row);

  auto *event_text_filter_row = new QWidget(event_filters);
  auto *event_text_filter_layout = new QHBoxLayout(event_text_filter_row);
  event_text_filter_layout->setContentsMargins(0, 0, 0, 0);
  event_text_filter_layout->setSpacing(4);
  event_search_ = new QLineEdit(event_filters);
  event_search_->setPlaceholderText("Filter events");
  event_search_->setMinimumWidth(0);
  event_text_filter_layout->addWidget(event_search_, 1);
  event_filter_layout->addWidget(event_text_filter_row);
  events_layout->addWidget(event_filters);
  event_list_ = new QListWidget(events);
  event_list_->setObjectName("diagnostic_event_feed");
  event_list_->setMinimumWidth(0);
  event_list_->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Expanding);
  event_list_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
  event_list_->setSelectionMode(QAbstractItemView::SingleSelection);
  event_list_->setUniformItemSizes(true);
  event_list_->setWordWrap(event_wrap_->isChecked());
  event_list_->setSpacing(0);
  event_list_->setFrameShape(QFrame::NoFrame);
  event_list_->setStyleSheet(
      "QListWidget#diagnostic_event_feed::item { margin: 0; padding: 2px 4px; }");
  events_layout->addWidget(event_list_, 1);
  tabs_->addTab(events, "Event Feed");

  QObject::connect(event_search_, &QLineEdit::textChanged, this,
                   [this]() { refreshEvents(); });
  QObject::connect(event_wrap_, &QCheckBox::toggled, this, [this]() {
    event_list_->setWordWrap(event_wrap_->isChecked());
  });
  QObject::connect(event_pause_button_, &QPushButton::clicked, this, [this]() {
    event_feed_paused_ = !event_feed_paused_;
    event_pause_button_->setText(event_feed_paused_ ? "Continue Feed"
                                                    : "Pause Feed");
    if (!event_feed_paused_) {
      event_row_signatures_.clear();
      refreshEvents();
    }
  });
  QObject::connect(event_list_, &QListWidget::itemDoubleClicked, this,
                   [this](QListWidgetItem *item) {
                     const auto id = item == nullptr
                                         ? std::string{}
                                         : str(item->data(Qt::UserRole).toString());
                     if (!id.empty()) {
                       openDetailDialog(id);
                     }
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
  settings_layout->addWidget(history_group);
  settings_layout->addStretch(1);
  tabs_->addTab(settings_page, "Settings");

  QObject::connect(apply_topic, &QPushButton::clicked, this,
                   [this]() { rebuildSubscriptionIfReady(); });
  for (auto *spin : {stale_timeout_spin_, history_window_spin_}) {
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

    std::vector<std::string> signatures;
    signatures.reserve(tab_snapshots.size());
    for (const auto &snapshot : tab_snapshots) {
      signatures.push_back(snapshot.id + "|" +
                           std::to_string(static_cast<int>(snapshot.level)) +
                           "|" + snapshot.name + "|" + snapshot.hardware_id);
    }
    if (overview_tree_signatures_[name] == signatures) {
      continue;
    }
    overview_tree_signatures_[name] = std::move(signatures);

    widget->clear();
    const auto tree = treeForSnapshots(tab_snapshots);
    for (const auto &[_, child] : tree.children) {
      addTreeNode(widget, nullptr, child);
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
  if (event_feed_paused_) {
    return;
  }

  EventFilter filter;
  filter.show_ok = event_ok_->isChecked();
  filter.show_warn = event_warn_->isChecked();
  filter.show_error = event_error_->isChecked();
  filter.show_stale = event_stale_->isChecked();
  filter.hardware_id = {};
  filter.search = str(event_search_->text());

  std::vector<DiagnosticEvent> events;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    events = model_.filteredEvents(filter);
  }
  events = newestRenderedEvents(events);

  std::vector<std::string> signatures;
  signatures.reserve(events.size());
  for (const auto &event : events) {
    signatures.push_back(std::to_string(event.sequence) + "|" +
                         event.snapshot.id + "|" +
                         std::to_string(static_cast<int>(event.snapshot.level)) +
                         "|" + event.snapshot.name + "|" +
                         event.snapshot.hardware_id + "|" +
                         event.snapshot.message + "|" +
                         str(valuesText(event.snapshot.values)));
  }
  const auto now = std::chrono::steady_clock::now();
  if (signatures == event_row_signatures_) {
    updateVisibleEventRowAges(event_list_, now);
    return;
  }
  event_row_signatures_ = std::move(signatures);

  const auto *selected_item = event_list_->currentItem();
  const QString selected_id =
      selected_item == nullptr ? QString{} : selected_item->data(Qt::UserRole).toString();
  const auto scroll_position = event_list_->verticalScrollBar()->value();

  event_list_->setUpdatesEnabled(false);
  event_list_->clear();
  for (int row = 0; row < static_cast<int>(events.size()); ++row) {
    const auto &event = events[row];
    auto *item = new QListWidgetItem;
    item->setData(Qt::UserRole, qstr(event.snapshot.id));
    item->setData(
        Qt::UserRole + 1,
        static_cast<qlonglong>(event.stamp.time_since_epoch().count()));
    item->setData(Qt::UserRole + 3, "main");
    setEventItemRoles(item, event, now);
    item->setFlags((item->flags() | Qt::ItemIsSelectable | Qt::ItemIsEnabled) &
                   ~Qt::ItemIsEditable);
    event_list_->addItem(item);
    item->setText(mainEventItemText(event, now));
    item->setForeground(QBrush(DiagnosticsMonitorPanel::colorFor(
        event.snapshot.level)));
    if (!selected_id.isEmpty() && selected_id == qstr(event.snapshot.id)) {
      event_list_->setCurrentItem(item);
    }
  }
  event_list_->verticalScrollBar()->setValue(scroll_position);
  event_list_->setUpdatesEnabled(true);
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

  for (auto it = group_dialogs_.begin(); it != group_dialogs_.end();) {
    auto *dialog = it->second.data();
    if (dialog == nullptr) {
      it = group_dialogs_.erase(it);
      continue;
    }
    dialog->refresh(snapshotsForGroupPath(it->first), historyForGroupPath(it->first),
                    now, window);
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

  const auto now = std::chrono::steady_clock::now();
  const auto window = std::chrono::seconds(history_window_spin_->value());
  std::optional<DiagnosticSnapshot> snapshot;
  std::vector<HistorySample> history;
  std::vector<DiagnosticEvent> events;
  {
    std::lock_guard<std::mutex> lock(mutex_);
    snapshot = model_.snapshot(id, now);
    history = model_.historyFor(id);
    events = model_.eventsForDiagnostic(id);
  }
  dialog->refresh(snapshot, std::move(history), events, now, window);
  dialog->show();
  dialog->raise();
  dialog->activateWindow();
}

void DiagnosticsMonitorPanel::openGroupDialog(const QString &path) {
  auto &dialog = group_dialogs_[path];
  if (dialog.isNull()) {
    dialog = new DiagnosticGroupDialog(
        path, [this](const std::string &id) { openDetailDialog(id); }, this);
    QObject::connect(dialog.data(), &QObject::destroyed, this, [this, path]() {
      group_dialogs_.erase(path);
    });
  }
  dialog->refresh(snapshotsForGroupPath(path), historyForGroupPath(path),
                  std::chrono::steady_clock::now(),
                  std::chrono::seconds(history_window_spin_->value()));
  dialog->show();
}

void DiagnosticsMonitorPanel::addTreeNode(QTreeWidget *tree, QTreeWidgetItem *parent,
                                          const TreeNode &node) {
  auto *item = parent == nullptr
                   ? new QTreeWidgetItem(tree,
                                         {qstr(node.label), qstr(node.hardware_id)})
                   : new QTreeWidgetItem(
                         parent, {qstr(node.label), qstr(node.hardware_id)});
  item->setData(0, Qt::UserRole, qstr(node.diagnostic_id));
  setItemSeverity(item, node.severity);
  for (const auto &[_, child] : node.children) {
    addTreeNode(tree, item, child);
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
                       return;
                     }
                     openGroupDialog(itemPath(item));
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
