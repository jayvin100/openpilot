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

#include <QCommandLineParser>
#include <QElapsedTimer>
#include <QMainWindow>
#include <QPushButton>
#include <QSignalMapper>
#include <QShortcut>
#include <QMovie>

#include "plotwidget.h"
#include "plot_docker.h"
#include "curvelist_panel.h"
#include "tabbedplotwidget.h"
#include "realslider.h"
#include "utils.h"
#include "PlotJuggler/dataloader_base.h"
#include "PlotJuggler/statepublisher_base.h"
#include "PlotJuggler/toolbox_base.h"
#include "PlotJuggler/datastreamer_base.h"
#include "PlotJuggler/util/delayed_callback.hpp"
#include "transforms/custom_function.h"
#include "transforms/function_editor.h"

#include "ui_mainwindow.h"

class MainWindow : public QMainWindow
{
  Q_OBJECT

public:
  explicit MainWindow(const QCommandLineParser& commandline_parser,
                      QWidget* parent = nullptr);

  ~MainWindow();

  bool isEmbeddedMode() const { return _embedded_mode; }

  void clearExternalData();
  void appendExternalData(PlotDataMapRef&& new_data);
  void setExternalTrackerTime(double absolute_time);
  void setExternalPlaybackPaused(bool paused);

  bool loadLayoutFromFile(QString filename);
  bool loadDataFromFiles(QStringList filenames);
  std::unordered_set<std::string> loadDataFromFile(const FileLoadInfo& info);

  void stopStreamingPlugin();
  void startStreamingPlugin(QString streamer_name);
  void enableStreamingNotificationsButton(bool enabled);

public slots:

  void resizeEvent(QResizeEvent*);
  // Undo - Redo
  void onUndoableChange();
  void onUndoInvoked();
  void onRedoInvoked();

  // Actions in UI
  void on_streamingToggled();

  void on_buttonStreamingPause_toggled(bool paused);
  void on_buttonStreamingNotifications_clicked(bool checked = false);

  void on_streamingSpinBox_valueChanged(int value);

  void on_comboStreaming_currentIndexChanged(const QString& current_text);

  void on_splitterMoved(int, int);

  void onTrackerTimeUpdated(double absolute_time, bool do_replot);
  void onTrackerMovedFromWidget(QPointF pos);
  void onTimeSlider_valueChanged(double abs_time);

  void onPlotAdded(PlotWidget* plot);

  void onPlotTabAdded(PlotDocker* docker);

  void onPlotZoomChanged(PlotWidget* modified_plot, QRectF new_range);

  void on_tabbedAreaDestroyed(QObject* object);

  void updateDataAndReplot(bool replot_hidden_tabs);

  void onUpdateLeftTableValues();

  void onDeleteMultipleCurves(const std::vector<std::string>& curve_names);

  void onAddCustomPlot(const std::string& plot_name);

  void onEditCustomPlot(const std::string& plot_name);

  void onRefreshCustomPlot(const std::string& plot_name);

  void onCustomPlotCreated(std::vector<CustomPlotPtr> plot);

  void onPlaybackLoop();

  void linkedZoomOut();

private:
  Ui::MainWindow* ui;

  TabbedPlotWidget* _main_tabbed_widget;

  QShortcut _undo_shortcut;
  QShortcut _redo_shortcut;
  QShortcut _fullscreen_shortcut;
  QShortcut _streaming_shortcut;
  QShortcut _playback_shotcut;

  bool _minimized;

  CurveListPanel* _curvelist_widget;

  PlotDataMapRef _mapped_plot_data;

  TransformsMap _transform_functions;

  std::map<QString, DataLoaderPtr> _data_loader;
  std::map<QString, StatePublisherPtr> _state_publisher;
  std::map<QString, DataStreamerPtr> _data_streamer;
  std::map<QString, ToolboxPluginPtr> _toolboxes;

  QString _default_streamer;

  ParserFactories _parser_factories;

