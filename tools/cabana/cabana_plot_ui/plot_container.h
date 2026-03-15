#pragma once

#include <string>
#include <unordered_map>

#include "PlotJuggler/plotdata.h"
#include "PlotJuggler/plotwidget_base.h"
#include "tools/cabana/pj_engine/series_snapshot.h"
#include "tools/cabana/pj_layout/layout_model.h"

class QwtPlotGrid;
class QwtPlotMarker;

namespace cabana::plot_ui {

/// A plot area backed by PlotWidgetBase (full Qwt polish: legend, zoom, pan,
/// magnifier, styled grid) that renders from immutable snapshots.
class PlotContainer : public PJ::PlotWidgetBase {
  Q_OBJECT

public:
  explicit PlotContainer(QWidget *parent = nullptr);
  ~PlotContainer() override;

  void configure(const cabana::pj_layout::PlotModel &model);
  void updateSnapshots(const cabana::pj_engine::PlotSnapshotBundle &bundle);
  void setTrackerTime(double time);
  void setGridVisible(bool visible);

  void setLinkedXRange(double min, double max);
  PJ::Range getXRange() const;

signals:
  void seekRequested(double time);
  void curveDropped(QString curve_name);
  void splitRequested(Qt::Orientation orientation);
  void xRangeChanged(double min, double max);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  cabana::pj_engine::SeriesSnapshotPtr lookupSnapshot(const PJ::PlotDataXY *series) const;

  PJ::PlotDataMapRef local_data_;
  cabana::pj_engine::PlotSnapshotBundle latest_bundle_;
  std::unordered_map<PJ::PlotData *, std::string> series_to_name_;
  QwtPlotGrid *grid_ = nullptr;
  QwtPlotMarker *tracker_line_ = nullptr;
  QwtPlotMarker *tracker_text_ = nullptr;
  std::set<std::string> transform_curves_;  // curves that need PlotData populated
  bool xy_mode_ = false;
  bool first_data_received_ = false;
};

}  // namespace cabana::plot_ui
