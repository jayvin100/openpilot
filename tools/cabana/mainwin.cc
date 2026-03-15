#include "tools/cabana/mainwin.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include <QAbstractButton>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopWidget>
#include <QDialog>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QShortcut>
#include <QTabBar>
#include <QTimer>
#include <QTextDocument>
#include <QToolBar>
#include <QToolButton>
#include <QUndoView>
#include <QVBoxLayout>
#include <QWidgetAction>

#include "tools/cabana/commands.h"
#include "tools/cabana/smoketest.h"
#include "tools/cabana/streamselector.h"
#include "tools/cabana/tools/findsignal.h"
#include "tools/cabana/utils/export.h"
#include "tools/replay/py_downloader.h"
#include "tools/replay/util.h"

namespace {

QJsonArray rectToJson(const QRect &rect) {
  return QJsonArray{rect.x(), rect.y(), rect.width(), rect.height()};
}

QString messageIdToString(const MessageId &id) {
  return QString::fromStdString(id.toString());
}

QString normalizedText(QString text) {
  return text.remove('&');
}

QJsonObject describeWidget(QWidget *widget) {
  QJsonObject obj;
  obj["object_name"] = widget->objectName();
  obj["class_name"] = widget->metaObject()->className();
  obj["visible"] = widget->isVisible();
  obj["enabled"] = widget->isEnabled();
  obj["rect"] = rectToJson(QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()));
  if (!widget->windowTitle().isEmpty()) obj["window_title"] = widget->windowTitle();
  if (auto *message_box = qobject_cast<QMessageBox *>(widget)) {
    obj["text"] = message_box->text();
    if (!message_box->informativeText().isEmpty()) obj["informative_text"] = message_box->informativeText();
    if (!message_box->detailedText().isEmpty()) obj["detailed_text"] = message_box->detailedText();
  } else if (auto *line_edit = qobject_cast<QLineEdit *>(widget)) {
    obj["text"] = line_edit->text();
    obj["placeholder_text"] = line_edit->placeholderText();
  } else if (auto *label = qobject_cast<QLabel *>(widget)) {
    obj["text"] = label->text();
  } else if (auto *button = qobject_cast<QAbstractButton *>(widget)) {
    obj["text"] = button->text();
    obj["checked"] = button->isCheckable() ? button->isChecked() : false;
  } else if (auto *combo = qobject_cast<QComboBox *>(widget)) {
    obj["text"] = combo->currentText();
  } else if (auto *tab_bar = qobject_cast<QTabBar *>(widget)) {
    QJsonArray tabs;
    for (int i = 0; i < tab_bar->count(); ++i) {
      tabs.append(tab_bar->tabText(i));
    }
    obj["tabs"] = tabs;
    obj["current_index"] = tab_bar->currentIndex();
  }
  if (auto *dialog = qobject_cast<QDialog *>(widget)) {
    obj["modal"] = dialog->isModal();
  }
  return obj;
}

void collectMenuActions(QMenu *menu, const QString &prefix, QJsonArray &actions) {
  for (auto *action : menu->actions()) {
    if (action->isSeparator()) continue;

    const QString text = normalizedText(action->text());
    const QString path = prefix.isEmpty() ? text : prefix + ">" + text;
    if (auto *submenu = action->menu()) {
      collectMenuActions(submenu, path, actions);
      continue;
    }

    QJsonObject obj;
    obj["path"] = path;
    obj["text"] = text;
    obj["enabled"] = action->isEnabled();
    obj["visible"] = action->isVisible();
    obj["checkable"] = action->isCheckable();
    obj["checked"] = action->isChecked();
    obj["tooltip"] = action->toolTip();
    obj["shortcut"] = action->shortcut().toString();
    actions.append(obj);
  }
}

}  // namespace

MainWindow::MainWindow(AbstractStream *stream, const QString &dbc_file) : QMainWindow() {
  setObjectName("CabanaMainWindow");
  loadFingerprints();
  createDockWindows();
  setCentralWidget(center_widget = new CenterWidget(this));
  center_widget->setObjectName("CenterWidget");
  createActions();
  menuBar()->setObjectName("MainMenuBar");
  createStatusBar();
  createShortcuts();

  // save default window state to allow resetting it
  default_state = saveState();

  // restore states
  if (cabana::smoketest::enabled()) {
    if (auto size = cabana::smoketest::forcedWindowSize()) {
      resize(*size);
      setMinimumSize(*size);
      setMaximumSize(*size);
    }
  } else {
    restoreGeometry(settings.geometry);
    if (isMaximized()) {
      setGeometry(QApplication::desktop()->availableGeometry(this));
    }
    restoreState(settings.window_state);
  }

  // install handlers
  static auto static_main_win = this;
  qRegisterMetaType<uint64_t>("uint64_t");
  qRegisterMetaType<SourceSet>("SourceSet");
  installDownloadProgressHandler([](uint64_t cur, uint64_t total, bool success) {
    emit static_main_win->updateProgressBar(cur, total, success);
  });
  qInstallMessageHandler([](QtMsgType type, const QMessageLogContext &context, const QString &msg) {
    if (type == QtDebugMsg) return;
    emit static_main_win->showMessage(msg, 2000);
  });
  installMessageHandler([](ReplyMsgType type, const std::string msg) { qInfo() << msg.c_str(); });

  setStyleSheet(QString(R"(QMainWindow::separator {
    width: %1px; /* when vertical */
    height: %1px; /* when horizontal */
  })").arg(style()->pixelMetric(QStyle::PM_SplitterWidth)));

  QObject::connect(this, &MainWindow::showMessage, statusBar(), &QStatusBar::showMessage);
  QObject::connect(this, &MainWindow::updateProgressBar, this, &MainWindow::updateDownloadProgress);
  QObject::connect(dbc(), &DBCManager::DBCFileChanged, this, &MainWindow::DBCFileChanged);
  QObject::connect(UndoStack::instance(), &QUndoStack::cleanChanged, this, &MainWindow::undoStackCleanChanged);
  QObject::connect(&settings, &Settings::changed, this, &MainWindow::updateStatus);

  if (cabana::smoketest::enabled()) {
    auto *ui_tick_timer = new QTimer(this);
    QObject::connect(ui_tick_timer, &QTimer::timeout, this, []() { cabana::smoketest::noteUiTick(); });
    ui_tick_timer->start(5);
    if (!cabana::smoketest::validationStatePath().empty()) {
      auto *validation_timer = new QTimer(this);
      QObject::connect(validation_timer, &QTimer::timeout, this, &MainWindow::writeValidationSnapshot);
      validation_timer->start(50);
    }
  }

  QTimer::singleShot(0, this, [this]() {
    cabana::smoketest::recordWindowShown(this);
    maybeFinalizeSmokeTest();
  });
  QTimer::singleShot(0, this, [=]() { stream ? openStream(stream, dbc_file) : selectAndOpenStream(); });
  show();
}

