#pragma once

#include <QWidget>

namespace cabana::plot_ui {

/// Timeline bar matching Cabana's video slider style.
/// Shows replay timeline segments and a zoom range overlay.
class TimelineWidget : public QWidget {
  Q_OBJECT

public:
  explicit TimelineWidget(QWidget *parent = nullptr);

  void setTimeRange(double min, double max);
  void setCurrentTime(double sec);
  void setZoomRange(double min, double max);

signals:
  void seekRequested(double sec);

protected:
  void paintEvent(QPaintEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;

private:
  double timeAtX(int x) const;

  double range_min_ = 0;
  double range_max_ = 1;
  double current_time_ = 0;
  double zoom_min_ = -1;
  double zoom_max_ = -1;
};

}  // namespace cabana::plot_ui
