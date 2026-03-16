#include "tools/imgui_cabana/app_state.h"

#include <cstdio>
#include <cstdlib>
#include <string_view>

namespace imgui_cabana {

namespace {

constexpr const char *kDefaultDemoRoute = "5beb9b58bd12b691/0000010a--a51155e496/0";

}  // namespace

AppState::AppState(Args args) : args_(std::move(args)), route_(args_.route.value_or(kDefaultDemoRoute)) {}

Args parseArgs(int argc, char *argv[]) {
  Args args;
  if (const char *size = std::getenv("CABANA_SMOKETEST_SIZE")) {
    int width = 0;
    int height = 0;
    if (std::sscanf(size, "%dx%d", &width, &height) == 2 && width > 0 && height > 0) {
      args.width = width;
      args.height = height;
    }
  }

  for (int i = 1; i < argc; ++i) {
    const std::string_view arg = argv[i];
    if (arg == "--help" || arg == "-h") {
      std::printf("Usage: %s [options] route\n", argv[0]);
      std::printf("  --demo\n  --no-vipc\n  --dbc <file>\n  --data_dir <dir>\n");
      std::exit(0);
    } else if (arg == "--demo") {
      args.demo = true;
    } else if (arg == "--no-vipc") {
      args.no_vipc = true;
    } else if (arg == "--dbc" && i + 1 < argc) {
      args.dbc_path = argv[++i];
    } else if (arg == "--data_dir" && i + 1 < argc) {
      args.data_dir = argv[++i];
    } else if (!arg.empty() && arg[0] != '-') {
      args.route = std::string(arg);
    }
  }

  return args;
}

}  // namespace imgui_cabana
