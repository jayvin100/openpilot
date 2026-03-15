#include "tools/cabana/pj_layout/layout_model.h"

#include <QDomNamedNodeMap>
#include <QFile>

#include <algorithm>
#include <cmath>

namespace cabana {
namespace pj_layout {

namespace {

constexpr double kDoubleTolerance = 1e-9;

bool SameDouble(double lhs, double rhs) {
  return std::fabs(lhs - rhs) <= kDoubleTolerance;
}

QString FormatDouble(double value) {
  return QString::number(value, 'g', 17);
}

bool ParseBoolAttribute(const QString &value) {
  const QString normalized = value.trimmed().toLower();
  return normalized == "1" || normalized == "true" || normalized == "yes";
}

QString AttributeOr(const QDomElement &element, const QString &name, const QString &fallback = {}) {
  return element.hasAttribute(name) ? element.attribute(name) : fallback;
}

std::vector<XmlAttribute> ParseAttributes(const QDomElement &element) {
  std::vector<XmlAttribute> attributes;
  const QDomNamedNodeMap dom_attributes = element.attributes();
  attributes.reserve(dom_attributes.count());
  for (int i = 0; i < dom_attributes.count(); ++i) {
    const QDomNode attribute = dom_attributes.item(i);
    attributes.push_back({attribute.nodeName(), attribute.nodeValue()});
  }
  return attributes;
}

XmlElement BuildXmlElement(const QDomElement &element) {
  XmlElement out;
  out.tag_name = element.tagName();
  out.attributes = ParseAttributes(element);
  out.text = element.text();
  for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
    out.children.push_back(BuildXmlElement(child));
  }
  return out;
}

void ApplyAttributes(const std::vector<XmlAttribute> &attributes, QDomElement *element) {
  for (const auto &attribute : attributes) {
    element->setAttribute(attribute.name, attribute.value);
  }
}

QDomElement BuildDomElement(const XmlElement &element, QDomDocument *document) {
  QDomElement out = document->createElement(element.tag_name);
  ApplyAttributes(element.attributes, &out);
  if (!element.text.isEmpty()) {
    out.appendChild(document->createTextNode(element.text));
  }
  for (const auto &child : element.children) {
    out.appendChild(BuildDomElement(child, document));
  }
  return out;
}

SnippetModel ParseSnippet(const QDomElement &element) {
  SnippetModel snippet;
  snippet.name = element.attribute("name");
  snippet.global_vars = element.firstChildElement("global").text().trimmed();
  snippet.function = element.firstChildElement("function").text().trimmed();
  snippet.linked_source = element.firstChildElement("linked_source").text().trimmed();

  const QDomElement additional_sources = element.firstChildElement("additional_sources");
  if (!additional_sources.isNull()) {
    for (int index = 1;; ++index) {
      const QString tag_name = QString("v%1").arg(index);
      const QDomElement source = additional_sources.firstChildElement(tag_name);
      if (source.isNull()) {
        break;
      }
      snippet.additional_sources.push_back(source.text());
    }
  }
  return snippet;
}

QDomElement SaveSnippet(const SnippetModel &snippet, QDomDocument *document) {
  QDomElement element = document->createElement("snippet");
  element.setAttribute("name", snippet.name);

  QDomElement global = document->createElement("global");
  global.appendChild(document->createTextNode(snippet.global_vars));
  element.appendChild(global);

  QDomElement function = document->createElement("function");
  function.appendChild(document->createTextNode(snippet.function));
  element.appendChild(function);

  QDomElement linked_source = document->createElement("linked_source");
  linked_source.appendChild(document->createTextNode(snippet.linked_source));
  element.appendChild(linked_source);

  if (!snippet.additional_sources.isEmpty()) {
    QDomElement additional_sources = document->createElement("additional_sources");
    for (int index = 0; index < snippet.additional_sources.size(); ++index) {
      QDomElement source = document->createElement(QString("v%1").arg(index + 1));
      source.appendChild(document->createTextNode(snippet.additional_sources[index]));
      additional_sources.appendChild(source);
    }
    element.appendChild(additional_sources);
  }

  return element;
}

TransformConfig ParseTransform(const QDomElement &element) {
  TransformConfig transform;
  transform.name = element.attribute("name");
  transform.alias = element.attribute("alias");
  for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
    transform.children.push_back(BuildXmlElement(child));
  }
  return transform;
}

QDomElement SaveTransform(const TransformConfig &transform, QDomDocument *document) {
  QDomElement element = document->createElement("transform");
  element.setAttribute("name", transform.name);
  element.setAttribute("alias", transform.alias);
  for (const auto &child : transform.children) {
    element.appendChild(BuildDomElement(child, document));
  }
  return element;
}

CurveBinding ParseCurve(const QDomElement &element) {
  CurveBinding curve;
  curve.name = element.attribute("name");
  curve.color = element.attribute("color");
  curve.curve_x = element.attribute("curve_x");
  curve.curve_y = element.attribute("curve_y");
  const QDomElement transform = element.firstChildElement("transform");
  if (!transform.isNull()) {
    curve.has_transform = true;
    curve.transform = ParseTransform(transform);
  }
  return curve;
}

QDomElement SaveCurve(const CurveBinding &curve, QDomDocument *document) {
  QDomElement element = document->createElement("curve");
  element.setAttribute("name", curve.name);
  element.setAttribute("color", curve.color);
  if (!curve.curve_x.isEmpty()) {
    element.setAttribute("curve_x", curve.curve_x);
  }
  if (!curve.curve_y.isEmpty()) {
    element.setAttribute("curve_y", curve.curve_y);
  }
  if (curve.has_transform) {
    element.appendChild(SaveTransform(curve.transform, document));
  }
  return element;
}

PlotModel ParsePlot(const QDomElement &element) {
  PlotModel plot;
  plot.style = AttributeOr(element, "style", "Lines");
  plot.mode = AttributeOr(element, "mode", "TimeSeries");
  plot.flip_x = ParseBoolAttribute(AttributeOr(element, "flip_x", "false"));
  plot.flip_y = ParseBoolAttribute(AttributeOr(element, "flip_y", "false"));

  const QDomElement range = element.firstChildElement("range");
  if (!range.isNull()) {
    plot.range.left = range.attribute("left").toDouble();
    plot.range.right = range.attribute("right").toDouble();
    plot.range.top = range.attribute("top").toDouble();
    plot.range.bottom = range.attribute("bottom").toDouble();
  }

  const QDomElement limit_y = element.firstChildElement("limitY");
  if (!limit_y.isNull()) {
    plot.has_limit_y = true;
    plot.limit_y.has_min = limit_y.hasAttribute("min");
    plot.limit_y.has_max = limit_y.hasAttribute("max");
    if (plot.limit_y.has_min) {
      plot.limit_y.min = limit_y.attribute("min").toDouble();
    }
    if (plot.limit_y.has_max) {
      plot.limit_y.max = limit_y.attribute("max").toDouble();
    }
  }

  for (QDomElement curve = element.firstChildElement("curve"); !curve.isNull(); curve = curve.nextSiblingElement("curve")) {
    plot.curves.push_back(ParseCurve(curve));
  }
  return plot;
}

QDomElement SavePlot(const PlotModel &plot, QDomDocument *document) {
  QDomElement element = document->createElement("plot");
  element.setAttribute("style", plot.style);
  element.setAttribute("mode", plot.mode);
  element.setAttribute("flip_y", plot.flip_y ? "true" : "false");
  element.setAttribute("flip_x", plot.flip_x ? "true" : "false");

  QDomElement range = document->createElement("range");
  range.setAttribute("left", FormatDouble(plot.range.left));
  range.setAttribute("right", FormatDouble(plot.range.right));
  range.setAttribute("top", FormatDouble(plot.range.top));
  range.setAttribute("bottom", FormatDouble(plot.range.bottom));
  element.appendChild(range);

  if (plot.has_limit_y) {
    QDomElement limit_y = document->createElement("limitY");
    if (plot.limit_y.has_min) {
      limit_y.setAttribute("min", FormatDouble(plot.limit_y.min));
    }
    if (plot.limit_y.has_max) {
      limit_y.setAttribute("max", FormatDouble(plot.limit_y.max));
    }
    element.appendChild(limit_y);
  }

  for (const auto &curve : plot.curves) {
    element.appendChild(SaveCurve(curve, document));
  }
  return element;
}

bool ParseLayoutNode(const QDomElement &element, LayoutNode *node, QString *error) {
  if (element.tagName() == "DockSplitter") {
    node->kind = LayoutNode::Kind::Splitter;
    node->orientation = element.attribute("orientation") == "|" ? Qt::Horizontal : Qt::Vertical;
    node->sizes.clear();
    const QStringList sizes = element.attribute("sizes").split(';', Qt::SkipEmptyParts);
    node->sizes.reserve(sizes.size());
    for (const QString &size : sizes) {
      node->sizes.push_back(size.toDouble());
    }
    node->children.clear();
    for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
      if (child.tagName() != "DockSplitter" && child.tagName() != "DockArea") {
        if (error) {
          *error = QString("Unsupported child element under DockSplitter: %1").arg(child.tagName());
        }
        return false;
      }
      LayoutNode child_node;
      if (!ParseLayoutNode(child, &child_node, error)) {
        return false;
      }
      node->children.push_back(child_node);
    }
    return true;
  }

