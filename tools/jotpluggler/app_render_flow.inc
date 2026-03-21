void render_layout(AppSession *session, UiState *state, bool show_camera_feed) {
  ensure_shared_range(state, *session);
  if (state->follow_latest) {
    update_follow_range(state, *session);
    state->suppress_range_side_effects = true;
  } else {
    clamp_shared_range(state, *session);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow, false)) {
    step_tracker(state, -1.0);
  }
  if (ImGui::IsKeyPressed(ImGuiKey_RightArrow, false)) {
    step_tracker(state, 1.0);
  }
  if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive() && ImGui::IsKeyPressed(ImGuiKey_Space, false)) {
    state->playback_playing = !state->playback_playing;
  }
  advance_playback(state, *session);
  if (show_camera_feed && session->camera_feed && state->has_tracker_time) {
    session->camera_feed->update(state->tracker_time);
  }
  const float menu_height = draw_main_menu_bar(session, state);
  const UiMetrics ui = compute_ui_metrics(ImGui::GetMainViewport()->Size, menu_height, state->sidebar_width);
  state->sidebar_width = ui.sidebar_width;
  draw_sidebar(session, ui, state, show_camera_feed);
  draw_workspace(session, ui, state);
  draw_sidebar_resizer(ui, state);
  if (!state->custom_series.selected && !state->logs.selected) {
    draw_pane_windows(session, state);
  }
  draw_status_bar(*session, ui, state);
  draw_popups(session, state);
}

void save_framebuffer_png(const fs::path &output_path, int width, int height) {
  ensure_parent_dir(output_path);
  if (width <= 0 || height <= 0) {
    throw std::runtime_error("Invalid framebuffer size");
  }

  std::vector<uint8_t> pixels(static_cast<size_t>(width) * static_cast<size_t>(height) * 4U, 0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

  const fs::path ppm_path = output_path.parent_path() / (output_path.stem().string() + ".ppm");
  {
    std::ofstream out(ppm_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("Failed to open " + ppm_path.string());
    }
    out << "P6\n" << width << " " << height << "\n255\n";
    for (int y = height - 1; y >= 0; --y) {
      for (int x = 0; x < width; ++x) {
        const size_t index = static_cast<size_t>((y * width + x) * 4);
        out.write(reinterpret_cast<const char *>(&pixels[index]), 3);
      }
    }
  }

  const std::string command = "convert " + shell_quote(ppm_path.string()) + " " + shell_quote(output_path.string());
  run_or_throw(command, "image conversion");
  fs::remove(ppm_path);
}

void render_frame(GLFWwindow *window, AppSession *session, UiState *state, const fs::path *capture_path) {
  glfwPollEvents();

  int framebuffer_width = 0;
  int framebuffer_height = 0;
  glfwGetFramebufferSize(window, &framebuffer_width, &framebuffer_height);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (state->request_save_layout) {
    if (session->layout_path.empty()) {
      state->open_save_layout = true;
    } else {
      save_layout(session, state, session->layout_path.string());
    }
    state->request_save_layout = false;
  }
  if (state->request_reset_layout) {
    reset_layout(session, state);
    state->request_reset_layout = false;
  }
  poll_async_route_load(session, state);
  if (session->data_mode == SessionDataMode::Stream && session->stream_poller) {
    StreamExtractBatch batch;
    std::string error_text;
    if (session->stream_poller->consume(&batch, &error_text)) {
      if (!error_text.empty()) {
        state->error_text = error_text;
        state->open_error_popup = true;
        state->status_text = "Stream disconnected";
      } else {
        apply_stream_batch(session, state, std::move(batch));
      }
    }
  }

  const bool show_camera = capture_path == nullptr && session->data_mode != SessionDataMode::Stream;
  render_layout(session, state, show_camera);
  ImGui::Render();
  if (state->request_close) {
    glfwSetWindowShouldClose(window, GLFW_TRUE);
    state->request_close = false;
  }

  glViewport(0, 0, framebuffer_width, framebuffer_height);
  glClearColor(227.0f / 255.0f, 229.0f / 255.0f, 233.0f / 255.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
  if (capture_path != nullptr) {
    save_framebuffer_png(*capture_path, framebuffer_width, framebuffer_height);
  }
  glfwSwapBuffers(window);
  state->suppress_range_side_effects = false;
}
