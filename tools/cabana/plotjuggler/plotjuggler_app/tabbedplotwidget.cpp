/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include <QMenu>
#include <QSignalMapper>
#include <QAction>
#include <QTabBar>
#include <QSvgGenerator>
#include <QInputDialog>
#include <QMouseEvent>
#include <QFileDialog>
#include <QApplication>
#include <QPainter>
#include <QTabWidget>
#include <QPushButton>
#include <QHBoxLayout>
#include "qwt_plot_renderer.h"
#include "mainwindow.h"
#include "tabbedplotwidget.h"
#include "tab_widget.h"
#include "PlotJuggler/svg_util.h"

TabbedPlotWidget::TabbedPlotWidget(QString name,
                                   PlotDataMapRef& mapped_data,
                                   cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup,
                                   QMainWindow* parent)
  : QWidget(parent), _name(name), _mapped_data(mapped_data),
    _snapshot_lookup(std::move(snapshot_lookup))
{
  setContentsMargins(0, 0, 0, 0);

  _horizontal_link = true;

  QHBoxLayout* main_layout = new QHBoxLayout(this);
  main_layout->setMargin(0);

  _tabWidget = new QTabWidget(this);
  _tabWidget->setTabsClosable(true);
  _tabWidget->setMovable(true);

  connect(_tabWidget->tabBar(), &QTabBar::tabBarDoubleClicked, this,
          &TabbedPlotWidget::on_renameCurrentTab);
  connect(_tabWidget, &QTabWidget::tabCloseRequested, this,
          &TabbedPlotWidget::on_tabWidget_tabCloseRequested);

  main_layout->addWidget(_tabWidget);

  connect(_tabWidget, &QTabWidget::currentChanged, this,
          &TabbedPlotWidget::on_tabWidget_currentChanged);

  tabWidget()->tabBar()->installEventFilter(this);

  // TODO _action_savePlots = new QAction(tr("&Save plots to file"), this);
  // TODO connect(_action_savePlots, &QAction::triggered, this,
  // &TabbedPlotWidget::on_savePlotsToFile);

  //  _tab_menu = new QMenu(this);
  //  _tab_menu->addSeparator();
  //  //_tab_menu->addAction(_action_savePlots);
  //  _tab_menu->addSeparator();

  this->addTab({});

  _buttonAddTab = new QPushButton("", this);
  _buttonAddTab->setFlat(true);
  _buttonAddTab->setFixedSize(QSize(32, 32));
  _buttonAddTab->setFocusPolicy(Qt::NoFocus);

  connect(_buttonAddTab, &QPushButton::pressed, this,
          &TabbedPlotWidget::on_addTabButton_pressed);
}

void TabbedPlotWidget::paintEvent(QPaintEvent* event)
{
  QWidget::paintEvent(event);

  auto size = tabWidget()->tabBar()->size();
  _buttonAddTab->move(QPoint(size.width() + 5, 0));
}

PlotDocker* TabbedPlotWidget::currentTab()
{
  return static_cast<PlotDocker*>(tabWidget()->currentWidget());
}

QTabWidget* TabbedPlotWidget::tabWidget()
{
  return _tabWidget;
}

const QTabWidget* TabbedPlotWidget::tabWidget() const
{
  return _tabWidget;
}

PlotDocker* TabbedPlotWidget::addTab(QString tab_name)
{
  return createTab(tab_name);
}

