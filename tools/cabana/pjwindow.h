#pragma once

#include <QDockWidget>
#include <QLabel>
#include <QMainWindow>
#include <QTimer>
#include "tools/cabana/streams/abstractstream.h"

class CabanaPlotJugglerWidget;
class ReplayStream;
class VideoWidget;

class PlotJugglerWindow : public QMainWindow {
  Q_OBJECT

public:
  PlotJugglerWindow(AbstractStream *stream, const QString &dbc_file, const QString &layout_file);

protected:
  void closeEvent(QCloseEvent *event) override;

private:
  QString resolveDBC(AbstractStream *stream, const QString &dbc_file) const;
  void startStream(AbstractStream *stream);
  void syncSegments();
  void scheduleTrackerUpdate(double sec, bool immediate);
  void flushTrackerUpdate();

  QString layout_file_;
  QDockWidget *video_dock = nullptr;
  VideoWidget *video_widget = nullptr;
  CabanaPlotJugglerWidget *pj_widget = nullptr;
  QLabel *perf_label = nullptr;
  bool perf_enabled = false;
  QTimer *tracker_update_timer = nullptr;
  double pending_tracker_sec = 0.0;
  bool tracker_update_pending = false;
  bool playback_paused_ = false;
};