void MainWindow::loadFingerprints() {
  QFile json_file(QApplication::applicationDirPath() + "/dbc/car_fingerprint_to_dbc.json");
  if (json_file.open(QIODevice::ReadOnly)) {
    fingerprint_to_dbc = QJsonDocument::fromJson(json_file.readAll());
  }
}

void MainWindow::createActions() {
  // File menu
  QMenu *file_menu = menuBar()->addMenu(tr("&File"));
  file_menu->addAction(tr("Open Stream..."), this, &MainWindow::selectAndOpenStream);
  close_stream_act = file_menu->addAction(tr("Close stream"), this, &MainWindow::closeStream);
  export_to_csv_act = file_menu->addAction(tr("Export to CSV..."), this, &MainWindow::exportToCSV);
  close_stream_act->setEnabled(false);
  export_to_csv_act->setEnabled(false);
  file_menu->addSeparator();

  file_menu->addAction(tr("New DBC File"), [this]() { newFile(); }, QKeySequence::New);
  file_menu->addAction(tr("Open DBC File..."), [this]() { openFile(); }, QKeySequence::Open);

  manage_dbcs_menu = file_menu->addMenu(tr("Manage &DBC Files"));
  QObject::connect(manage_dbcs_menu, &QMenu::aboutToShow, this, &MainWindow::updateLoadSaveMenus);

  open_recent_menu = file_menu->addMenu(tr("Open &Recent"));
  QObject::connect(open_recent_menu, &QMenu::aboutToShow, this, &MainWindow::updateRecentFileMenu);

  file_menu->addSeparator();
  QMenu *load_opendbc_menu = file_menu->addMenu(tr("Load DBC from commaai/opendbc"));
  // load_opendbc_menu->setStyleSheet("QMenu { menu-scrollable: true; }");
  for (const auto &dbc_name : QDir(OPENDBC_FILE_PATH).entryList({"*.dbc"}, QDir::Files, QDir::Name)) {
    load_opendbc_menu->addAction(dbc_name, [this, name = dbc_name]() { loadDBCFromOpendbc(name); });
  }

  file_menu->addAction(tr("Load DBC From Clipboard"), [=]() { loadFromClipboard(); });

  file_menu->addSeparator();
  save_dbc = file_menu->addAction(tr("Save DBC..."), this, &MainWindow::save, QKeySequence::Save);
  save_dbc_as = file_menu->addAction(tr("Save DBC As..."), this, &MainWindow::saveAs, QKeySequence::SaveAs);
  copy_dbc_to_clipboard = file_menu->addAction(tr("Copy DBC To Clipboard"), this, &MainWindow::saveToClipboard);

  file_menu->addSeparator();
  file_menu->addAction(tr("Settings..."), this, &MainWindow::setOption, QKeySequence::Preferences);

  file_menu->addSeparator();
  file_menu->addAction(tr("E&xit"), qApp, &QApplication::closeAllWindows, QKeySequence::Quit);

  // Edit Menu
  QMenu *edit_menu = menuBar()->addMenu(tr("&Edit"));
  auto undo_act = UndoStack::instance()->createUndoAction(this, tr("&Undo"));
  undo_act->setShortcuts(QKeySequence::Undo);
  edit_menu->addAction(undo_act);
  auto redo_act = UndoStack::instance()->createRedoAction(this, tr("&Redo"));
  redo_act->setShortcuts(QKeySequence::Redo);
  edit_menu->addAction(redo_act);
  edit_menu->addSeparator();

  QMenu *commands_menu = edit_menu->addMenu(tr("Command &List"));
  QWidgetAction *commands_act = new QWidgetAction(this);
  commands_act->setDefaultWidget(new QUndoView(UndoStack::instance()));
  commands_menu->addAction(commands_act);

  // View Menu
  QMenu *view_menu = menuBar()->addMenu(tr("&View"));
  auto act = view_menu->addAction(tr("Full Screen"), this, &MainWindow::toggleFullScreen, QKeySequence::FullScreen);
  addAction(act);
  view_menu->addSeparator();
  view_menu->addAction(messages_dock->toggleViewAction());
  view_menu->addAction(video_dock->toggleViewAction());
  view_menu->addSeparator();
  view_menu->addAction(tr("Reset Window Layout"), [this]() { restoreState(default_state); });

  // Tools Menu
  tools_menu = menuBar()->addMenu(tr("&Tools"));
  tools_menu->addAction(tr("Find &Similar Bits"), this, &MainWindow::findSimilarBits);
  tools_menu->addAction(tr("&Find Signal"), this, &MainWindow::findSignal);

  // Help Menu
  QMenu *help_menu = menuBar()->addMenu(tr("&Help"));
  help_menu->addAction(tr("Help"), this, &MainWindow::onlineHelp, QKeySequence::HelpContents);
  help_menu->addAction(tr("About &Qt"), qApp, &QApplication::aboutQt);
}

