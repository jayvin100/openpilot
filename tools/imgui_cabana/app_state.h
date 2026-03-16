#pragma once

#include <optional>
#include <string>

namespace imgui_cabana {

struct Args {
  bool demo = false;
  bool no_vipc = false;
  std::optional<std::string> route;
  std::optional<std::string> dbc_path;
  std::optional<std::string> data_dir;
  int width = 1600;
  int height = 900;
};

// AppState will become the framework-agnostic source of truth for Cabana UI state.
// For now it carries launch-time configuration so the shell can be split into modules
// without losing the current working behavior.
class AppState {
 public:
  explicit AppState(Args args);

  const Args &args() const { return args_; }
  const std::string &route() const { return route_; }

 private:
  Args args_;
  std::string route_;
};

Args parseArgs(int argc, char *argv[]);

}  // namespace imgui_cabana
