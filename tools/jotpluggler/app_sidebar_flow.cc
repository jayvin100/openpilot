std::string dbc_combo_label(const AppSession &session) {
  if (!session.dbc_override.empty()) {
    return session.dbc_override;
  }
  if (!session.route_data.dbc_name.empty()) {
    return "Auto: " + session.route_data.dbc_name;
  }
  return "Auto";
}

void draw_status_bar(const AppSession &session, const UiMetrics &ui, UiState *state) {
  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.status_bar_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, STATUS_BAR_HEIGHT));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(247, 248, 250));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(188, 193, 199));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##status_bar", nullptr, flags)) {
    ImGui::BeginDisabled(!session.route_data.has_time_range);
    ImGui::Checkbox("Loop", &state->playback_loop);
    ImGui::SameLine();
    if (ImGui::Button(state->playback_playing ? "Pause" : "Play", ImVec2(56.0f, 0.0f))) {
      state->playback_playing = !state->playback_playing;
    }
    ImGui::SameLine();
    const float time_width = 88.0f;
    ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - time_width - 16.0f));
    if (ImGui::SliderScalar("##time_slider",
                            ImGuiDataType_Double,
                            &state->tracker_time,
                            &state->route_x_min,
                            &state->route_x_max,
                            "%.3f")) {
      const double span = std::max(MIN_HORIZONTAL_ZOOM_SECONDS, state->x_view_max - state->x_view_min);
      if (state->tracker_time < state->x_view_min || state->tracker_time > state->x_view_max) {
        state->x_view_min = state->tracker_time - span * 0.5;
        state->x_view_max = state->tracker_time + span * 0.5;
        clamp_shared_range(state, session);
      }
    }
    ImGui::SameLine();
    char tracker_text[64] = {};
    std::snprintf(tracker_text, sizeof(tracker_text), "%.3f", state->has_tracker_time ? state->tracker_time : 0.0);
    app_push_mono_font();
    ImGui::TextUnformatted(tracker_text);
    app_pop_mono_font();
    ImGui::EndDisabled();
  }
  ImGui::End();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar();
}

void draw_sidebar_resizer(const UiMetrics &ui, UiState *state) {
  constexpr float kHandleWidth = 14.0f;
  ImGui::SetNextWindowPos(ImVec2(ui.sidebar_width - kHandleWidth * 0.5f, ui.top_offset));
  ImGui::SetNextWindowSize(ImVec2(kHandleWidth, std::max(1.0f, ui.height - ui.top_offset)));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoBackground;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  if (ImGui::Begin("##sidebar_resizer", nullptr, flags)) {
    ImGui::InvisibleButton("##sidebar_resizer_button", ImVec2(kHandleWidth, std::max(1.0f, ui.height - ui.top_offset)));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    if (ImGui::IsItemActive()) {
      const float max_width = std::min(SIDEBAR_MAX_WIDTH, ui.width * 0.6f);
      state->sidebar_width = std::clamp(ImGui::GetIO().MousePos.x, SIDEBAR_MIN_WIDTH, max_width);
    }

    ImDrawList *draw_list = ImGui::GetWindowDrawList();
    const ImVec2 origin = ImGui::GetWindowPos();
    draw_list->AddLine(ImVec2(origin.x + kHandleWidth * 0.5f, origin.y),
                       ImVec2(origin.x + kHandleWidth * 0.5f, origin.y + std::max(1.0f, ui.height - ui.top_offset)),
                       IM_COL32(194, 198, 204, 255));
  }
  ImGui::End();
  ImGui::PopStyleVar();
}
