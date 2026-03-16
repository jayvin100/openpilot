#include "tools/cabana/cabana_plot_ui/timeline_widget.h"

#include <algorithm>

#include <QMouseEvent>
#include <QPainter>

namespace cabana::plot_ui {

static const QColor kBarColor(111, 143, 175);
static const QColor kLoadedColor(0, 163, 108);

TimelineWidget::TimelineWidget(QWidget *parent) : QWidget(parent) {
  setFixedHeight(24);
  setMouseTracking(true);
  setCursor(Qt::PointingHandCursor);
}

void TimelineWidget::setTimeRange(double min, double max) {
  range_min_ = min;
  range_max_ = max;
  update();
}

void TimelineWidget::setCurrentTime(double sec) {
  current_time_ = sec;
  update();
}

void TimelineWidget::setZoomRange(double min, double max) {
  zoom_min_ = min;
  zoom_max_ = max;
  update();
}

double TimelineWidget::timeAtX(int x) const {
  double range = range_max_ - range_min_;
  if (range <= 0 || width() <= 0) return range_min_;
  return range_min_ + (static_cast<double>(x) / width()) * range;
}

void TimelineWidget::paintEvent(QPaintEvent *) {
  QPainter p(this);
  p.setRenderHint(QPainter::Antialiasing);

  double range = range_max_ - range_min_;
  if (range <= 0) {
    p.fillRect(rect(), kBarColor);
    return;
  }

  QRect bar = rect().adjusted(0, 2, 0, -2);

  // Background — full route range.
  p.fillRect(bar, kBarColor);

  // Fill loaded portion (up to current time).
  if (current_time_ > range_min_) {
    int loaded_x = static_cast<int>(((current_time_ - range_min_) / range) * width());
    loaded_x = std::clamp(loaded_x, 0, width());
    QRect loaded_rect = bar;
    loaded_rect.setRight(loaded_x);
    p.fillRect(loaded_rect, kLoadedColor);
  }

  // Zoom range overlay — highlight the visible plot range.
  if (zoom_min_ >= range_min_ && zoom_max_ > zoom_min_) {
    int zx1 = static_cast<int>(((zoom_min_ - range_min_) / range) * width());
    int zx2 = static_cast<int>(((zoom_max_ - range_min_) / range) * width());
    zx1 = std::clamp(zx1, 0, width());
    zx2 = std::clamp(zx2, 0, width());

    // Dim areas outside zoom.
    QColor dim(0, 0, 0, 120);
    if (zx1 > 0) p.fillRect(QRect(bar.left(), bar.top(), zx1, bar.height()), dim);
    if (zx2 < width()) p.fillRect(QRect(zx2, bar.top(), width() - zx2, bar.height()), dim);

    // Zoom range border.
    p.setPen(QPen(QColor(255, 255, 255, 180), 1));
    p.drawRect(QRect(zx1, bar.top(), zx2 - zx1, bar.height() - 1));
  }

  // Current time indicator — bright white line with slight glow.
  int cx = static_cast<int>(((current_time_ - range_min_) / range) * width());
  cx = std::clamp(cx, 0, width() - 1);
  p.setPen(QPen(QColor(255, 255, 255, 100), 5));
  p.drawLine(cx, 0, cx, height());
  p.setPen(QPen(Qt::white, 2));
  p.drawLine(cx, 0, cx, height());
}

void TimelineWidget::mousePressEvent(QMouseEvent *event) {
  if (event->button() == Qt::LeftButton) {
    emit seekRequested(timeAtX(event->pos().x()));
    event->accept();
  }
}

void TimelineWidget::mouseMoveEvent(QMouseEvent *event) {
  if (event->buttons() & Qt::LeftButton) {
    emit seekRequested(timeAtX(event->pos().x()));
    event->accept();
  }
}

}  // namespace cabana::plot_ui