void MainWindow::createDockWindows() {
  messages_dock = new QDockWidget(tr("MESSAGES"), this);
  messages_dock->setObjectName("MessagesPanel");
  messages_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea | Qt::TopDockWidgetArea | Qt::BottomDockWidgetArea);
  messages_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
  addDockWidget(Qt::LeftDockWidgetArea, messages_dock);

  video_dock = new QDockWidget("", this);
  video_dock->setObjectName(tr("VideoPanel"));
  video_dock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
  video_dock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable | QDockWidget::DockWidgetClosable);
  addDockWidget(Qt::RightDockWidgetArea, video_dock);
}

void MainWindow::createDockWidgets() {
  messages_widget = new MessagesWidget(this);
  messages_dock->setWidget(messages_widget);
  QObject::connect(messages_widget, &MessagesWidget::titleChanged, messages_dock, &QDockWidget::setWindowTitle);
  QObject::connect(messages_widget, &MessagesWidget::msgSelectionChanged, center_widget, &CenterWidget::setMessage);

  // right panel
  charts_widget = new ChartsWidget(this);
  QWidget *charts_container = new QWidget(this);
  charts_layout = new QVBoxLayout(charts_container);
  charts_layout->setContentsMargins(0, 0, 0, 0);
  charts_layout->addWidget(charts_widget);

  // splitter between video and charts
  video_splitter = new QSplitter(Qt::Vertical, this);
  video_widget = new VideoWidget(this);
  video_splitter->addWidget(video_widget);

  video_splitter->addWidget(charts_container);
  video_splitter->setStretchFactor(1, 1);
  video_splitter->restoreState(settings.video_splitter_state);
  video_splitter->handle(1)->setEnabled(!can->liveStreaming());
  video_dock->setWidget(video_splitter);
  QObject::connect(charts_widget, &ChartsWidget::toggleChartsDocking, this, &MainWindow::toggleChartsDocking);
  QObject::connect(charts_widget, &ChartsWidget::showTip, video_widget, &VideoWidget::showThumbnail);
}

void MainWindow::createStatusBar() {
  progress_bar = new QProgressBar();
  progress_bar->setObjectName("DownloadProgressBar");
  progress_bar->setRange(0, 100);
  progress_bar->setTextVisible(true);
  progress_bar->setFixedSize({300, 16});
  progress_bar->setVisible(false);
  statusBar()->addWidget(new QLabel(tr("For Help, Press F1")));
  statusBar()->setObjectName("MainStatusBar");
  statusBar()->addPermanentWidget(progress_bar);
  statusBar()->addPermanentWidget(status_label = new QLabel(this));
  status_label->setObjectName("StatusLabel");
  updateStatus();
}

void MainWindow::createShortcuts() {
  auto shortcut = new QShortcut(QKeySequence(Qt::Key_Space), this, nullptr, nullptr, Qt::ApplicationShortcut);
  QObject::connect(shortcut, &QShortcut::activated, this, []() {
    if (can) can->pause(!can->isPaused());
  });
  // TODO: add more shortcuts here.
}

void MainWindow::undoStackCleanChanged(bool clean) {
  setWindowModified(!clean);
}

void MainWindow::DBCFileChanged() {
  UndoStack::instance()->clear();

  // Update file menu
  int cnt = dbc()->nonEmptyDBCCount();
  save_dbc->setText(cnt > 1 ? tr("Save %1 DBCs...").arg(cnt) : tr("Save DBC..."));
  save_dbc->setEnabled(cnt > 0);
  save_dbc_as->setEnabled(cnt == 1);
  // TODO: Support clipboard for multiple files
  copy_dbc_to_clipboard->setEnabled(cnt == 1);
  manage_dbcs_menu->setEnabled(dynamic_cast<DummyStream *>(can) == nullptr);

  QStringList title;
  for (auto f : dbc()->allDBCFiles()) {
    title.push_back(tr("(%1) %2").arg(QString::fromStdString(toString(dbc()->sources(f))), QString::fromStdString(f->name())));
  }
  setWindowFilePath(title.join(" | "));

  if (cabana::smoketest::enabled()) {
    if (cabana::smoketest::sessionRestoreEnabled()) {
      QTimer::singleShot(0, this, [this]() {
        restoreSessionState();
        maybeFinalizeSmokeTest();
      });
    } else {
      maybeFinalizeSmokeTest();
    }
  } else {
    QTimer::singleShot(0, this, &::MainWindow::restoreSessionState);
  }
}

void MainWindow::selectAndOpenStream() {
  StreamSelector dlg(this);
  if (dlg.exec()) {
    openStream(dlg.stream(), dlg.dbcFile());
  } else if (!can) {
    openStream(new DummyStream(this));
  }
}

void MainWindow::closeStream() {
  openStream(new DummyStream(this));
  if (dbc()->nonEmptyDBCCount() > 0) {
    emit dbc()->DBCFileChanged();
  }
  statusBar()->showMessage(tr("stream closed"));
}

void MainWindow::exportToCSV() {
  QString dir = QString("%1/%2.csv").arg(settings.last_dir).arg(QString::fromStdString(can->routeName()));
  QString fn = QFileDialog::getSaveFileName(this, "Export stream to CSV file", dir, tr("csv (*.csv)"));
  if (!fn.isEmpty()) {
    utils::exportToCSV(fn);
  }
}

void MainWindow::newFile(SourceSet s) {
  closeFile(s);
  dbc()->open(s, std::string(""), std::string(""));
}

