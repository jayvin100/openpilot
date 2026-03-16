#pragma once

#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QToolBar>

namespace cabana::plot_ui {

/// Toolbar matching PlotJuggler's playback and plot control buttons.
class PlotToolbar : public QToolBar {
  Q_OBJECT

public:
  explicit PlotToolbar(QWidget *parent = nullptr);

  void setTime(double sec);
  void setTimeRange(double max_sec);
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
  QLineEdit *time_display_ = nullptr;
  QSlider *seek_slider_ = nullptr;
  QDoubleSpinBox *speed_spin_ = nullptr;
  QPushButton *grid_btn_ = nullptr;
  QPushButton *legend_btn_ = nullptr;
  QPushButton *link_btn_ = nullptr;
  bool paused_ = false;
  double max_time_ = 1.0;
};

}  // namespace cabana::plot_ui
