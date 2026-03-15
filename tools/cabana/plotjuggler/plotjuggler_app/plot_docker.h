/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef PLOT_DOCKER_H
#define PLOT_DOCKER_H

#include <QDomElement>
#include <QXmlStreamReader>
#include "PlotJuggler/plotdata.h"
#include "plotwidget.h"
#include "plot_docker_toolbar.h"
#include "tools/cabana/pj_layout/layout_model.h"

class DockWidget : public ads::CDockWidget
{
  Q_OBJECT

public:
  DockWidget(PlotDataMapRef& datamap,
             cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup = {},
             QWidget* parent = nullptr);

  ~DockWidget() override;

  PlotWidget* plotWidget();
  PlotWidget* takePlotWidget();

  DockToolbar* toolBar();
  QString title() const;
  void setTitle(const QString& title);

public slots:
  DockWidget* splitHorizontal();

  DockWidget* splitVertical();

private:
  PlotWidget* _plot_widget = nullptr;

  DockToolbar* _toolbar;

  PlotDataMapRef& _datamap;
  cabana::pj_engine::SeriesSnapshotLookup _snapshot_lookup;

signals:
  void undoableChange();
};

class PlotDocker : public ads::CDockManager
{
  Q_OBJECT

public:
  PlotDocker(QString name, PlotDataMapRef& datamap,
             cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup = {},
             QWidget* parent = nullptr);

  ~PlotDocker();

  QString name() const;

  void setName(QString name);

  cabana::pj_layout::TabModel saveStateModel() const;
  bool loadStateModel(const cabana::pj_layout::TabModel& tab_model);

  QDomElement xmlSaveState(QDomDocument& doc) const;

  bool xmlLoadState(QDomElement& tab_element);

  int plotCount() const;

  PlotWidget* plotAt(int index);
  DockWidget* createDockWidget();
  DockWidget* splitDockWidget(DockWidget* source, Qt::Orientation orientation);
  bool closeDockWidget(DockWidget* dock_widget);
  void setDockTitle(DockWidget* dock_widget, const QString& title);
  void toggleDockFullscreen(DockWidget* dock_widget);

  void setHorizontalLink(bool enabled);

  void zoomOut();

  void replot();

public slots:

  void on_stylesheetChanged(QString theme);

private:
  void restoreSplitter(QDomElement elem, DockWidget* widget);

  QString _name;

  PlotDataMapRef& _datamap;
  cabana::pj_engine::SeriesSnapshotLookup _snapshot_lookup;

signals:

  void plotWidgetAdded(PlotWidget*);

  void undoableChange();
};

#endif  // PLOT_DOCKER_H
