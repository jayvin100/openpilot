/*
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 */

#include "plot_docker.h"
#include "plotwidget_editor.h"
#include "tabbedplotwidget.h"
#include "Qads/DockSplitter.h"
#include <QPushButton>
#include <QBoxLayout>
#include <QMouseEvent>
#include <QSplitter>
#include <QDebug>
#include <QInputDialog>
#include <QLineEdit>
#include "PlotJuggler/svg_util.h"

namespace {

cabana::pj_layout::LayoutNode MakeDefaultDockAreaNode(const QString& title = "...")
{
  cabana::pj_layout::LayoutNode node;
  node.kind = cabana::pj_layout::LayoutNode::Kind::DockArea;
  node.name = title;
  node.plots.push_back(cabana::pj_layout::PlotModel{});
  return node;
}

void NormalizeSplitterSizes(cabana::pj_layout::LayoutNode* node)
{
  if (!node || node->kind != cabana::pj_layout::LayoutNode::Kind::Splitter)
  {
    return;
  }

  node->sizes.assign(node->children.size(),
                     node->children.empty() ? 0.0 : 1.0 / double(node->children.size()));
}

bool FindWidgetPath(QWidget* current, QWidget* target, std::vector<int>* path)
{
  if (!current || !target)
  {
    return false;
  }
  if (current == target)
  {
    return true;
  }

  if (auto* splitter = qobject_cast<QSplitter*>(current))
  {
    for (int i = 0; i < splitter->count(); ++i)
    {
      path->push_back(i);
      if (FindWidgetPath(splitter->widget(i), target, path))
      {
        return true;
      }
      path->pop_back();
    }
  }
  return false;
}

bool FindDockPath(const PlotDocker* docker, DockWidget* dock_widget, int* container_index,
                  std::vector<int>* path)
{
  if (!docker || !dock_widget || !dock_widget->dockAreaWidget())
  {
    return false;
  }

  auto* target_container = dock_widget->dockAreaWidget()->dockContainer();
  const auto containers = docker->dockContainers();
  for (int i = 0; i < containers.size(); ++i)
  {
    if (containers[i] != target_container)
    {
      continue;
    }
    path->clear();
    *container_index = i;
    return FindWidgetPath(containers[i]->rootSplitter(), dock_widget->dockAreaWidget(), path);
  }
  return false;
}

cabana::pj_layout::LayoutNode* FindLayoutNode(cabana::pj_layout::LayoutNode* node,
                                              const std::vector<int>& path, size_t depth = 0)
{
  if (!node)
  {
    return nullptr;
  }
  if (depth == path.size())
  {
    return node;
  }
  if (node->kind != cabana::pj_layout::LayoutNode::Kind::Splitter)
  {
    return nullptr;
  }
  const int index = path[depth];
  if (index < 0 || index >= static_cast<int>(node->children.size()))
  {
    return nullptr;
  }
  return FindLayoutNode(&node->children[index], path, depth + 1);
}

bool RenameLayoutNode(cabana::pj_layout::LayoutNode* root, const std::vector<int>& path,
                      const QString& title)
{
  auto* node = FindLayoutNode(root, path);
  if (!node || node->kind != cabana::pj_layout::LayoutNode::Kind::DockArea || title.isEmpty())
  {
    return false;
  }
  node->name = title;
  return true;
}

bool InsertSplitNode(cabana::pj_layout::LayoutNode* root, const std::vector<int>& path,
                     Qt::Orientation orientation)
{
  if (!root)
  {
    return false;
  }

  cabana::pj_layout::LayoutNode new_leaf = MakeDefaultDockAreaNode();
  if (path.empty())
  {
    cabana::pj_layout::LayoutNode existing = *root;
    root->kind = cabana::pj_layout::LayoutNode::Kind::Splitter;
    root->orientation = orientation;
    root->name.clear();
    root->plots.clear();
    root->children = { std::move(existing), std::move(new_leaf) };
    NormalizeSplitterSizes(root);
    return true;
  }

  std::vector<int> parent_path(path.begin(), path.end() - 1);
  const int target_index = path.back();
  auto* parent = FindLayoutNode(root, parent_path);
  if (!parent || parent->kind != cabana::pj_layout::LayoutNode::Kind::Splitter ||
      target_index < 0 || target_index >= static_cast<int>(parent->children.size()))
  {
    return false;
  }

  if (parent->orientation == orientation)
  {
    parent->children.insert(parent->children.begin() + target_index + 1, std::move(new_leaf));
    NormalizeSplitterSizes(parent);
    return true;
  }

  cabana::pj_layout::LayoutNode wrapped_existing = std::move(parent->children[target_index]);
  parent->children[target_index].kind = cabana::pj_layout::LayoutNode::Kind::Splitter;
  parent->children[target_index].orientation = orientation;
  parent->children[target_index].name.clear();
  parent->children[target_index].plots.clear();
  parent->children[target_index].children = { std::move(wrapped_existing), std::move(new_leaf) };
  NormalizeSplitterSizes(&parent->children[target_index]);
  return true;
}

bool RemoveLayoutNode(cabana::pj_layout::LayoutNode* node, const std::vector<int>& path,
                      size_t depth = 0)
{
  if (!node)
  {
    return false;
  }
  if (depth == path.size())
  {
    *node = MakeDefaultDockAreaNode();
    return true;
  }
  if (node->kind != cabana::pj_layout::LayoutNode::Kind::Splitter)
  {
    return false;
  }

  const int index = path[depth];
  if (index < 0 || index >= static_cast<int>(node->children.size()))
  {
    return false;
  }

  if (depth + 1 == path.size())
  {
    node->children.erase(node->children.begin() + index);
  }
  else if (!RemoveLayoutNode(&node->children[index], path, depth + 1))
  {
    return false;
  }

  if (node->children.empty())
  {
    *node = MakeDefaultDockAreaNode();
    return true;
  }
  if (node->children.size() == 1)
  {
    *node = std::move(node->children.front());
    return true;
  }

  NormalizeSplitterSizes(node);
  return true;
}

cabana::pj_layout::LayoutNode SaveLayoutNodeModel(QWidget* widget)
{
  cabana::pj_layout::LayoutNode node;

  if (auto* splitter = qobject_cast<QSplitter*>(widget))
  {
    node.kind = cabana::pj_layout::LayoutNode::Kind::Splitter;
    node.orientation = splitter->orientation();

    int total_size = 0;
    for (int size : splitter->sizes())
    {
      total_size += size;
    }
    for (int size : splitter->sizes())
    {
      node.sizes.push_back(total_size > 0 ? double(size) / double(total_size) : 0.0);
    }
    for (int i = 0; i < splitter->count(); ++i)
    {
      node.children.push_back(SaveLayoutNodeModel(splitter->widget(i)));
    }
    return node;
  }

  if (auto* dock_area = qobject_cast<ads::CDockAreaWidget*>(widget))
  {
    node.kind = cabana::pj_layout::LayoutNode::Kind::DockArea;
    for (int i = 0; i < dock_area->dockWidgetsCount(); ++i)
    {
      auto* dock_widget = dynamic_cast<DockWidget*>(dock_area->dockWidget(i));
      if (dock_widget)
      {
        node.name = dock_widget->title();
        node.plots.push_back(dock_widget->plotWidget()->savePlotModel());
      }
    }
  }
  return node;
}

void RestoreLayoutNodeModel(const cabana::pj_layout::LayoutNode& node, DockWidget* widget)
{
  if (node.kind == cabana::pj_layout::LayoutNode::Kind::DockArea)
  {
    if (!node.plots.empty())
    {
      widget->plotWidget()->loadPlotModel(node.plots.front());
    }
    if (!node.name.isEmpty())
    {
      widget->setTitle(node.name);
    }
    return;
  }

  const int splitter_count = static_cast<int>(node.children.size());
  if (splitter_count == 0)
  {
    return;
  }

  std::vector<DockWidget*> widgets(splitter_count);
  widgets[0] = widget;
  PlotDocker* docker = static_cast<PlotDocker*>(widget->dockManager());
  for (int i = 1; i < splitter_count; ++i)
  {
    widget = docker->splitDockWidget(widget, node.orientation);
    widgets[i] = widget;
  }

  int total_size = 0;
  for (int i = 0; i < splitter_count; ++i)
  {
    total_size += (node.orientation == Qt::Horizontal) ? widgets[i]->width() : widgets[i]->height();
  }
  if (total_size <= 0)
  {
    total_size = 1;
  }

  QList<int> sizes;
  for (double size : node.sizes)
  {
    sizes.push_back(static_cast<int>(size * total_size));
  }
  while (sizes.size() < splitter_count)
  {
    sizes.push_back(total_size / splitter_count);
  }

  if (auto* splitter = ads::internal::findParent<ads::CDockSplitter*>(widget))
  {
    splitter->setSizes(sizes);
  }

  for (int i = 0; i < splitter_count; ++i)
  {
    RestoreLayoutNodeModel(node.children[i], widgets[i]);
  }
}

}  // namespace

