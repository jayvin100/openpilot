/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <functional>
#include <stdio.h>
#include <numeric>

#include <QApplication>
#include <QActionGroup>
#include <QCheckBox>
#include <QDebug>
#include <QDesktopServices>
#include <QDomDocument>
#include <QDoubleSpinBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QMenu>
#include <QMenuBar>
#include <QGroupBox>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPushButton>
#include <QKeySequence>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QStringListModel>
#include <QStringRef>
#include <QThread>
#include <QTextStream>
#include <QWindow>
#include <QHeaderView>
#include <QStandardPaths>
#include <QXmlStreamReader>

#include "mainwindow.h"
#include "curvelist_panel.h"
#include "tabbedplotwidget.h"
#include "PlotJuggler/plotdata.h"
#include "qwt_plot_canvas.h"
#include "transforms/function_editor.h"
#include "transforms/lua_custom_function.h"
#include "utils.h"
#include "stylesheet.h"
#include "PlotJuggler/svg_util.h"
#include "PlotJuggler/reactive_function.h"
#include "plotjuggler_plugins/ToolboxLuaEditor/lua_editor.h"

#include "preferences_dialog.h"

using cabana::pj_layout::LayoutModel;
using cabana::pj_layout::PluginState;
using cabana::pj_layout::SnippetModel;

namespace {

SnippetModel ToSnippetModel(const SnippetData& snippet)
{
  return {
    .name = snippet.alias_name,
    .global_vars = snippet.global_vars,
    .function = snippet.function,
    .linked_source = snippet.linked_source,
    .additional_sources = snippet.additional_sources,
  };
}

SnippetData ToSnippetData(const SnippetModel& snippet)
{
  return {
    .alias_name = snippet.name,
    .global_vars = snippet.global_vars,
    .function = snippet.function,
    .linked_source = snippet.linked_source,
    .additional_sources = snippet.additional_sources,
  };
}

PluginState ToPluginStateModel(const QDomElement& element)
{
  PluginState state;
  state.id = element.attribute("ID");
  for (QDomElement child = element.firstChildElement(); !child.isNull();
       child = child.nextSiblingElement())
  {
    state.children.push_back(cabana::pj_layout::FromDomElement(child));
  }
  return state;
}

QDomElement ToPluginDomElement(const PluginState& plugin_state, QDomDocument* document)
{
  auto plugin_element = document->createElement("plugin");
  plugin_element.setAttribute("ID", plugin_state.id);
  for (const auto& child : plugin_state.children)
  {
    plugin_element.appendChild(cabana::pj_layout::ToDomElement(child, document));
  }
  return plugin_element;
}

}  // namespace

MainWindow::MainWindow(const MainWindowConfig& config, QWidget* parent)
  : QMainWindow(parent)
  , ui(new Ui::MainWindow)
  , _undo_shortcut(QKeySequence(Qt::CTRL + Qt::Key_Z), this)
  , _redo_shortcut(QKeySequence(Qt::CTRL + Qt::SHIFT + Qt::Key_Z), this)
  , _fullscreen_shortcut(Qt::Key_F10, this)
  , _playback_shotcut(Qt::Key_Space, this)
  , _minimized(false)
  , _mapped_plot_data(_session.plotData())
  , _transform_functions(_session.transforms())
  , _disable_undo_logging(false)
  , _tracker_time(0)
  , _tracker_param(CurveTracker::VALUE)
  , _labels_status(LabelStatus::RIGHT)
  , _embedded_mode(parent != nullptr)
  , _recent_layout_files(new QMenu())
{
  QLocale::setDefault(QLocale::c());  // set as default
  setAcceptDrops(true);
  if (_embedded_mode)
  {
    // Force the imported PJ shell to behave like a child widget inside Cabana,
    // not a separate top-level window managed by the window system.
    setWindowFlag(Qt::Widget, true);
    setWindowFlag(Qt::Window, false);
  }

  _curvelist_widget = new CurveListPanel(_mapped_plot_data, _transform_functions, this);

  ui->setupUi(this);

  auto* menu_bar = new QMenuBar(this);
  auto* app_menu = menu_bar->addMenu(tr("App"));
  _tools_menu = menu_bar->addMenu(tr("Tools"));
  auto* help_menu = menu_bar->addMenu(tr("Help"));
  app_menu->addAction(ui->actionPreferences);
  app_menu->addAction(ui->actionLoadStyleSheet);
  app_menu->addSeparator();
  app_menu->addAction(ui->actionExit);
  auto* about_qt = help_menu->addAction(tr("About Qt"));
  connect(about_qt, &QAction::triggered, qApp, &QApplication::aboutQt);
  menu_bar->setNativeMenuBar(false);
  setMenuBar(menu_bar);

  if (!config.window_title.isEmpty())
  {
    setWindowTitle(config.window_title);
  }

  QSettings settings;

  ui->playbackLoop->setText("");
  ui->pushButtonZoomOut->setText("");
  ui->pushButtonPlay->setText("");
  ui->pushButtonUseDateTime->setText("");
  ui->pushButtonActivateGrid->setText("");
  ui->pushButtonRatio->setText("");
  ui->pushButtonLink->setText("");
  ui->pushButtonTimeTracker->setText("");
  ui->pushButtonRemoveTimeOffset->setText("");
  ui->pushButtonLegend->setText("");

  _tracker_delay.connectCallback([this]() {
    updatedDisplayTime();
    onUpdateLeftTableValues();
  });

  connect(_curvelist_widget, &CurveListPanel::hiddenItemsChanged, this,
          &MainWindow::onUpdateLeftTableValues);

  connect(_curvelist_widget, &CurveListPanel::deleteCurves, this,
          &MainWindow::onDeleteMultipleCurves);

  connect(_curvelist_widget, &CurveListPanel::createMathPlot, this,
          &MainWindow::onAddCustomPlot);

  connect(_curvelist_widget, &CurveListPanel::editMathPlot, this,
          &MainWindow::onEditCustomPlot);

  connect(_curvelist_widget, &CurveListPanel::refreshMathPlot, this,
          &MainWindow::onRefreshCustomPlot);

  connect(ui->timeSlider, &RealSlider::realValueChanged, this,
          &MainWindow::onTimeSlider_valueChanged);

  connect(ui->playbackRate, &QDoubleSpinBox::editingFinished, this,
          [this]() { ui->playbackRate->clearFocus(); });

  connect(ui->playbackStep, &QDoubleSpinBox::editingFinished, this,
          [this]() { ui->playbackStep->clearFocus(); });

  _main_tabbed_widget =
      new TabbedPlotWidget(
          "Main Window", this, _mapped_plot_data,
          [this](const PlotDataXY* series) { return _session.snapshotFor(series); }, this);

  ui->plottingLayout->insertWidget(0, _main_tabbed_widget, 1);
  ui->leftLayout->addWidget(_curvelist_widget, 1);

  ui->mainSplitter->setCollapsible(0, true);
  ui->mainSplitter->setStretchFactor(0, 2);
  ui->mainSplitter->setStretchFactor(1, 6);

  ui->layoutTimescale->removeWidget(ui->widgetButtons);
  _main_tabbed_widget->tabWidget()->setCornerWidget(ui->widgetButtons);

  connect(ui->mainSplitter, &QSplitter::splitterMoved, this, &MainWindow::on_splitterMoved);

  initializeActions();
  initializeEmbeddedToolboxes();

  _undo_timer.start();

  // save initial state
  onUndoableChange();

  _replot_timer = new QTimer(this);
  connect(_replot_timer, &QTimer::timeout, this,
          [this]() { updateDataAndReplot(false); });

  _publish_timer = new QTimer(this);
  _publish_timer->setInterval(20);
  connect(_publish_timer, &QTimer::timeout, this, &MainWindow::onPlaybackLoop);

  if (!_embedded_mode)
  {
    restoreGeometry(settings.value("MainWindow.geometry").toByteArray());
    restoreState(settings.value("MainWindow.state").toByteArray());
  }

  // qDebug() << "restoreGeometry";

  bool activate_grid = settings.contains("MainWindow.activateGrid")
                           ? settings.value("MainWindow.activateGrid").toBool()
                           : _embedded_mode;
  ui->pushButtonActivateGrid->setChecked(activate_grid);

  bool zoom_link_active = settings.value("MainWindow.buttonLink", true).toBool();
  ui->pushButtonLink->setChecked(zoom_link_active);

  bool ration_active = settings.value("MainWindow.buttonRatio", true).toBool();
  ui->pushButtonRatio->setChecked(ration_active);

  bool datetime_display = settings.value("MainWindow.dateTimeDisplay", false).toBool();
  ui->pushButtonUseDateTime->setChecked(datetime_display);

  bool remove_time_offset = settings.value("MainWindow.removeTimeOffset", true).toBool();
  ui->pushButtonRemoveTimeOffset->setChecked(remove_time_offset);

  //  ui->widgetOptions->setVisible(ui->pushButtonOptions->isChecked());

  if (settings.value("MainWindow.hiddenFileFrame", false).toBool())
  {
    ui->buttonHideFileFrame->setText("+");
    ui->frameFile->setHidden(true);
  }
  //----------------------------------------------------------
  QIcon trackerIconA, trackerIconB, trackerIconC;

  trackerIconA.addFile(QStringLiteral(":/style_light/line_tracker.png"), QSize(36, 36));
  trackerIconB.addFile(QStringLiteral(":/style_light/line_tracker_1.png"), QSize(36, 36));
  trackerIconC.addFile(QStringLiteral(":/style_light/line_tracker_a.png"), QSize(36, 36));

  _tracker_button_icons[CurveTracker::LINE_ONLY] = trackerIconA;
  _tracker_button_icons[CurveTracker::VALUE] = trackerIconB;
  _tracker_button_icons[CurveTracker::VALUE_NAME] = trackerIconC;

  int tracker_setting =
      settings.value("MainWindow.timeTrackerSetting", (int)CurveTracker::VALUE).toInt();
  _tracker_param = static_cast<CurveTracker::Parameter>(tracker_setting);

  ui->pushButtonTimeTracker->setIcon(_tracker_button_icons[_tracker_param]);

  forEachWidget([&](PlotWidget* plot) { plot->configureTracker(_tracker_param); });

  auto editor_layout = new QVBoxLayout();
  editor_layout->setMargin(0);
  ui->formulaPage->setLayout(editor_layout);
  _function_editor =
      new FunctionEditorWidget(_mapped_plot_data, _transform_functions, this);
  editor_layout->addWidget(_function_editor);

  connect(_function_editor, &FunctionEditorWidget::closed, this,
          [this]() { ui->widgetStack->setCurrentIndex(0); });

  connect(_function_editor, &FunctionEditorWidget::accept, this,
          &MainWindow::onCustomPlotCreated);

  QString theme = settings.value("Preferences::theme", "light").toString();
  if (theme != "dark")
  {
    theme = "light";
  }
  loadStyleSheet(tr(":/resources/stylesheet_%1.qss").arg(theme));
}