  if (element.tagName() == "DockArea") {
    node->kind = LayoutNode::Kind::DockArea;
    node->name = element.attribute("name");
    node->plots.clear();
    node->children.clear();
    node->sizes.clear();
    for (QDomElement plot = element.firstChildElement("plot"); !plot.isNull(); plot = plot.nextSiblingElement("plot")) {
      node->plots.push_back(ParsePlot(plot));
    }
    return true;
  }

  if (error) {
    *error = QString("Unsupported layout node: %1").arg(element.tagName());
  }
  return false;
}

QDomElement SaveLayoutNode(const LayoutNode &node, QDomDocument *document) {
  if (node.kind == LayoutNode::Kind::DockArea) {
    QDomElement element = document->createElement("DockArea");
    if (!node.name.isEmpty()) {
      element.setAttribute("name", node.name);
    }
    for (const auto &plot : node.plots) {
      element.appendChild(SavePlot(plot, document));
    }
    return element;
  }

  QDomElement element = document->createElement("DockSplitter");
  element.setAttribute("orientation", node.orientation == Qt::Horizontal ? "|" : "-");
  element.setAttribute("count", QString::number(node.children.size()));

  QStringList sizes;
  for (double size : node.sizes) {
    sizes.push_back(FormatDouble(size));
  }
  element.setAttribute("sizes", sizes.join(';'));

  for (const auto &child : node.children) {
    element.appendChild(SaveLayoutNode(child, document));
  }
  return element;
}