class SplittableComponentsFactory : public ads::CDockComponentsFactory
{
public:
  ads::CDockAreaTitleBar*
  createDockAreaTitleBar(ads::CDockAreaWidget* dock_area) const override
  {
    auto title_bar = new ads::CDockAreaTitleBar(dock_area);
    title_bar->setVisible(false);
    return title_bar;
  }
};

PlotDocker::PlotDocker(QString name, PlotDataMapRef& datamap,
                       cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup,
                       QWidget* parent)
  : ads::CDockManager(parent), _name(name), _datamap(datamap),
    _snapshot_lookup(std::move(snapshot_lookup))
{
  ads::CDockComponentsFactory::setFactory(new SplittableComponentsFactory());

  auto CreateFirstWidget = [&]() {
    if (dockAreaCount() == 0)
    {
      DockWidget* widget = createDockWidget();
      auto area = addDockWidget(ads::TopDockWidgetArea, widget);
      area->setAllowedAreas(ads::OuterDockAreas);
    }
  };

  connect(this, &ads::CDockManager::dockWidgetRemoved, this, CreateFirstWidget);

  connect(this, &ads::CDockManager::dockAreasAdded, this, &PlotDocker::undoableChange);

  CreateFirstWidget();
}

PlotDocker::~PlotDocker()
{
}