MainWindow::~MainWindow()
{
  delete ui;
}

void MainWindow::clearExternalData()
{
  deleteAllData();
  forEachWidget([](PlotWidget* plot) {
    plot->updateCurves(nullptr, true);
    plot->replot();
  });
  updateTimeSlider();
}

void MainWindow::appendExternalData(PlotDataMapRef&& new_data)
{
  const auto previous_curve_count = _mapped_plot_data.getAllNames().size();
  const auto refresh = _session.importAndRefresh(new_data, false, _tracker_time);
  for (const auto& added_curve : refresh.added_curves)
  {
    _curvelist_widget->addCurve(added_curve);
  }
  if (refresh.curves_updated)
  {
    _curvelist_widget->refreshColumns();
  }
  if (_mapped_plot_data.getAllNames().size() != previous_curve_count)
  {
    _curvelist_widget->updateFilter();
  }
  updateDataAndReplot(refresh.updated_curves, true);

  if (qEnvironmentVariableIsSet("CABANA_PJ_DEBUG"))
  {
    const char* sample_curves[] = {
      "/livePose/inputsOK",
      "/accelerometer/acceleration/v/0",
      "/gyroscope/gyroUncalibrated/v/0",
      "/accelerometer/__valid",
      "/gyroscope/__logMonoTime",
      "/accelerometer/timestamp",
    };
    for (const char* curve_name : sample_curves)
    {
      auto it = _mapped_plot_data.numeric.find(curve_name);
      qInfo() << "Cabana PJ curve" << curve_name << "samples"
              << (it != _mapped_plot_data.numeric.end() ? static_cast<qlonglong>(it->second.size()) : -1);
    }
    bool logged_plot_state = false;
    forEachWidget([&](PlotWidget* plot) {
      if (logged_plot_state || plot->curveList().empty())
      {
        return;
      }
      const auto& curve_info = plot->curveList().front();
      auto it = _mapped_plot_data.numeric.find(curve_info.src_name);
      qInfo() << "Cabana PJ first plot curve" << QString::fromStdString(curve_info.src_name)
              << "samples"
              << (it != _mapped_plot_data.numeric.end() ? static_cast<qlonglong>(it->second.size()) : -1);
      logged_plot_state = true;
    });
  }
}

void MainWindow::setExternalTrackerTime(double absolute_time)
{
  _tracker_time = absolute_time;
  auto prev = ui->timeSlider->blockSignals(true);
  ui->timeSlider->setRealValue(absolute_time);
  ui->timeSlider->blockSignals(prev);
  onTrackerTimeUpdated(absolute_time, true);
}

void MainWindow::setExternalPlaybackPaused(bool paused)
{
  ui->pushButtonPlay->setChecked(!paused);
}

void MainWindow::onUndoableChange()
{
  if (_disable_undo_logging)
    return;

  int elapsed_ms = _undo_timer.restart();

  // overwrite the previous
  if (elapsed_ms < 100)
  {
    if (_undo_states.empty() == false)
      _undo_states.pop_back();
  }

  while (_undo_states.size() >= 100)
    _undo_states.pop_front();
  _undo_states.push_back(saveUiStateModel());
  _redo_states.clear();
  // qDebug() << "undo " << _undo_states.size();
}

void MainWindow::onRedoInvoked()
{
  _disable_undo_logging = true;
  if (_redo_states.size() > 0)
  {
    LayoutModel state_layout = _redo_states.back();
    while (_undo_states.size() >= 100)
      _undo_states.pop_front();
    _undo_states.push_back(state_layout);
    _redo_states.pop_back();

    loadUiStateModel(state_layout);
  }
  // qDebug() << "undo " << _undo_states.size();
  _disable_undo_logging = false;
}

void MainWindow::onUndoInvoked()
{
  _disable_undo_logging = true;
  if (_undo_states.size() > 1)
  {
    LayoutModel state_layout = _undo_states.back();
    while (_redo_states.size() >= 100)
      _redo_states.pop_front();
    _redo_states.push_back(state_layout);
    _undo_states.pop_back();
    state_layout = _undo_states.back();

    loadUiStateModel(state_layout);
  }
  // qDebug() << "undo " << _undo_states.size();
  _disable_undo_logging = false;
}

void MainWindow::onUpdateLeftTableValues()
{
  _curvelist_widget->update2ndColumnValues(_tracker_time);
}

void MainWindow::onTrackerMovedFromWidget(QPointF relative_pos)
{
  _tracker_time = relative_pos.x() + _time_offset.get();

  auto prev = ui->timeSlider->blockSignals(true);
  ui->timeSlider->setRealValue(_tracker_time);
  ui->timeSlider->blockSignals(prev);

  onTrackerTimeUpdated(_tracker_time, true);
}

void MainWindow::onTimeSlider_valueChanged(double abs_time)
{
  _tracker_time = abs_time;
  onTrackerTimeUpdated(_tracker_time, true);
}

void MainWindow::onTrackerTimeUpdated(double absolute_time, bool do_replot)
{
  _tracker_delay.triggerSignal(100);

  updateReactivePlots();

  forEachWidget([&](PlotWidget* plot) {
    plot->setTrackerPosition(_tracker_time);
    if (do_replot)
    {
      plot->replot();
    }
  });
}

void MainWindow::initializeActions()
{
  _undo_shortcut.setContext(Qt::ApplicationShortcut);
  _redo_shortcut.setContext(Qt::ApplicationShortcut);
  _fullscreen_shortcut.setContext(Qt::ApplicationShortcut);

  connect(&_undo_shortcut, &QShortcut::activated, this, &MainWindow::onUndoInvoked);
  connect(&_redo_shortcut, &QShortcut::activated, this, &MainWindow::onRedoInvoked);
  connect(&_playback_shotcut, &QShortcut::activated, ui->pushButtonPlay,
          &QPushButton::toggle);
  connect(&_fullscreen_shortcut, &QShortcut::activated, this,
          &MainWindow::onActionFullscreenTriggered);

  //---------------------------------------------

  QSettings settings;
  _recent_layout_files->addAction(ui->actionClearRecentLayout);
  _recent_layout_files->addSeparator();
  updateRecentLayoutMenu(
      settings.value("MainWindow.recentlyLoadedLayout").toStringList());
}

