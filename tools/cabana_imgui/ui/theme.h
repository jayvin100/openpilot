#pragma once

#include "imgui.h"

namespace cabana {
namespace theme {

void apply();
void load_fonts(float dpi_scale);

// Font accessors (valid after load_fonts)
ImFont *font_default();
ImFont *font_bold();
ImFont *font_splash();
float dpi_scale();

}  // namespace theme
}  // namespace cabana
