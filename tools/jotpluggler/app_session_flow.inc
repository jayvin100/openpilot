#include "tools/jotpluggler/app_stream_flow.inc"

const RouteSeries *find_route_series(const AppSession &session, const std::string &path) {
  auto it = session.series_by_path.find(path);
  return it == session.series_by_path.end() ? nullptr : it->second;
}

void apply_route_data(AppSession *session, UiState *state, RouteData route_data) {
  session->route_data = std::move(route_data);
  rebuild_route_index(session);
  rebuild_browser_nodes(session, state);
  refresh_all_custom_curves(session, state);
  if (session->camera_feed) {
    session->camera_feed->set_route_data(session->route_data);
  }
  state->has_shared_range = false;
  state->has_tracker_time = false;
  reset_shared_range(state, *session);
}

std::optional<std::pair<double, double>> tab_default_x_range(const WorkspaceTab &tab) {
  bool found = false;
  double min_value = 0.0;
  double max_value = 1.0;
  for (const Pane &pane : tab.panes) {
    if (!pane.range.valid || pane.range.right <= pane.range.left) {
      continue;
    }
    if (!found) {
      min_value = pane.range.left;
      max_value = pane.range.right;
      found = true;
    } else {
      min_value = std::min(min_value, pane.range.left);
      max_value = std::max(max_value, pane.range.right);
    }
  }
  if (!found) {
    return std::nullopt;
  }
  return std::make_pair(min_value, max_value);
}

void ensure_shared_range(UiState *state, const AppSession &session) {
  if (session.route_data.has_time_range) {
    state->route_x_min = session.route_data.x_min;
    state->route_x_max = session.route_data.x_max;
  } else {
    state->route_x_min = 0.0;
    state->route_x_max = 1.0;
  }

  if (state->has_shared_range) {
    return;
  }

  if (session.data_mode == SessionDataMode::Stream) {
    const double target_span = std::max(kMinHorizontalZoomSeconds, session.stream_buffer_seconds);
    if (session.route_data.has_time_range) {
      state->x_view_max = state->route_x_max;
      state->x_view_min = state->x_view_max - target_span;
    } else {
      state->x_view_min = 0.0;
      state->x_view_max = target_span;
    }
    if (state->x_view_max <= state->x_view_min) {
      state->x_view_max = state->x_view_min + 1.0;
    }
    state->has_shared_range = true;
    if (!state->has_tracker_time || state->tracker_time < state->route_x_min || state->tracker_time > state->route_x_max) {
      state->tracker_time = state->route_x_max;
      state->has_tracker_time = session.route_data.has_time_range;
    }
    return;
  }

  if (const WorkspaceTab *tab = active_tab(session.layout, *state); tab != nullptr) {
    if (std::optional<std::pair<double, double>> tab_range = tab_default_x_range(*tab); tab_range.has_value()) {
      state->x_view_min = tab_range->first;
      state->x_view_max = tab_range->second;
      state->has_shared_range = true;
      if (!state->has_tracker_time || state->tracker_time < state->route_x_min || state->tracker_time > state->route_x_max) {
        state->tracker_time = state->route_x_min;
        state->has_tracker_time = true;
      }
      return;
    }
  }

  state->x_view_min = state->route_x_min;
  state->x_view_max = state->route_x_max;
  if (state->x_view_max <= state->x_view_min) {
    state->x_view_max = state->x_view_min + 1.0;
  }
  state->has_shared_range = true;
  if (!state->has_tracker_time || state->tracker_time < state->route_x_min || state->tracker_time > state->route_x_max) {
    state->tracker_time = state->route_x_min;
    state->has_tracker_time = true;
  }
}

void clamp_shared_range(UiState *state, const AppSession &session) {
  if (!state->has_shared_range) {
    return;
  }
  const double min_span = kMinHorizontalZoomSeconds;
  double span = state->x_view_max - state->x_view_min;
  if (span < min_span) {
    const double center = 0.5 * (state->x_view_min + state->x_view_max);
    span = min_span;
    state->x_view_min = center - span * 0.5;
    state->x_view_max = center + span * 0.5;
  }
  if (session.data_mode == SessionDataMode::Stream) {
    if (session.route_data.has_time_range && state->x_view_max > state->route_x_max) {
      state->x_view_min -= state->x_view_max - state->route_x_max;
      state->x_view_max = state->route_x_max;
    }
    if (state->x_view_max <= state->x_view_min) {
      state->x_view_max = state->x_view_min + min_span;
    }
    if (state->has_tracker_time && session.route_data.has_time_range) {
      state->tracker_time = std::clamp(state->tracker_time, state->route_x_min, state->route_x_max);
    }
    return;
  }
  if (state->route_x_max > state->route_x_min) {
    if (state->x_view_min < state->route_x_min) {
      state->x_view_max += state->route_x_min - state->x_view_min;
      state->x_view_min = state->route_x_min;
    }
    if (state->x_view_max > state->route_x_max) {
      state->x_view_min -= state->x_view_max - state->route_x_max;
      state->x_view_max = state->route_x_max;
    }
    if (state->x_view_min < state->route_x_min) {
      state->x_view_min = state->route_x_min;
    }
    if (state->x_view_max <= state->x_view_min) {
      state->x_view_max = std::min(state->route_x_max, state->x_view_min + min_span);
    }
  }
  if (state->has_tracker_time) {
    state->tracker_time = std::clamp(state->tracker_time, state->route_x_min, state->route_x_max);
  }
}