void MainWindow::initializeEmbeddedToolboxes()
{
  addToolbox(std::make_shared<ToolboxLuaEditor>());
}

void MainWindow::addToolbox(ToolboxPluginPtr toolbox)
{
  const QString plugin_name = toolbox->name();
  if (_toolboxes.find(plugin_name) != _toolboxes.end())
  {
    return;
  }

  toolbox->init(_mapped_plot_data, _transform_functions);
  _toolboxes.insert(std::make_pair(plugin_name, toolbox));

  auto action = _tools_menu->addAction(toolbox->name());
  int new_index = ui->widgetStack->count();
  auto provided = toolbox->providedWidget();
  auto widget = provided.first;
  ui->widgetStack->addWidget(widget);

  connect(action, &QAction::triggered, toolbox.get(), &ToolboxPlugin::onShowWidget);
  connect(action, &QAction::triggered, this,
          [=]() { ui->widgetStack->setCurrentIndex(new_index); });
  connect(toolbox.get(), &ToolboxPlugin::closed, this,
          [=]() { ui->widgetStack->setCurrentIndex(0); });
  connect(toolbox.get(), &ToolboxPlugin::plotCreated, this, [=](std::string name) {
    _curvelist_widget->addCustom(QString::fromStdString(name));
    _curvelist_widget->updateAppearance();
    _curvelist_widget->clearSelections();
  });
}

void MainWindow::on_splitterMoved(int, int)
{
  QList<int> sizes = ui->mainSplitter->sizes();
  int max_left_size = _curvelist_widget->maximumWidth();
  int totalWidth = sizes[0] + sizes[1];

  // this is needed only once to restore the old size
  static bool first = true;
  if (sizes[0] != 0 && first)
  {
    first = false;
    QSettings settings;
    int splitter_width = settings.value("MainWindow.splitterWidth", 200).toInt();
    auto initial_sizes = ui->mainSplitter->sizes();
    int tot_splitter_width = initial_sizes[0] + initial_sizes[1];
    initial_sizes[0] = splitter_width;
    initial_sizes[1] = tot_splitter_width - splitter_width;
    ui->mainSplitter->setSizes(initial_sizes);
    return;
  }

  if (sizes[0] > max_left_size)
  {
    sizes[0] = max_left_size;
    sizes[1] = totalWidth - max_left_size;
    ui->mainSplitter->setSizes(sizes);
  }
}

void MainWindow::resizeEvent(QResizeEvent*)
{
  on_splitterMoved(0, 0);
}

void MainWindow::onPlotAdded(PlotWidget* plot)
{
  connect(plot, &PlotWidget::undoableChange, this, &MainWindow::onUndoableChange);

  connect(plot, &PlotWidget::trackerMoved, this, &MainWindow::onTrackerMovedFromWidget);

  connect(plot, &PlotWidget::curveListChanged, this, [this]() {
    updateTimeOffset();
    updateTimeSlider();
  });

  connect(&_time_offset, &MonitoredValue::valueChanged, plot, &PlotWidget::on_changeTimeOffset);

  connect(ui->pushButtonUseDateTime, &QPushButton::toggled, plot,
          &PlotWidget::on_changeDateTimeScale);

  connect(plot, &PlotWidget::curvesDropped, _curvelist_widget,
          &CurveListPanel::clearSelections);

  connect(plot, &PlotWidget::legendSizeChanged, this, [=](int point_size) {
    auto visitor = [=](PlotWidget* p) {
      if (plot != p)
        p->setLegendSize(point_size);
    };
    this->forEachWidget(visitor);
  });

  connect(plot, &PlotWidget::rectChanged, this, &MainWindow::onPlotZoomChanged);

  plot->on_changeTimeOffset(_time_offset.get());
  plot->on_changeDateTimeScale(ui->pushButtonUseDateTime->isChecked());
  plot->activateGrid(ui->pushButtonActivateGrid->isChecked());
  plot->enableTracker(true);
  plot->setKeepRatioXY(ui->pushButtonRatio->isChecked());
  plot->configureTracker(_tracker_param);
}

void MainWindow::onPlotZoomChanged(PlotWidget* modified_plot, QRectF new_range)
{
  if (ui->pushButtonLink->isChecked())
  {
    auto visitor = [=](PlotWidget* plot) {
      if (plot != modified_plot && !plot->isEmpty() && !plot->isXYPlot() &&
          plot->isZoomLinkEnabled())
      {
        QRectF bound_act = plot->currentBoundingRect();
        bound_act.setLeft(new_range.left());
        bound_act.setRight(new_range.right());
        plot->setZoomRectangle(bound_act, false);
        plot->on_zoomOutVertical_triggered(false);
        plot->replot();
      }
    };
    this->forEachWidget(visitor);
  }

  onUndoableChange();
}

void MainWindow::onPlotTabAdded(PlotDocker* docker)
{
  connect(docker, &PlotDocker::plotWidgetAdded, this, &MainWindow::onPlotAdded);
  docker->on_stylesheetChanged(_current_theme);

  // TODO  connect(matrix, &PlotMatrix::undoableChange, this,
  // &MainWindow::onUndoableChange);
}

QDomDocument MainWindow::saveUiStateDom() const
{
  QDomDocument doc;
  QDomProcessingInstruction instr = doc.createProcessingInstruction("xml", "version='1.0'"
                                                                           " encoding='"
                                                                           "UTF-8'");

  doc.appendChild(instr);

  QDomElement root = doc.createElement("root");

  for (auto& it : TabbedPlotWidget::instances())
  {
    QDomElement tabbed_area = it.second->xmlSaveState(doc);
    root.appendChild(tabbed_area);
  }

  doc.appendChild(root);

  QDomElement relative_time = doc.createElement("use_relative_time_offset");
  relative_time.setAttribute("enabled", ui->pushButtonRemoveTimeOffset->isChecked());
  root.appendChild(relative_time);

  return doc;
}

LayoutModel MainWindow::saveUiStateModel() const
{
  const QDomDocument legacy_doc = saveUiStateDom();

  LayoutModel layout;
  QString error;
  if (!cabana::pj_layout::ParseLayoutXml(legacy_doc.toString(), &layout, &error))
  {
    qWarning().noquote() << QString("Failed to convert UI state through pj_layout: %1").arg(error);
  }
  return layout;
}

QDomDocument MainWindow::xmlSaveState() const
{
  return cabana::pj_layout::ToXmlDocument(saveUiStateModel());
}

void MainWindow::checkAllCurvesFromLayout(const QDomElement& root)
{
  std::set<std::string> missing_curves;

  for (const auto& curve_name_qt : readAllCurvesFromXML(root))
  {
    const auto curve_name = curve_name_qt.toStdString();
    if (curve_name.empty())
    {
      continue;
    }

    const bool has_numeric = (_mapped_plot_data.numeric.count(curve_name) != 0);
    const bool has_string = (_mapped_plot_data.strings.count(curve_name) != 0);
    const bool has_scatter = (_mapped_plot_data.scatter_xy.count(curve_name) != 0);
    if (!has_numeric && !has_string && !has_scatter)
    {
      missing_curves.insert(curve_name);
    }
  }

  if (missing_curves.empty())
  {
    return;
  }

  auto create_placeholders = [&]() {
    for (const auto& name : missing_curves)
    {
      _mapped_plot_data.addNumeric(name);
      _curvelist_widget->addCurve(name);
    }
    _curvelist_widget->refreshColumns();
  };

  if (_embedded_mode)
  {
    create_placeholders();
    return;
  }

  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Warning");
  msgBox.setText(tr("One or more timeseries in the layout haven't been loaded yet\n"
                    "What do you want to do?"));

  msgBox.addButton(tr("Remove curves from plots"), QMessageBox::RejectRole);
  QPushButton* buttonPlaceholder =
      msgBox.addButton(tr("Create empty placeholders"), QMessageBox::YesRole);
  msgBox.setDefaultButton(buttonPlaceholder);
  msgBox.exec();
  if (msgBox.clickedButton() == buttonPlaceholder)
  {
    create_placeholders();
  }
}

