#pragma once

#include <QTabWidget>
#include <QWidget>

#include <deque>
#include <vector>

#include <QElapsedTimer>

#include "tools/cabana/cabana_plot_ui/plot_container.h"
#include "tools/cabana/pj_engine/series_snapshot.h"
#include "tools/cabana/pj_layout/layout_model.h"

namespace cabana::plot_ui {

/// Builds a tab/splitter hierarchy from LayoutModel and renders all plots
/// from immutable PlotSnapshotBundle data.
class PlotTabWidget : public QWidget {
  Q_OBJECT

public:
  explicit PlotTabWidget(QWidget *parent = nullptr);
  ~PlotTabWidget() override;

  void rebuildFromLayout(const cabana::pj_layout::LayoutModel &layout);
  void updateSnapshots(cabana::pj_engine::PlotSnapshotBundlePtr bundle);
  void setTrackerTime(double time);
  void setLinkedZoom(bool enabled);
  void setGridVisible(bool visible);
  void cycleLegend();
  void zoomOutAll();
  void undo();
  void redo();
  void snapshotForUndo();

signals:
  void seekRequested(double time);
  void curveDropped(QString curve_name, int plot_index);
  void splitRequested(int plot_index, Qt::Orientation orientation);
  void tabCreateRequested(QString name);
  void tabCloseRequested(int index);

protected:
  bool eventFilter(QObject *obj, QEvent *event) override;

private:
  QWidget *buildLayoutNode(const cabana::pj_layout::LayoutNode &node, QWidget *parent);
  void onTabCloseRequested(int index);

  QTabWidget *tab_widget_ = nullptr;
  std::vector<PlotContainer *> all_plots_;
  std::vector<std::vector<PlotContainer *>> plots_per_tab_;
  cabana::pj_engine::PlotSnapshotBundlePtr last_bundle_;
  std::deque<cabana::pj_layout::LayoutModel> undo_stack_;
  std::deque<cabana::pj_layout::LayoutModel> redo_stack_;
  cabana::pj_layout::LayoutModel current_layout_;
  QElapsedTimer undo_timer_;
  bool linked_zoom_ = true;
};

}  // namespace cabana::plot_ui
