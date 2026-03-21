std::optional<double> earliest_stream_batch_time(const StreamExtractBatch &batch) {
  std::optional<double> earliest;
  for (const RouteSeries &series : batch.series) {
    if (!series.times.empty()) {
      earliest = earliest.has_value() ? std::min(*earliest, series.times.front()) : series.times.front();
    }
  }
  if (!batch.logs.empty()) {
    const double log_time = batch.logs.front().mono_time;
    earliest = earliest.has_value() ? std::min(*earliest, log_time) : log_time;
  }
  return earliest;
}

std::optional<double> latest_stream_batch_time(const StreamExtractBatch &batch) {
  std::optional<double> latest;
  for (const RouteSeries &series : batch.series) {
    if (!series.times.empty()) {
      latest = latest.has_value() ? std::max(*latest, series.times.back()) : series.times.back();
    }
  }
  if (!batch.logs.empty()) {
    const double log_time = batch.logs.back().mono_time;
    latest = latest.has_value() ? std::max(*latest, log_time) : log_time;
  }
  return latest;
}

bool layout_has_custom_curves(const SketchLayout &layout) {
  for (const WorkspaceTab &tab : layout.tabs) {
    for (const Pane &pane : tab.panes) {
      for (const Curve &curve : pane.curves) {
        if (curve.custom_python.has_value()) {
          return true;
        }
      }
    }
  }
  return false;
}

void apply_stream_batch(AppSession *session, UiState *state, StreamExtractBatch batch) {
  if (batch.has_time_offset) {
    session->stream_time_offset = batch.time_offset;
  }
  if (!batch.car_fingerprint.empty()) {
    session->route_data.car_fingerprint = batch.car_fingerprint;
  }
  if (!batch.dbc_name.empty()) {
    session->route_data.dbc_name = batch.dbc_name;
  }
  if (!batch.enum_info.empty()) {
    for (auto &[path, info] : batch.enum_info) {
      session->route_data.enum_info[path] = std::move(info);
    }
  }

  bool new_paths = false;
  std::vector<RouteSeries> new_series;
  for (RouteSeries &incoming : batch.series) {
    const RouteSeries *existing_const = app_find_route_series(*session, incoming.path);
    if (existing_const == nullptr) {
      new_series.push_back(std::move(incoming));
      new_paths = true;
      continue;
    }
    RouteSeries &series = *const_cast<RouteSeries *>(existing_const);
    series.times.insert(series.times.end(), incoming.times.begin(), incoming.times.end());
    series.values.insert(series.values.end(), incoming.values.begin(), incoming.values.end());
  }
  for (RouteSeries &series : new_series) {
    session->route_data.paths.push_back(series.path);
    session->route_data.series.push_back(std::move(series));
  }

  if (!batch.logs.empty()) {
    std::sort(batch.logs.begin(), batch.logs.end(), [](const LogEntry &a, const LogEntry &b) {
      return a.mono_time < b.mono_time;
    });
    const size_t old_size = session->route_data.logs.size();
    session->route_data.logs.insert(session->route_data.logs.end(),
                                    std::make_move_iterator(batch.logs.begin()),
                                    std::make_move_iterator(batch.logs.end()));
    if (old_size > 0 && session->route_data.logs.size() > old_size
        && session->route_data.logs[old_size - 1].mono_time > session->route_data.logs[old_size].mono_time) {
      std::inplace_merge(session->route_data.logs.begin(),
                         session->route_data.logs.begin() + static_cast<ptrdiff_t>(old_size),
                         session->route_data.logs.end(),
                         [](const LogEntry &a, const LogEntry &b) {
                           return a.mono_time < b.mono_time;
                         });
    }
  }

  if (new_paths) {
    std::sort(session->route_data.paths.begin(), session->route_data.paths.end());
    std::sort(session->route_data.series.begin(), session->route_data.series.end(), [](const RouteSeries &a, const RouteSeries &b) {
      return a.path < b.path;
    });
    session->route_data.roots = collect_route_roots_for_paths(session->route_data.paths);
    rebuild_route_index(session);
    rebuild_browser_nodes(session, state);
  }

  const std::optional<double> earliest_time = earliest_stream_batch_time(batch);
  const std::optional<double> latest_time = latest_stream_batch_time(batch);
  if (earliest_time.has_value() && latest_time.has_value()) {
    if (!session->route_data.has_time_range) {
      session->route_data.x_min = *earliest_time;
      session->route_data.x_max = *latest_time;
    } else {
      session->route_data.x_min = std::min(session->route_data.x_min, *earliest_time);
      session->route_data.x_max = std::max(session->route_data.x_max, *latest_time);
    }
    session->route_data.has_time_range = true;
  }

  if (latest_time.has_value() && layout_has_custom_curves(session->layout)
      && *latest_time >= session->next_stream_custom_refresh_time) {
    refresh_all_custom_curves(session, state);
    session->next_stream_custom_refresh_time = *latest_time + 0.1;
  }
  if (state->follow_latest || !state->has_tracker_time) {
    state->tracker_time = session->route_data.x_max;
    state->has_tracker_time = session->route_data.has_time_range;
  }
  if (!state->has_shared_range) {
    reset_shared_range(state, *session);
  }
}
