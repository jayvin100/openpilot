#include "tools/cabana/pjwindow.h"

#include <chrono>
#include <cstdlib>
#include <thread>

#include <QAction>
#include <QCloseEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>

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

  // Place the playback toolbar under the video dock.
  if (auto *tb = pj_widget->takeToolbar()) {
    auto *dock_container = new QWidget(this);
    auto *dock_layout = new QVBoxLayout(dock_container);
    dock_layout->setContentsMargins(0, 0, 0, 0);
    dock_layout->setSpacing(0);
    // Video will be added later in startStream; reserve the spot.
    dock_layout->addStretch(1);
    dock_layout->addWidget(tb);
    video_dock->setWidget(dock_container);
  }
  tracker_update_timer = new QTimer(this);
  tracker_update_timer->setSingleShot(true);
  tracker_update_timer->setInterval(250);
  connect(tracker_update_timer, &QTimer::timeout, this, &PlotJugglerWindow::flushTrackerUpdate);
  perf_enabled = qEnvironmentVariableIsSet("CABANA_PJ_PERF");
  if (perf_enabled) {
    perf_label = new QLabel(this);
    statusBar()->addPermanentWidget(perf_label, 1);
  }

  QSettings pj_settings("cabana");
  restoreGeometry(pj_settings.value("pj_geometry").toByteArray());
  restoreState(pj_settings.value("pj_window_state").toByteArray());

  startStream(stream);
  resizeDocks({video_dock}, {420}, Qt::Horizontal);
  show();

  bool screenshot_delay_ok = false;
  const int screenshot_delay_ms =
      qEnvironmentVariableIntValue("CABANA_PJ_SCREENSHOT_DELAY_MS", &screenshot_delay_ok);
  const bool exit_after_screenshot = qEnvironmentVariableIsSet("CABANA_PJ_SCREENSHOT_EXIT");
  const QString screenshot_path = qEnvironmentVariable("CABANA_PJ_SCREENSHOT");
  if (!screenshot_path.isEmpty()) {
    auto capture = [this, screenshot_path, exit_after_screenshot]() {
      if (screenshot_captured_) {
        return;
      }
      screenshot_captured_ = true;
      grab().save(screenshot_path);
      if (exit_after_screenshot) {
        std::thread([]() {
          std::this_thread::sleep_for(std::chrono::seconds(2));
          std::_Exit(0);
        }).detach();
        qApp->exit(0);
      }
    };
    if (screenshot_delay_ok) {
      QTimer::singleShot(screenshot_delay_ms, this, capture);
    } else {
      connect(pj_widget, &CabanaPlotJugglerWidget::captureReady, this, capture);
    }
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
  // Insert video into the dock container (toolbar is already at the bottom).
  if (auto *container = video_dock->widget()) {
    auto *vl = qobject_cast<QVBoxLayout *>(container->layout());
    if (vl) {
      // Replace the stretch with the video widget.
      auto *stretch = vl->itemAt(0);
      if (stretch) vl->removeItem(stretch);
      delete stretch;
      vl->insertWidget(0, video_widget, 1);
    }
  } else {
    video_dock->setWidget(video_widget);
  }
  video_dock->setWindowTitle(QString::fromStdString(can->routeName()));

  connect(can, &AbstractStream::seekedTo, this, [this](double sec) { scheduleTrackerUpdate(sec, true); });
  connect(can, &AbstractStream::paused, this, [this]() {
    playback_paused_ = true;
    pj_widget->setPlaybackPaused(true);
    if (can) scheduleTrackerUpdate(can->currentSec(), true);
  });
  connect(can, &AbstractStream::resume, this, [this]() {
    playback_paused_ = false;
    pj_widget->setPlaybackPaused(false);
    if (can) scheduleTrackerUpdate(can->currentSec(), false);
  });
  connect(can, &AbstractStream::msgsReceived, this, [this](const std::set<MessageId> *, bool) {
    if (can && !playback_paused_) {
      scheduleTrackerUpdate(can->currentSec(), false);
    }
  });
  connect(can, &AbstractStream::eventsMerged, this, [this](const MessageEventsMap &) { syncSegments(); });

  // Toolbar → stream control.
  connect(pj_widget, &CabanaPlotJugglerWidget::playPauseRequested, this, [this](bool paused) {
    if (can) can->pause(paused);
  });
  connect(pj_widget, &CabanaPlotJugglerWidget::seekSliderMoved, this, [this](double sec) {
    if (can) can->seekTo(sec);
  });
  connect(pj_widget, &CabanaPlotJugglerWidget::speedChanged, this, [this](double rate) {
    if (can) can->setSpeed(rate);
  });

  syncSegments();
  scheduleTrackerUpdate(can->currentSec(), true);

  // Set route duration for timeline — retry until maxSeconds is available.
  auto *duration_timer = new QTimer(this);
  duration_timer->setInterval(500);
  connect(duration_timer, &QTimer::timeout, this, [this, duration_timer]() {
    double dur = can->maxSeconds() - can->minSeconds();
    if (dur > 0) {
      pj_widget->setRouteDuration(dur);
      duration_timer->stop();
      duration_timer->deleteLater();
    }
  });
  duration_timer->start();
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

void PlotJugglerWindow::scheduleTrackerUpdate(double sec, bool immediate) {
  pending_tracker_sec = sec;
  tracker_update_pending = true;
  if (immediate) {
    if (tracker_update_timer->isActive()) {
      tracker_update_timer->stop();
    }
    flushTrackerUpdate();
    return;
  }
  if (!tracker_update_timer->isActive()) {
    tracker_update_timer->start();
  }
}

void PlotJugglerWindow::flushTrackerUpdate() {
  if (!tracker_update_pending || !pj_widget) return;
  tracker_update_pending = false;
  QElapsedTimer timer;
  if (perf_enabled) timer.start();
  pj_widget->setCurrentTime(pending_tracker_sec);
  if (perf_enabled) {
    const qint64 elapsed = timer.elapsed();
    if (elapsed >= 16) {
      qInfo().noquote() << QString("CABANA_PJ_PERF tracker_flush=%1ms t=%2")
                               .arg(elapsed)
                               .arg(pending_tracker_sec, 0, 'f', 3);
    }
    if (perf_label) {
      perf_label->setText(pj_widget->perfSummary() + QString(" | flush %1ms").arg(elapsed));
    }
  }
}
