#pragma once

#include "imgui.h"

namespace cabana {
namespace theme {

void apply();
void load_fonts();

// Font accessors (valid after load_fonts)
ImFont *font_default();
ImFont *font_bold();
ImFont *font_splash();

}  // namespace theme
}  // namespace cabana
