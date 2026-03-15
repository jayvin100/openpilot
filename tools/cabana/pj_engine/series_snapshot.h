#pragma once

#include <QPointF>

#include <functional>
#include <memory>
#include <vector>

#include "PlotJuggler/plotdata.h"

namespace cabana::pj_engine {

struct SeriesSnapshot {
  std::vector<QPointF> points;
  PJ::RangeOpt range_x;
  PJ::RangeOpt range_y;
};

using SeriesSnapshotPtr = std::shared_ptr<const SeriesSnapshot>;
using SeriesSnapshotLookup = std::function<SeriesSnapshotPtr(const PJ::PlotDataXY *)>;

SeriesSnapshotPtr BuildSeriesSnapshot(const PJ::PlotDataXY &data);

}  // namespace cabana::pj_engine
