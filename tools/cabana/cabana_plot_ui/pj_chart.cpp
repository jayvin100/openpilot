#include "tools/cabana/cabana_plot_ui/pj_chart.h"
#include "tools/cabana/cabana_plot_ui/curve_list.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QGraphicsLayout>
#include <QMenu>
#include <QMimeData>
#include <QPainter>
#include <QRubberBand>
#include <QToolTip>
#include <QtCharts/QLegendMarker>

static const QColor kDefaultColors[] = {
    QColor(31, 119, 180),  QColor(255, 127, 14),  QColor(44, 160, 44),
    QColor(214, 39, 40),   QColor(148, 103, 189), QColor(140, 86, 75),
    QColor(227, 119, 194), QColor(127, 127, 127),  QColor(188, 189, 34),
    QColor(23, 190, 207),
};

static inline bool xLessThan(const QPointF &p, float x) { return p.x() < x; }

namespace cabana::plot_ui {

PjChartView::PjChartView(QWidget *parent) : QChartView(parent) {
  auto *ch = new QChart();
  ch->setBackgroundVisible(false);
  ch->legend()->layout()->setContentsMargins(0, 0, 0, 0);
  ch->legend()->setShowToolTips(true);
  ch->setMargins({0, 0, 0, 0});
  setChart(ch);

  axis_x_ = new QValueAxis(this);
  axis_y_ = new QValueAxis(this);
  ch->addAxis(axis_x_, Qt::AlignBottom);
  ch->addAxis(axis_y_, Qt::AlignLeft);
  axis_x_->setLineVisible(false);
  axis_y_->setLineVisible(false);

  // Match Cabana dark theme.
  ch->setTheme(QChart::ChartThemeDark);
  // Re-apply after theme (theme overrides pens).
  axis_x_->setTitleBrush(palette().text());
  axis_x_->setLabelsBrush(palette().text());
  axis_y_->setTitleBrush(palette().text());
  axis_y_->setLabelsBrush(palette().text());
  ch->legend()->setLabelColor(palette().color(QPalette::Text));
  axis_x_->setLineVisible(false);
  axis_y_->setLineVisible(false);

  // Grid lines — match PJ's dotted gray grid.
  axis_x_->setGridLineVisible(true);
  axis_y_->setGridLineVisible(true);
  axis_x_->setMinorGridLineVisible(true);
  axis_y_->setMinorGridLineVisible(true);
  QPen grid_pen(QColor(100, 100, 100), 0, Qt::DotLine);
  axis_x_->setGridLinePen(grid_pen);
  axis_y_->setGridLinePen(grid_pen);
  QPen minor_pen(QColor(60, 60, 60), 0, Qt::DotLine);
  axis_x_->setMinorGridLinePen(minor_pen);
  axis_y_->setMinorGridLinePen(minor_pen);

  setRubberBand(QChartView::HorizontalRubberBand);
  setMouseTracking(true);
  setAcceptDrops(true);
  viewport()->setAcceptDrops(true);

  tip_label_ = new QLabel(this);
  tip_label_->setStyleSheet("background: rgba(40,40,40,220); color: white; padding: 4px; border-radius: 3px;");
  tip_label_->hide();

  signal_value_font_.setPointSize(9);
}

void PjChartView::configure(const cabana::pj_layout::PlotModel &model) {
  // Remove old series.
  for (auto &ci : curves_) {
    chart()->removeSeries(ci.series);
    ci.series->deleteLater();
  }
  curves_.clear();
  transform_curves_.clear();

  xy_mode_ = (model.mode == "XYPlot");

  for (size_t i = 0; i < model.curves.size(); ++i) {
    const auto &binding = model.curves[i];
    QColor color;
    if (!binding.color.isEmpty()) {
      color = QColor(binding.color);
    } else {
      color = kDefaultColors[i % 10];
    }

    auto *series = createSeries(color);
    std::string name = binding.name.toStdString();
    series->setName(binding.name);

    CurveItem ci;
    ci.name = name;
    ci.series = series;
    ci.color = color;
    curves_.push_back(std::move(ci));

    if (binding.has_transform && !binding.transform.name.isEmpty()) {
      transform_curves_.insert(name);
    }
  }
}

QXYSeries *PjChartView::createSeries(QColor color) {
  QXYSeries *series;
  if (xy_mode_) {
    auto *scatter = new QScatterSeries(this);
    scatter->setMarkerSize(6);
    scatter->setColor(color);
    series = scatter;
  } else {
    auto *line = new QLineSeries(this);
    line->setPen(QPen(color, 1.3));
    series = line;
  }
  series->setColor(color);
  chart()->addSeries(series);
  series->attachAxis(axis_x_);
  series->attachAxis(axis_y_);
  return series;
}

void PjChartView::updateSnapshots(const cabana::pj_engine::PlotSnapshotBundle &bundle) {
  bool any_updated = false;

  for (auto &ci : curves_) {
    auto it = bundle.snapshots.find(ci.name);
    if (it == bundle.snapshots.end() || !it->second) continue;
    const auto &pts = it->second->points;
    if (ci.vals.size() == pts.size()) continue;  // no new data

    // Convert to relative time (subtract route start).
    ci.vals.resize(pts.size());
    for (size_t i = 0; i < pts.size(); ++i) {
      ci.vals[i] = QPointF(pts[i].x() - route_start_, pts[i].y());
    }
    ci.series->replace(QVector<QPointF>(ci.vals.begin(), ci.vals.end()));
    any_updated = true;
  }

  if (any_updated) {
    // Auto-scale X axis to fit all data.
    double min_x = std::numeric_limits<double>::max();
    double max_x = std::numeric_limits<double>::lowest();
    for (auto &ci : curves_) {
      if (!ci.vals.empty()) {
        min_x = std::min(min_x, ci.vals.front().x());
        max_x = std::max(max_x, ci.vals.back().x());
      }
    }
    if (min_x < max_x) {
      axis_x_->setRange(min_x, max_x);
      emit xRangeChanged(min_x, max_x);
    }
    updateAxisY();
    updateSeriesPoints();
    chart_pixmap_ = QPixmap();
    viewport()->update();
  }
}

void PjChartView::setTrackerTime(double time) {
  tracker_time_ = time - route_start_;  // convert to relative
  viewport()->update();
}

void PjChartView::setGridVisible(bool visible) {
  axis_x_->setGridLineVisible(visible);
  axis_y_->setGridLineVisible(visible);
}

void PjChartView::setLinkedXRange(double min, double max) {
  axis_x_->setRange(min, max);
  updateAxisY();
  updateSeriesPoints();
  chart_pixmap_ = QPixmap();
  viewport()->update();
}

void PjChartView::setRouteStart(double sec) {
  route_start_ = sec;
}

void PjChartView::resetZoom() {
  double min_x = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  for (auto &ci : curves_) {
    if (!ci.vals.empty()) {
      min_x = std::min(min_x, ci.vals.front().x());
      max_x = std::max(max_x, ci.vals.back().x());
    }
  }
  if (min_x < max_x) {
    axis_x_->setRange(min_x, max_x);
    updateAxisY();
    updateSeriesPoints();
    chart_pixmap_ = QPixmap();
    viewport()->update();
  }
}

void PjChartView::updateAxisY() {
  if (curves_.empty()) return;

  double min_y = std::numeric_limits<double>::max();
  double max_y = std::numeric_limits<double>::lowest();

  for (auto &ci : curves_) {
    if (!ci.series->isVisible() || ci.vals.empty()) continue;
    auto first = std::lower_bound(ci.vals.cbegin(), ci.vals.cend(), axis_x_->min(), xLessThan);
    auto last = std::lower_bound(first, ci.vals.cend(), axis_x_->max(), xLessThan);
    ci.min_y = std::numeric_limits<double>::max();
    ci.max_y = std::numeric_limits<double>::lowest();
    for (auto it = first; it != last; ++it) {
      if (it->y() < ci.min_y) ci.min_y = it->y();
      if (it->y() > ci.max_y) ci.max_y = it->y();
    }
    min_y = std::min(min_y, ci.min_y);
    max_y = std::max(max_y, ci.max_y);
  }

  if (min_y == std::numeric_limits<double>::max()) min_y = 0;
  if (max_y == std::numeric_limits<double>::lowest()) max_y = 0;

  double delta = std::abs(max_y - min_y) < 1e-3 ? 1 : (max_y - min_y) * 0.05;
  auto [nice_min, nice_max, ticks] = getNiceAxisNumbers(min_y - delta, max_y + delta, 3);
  axis_y_->setRange(nice_min, nice_max);
  axis_y_->setTickCount(ticks);
}

void PjChartView::updateSeriesPoints() {
  for (auto &ci : curves_) {
    auto begin = std::lower_bound(ci.vals.cbegin(), ci.vals.cend(), axis_x_->min(), xLessThan);
    auto end = std::lower_bound(begin, ci.vals.cend(), axis_x_->max(), xLessThan);
    if (begin != end) {
      int num_points = std::max<int>(end - begin, 1);
      QPointF right_pt = (end == ci.vals.cend()) ? ci.vals.back() : *end;
      double px_per_pt = (chart()->mapToPosition(right_pt).x() - chart()->mapToPosition(*begin).x()) / num_points;
      ci.series->setPointsVisible(num_points == 1 || px_per_pt > 20);
    }
  }
}

std::tuple<double, double, int> PjChartView::getNiceAxisNumbers(qreal min, qreal max, int tick_count) {
  qreal range = niceNumber(max - min, true);
  qreal step = niceNumber(range / (tick_count - 1), false);
  min = std::floor(min / step);
  max = std::ceil(max / step);
  tick_count = int(max - min) + 1;
  return {min * step, max * step, tick_count};
}

qreal PjChartView::niceNumber(qreal x, bool ceiling) {
  qreal z = std::pow(10, std::floor(std::log10(x)));
  qreal q = x / z;
  if (ceiling) {
    if (q <= 1.0) q = 1; else if (q <= 2.0) q = 2; else if (q <= 5.0) q = 5; else q = 10;
  } else {
    if (q < 1.5) q = 1; else if (q < 3.0) q = 2; else if (q < 7.0) q = 5; else q = 10;
  }
  return q * z;
}

void PjChartView::drawForeground(QPainter *painter, const QRectF &) {
  auto plot_area = chart()->plotArea();
  if (tracker_time_ > 0 && plot_area.width() > 0) {
    double x = chart()->mapToPosition({tracker_time_, 0}).x();
    if (x >= plot_area.left() && x <= plot_area.right()) {
      painter->setPen(QPen(Qt::red, 1, Qt::DashLine));
      painter->drawLine(QPointF(x, plot_area.top()), QPointF(x, plot_area.bottom()));

      // Draw signal values at tracker.
      painter->setFont(signal_value_font_);
      double y_offset = plot_area.top() + 2;
      for (auto &ci : curves_) {
        if (ci.vals.empty() || !ci.series->isVisible()) continue;
        auto it = std::lower_bound(ci.vals.begin(), ci.vals.end(), tracker_time_, xLessThan);
        if (it == ci.vals.end() && !ci.vals.empty()) it = std::prev(it);
        else if (it != ci.vals.begin() && it != ci.vals.end()) {
          auto prev = std::prev(it);
          if (std::abs(prev->x() - tracker_time_) < std::abs(it->x() - tracker_time_)) it = prev;
        }
        QString text = QString("%1: %2").arg(QString::fromStdString(ci.name).section('/', -1))
                                        .arg(it->y(), 0, 'g', 5);
        painter->setPen(ci.color);
        painter->drawText(QPointF(x + 4, y_offset + painter->fontMetrics().ascent()), text);
        y_offset += painter->fontMetrics().height();
      }
    }
  }
}

void PjChartView::paintEvent(QPaintEvent *event) {
  QChartView::paintEvent(event);
}

void PjChartView::resizeEvent(QResizeEvent *event) {
  QChartView::resizeEvent(event);
  chart_pixmap_ = QPixmap();
}

void PjChartView::contextMenuEvent(QContextMenuEvent *event) {
  QMenu menu(this);
  menu.addAction("Split Horizontal", this, [this]() { emit splitRequested(Qt::Horizontal); });
  menu.addAction("Split Vertical", this, [this]() { emit splitRequested(Qt::Vertical); });
  menu.addSeparator();
  menu.addAction("Zoom Out", this, [this]() { resetZoom(); });
  menu.addAction("Copy Image", this, [this]() {
    QApplication::clipboard()->setPixmap(grab());
  });
  menu.exec(event->globalPos());
}

void PjChartView::mousePressEvent(QMouseEvent *event) {
  QChartView::mousePressEvent(event);
}

void PjChartView::mouseReleaseEvent(QMouseEvent *event) {
  auto rubber = findChild<QRubberBand *>();
  if (event->button() == Qt::LeftButton && rubber && rubber->isVisible()) {
    rubber->hide();
    auto rect = rubber->geometry().normalized();
    double min = chart()->mapToValue(rect.topLeft()).x();
    double max = chart()->mapToValue(rect.bottomRight()).x();
    // Clamp to data bounds.
    double data_min = std::numeric_limits<double>::max();
    double data_max = std::numeric_limits<double>::lowest();
    for (const auto &ci : curves_) {
      if (!ci.vals.empty()) {
        data_min = std::min(data_min, ci.vals.front().x());
        data_max = std::max(data_max, ci.vals.back().x());
      }
    }
    min = std::max(min, data_min);
    max = std::min(max, data_max);

    if (rubber->width() <= 0) {
      emit seekRequested(min);
    } else if (rubber->width() > 10 && (max - min) > 0.01) {
      axis_x_->setRange(min, max);
      updateAxisY();
      updateSeriesPoints();
      emit xRangeChanged(min, max);
    }
    event->accept();
  } else {
    QChartView::mouseReleaseEvent(event);
  }
}

void PjChartView::mouseMoveEvent(QMouseEvent *event) {
  auto plot_area = chart()->plotArea();
  if (plot_area.contains(event->pos())) {
    showTip(chart()->mapToValue(event->pos()).x());
  } else {
    hideTip();
  }
  QChartView::mouseMoveEvent(event);
}

void PjChartView::showTip(double sec) {
  tooltip_x_ = chart()->mapToPosition({sec, 0}).x();
  QStringList lines;
  lines << QString::number(sec, 'f', 3);
  for (auto &ci : curves_) {
    if (!ci.series->isVisible() || ci.vals.empty()) continue;
    auto it = std::lower_bound(ci.vals.begin(), ci.vals.end(), sec, xLessThan);
    if (it == ci.vals.end() && !ci.vals.empty()) it = std::prev(it);
    else if (it != ci.vals.begin()) {
      auto prev = std::prev(it);
      if (std::abs(prev->x() - sec) < std::abs(it->x() - sec)) it = prev;
    }
    if (it != ci.vals.end()) {
      lines << QString("<span style=\"color:%1;\">■</span> %2: <b>%3</b>")
               .arg(ci.color.name())
               .arg(QString::fromStdString(ci.name).section('/', -1))
               .arg(it->y(), 0, 'g', 5);
    }
  }
  tip_label_->setText("<p style='white-space:pre'>" + lines.join("<br/>") + "</p>");
  tip_label_->adjustSize();

  QPoint pos(tooltip_x_ + 10, chart()->plotArea().top());
  if (pos.x() + tip_label_->width() > width()) {
    pos.setX(tooltip_x_ - tip_label_->width() - 10);
  }
  tip_label_->move(pos);
  tip_label_->show();
}

void PjChartView::hideTip() {
  tooltip_x_ = -1;
  tip_label_->hide();
}

void PjChartView::wheelEvent(QWheelEvent *event) {
  auto plot_area = chart()->plotArea();
  if (!plot_area.contains(event->position())) {
    QChartView::wheelEvent(event);
    return;
  }

  // Zoom X axis centered on cursor position.
  double cursor_x = chart()->mapToValue(event->position()).x();
  double min = axis_x_->min();
  double max = axis_x_->max();
  double range = max - min;

  double factor = event->angleDelta().y() > 0 ? 0.8 : 1.25;  // scroll up = zoom in
  double new_range = range * factor;
  if (new_range < 0.1) return;  // don't zoom past 100ms

  // Clamp to data bounds.
  double data_min = std::numeric_limits<double>::max();
  double data_max = std::numeric_limits<double>::lowest();
  for (const auto &ci : curves_) {
    if (!ci.vals.empty()) {
      data_min = std::min(data_min, ci.vals.front().x());
      data_max = std::max(data_max, ci.vals.back().x());
    }
  }
  if (data_min >= data_max) return;

  // Keep cursor position fixed — distribute the zoom proportionally.
  double ratio = (cursor_x - min) / range;
  double new_min = std::max(cursor_x - ratio * new_range, data_min);
  double new_max = std::min(cursor_x + (1.0 - ratio) * new_range, data_max);

  axis_x_->setRange(new_min, new_max);
  updateAxisY();
  updateSeriesPoints();
  emit xRangeChanged(new_min, new_max);
  chart_pixmap_ = QPixmap();
  viewport()->update();
  event->accept();
}

void PjChartView::dragEnterEvent(QDragEnterEvent *event) {
  if (event->mimeData()->hasFormat(CurveList::kCurveMimeType)) {
    event->acceptProposedAction();
  }
}

void PjChartView::dragMoveEvent(QDragMoveEvent *event) {
  if (event->mimeData()->hasFormat(CurveList::kCurveMimeType)) {
    event->acceptProposedAction();
  }
}

void PjChartView::dropEvent(QDropEvent *event) {
  QByteArray data = event->mimeData()->data(CurveList::kCurveMimeType);
  for (const QString &name : QString::fromUtf8(data).split('\n', Qt::SkipEmptyParts)) {
    emit curveDropped(name.trimmed());
  }
  event->acceptProposedAction();
}

}  // namespace cabana::plot_ui
