#pragma once

#include <string>

namespace cabana {

struct AppState;

namespace settings {

std::string imguiIniPath();
std::string statePath();
bool load(AppState &state);
bool save(const AppState &state);

}  // namespace settings
}  // namespace cabana
