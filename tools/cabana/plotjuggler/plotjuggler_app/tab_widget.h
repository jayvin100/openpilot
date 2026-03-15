/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#ifndef TAB_BAR_H
#define TAB_BAR_H

#include <QTabBar>
#include <QLabel>
#include <QPixmap>
#include <QDebug>
#include <QTabWidget>
#include <QMimeData>
#include <QDragEnterEvent>
#include <QDragLeaveEvent>
#include <QApplication>
#include "curve_drag.h"

class TabWidget : public QTabWidget
{
  Q_OBJECT
public:
  TabWidget(QWidget* parent) : QTabWidget(parent)
  {
    this->tabBar()->setFixedHeight(40);
    setAcceptDrops(true);
  }

  void dragEnterEvent(QDragEnterEvent* ev) override
  {
    if (ev->pos().y() > 43)
    {
      ev->ignore();
      return;
    }
    CurveDragPayload payload = DecodeCurveDragPayload(ev->mimeData());
    if (!payload.isValid())
    {
      ev->ignore();
      return;
    }
    ev->ignore();
  }

  void dragMoveEvent(QDragMoveEvent* ev) override
  {
    if (ev->pos().y() > 43)
    {
      ev->ignore();
      return;
    }
    ev->accept();
  }

  void dragLeaveEvent(QDragLeaveEvent* ev) override
  {
    _plot_to_move = "";
    QApplication::restoreOverrideCursor();
  }

  void dropEvent(QDropEvent* ev) override
  {
    emit movingPlotWidgetToTab(_plot_to_move);
    _plot_to_move = "";
    QApplication::restoreOverrideCursor();
  }

private:
  QString _plot_to_move;

signals:

  void movingPlotWidgetToTab(QString plot_name);
};

#endif  // TAB_BAR_H
