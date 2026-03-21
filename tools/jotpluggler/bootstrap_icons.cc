#include "tools/jotpluggler/bootstrap_icons.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>

namespace jotpluggler::bootstrap_icons {
namespace {

struct RasterizedBootstrapIconData {
  const char *id;
  int width;
  int height;
  const unsigned char *alpha;
};

#include "tools/jotpluggler/bootstrap_icons_generated.inc"

const RasterizedBootstrapIconData *find_icon(std::string_view icon_id) {
  const auto it = std::find_if(std::begin(kBootstrapIcons), std::end(kBootstrapIcons), [&](const auto &icon) {
    return icon_id == icon.id;
  });
  return it == std::end(kBootstrapIcons) ? nullptr : &(*it);
}

ImU32 modulate_alpha(ImU32 color, unsigned char alpha) {
  ImVec4 rgba = ImGui::ColorConvertU32ToFloat4(color);
  rgba.w *= static_cast<float>(alpha) / 255.0f;
  return ImGui::ColorConvertFloat4ToU32(rgba);
}

}  // namespace

void draw(std::string_view icon_id, ImVec2 pos, float size, ImU32 color, ImDrawList *draw_list) {
  const RasterizedBootstrapIconData *icon = find_icon(icon_id);
  if (icon == nullptr) {
    return;
  }

  ImDrawList *target = draw_list != nullptr ? draw_list : ImGui::GetWindowDrawList();
  const float scale_x = size / static_cast<float>(icon->width);
  const float scale_y = size / static_cast<float>(icon->height);
  const float pixel_w = std::max(1.0f, scale_x);
  const float pixel_h = std::max(1.0f, scale_y);

  for (int y = 0; y < icon->height; ++y) {
    for (int x = 0; x < icon->width; ++x) {
      const unsigned char alpha = icon->alpha[y * icon->width + x];
      if (alpha == 0) {
        continue;
      }
      const ImVec2 p0(pos.x + static_cast<float>(x) * scale_x, pos.y + static_cast<float>(y) * scale_y);
      const ImVec2 p1(p0.x + pixel_w, p0.y + pixel_h);
      target->AddRectFilled(p0, p1, modulate_alpha(color, alpha));
    }
  }
}

bool menu_item(std::string_view icon_id,
               const char *label,
               const char *shortcut,
               bool selected,
               bool enabled) {
  std::string padded_label = "      ";
  padded_label += label;
  const bool activated = ImGui::MenuItem(padded_label.c_str(), shortcut, selected, enabled);

  const ImVec2 item_min = ImGui::GetItemRectMin();
  const ImVec2 item_max = ImGui::GetItemRectMax();
  const float icon_size = ImGui::GetTextLineHeight();
  const float icon_x = item_min.x + ImGui::GetStyle().FramePadding.x;
  const float icon_y = item_min.y + std::max(0.0f, (item_max.y - item_min.y - icon_size) * 0.5f);
  const ImU32 color = ImGui::GetColorU32(enabled ? ImGuiCol_Text : ImGuiCol_TextDisabled);
  draw(icon_id, ImVec2(icon_x, icon_y), icon_size, color, ImGui::GetWindowDrawList());
  return activated;
}

}  // namespace jotpluggler::bootstrap_icons
