bool reset_layout(AppSession *session, UiState *state) {
  try {
    if (session->layout_path.empty()) {
      start_new_layout(session, state, "Reset layout");
      return true;
    }
    clear_layout_autosave(*session);
    session->layout = load_sketch_layout(session->layout_path);
    state->layout_dirty = false;
    session->autosave_path = autosave_path_for_layout(session->layout_path);
    state->undo.reset(session->layout);
    state->tabs.clear();
    cancel_rename_tab(state);
    sync_ui_state(state, session->layout);
    sync_layout_buffers(state, *session);
    reset_shared_range(state, *session);
    state->status_text = "Reset layout";
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to reset layout";
    return false;
  }
}

bool reload_layout(AppSession *session, UiState *state, const std::string &layout_arg) {
  try {
    const bool preserve_shared_range = session->route_data.has_time_range && state->has_shared_range;
    const double preserved_x_min = state->x_view_min;
    const double preserved_x_max = state->x_view_max;
    const fs::path layout_path = resolve_layout_path(layout_arg);
    session->autosave_path = autosave_path_for_layout(layout_path);
    const bool load_draft = fs::exists(session->autosave_path);
    session->layout = load_sketch_layout(load_draft ? session->autosave_path : layout_path);
    session->layout_path = layout_path;
    state->layout_dirty = load_draft;
    state->undo.reset(session->layout);
    cancel_rename_tab(state);
    state->tabs.clear();
    sync_ui_state(state, session->layout);
    sync_layout_buffers(state, *session);
    mark_all_docks_dirty(state);
    if (preserve_shared_range) {
      state->has_shared_range = true;
      state->x_view_min = preserved_x_min;
      state->x_view_max = preserved_x_max;
      clamp_shared_range(state, *session);
    } else {
      reset_shared_range(state, *session);
    }
    state->status_text = std::string(load_draft ? "Loaded layout draft " : "Loaded layout ")
      + layout_path.filename().string();
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to load layout";
    return false;
  }
}

bool save_layout(AppSession *session, UiState *state, const std::string &layout_path) {
  try {
    if (layout_path.empty()) throw std::runtime_error("Layout path is empty");
    session->layout.current_tab_index = state->active_tab_index;
    const fs::path previous_autosave = session->autosave_path;
    const fs::path output = fs::absolute(fs::path(layout_path));
    save_layout_json(session->layout, output);
    session->layout_path = output;
    session->autosave_path = autosave_path_for_layout(output);
    if (!previous_autosave.empty() && previous_autosave != session->autosave_path && fs::exists(previous_autosave)) {
      fs::remove(previous_autosave);
    }
    clear_layout_autosave(*session);
    state->layout_dirty = false;
    sync_layout_buffers(state, *session);
    state->status_text = "Saved layout " + output.filename().string();
    g_layout_names_dirty = true;
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to save layout";
    return false;
  }
}

void rebuild_session_route_data(AppSession *session, UiState *state,
                                const RouteLoadProgressCallback &progress = {}) {
  apply_route_data(session, state, load_route_data(session->route_name, session->data_dir, session->dbc_override, progress));
}

void stop_stream_session(AppSession *session, UiState *state, bool preserve_data) {
  if (preserve_data && session->stream_poller && session->data_mode == SessionDataMode::Stream) {
    session->stream_poller->setPaused(true);
  } else if (session->stream_poller) {
    session->stream_poller->stop();
  }
  session->stream_paused = preserve_data && session->data_mode == SessionDataMode::Stream;
  if (!preserve_data) {
    session->stream_time_offset.reset();
    apply_route_data(session, state, RouteData{});
  }
  sync_stream_buffers(state, *session);
}

