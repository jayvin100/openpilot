#include <cstdlib>
#include <iostream>
#include <string>

#include "tools/jotpluggler/app.h"

namespace {

constexpr const char *kDemoRoute = "5beb9b58bd12b691/0000010a--a51155e496";
constexpr const char *kDefaultLayout = "longitudinal";

void print_usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " [--layout <layout>] [options] [route]\n"
      << "\n"
      << "Options:\n"
      << "  --demo\n"
      << "  --data-dir <dir>\n"
      << "  --width <pixels>\n"
      << "  --height <pixels>\n"
      << "  --output <png>\n"
      << "  --show\n"
      << "\n"
      << "Examples:\n"
      << "  " << argv0 << " --demo\n"
      << "  " << argv0 << " --layout longitudinal --demo --output /tmp/longitudinal.png\n";
}

bool parse_int(const char *value, int *out) {
  char *end = nullptr;
  const long parsed = std::strtol(value, &end, 10);
  if (end == nullptr || *end != '\0') {
    return false;
  }
  *out = static_cast<int>(parsed);
  return true;
}

}  // namespace

int main(int argc, char *argv[]) {
  jotpluggler::Options options;
  bool demo_requested = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    const auto require_value = [&](const char *flag) -> const char * {
      if (i + 1 >= argc) {
        std::cerr << "Missing value for " << flag << "\n";
        print_usage(argv[0]);
        std::exit(2);
      }
      return argv[++i];
    };

    if (arg == "--layout") {
      options.layout = require_value("--layout");
    } else if (arg == "--demo") {
      demo_requested = true;
      options.route_name = kDemoRoute;
    } else if (arg == "--data-dir") {
      options.data_dir = require_value("--data-dir");
    } else if (arg == "--output") {
      options.output_path = require_value("--output");
    } else if (arg == "--width") {
      if (!parse_int(require_value("--width"), &options.width)) {
        std::cerr << "Invalid width\n";
        return 2;
      }
    } else if (arg == "--height") {
      if (!parse_int(require_value("--height"), &options.height)) {
        std::cerr << "Invalid height\n";
        return 2;
      }
    } else if (arg == "--show") {
      options.show = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else if (!arg.empty() && arg[0] != '-' && options.route_name.empty()) {
      options.route_name = arg;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 2;
    }
  }

  if (demo_requested && options.layout.empty()) {
    options.layout = kDefaultLayout;
  }
  if (options.output_path.empty() && !options.show) {
    options.show = true;
  }

  if (options.layout.empty() || options.route_name.empty()) {
    print_usage(argv[0]);
    return 2;
  }
  if (options.width <= 0 || options.height <= 0) {
    std::cerr << "Width and height must be positive\n";
    return 2;
  }

  return jotpluggler::run(options);
}
