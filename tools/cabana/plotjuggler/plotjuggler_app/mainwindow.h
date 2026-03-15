/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <set>
#include <deque>
#include <functional>

#include <QElapsedTimer>
#include <QMainWindow>
#include <QMovie>
#include <QPushButton>
#include <QShortcut>

#include "plotwidget.h"
#include "plot_docker.h"
#include "curvelist_panel.h"
#include "tabbedplotwidget.h"
#include "PlotJuggler/util/delayed_callback.hpp"
#include "PlotJuggler/toolbox_base.h"
#include "realslider.h"
#include "transforms/custom_function.h"
#include "transforms/function_editor.h"
#include "utils.h"
#include "tools/cabana/pj_engine/session.h"
#include "tools/cabana/pj_layout/layout_model.h"

#include "ui_mainwindow.h"

struct MainWindowConfig
{
  QString window_title;
};

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const MainWindowConfig& config = {}, QWidget* parent = nullptr);

  ~MainWindow();

  bool isEmbeddedMode() const { return _embedded_mode; }

  void clearExternalData();
  void appendExternalData(PlotDataMapRef&& new_data);
  void setExternalTrackerTime(double absolute_time);
  void setExternalPlaybackPaused(bool paused);

  bool loadLayoutFromFile(QString filename);

public slots:

  void resizeEvent(QResizeEvent*);
  // Undo - Redo
  void onUndoableChange();
  void onUndoInvoked();
  void onRedoInvoked();

  void on_splitterMoved(int, int);

  void onTrackerTimeUpdated(double absolute_time, bool do_replot);
  void onTrackerMovedFromWidget(QPointF pos);
  void onTimeSlider_valueChanged(double abs_time);

  void onPlotAdded(PlotWidget* plot);

  void onPlotTabAdded(PlotDocker* docker);

  void onPlotZoomChanged(PlotWidget* modified_plot, QRectF new_range);

  void on_tabbedAreaDestroyed(QObject* object);

  void updateDataAndReplot(bool replot_hidden_tabs);
  void updateDataAndReplot(const std::unordered_set<std::string>& updated_curves,
                           bool replot_hidden_tabs);

  void onUpdateLeftTableValues();

  void onDeleteMultipleCurves(const std::vector<std::string>& curve_names);

  void onAddCustomPlot(const std::string& plot_name);

  void onEditCustomPlot(const std::string& plot_name);

  void onRefreshCustomPlot(const std::string& plot_name);

  void onCustomPlotCreated(std::vector<CustomPlotPtr> plot);

  void onPlaybackLoop();

  void linkedZoomOut();