QString PlotDocker::name() const
{
  return _name;
}

void PlotDocker::setName(QString name)
{
  _name = name;
}

cabana::pj_layout::TabModel PlotDocker::saveStateModel() const
{
  cabana::pj_layout::TabModel tab_model;
  tab_model.tab_name = _name;
  for (CDockContainerWidget* container : dockContainers())
  {
    cabana::pj_layout::ContainerModel container_model;
    container_model.has_root = true;
    container_model.root = SaveLayoutNodeModel(container->rootSplitter());
    tab_model.containers.push_back(std::move(container_model));
  }
  return tab_model;
}

bool PlotDocker::loadStateModel(const cabana::pj_layout::TabModel& tab_model)
{
  if (!isHidden())
  {
    hide();
  }

  _name = tab_model.tab_name;

  for (const auto& container_model : tab_model.containers)
  {
    if (!container_model.has_root)
    {
      continue;
    }
    auto* widget = dynamic_cast<DockWidget*>(dockArea(0)->currentDockWidget());
    if (widget)
    {
      RestoreLayoutNodeModel(container_model.root, widget);
    }
  }

  if (isHidden())
  {
    show();
  }
  return true;
}

QDomElement saveChildNodesState(QDomDocument& doc, QWidget* widget)
{
  QSplitter* splitter = qobject_cast<QSplitter*>(widget);
  if (splitter)
  {
    QDomElement splitter_elem = doc.createElement("DockSplitter");
    splitter_elem.setAttribute("orientation",
                               (splitter->orientation() == Qt::Horizontal) ? "|" : "-");
    splitter_elem.setAttribute("count", QString::number(splitter->count()));

    QString sizes_str;
    int total_size = 0;
    for (int size : splitter->sizes())
    {
      total_size += size;
    }
    for (int size : splitter->sizes())
    {
      sizes_str += QString::number(double(size) / double(total_size));
      sizes_str += ";";
    }
    sizes_str.resize(sizes_str.size() - 1);
    splitter_elem.setAttribute("sizes", sizes_str);

    for (int i = 0; i < splitter->count(); ++i)
    {
      auto child = saveChildNodesState(doc, splitter->widget(i));
      splitter_elem.appendChild(child);
    }
    return splitter_elem;
  }
  else
  {
    ads::CDockAreaWidget* dockArea = qobject_cast<ads::CDockAreaWidget*>(widget);
    if (dockArea)
    {
      QDomElement area_elem = doc.createElement("DockArea");
      for (int i = 0; i < dockArea->dockWidgetsCount(); ++i)
      {
        auto dock_widget = dynamic_cast<DockWidget*>(dockArea->dockWidget(i));
        if (dock_widget)
        {
          auto plotwidget_elem = dock_widget->plotWidget()->xmlSaveState(doc);
          area_elem.appendChild(plotwidget_elem);
          area_elem.setAttribute("name", dock_widget->title());
        }
      }
      return area_elem;
    }
  }
  return {};
}

