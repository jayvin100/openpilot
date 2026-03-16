#pragma once

#include <QMetaType>
#include <QObject>

#include <set>

#include "tools/cabana/pj_engine/replay_engine.h"
#include "tools/cabana/pj_engine/series_snapshot.h"
#include "tools/cabana/pj_engine/session.h"
#include "tools/cabana/pj_layout/layout_model.h"
#include "tools/replay/seg_mgr.h"

namespace cabana::pj_engine {

struct EnginePerfStats {
  qint64 parse_ms = 0;
  qint64 merge_ms = 0;
  qint64 transform_ms = 0;
  qint64 snapshot_ms = 0;
  qint64 total_ms = 0;
};

class Engine : public QObject {
  Q_OBJECT

public:
  explicit Engine(QObject *parent = nullptr);
  ~Engine() override;

  void initialize();

  double routeStartSec() const;
  QString perfSummary() const;
  PlotJugglerSession &session();
  const cabana::pj_layout::LayoutModel &layout() const;

public slots:
  // Data lifecycle
  void loadLayout(cabana::pj_layout::LayoutModel layout);
  void saveLayout();
  void appendSegments(SegmentMap segments, uint64_t route_start_nanos);
  void seek(double time);
  void setPaused(bool paused);
  void clearData();

  // Curve & plot interaction
  void setCurveVisibility(QString curve, bool visible);
  void setVisibleRange(double x_min, double x_max);
  void addCurveToPlot(QString curve, int plot_index);
  void moveCurveToPlot(QString curve, int tab_index, int plot_index);

  // Layout mutations
  void splitPlot(int plot_index, Qt::Orientation orientation);
  void createTab(QString name);
  void removeTab(int tab_index);

  // Transform & Lua
  void updateTransform(QString curve, cabana::pj_layout::TransformConfig config);
  void updateLua(cabana::pj_layout::SnippetModel snippet);

signals:
  void snapshotsReady(cabana::pj_engine::PlotSnapshotBundlePtr bundle);
  void curveTreeChanged(cabana::pj_engine::CurveTreeSnapshot tree);
  void layoutChanged(cabana::pj_layout::LayoutModel layout);
  void layoutSaved(cabana::pj_layout::LayoutModel layout);
  void perfReport(cabana::pj_engine::EnginePerfStats stats);
  void batchApplied();

private:
  void handleBatch(const ParsedBatchPtr &batch);
  void setupCustomMath();
  void emitSnapshots();
  PlotSnapshotBundlePtr buildSnapshotBundle() const;
  CurveTreeSnapshot buildCurveTree() const;

  struct CumulativePerfStats {
    qint64 batch_count = 0;
    qint64 merge_total_ms = 0;
    qint64 merge_max_ms = 0;
    qint64 transform_total_ms = 0;
    qint64 transform_max_ms = 0;
    qint64 snapshot_total_ms = 0;
    qint64 snapshot_max_ms = 0;
  };

  PlotJugglerSession session_;
  ReplayEngine *replay_engine_ = nullptr;
  std::set<std::string> hidden_curves_;
  double tracker_time_ = 0.0;
  double visible_x_min_ = 0.0;
  double visible_x_max_ = 0.0;
  bool paused_ = false;
  cabana::pj_layout::LayoutModel layout_;
  mutable uint64_t snapshot_generation_ = 0;
  bool perf_enabled_ = false;
  CumulativePerfStats cum_perf_;
};

}  // namespace cabana::pj_engine

Q_DECLARE_METATYPE(cabana::pj_engine::PlotSnapshotBundlePtr)
Q_DECLARE_METATYPE(cabana::pj_engine::CurveTreeSnapshot)
Q_DECLARE_METATYPE(cabana::pj_engine::EnginePerfStats)
Q_DECLARE_METATYPE(cabana::pj_layout::LayoutModel)
Q_DECLARE_METATYPE(cabana::pj_layout::SnippetModel)
Q_DECLARE_METATYPE(cabana::pj_layout::TransformConfig)
Q_DECLARE_METATYPE(SegmentMap)
