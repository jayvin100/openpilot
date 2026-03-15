#pragma once

#include <QLineEdit>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

#include <set>

#include "tools/cabana/pj_engine/series_snapshot.h"

namespace cabana::plot_ui {

/// Hierarchical curve list matching PlotJuggler's CurveListPanel.
/// Curves are split by '/' into a tree (e.g. /livePose/inputsOK → livePose → inputsOK).
/// Includes a search/filter bar at the top.
class CurveList : public QWidget {
  Q_OBJECT

public:
  explicit CurveList(QWidget *parent = nullptr);
  ~CurveList() override;

  void updateTree(const cabana::pj_engine::CurveTreeSnapshot &tree);
  void updateValues(const cabana::pj_engine::PlotSnapshotBundle &bundle, double tracker_time);

  static const char *kCurveMimeType;

signals:
  void curveVisibilityChanged(QString curve, bool visible);

private:
  void addCurveToTree(const cabana::pj_engine::CurveEntry &entry);
  void applyFilter(const QString &text);

  QLineEdit *filter_edit_ = nullptr;
  QTreeWidget *tree_widget_ = nullptr;
  std::set<std::string> known_curves_;
};

}  // namespace cabana::plot_ui