void MainWindow::openFile(SourceSet s) {
  remindSaveChanges();
  QString fn = QFileDialog::getOpenFileName(this, tr("Open File"), settings.last_dir, "DBC (*.dbc)");
  if (!fn.isEmpty()) {
    loadFile(fn, s);
  }
}

void MainWindow::loadFile(const QString &fn, SourceSet s) {
  if (!fn.isEmpty()) {
    closeFile(s);

    QString error;
    if (dbc()->open(s, fn.toStdString(), &error)) {
      updateRecentFiles(fn);
      statusBar()->showMessage(tr("DBC File %1 loaded").arg(fn), 2000);
    } else {
      QMessageBox msg_box(QMessageBox::Warning, tr("Failed to load DBC file"), tr("Failed to parse DBC file %1").arg(fn));
      msg_box.setDetailedText(error);
      msg_box.exec();
    }
  }
}

void MainWindow::loadDBCFromOpendbc(const QString &name) {
  loadFile(QString("%1/%2").arg(OPENDBC_FILE_PATH, name));
}

void MainWindow::loadFromClipboard(SourceSet s, bool close_all) {
  closeFile(s);

  QString dbc_str = QGuiApplication::clipboard()->text();
  QString error;
  bool ret = dbc()->open(s, std::string(""), dbc_str.toStdString(), &error);
  if (ret && dbc()->nonEmptyDBCCount() > 0) {
    QMessageBox::information(this, tr("Load From Clipboard"), tr("DBC Successfully Loaded!"));
  } else {
    QMessageBox msg_box(QMessageBox::Warning, tr("Failed to load DBC from clipboard"), tr("Make sure that you paste the text with correct format."));
    msg_box.setDetailedText(error);
    msg_box.exec();
  }
}

void MainWindow::openStream(AbstractStream *stream, const QString &dbc_file) {
  if (can) {
    QObject::connect(can, &QObject::destroyed, this, [=]() { startStream(stream, dbc_file); });
    can->deleteLater();
  } else {
    startStream(stream, dbc_file);
  }
}

void MainWindow::startStream(AbstractStream *stream, QString dbc_file) {
  center_widget->clear();
  delete messages_widget;
  delete video_splitter;

  can = stream;
  can->setParent(this);  // take ownership
  smoke_test_ready_scheduled = false;
  cabana::smoketest::recordRouteName(can->routeName());

  bool has_stream = dynamic_cast<DummyStream *>(can) == nullptr;
  createDockWidgets();

  loadFile(dbc_file);
  statusBar()->showMessage(tr("Stream [%1] started").arg(QString::fromStdString(can->routeName())), 2000);

  close_stream_act->setEnabled(has_stream);
  export_to_csv_act->setEnabled(has_stream);
  tools_menu->setEnabled(has_stream);

  video_dock->setWindowTitle(QString::fromStdString(can->routeName()));
  if (can->liveStreaming() || video_splitter->sizes()[0] == 0) {
    // display video at minimum size.
    video_splitter->setSizes({1, 1});
  }
  // Don't overwrite already loaded DBC
  if (!dbc()->nonEmptyDBCCount()) {
    newFile();
  }

  QObject::connect(can, &AbstractStream::eventsMerged, this, &MainWindow::eventsMerged);
  if (cabana::smoketest::enabled()) {
    QObject::connect(can, &AbstractStream::msgsReceived, this, [this](const std::set<MessageId> *new_msgs, bool) {
      if (new_msgs != nullptr && !new_msgs->empty()) {
        cabana::smoketest::recordFirstMsgsReceived();
        maybeFinalizeSmokeTest();
      }
    });
  }

  QProgressDialog *wait_dlg = nullptr;
  if (has_stream) {
    wait_dlg = new QProgressDialog(
        can->liveStreaming() ? tr("Waiting for the live stream to start...") : tr("Loading segment data..."),
        tr("&Abort"), 0, 100, this);
    wait_dlg->setWindowModality(Qt::WindowModal);
    wait_dlg->setFixedSize(400, wait_dlg->sizeHint().height());
    QObject::connect(wait_dlg, &QProgressDialog::canceled, this, &MainWindow::close);
    QObject::connect(can, &AbstractStream::eventsMerged, wait_dlg, &QProgressDialog::deleteLater);
    QObject::connect(this, &MainWindow::updateProgressBar, wait_dlg, [=](uint64_t cur, uint64_t total, bool success) {
      wait_dlg->setValue((int)((cur / (double)total) * 100));
    });
  }

  can->start();
  if (wait_dlg != nullptr && !can->allEvents().empty()) {
    wait_dlg->deleteLater();
  }
}

void MainWindow::eventsMerged() {
  cabana::smoketest::recordFirstEventsMerged();
  if (!can->liveStreaming() && std::exchange(car_fingerprint, QString::fromStdString(can->carFingerprint())) != car_fingerprint) {
    video_dock->setWindowTitle(tr("ROUTE: %1  FINGERPRINT: %2")
                                    .arg(QString::fromStdString(can->routeName()))
                                    .arg(car_fingerprint.isEmpty() ? tr("Unknown Car") : car_fingerprint));
    // Don't overwrite already loaded DBC
    if (!dbc()->nonEmptyDBCCount() && fingerprint_to_dbc.object().contains(car_fingerprint)) {
      QTimer::singleShot(0, this, [this]() { loadDBCFromOpendbc(fingerprint_to_dbc[car_fingerprint].toString() + ".dbc"); });
    }
  }
  maybeFinalizeSmokeTest();
}

