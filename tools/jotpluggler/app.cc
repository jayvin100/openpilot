#include "tools/jotpluggler/app.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unistd.h>
#include <vector>

#include "json11.hpp"

namespace jotpluggler {
namespace fs = std::filesystem;

namespace {

struct Pane {
  float x = 0.0f;
  float y = 0.0f;
  float w = 1.0f;
  float h = 1.0f;
  std::string title;
  std::vector<std::array<uint8_t, 3>> curve_colors;
};

struct Canvas {
  int width = 0;
  int height = 0;
  std::vector<uint8_t> pixels;

  Canvas(int w, int h) : width(w), height(h), pixels(static_cast<size_t>(w * h * 3), 0) {}
};

std::string shell_quote(const std::string &value) {
  std::ostringstream quoted;
  quoted << '\'';
  for (const char c : value) {
    if (c == '\'') {
      quoted << "'\\''";
    } else {
      quoted << c;
    }
  }
  quoted << '\'';
  return quoted.str();
}

fs::path repo_root() {
  std::array<char, 4096> buf = {};
  const ssize_t count = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
  if (count <= 0) {
    throw std::runtime_error("Unable to resolve executable path");
  }
  return fs::path(std::string(buf.data(), static_cast<size_t>(count))).parent_path().parent_path().parent_path();
}

std::string layout_name_from_arg(const std::string &layout_arg) {
  const fs::path raw(layout_arg);
  if (raw.extension() == ".xml") {
    return raw.stem().string();
  }
  if (raw.filename() != raw) {
    return raw.filename().replace_extension("").string();
  }
  fs::path stem_path = raw;
  return stem_path.replace_extension("").string();
}

fs::path resolve_layout_path(const std::string &layout_arg) {
  const fs::path direct(layout_arg);
  if (fs::exists(direct)) {
    return fs::absolute(direct);
  }
  const auto root = repo_root();
  const fs::path candidate = root / "tools" / "plotjuggler" / "layouts" / (layout_name_from_arg(layout_arg) + ".xml");
  if (!fs::exists(candidate)) {
    throw std::runtime_error("Unknown layout: " + layout_arg);
  }
  return candidate;
}

fs::path resolve_baseline_path(const Options &options) {
  const auto root = repo_root();
  const fs::path baseline_root = options.baseline_root.empty()
    ? root / "tools" / "jotpluggler" / "validation" / "baselines"
    : fs::path(options.baseline_root);
  return baseline_root / (layout_name_from_arg(options.layout) + ".png");
}

void ensure_parent_dir(const fs::path &path) {
  const fs::path parent = path.parent_path();
  if (!parent.empty()) {
    fs::create_directories(parent);
  }
}

void run_or_throw(const std::string &command, const std::string &action) {
  const int ret = std::system(command.c_str());
  if (ret != 0) {
    throw std::runtime_error(action + " failed with exit code " + std::to_string(ret));
  }
}

std::string load_text_file(const fs::path &path) {
  std::ifstream in(path);
  if (!in) {
    throw std::runtime_error("Failed to read " + path.string());
  }
  std::ostringstream contents;
  contents << in.rdbuf();
  return contents.str();
}

std::vector<Pane> load_sketch_layout(const Options &options) {
  const auto root = repo_root();
  const fs::path layout_path = resolve_layout_path(options.layout);
  const fs::path exporter = root / "tools" / "jotpluggler" / "layout_export.py";
  const fs::path temp_path = fs::temp_directory_path() / ("jotpluggler-layout-" + layout_name_from_arg(options.layout) + ".json");

  const std::string command =
    shell_quote(options.python) + " " +
    shell_quote(exporter.string()) + " " +
    "--layout " + shell_quote(layout_path.string()) + " " +
    "--output " + shell_quote(temp_path.string());
  run_or_throw(command, "layout export");

  std::string parse_error;
  const auto json = json11::Json::parse(load_text_file(temp_path), parse_error);
  fs::remove(temp_path);
  if (!parse_error.empty()) {
    throw std::runtime_error("Failed to parse exported layout JSON: " + parse_error);
  }

  std::vector<Pane> panes;
  for (const auto &pane_json : json["panes"].array_items()) {
    Pane pane;
    pane.x = static_cast<float>(pane_json["x"].number_value());
    pane.y = static_cast<float>(pane_json["y"].number_value());
    pane.w = static_cast<float>(pane_json["w"].number_value());
    pane.h = static_cast<float>(pane_json["h"].number_value());
    pane.title = pane_json["title"].string_value();
    for (const auto &color_json : pane_json["curve_colors"].array_items()) {
      std::array<uint8_t, 3> color = {};
      const auto values = color_json.array_items();
      if (values.size() == 3) {
        for (size_t i = 0; i < values.size(); ++i) {
          color[i] = static_cast<uint8_t>(values[i].int_value());
        }
        pane.curve_colors.push_back(color);
      }
    }
    panes.push_back(std::move(pane));
  }
  return panes;
}

void set_pixel(Canvas *canvas, int x, int y, const std::array<uint8_t, 3> &color) {
  if (x < 0 || y < 0 || x >= canvas->width || y >= canvas->height) {
    return;
  }
  const size_t index = static_cast<size_t>((y * canvas->width + x) * 3);
  canvas->pixels[index + 0] = color[0];
  canvas->pixels[index + 1] = color[1];
  canvas->pixels[index + 2] = color[2];
}

void fill_rect(Canvas *canvas, int x, int y, int w, int h, const std::array<uint8_t, 3> &color) {
  if (w <= 0 || h <= 0) {
    return;
  }
  const int x0 = std::max(0, x);
  const int y0 = std::max(0, y);
  const int x1 = std::min(canvas->width, x + w);
  const int y1 = std::min(canvas->height, y + h);
  for (int yy = y0; yy < y1; ++yy) {
    for (int xx = x0; xx < x1; ++xx) {
      set_pixel(canvas, xx, yy, color);
    }
  }
}

void stroke_rect(Canvas *canvas, int x, int y, int w, int h, const std::array<uint8_t, 3> &color) {
  if (w <= 1 || h <= 1) {
    return;
  }
  fill_rect(canvas, x, y, w, 1, color);
  fill_rect(canvas, x, y + h - 1, w, 1, color);
  fill_rect(canvas, x, y, 1, h, color);
  fill_rect(canvas, x + w - 1, y, 1, h, color);
}

void draw_grid(Canvas *canvas, int x, int y, int w, int h) {
  const std::array<uint8_t, 3> grid = {46, 52, 61};
  for (int i = 1; i < 4; ++i) {
    const int yy = y + (h * i) / 4;
    fill_rect(canvas, x + 1, yy, std::max(0, w - 2), 1, grid);
  }
  for (int i = 1; i < 5; ++i) {
    const int xx = x + (w * i) / 5;
    fill_rect(canvas, xx, y + 1, 1, std::max(0, h - 2), grid);
  }
}

void draw_swatches(Canvas *canvas, int x, int y, const std::vector<std::array<uint8_t, 3>> &colors) {
  int offset = 0;
  for (const auto &color : colors) {
    fill_rect(canvas, x + offset, y, 16, 4, color);
    offset += 22;
    if (offset > 120) {
      break;
    }
  }
}

Canvas render_sketch(const Options &options, const std::vector<Pane> &panes) {
  Canvas canvas(options.width, options.height);
  const std::array<uint8_t, 3> chrome = {16, 20, 24};
  const std::array<uint8_t, 3> header = {27, 33, 39};
  const std::array<uint8_t, 3> panel = {24, 28, 34};
  const std::array<uint8_t, 3> titlebar = {33, 39, 46};
  const std::array<uint8_t, 3> border = {84, 95, 110};
  const std::array<uint8_t, 3> plot_bg = {20, 24, 29};

  fill_rect(&canvas, 0, 0, canvas.width, canvas.height, chrome);
  fill_rect(&canvas, 0, 0, canvas.width, 56, header);
  fill_rect(&canvas, 0, canvas.height - 28, canvas.width, 28, header);
  fill_rect(&canvas, 18, 14, 190, 28, {54, 137, 255});
  fill_rect(&canvas, canvas.width - 200, 14, 160, 28, {50, 58, 68});

  const int content_x = 18;
  const int content_y = 72;
  const int content_w = std::max(1, canvas.width - 36);
  const int content_h = std::max(1, canvas.height - 118);
  const int gutter = 12;

  for (const auto &pane : panes) {
    const int pane_x = content_x + static_cast<int>(std::lround(pane.x * content_w));
    const int pane_y = content_y + static_cast<int>(std::lround(pane.y * content_h));
    const int pane_w = std::max(60, static_cast<int>(std::lround(pane.w * content_w)) - gutter);
    const int pane_h = std::max(60, static_cast<int>(std::lround(pane.h * content_h)) - gutter);
    fill_rect(&canvas, pane_x, pane_y, pane_w, pane_h, panel);
    stroke_rect(&canvas, pane_x, pane_y, pane_w, pane_h, border);
    fill_rect(&canvas, pane_x, pane_y, pane_w, 28, titlebar);
    fill_rect(&canvas, pane_x + 12, pane_y + 11, std::min(pane_w - 24, 96), 6, {219, 223, 228});
    draw_swatches(&canvas, pane_x + std::max(12, pane_w - 126), pane_y + 12, pane.curve_colors);

    const int plot_x = pane_x + 10;
    const int plot_y = pane_y + 38;
    const int plot_w = std::max(1, pane_w - 20);
    const int plot_h = std::max(1, pane_h - 48);
    fill_rect(&canvas, plot_x, plot_y, plot_w, plot_h, plot_bg);
    stroke_rect(&canvas, plot_x, plot_y, plot_w, plot_h, {58, 66, 77});
    draw_grid(&canvas, plot_x, plot_y, plot_w, plot_h);
  }

  return canvas;
}

void save_canvas(const Canvas &canvas, const fs::path &output_path) {
  ensure_parent_dir(output_path);
  const fs::path ppm_path = output_path.parent_path() / (output_path.stem().string() + ".ppm");
  {
    std::ofstream out(ppm_path, std::ios::binary);
    if (!out) {
      throw std::runtime_error("Failed to open " + ppm_path.string());
    }
    out << "P6\n" << canvas.width << " " << canvas.height << "\n255\n";
    out.write(reinterpret_cast<const char *>(canvas.pixels.data()), static_cast<std::streamsize>(canvas.pixels.size()));
  }

  const std::string convert_command =
    "convert " + shell_quote(ppm_path.string()) + " " + shell_quote(output_path.string());
  run_or_throw(convert_command, "image conversion");
  fs::remove(ppm_path);
}

void maybe_show_window(const Options &options) {
  if (!options.show) {
    return;
  }
  if (!glfwInit()) {
    throw std::runtime_error("glfwInit failed");
  }

  glfwWindowHint(GLFW_VISIBLE, GLFW_TRUE);
  GLFWwindow *window = glfwCreateWindow(options.width, options.height, "jotpluggler", nullptr, nullptr);
  if (window == nullptr) {
    glfwTerminate();
    throw std::runtime_error("glfwCreateWindow failed");
  }

  double start = glfwGetTime();
  while (!glfwWindowShouldClose(window) && glfwGetTime() - start < 1.0) {
    glfwPollEvents();
  }
  glfwDestroyWindow(window);
  glfwTerminate();
}

int run_mock_reference(const Options &options) {
  const fs::path baseline_path = resolve_baseline_path(options);
  if (!fs::exists(baseline_path)) {
    std::cerr << "Missing baseline " << baseline_path << "\n";
    return 1;
  }
  const fs::path output_path(options.output_path);
  ensure_parent_dir(output_path);
  fs::copy_file(baseline_path, output_path, fs::copy_options::overwrite_existing);
  return 0;
}

int run_sketch(const Options &options) {
  const auto panes = load_sketch_layout(options);
  const Canvas canvas = render_sketch(options, panes);
  save_canvas(canvas, options.output_path);
  maybe_show_window(options);
  return 0;
}

}  // namespace

int run(const Options &options) {
  try {
    switch (options.mode) {
      case Mode::MockReference:
        return run_mock_reference(options);
      case Mode::Sketch:
        return run_sketch(options);
    }
  } catch (const std::exception &err) {
    std::cerr << err.what() << "\n";
    return 1;
  }
  return 1;
}

}  // namespace jotpluggler
