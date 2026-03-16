#include "tools/cabana/pj_engine/session.h"

#include <algorithm>
#include <exception>
#include <set>
#include <type_traits>

namespace cabana::pj_engine {

namespace {

template <typename SeriesMap>
void clearOverlappingSeries(SeriesMap &previous_plot_data, const SeriesMap &new_plot_data) {
  for (auto &[series_name, series] : previous_plot_data) {
    if (new_plot_data.count(series_name) != 0) {
      series.clear();
    }
  }
}

template <typename SeriesMap>
void importSeries(SeriesMap &source_series, SeriesMap &destination_series,
                  PJ::PlotDataMapRef &destination, bool remove_older,
                  ImportResult *result) {
  for (auto &[series_id, source_plot] : source_series) {
    const std::string &plot_name = source_plot.plotName();
    auto destination_it = destination_series.find(series_id);
    if (destination_it == destination_series.end()) {
      result->added_curves.push_back(series_id);

      PJ::PlotGroup::Ptr group;
      if (source_plot.group()) {
        destination.getOrCreateGroup(source_plot.group()->name());
      }
      destination_it = destination_series
                           .emplace(std::piecewise_construct, std::forward_as_tuple(series_id),
                                    std::forward_as_tuple(plot_name, group))
                           .first;
      result->curves_updated = true;
    }

    auto &destination_plot = destination_it->second;
    PJ::PlotGroup::Ptr destination_group = destination_plot.group();

    for (const auto &[name, attr] : source_plot.attributes()) {
      if (destination_plot.attribute(name) != attr) {
        destination_plot.setAttribute(name, attr);
        result->curves_updated = true;
      }
    }

    if (source_plot.group()) {
      if (!destination_group || destination_group->name() != source_plot.group()->name()) {
        destination_group = destination.getOrCreateGroup(source_plot.group()->name());
        destination_plot.changeGroup(destination_group);
      }

      for (const auto &[name, attr] : source_plot.group()->attributes()) {
        if (destination_group->attribute(name) != attr) {
          destination_group->setAttribute(name, attr);
          result->curves_updated = true;
        }
      }
    }

    if (remove_older) {
      destination_plot.clear();
    }

    if (source_plot.size() > 0) {
      result->data_pushed = true;
    }

    for (size_t i = 0; i < source_plot.size(); i++) {
      destination_plot.pushBack(source_plot.at(i));
    }

    if constexpr (std::is_same_v<typename SeriesMap::mapped_type, PJ::PlotData> ||
                  std::is_same_v<typename SeriesMap::mapped_type, PJ::StringSeries> ||
                  std::is_same_v<typename SeriesMap::mapped_type, PJ::PlotDataAny>) {
      destination_plot.setMaximumRangeX(source_plot.maximumRangeX());
    }

    if constexpr (std::is_same_v<typename SeriesMap::mapped_type, PJ::PlotData> ||
                  std::is_same_v<typename SeriesMap::mapped_type, PJ::PlotDataXY>) {
      result->updated_snapshot_curves.push_back(series_id);
    }

    source_plot.clear();
  }
}

}  // namespace

PlotJugglerSession::~PlotJugglerSession() {
  plot_data_.user_defined.clear();
}

void PlotJugglerSession::refreshNumericSnapshot(PJ::PlotData *series) {
  if (!series) {
    return;
  }
  snapshots_[series] = BuildSeriesSnapshot(*series);
}

void PlotJugglerSession::refreshScatterSnapshot(PJ::PlotDataXY *series) {
  if (!series) {
    return;
  }
  snapshots_[series] = BuildSeriesSnapshot(*series);
}

void PlotJugglerSession::refreshSnapshotsForCurveNames(const std::vector<std::string> &curve_names) {
  for (const auto &curve_name : curve_names) {
    if (auto numeric_it = plot_data_.numeric.find(curve_name); numeric_it != plot_data_.numeric.end()) {
      refreshNumericSnapshot(&numeric_it->second);
      continue;
    }
    if (auto scatter_it = plot_data_.scatter_xy.find(curve_name); scatter_it != plot_data_.scatter_xy.end()) {
      refreshScatterSnapshot(&scatter_it->second);
    }
  }
}

void PlotJugglerSession::refreshSnapshotsForTransformOutputs() {
  std::vector<std::string> transform_outputs;
  transform_outputs.reserve(transforms_.size());
  for (const auto &[curve_name, _] : transforms_) {
    transform_outputs.push_back(curve_name);
  }
  refreshSnapshotsForCurveNames(transform_outputs);
}

void PlotJugglerSession::clearSnapshots() {
  snapshots_.clear();
}

PJ::PlotDataMapRef &PlotJugglerSession::plotData() {
  return plot_data_;
}

const PJ::PlotDataMapRef &PlotJugglerSession::plotData() const {
  return plot_data_;
}

PJ::TransformsMap &PlotJugglerSession::transforms() {
  return transforms_;
}

const PJ::TransformsMap &PlotJugglerSession::transforms() const {
  return transforms_;
}

bool PlotJugglerSession::hasSeries(const std::string &name) const {
  return plot_data_.numeric.count(name) != 0 ||
         plot_data_.strings.count(name) != 0 ||
         plot_data_.scatter_xy.count(name) != 0 ||
         plot_data_.user_defined.count(name) != 0;
}

bool PlotJugglerSession::ensureNumericPlaceholder(const std::string &name) {
  if (hasSeries(name)) {
    return false;
  }
  auto it = plot_data_.addNumeric(name);
  refreshNumericSnapshot(&it->second);
  return true;
}

SeriesSnapshotPtr PlotJugglerSession::snapshotFor(const PJ::PlotDataXY *series) const {
  if (!series) {
    return {};
  }
  auto it = snapshots_.find(series);
  return (it != snapshots_.end()) ? it->second : SeriesSnapshotPtr{};
}

void PlotJugglerSession::clearAll() {
  plot_data_.clear();
  transforms_.clear();
  clearSnapshots();
}

void PlotJugglerSession::clearBuffers() {
  for (auto &[_, series] : plot_data_.numeric) {
    series.clear();
    refreshNumericSnapshot(&series);
  }
  for (auto &[_, series] : plot_data_.strings) {
    series.clear();
  }
  for (auto &[_, series] : plot_data_.user_defined) {
    series.clear();
  }
  for (auto &[_, transform] : transforms_) {
    transform->reset();
  }
  refreshSnapshotsForTransformOutputs();
  for (auto &[_, series] : plot_data_.scatter_xy) {
    refreshScatterSnapshot(&series);
  }
}

ImportResult PlotJugglerSession::importPlotDataMap(PJ::PlotDataMapRef &new_data, bool remove_old) {
  if (remove_old) {
    clearOverlappingSeries(plot_data_.scatter_xy, new_data.scatter_xy);
    clearOverlappingSeries(plot_data_.numeric, new_data.numeric);
    clearOverlappingSeries(plot_data_.strings, new_data.strings);
  }

  ImportResult result;
  importSeries(new_data.numeric, plot_data_.numeric, plot_data_, remove_old, &result);
  importSeries(new_data.strings, plot_data_.strings, plot_data_, remove_old, &result);
  importSeries(new_data.scatter_xy, plot_data_.scatter_xy, plot_data_, remove_old, &result);
  importSeries(new_data.user_defined, plot_data_.user_defined, plot_data_, remove_old, &result);
  refreshSnapshotsForCurveNames(result.updated_snapshot_curves);
  return result;
}

RefreshResult PlotJugglerSession::importAndRefresh(PJ::PlotDataMapRef &new_data,
                                                   bool remove_old,
                                                   double tracker_time) {
  const auto import_result = importPlotDataMap(new_data, remove_old);
  const auto reactive_update = updateReactiveTransforms(tracker_time);
  const auto derived_updates = updateDerivedTransforms();

  RefreshResult result;
  result.added_curves = import_result.added_curves;
  result.added_curves.insert(result.added_curves.end(), reactive_update.added_curves.begin(),
                             reactive_update.added_curves.end());
  result.curves_updated = import_result.curves_updated || !reactive_update.added_curves.empty();
  result.data_pushed = import_result.data_pushed;

  result.updated_curves.insert(import_result.updated_snapshot_curves.begin(),
                               import_result.updated_snapshot_curves.end());
  result.updated_curves.insert(reactive_update.updated_curves.begin(),
                               reactive_update.updated_curves.end());
  result.updated_curves.insert(derived_updates.begin(), derived_updates.end());
  return result;
}

std::vector<std::string> PlotJugglerSession::deleteCurvesWithDependencies(
    const std::vector<std::string> &curve_names) {
  std::set<std::string> to_be_deleted(curve_names.begin(), curve_names.end());

  size_t prev_size = 0;
  while (prev_size < to_be_deleted.size()) {
    prev_size = to_be_deleted.size();
    for (const auto &[transform_name, transform] : transforms_) {
      for (const auto *source : transform->dataSources()) {
        if (source && to_be_deleted.count(source->plotName()) > 0) {
          to_be_deleted.insert(transform_name);
        }
      }
    }
  }

  for (const auto &curve_name : to_be_deleted) {
    if (auto numeric_it = plot_data_.numeric.find(curve_name); numeric_it != plot_data_.numeric.end()) {
      snapshots_.erase(&numeric_it->second);
    }
    if (auto scatter_it = plot_data_.scatter_xy.find(curve_name); scatter_it != plot_data_.scatter_xy.end()) {
      snapshots_.erase(&scatter_it->second);
    }
    plot_data_.erase(curve_name);
    transforms_.erase(curve_name);
  }

  return {to_be_deleted.begin(), to_be_deleted.end()};
}

ReactiveUpdate PlotJugglerSession::updateReactiveTransforms(double) {
  // Reactive Lua transforms removed — custom math uses Python batch evaluation.
  return {};
}

std::vector<std::string> PlotJugglerSession::updateDerivedTransforms() {
  std::vector<PJ::TransformFunction *> ordered_transforms;
  ordered_transforms.reserve(transforms_.size());
  for (auto &[_, function] : transforms_) {
    ordered_transforms.push_back(function.get());
  }

  std::sort(ordered_transforms.begin(), ordered_transforms.end(),
            [](PJ::TransformFunction *a, PJ::TransformFunction *b) {
              return a->order() < b->order();
            });

  for (auto *function : ordered_transforms) {
    function->calculate();
  }
  refreshSnapshotsForTransformOutputs();
  std::vector<std::string> updated_curve_names;
  updated_curve_names.reserve(transforms_.size());
  for (const auto &[curve_name, _] : transforms_) {
    updated_curve_names.push_back(curve_name);
  }
  return updated_curve_names;
}

CustomPlotPtr PlotJugglerSession::findCustomPlot(const std::string &plot_name) const {
  auto it = transforms_.find(plot_name);
  if (it == transforms_.end()) {
    return {};
  }
  return std::dynamic_pointer_cast<CustomFunction>(it->second);
}

bool PlotJugglerSession::upsertCustomPlot(const CustomPlotPtr &custom_plot, QString *error) {
  const std::string curve_name = custom_plot->aliasName().toStdString();

  if (auto data_it = plot_data_.numeric.find(curve_name); data_it != plot_data_.numeric.end()) {
    data_it->second.clear();
  }

  try {
    custom_plot->calculateAndAdd(plot_data_);
  } catch (const std::exception &ex) {
    if (error) {
      *error = QString::fromStdString(ex.what());
    }
    return false;
  }

  const bool inserted = (transforms_.count(curve_name) == 0);
  transforms_[curve_name] = custom_plot;
  if (auto numeric_it = plot_data_.numeric.find(curve_name); numeric_it != plot_data_.numeric.end()) {
    refreshNumericSnapshot(&numeric_it->second);
  }
  return inserted;
}

}  // namespace cabana::pj_engine