private:
  cabana::pj_engine::PlotJugglerSession _session;
  Ui::MainWindow* ui;

  TabbedPlotWidget* _main_tabbed_widget;

  QShortcut _undo_shortcut;
  QShortcut _redo_shortcut;
  QShortcut _fullscreen_shortcut;
  QShortcut _playback_shotcut;

  bool _minimized;

  CurveListPanel* _curvelist_widget;

  PlotDataMapRef& _mapped_plot_data;

  TransformsMap& _transform_functions;
  std::map<QString, ToolboxPluginPtr> _toolboxes;

  std::deque<cabana::pj_layout::LayoutModel> _undo_states;
  std::deque<cabana::pj_layout::LayoutModel> _redo_states;
  QElapsedTimer _undo_timer;
  bool _disable_undo_logging;

  double _tracker_time;
  CurveTracker::Parameter _tracker_param;

  std::map<CurveTracker::Parameter, QIcon> _tracker_button_icons;

  MonitoredValue _time_offset;

  QTimer* _replot_timer;
  QTimer* _publish_timer;
  PJ::DelayedCallback _tracker_delay;

  QDateTime _prev_publish_time;

  FunctionEditorWidget* _function_editor;

  QMovie* _animated_streaming_movie;
  QTimer* _animated_streaming_timer;

  enum LabelStatus
  {
    LEFT,
    RIGHT,
    HIDDEN
  };

  LabelStatus _labels_status;
  bool _embedded_mode;

  QMenu* _tools_menu = nullptr;
  QMenu* _recent_layout_files;

  QString _current_theme = "light";

  void initializeActions();
  void initializeEmbeddedToolboxes();
  void addToolbox(ToolboxPluginPtr toolbox);

  void forEachWidget(std::function<void(PlotWidget*, PlotDocker*, int)> op);
  void forEachWidget(std::function<void(PlotWidget*)> op);

  void rearrangeGridLayout();

  cabana::pj_layout::LayoutModel saveUiStateModel() const;
  bool loadUiStateModel(const cabana::pj_layout::LayoutModel& layout);
  QDomDocument xmlSaveState() const;
  bool xmlLoadState(QDomDocument state_document);
  QDomDocument saveUiStateDom() const;
  bool loadUiStateDom(const QDomDocument& state_document);
  bool loadLayoutModel(const cabana::pj_layout::LayoutModel& layout);
  bool loadLayoutDocument(const QDomDocument& dom_document);

  void checkAllCurvesFromLayout(const QDomElement& root);

  void importPlotDataMap(PlotDataMapRef& new_data, bool remove_old);

  void closeEvent(QCloseEvent* event);

  void loadPluginState(const cabana::pj_layout::LayoutModel& layout);
  std::vector<cabana::pj_layout::PluginState> savePluginStateModel() const;

  std::tuple<double, double, int> calculateVisibleRangeX();

  void deleteAllData();

  void updateRecentLayoutMenu(QStringList new_filenames);

  void updatedDisplayTime();

  void updateTimeSlider();
  void updateTimeOffset();

  void loadStyleSheet(QString file_path);
  void applyTheme(const QString& theme);

  void updateDerivedSeries();

  void updateReactivePlots();

  void dragEnterEvent(QDragEnterEvent* event);

  void dropEvent(QDropEvent* event);

public slots:

  void on_actionClearRecentLayout_triggered(bool checked = false);

  void on_actionDeleteAllData_triggered(bool checked = false);
  void on_actionClearBuffer_triggered(bool checked = false);

  void on_deleteSerieFromGroup(std::string group_name);

  void onActionFullscreenTriggered();

  void on_actionExit_triggered(bool checked = false);

  void on_pushButtonActivateGrid_toggled(bool checked);
  void on_pushButtonRatio_toggled(bool checked);
  void on_pushButtonPlay_toggled(bool checked);
  void on_pushButtonUseDateTime_toggled(bool checked);
  void on_pushButtonTimeTracker_pressed();
  void on_pushButtonRemoveTimeOffset_toggled(bool checked);

private slots:
  void on_stylesheetChanged(QString style_name);
  void on_actionPreferences_triggered(bool checked = false);
  void on_playbackStep_valueChanged(double arg1);
  void on_actionLoadStyleSheet_triggered(bool checked = false);
  void on_pushButtonLegend_clicked(bool checked = false);
  void on_pushButtonZoomOut_clicked(bool checked = false);

  void on_buttonHideFileFrame_clicked(bool checked = false);

  void on_buttonRecentLayout_clicked(bool checked = false);
  void on_pushButtonLoadLayout_clicked(bool checked = false);
  void on_pushButtonSaveLayout_clicked(bool checked = false);

private:
  QStringList readAllCurvesFromXML(QDomElement root_node);
};

class PopupMenu : public QMenu
{
  Q_OBJECT
public:
  explicit PopupMenu(QWidget* relative_widget, QWidget* parent = nullptr);

  void showEvent(QShowEvent*) override;
  void leaveEvent(QEvent*) override;
  void closeEvent(QCloseEvent*) override;

private:
  QWidget* _w;
};

#endif  // MAINWINDOW_H
