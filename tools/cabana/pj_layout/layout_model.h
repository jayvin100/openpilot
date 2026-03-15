#pragma once

#include <QString>
#include <QStringList>
#include <QDomDocument>
#include <Qt>

#include <vector>

namespace cabana {
namespace pj_layout {

struct XmlAttribute {
  QString name;
  QString value;

  bool operator==(const XmlAttribute &other) const;
};

struct XmlElement {
  QString tag_name;
  std::vector<XmlAttribute> attributes;
  QString text;
  std::vector<XmlElement> children;

  bool operator==(const XmlElement &other) const;
};

struct TransformConfig {
  QString name;
  QString alias;
  std::vector<XmlElement> children;

  bool operator==(const TransformConfig &other) const;
};

struct CurveBinding {
  QString name;
  QString color;
  QString curve_x;
  QString curve_y;
  bool has_transform = false;
  TransformConfig transform;

  bool operator==(const CurveBinding &other) const;
};

struct AxisRange {
  double left = 0.0;
  double right = 0.0;
  double top = 0.0;
  double bottom = 0.0;

  bool operator==(const AxisRange &other) const;
};

struct AxisLimit {
  bool has_min = false;
  double min = 0.0;
  bool has_max = false;
  double max = 0.0;

  bool operator==(const AxisLimit &other) const;
};

struct PlotModel {
  QString style = "Lines";
  QString mode = "TimeSeries";
  bool flip_x = false;
  bool flip_y = false;
  AxisRange range;
  bool has_limit_y = false;
  AxisLimit limit_y;
  std::vector<CurveBinding> curves;

  bool operator==(const PlotModel &other) const;
};

struct LayoutNode {
  enum class Kind {
    DockArea,
    Splitter,
  };

  Kind kind = Kind::DockArea;

  // DockArea
  QString name;
  std::vector<PlotModel> plots;

  // DockSplitter
  Qt::Orientation orientation = Qt::Horizontal;
  std::vector<double> sizes;
  std::vector<LayoutNode> children;

  bool operator==(const LayoutNode &other) const;
};

struct ContainerModel {
  bool has_root = false;
  LayoutNode root;

  bool operator==(const ContainerModel &other) const;
};

struct TabModel {
  QString tab_name;
  std::vector<ContainerModel> containers;

  bool operator==(const TabModel &other) const;
};

struct TabbedWidgetModel {
  QString name;
  QString parent;
  std::vector<TabModel> tabs;
  int current_tab_index = 0;

  bool operator==(const TabbedWidgetModel &other) const;
};

struct SnippetModel {
  QString name;
  QString global_vars;
  QString function;
  QString linked_source;
  QStringList additional_sources;

  bool operator==(const SnippetModel &other) const;
};

struct PluginState {
  QString id;
  std::vector<XmlElement> children;

  bool operator==(const PluginState &other) const;
};

struct LayoutModel {
  QString version;
  std::vector<TabbedWidgetModel> tabbed_widgets;
  bool has_relative_time_offset = false;
  bool use_relative_time_offset = false;
  bool plugins_present = false;
  std::vector<PluginState> plugins;
  bool custom_math_present = false;
  std::vector<SnippetModel> custom_math_snippets;
  bool snippets_present = false;
  std::vector<SnippetModel> snippets;

  bool operator==(const LayoutModel &other) const;
};

struct LayoutStats {
  int tabs = 0;
  int plots = 0;
  int splitters = 0;
  int xy_plots = 0;
  int timeseries_plots = 0;
  int custom_math_snippets = 0;
  bool reactive_script_editor = false;
  QStringList transforms;
};

bool LoadLayoutFile(const QString &file_name, LayoutModel *layout, QString *error = nullptr);
bool ParseLayoutXml(const QString &xml_text, LayoutModel *layout, QString *error = nullptr);
QDomDocument ToXmlDocument(const LayoutModel &layout);
QString ToXmlString(const LayoutModel &layout, int indent = 1);
LayoutStats ComputeLayoutStats(const LayoutModel &layout);
XmlElement FromDomElement(const QDomElement &element);
QDomElement ToDomElement(const XmlElement &element, QDomDocument *document);
bool ParsePlotElement(const QDomElement &element, PlotModel *plot, QString *error = nullptr);
QDomElement ToPlotDomElement(const PlotModel &plot, QDomDocument *document);

}  // namespace pj_layout
}  // namespace cabana
