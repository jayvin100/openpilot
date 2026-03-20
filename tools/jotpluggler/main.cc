#include <cstdlib>
#include <iostream>
#include <string>

#include "tools/jotpluggler/app.h"

namespace {

void print_usage(const char *argv0) {
  std::cerr
      << "Usage: " << argv0 << " --layout <layout> --output <png> [options]\n"
      << "\n"
      << "Options:\n"
      << "  --mode <mock-reference|sketch>\n"
      << "  --width <pixels>\n"
      << "  --height <pixels>\n"
      << "  --baseline-root <dir>\n"
      << "  --python <python>\n"
      << "  --show\n";
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
    } else if (arg == "--output") {
      options.output_path = require_value("--output");
    } else if (arg == "--mode") {
      const std::string mode = require_value("--mode");
      if (mode == "mock-reference") {
        options.mode = jotpluggler::Mode::MockReference;
      } else if (mode == "sketch") {
        options.mode = jotpluggler::Mode::Sketch;
      } else {
        std::cerr << "Unknown mode: " << mode << "\n";
        print_usage(argv[0]);
        return 2;
      }
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
    } else if (arg == "--baseline-root") {
      options.baseline_root = require_value("--baseline-root");
    } else if (arg == "--python") {
      options.python = require_value("--python");
    } else if (arg == "--show") {
      options.show = true;
    } else if (arg == "--help" || arg == "-h") {
      print_usage(argv[0]);
      return 0;
    } else {
      std::cerr << "Unknown argument: " << arg << "\n";
      print_usage(argv[0]);
      return 2;
    }
  }

  if (options.layout.empty() || options.output_path.empty()) {
    print_usage(argv[0]);
    return 2;
  }
  if (options.width <= 0 || options.height <= 0) {
    std::cerr << "Width and height must be positive\n";
    return 2;
  }

  return jotpluggler::run(options);
}