void CollectStats(const LayoutNode &node, LayoutStats *stats) {
  if (node.kind == LayoutNode::Kind::Splitter) {
    stats->splitters += 1;
    for (const auto &child : node.children) {
      CollectStats(child, stats);
    }
    return;
  }

  stats->plots += static_cast<int>(node.plots.size());
  for (const auto &plot : node.plots) {
    if (plot.mode == "XYPlot") {
      stats->xy_plots += 1;
    } else {
      stats->timeseries_plots += 1;
    }
  }
}

void CollectTransforms(const LayoutNode &node, QStringList *transforms) {
  if (node.kind == LayoutNode::Kind::Splitter) {
    for (const auto &child : node.children) {
      CollectTransforms(child, transforms);
    }
    return;
  }

  for (const auto &plot : node.plots) {
    for (const auto &curve : plot.curves) {
      if (curve.has_transform && !curve.transform.name.isEmpty() &&
          !transforms->contains(curve.transform.name)) {
        transforms->push_back(curve.transform.name);
      }
    }
  }
}

bool ParseLayoutDocument(const QDomDocument &document, LayoutModel *layout, QString *error) {
  QDomElement root = document.documentElement();
  if (root.isNull() || root.tagName() != "root") {
    if (error) {
      *error = "Layout XML is missing a <root> element";
    }
    return false;
  }

  LayoutModel out;
  out.version = root.attribute("version");

  for (QDomElement child = root.firstChildElement(); !child.isNull(); child = child.nextSiblingElement()) {
    if (child.tagName() == "tabbed_widget") {
      TabbedWidgetModel widget;
      widget.name = child.attribute("name");
      widget.parent = child.attribute("parent");

      for (QDomElement tab_child = child.firstChildElement(); !tab_child.isNull();
           tab_child = tab_child.nextSiblingElement()) {
        if (tab_child.tagName() == "Tab") {
          TabModel tab;
          tab.tab_name = tab_child.attribute("tab_name");

          for (QDomElement container = tab_child.firstChildElement("Container");
               !container.isNull(); container = container.nextSiblingElement("Container")) {
            ContainerModel container_model;
            QDomElement root_node = container.firstChildElement();
            if (!root_node.isNull()) {
              container_model.has_root = true;
              if (!ParseLayoutNode(root_node, &container_model.root, error)) {
                return false;
              }
            }
            tab.containers.push_back(container_model);
          }
          widget.tabs.push_back(tab);
        } else if (tab_child.tagName() == "currentTabIndex") {
          widget.current_tab_index = tab_child.attribute("index").toInt();
        }
      }

      out.tabbed_widgets.push_back(widget);
    } else if (child.tagName() == "use_relative_time_offset") {
      out.has_relative_time_offset = true;
      out.use_relative_time_offset = ParseBoolAttribute(child.attribute("enabled"));
    } else if (child.tagName() == "Plugins") {
      out.plugins_present = true;
      for (QDomElement plugin = child.firstChildElement("plugin"); !plugin.isNull();
           plugin = plugin.nextSiblingElement("plugin")) {
        PluginState state;
        state.id = plugin.attribute("ID");
        for (QDomElement plugin_child = plugin.firstChildElement(); !plugin_child.isNull();
             plugin_child = plugin_child.nextSiblingElement()) {
          state.children.push_back(BuildXmlElement(plugin_child));
        }
        out.plugins.push_back(state);
      }
    } else if (child.tagName() == "customMathEquations") {
      out.custom_math_present = true;
      for (QDomElement snippet = child.firstChildElement("snippet"); !snippet.isNull();
           snippet = snippet.nextSiblingElement("snippet")) {
        out.custom_math_snippets.push_back(ParseSnippet(snippet));
      }
    } else if (child.tagName() == "snippets") {
      out.snippets_present = true;
      for (QDomElement snippet = child.firstChildElement("snippet"); !snippet.isNull();
           snippet = snippet.nextSiblingElement("snippet")) {
        out.snippets.push_back(ParseSnippet(snippet));
      }
    }
  }

  *layout = out;
  return true;
}

}  // namespace