QDomElement PlotDocker::xmlSaveState(QDomDocument& doc) const
{
  QDomElement containers_elem = doc.createElement("Tab");

  containers_elem.setAttribute("containers", dockContainers().count());

  for (CDockContainerWidget* container : dockContainers())
  {
    QDomElement elem = doc.createElement("Container");
    auto child = saveChildNodesState(doc, container->rootSplitter());
    elem.appendChild(child);
    containers_elem.appendChild(elem);
  }
  return containers_elem;
}

void PlotDocker::restoreSplitter(QDomElement elem, DockWidget* widget)
{
  QString orientation_str = elem.attribute("orientation");
  int splitter_count = elem.attribute("count").toInt();

  // Check if the orientation string is right
  if (!orientation_str.startsWith("|") && !orientation_str.startsWith("-"))
  {
    return;
  }

  Qt::Orientation orientation =
      orientation_str.startsWith("|") ? Qt::Horizontal : Qt::Vertical;

  std::vector<DockWidget*> widgets(splitter_count);

  widgets[0] = widget;
  for (int i = 1; i < splitter_count; i++)
  {
    widget = (orientation == Qt::Horizontal) ? widget->splitHorizontal() :
                                               widget->splitVertical();
    widgets[i] = widget;
  }

  int tot_size = 0;

  for (int i = 0; i < splitter_count; i++)
  {
    tot_size +=
        (orientation == Qt::Horizontal) ? widgets[i]->width() : widgets[i]->height();
  }

  auto sizes_str = elem.attribute("sizes").splitRef(";", Qt::SkipEmptyParts);
  QList<int> sizes;

  for (int i = 0; i < splitter_count; i++)
  {
    sizes.push_back(static_cast<int>(sizes_str[i].toDouble() * tot_size));
  }

  auto splitter = ads::internal::findParent<ads::CDockSplitter*>(widget);
  splitter->setSizes(sizes);

  int index = 0;

  QDomElement child_elem = elem.firstChildElement();
  while (child_elem.isNull() == false)
  {
    if (child_elem.tagName() == "DockArea")
    {
      auto plot_elem = child_elem.firstChildElement("plot");
      widgets[index]->plotWidget()->xmlLoadState(plot_elem);
      if (child_elem.hasAttribute("name"))
      {
        QString area_name = child_elem.attribute("name");
        widgets[index]->setTitle(area_name);
      }
      index++;
    }
    else if (child_elem.tagName() == "DockSplitter")
    {
      restoreSplitter(child_elem, widgets[index++]);
    }
    else
    {
      return;
    }

    child_elem = child_elem.nextSiblingElement();
  }
};

bool PlotDocker::xmlLoadState(QDomElement& tab_element)
{
  if (!isHidden())
  {
    hide();
  }

  for (auto container_elem = tab_element.firstChildElement("Container");
       !container_elem.isNull(); container_elem = container_elem.nextSiblingElement("Cont"
                                                                                    "aine"
                                                                                    "r"))
  {
    auto splitter_elem = container_elem.firstChildElement("DockSplitter");
    if (!splitter_elem.isNull())
    {
      auto widget = dynamic_cast<DockWidget*>(dockArea(0)->currentDockWidget());
      restoreSplitter(splitter_elem, widget);
    }
  }

  if (isHidden())
  {
    show();
  }
  return true;
}

int PlotDocker::plotCount() const
{
  return dockAreaCount();
}

PlotWidget* PlotDocker::plotAt(int index)
{
  DockWidget* dock_widget =
      dynamic_cast<DockWidget*>(dockArea(index)->currentDockWidget());
  return static_cast<PlotWidget*>(dock_widget->plotWidget());
}

