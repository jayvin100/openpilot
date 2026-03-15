#include "tools/cabana/pj_engine/series_snapshot.h"

#include <limits>

namespace cabana::pj_engine {

SeriesSnapshotPtr BuildSeriesSnapshot(const PJ::PlotDataXY &data) {
  auto snapshot = std::make_shared<SeriesSnapshot>();
  snapshot->points.reserve(data.size());

  double min_x = std::numeric_limits<double>::max();
  double max_x = std::numeric_limits<double>::lowest();
  double min_y = std::numeric_limits<double>::max();
  double max_y = std::numeric_limits<double>::lowest();

  for (size_t i = 0; i < data.size(); ++i) {
    const auto &point = data.at(i);
    snapshot->points.emplace_back(point.x, point.y);
    min_x = std::min(min_x, point.x);
    max_x = std::max(max_x, point.x);
    min_y = std::min(min_y, point.y);
    max_y = std::max(max_y, point.y);
  }

  if (!snapshot->points.empty()) {
    snapshot->range_x = PJ::Range{min_x, max_x};
    snapshot->range_y = PJ::Range{min_y, max_y};
  }

  return snapshot;
}

}  // namespace cabana::pj_engine