  std::shared_ptr<DataStreamer> _active_streamer_plugin;

  std::deque<QDomDocument> _undo_states;
  std::deque<QDomDocument> _redo_states;
  QElapsedTimer _undo_timer;
  bool _disable_undo_logging;

  bool _test_option;

  bool _autostart_publishers;

  double _tracker_time;

  QStringList _enabled_plugins;
  QStringList _disabled_plugins;

  std::vector<FileLoadInfo> _loaded_datafiles;
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

  QMenu* _recent_data_files;
  QMenu* _recent_layout_files;

  QString _current_theme = "light";

  void initializeActions();
  QStringList initializePlugins(QString subdir_name);

  void forEachWidget(std::function<void(PlotWidget*, PlotDocker*, int)> op);
  void forEachWidget(std::function<void(PlotWidget*)> op);

  void rearrangeGridLayout();

  QDomDocument xmlSaveState() const;
  bool xmlLoadState(QDomDocument state_document);

  void checkAllCurvesFromLayout(const QDomElement& root);

  void importPlotDataMap(PlotDataMapRef& new_data, bool remove_old);

  bool isStreamingActive() const;

  void closeEvent(QCloseEvent* event);

  void loadPluginState(const QDomElement& root);
  QDomElement savePluginState(QDomDocument& doc);

  std::tuple<double, double, int> calculateVisibleRangeX();

  void deleteAllData();

  void updateRecentDataMenu(QStringList new_filenames);
  void updateRecentLayoutMenu(QStringList new_filenames);

  void updatedDisplayTime();

  void updateTimeSlider();
  void updateTimeOffset();

  void buildDummyData();

  void loadStyleSheet(QString file_path);
  void applyTheme(const QString& theme);

  void updateDerivedSeries();

  void updateReactivePlots();

  void dragEnterEvent(QDragEnterEvent* event);

  void dropEvent(QDropEvent* event);

public slots:

  void on_actionClearRecentData_triggered(bool checked = false);
  void on_actionClearRecentLayout_triggered(bool checked = false);

  void on_actionDeleteAllData_triggered(bool checked = false);
  void on_actionClearBuffer_triggered(bool checked = false);

  void on_deleteSerieFromGroup(std::string group_name);

  void on_streamingNotificationsChanged(int active_notifications_count);

  void onActionFullscreenTriggered();

  void on_actionExit_triggered(bool checked = false);

  void on_pushButtonActivateGrid_toggled(bool checked);
  void on_pushButtonRatio_toggled(bool checked);
  void on_pushButtonPlay_toggled(bool checked);
  void on_pushButtonUseDateTime_toggled(bool checked);
  void on_pushButtonTimeTracker_pressed();
  void on_pushButtonRemoveTimeOffset_toggled(bool checked);

  void on_buttonStreamingStart_clicked(bool checked = false);

private slots:
  void on_stylesheetChanged(QString style_name);
  void on_actionPreferences_triggered(bool checked = false);
  void on_actionShare_the_love_triggered(bool checked = false);
  void on_playbackStep_valueChanged(double arg1);
  void on_actionLoadStyleSheet_triggered(bool checked = false);
  void on_pushButtonLegend_clicked(bool checked = false);
  void on_pushButtonZoomOut_clicked(bool checked = false);

  void on_buttonStreamingOptions_clicked(bool checked = false);
  void on_buttonHideFileFrame_clicked(bool checked = false);
  void on_buttonHideStreamingFrame_clicked(bool checked = false);

  void on_buttonRecentData_clicked(bool checked = false);
  void on_buttonRecentLayout_clicked(bool checked = false);
  void on_pushButtonLoadLayout_clicked(bool checked = false);
  void on_pushButtonSaveLayout_clicked(bool checked = false);
  void on_pushButtonLoadDatafile_clicked(bool checked = false);

private:
  QStringList readAllCurvesFromXML(QDomElement root_node);
  void loadAllPlugins(QStringList command_line_plugin_folders);
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