bool XmlAttribute::operator==(const XmlAttribute &other) const {
  return name == other.name && value == other.value;
}

bool XmlElement::operator==(const XmlElement &other) const {
  return tag_name == other.tag_name && attributes == other.attributes &&
         text == other.text && children == other.children;
}

bool TransformConfig::operator==(const TransformConfig &other) const {
  return name == other.name && alias == other.alias && children == other.children;
}

bool CurveBinding::operator==(const CurveBinding &other) const {
  return name == other.name && color == other.color && curve_x == other.curve_x &&
         curve_y == other.curve_y && has_transform == other.has_transform &&
         (!has_transform || transform == other.transform);
}

bool AxisRange::operator==(const AxisRange &other) const {
  return SameDouble(left, other.left) && SameDouble(right, other.right) &&
         SameDouble(top, other.top) && SameDouble(bottom, other.bottom);
}

bool AxisLimit::operator==(const AxisLimit &other) const {
  return has_min == other.has_min && has_max == other.has_max &&
         (!has_min || SameDouble(min, other.min)) &&
         (!has_max || SameDouble(max, other.max));
}

bool PlotModel::operator==(const PlotModel &other) const {
  return style == other.style && mode == other.mode && flip_x == other.flip_x &&
         flip_y == other.flip_y && range == other.range &&
         has_limit_y == other.has_limit_y &&
         (!has_limit_y || limit_y == other.limit_y) && curves == other.curves;
}