bool start_stream_session(AppSession *session,
                          UiState *state,
                          const std::string &address,
                          double buffer_seconds,
                          bool preserve_existing_data) {
  try {
    if (session->route_loader) {
      session->route_loader.reset();
    }
    session->data_mode = SessionDataMode::Stream;
    session->route_id = {};
    session->route_name.clear();
    session->data_dir.clear();
    session->stream_address = address.empty() ? "127.0.0.1" : address;
    session->stream_buffer_seconds = std::max(1.0, buffer_seconds);
    session->next_stream_custom_refresh_time = 0.0;
    session->stream_paused = false;
    if (preserve_existing_data && session->stream_poller) {
      StreamPollSnapshot snapshot = session->stream_poller->snapshot();
      if (snapshot.active) {
        session->stream_poller->setPaused(false);
        sync_route_buffers(state, *session);
        sync_stream_buffers(state, *session);
        state->follow_latest = true;
        state->playback_playing = false;
        state->status_text = "Resumed stream " + session->stream_address;
        return true;
      }
    }
    if (!preserve_existing_data) {
      session->stream_time_offset.reset();
      apply_route_data(session, state, RouteData{});
    }
    if (!session->stream_poller) {
      session->stream_poller = std::make_unique<StreamPoller>();
    }
    session->stream_poller->start(session->stream_address,
                                  session->stream_buffer_seconds,
                                  session->dbc_override,
                                  session->stream_time_offset);
    sync_route_buffers(state, *session);
    sync_stream_buffers(state, *session);
    state->follow_latest = true;
    state->playback_playing = false;
    state->status_text = preserve_existing_data ? "Resumed stream " + session->stream_address
                                                : "Streaming from " + session->stream_address;
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to start stream";
    return false;
  }
}

void start_async_route_load(AppSession *session, UiState *state) {
  if (!session->route_loader) {
    return;
  }
  apply_route_data(session, state, RouteData{});
  session->route_loader->start(session->route_name, session->data_dir, session->dbc_override);
  state->status_text = session->route_name.empty() ? "Ready" : "Loading route " + session->route_name;
}

void poll_async_route_load(AppSession *session, UiState *state) {
  if (!session->route_loader) {
    return;
  }
  RouteData loaded_route;
  std::string error_text;
  if (!session->route_loader->consume(&loaded_route, &error_text)) {
    return;
  }
  if (!error_text.empty()) {
    state->error_text = error_text;
    state->open_error_popup = true;
    state->status_text = "Failed to load route";
    return;
  }
  apply_route_data(session, state, std::move(loaded_route));
  state->status_text = session->route_name.empty() ? "Ready" : "Loaded route " + session->route_name;
}

bool reload_session(AppSession *session, UiState *state, const std::string &route_name, const std::string &data_dir) {
  try {
    stop_stream_session(session, state, false);
    session->data_mode = SessionDataMode::Route;
    session->route_name = route_name;
    session->route_id = parse_route_identifier(route_name);
    session->data_dir = data_dir;
    if (session->async_route_loading) {
      if (!session->route_loader) {
        session->route_loader = std::make_unique<AsyncRouteLoader>(::isatty(STDERR_FILENO) != 0);
      }
      start_async_route_load(session, state);
    } else {
      rebuild_session_route_data(session, state);
      state->status_text = "Loaded route " + route_name;
    }
    sync_route_buffers(state, *session);
    return true;
  } catch (const std::exception &err) {
    state->error_text = err.what();
    state->open_error_popup = true;
    state->status_text = "Failed to load route";
    return false;
  }
}

