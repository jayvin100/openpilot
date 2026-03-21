#pragma once

#include "tools/jotpluggler/app_internal.h"

#include <string>
#include <string_view>
#include <vector>

namespace jotpluggler {

void rebuild_route_index(AppSession *session);
void rebuild_browser_nodes(AppSession *session, UiState *state);
BrowserSeriesDisplayInfo compute_browser_display_info(const AppSession &session, const RouteSeries &series);
std::string format_display_value(double display_value,
                                 const BrowserSeriesDisplayInfo &display_info,
                                 const EnumInfo *enum_info);
std::vector<std::string> decode_browser_drag_payload(std::string_view payload);
void collect_visible_leaf_paths(const BrowserNode &node,
                                const std::string &filter,
                                std::vector<std::string> *out);
void draw_browser_node(AppSession *session,
                       const BrowserNode &node,
                       UiState *state,
                       const std::string &filter,
                       const std::vector<std::string> &visible_paths);

}  // namespace jotpluggler