bool MainWindow::loadUiStateDom(const QDomDocument& state_document)
{
  QDomElement root = state_document.namedItem("root").toElement();
  if (root.isNull())
  {
    qWarning() << "No <root> element found at the top-level of the XML file!";
    return false;
  }

  size_t num_floating = 0;
  std::map<QString, QDomElement> tabbed_widgets_with_name;

  for (QDomElement tw = root.firstChildElement("tabbed_widget"); tw.isNull() == false;
       tw = tw.nextSiblingElement("tabbed_widget"))
  {
    if (tw.attribute("parent") != ("main_window"))
    {
      num_floating++;
    }
    tabbed_widgets_with_name[tw.attribute("name")] = tw;
  }

  // add if missing
  for (const auto& it : tabbed_widgets_with_name)
  {
    if (TabbedPlotWidget::instance(it.first) == nullptr)
    {
      // TODO createTabbedDialog(it.first, nullptr);
    }
  }

  // remove those which don't share list of names
  for (const auto& it : TabbedPlotWidget::instances())
  {
    if (tabbed_widgets_with_name.count(it.first) == 0)
    {
      it.second->deleteLater();
    }
  }

  //-----------------------------------------------------
  checkAllCurvesFromLayout(root);
  //-----------------------------------------------------

  for (QDomElement tw = root.firstChildElement("tabbed_widget"); tw.isNull() == false;
       tw = tw.nextSiblingElement("tabbed_widget"))
  {
    TabbedPlotWidget* tabwidget = TabbedPlotWidget::instance(tw.attribute("name"));
    tabwidget->xmlLoadState(tw);
  }

  QDomElement relative_time = root.firstChildElement("use_relative_time_offset");
  if (!relative_time.isNull())
  {
    bool remove_offset = (relative_time.attribute("enabled") == QString("1"));
    ui->pushButtonRemoveTimeOffset->setChecked(remove_offset);
  }
  return true;
}

bool MainWindow::loadUiStateModel(const LayoutModel& layout)
{
  return loadUiStateDom(cabana::pj_layout::ToXmlDocument(layout));
}

bool MainWindow::xmlLoadState(QDomDocument state_document)
{
  LayoutModel layout;
  QString error;
  if (!cabana::pj_layout::ParseLayoutXml(state_document.toString(), &layout, &error))
  {
    qWarning().noquote() << QString("Failed to parse layout state through pj_layout: %1").arg(error);
    return false;
  }
  return loadUiStateModel(layout);
}

void MainWindow::onDeleteMultipleCurves(const std::vector<std::string>& curve_names)
{
  const auto deleted_curves = _session.deleteCurvesWithDependencies(curve_names);
  for (const auto& curve_name : deleted_curves)
  {
    forEachWidget([&](PlotWidget* plot) { plot->onDataSourceRemoved(curve_name); });
    _curvelist_widget->removeCurve(curve_name);
  }
  updateTimeOffset();
  forEachWidget([](PlotWidget* plot) { plot->replot(); });
}

void MainWindow::updateRecentLayoutMenu(QStringList new_filenames)
{
  QMenu* menu = _recent_layout_files;

  QAction* separator = nullptr;
  QStringList prev_filenames;
  for (QAction* action : menu->actions())
  {
    if (action->isSeparator())
    {
      separator = action;
      break;
    }
    if (new_filenames.contains(action->text()) == false)
    {
      prev_filenames.push_back(action->text());
    }
    menu->removeAction(action);
  }

  new_filenames.append(prev_filenames);
  while (new_filenames.size() > 10)
  {
    new_filenames.removeLast();
  }

  for (const auto& filename : new_filenames)
  {
    QAction* action = new QAction(filename, nullptr);
    connect(action, &QAction::triggered, this, [this, filename] {
      if (this->loadLayoutFromFile(filename))
      {
        updateRecentLayoutMenu({ filename });
      }
    });
    menu->insertAction(separator, action);
  }

  QSettings settings;
  settings.setValue("MainWindow.recentlyLoadedLayout", new_filenames);
  menu->setEnabled(new_filenames.size() > 0);
}

void MainWindow::deleteAllData()
{
  forEachWidget([](PlotWidget* plot) { plot->removeAllCurves(); });

  _session.clearAll();
  _curvelist_widget->clear();
  _undo_states.clear();
  _redo_states.clear();
}

void MainWindow::importPlotDataMap(PlotDataMapRef& new_data, bool remove_old)
{
  const auto import_result = _session.importPlotDataMap(new_data, remove_old);

  for (const auto& added_curve : import_result.added_curves)
  {
    _curvelist_widget->addCurve(added_curve);
  }

  if (import_result.curves_updated)
  {
    _curvelist_widget->refreshColumns();
  }
}

void MainWindow::applyTheme(const QString& theme)
{
  _current_theme = theme;
  on_stylesheetChanged(theme);
  _curvelist_widget->on_stylesheetChanged(theme);
  _main_tabbed_widget->on_stylesheetChanged(theme);
  if (_function_editor)
  {
    _function_editor->on_stylesheetChanged(theme);
  }
  forEachWidget([&](PlotWidget*, PlotDocker* docker, int) {
    docker->on_stylesheetChanged(theme);
  });
}

void MainWindow::loadStyleSheet(QString file_path)
{
  QFile styleFile(file_path);
  styleFile.open(QFile::ReadOnly);
  try
  {
    QString theme = SetApplicationStyleSheet(styleFile.readAll());

    forEachWidget([&](PlotWidget* plot) { plot->replot(); });

    _curvelist_widget->updateAppearance();
    applyTheme(theme);
  }
  catch (std::runtime_error& err)
  {
    QMessageBox::warning(this, tr("Error loading StyleSheet"), tr(err.what()));
  }
}

void MainWindow::updateDerivedSeries()
{
  Q_UNUSED(_transform_functions);
}

void MainWindow::updateReactivePlots()
{
  const auto reactive_update = _session.updateReactiveTransforms(_tracker_time);
  bool curve_added = false;
  for (const auto& name : reactive_update.added_curves)
  {
    curve_added |= _curvelist_widget->addCurve(name);
  }
  if (curve_added)
  {
    _curvelist_widget->refreshColumns();
  }

  forEachWidget([&](PlotWidget* plot) {
    if (plot->updateCurves(&reactive_update.updated_curves, false))
    {
      plot->replot();
    }
  });
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
  if (event->mimeData()->hasUrls())
  {
    event->acceptProposedAction();
  }
}

void MainWindow::dropEvent(QDropEvent* event)
{
  QStringList layout_files;
  QStringList unsupported_files;
  const auto urls = event->mimeData()->urls();

  for (const auto& url : urls)
  {
    const QString filename = QDir::toNativeSeparators(url.toLocalFile());
    if (QFileInfo(filename).suffix().compare("xml", Qt::CaseInsensitive) == 0)
    {
      layout_files << filename;
    }
    else
    {
      unsupported_files << filename;
    }
  }

  if (!unsupported_files.isEmpty())
  {
    QMessageBox::information(
        this, tr("Replay-only mode"),
        tr("Cabana-hosted PlotJuggler no longer opens standalone data files.\n"
           "Use Cabana replay input and optional layout XML files instead."));
  }

  for (const auto& filename : layout_files)
  {
    loadLayoutFromFile(filename);
  }
}

void MainWindow::on_stylesheetChanged(QString theme)
{
  ui->buttonRecentLayout->setIcon(LoadSvg(":/resources/svg/right-arrow.svg", theme));

  ui->pushButtonZoomOut->setIcon(LoadSvg(":/resources/svg/zoom_max.svg", theme));
  ui->playbackLoop->setIcon(LoadSvg(":/resources/svg/loop.svg", theme));
  ui->pushButtonPlay->setIcon(LoadSvg(":/resources/svg/play_arrow.svg", theme));
  ui->pushButtonUseDateTime->setIcon(LoadSvg(":/resources/svg/datetime.svg", theme));
  ui->pushButtonActivateGrid->setIcon(LoadSvg(":/resources/svg/grid.svg", theme));
  ui->pushButtonRatio->setIcon(LoadSvg(":/resources/svg/ratio.svg", theme));

  ui->pushButtonLoadLayout->setIcon(LoadSvg(":/resources/svg/import.svg", theme));
  ui->pushButtonSaveLayout->setIcon(LoadSvg(":/resources/svg/export.svg", theme));

  ui->pushButtonLink->setIcon(LoadSvg(":/resources/svg/link.svg", theme));
  ui->pushButtonRemoveTimeOffset->setIcon(LoadSvg(":/resources/svg/t0.svg", theme));
  ui->pushButtonLegend->setIcon(LoadSvg(":/resources/svg/legend.svg", theme));
}

void MainWindow::loadPluginState(const LayoutModel& layout)
{
  for (const auto& plugin_state : layout.plugins)
  {
    const QString plugin_name = plugin_state.id;

    if (plugin_name.isEmpty())
    {
      QMessageBox::warning(this, tr("Error loading Plugin State from Layout"),
                           tr("The method xmlSaveState() must return a node like this "
                              "<plugin ID=\"PluginName\" "));
      continue;
    }

    if (_toolboxes.find(plugin_name) != _toolboxes.end())
    {
      QDomDocument document;
      QDomElement plugin_elem = ToPluginDomElement(plugin_state, &document);
      document.appendChild(plugin_elem);
      _toolboxes[plugin_name]->xmlLoadState(plugin_elem);
    }
  }
}

