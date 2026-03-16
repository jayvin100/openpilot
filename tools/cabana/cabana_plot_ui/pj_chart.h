#pragma once

#include <set>
#include <string>
#include <vector>

#include <QLabel>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QScatterSeries>
#include <QtCharts/QValueAxis>
using namespace QtCharts;

#include "tools/cabana/pj_engine/series_snapshot.h"
#include "tools/cabana/pj_layout/layout_model.h"

namespace cabana::plot_ui {

class TipLabel;

/// A chart view that renders named curves from PlotSnapshotBundle,
/// visually matching Cabana's existing ChartView.
class PjChartView : public QChartView {
  Q_OBJECT

public:
  explicit PjChartView(QWidget *parent = nullptr);

  void configure(const cabana::pj_layout::PlotModel &model);
  void updateSnapshots(const cabana::pj_engine::PlotSnapshotBundle &bundle);
  void setTrackerTime(double time);
  void setGridVisible(bool visible);
  void setLinkedXRange(double min, double max);
  void setRouteStart(double sec);
  void resetZoom();

  struct CurveItem {
    std::string name;
    QXYSeries *series = nullptr;
    std::vector<QPointF> vals;
    QColor color;
    double min_y = 0;
    double max_y = 0;
  };

signals:
  void seekRequested(double time);
  void curveDropped(QString curve_name);
  void splitRequested(Qt::Orientation orientation);
  void xRangeChanged(double min, double max);

protected:
  void contextMenuEvent(QContextMenuEvent *event) override;
  void mousePressEvent(QMouseEvent *event) override;
  void mouseReleaseEvent(QMouseEvent *event) override;
  void mouseMoveEvent(QMouseEvent *event) override;
  void wheelEvent(QWheelEvent *event) override;
  void dragEnterEvent(QDragEnterEvent *event) override;
  void dragMoveEvent(QDragMoveEvent *event) override;
  void dropEvent(QDropEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;
  void paintEvent(QPaintEvent *event) override;
  void drawForeground(QPainter *painter, const QRectF &rect) override;

private:
  void updateAxisY();
  void updateSeriesPoints();
  void showTip(double sec);
  void hideTip();
  QXYSeries *createSeries(QColor color);
  std::tuple<double, double, int> getNiceAxisNumbers(qreal min, qreal max, int tick_count);
  qreal niceNumber(qreal x, bool ceiling);

  QValueAxis *axis_x_ = nullptr;
  QValueAxis *axis_y_ = nullptr;
  std::vector<CurveItem> curves_;
  double tracker_time_ = 0;
  QLabel *tip_label_ = nullptr;
  double tooltip_x_ = -1;
  QFont signal_value_font_;
  double route_start_ = 0;
  bool xy_mode_ = false;
  std::set<std::string> transform_curves_;
  QPixmap chart_pixmap_;
};

}  // namespace cabana::plot_ui
