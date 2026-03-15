#include "tools/cabana/pj_engine/engine.h"

#include <QElapsedTimer>
#include <QThread>

#include <algorithm>
#include <exception>

#include "plotjuggler_app/transforms/lua_custom_function.h"

namespace cabana::pj_engine {

namespace {

struct MetaTypeRegistrar {
  MetaTypeRegistrar() {
    qRegisterMetaType<PlotSnapshotBundlePtr>("cabana::pj_engine::PlotSnapshotBundlePtr");
    qRegisterMetaType<CurveTreeSnapshot>("cabana::pj_engine::CurveTreeSnapshot");
    qRegisterMetaType<EnginePerfStats>("cabana::pj_engine::EnginePerfStats");
    qRegisterMetaType<cabana::pj_layout::LayoutModel>("cabana::pj_layout::LayoutModel");
    qRegisterMetaType<SegmentMap>("SegmentMap");
    qRegisterMetaType<uint64_t>("uint64_t");
    qRegisterMetaType<cabana::pj_layout::SnippetModel>("cabana::pj_layout::SnippetModel");
    qRegisterMetaType<cabana::pj_layout::TransformConfig>("cabana::pj_layout::TransformConfig");
  }
};
static MetaTypeRegistrar s_registrar;

}  // namespace

Engine::Engine(QObject *parent) : QObject(parent) {
  perf_enabled_ = qEnvironmentVariableIsSet("CABANA_PJ_PERF");
}

Engine::~Engine() {
  delete replay_engine_;
}

void Engine::initialize() {
  replay_engine_ = new ReplayEngine(this);
  replay_engine_->setBatchReadyHandler([this](const ParsedBatchPtr &batch) {
    handleBatch(batch);
  });
}

double Engine::routeStartSec() const {
  return replay_engine_ ? replay_engine_->routeStartSec() : 0.0;
}

QString Engine::perfSummary() const {
  QString summary = replay_engine_ ? replay_engine_->perfSummary() : QString();
  if (perf_enabled_ && cum_perf_.batch_count > 0) {
    const auto avg = [](qint64 total, qint64 n) -> qint64 { return n > 0 ? total / n : 0; };
    const qint64 n = cum_perf_.batch_count;
    summary += QString(" | eng merge %1/%2ms max %3 xform %4/%5ms max %6 snap %7/%8ms max %9")
        .arg(n).arg(avg(cum_perf_.merge_total_ms, n)).arg(cum_perf_.merge_max_ms)
        .arg(n).arg(avg(cum_perf_.transform_total_ms, n)).arg(cum_perf_.transform_max_ms)
        .arg(n).arg(avg(cum_perf_.snapshot_total_ms, n)).arg(cum_perf_.snapshot_max_ms);
  }
  return summary;
}

PlotJugglerSession &Engine::session() {
  return session_;
}

const cabana::pj_layout::LayoutModel &Engine::layout() const {
  return layout_;
}

void Engine::loadLayout(cabana::pj_layout::LayoutModel layout) {
  layout_ = std::move(layout);
  setupCustomMath();
  emit layoutChanged(layout_);
}

void Engine::setupCustomMath() {
  // Register custom math snippets from the layout as transforms in the session.
  auto registerSnippets = [this](const std::vector<cabana::pj_layout::SnippetModel> &snippets) {
    for (const auto &snippet : snippets) {
      if (snippet.name.isEmpty() || snippet.function.isEmpty()) continue;

      SnippetData data;
      data.alias_name = snippet.name;
      data.global_vars = snippet.global_vars;
      data.function = snippet.function;
      data.linked_source = snippet.linked_source;
      data.additional_sources = snippet.additional_sources;

      try {
        auto custom_plot = std::make_shared<LuaCustomFunction>(data);
        QString error;
        session_.upsertCustomPlot(custom_plot, &error);
      } catch (const std::exception &e) {
        qWarning("Engine: failed to create custom math '%s': %s",
                 qPrintable(snippet.name), e.what());
      }
    }
  };

  if (layout_.custom_math_present) {
    registerSnippets(layout_.custom_math_snippets);
  }
  if (layout_.snippets_present) {
    registerSnippets(layout_.snippets);
  }
}

void Engine::appendSegments(SegmentMap segments, uint64_t route_start_nanos) {
  if (replay_engine_) {
    replay_engine_->queueSegments(segments, route_start_nanos);
  }
}

void Engine::seek(double time) {
  tracker_time_ = time;
  if (!paused_) return;
  session_.updateReactiveTransforms(tracker_time_);
  emitSnapshots();
}

void Engine::setPaused(bool paused) {
  paused_ = paused;
}

void Engine::clearData() {
  if (replay_engine_) {
    replay_engine_->clear();
  }
  session_.clearAll();
  hidden_curves_.clear();
  snapshot_generation_ = 0;
  tracker_time_ = 0.0;
}

void Engine::setCurveVisibility(QString curve, bool visible) {
  std::string name = curve.toStdString();
  if (visible) {
    hidden_curves_.erase(name);
  } else {
    hidden_curves_.insert(name);
  }
  emitSnapshots();
}

void Engine::saveLayout() {
  emit layoutSaved(layout_);
}

void Engine::setVisibleRange(double x_min, double x_max) {
  visible_x_min_ = x_min;
  visible_x_max_ = x_max;
}

namespace {

void collectPlots(cabana::pj_layout::LayoutNode &node,
                  std::vector<cabana::pj_layout::PlotModel *> &out) {
  if (node.kind == cabana::pj_layout::LayoutNode::Kind::DockArea) {
    for (auto &plot : node.plots) {
      out.push_back(&plot);
    }
  } else {
    for (auto &child : node.children) {
      collectPlots(child, out);
    }
  }
}

std::vector<cabana::pj_layout::PlotModel *> allPlots(cabana::pj_layout::LayoutModel &layout) {
  std::vector<cabana::pj_layout::PlotModel *> plots;
  for (auto &tw : layout.tabbed_widgets) {
    for (auto &tab : tw.tabs) {
      for (auto &c : tab.containers) {
        if (c.has_root) collectPlots(c.root, plots);
      }
    }
  }
  return plots;
}

}  // namespace

void Engine::moveCurveToPlot(QString curve, int tab_index, int plot_index) {
  auto plots = allPlots(layout_);
  if (plot_index < 0 || plot_index >= static_cast<int>(plots.size())) return;

  std::string name = curve.toStdString();

  // Remove from all plots first.
  for (auto *plot : plots) {
    auto &curves = plot->curves;
    curves.erase(std::remove_if(curves.begin(), curves.end(),
                                [&](const cabana::pj_layout::CurveBinding &b) {
                                  return b.name.toStdString() == name;
                                }),
                 curves.end());
  }

  // Add to target plot.
  cabana::pj_layout::CurveBinding binding;
  binding.name = curve;
  plots[plot_index]->curves.push_back(std::move(binding));

  emit layoutChanged(layout_);
  emitSnapshots();
}

void Engine::splitPlot(int plot_index, Qt::Orientation orientation) {
  // Find the plot by flat index and split its parent node.
  // For now, duplicate the plot into two side-by-side plots.
  auto plots = allPlots(layout_);
  if (plot_index < 0 || plot_index >= static_cast<int>(plots.size())) return;

  auto *target = plots[plot_index];
  cabana::pj_layout::PlotModel copy = *target;

  // Walk the layout to find the LayoutNode containing this plot and split it.
  // Simplified: find which tab/container owns this plot and add a sibling.
  int idx = 0;
  for (auto &tw : layout_.tabbed_widgets) {
    for (auto &tab : tw.tabs) {
      for (auto &c : tab.containers) {
        if (!c.has_root) continue;
        std::function<bool(cabana::pj_layout::LayoutNode &)> splitInNode;
        splitInNode = [&](cabana::pj_layout::LayoutNode &node) -> bool {
          if (node.kind == cabana::pj_layout::LayoutNode::Kind::DockArea) {
            for (size_t i = 0; i < node.plots.size(); ++i) {
              if (idx == plot_index) {
                // Replace this DockArea with a Splitter containing two DockAreas.
                cabana::pj_layout::LayoutNode left;
                left.kind = cabana::pj_layout::LayoutNode::Kind::DockArea;
                left.plots.push_back(node.plots[i]);

                cabana::pj_layout::LayoutNode right;
                right.kind = cabana::pj_layout::LayoutNode::Kind::DockArea;
                right.plots.push_back(copy);

                node.kind = cabana::pj_layout::LayoutNode::Kind::Splitter;
                node.orientation = orientation;
                node.plots.clear();
                node.children = {std::move(left), std::move(right)};
                node.sizes = {50, 50};
                return true;
              }
              idx++;
            }
            return false;
          }
          for (auto &child : node.children) {
            if (splitInNode(child)) return true;
          }
          return false;
        };
        if (splitInNode(c.root)) {
          emit layoutChanged(layout_);
          return;
        }
      }
    }
  }
}

void Engine::createTab(QString name) {
  if (layout_.tabbed_widgets.empty()) return;
  cabana::pj_layout::TabModel tab;
  tab.tab_name = name;
  layout_.tabbed_widgets[0].tabs.push_back(std::move(tab));
  emit layoutChanged(layout_);
}

void Engine::removeTab(int tab_index) {
  if (layout_.tabbed_widgets.empty()) return;
  auto &tabs = layout_.tabbed_widgets[0].tabs;
  if (tab_index >= 0 && tab_index < static_cast<int>(tabs.size())) {
    tabs.erase(tabs.begin() + tab_index);
    emit layoutChanged(layout_);
  }
}

void Engine::updateTransform(QString curve, cabana::pj_layout::TransformConfig config) {
  // Update the transform config for this curve in the layout model.
  auto plots = allPlots(layout_);
  for (auto *plot : plots) {
    for (auto &binding : plot->curves) {
      if (binding.name == curve) {
        binding.has_transform = !config.name.isEmpty();
        binding.transform = std::move(config);
        emit layoutChanged(layout_);
        return;
      }
    }
  }
}

void Engine::updateLua(cabana::pj_layout::SnippetModel snippet) {
  SnippetData data;
  data.alias_name = snippet.name;
  data.global_vars = snippet.global_vars;
  data.function = snippet.function;
  data.linked_source = snippet.linked_source;
  data.additional_sources = snippet.additional_sources;
  try {
    auto custom_plot = std::make_shared<LuaCustomFunction>(data);
    QString error;
    session_.upsertCustomPlot(custom_plot, &error);
  } catch (const std::exception &e) {
    qWarning("Engine: updateLua failed for '%s': %s", qPrintable(snippet.name), e.what());
  }
  emitSnapshots();
}

void Engine::handleBatch(const ParsedBatchPtr &batch) {
  if (!batch || batch->data_map.getAllNames().empty()) {
    return;
  }

  QElapsedTimer timer;
  if (perf_enabled_) timer.start();

  // Stage 1: Import data
  auto import_result = session_.importPlotDataMap(batch->data_map, false);
  qint64 merge_ms = 0;
  if (perf_enabled_) merge_ms = timer.elapsed();

  // Stage 2: Run transforms (catch exceptions from Lua/custom math)
  ReactiveUpdate reactive;
  std::vector<std::string> derived;
  try {
    reactive = session_.updateReactiveTransforms(tracker_time_);
    derived = session_.updateDerivedTransforms();
  } catch (const std::exception &e) {
    qWarning("Engine: transform exception: %s", e.what());
  }
  qint64 transform_ms = 0;
  if (perf_enabled_) transform_ms = timer.elapsed() - merge_ms;

  bool curves_updated = import_result.curves_updated || !reactive.added_curves.empty();

  // Stage 3: Build and emit snapshots
  emitSnapshots();
  qint64 snapshot_ms = 0;
  if (perf_enabled_) snapshot_ms = timer.elapsed() - merge_ms - transform_ms;

  if (replay_engine_) {
    replay_engine_->noteBatchConsumed(*batch, merge_ms + transform_ms);
  }

  if (curves_updated) {
    emit curveTreeChanged(buildCurveTree());
  }

  if (perf_enabled_) {
    cum_perf_.batch_count++;
    cum_perf_.merge_total_ms += merge_ms;
    cum_perf_.merge_max_ms = std::max(cum_perf_.merge_max_ms, merge_ms);
    cum_perf_.transform_total_ms += transform_ms;
    cum_perf_.transform_max_ms = std::max(cum_perf_.transform_max_ms, transform_ms);
    cum_perf_.snapshot_total_ms += snapshot_ms;
    cum_perf_.snapshot_max_ms = std::max(cum_perf_.snapshot_max_ms, snapshot_ms);

    EnginePerfStats stats;
    stats.parse_ms = batch->parse_ms;
    stats.merge_ms = merge_ms;
    stats.transform_ms = transform_ms;
    stats.snapshot_ms = snapshot_ms;
    stats.total_ms = timer.elapsed();
    emit perfReport(stats);

    if (stats.total_ms >= 16) {
      qInfo().noquote() << QString("CABANA_PJ_PERF engine merge=%1ms xform=%2ms snap=%3ms total=%4ms")
                               .arg(merge_ms).arg(transform_ms).arg(snapshot_ms).arg(stats.total_ms);
    }
  }

  emit batchApplied();
}

void Engine::emitSnapshots() {
  emit snapshotsReady(buildSnapshotBundle());
}

PlotSnapshotBundlePtr Engine::buildSnapshotBundle() const {
  auto bundle = std::make_shared<PlotSnapshotBundle>();
  bundle->tracker_time = tracker_time_;
  bundle->generation = ++snapshot_generation_;

  for (const auto &[name, series] : session_.plotData().numeric) {
    if (hidden_curves_.count(name)) continue;
    auto snap = session_.snapshotFor(&series);
    if (snap) {
      bundle->snapshots[name] = snap;
    }
  }
  for (const auto &[name, series] : session_.plotData().scatter_xy) {
    if (hidden_curves_.count(name)) continue;
    auto snap = session_.snapshotFor(&series);
    if (snap) {
      bundle->snapshots[name] = snap;
    }
  }

  return bundle;
}

CurveTreeSnapshot Engine::buildCurveTree() const {
  CurveTreeSnapshot tree;
  for (const auto &[name, series] : session_.plotData().numeric) {
    std::string group = series.group() ? series.group()->name() : std::string();
    tree.numeric.push_back({name, group});
  }
  for (const auto &[name, series] : session_.plotData().strings) {
    std::string group = series.group() ? series.group()->name() : std::string();
    tree.strings.push_back({name, group});
  }
  for (const auto &[name, series] : session_.plotData().scatter_xy) {
    std::string group = series.group() ? series.group()->name() : std::string();
    tree.scatter.push_back({name, group});
  }
  return tree;
}

}  // namespace cabana::pj_engine