std::vector<PluginState> MainWindow::savePluginStateModel() const
{
  std::vector<PluginState> plugin_states;
  plugin_states.reserve(_toolboxes.size());

  auto AddPlugins = [&](auto& toolbox_map) {
    for (auto& [name, plugin] : toolbox_map)
    {
      QDomDocument doc;
      QDomElement elem = plugin->xmlSaveState(doc);
      plugin_states.push_back(ToPluginStateModel(elem));
    }
  };

  AddPlugins(_toolboxes);
  return plugin_states;
}

std::tuple<double, double, int> MainWindow::calculateVisibleRangeX()
{
  // find min max time
  double min_time = std::numeric_limits<double>::max();
  double max_time = std::numeric_limits<double>::lowest();
  int max_steps = 0;

  forEachWidget([&](const PlotWidget* widget) {
    for (auto& it : widget->curveList())
    {
      const auto& curve_name = it.src_name;

      auto plot_it = _mapped_plot_data.numeric.find(curve_name);
      if (plot_it == _mapped_plot_data.numeric.end())
      {
        continue;  // FIXME?
      }
      const auto& data = plot_it->second;
      if (data.size() >= 1)
      {
        const double t0 = data.front().x;
        const double t1 = data.back().x;
        min_time = std::min(min_time, t0);
        max_time = std::max(max_time, t1);
        max_steps = std::max(max_steps, (int)data.size());
      }
    }
  });

  // needed if all the plots are empty
  if (max_steps == 0 || max_time < min_time)
  {
    for (const auto& it : _mapped_plot_data.numeric)
    {
      const PlotData& data = it.second;
      if (data.size() >= 1)
      {
        const double t0 = data.front().x;
        const double t1 = data.back().x;
        min_time = std::min(min_time, t0);
        max_time = std::max(max_time, t1);
        max_steps = std::max(max_steps, (int)data.size());
      }
    }
  }

  // last opportunity. Everything else failed
  if (max_steps == 0 || max_time < min_time)
  {
    min_time = 0.0;
    max_time = 1.0;
    max_steps = 1;
  }
  return std::tuple<double, double, int>(min_time, max_time, max_steps);
}

bool MainWindow::loadLayoutFromFile(QString filename)
{
  LayoutModel layout;
  QString error;
  if (!cabana::pj_layout::LoadLayoutFile(filename, &layout, &error))
  {
    QMessageBox::warning(
        this, tr("Layout"), tr("Cannot load layout %1:\n%2.").arg(filename).arg(error));
    return false;
  }

  return loadLayoutModel(layout);
}

bool MainWindow::loadLayoutModel(const LayoutModel& layout)
{
  const QDomDocument domDocument = cabana::pj_layout::ToXmlDocument(layout);
  QSettings settings;

  //-------------------------------------------------
  // refresh plugins
  QDomElement root = domDocument.namedItem("root").toElement();

  if (_embedded_mode)
  {
    std::set<std::string> added_placeholders;
    for (const auto& curve_name_qt : readAllCurvesFromXML(root))
    {
      const auto curve_name = curve_name_qt.toStdString();
      if (curve_name.empty())
      {
        continue;
      }
      if (_session.ensureNumericPlaceholder(curve_name))
      {
        added_placeholders.insert(curve_name);
      }
    }
    for (const auto& curve_name : added_placeholders)
    {
      _curvelist_widget->addCurve(curve_name);
    }
    if (!added_placeholders.empty())
    {
      _curvelist_widget->refreshColumns();
    }
  }

  loadPluginState(layout);
  //-------------------------------------------------
  if (layout.custom_math_present)
  {
    std::vector<SnippetData> snippets;
    snippets.reserve(layout.custom_math_snippets.size());
    for (const auto& snippet_model : layout.custom_math_snippets)
    {
      snippets.push_back(ToSnippetData(snippet_model));
    }
    // A custom plot may depend on other custom plots.
    // Reorder them to respect the mutual depencency.
    auto DependOn = [](const SnippetData& a, const SnippetData& b) {
      if (b.linked_source == a.alias_name)
      {
        return true;
      }
      for (const auto& source : b.additional_sources)
      {
        if (source == a.alias_name)
        {
          return true;
        }
      }
      return false;
    };
    std::sort(snippets.begin(), snippets.end(), DependOn);

    if (_embedded_mode)
    {
      auto ensure_placeholder = [this](const QString& source_name) {
        if (source_name.isEmpty())
        {
          return;
        }
        _session.ensureNumericPlaceholder(source_name.toStdString());
      };

      for (const auto& snippet : snippets)
      {
        ensure_placeholder(snippet.linked_source);
        for (const auto& source : snippet.additional_sources)
        {
          ensure_placeholder(source);
        }
      }
    }

    for (const auto& snippet : snippets)
    {
      try
      {
        CustomPlotPtr new_custom_plot = std::make_shared<LuaCustomFunction>(snippet);
        const auto& alias_name = new_custom_plot->aliasName();
        QString error;
        const bool inserted = _session.upsertCustomPlot(new_custom_plot, &error);
        if (!error.isEmpty())
        {
          throw std::runtime_error(error.toStdString());
        }
        if (inserted)
        {
          _curvelist_widget->addCustom(alias_name);
        }
      }
      catch (std::runtime_error& err)
      {
        const QString message =
            tr("Failed to load customMathEquation [%1] \n\n %2\n")
                .arg(snippet.alias_name)
                .arg(err.what());
        if (_embedded_mode)
        {
          qWarning().noquote() << message;
        }
        else
        {
          QMessageBox::warning(this, tr("Exception"), message);
        }
      }
    }
    _curvelist_widget->refreshColumns();
  }

  QByteArray snippets_saved_xml =
      settings.value("AddCustomPlotDialog.savedXML", QByteArray()).toByteArray();

  if (layout.snippets_present)
  {
    auto snippets_previous = GetSnippetsFromXML(snippets_saved_xml);
    SnippetsMap snippets_layout;
    for (const auto& snippet_model : layout.snippets)
    {
      const auto snippet = ToSnippetData(snippet_model);
      SnippetsMap::value_type value(snippet.alias_name, snippet);
      snippets_layout.insert(value);
    }

    bool snippets_are_different = false;
    for (const auto& snippet_it : snippets_layout)
    {
      auto prev_it = snippets_previous.find(snippet_it.first);

      if (prev_it == snippets_previous.end() ||
          prev_it->second.function != snippet_it.second.function ||
          prev_it->second.global_vars != snippet_it.second.global_vars)
      {
        snippets_are_different = true;
        break;
      }
    }

    if (snippets_are_different)
    {
      QMessageBox msgBox(this);
      msgBox.setWindowTitle("Overwrite custom transforms?");
      msgBox.setText("Your layout file contains a set of custom transforms different "
                     "from "
                     "the last one you used.\nWant to load these transformations?");
      msgBox.addButton(QMessageBox::No);
      msgBox.addButton(QMessageBox::Yes);
      msgBox.setDefaultButton(QMessageBox::Yes);

      if (msgBox.exec() == QMessageBox::Yes)
      {
        for (const auto& snippet_it : snippets_layout)
        {
          snippets_previous[snippet_it.first] = snippet_it.second;
        }
        QDomDocument doc;
        auto snippets_root_element = ExportSnippets(snippets_previous, doc);
        doc.appendChild(snippets_root_element);
        settings.setValue("AddCustomPlotDialog.savedXML", doc.toByteArray(2));
      }
    }
  }

  ///--------------------------------------------------

  loadUiStateModel(layout);

  linkedZoomOut();

  _undo_states.clear();
  _undo_states.push_back(saveUiStateModel());
  return true;
}

bool MainWindow::loadLayoutDocument(const QDomDocument& domDocument)
{
  LayoutModel layout;
  QString error;
  if (!cabana::pj_layout::ParseLayoutXml(domDocument.toString(), &layout, &error))
  {
    qWarning().noquote() << QString("Failed to parse layout document through pj_layout: %1").arg(error);
    return false;
  }
  return loadLayoutModel(layout);
}