bool LayoutNode::operator==(const LayoutNode &other) const {
  if (kind != other.kind) {
    return false;
  }
  if (kind == Kind::DockArea) {
    return name == other.name && plots == other.plots;
  }
  if (orientation != other.orientation || children.size() != other.children.size() ||
      sizes.size() != other.sizes.size()) {
    return false;
  }
  for (size_t i = 0; i < sizes.size(); ++i) {
    if (!SameDouble(sizes[i], other.sizes[i])) {
      return false;
    }
  }
  return children == other.children;
}

bool ContainerModel::operator==(const ContainerModel &other) const {
  return has_root == other.has_root && (!has_root || root == other.root);
}

bool TabModel::operator==(const TabModel &other) const {
  return tab_name == other.tab_name && containers == other.containers;
}

bool TabbedWidgetModel::operator==(const TabbedWidgetModel &other) const {
  return name == other.name && parent == other.parent &&
         current_tab_index == other.current_tab_index && tabs == other.tabs;
}

bool SnippetModel::operator==(const SnippetModel &other) const {
  return name == other.name && global_vars == other.global_vars &&
         function == other.function && linked_source == other.linked_source &&
         additional_sources == other.additional_sources;
}

bool PluginState::operator==(const PluginState &other) const {
  return id == other.id && children == other.children;
}

bool LayoutModel::operator==(const LayoutModel &other) const {
  return version == other.version && tabbed_widgets == other.tabbed_widgets &&
         has_relative_time_offset == other.has_relative_time_offset &&
         (!has_relative_time_offset ||
          use_relative_time_offset == other.use_relative_time_offset) &&
         plugins_present == other.plugins_present && plugins == other.plugins &&
         custom_math_present == other.custom_math_present &&
         custom_math_snippets == other.custom_math_snippets &&
         snippets_present == other.snippets_present && snippets == other.snippets;
}

bool LoadLayoutFile(const QString &file_name, LayoutModel *layout, QString *error) {
  QFile file(file_name);
  if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
    if (error) {
      *error = file.errorString();
    }
    return false;
  }

  QDomDocument document;
  QString parse_error;
  int error_line = 0;
  int error_column = 0;
  if (!document.setContent(&file, true, &parse_error, &error_line, &error_column)) {
    if (error) {
      *error = QString("Parse error at line %1, column %2: %3")
                   .arg(error_line)
                   .arg(error_column)
                   .arg(parse_error);
    }
    return false;
  }
  return ParseLayoutDocument(document, layout, error);
}

bool ParseLayoutXml(const QString &xml_text, LayoutModel *layout, QString *error) {
  QDomDocument document;
  QString parse_error;
  int error_line = 0;
  int error_column = 0;
  if (!document.setContent(xml_text, true, &parse_error, &error_line, &error_column)) {
    if (error) {
      *error = QString("Parse error at line %1, column %2: %3")
                   .arg(error_line)
                   .arg(error_column)
                   .arg(parse_error);
    }
    return false;
  }
  return ParseLayoutDocument(document, layout, error);
}

