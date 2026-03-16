#pragma once

#include <QDoubleSpinBox>
#include <QPushButton>
#include <QToolBar>

namespace cabana::plot_ui {

/// Toolbar matching PlotJuggler's playback and plot control buttons.
class PlotToolbar : public QToolBar {
  Q_OBJECT

public:
  explicit PlotToolbar(QWidget *parent = nullptr);

  void setPaused(bool paused);

signals:
  void playPauseToggled(bool paused);
  void seekSliderMoved(double sec);
  void speedChanged(double rate);
  void zoomOutAll();
  void gridToggled(bool on);
  void legendToggled();
  void linkedZoomToggled(bool on);
  void saveLayoutRequested();
  void loadLayoutRequested();
  void undoRequested();
  void redoRequested();
  void timeOffsetToggled(bool on);

private:
  QPushButton *play_btn_ = nullptr;
  QDoubleSpinBox *speed_spin_ = nullptr;
  QPushButton *grid_btn_ = nullptr;
  QPushButton *legend_btn_ = nullptr;
  QPushButton *link_btn_ = nullptr;
  bool paused_ = false;
};

}  // namespace cabana::plot_ui