void MainWindow::maybeFinalizeSmokeTest() {
  if (!cabana::smoketest::enabled() || smoke_test_ready_scheduled || !can || !cabana::smoketest::readyToFinalize()) {
    return;
  }

  if (!dbc()->nonEmptyDBCCount() && !car_fingerprint.isEmpty() && fingerprint_to_dbc.object().contains(car_fingerprint)) {
    return;
  }

  smoke_test_ready_scheduled = true;
  double ready_sec = can->currentSec();
  if (!can->liveStreaming() && !can->isPaused()) {
    can->pause(true);
  }
  if (!can->liveStreaming()) {
    ready_sec = can->minSeconds();
    can->seekTo(ready_sec);
  }
  cabana::smoketest::recordAutoPaused(ready_sec);

  QTimer::singleShot(120, this, [this, ready_sec]() {
    if (messages_widget) messages_widget->repaint();
    if (charts_widget) charts_widget->repaint();
    if (video_widget) video_widget->repaint();
    if (center_widget) center_widget->repaint();
    repaint();
    QCoreApplication::processEvents();
    const auto screenshot_path = cabana::smoketest::screenshotPath();
    if (!screenshot_path.empty()) {
      grab().save(QString::fromStdString(screenshot_path));
    }
    cabana::smoketest::markReady(can ? can->currentSec() : ready_sec);
  });
}

