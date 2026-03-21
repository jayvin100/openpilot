#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace jotpluggler {

struct PlotRange {
  bool valid = false;
  double left = 0.0;
  double right = 0.0;
  double bottom = 0.0;
  double top = 1.0;
  bool has_y_limit_min = false;
  bool has_y_limit_max = false;
  double y_limit_min = 0.0;
  double y_limit_max = 1.0;
};

struct Curve {
  std::string name;
  std::string label;
  std::array<uint8_t, 3> color = {160, 170, 180};
  bool visible = true;
  bool derivative = false;
  double value_scale = 1.0;
  double value_offset = 0.0;
  std::vector<double> xs;
  std::vector<double> ys;
};

struct Pane {
  std::string title;
  PlotRange range;
  std::vector<Curve> curves;
};

enum class SplitOrientation {
  Horizontal,
  Vertical,
};

struct WorkspaceNode {
  bool is_pane = false;
  int pane_index = -1;
  SplitOrientation orientation = SplitOrientation::Horizontal;
  std::vector<float> sizes;
  std::vector<WorkspaceNode> children;
};

struct WorkspaceTab {
  std::string tab_name;
  WorkspaceNode root;
  std::vector<Pane> panes;
};

struct RouteSeries {
  std::string path;
  std::vector<double> times;
  std::vector<double> values;
};

struct RouteData {
  std::vector<RouteSeries> series;
  std::vector<std::string> paths;
  std::vector<std::string> roots;
  bool has_time_range = false;
  double x_min = 0.0;
  double x_max = 1.0;
};

struct SketchLayout {
  std::vector<WorkspaceTab> tabs;
  std::vector<std::string> roots;
  int current_tab_index = 0;
};

enum class RouteLoadStage {
  Resolving,
  DownloadingSegment,
  ParsingSegment,
  Finished,
};

struct RouteLoadProgress {
  RouteLoadStage stage = RouteLoadStage::Resolving;
  size_t segment_index = 0;
  size_t segment_count = 0;
  uint64_t current = 0;
  uint64_t total = 0;
  std::string segment_name;
};

using RouteLoadProgressCallback = std::function<void(const RouteLoadProgress &)>;

SketchLayout load_sketch_layout(const std::filesystem::path &layout_path);
RouteData load_route_data(const std::string &route_name,
                          const std::string &data_dir = {},
                          const RouteLoadProgressCallback &progress = {});

}  // namespace jotpluggler