DockWidget* PlotDocker::createDockWidget()
{
  auto* widget = new DockWidget(_datamap, _snapshot_lookup, this);
  connect(widget, &DockWidget::undoableChange, this, &PlotDocker::undoableChange);
  plotWidgetAdded(widget->plotWidget());
  return widget;
}

DockWidget* PlotDocker::splitDockWidget(DockWidget* source, Qt::Orientation orientation)
{
  if (!source)
  {
    return nullptr;
  }

  auto* new_widget = createDockWidget();
  auto area = addDockWidget((orientation == Qt::Horizontal) ? ads::RightDockWidgetArea :
                                                          ads::BottomDockWidgetArea,
                            new_widget, source->dockAreaWidget());
  area->setAllowedAreas(ads::OuterDockAreas);
  emit undoableChange();
  return new_widget;
}

bool PlotDocker::requestSplitDockWidget(DockWidget* source, Qt::Orientation orientation)
{
  auto* parent_tabs = qobject_cast<TabbedPlotWidget*>(parentWidget());
  int container_index = -1;
  std::vector<int> path;
  if (!parent_tabs || !FindDockPath(this, source, &container_index, &path))
  {
    return false;
  }

  return parent_tabs->applyTabModelEdit(this, [&](cabana::pj_layout::TabModel* tab_model) {
    if (container_index < 0 ||
        container_index >= static_cast<int>(tab_model->containers.size()) ||
        !tab_model->containers[container_index].has_root)
    {
      return false;
    }
    return InsertSplitNode(&tab_model->containers[container_index].root, path, orientation);
  });
}

bool PlotDocker::closeDockWidget(DockWidget* dock_widget)
{
  if (!dock_widget)
  {
    return false;
  }

  if (auto* area = dock_widget->dockAreaWidget())
  {
    area->closeArea();
  }

  if (auto* plot_widget = dock_widget->takePlotWidget())
  {
    plot_widget->deleteLater();
  }

  emit undoableChange();
  return true;
}

bool PlotDocker::requestCloseDockWidget(DockWidget* dock_widget)
{
  auto* parent_tabs = qobject_cast<TabbedPlotWidget*>(parentWidget());
  int container_index = -1;
  std::vector<int> path;
  if (!parent_tabs || !FindDockPath(this, dock_widget, &container_index, &path))
  {
    return false;
  }

  return parent_tabs->applyTabModelEdit(this, [&](cabana::pj_layout::TabModel* tab_model) {
    if (container_index < 0 ||
        container_index >= static_cast<int>(tab_model->containers.size()) ||
        !tab_model->containers[container_index].has_root)
    {
      return false;
    }
    return RemoveLayoutNode(&tab_model->containers[container_index].root, path);
  });
}

void PlotDocker::setDockTitle(DockWidget* dock_widget, const QString& title)
{
  if (!dock_widget || title.isEmpty())
  {
    return;
  }
  dock_widget->setTitle(title);
  emit undoableChange();
}

bool PlotDocker::requestDockTitleChange(DockWidget* dock_widget, const QString& title)
{
  auto* parent_tabs = qobject_cast<TabbedPlotWidget*>(parentWidget());
  int container_index = -1;
  std::vector<int> path;
  if (!parent_tabs || !FindDockPath(this, dock_widget, &container_index, &path))
  {
    return false;
  }

  return parent_tabs->applyTabModelEdit(this, [&](cabana::pj_layout::TabModel* tab_model) {
    if (container_index < 0 ||
        container_index >= static_cast<int>(tab_model->containers.size()) ||
        !tab_model->containers[container_index].has_root)
    {
      return false;
    }
    return RenameLayoutNode(&tab_model->containers[container_index].root, path, title);
  });
}

void PlotDocker::toggleDockFullscreen(DockWidget* dock_widget)
{
  if (!dock_widget)
  {
    return;
  }

  dock_widget->toolBar()->toggleFullscreen();
  bool fullscreen = dock_widget->toolBar()->isFullscreen();

  for (int i = 0; i < dockAreaCount(); i++)
  {
    auto area = dockArea(i);
    if (area != dock_widget->dockAreaWidget())
    {
      area->setVisible(!fullscreen);
    }
    dock_widget->toolBar()->buttonClose()->setHidden(fullscreen);
  }
}

void PlotDocker::setHorizontalLink(bool enabled)
{
  // TODO
}