void MainWindow::linkedZoomOut()
{
  if (ui->pushButtonLink->isChecked())
  {
    for (const auto& it : TabbedPlotWidget::instances())
    {
      auto tabs = it.second->tabWidget();
      for (int t = 0; t < tabs->count(); t++)
      {
        if (PlotDocker* matrix = dynamic_cast<PlotDocker*>(tabs->widget(t)))
        {
          bool first = true;
          Range range;
          // find the ideal zoom
          for (int index = 0; index < matrix->plotCount(); index++)
          {
            PlotWidget* plot = matrix->plotAt(index);
            if (plot->isEmpty())
            {
              continue;
            }

            auto rect = plot->maxZoomRect();
            if (first)
            {
              range.min = rect.left();
              range.max = rect.right();
              first = false;
            }
            else
            {
              range.min = std::min(rect.left(), range.min);
              range.max = std::max(rect.right(), range.max);
            }
          }

          for (int index = 0; index < matrix->plotCount() && !first; index++)
          {
            PlotWidget* plot = matrix->plotAt(index);
            if (plot->isEmpty())
            {
              continue;
            }
            QRectF bound_act = plot->maxZoomRect();
            bound_act.setLeft(range.min);
            bound_act.setRight(range.max);
            plot->setZoomRectangle(bound_act, false);
            plot->replot();
          }
        }
      }
    }
  }
  else
  {
    this->forEachWidget([](PlotWidget* plot) { plot->zoomOut(false); });
  }
}

void MainWindow::on_tabbedAreaDestroyed(QObject* object)
{
  this->setFocus();
}

void MainWindow::forEachWidget(
    std::function<void(PlotWidget*, PlotDocker*, int)> operation)
{
  auto func = [&](QTabWidget* tabs) {
    for (int t = 0; t < tabs->count(); t++)
    {
      PlotDocker* matrix = dynamic_cast<PlotDocker*>(tabs->widget(t));
      if (!matrix)
      {
        continue;
      }

      for (int index = 0; index < matrix->plotCount(); index++)
      {
        PlotWidget* plot = matrix->plotAt(index);
        operation(plot, matrix, index);
      }
    }
  };

  for (const auto& it : TabbedPlotWidget::instances())
  {
    func(it.second->tabWidget());
  }
}

void MainWindow::forEachWidget(std::function<void(PlotWidget*)> op)
{
  forEachWidget([&](PlotWidget* plot, PlotDocker*, int) { op(plot); });
}

void MainWindow::updateTimeSlider()
{
  auto range = calculateVisibleRangeX();

  ui->timeSlider->setLimits(std::get<0>(range), std::get<1>(range), std::get<2>(range));

  _tracker_time = std::max(_tracker_time, ui->timeSlider->getMinimum());
  _tracker_time = std::min(_tracker_time, ui->timeSlider->getMaximum());
}

void MainWindow::updateTimeOffset()
{
  auto range = calculateVisibleRangeX();
  double min_time = std::get<0>(range);

  const bool remove_offset = ui->pushButtonRemoveTimeOffset->isChecked();
  if (remove_offset && min_time != std::numeric_limits<double>::max())
  {
    _time_offset.set(min_time);
  }
  else
  {
    _time_offset.set(0.0);
  }

  updatedDisplayTime();
}

void MainWindow::updateDataAndReplot(bool replot_hidden_tabs)
{
  Q_UNUSED(replot_hidden_tabs);
  _replot_timer->stop();

  // Update the reactive plots
  updateReactivePlots();

  const auto derived_updates = _session.updateDerivedTransforms();

  const std::unordered_set<std::string> updated_curves(derived_updates.begin(),
                                                       derived_updates.end());
  forEachWidget([&](PlotWidget* plot) { plot->updateCurves(&updated_curves, false); });

  //--------------------------------
  updateTimeOffset();
  updateTimeSlider();
  //--------------------------------
  linkedZoomOut();
}

void MainWindow::updateDataAndReplot(const std::unordered_set<std::string>& updated_curves,
                                     bool replot_hidden_tabs)
{
  Q_UNUSED(replot_hidden_tabs);
  _replot_timer->stop();

  forEachWidget([&](PlotWidget* plot) {
    if (plot->updateCurves(&updated_curves, false))
    {
      plot->replot();
    }
  });

  updateTimeOffset();
  updateTimeSlider();
  linkedZoomOut();
}

void MainWindow::on_actionExit_triggered(bool)
{
  this->close();
}

void MainWindow::on_pushButtonRemoveTimeOffset_toggled(bool)
{
  updateTimeOffset();
  updatedDisplayTime();

  forEachWidget([](PlotWidget* plot) { plot->replot(); });

  if (this->signalsBlocked() == false)
  {
    onUndoableChange();
  }
}

void MainWindow::updatedDisplayTime()
{
  QLineEdit* timeLine = ui->displayTime;
  const double relative_time = _tracker_time - _time_offset.get();
  if (ui->pushButtonUseDateTime->isChecked())
  {
    if (ui->pushButtonRemoveTimeOffset->isChecked())
    {
      QTime time = QTime::fromMSecsSinceStartOfDay(std::round(relative_time * 1000.0));
      timeLine->setText(time.toString("HH:mm::ss.zzz"));
    }
    else
    {
      QDateTime datetime =
          QDateTime::fromMSecsSinceEpoch(std::round(_tracker_time * 1000.0));
      timeLine->setText(datetime.toString("[yyyy MMM dd] HH:mm::ss.zzz"));
    }
  }
  else
  {
    timeLine->setText(QString::number(relative_time, 'f', 3));
  }

  QFontMetrics fm(timeLine->font());
  int width = fm.horizontalAdvance(timeLine->text()) + 10;
  timeLine->setFixedWidth(std::max(100, width));
}

void MainWindow::on_pushButtonActivateGrid_toggled(bool checked)
{
  forEachWidget([checked](PlotWidget* plot) {
    plot->activateGrid(checked);
    plot->replot();
  });
}

void MainWindow::on_pushButtonRatio_toggled(bool checked)
{
  forEachWidget([checked](PlotWidget* plot) {
    plot->setKeepRatioXY(checked);
    plot->replot();
  });
}

void MainWindow::on_pushButtonPlay_toggled(bool checked)
{
  if (checked)
  {
    _publish_timer->start();
    _prev_publish_time = QDateTime::currentDateTime();
  }
  else
  {
    _publish_timer->stop();
  }
}

void MainWindow::on_actionClearBuffer_triggered(bool)
{
  _session.clearBuffers();

  forEachWidget([](PlotWidget* plot) {
    plot->updateCurves(nullptr, true);
    plot->replot();
  });
}

void MainWindow::on_deleteSerieFromGroup(std::string group_name)
{
  std::vector<std::string> names;

  auto AddFromGroup = [&](auto& series) {
    for (auto& it : series)
    {
      const auto& group = it.second.group();
      if (group && group->name() == group_name)
      {
        names.push_back(it.first);
      }
    }
  };
  AddFromGroup(_mapped_plot_data.numeric);
  AddFromGroup(_mapped_plot_data.strings);
  AddFromGroup(_mapped_plot_data.user_defined);

  onDeleteMultipleCurves(names);
}

void MainWindow::on_pushButtonUseDateTime_toggled(bool checked)
{
  static bool first = true;
  if (checked && ui->pushButtonRemoveTimeOffset->isChecked())
  {
    if (first)
    {
      QMessageBox::information(this, tr("Note"),
                               tr("When \"Use Date Time\" is checked, the option "
                                  "\"Remove Time Offset\" "
                                  "is automatically disabled.\n"
                                  "This message will be shown only once."));
      first = false;
    }
    ui->pushButtonRemoveTimeOffset->setChecked(false);
  }
  updatedDisplayTime();
}

void MainWindow::on_pushButtonTimeTracker_pressed()
{
  if (_tracker_param == CurveTracker::LINE_ONLY)
  {
    _tracker_param = CurveTracker::VALUE;
  }
  else if (_tracker_param == CurveTracker::VALUE)
  {
    _tracker_param = CurveTracker::VALUE_NAME;
  }
  else if (_tracker_param == CurveTracker::VALUE_NAME)
  {
    _tracker_param = CurveTracker::LINE_ONLY;
  }
  ui->pushButtonTimeTracker->setIcon(_tracker_button_icons[_tracker_param]);

  forEachWidget([&](PlotWidget* plot) {
    plot->configureTracker(_tracker_param);
    plot->replot();
  });
}

void MainWindow::closeEvent(QCloseEvent* event)
{
  const bool embedded_mode = (parentWidget() != nullptr);
  _replot_timer->stop();
  _publish_timer->stop();
  QSettings settings;
  if (!embedded_mode)
  {
    settings.setValue("MainWindow.geometry", saveGeometry());
    settings.setValue("MainWindow.state", saveState());
  }

  settings.setValue("MainWindow.activateGrid", ui->pushButtonActivateGrid->isChecked());
  settings.setValue("MainWindow.removeTimeOffset",
                    ui->pushButtonRemoveTimeOffset->isChecked());
  settings.setValue("MainWindow.dateTimeDisplay", ui->pushButtonUseDateTime->isChecked());
  settings.setValue("MainWindow.buttonLink", ui->pushButtonLink->isChecked());
  settings.setValue("MainWindow.buttonRatio", ui->pushButtonRatio->isChecked());

  settings.setValue("MainWindow.timeTrackerSetting", (int)_tracker_param);
  settings.setValue("MainWindow.splitterWidth", ui->mainSplitter->sizes()[0]);

  _toolboxes.clear();
}

