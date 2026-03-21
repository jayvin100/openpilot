#pragma once

#include "tools/jotpluggler/app_internal.h"

#include <string>

namespace jotpluggler {

void open_custom_series_editor(UiState *state, const std::string &preferred_source = {});
std::string preferred_custom_series_source(const Pane &pane);
void refresh_all_custom_curves(AppSession *session, UiState *state);
void draw_custom_series_editor(AppSession *session, UiState *state);

}  // namespace jotpluggler
