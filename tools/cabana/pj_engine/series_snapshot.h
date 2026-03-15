#pragma once

#include <QPointF>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
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

struct PlotSnapshotBundle {
  std::unordered_map<std::string, SeriesSnapshotPtr> snapshots;
  double tracker_time = 0.0;
  uint64_t generation = 0;
};

using PlotSnapshotBundlePtr = std::shared_ptr<const PlotSnapshotBundle>;

struct CurveEntry {
  std::string name;
  std::string group;
};

struct CurveTreeSnapshot {
  std::vector<CurveEntry> numeric;
  std::vector<CurveEntry> strings;
  std::vector<CurveEntry> scatter;
};

}  // namespace cabana::pj_engine