void reset_shared_range(UiState *state, const AppSession &session) {
  state->has_shared_range = false;
  ensure_shared_range(state, session);
  clamp_shared_range(state, session);
}

void update_follow_range(UiState *state, const AppSession &session) {
  if (!state->follow_latest || !state->has_shared_range) {
    return;
  }
  const double span = session.data_mode == SessionDataMode::Stream
    ? std::max(kMinHorizontalZoomSeconds, session.stream_buffer_seconds)
    : std::max(kMinHorizontalZoomSeconds, state->x_view_max - state->x_view_min);
  const double route_span = state->route_x_max - state->route_x_min;
  if (route_span <= 0.0) {
    return;
  }
  state->x_view_max = state->route_x_max;
  state->x_view_min = state->x_view_max - span;
  clamp_shared_range(state, session);
}

void advance_playback(UiState *state, const AppSession &session) {
  if (!state->playback_playing || !state->has_shared_range || state->route_x_max <= state->route_x_min) {
    return;
  }

  state->tracker_time += std::max(0.0, static_cast<double>(ImGui::GetIO().DeltaTime)) * state->playback_rate;
  if (state->tracker_time >= state->route_x_max) {
    if (state->playback_loop) {
      state->tracker_time = state->route_x_min;
    } else {
      state->tracker_time = state->route_x_max;
      state->playback_playing = false;
    }
  }

  const double span = std::max(kMinHorizontalZoomSeconds, state->x_view_max - state->x_view_min);
  if (state->tracker_time < state->x_view_min || state->tracker_time > state->x_view_max) {
    state->x_view_min = state->tracker_time - span * 0.5;
    state->x_view_max = state->tracker_time + span * 0.5;
    clamp_shared_range(state, session);
  }
}

void step_tracker(UiState *state, double direction) {
  if (!state->has_shared_range) {
    return;
  }
  state->tracker_time += direction * std::max(0.001, state->playback_step);
  state->tracker_time = std::clamp(state->tracker_time, state->route_x_min, state->route_x_max);
}

void show_hover_tooltip(const char *text) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort)) {
    return;
  }
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 6.0f));
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(text);
  ImGui::EndTooltip();
  ImGui::PopStyleVar();
}

std::string layout_combo_label(const AppSession &session, const UiState &state) {
  const std::string base = session.layout_path.empty() ? std::string("untitled") : session.layout_path.stem().string();
  return state.layout_dirty ? base + " *" : base;
}

float draw_main_menu_bar(AppSession *session, UiState *state) {
  float height = ImGui::GetFrameHeight();
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Open Route...")) {
        state->open_open_route = true;
      }
      if (ImGui::MenuItem("Stream...")) {
        state->open_stream = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("New Layout")) {
        start_new_layout(session, state);
      }
      if (ImGui::MenuItem("Load Layout...")) {
        state->open_load_layout = true;
      }
      if (ImGui::MenuItem("Save Layout")) {
        state->request_save_layout = true;
      }
      if (ImGui::MenuItem("Save Layout As...")) {
        state->open_save_layout = true;
      }
      if (ImGui::MenuItem("Reset Layout")) {
        state->request_reset_layout = true;
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Show DEPRECATED Fields", nullptr, state->show_deprecated_fields)) {
        state->show_deprecated_fields = !state->show_deprecated_fields;
        rebuild_browser_nodes(session, state);
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Plot View")) {
        reset_shared_range(state, *session);
        state->follow_latest = false;
        state->suppress_range_side_effects = true;
        state->status_text = "Plot view reset";
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Close")) {
        state->request_close = true;
      }
      ImGui::EndMenu();
    }
    height = ImGui::GetWindowSize().y;
    ImGui::EndMainMenuBar();
  }
  return height;
}