void MainWindow::writeValidationSnapshot() {
  const std::string output_path = cabana::smoketest::validationStatePath();
  if (output_path.empty()) return;

  QJsonObject root;
  root["snapshot_time_ms"] = QString::number(QDateTime::currentMSecsSinceEpoch());
  root["ready"] = cabana::smoketest::isReady();
  root["window_title"] = windowTitle();
  root["window_file_path"] = windowFilePath();
  root["route_name"] = can ? QString::fromStdString(can->routeName()) : "";
  root["car_fingerprint"] = car_fingerprint;
  root["has_stream"] = can && dynamic_cast<DummyStream *>(can) == nullptr;
  root["live_streaming"] = can ? can->liveStreaming() : false;
  root["paused"] = can ? can->isPaused() : true;
  root["current_sec"] = can ? can->currentSec() : 0.0;
  root["min_sec"] = can ? can->minSeconds() : 0.0;
  root["max_sec"] = can ? can->maxSeconds() : 0.0;
  if (can && can->timeRange().has_value()) {
    root["time_range_start"] = can->timeRange()->first;
    root["time_range_end"] = can->timeRange()->second;
  }
  root["status_message"] = statusBar() ? statusBar()->currentMessage() : "";
  root["status_label"] = status_label ? status_label->text() : "";
  root["max_ui_gap_ms"] = cabana::smoketest::maxUiGapMs();
  root["ui_gaps_over_16ms"] = cabana::smoketest::uiGapsOver16Ms();
  root["ui_gaps_over_33ms"] = cabana::smoketest::uiGapsOver33Ms();
  root["ui_gaps_over_50ms"] = cabana::smoketest::uiGapsOver50Ms();
  root["ui_gaps_over_100ms"] = cabana::smoketest::uiGapsOver100Ms();

  QJsonArray actions;
  if (menuBar()) {
    for (auto *action : menuBar()->actions()) {
      if (auto *menu = action->menu()) {
        collectMenuActions(menu, normalizedText(menu->title()), actions);
      }
    }
  }
  root["actions"] = actions;

  QJsonObject widgets;
  for (QWidget *widget : qApp->allWidgets()) {
    if (widget == nullptr || widget->objectName().isEmpty()) continue;
    widgets[widget->objectName()] = describeWidget(widget);
  }
  root["widgets"] = widgets;

  QJsonArray dialogs;
  for (QWidget *widget : qApp->topLevelWidgets()) {
    auto *dialog = qobject_cast<QDialog *>(widget);
    if (!dialog || !dialog->isVisible()) continue;
    dialogs.append(describeWidget(dialog));
  }
  root["dialogs"] = dialogs;

  if (messages_widget != nullptr) {
    QJsonObject messages;
    auto *view = messages_widget->messageView();
    auto *header = messages_widget->messageHeader();
    auto *model = messages_widget->messageModel();
    messages["row_count"] = model ? model->rowCount() : 0;
    if (auto current = messages_widget->currentMessageId()) {
      messages["current_message_id"] = messageIdToString(*current);
    }
    messages["current_row"] = view ? view->currentIndex().row() : -1;

    QJsonObject filters;
    if (header != nullptr && model != nullptr) {
      for (auto it = header->editors.cbegin(); it != header->editors.cend(); ++it) {
        if (!it.value()) continue;
        QString key = it.value()->objectName();
        if (key.isEmpty()) {
          key = normalizedText(model->headerData(it.key(), Qt::Horizontal, Qt::DisplayRole).toString());
        }
        filters[key] = it.value()->text();
      }
    }
    messages["filters"] = filters;

    QJsonArray rows;
    if (view != nullptr && model != nullptr) {
      const int row_limit = std::min(model->rowCount(), 25);
      for (int row = 0; row < row_limit; ++row) {
        const QModelIndex index = model->index(row, 0);
        if (!index.isValid()) continue;

        QJsonObject item;
        if (row < static_cast<int>(model->items_.size())) {
          const auto &entry = model->items_[row];
          item["message_id"] = messageIdToString(entry.id);
          item["name"] = entry.name;
          item["node"] = entry.node;
          item["address"] = QString::number(entry.id.address, 16).toUpper();
          item["source"] = entry.id.source;
        }

        const QRect local_rect = view->visualRect(index);
        item["rect"] = rectToJson(QRect(view->viewport()->mapToGlobal(local_rect.topLeft()), local_rect.size()));
        item["selected"] = view->selectionModel() && view->selectionModel()->currentIndex().row() == row;
        rows.append(item);
      }
    }
    messages["rows"] = rows;
    root["messages"] = messages;
  }

  QJsonArray dbc_files;
  for (auto *dbc_file : dbc()->allDBCFiles()) {
    if (dbc_file == nullptr || dbc_file->isEmpty()) continue;
    QJsonObject dbc_obj;
    dbc_obj["name"] = QString::fromStdString(dbc_file->name());
    dbc_obj["filename"] = QString::fromStdString(dbc_file->filename);
    dbc_obj["sources"] = QString::fromStdString(toString(dbc()->sources(dbc_file)));
    dbc_files.append(dbc_obj);
  }
  root["dbc_files"] = dbc_files;

  if (auto *detail = center_widget ? center_widget->getDetailWidget() : nullptr) {
    QJsonObject detail_obj;
    detail_obj["current_message_id"] = messageIdToString(detail->currentMessageId());
    detail_obj["message_label"] = detail->messageLabel();
    detail_obj["warning_visible"] = detail->warningVisible();
    detail_obj["warning_text"] = detail->warningText();

    if (auto *signal_view = detail->signalView()) {
      auto *tree = signal_view->treeView();
      auto *model = signal_view->signalModel();
      detail_obj["signal_filter"] = signal_view->filterEdit() ? signal_view->filterEdit()->text() : "";
      detail_obj["signal_count"] = model ? model->rowCount() : 0;

      QJsonArray signal_rows;
      if (tree != nullptr && model != nullptr) {
        const int row_limit = std::min(model->rowCount(), 25);
        for (int row = 0; row < row_limit; ++row) {
          const QModelIndex label_index = model->index(row, 0);
          const QModelIndex actions_index = model->index(row, 1);
          if (!label_index.isValid()) continue;

          auto *item = model->getItem(label_index);
          if (!item || item->sig == nullptr) continue;

          QJsonObject row_obj;
          row_obj["row"] = row;
          row_obj["name"] = QString::fromStdString(item->sig->name);
          row_obj["value"] = item->sig_val;
          row_obj["expanded"] = tree->isExpanded(label_index);

          const QRect row_rect = tree->visualRect(label_index);
          row_obj["rect"] = rectToJson(QRect(tree->viewport()->mapToGlobal(row_rect.topLeft()), row_rect.size()));

          if (QWidget *actions_widget = tree->indexWidget(actions_index)) {
            auto *plot_btn = actions_widget->findChild<QToolButton *>("SignalPlotButton");
            auto *remove_btn = actions_widget->findChild<QToolButton *>("SignalRemoveButton");
            if (plot_btn) {
              row_obj["plot_checked"] = plot_btn->isChecked();
              row_obj["plot_rect"] = rectToJson(QRect(plot_btn->mapToGlobal(QPoint(0, 0)), plot_btn->size()));
            }
            if (remove_btn) {
              row_obj["remove_rect"] = rectToJson(QRect(remove_btn->mapToGlobal(QPoint(0, 0)), remove_btn->size()));
            }
          }

          signal_rows.append(row_obj);
        }
      }
      detail_obj["signal_rows"] = signal_rows;
    }

    if (auto *logs = detail->logsWidget()) {
      detail_obj["logs_visible"] = logs->isVisible();
      detail_obj["logs_rect"] = rectToJson(QRect(logs->mapToGlobal(QPoint(0, 0)), logs->size()));
    }
    root["detail"] = detail_obj;
  }

  if (charts_widget != nullptr) {
    QJsonObject charts;
    charts["count"] = charts_widget->chartCount();
    charts["title"] = charts_widget->titleText();
    charts["docked"] = floating_window == nullptr;
    charts["serialized_ids"] = QJsonArray::fromStringList(charts_widget->serializeChartIds());

    QJsonArray rects;
    for (const QRect &rect : charts_widget->chartRects()) {
      rects.append(rectToJson(rect));
    }
    charts["rects"] = rects;
    root["charts"] = charts;
  }

  if (video_widget != nullptr) {
    QJsonObject video;
    if (auto *slider = video_widget->playbackSlider()) {
      video["slider_visible"] = slider->isVisible();
      video["slider_rect"] = rectToJson(QRect(slider->mapToGlobal(QPoint(0, 0)), slider->size()));
      video["slider_min"] = slider->minimum();
      video["slider_max"] = slider->maximum();
      video["slider_value"] = slider->value();
    }
    root["video"] = video;
  }

  const auto json = QJsonDocument(root).toJson(QJsonDocument::Indented);
  std::filesystem::path final_path(output_path);
  std::error_code ec;
  std::filesystem::create_directories(final_path.parent_path(), ec);
  std::filesystem::path tmp_path = final_path;
  tmp_path += ".tmp";

  std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
  if (!out.is_open()) {
    return;
  }
  out.write(json.constData(), json.size());
  out.close();

  std::filesystem::rename(tmp_path, final_path, ec);
  if (ec) {
    std::ofstream fallback(final_path, std::ios::binary | std::ios::trunc);
    if (fallback.is_open()) {
      fallback.write(json.constData(), json.size());
    }
    std::filesystem::remove(tmp_path, ec);
  }
}

void MainWindow::save() {
  // Save all open DBC files
  for (auto dbc_file : dbc()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFile(dbc_file);
  }
}

void MainWindow::saveAs() {
  // Save as all open DBC files. Should not be called with more than 1 file open
  for (auto dbc_file : dbc()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFileAs(dbc_file);
  }
}

void MainWindow::closeFile(SourceSet s) {
  remindSaveChanges();
  if (s == SOURCE_ALL) {
    dbc()->closeAll();
  } else {
    dbc()->close(s);
  }
}

void MainWindow::closeFile(DBCFile *dbc_file) {
  assert(dbc_file != nullptr);
  remindSaveChanges();
  dbc()->close(dbc_file);
  // Ensure we always have at least one file open
  if (dbc()->dbcCount() == 0) {
    newFile();
  }
}

