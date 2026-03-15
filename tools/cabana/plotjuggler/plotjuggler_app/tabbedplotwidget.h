/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef TABBEDPLOTWIDGET_H
#define TABBEDPLOTWIDGET_H

#include <QWidget>
#include <QMainWindow>
#include <QTableWidget>
#include <QDomDocument>
#include <functional>
#include "plot_docker.h"
#include "tools/cabana/pj_layout/layout_model.h"

class TabbedPlotWidget : public QWidget
{
  Q_OBJECT

public:
  explicit TabbedPlotWidget(QString name,
                            PlotDataMapRef& mapped_data,
                            cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup,
                            QMainWindow* parent);

  PlotDocker* currentTab();

  QTabWidget* tabWidget();

  const QTabWidget* tabWidget() const;

  PlotDocker* addTab(QString name);
  PlotDocker* createTab(const QString& name = {});
  bool renameTab(int index, const QString& name);
  bool closeTab(int index);
  bool applyWidgetModelEdit(
      const std::function<bool(cabana::pj_layout::TabbedWidgetModel*)>& mutator);
  bool applyTabModelEdit(PlotDocker* docker,
                         const std::function<bool(cabana::pj_layout::TabModel*)>& mutator);

  cabana::pj_layout::TabbedWidgetModel saveStateModel() const;
  bool loadStateModel(const cabana::pj_layout::TabbedWidgetModel& widget_model);

  QDomElement xmlSaveState(QDomDocument& doc) const;

  bool xmlLoadState(QDomElement& tabbed_area);

  ~TabbedPlotWidget() override;

  QString name() const
  {
    return _name;
  }

  void setControlsVisible(bool visible);

public slots:

  void setStreamingMode(bool streaming_mode);

  // static void saveTabImage(QString fileName, PlotDocker* matrix);

  void on_stylesheetChanged(QString style_dir);

private slots:

  void on_renameCurrentTab(int index = -1);

  // void on_savePlotsToFile();

  void on_addTabButton_pressed();

  void on_tabWidget_currentChanged(int index);

  void on_tabWidget_tabCloseRequested(int index);

  void on_buttonLinkHorizontalScale_toggled(bool checked);

  void paintEvent(QPaintEvent* event) override;

private:
  PlotDocker* createTabWidget(const QString& name = {});

  QTabWidget* _tabWidget;

  QPushButton* _buttonHorizontalLink;
  QPushButton* _buttonLegend;
  QPushButton* _buttonAddTab;

  // TODO QAction* _action_savePlots;

  // QMenu* _tab_menu;

  const QString _name;

  PlotDataMapRef& _mapped_data;
  cabana::pj_engine::SeriesSnapshotLookup _snapshot_lookup;

  bool _horizontal_link;

  const QString _parent_type = "main_window";

protected:
  virtual bool eventFilter(QObject* obj, QEvent* event) override;

signals:
  void created();
  void undoableChange();
  void tabAdded(PlotDocker*);
};

#endif  // TABBEDPLOTWIDGET_H
