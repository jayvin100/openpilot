#pragma once

#include <QDockWidget>
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

  QString layout_file_;
  QDockWidget *video_dock = nullptr;
  VideoWidget *video_widget = nullptr;
  CabanaPlotJugglerWidget *pj_widget = nullptr;
  QTimer playback_sync_timer;
};
