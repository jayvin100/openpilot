#pragma once

#include <QString>

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PlotJuggler/plotdata.h"
#include "PlotJuggler/transform_function.h"
#include "plotjuggler_app/transforms/custom_function.h"
#include "tools/cabana/pj_engine/series_snapshot.h"

namespace cabana::pj_engine {

struct ImportResult {
  std::vector<std::string> added_curves;
  std::vector<std::string> updated_snapshot_curves;
  bool curves_updated = false;
  bool data_pushed = false;
};

struct ReactiveUpdate {
  std::vector<std::string> added_curves;
  std::unordered_set<std::string> updated_curves;
};

struct RefreshResult {
  std::vector<std::string> added_curves;
  std::unordered_set<std::string> updated_curves;
  bool curves_updated = false;
  bool data_pushed = false;
};

class PlotJugglerSession {
public:
  PlotJugglerSession() = default;
  ~PlotJugglerSession();

  PJ::PlotDataMapRef &plotData();
  const PJ::PlotDataMapRef &plotData() const;

  PJ::TransformsMap &transforms();
  const PJ::TransformsMap &transforms() const;

  bool hasSeries(const std::string &name) const;
  bool ensureNumericPlaceholder(const std::string &name);
  SeriesSnapshotPtr snapshotFor(const PJ::PlotDataXY *series) const;

  void clearAll();
  void clearBuffers();

  ImportResult importPlotDataMap(PJ::PlotDataMapRef &new_data, bool remove_old);
  RefreshResult importAndRefresh(PJ::PlotDataMapRef &new_data, bool remove_old, double tracker_time);
  std::vector<std::string> deleteCurvesWithDependencies(const std::vector<std::string> &curve_names);
  ReactiveUpdate updateReactiveTransforms(double tracker_time);
  std::vector<std::string> updateDerivedTransforms();

  CustomPlotPtr findCustomPlot(const std::string &plot_name) const;
  bool upsertCustomPlot(const CustomPlotPtr &custom_plot, QString *error = nullptr);

private:
  void refreshNumericSnapshot(PJ::PlotData *series);
  void refreshScatterSnapshot(PJ::PlotDataXY *series);
  void refreshSnapshotsForTransformOutputs();
  void refreshSnapshotsForCurveNames(const std::vector<std::string> &curve_names);
  void clearSnapshots();

  PJ::PlotDataMapRef plot_data_;
  PJ::TransformsMap transforms_;
  std::unordered_map<const PJ::PlotDataXY *, SeriesSnapshotPtr> snapshots_;
};

}  // namespace cabana::pj_engine