void MainWindow::onAddCustomPlot(const std::string& plot_name)
{
  ui->widgetStack->setCurrentIndex(1);
  _function_editor->setLinkedPlotName(QString::fromStdString(plot_name));
  _function_editor->createNewPlot();
}

void MainWindow::onEditCustomPlot(const std::string& plot_name)
{
  ui->widgetStack->setCurrentIndex(1);
  auto custom_plot = _session.findCustomPlot(plot_name);
  if (!custom_plot)
  {
    qWarning("failed to find custom equation");
    return;
  }
  _function_editor->editExistingPlot(custom_plot);
}

void MainWindow::onRefreshCustomPlot(const std::string& plot_name)
{
  try
  {
    auto custom_plot = _session.findCustomPlot(plot_name);
    if (!custom_plot)
    {
      qWarning("failed to find custom equation");
      return;
    }
    QString error;
    if (!_session.upsertCustomPlot(custom_plot, &error))
    {
      throw std::runtime_error(error.toStdString());
    }

    onUpdateLeftTableValues();
    updateDataAndReplot(true);
  }
  catch (const std::runtime_error& e)
  {
    QMessageBox::critical(this, "error",
                          "Failed to refresh data : " + QString::fromStdString(e.what()));
  }
}

void MainWindow::onPlaybackLoop()
{
  qint64 delta_ms =
      (QDateTime::currentMSecsSinceEpoch() - _prev_publish_time.toMSecsSinceEpoch());
  _prev_publish_time = QDateTime::currentDateTime();
  delta_ms = std::max((qint64)_publish_timer->interval(), delta_ms);

  _tracker_time += delta_ms * 0.001 * ui->playbackRate->value();
  if (_tracker_time >= ui->timeSlider->getMaximum())
  {
    if (!ui->playbackLoop->isChecked())
    {
      ui->pushButtonPlay->setChecked(false);
    }
    _tracker_time = ui->timeSlider->getMinimum();
  }
  //////////////////
  auto prev = ui->timeSlider->blockSignals(true);
  ui->timeSlider->setRealValue(_tracker_time);
  ui->timeSlider->blockSignals(prev);

  //////////////////
  updatedDisplayTime();
  onUpdateLeftTableValues();
  updateReactivePlots();

  forEachWidget([&](PlotWidget* plot) {
    plot->setTrackerPosition(_tracker_time);
    plot->replot();
  });
}

void MainWindow::onCustomPlotCreated(std::vector<CustomPlotPtr> custom_plots)
{
  std::set<PlotWidget*> widget_to_replot;
  std::unordered_set<std::string> dirty_curves;

  for (auto custom_plot : custom_plots)
  {
    const std::string& curve_name = custom_plot->aliasName().toStdString();
    dirty_curves.insert(curve_name);
    QString error;
    const bool inserted = _session.upsertCustomPlot(custom_plot, &error);
    if (!error.isEmpty())
    {
      QMessageBox::warning(this, tr("Warning"),
                           tr("Failed to create the custom timeseries. "
                              "Error:\n\n%1")
                               .arg(error));
    }

    if (inserted)
    {
      _curvelist_widget->addCustom(QString::fromStdString(curve_name));
    }

    forEachWidget([&](PlotWidget* plot) {
      if (plot->curveFromTitle(QString::fromStdString(curve_name)))
      {
        widget_to_replot.insert(plot);
      }
    });
  }

  onUpdateLeftTableValues();
  ui->widgetStack->setCurrentIndex(0);
  _function_editor->clear();

  for (auto plot : widget_to_replot)
  {
    plot->updateCurves(&dirty_curves, true);
    plot->replot();
  }
  _curvelist_widget->clearSelections();
}

/*
void MainWindow::on_actionSaveAllPlotTabs_triggered()
{
  QSettings settings;
  QString directory_path = settings.value("MainWindow.saveAllPlotTabs",
QDir::currentPath()).toString();
  // Get destination folder
  QFileDialog saveDialog(this);
  saveDialog.setDirectory(directory_path);
  saveDialog.setFileMode(QFileDialog::FileMode::Directory);
  saveDialog.setAcceptMode(QFileDialog::AcceptSave);
  saveDialog.exec();

  uint image_number = 1;
  if (saveDialog.result() == QDialog::Accepted && !saveDialog.selectedFiles().empty())
  {
    // Save Plots
    QString directory = saveDialog.selectedFiles().first();
    settings.setValue("MainWindow.saveAllPlotTabs", directory);

    QStringList file_names;
    QStringList existing_files;
    QDateTime current_date_time(QDateTime::currentDateTime());
    QString current_date_time_name(current_date_time.toString("yyyy-MM-dd_HH-mm-ss"));
    for (const auto& it : TabbedPlotWidget::instances())
    {
      auto tab_widget = it.second->tabWidget();
      for (int i = 0; i < tab_widget->count(); i++)
      {
        PlotDocker* matrix = static_cast<PlotDocker*>(tab_widget->widget(i));
        QString name = QString("%1/%2_%3_%4.png")
                           .arg(directory)
                           .arg(current_date_time_name)
                           .arg(image_number, 2, 10, QLatin1Char('0'))
                           .arg(matrix->name());
        file_names.push_back(name);
        image_number++;

        QFileInfo check_file(file_names.back());
        if (check_file.exists() && check_file.isFile())
        {
          existing_files.push_back(name);
        }
      }
    }
    if (existing_files.isEmpty() == false)
    {
      QMessageBox msgBox;
      msgBox.setText("One or more files will be overwritten. ant to continue?");
      QString all_files;
      for (const auto& str : existing_files)
      {
        all_files.push_back("\n");
        all_files.append(str);
      }
      msgBox.setInformativeText(all_files);
      msgBox.setStandardButtons(QMessageBox::Cancel | QMessageBox::Ok);
      msgBox.setDefaultButton(QMessageBox::Ok);

      if (msgBox.exec() != QMessageBox::Ok)
      {
        return;
      }
    }

    image_number = 0;
    for (const auto& it : TabbedPlotWidget::instances())
    {
      auto tab_widget = it.second->tabWidget();
      for (int i = 0; i < tab_widget->count(); i++)
      {
        PlotDocker* matrix = static_cast<PlotDocker*>(tab_widget->widget(i));
        TabbedPlotWidget::saveTabImage(file_names[image_number], matrix);
        image_number++;
      }
    }
  }
}*/

void MainWindow::on_pushButtonLoadLayout_clicked(bool)
{
  QSettings settings;

  QString directory_path =
      settings.value("MainWindow.lastLayoutDirectory", QDir::currentPath()).toString();
  QString filename =
      QFileDialog::getOpenFileName(this, "Open Layout", directory_path, "*.xml");
  if (filename.isEmpty())
  {
    return;
  }

  if (loadLayoutFromFile(filename))
  {
    updateRecentLayoutMenu({ filename });
  }

  directory_path = QFileInfo(filename).absolutePath();
  settings.setValue("MainWindow.lastLayoutDirectory", directory_path);
}