void MainWindow::saveFile(DBCFile *dbc_file) {
  assert(dbc_file != nullptr);
  if (!dbc_file->filename.empty()) {
    dbc_file->save();
    UndoStack::instance()->setClean();
    statusBar()->showMessage(tr("File saved"), 2000);
  } else if (!dbc_file->isEmpty()) {
    saveFileAs(dbc_file);
  }
}

void MainWindow::saveFileAs(DBCFile *dbc_file) {
  QString title = tr("Save File (bus: %1)").arg(QString::fromStdString(toString(dbc()->sources(dbc_file))));
  QString fn = QFileDialog::getSaveFileName(this, title, QDir::cleanPath(settings.last_dir + "/untitled.dbc"), tr("DBC (*.dbc)"));
  if (!fn.isEmpty()) {
    dbc_file->saveAs(fn.toStdString());
    UndoStack::instance()->setClean();
    statusBar()->showMessage(tr("File saved as %1").arg(fn), 2000);
    updateRecentFiles(fn);
  }
}

void MainWindow::saveToClipboard() {
  // Copy all open DBC files to clipboard. Should not be called with more than 1 file open
  for (auto dbc_file : dbc()->allDBCFiles()) {
    if (dbc_file->isEmpty()) continue;
    saveFileToClipboard(dbc_file);
  }
}

void MainWindow::saveFileToClipboard(DBCFile *dbc_file) {
  assert(dbc_file != nullptr);
  QGuiApplication::clipboard()->setText(QString::fromStdString(dbc_file->generateDBC()));
  QMessageBox::information(this, tr("Copy To Clipboard"), tr("DBC Successfully copied!"));
}

void MainWindow::updateLoadSaveMenus() {
  manage_dbcs_menu->clear();

  for (int source : can->sources) {
    if (source >= 64) continue; // Sent and blocked buses are handled implicitly

    SourceSet ss = {source, uint8_t(source + 128), uint8_t(source + 192)};

    QMenu *bus_menu = new QMenu(this);
    bus_menu->addAction(tr("New DBC File..."), [=]() { newFile(ss); });
    bus_menu->addAction(tr("Open DBC File..."), [=]() { openFile(ss); });
    bus_menu->addAction(tr("Load DBC From Clipboard..."), [=]() { loadFromClipboard(ss, false); });

    // Show sub-menu for each dbc for this source.
    auto dbc_file = dbc()->findDBCFile(source);
    if (dbc_file) {
      bus_menu->addSeparator();
      bus_menu->addAction(QString::fromStdString(dbc_file->name()) + " (" + QString::fromStdString(toString(dbc()->sources(dbc_file))) + ")")->setEnabled(false);
      bus_menu->addAction(tr("Save..."), [=]() { saveFile(dbc_file); });
      bus_menu->addAction(tr("Save As..."), [=]() { saveFileAs(dbc_file); });
      bus_menu->addAction(tr("Copy to Clipboard..."), [=]() { saveFileToClipboard(dbc_file); });
      bus_menu->addAction(tr("Remove from this bus..."), [=]() { closeFile(ss); });
      bus_menu->addAction(tr("Remove from all buses..."), [=]() { closeFile(dbc_file); });
    }
    bus_menu->setTitle(tr("Bus %1 (%2)").arg(source).arg(dbc_file ? QString::fromStdString(dbc_file->name()) : "No DBCs loaded"));

    manage_dbcs_menu->addMenu(bus_menu);
  }
}

void MainWindow::updateRecentFiles(const QString &fn) {
  settings.recent_files.removeAll(fn);
  settings.recent_files.prepend(fn);
  while (settings.recent_files.size() > MAX_RECENT_FILES) {
    settings.recent_files.removeLast();
  }
  settings.last_dir = QFileInfo(fn).absolutePath();
}

void MainWindow::updateRecentFileMenu() {
  open_recent_menu->clear();

  int num_recent_files = std::min<int>(settings.recent_files.size(), MAX_RECENT_FILES);
  if (!num_recent_files) {
    open_recent_menu->addAction(tr("No Recent Files"))->setEnabled(false);
    return;
  }

  for (int i = 0; i < num_recent_files; ++i) {
    QString text = tr("&%1 %2").arg(i + 1).arg(QFileInfo(settings.recent_files[i]).fileName());
    open_recent_menu->addAction(text, this, [this, file = settings.recent_files[i]]() { loadFile(file); });
  }
}

void MainWindow::remindSaveChanges() {
  while (!UndoStack::instance()->isClean()) {
    QString text = tr("You have unsaved changes. Press ok to save them, cancel to discard.");
    int ret = QMessageBox::question(this, tr("Unsaved Changes"), text, QMessageBox::Ok | QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) break;
    save();
  }
  UndoStack::instance()->clear();
}

void MainWindow::updateDownloadProgress(uint64_t cur, uint64_t total, bool success) {
  if (success && cur < total) {
    progress_bar->setValue((cur / (double)total) * 100);
    progress_bar->setFormat(tr("Downloading %p% (%1)").arg(formattedDataSize(total).c_str()));
    progress_bar->show();
  } else {
    progress_bar->hide();
  }
}

void MainWindow::updateStatus() {
  status_label->setText(tr("Cached Minutes:%1 FPS:%2").arg(settings.max_cached_minutes).arg(settings.fps));
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == floating_window && event->type() == QEvent::Close) {
    toggleChartsDocking();
    return true;
  }
  return QMainWindow::eventFilter(obj, event);
}

