#include "tools/cabana/pjwindow.h"

#include <QAction>
#include <QCloseEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

#include "tools/cabana/settings.h"
#include "tools/cabana/videowidget.h"
#include "tools/cabana/streams/replaystream.h"
#include "tools/cabana/plotjuggler/cabana_plotjuggler_widget.h"

PlotJugglerWindow::PlotJugglerWindow(AbstractStream *stream, const QString &dbc_file, const QString &layout_file)
    : layout_file_(layout_file) {
  setWindowTitle("Cabana");
  setWindowIcon(QIcon(":cabana-icon.png"));
  resize(1600, 960);

  auto *quit = new QAction(this);
  quit->setShortcut(QKeySequence::Quit);
  addAction(quit);
  connect(quit, &QAction::triggered, qApp, &QApplication::closeAllWindows);

  auto *fullscreen = new QAction(this);
  fullscreen->setShortcut(QKeySequence::FullScreen);
  addAction(fullscreen);
  connect(fullscreen, &QAction::triggered, this, [this]() { isFullScreen() ? showNormal() : showFullScreen(); });

  video_dock = new QDockWidget("", this);
  video_dock->setObjectName("PlotJugglerVideoPanel");
  video_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  video_dock->setMinimumWidth(360);
  addDockWidget(Qt::RightDockWidgetArea, video_dock);

  pj_widget = new CabanaPlotJugglerWidget(resolveDBC(stream, dbc_file), layout_file_, this);
  setCentralWidget(pj_widget);
  setDockNestingEnabled(false);

  QSettings pj_settings("cabana");
  restoreGeometry(pj_settings.value("pj_geometry").toByteArray());
  restoreState(pj_settings.value("pj_window_state").toByteArray());

  startStream(stream);
  resizeDocks({video_dock}, {420}, Qt::Horizontal);
  playback_sync_timer.setInterval(100);
  connect(&playback_sync_timer, &QTimer::timeout, this, [this]() {
    if (can) {
      pj_widget->setCurrentTime(can->currentSec());
    }
  });
  playback_sync_timer.start();
  show();

  bool screenshot_delay_ok = false;
  const int screenshot_delay_ms =
      qEnvironmentVariableIntValue("CABANA_PJ_SCREENSHOT_DELAY_MS", &screenshot_delay_ok);
  const int capture_delay_ms = screenshot_delay_ok ? screenshot_delay_ms : 20000;
  const bool exit_after_screenshot = qEnvironmentVariableIsSet("CABANA_PJ_SCREENSHOT_EXIT");
  const QString screenshot_path = qEnvironmentVariable("CABANA_PJ_SCREENSHOT");
  if (!screenshot_path.isEmpty()) {
    QTimer::singleShot(capture_delay_ms, this, [this, screenshot_path, exit_after_screenshot]() {
      grab().save(screenshot_path);
      if (exit_after_screenshot) {
        qApp->quit();
      }
    });
  }
}

QString PlotJugglerWindow::resolveDBC(AbstractStream *stream, const QString &dbc_file) const {
  if (!dbc_file.isEmpty()) {
    return dbc_file;
  }

  QFile json_file(QApplication::applicationDirPath() + "/dbc/car_fingerprint_to_dbc.json");
  if (!json_file.open(QIODevice::ReadOnly)) {
    return QString::fromStdString(stream->carFingerprint());
  }

  const auto json = QJsonDocument::fromJson(json_file.readAll()).object();
  const auto fingerprint = QString::fromStdString(stream->carFingerprint());
  return json.value(fingerprint).toString(fingerprint);
}

void PlotJugglerWindow::startStream(AbstractStream *stream) {
  can = stream;
  can->setParent(this);
  can->start();

  video_widget = new VideoWidget(this);
  video_dock->setWidget(video_widget);
  video_dock->setWindowTitle(QString::fromStdString(can->routeName()));

  connect(can, &AbstractStream::seekedTo, pj_widget, &CabanaPlotJugglerWidget::setCurrentTime);
  connect(can, &AbstractStream::paused, this, [this]() { pj_widget->setPlaybackPaused(true); });
  connect(can, &AbstractStream::resume, this, [this]() { pj_widget->setPlaybackPaused(false); });
  connect(can, &AbstractStream::eventsMerged, this, [this](const MessageEventsMap &) { syncSegments(); });

  syncSegments();
  pj_widget->setCurrentTime(can->currentSec());
}

void PlotJugglerWindow::syncSegments() {
  auto *replay_stream = qobject_cast<ReplayStream *>(can);
  if (!replay_stream) return;

  auto replay = replay_stream->getReplay();
  pj_widget->appendSegments(replay->getEventData()->segments, replay->routeStartNanos());
}

void PlotJugglerWindow::closeEvent(QCloseEvent *event) {
  QSettings pj_settings("cabana");
  pj_settings.setValue("pj_geometry", saveGeometry());
  pj_settings.setValue("pj_window_state", saveState());
  QMainWindow::closeEvent(event);
}