void MainWindow::on_pushButtonSaveLayout_clicked(bool)
{
  LayoutModel saved_layout = saveUiStateModel();

  QSettings settings;

  QString directory_path =
      settings.value("MainWindow.lastLayoutDirectory", QDir::currentPath()).toString();

  QFileDialog saveDialog(this);
  saveDialog.setOption(QFileDialog::DontUseNativeDialog, true);

  QGridLayout* save_layout = static_cast<QGridLayout*>(saveDialog.layout());

  QFrame* frame = new QFrame;
  frame->setFrameStyle(QFrame::Box | QFrame::Plain);
  frame->setLineWidth(1);

  QVBoxLayout* vbox = new QVBoxLayout;
  QLabel* title = new QLabel("Save Layout options");
  QFrame* separator = new QFrame;
  separator->setFrameStyle(QFrame::HLine | QFrame::Plain);

  auto checkbox_snippets = new QCheckBox("Save Scripts (transforms)");
  checkbox_snippets->setToolTip("Do you want the layout to save your Lua scripts?");
  checkbox_snippets->setFocusPolicy(Qt::NoFocus);
  checkbox_snippets->setChecked(
      settings.value("MainWindow.saveLayoutSnippets", true).toBool());

  vbox->addWidget(title);
  vbox->addWidget(separator);
  vbox->addWidget(checkbox_snippets);
  frame->setLayout(vbox);

  int rows = save_layout->rowCount();
  int col = save_layout->columnCount();
  save_layout->addWidget(frame, 0, col, rows, 1, Qt::AlignTop);

  saveDialog.setAcceptMode(QFileDialog::AcceptSave);
  saveDialog.setDefaultSuffix("xml");
  saveDialog.setNameFilter("XML (*.xml)");
  saveDialog.setDirectory(directory_path);
  saveDialog.exec();

  if (saveDialog.result() != QDialog::Accepted || saveDialog.selectedFiles().empty())
  {
    return;
  }

  QString fileName = saveDialog.selectedFiles().first();

  if (fileName.isEmpty())
  {
    return;
  }

  directory_path = QFileInfo(fileName).absolutePath();
  settings.setValue("MainWindow.lastLayoutDirectory", directory_path);
  settings.setValue("MainWindow.saveLayoutSnippets", checkbox_snippets->isChecked());

  saved_layout.plugins_present = true;
  saved_layout.plugins = savePluginStateModel();

  saved_layout.custom_math_present = checkbox_snippets->isChecked();
    saved_layout.custom_math_snippets.clear();
    saved_layout.snippets_present = checkbox_snippets->isChecked();
    saved_layout.snippets.clear();
  if (checkbox_snippets->isChecked())
  {
    for (const auto& custom_it : _transform_functions)
    {
      if (auto custom_plot = std::dynamic_pointer_cast<CustomFunction>(custom_it.second))
      {
        saved_layout.custom_math_snippets.push_back(ToSnippetModel(custom_plot->snippet()));
      }
    }

    QByteArray snippets_xml_text =
        settings.value("AddCustomPlotDialog.savedXML", QByteArray()).toByteArray();
    auto snipped_saved = GetSnippetsFromXML(snippets_xml_text);
    for (const auto& [_, snippet] : snipped_saved)
    {
      saved_layout.snippets.push_back(ToSnippetModel(snippet));
    }
  }
  const QDomDocument normalized_doc = cabana::pj_layout::ToXmlDocument(saved_layout);

  QFile file(fileName);
  if (file.open(QIODevice::WriteOnly))
  {
    QTextStream stream(&file);
    stream << normalized_doc.toString() << Qt::endl;
  }
}

void MainWindow::onActionFullscreenTriggered()
{
  static bool first_call = true;
  if (first_call && !_minimized)
  {
    first_call = false;
    QMessageBox::information(this, "Remember!",
                             "Press F10 to switch back to the normal view");
  }

  _minimized = !_minimized;

  ui->leftMainWindowFrame->setVisible(!_minimized);
  //  ui->widgetOptions->setVisible(!_minimized && ui->pushButtonOptions->isChecked());
  ui->widgetTimescale->setVisible(!_minimized);

  for (auto& it : TabbedPlotWidget::instances())
  {
    it.second->setControlsVisible(!_minimized);
  }
}

void MainWindow::on_actionClearRecentLayout_triggered(bool)
{
  QMenu* menu = _recent_layout_files;
  for (QAction* action : menu->actions())
  {
    if (action->isSeparator())
    {
      break;
    }
    menu->removeAction(action);
  }
  menu->setEnabled(false);
  QSettings settings;
  settings.setValue("MainWindow.recentlyLoadedLayout", {});
}

void MainWindow::on_actionDeleteAllData_triggered(bool)
{
  QMessageBox msgBox(this);
  msgBox.setWindowTitle("Warning. Can't be undone.");
  msgBox.setText(tr("Do you want to remove the previously loaded data?\n"));
  msgBox.addButton(QMessageBox::No);
  msgBox.addButton(QMessageBox::Yes);
  msgBox.setDefaultButton(QMessageBox::Yes);
  auto reply = msgBox.exec();

  if (reply == QMessageBox::No)
  {
    return;
  }

  deleteAllData();
}

void MainWindow::on_actionPreferences_triggered(bool)
{
  QSettings settings;
  QString prev_style = settings.value("Preferences::theme", "light").toString();

  PreferencesDialog dialog;
  dialog.exec();

  QString theme = settings.value("Preferences::theme").toString();

  if (!theme.isEmpty() && theme != prev_style)
  {
    loadStyleSheet(tr(":/resources/stylesheet_%1.qss").arg(theme));
  }
}

void MainWindow::on_playbackStep_valueChanged(double step)
{
  ui->timeSlider->setFocus();
  ui->timeSlider->setRealStepValue(step);
}

void MainWindow::on_actionLoadStyleSheet_triggered(bool)
{
  QSettings settings;
  QString directory_path =
      settings.value("MainWindow.loadStyleSheetDirectory", QDir::currentPath())
          .toString();

  QString fileName = QFileDialog::getOpenFileName(this, tr("Load StyleSheet"),
                                                  directory_path, tr("(*.qss)"));
  if (fileName.isEmpty())
  {
    return;
  }

  loadStyleSheet(fileName);

  directory_path = QFileInfo(fileName).absolutePath();
  settings.setValue("MainWindow.loadStyleSheetDirectory", directory_path);
}

void MainWindow::on_pushButtonLegend_clicked(bool)
{
  switch (_labels_status)
  {
    case LabelStatus::LEFT:
      _labels_status = LabelStatus::HIDDEN;
      break;
    case LabelStatus::RIGHT:
      _labels_status = LabelStatus::LEFT;
      break;
    case LabelStatus::HIDDEN:
      _labels_status = LabelStatus::RIGHT;
      break;
  }

  auto visitor = [=](PlotWidget* plot) {
    plot->activateLegend(_labels_status != LabelStatus::HIDDEN);

    if (_labels_status == LabelStatus::LEFT)
    {
      plot->setLegendAlignment(Qt::AlignLeft);
    }
    else if (_labels_status == LabelStatus::RIGHT)
    {
      plot->setLegendAlignment(Qt::AlignRight);
    }
    plot->replot();
  };

  this->forEachWidget(visitor);
}

void MainWindow::on_pushButtonZoomOut_clicked(bool)
{
  linkedZoomOut();
  onUndoableChange();
}

PopupMenu::PopupMenu(QWidget* relative_widget, QWidget* parent)
  : QMenu(parent), _w(relative_widget)
{
}

void PopupMenu::showEvent(QShowEvent*)
{
  QPoint p = _w->mapToGlobal({});
  QRect geo = _w->geometry();
  this->move(p.x() + geo.width(), p.y());
}

void PopupMenu::leaveEvent(QEvent*)
{
  close();
}

void PopupMenu::closeEvent(QCloseEvent*)
{
  _w->setAttribute(Qt::WA_UnderMouse, false);
}

void MainWindow::on_buttonHideFileFrame_clicked(bool)
{
  bool hidden = !ui->frameFile->isHidden();
  ui->buttonHideFileFrame->setText(hidden ? "+" : " -");
  ui->frameFile->setHidden(hidden);

  QSettings settings;
  settings.setValue("MainWindow.hiddenFileFrame", hidden);
}

void MainWindow::on_buttonRecentLayout_clicked(bool)
{
  PopupMenu* menu = new PopupMenu(ui->buttonRecentLayout, this);

  for (auto action : _recent_layout_files->actions())
  {
    menu->addAction(action);
  }
  menu->exec();
}

QStringList MainWindow::readAllCurvesFromXML(QDomElement root_node)
{
  QSet<QString> curves;

  QStringList level_names = { "tabbed_widget", "Tab",  "Container", "DockSplitter",
                              "DockArea",      "plot", "curve" };

  std::function<void(int, QDomElement)> recursiveXmlStream;
  recursiveXmlStream = [&](int level, QDomElement parent_elem) {
    QString level_name = level_names[level];
    for (auto elem = parent_elem.firstChildElement(level_name); elem.isNull() == false;
         elem = elem.nextSiblingElement(level_name))
    {
      if (level_name == "curve")
      {
        const QString name = elem.attribute("name");
        const QString curve_x = elem.attribute("curve_x");
        const QString curve_y = elem.attribute("curve_y");
        if (!name.isEmpty())
        {
          curves.insert(name);
        }
        if (!curve_x.isEmpty())
        {
          curves.insert(curve_x);
        }
        if (!curve_y.isEmpty())
        {
          curves.insert(curve_y);
        }
      }
      else
      {
        recursiveXmlStream(level + 1, elem);
      }
    }
  };

  // start recursion
  recursiveXmlStream(0, root_node);

  return curves.values();
}