void MainWindow::toggleChartsDocking() {
  if (floating_window) {
    // Dock the charts widget back to the main window
    floating_window->removeEventFilter(this);
    charts_layout->insertWidget(0, charts_widget, 1);
    floating_window->deleteLater();
    floating_window = nullptr;
    charts_widget->setIsDocked(true);
  } else {
    // Float the charts widget in a separate window
    floating_window = new QWidget(this, Qt::Window);
    floating_window->setWindowTitle("Charts");
    floating_window->setLayout(new QVBoxLayout());
    floating_window->layout()->addWidget(charts_widget);
    floating_window->installEventFilter(this);
    floating_window->showMaximized();
    charts_widget->setIsDocked(false);
  }
}

void MainWindow::closeEvent(QCloseEvent *event) {
  remindSaveChanges();

  installDownloadProgressHandler(nullptr);
  qInstallMessageHandler(nullptr);

  if (floating_window)
    floating_window->deleteLater();

  // save states
  settings.geometry = saveGeometry();
  settings.window_state = saveState();
  if (can && !can->liveStreaming()) {
    settings.video_splitter_state = video_splitter->saveState();
  }
  if (messages_widget) {
    settings.message_header_state = messages_widget->saveHeaderState();
  }

  saveSessionState();
  QWidget::closeEvent(event);
}

void MainWindow::setOption() {
  SettingsDlg dlg(this);
  dlg.exec();
}

void MainWindow::findSimilarBits() {
  FindSimilarBitsDlg *dlg = new FindSimilarBitsDlg(this);
  QObject::connect(dlg, &FindSimilarBitsDlg::openMessage, messages_widget, &MessagesWidget::selectMessage);
  dlg->show();
}

void MainWindow::findSignal() {
  FindSignalDlg *dlg = new FindSignalDlg(this);
  QObject::connect(dlg, &FindSignalDlg::openMessage, messages_widget, &MessagesWidget::selectMessage);
  dlg->show();
}

void MainWindow::onlineHelp() {
  if (auto help = findChild<HelpOverlay*>()) {
    help->close();
  } else {
    help = new HelpOverlay(this);
    help->setGeometry(rect());
    help->show();
    help->raise();
  }
}

void MainWindow::toggleFullScreen() {
  if (isFullScreen()) {
    menuBar()->show();
    statusBar()->show();
    showNormal();
    showMaximized();
  } else {
    menuBar()->hide();
    statusBar()->hide();
    showFullScreen();
  }
}

void MainWindow::saveSessionState() {
  settings.recent_dbc_file = "";
  settings.active_msg_id = "";
  settings.selected_msg_ids.clear();
  settings.active_charts.clear();

  for (auto &f : dbc()->allDBCFiles())
    if (!f->isEmpty()) { settings.recent_dbc_file = QString::fromStdString(f->filename); break; }

  if (auto *detail = center_widget->getDetailWidget()) {
    auto [active_id, ids] = detail->serializeMessageIds();
    settings.active_msg_id = active_id;
    settings.selected_msg_ids = ids;
  }
  if (charts_widget)
    settings.active_charts = charts_widget->serializeChartIds();
}

void MainWindow::restoreSessionState() {
  if (settings.recent_dbc_file.isEmpty() || dbc()->nonEmptyDBCCount() == 0) return;

  QString dbc_file;
  for (auto& f : dbc()->allDBCFiles())
    if (!f->isEmpty()) { dbc_file = QString::fromStdString(f->filename); break; }
  if (dbc_file != settings.recent_dbc_file) return;

  if (!settings.selected_msg_ids.isEmpty())
    center_widget->ensureDetailWidget()->restoreTabs(settings.active_msg_id, settings.selected_msg_ids);

  if (charts_widget != nullptr && !settings.active_charts.empty())
    charts_widget->restoreChartsFromIds(settings.active_charts);
}

// HelpOverlay
HelpOverlay::HelpOverlay(MainWindow *parent) : QWidget(parent) {
  setAttribute(Qt::WA_NoSystemBackground, true);
  setAttribute(Qt::WA_TranslucentBackground, true);
  setAttribute(Qt::WA_DeleteOnClose);
  parent->installEventFilter(this);
}

void HelpOverlay::paintEvent(QPaintEvent *event) {
  QPainter painter(this);
  painter.fillRect(rect(), QColor(0, 0, 0, 50));
  auto parent = parentWidget();
  drawHelpForWidget(painter, parent->findChild<MessagesWidget *>());
  drawHelpForWidget(painter, parent->findChild<BinaryView *>());
  drawHelpForWidget(painter, parent->findChild<SignalView *>());
  drawHelpForWidget(painter, parent->findChild<ChartsWidget *>());
  drawHelpForWidget(painter, parent->findChild<VideoWidget *>());
}

void HelpOverlay::drawHelpForWidget(QPainter &painter, QWidget *w) {
  if (w && w->isVisible() && !w->whatsThis().isEmpty()) {
    QPoint pt = mapFromGlobal(w->mapToGlobal(w->rect().center()));
    if (rect().contains(pt)) {
      QTextDocument document;
      document.setHtml(w->whatsThis());
      QSize doc_size = document.size().toSize();
      QPoint topleft = {pt.x() - doc_size.width() / 2, pt.y() - doc_size.height() / 2};
      painter.translate(topleft);
      painter.fillRect(QRect{{0, 0}, doc_size}, palette().toolTipBase());
      document.drawContents(&painter);
      painter.translate(-topleft);
    }
  }
}

bool HelpOverlay::eventFilter(QObject *obj, QEvent *event) {
  if (obj == parentWidget() && event->type() == QEvent::Resize) {
    QResizeEvent *resize_event = (QResizeEvent *)(event);
    setGeometry(QRect{QPoint(0, 0), resize_event->size()});
  }
  return false;
}

void HelpOverlay::mouseReleaseEvent(QMouseEvent *event) {
  close();
}