PlotDocker* TabbedPlotWidget::createTab(const QString& requested_name)
{
  static int tab_suffix_count = 1;

  // this must be done before any PlotDocker is created
  ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasTabsMenuButton, false);
  ads::CDockManager::setConfigFlag(ads::CDockManager::DockAreaHasUndockButton, false);
  ads::CDockManager::setConfigFlag(ads::CDockManager::AlwaysShowTabs, true);
  ads::CDockManager::setConfigFlag(ads::CDockManager::EqualSplitOnInsertion, true);
  ads::CDockManager::setConfigFlag(ads::CDockManager::OpaqueSplitterResize, true);

  QString tab_name = requested_name;
  if (tab_name.isEmpty())
  {
    tab_name = QString("tab%1").arg(tab_suffix_count++);
  }

  auto docker = new PlotDocker(tab_name, _mapped_data, _snapshot_lookup, this);
  connect(docker, &PlotDocker::undoableChange, this, &TabbedPlotWidget::undoableChange);

  tabWidget()->addTab(docker, tab_name);

  emit tabAdded(docker);
  // we need to send the signal for the very first widget
  emit docker->plotWidgetAdded(docker->plotAt(0));

  int index = tabWidget()->count() - 1;

  QWidget* button_widget = new QWidget();
  QHBoxLayout* layout = new QHBoxLayout(button_widget);
  layout->setSpacing(2);
  layout->setMargin(0);

  QPushButton* close_button = new QPushButton();

  QSettings settings;
  QString theme = settings.value("StyleSheet::theme", "light").toString();
  close_button->setIcon(LoadSvg(":/resources/svg/close-button.svg", theme));

  close_button->setFixedSize(QSize(16, 16));
  close_button->setFlat(true);
  connect(close_button, &QPushButton::pressed, this, [this, docker]() {
    closeTab(tabWidget()->indexOf(docker));
  });

  layout->addWidget(close_button);
  tabWidget()->tabBar()->setTabButton(index, QTabBar::RightSide, button_widget);

  docker->setHorizontalLink(_horizontal_link);

  tabWidget()->setCurrentWidget(docker);

  return docker;
}

bool TabbedPlotWidget::renameTab(int index, const QString& name)
{
  if (index < 0 || index >= tabWidget()->count() || name.isEmpty())
  {
    return false;
  }

  tabWidget()->setTabText(index, name);
  auto* docker = qobject_cast<PlotDocker*>(tabWidget()->widget(index));
  if (!docker)
  {
    return false;
  }
  docker->setName(name);
  emit undoableChange();
  return true;
}

bool TabbedPlotWidget::closeTab(int index)
{
  if (index < 0 || index >= tabWidget()->count())
  {
    return false;
  }

  if (tabWidget()->count() == 1)
  {
    createTab({});
  }

  auto* docker = qobject_cast<PlotDocker*>(tabWidget()->widget(index));
  if (!docker)
  {
    return false;
  }

  for (unsigned p = 0; p < docker->plotCount(); p++)
  {
    PlotWidget* plot = docker->plotAt(p);
    plot->removeAllCurves();
    plot->deleteLater();
  }
  docker->deleteLater();

  tabWidget()->removeTab(index);
  emit undoableChange();
  return true;
}

cabana::pj_layout::TabbedWidgetModel TabbedPlotWidget::saveStateModel() const
{
  cabana::pj_layout::TabbedWidgetModel widget_model;
  widget_model.name = _name;
  widget_model.parent = _parent_type;
  widget_model.current_tab_index = tabWidget()->currentIndex();

  for (int i = 0; i < tabWidget()->count(); ++i)
  {
    auto* widget = static_cast<PlotDocker*>(tabWidget()->widget(i));
    auto tab_model = widget->saveStateModel();
    tab_model.tab_name = tabWidget()->tabText(i);
    widget_model.tabs.push_back(std::move(tab_model));
  }
  return widget_model;
}

bool TabbedPlotWidget::loadStateModel(const cabana::pj_layout::TabbedWidgetModel& widget_model)
{
  int prev_count = tabWidget()->count();

  for (const auto& tab_model : widget_model.tabs)
  {
    PlotDocker* docker = addTab(tab_model.tab_name);
    if (!docker->loadStateModel(tab_model))
    {
      return false;
    }
  }

  for (int i = 0; i < prev_count; ++i)
  {
    tabWidget()->widget(0)->deleteLater();
    tabWidget()->removeTab(0);
  }

  if (widget_model.current_tab_index >= 0 && widget_model.current_tab_index < tabWidget()->count())
  {
    tabWidget()->setCurrentIndex(widget_model.current_tab_index);
  }

  emit undoableChange();
  return true;
}

QDomElement TabbedPlotWidget::xmlSaveState(QDomDocument& doc) const
{
  QDomElement tabbed_area = doc.createElement("tabbed_widget");

  tabbed_area.setAttribute("name", _name);
  tabbed_area.setAttribute("parent", _parent_type);

  for (int i = 0; i < tabWidget()->count(); i++)
  {
    PlotDocker* widget = static_cast<PlotDocker*>(tabWidget()->widget(i));
    QDomElement element = widget->xmlSaveState(doc);

    element.setAttribute("tab_name", tabWidget()->tabText(i));
    tabbed_area.appendChild(element);
  }

  QDomElement current_plotmatrix = doc.createElement("currentTabIndex");
  current_plotmatrix.setAttribute("index", tabWidget()->currentIndex());
  tabbed_area.appendChild(current_plotmatrix);

  return tabbed_area;
}