void draw_popups(AppSession *session, UiState *state) {
  auto open_popup = [](bool &flag, const char *name) {
    if (flag) { ImGui::OpenPopup(name); flag = false; }
  };
  open_popup(state->open_open_route, "Open Route");
  if (state->open_stream) { sync_stream_buffers(state, *session); }
  open_popup(state->open_stream, "Live Stream");
  if (state->open_load_layout || state->open_save_layout) { sync_layout_buffers(state, *session); }
  open_popup(state->open_load_layout, "Load Layout");
  open_popup(state->open_save_layout, "Save Layout");
  open_popup(state->axis_limits.open, "Edit Axis Limits");

  if (ImGui::BeginPopupModal("Open Route", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Load a route into the current layout.");
    ImGui::Separator();
    ImGui::InputText("Route", state->route_buffer.data(), state->route_buffer.size());
    ImGui::InputText("Data Dir", state->data_dir_buffer.data(), state->data_dir_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Load", ImVec2(120.0f, 0.0f))) {
      reload_session(session, state, std::string(state->route_buffer.data()), std::string(state->data_dir_buffer.data()));
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_route_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Live Stream", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Connect to live cereal services.");
    ImGui::Separator();
    if (ImGui::RadioButton("Local (MSGQ)", !state->stream_remote)) {
      state->stream_remote = false;
    }
    if (ImGui::RadioButton("Remote (ZMQ)", state->stream_remote)) {
      state->stream_remote = true;
    }
    ImGui::BeginDisabled(!state->stream_remote);
    ImGui::InputText("Address", state->stream_address_buffer.data(), state->stream_address_buffer.size());
    ImGui::EndDisabled();
    ImGui::InputDouble("Buffer (seconds)", &state->stream_buffer_seconds, 0.0, 0.0, "%.0f");
    ImGui::Spacing();
    if (ImGui::Button("Connect", ImVec2(120.0f, 0.0f))) {
      const std::string address = state->stream_remote ? std::string(state->stream_address_buffer.data()) : "127.0.0.1";
      if (start_stream_session(session, state, address, state->stream_buffer_seconds, false)) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_stream_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Load Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Load a JotPlugger JSON layout.");
    ImGui::Separator();
    ImGui::InputText("Layout", state->load_layout_buffer.data(), state->load_layout_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Load", ImVec2(120.0f, 0.0f))) {
      if (reload_layout(session, state, std::string(state->load_layout_buffer.data()))) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_layout_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Save Layout", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextUnformatted("Save the current workspace as a JotPlugger JSON layout.");
    ImGui::Separator();
    ImGui::InputText("Layout", state->save_layout_buffer.data(), state->save_layout_buffer.size());
    ImGui::Spacing();
    if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
      if (save_layout(session, state, std::string(state->save_layout_buffer.data()))) {
        ImGui::CloseCurrentPopup();
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
      sync_layout_buffers(state, *session);
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
  if (ImGui::BeginPopupModal("Edit Axis Limits", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    const WorkspaceTab *tab = app_active_tab(session->layout, *state);
    const bool valid_pane = tab != nullptr
      && state->axis_limits.pane_index >= 0
      && state->axis_limits.pane_index < static_cast<int>(tab->panes.size());
    if (!valid_pane) {
      ImGui::TextWrapped("The selected pane is no longer available.");
      ImGui::Spacing();
      if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
        state->axis_limits.pane_index = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    } else {
      ImGui::TextUnformatted("X range applies to the active tab. Y limits apply to the selected pane.");
      ImGui::Separator();
      ImGui::TextUnformatted("Horizontal");
      ImGui::SetNextItemWidth(180.0f);
      ImGui::InputDouble("X Min", &state->axis_limits.x_min, 0.0, 0.0, "%.3f");
      ImGui::SetNextItemWidth(180.0f);
      ImGui::InputDouble("X Max", &state->axis_limits.x_max, 0.0, 0.0, "%.3f");
      ImGui::Spacing();
      ImGui::TextUnformatted("Vertical");
      ImGui::Checkbox("Use Y Min", &state->axis_limits.y_min_enabled);
      ImGui::BeginDisabled(!state->axis_limits.y_min_enabled);
      ImGui::SetNextItemWidth(180.0f);
      ImGui::InputDouble("Y Min", &state->axis_limits.y_min, 0.0, 0.0, "%.6g");
      ImGui::EndDisabled();
      ImGui::Checkbox("Use Y Max", &state->axis_limits.y_max_enabled);
      ImGui::BeginDisabled(!state->axis_limits.y_max_enabled);
      ImGui::SetNextItemWidth(180.0f);
      ImGui::InputDouble("Y Max", &state->axis_limits.y_max, 0.0, 0.0, "%.6g");
      ImGui::EndDisabled();
      ImGui::Spacing();
      if (ImGui::Button("Apply", ImVec2(120.0f, 0.0f))) {
        if (apply_axis_limits_editor(session, state)) {
          state->axis_limits.pane_index = -1;
          ImGui::CloseCurrentPopup();
        }
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        state->axis_limits.pane_index = -1;
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
  }
  if (state->open_error_popup) {
    ImGui::OpenPopup("Error");
    state->open_error_popup = false;
  }
  if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", state->error_text.c_str());
    ImGui::Spacing();
    if (ImGui::Button("Close", ImVec2(120.0f, 0.0f))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}