void PlotDocker::zoomOut()
{
  for (int index = 0; index < plotCount(); index++)
  {
    plotAt(index)->zoomOut(false);  // TODO is it false?
  }
}

void PlotDocker::replot()
{
  for (int index = 0; index < plotCount(); index++)
  {
    plotAt(index)->replot();
  }
}

void PlotDocker::on_stylesheetChanged(QString theme)
{
  for (int index = 0; index < plotCount(); index++)
  {
    auto dock_widget = static_cast<DockWidget*>(dockArea(index)->currentDockWidget());
    dock_widget->toolBar()->on_stylesheetChanged(theme);
  }
}

DockWidget::DockWidget(PlotDataMapRef& datamap,
                       cabana::pj_engine::SeriesSnapshotLookup snapshot_lookup,
                       QWidget* parent)
  : ads::CDockWidget("Plot", parent), _datamap(datamap),
    _snapshot_lookup(std::move(snapshot_lookup))
{
  setFrameShape(QFrame::NoFrame);

  static int plot_count = 0;
  QString plot_name = QString("_plot_%1_").arg(plot_count++);
  _plot_widget = new PlotWidget(datamap, this, _snapshot_lookup);
  setWidget(_plot_widget);
  setFeature(ads::CDockWidget::DockWidgetFloatable, false);
  setFeature(ads::CDockWidget::DockWidgetDeleteOnClose, true);

  _toolbar = new DockToolbar(this);
  qobject_cast<QBoxLayout*>(layout())->insertWidget(0, _toolbar);
  setTitle("...");

  connect(_toolbar->buttonSplitHorizontal(), &QPushButton::clicked, this,
          [this]() {
            if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
            {
              parent_docker->requestSplitDockWidget(this, Qt::Horizontal);
            }
          });

  connect(_toolbar->buttonSplitVertical(), &QPushButton::clicked, this,
          [this]() {
            if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
            {
              parent_docker->requestSplitDockWidget(this, Qt::Vertical);
            }
          });

  connect(_plot_widget, &PlotWidget::splitHorizontal, this, [this]() {
    if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
    {
      parent_docker->requestSplitDockWidget(this, Qt::Horizontal);
    }
  });

  connect(_plot_widget, &PlotWidget::splitVertical, this, [this]() {
    if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
    {
      parent_docker->requestSplitDockWidget(this, Qt::Vertical);
    }
  });

  connect(_toolbar, &DockToolbar::titleChanged, this, [this](const QString& title) {
    if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
    {
      parent_docker->requestDockTitleChange(this, title);
    }
  });

  connect(_toolbar->buttonFullscreen(), &QPushButton::clicked, this, [this]() {
    if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
    {
      parent_docker->toggleDockFullscreen(this);
    }
  });

  connect(_toolbar->buttonClose(), &QPushButton::pressed, this, [this]() {
    if (auto* parent_docker = static_cast<PlotDocker*>(dockManager()))
    {
      parent_docker->requestCloseDockWidget(this);
    }
  });

  this->layout()->setMargin(10);
}

DockWidget::~DockWidget()
{
}

DockWidget* DockWidget::splitHorizontal()
{
  auto* parent_docker = static_cast<PlotDocker*>(dockManager());
  return parent_docker ? parent_docker->splitDockWidget(this, Qt::Horizontal) : nullptr;
}

DockWidget* DockWidget::splitVertical()
{
  auto* parent_docker = static_cast<PlotDocker*>(dockManager());
  return parent_docker ? parent_docker->splitDockWidget(this, Qt::Vertical) : nullptr;
}

PlotWidget* DockWidget::plotWidget()
{
  return _plot_widget;
}

PlotWidget* DockWidget::takePlotWidget()
{
  PlotWidget* plot_widget = _plot_widget;
  takeWidget();
  _plot_widget = nullptr;
  return plot_widget;
}

DockToolbar* DockWidget::toolBar()
{
  return _toolbar;
}

QString DockWidget::title() const
{
  return _toolbar ? _toolbar->label()->text() : QString{};
}

void DockWidget::setTitle(const QString& title)
{
  if (_toolbar)
  {
    _toolbar->label()->setText(title);
  }
  if (_plot_widget)
  {
    _plot_widget->setStatisticsTitle(title);
  }
}