QDomDocument ToXmlDocument(const LayoutModel &layout) {
  QDomDocument document;
  const QDomProcessingInstruction instruction =
      document.createProcessingInstruction("xml", "version='1.0' encoding='UTF-8'");
  document.appendChild(instruction);

  QDomElement root = document.createElement("root");
  if (!layout.version.isEmpty()) {
    root.setAttribute("version", layout.version);
  }

  for (const auto &tabbed_widget : layout.tabbed_widgets) {
    QDomElement widget = document.createElement("tabbed_widget");
    widget.setAttribute("name", tabbed_widget.name);
    widget.setAttribute("parent", tabbed_widget.parent);

    for (const auto &tab : tabbed_widget.tabs) {
      QDomElement tab_element = document.createElement("Tab");
      tab_element.setAttribute("containers", QString::number(tab.containers.size()));
      tab_element.setAttribute("tab_name", tab.tab_name);

      for (const auto &container : tab.containers) {
        QDomElement container_element = document.createElement("Container");
        if (container.has_root) {
          container_element.appendChild(SaveLayoutNode(container.root, &document));
        }
        tab_element.appendChild(container_element);
      }
      widget.appendChild(tab_element);
    }

    QDomElement current = document.createElement("currentTabIndex");
    current.setAttribute("index", QString::number(tabbed_widget.current_tab_index));
    widget.appendChild(current);
    root.appendChild(widget);
  }

  if (layout.has_relative_time_offset) {
    QDomElement relative_time = document.createElement("use_relative_time_offset");
    relative_time.setAttribute("enabled", layout.use_relative_time_offset ? "1" : "0");
    root.appendChild(relative_time);
  }

  if (layout.plugins_present) {
    QDomElement plugins = document.createElement("Plugins");
    for (const auto &plugin : layout.plugins) {
      QDomElement plugin_element = document.createElement("plugin");
      plugin_element.setAttribute("ID", plugin.id);
      for (const auto &child : plugin.children) {
        plugin_element.appendChild(ToDomElement(child, &document));
      }
      plugins.appendChild(plugin_element);
    }
    root.appendChild(plugins);
  }

  if (layout.custom_math_present) {
    QDomElement custom_math = document.createElement("customMathEquations");
    for (const auto &snippet : layout.custom_math_snippets) {
      custom_math.appendChild(SaveSnippet(snippet, &document));
    }
    root.appendChild(custom_math);
  }

  if (layout.snippets_present) {
    QDomElement snippets = document.createElement("snippets");
    for (const auto &snippet : layout.snippets) {
      snippets.appendChild(SaveSnippet(snippet, &document));
    }
    root.appendChild(snippets);
  }

  document.appendChild(root);
  return document;
}

QString ToXmlString(const LayoutModel &layout, int indent) {
  return ToXmlDocument(layout).toString(indent);
}

LayoutStats ComputeLayoutStats(const LayoutModel &layout) {
  LayoutStats stats;
  stats.custom_math_snippets = static_cast<int>(layout.custom_math_snippets.size());
  for (const auto &plugin : layout.plugins) {
    if (plugin.id == "Reactive Script Editor") {
      stats.reactive_script_editor = true;
      break;
    }
  }

  for (const auto &widget : layout.tabbed_widgets) {
    stats.tabs += static_cast<int>(widget.tabs.size());
    for (const auto &tab : widget.tabs) {
      for (const auto &container : tab.containers) {
        if (container.has_root) {
          CollectStats(container.root, &stats);
          CollectTransforms(container.root, &stats.transforms);
        }
      }
    }
  }
  std::sort(stats.transforms.begin(), stats.transforms.end());
  return stats;
}

XmlElement FromDomElement(const QDomElement &element) {
  return BuildXmlElement(element);
}

QDomElement ToDomElement(const XmlElement &element, QDomDocument *document) {
  return BuildDomElement(element, document);
}

}  // namespace pj_layout
}  // namespace cabana