bool TabbedPlotWidget::xmlLoadState(QDomElement& tabbed_area)
{
  int prev_count = tabWidget()->count();

  for (auto docker_elem = tabbed_area.firstChildElement("Tab"); !docker_elem.isNull();
       docker_elem = docker_elem.nextSiblingElement("Tab"))
  {
    QString tab_name = docker_elem.attribute("tab_name");
    PlotDocker* docker = addTab(tab_name);

    bool success = docker->xmlLoadState(docker_elem);

    if (!success)
    {
      return false;
    }
  }

  // remove old ones
  for (int i = 0; i < prev_count; i++)
  {
    tabWidget()->widget(0)->deleteLater();
    tabWidget()->removeTab(0);
  }

  QDomElement current_tab = tabbed_area.firstChildElement("currentTabIndex");
  int current_index = current_tab.attribute("index").toInt();

  if (current_index >= 0 && current_index < tabWidget()->count())
  {
    tabWidget()->setCurrentIndex(current_index);
  }

  emit undoableChange();
  return true;
}

void TabbedPlotWidget::setStreamingMode(bool streaming_mode)
{
}

TabbedPlotWidget::~TabbedPlotWidget()
{
}

void TabbedPlotWidget::on_renameCurrentTab(int index)
{
  int idx = (index >= 0) ? index : tabWidget()->tabBar()->currentIndex();
  if (idx < 0 || idx >= tabWidget()->count())
  {
    return;
  }

  bool ok = true;
  QString newName =
      QInputDialog::getText(this, tr("Change the tab name"), tr("New name:"),
                            QLineEdit::Normal, tabWidget()->tabText(idx), &ok);
  if (ok)
  {
    renameTab(idx, newName);
  }
}

void TabbedPlotWidget::on_stylesheetChanged(QString theme)
{
  _buttonAddTab->setIcon(LoadSvg(":/resources/svg/add_tab.svg", theme));
}

void TabbedPlotWidget::on_addTabButton_pressed()
{
  createTab({});
  emit undoableChange();
}

void TabbedPlotWidget::on_tabWidget_currentChanged(int index)
{
  if (tabWidget()->count() == 0)
  {
    if (_parent_type.compare("main_window") == 0)
    {
      addTab(nullptr);
    }
    else
    {
      this->parent()->deleteLater();
    }
  }

  PlotDocker* tab = dynamic_cast<PlotDocker*>(tabWidget()->widget(index));
  if (tab)
  {
    tab->replot();
  }
  for (int i = 0; i < tabWidget()->count(); i++)
  {
    auto button = _tabWidget->tabBar()->tabButton(i, QTabBar::RightSide);
    if (button)
    {
      button->setHidden(i != index);
    }
  }
}

void TabbedPlotWidget::on_tabWidget_tabCloseRequested(int index)
{
  closeTab(index);
}

void TabbedPlotWidget::on_buttonLinkHorizontalScale_toggled(bool checked)
{
  _horizontal_link = checked;

  for (int i = 0; i < tabWidget()->count(); i++)
  {
    PlotDocker* tab = static_cast<PlotDocker*>(tabWidget()->widget(i));
    tab->setHorizontalLink(_horizontal_link);
  }
}

bool TabbedPlotWidget::eventFilter(QObject* obj, QEvent* event)
{
  QTabBar* tab_bar = tabWidget()->tabBar();

  if (obj == tab_bar)
  {
    if (event->type() == QEvent::MouseButtonPress)
    {
      QMouseEvent* mouse_event = static_cast<QMouseEvent*>(event);

      int index = tab_bar->tabAt(mouse_event->pos());
      tab_bar->setCurrentIndex(index);

      if (mouse_event->button() == Qt::RightButton)
      {
        // QMenu* submenu = new QMenu("Move tab to...");
        // _tab_menu->addMenu(submenu);

        // QSignalMapper* signalMapper = new QSignalMapper(submenu);

        //-----------------------------------
        // Reserved for future tab context actions.
      }
    }
  }

  // Standard event processing
  return QObject::eventFilter(obj, event);
}

void TabbedPlotWidget::setControlsVisible(bool visible)
{
  // ui->widgetControls->setVisible(visible);
}
