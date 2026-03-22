#include "ui/panes/video_pane.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <memory>
#include <set>
#include <string>
#include <string_view>
#include <vector>

#include <GL/gl.h>

#include "imgui.h"
#include "app/application.h"
#include "core/app_state.h"
#include "msgq/visionipc/visionipc_client.h"
#include "sources/replay_source.h"
#include "tools/replay/replay.h"
#include "ui/bootstrap_icons.h"
#include "ui/tools_windows.h"

namespace cabana {
namespace panes {

namespace {

constexpr float kVideoMinHeight = 100.0f;
constexpr float kVideoPreferredFraction = 0.58f;
constexpr float kTimelineHeight = 16.0f;

const std::array<float, 11> kPlaybackSpeeds = {
    0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.5f, 0.8f, 1.0f, 2.0f, 3.0f, 5.0f,
};

const std::array<ImU32, 6> kTimelineColors = {
    IM_COL32(111, 143, 175, 255),
    IM_COL32(0, 163, 108, 255),
    IM_COL32(0, 255, 0, 255),
    IM_COL32(255, 195, 0, 255),
    IM_COL32(199, 0, 57, 255),
    IM_COL32(255, 0, 255, 255),
};

static bool icon_button(const char *id, std::string_view icon_id) {
  const char *g = cabana::icons::glyph(icon_id);
  if (g[0] == '\0') return ImGui::Button(id);
  std::string label = std::string(g) + "##" + id;
  return ImGui::Button(label.c_str());
}

static std::string ellipsize_middle(const std::string &text, float max_width) {
  if (text.empty() || ImGui::CalcTextSize(text.c_str()).x <= max_width) {
    return text;
  }

  constexpr const char *ellipsis = "...";
  if (ImGui::CalcTextSize(ellipsis).x > max_width) {
    return {};
  }

  size_t head = text.size() / 2;
  size_t tail = text.size() - head;
  while (head > 2 && tail > 2) {
    std::string result = text.substr(0, head) + ellipsis + text.substr(text.size() - tail);
    if (ImGui::CalcTextSize(result.c_str()).x <= max_width) {
      return result;
    }
    if (head >= tail) {
      --head;
    } else {
      --tail;
    }
  }

  return ellipsis;
}

static void render_meta_row(const char *label, const std::string &value) {
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("%s", label);
  ImGui::SameLine(74.0f);
  const float max_width = std::max(40.0f, ImGui::GetContentRegionAvail().x);
  const std::string display = ellipsize_middle(value, max_width);
  ImGui::TextUnformatted(display.c_str());
  if (display != value && ImGui::IsItemHovered()) {
    ImGui::SetTooltip("%s", value.c_str());
  }
}

static void draw_empty_video_state(const char *headline, const char *detail) {
  ImVec2 avail = ImGui::GetContentRegionAvail();
  float video_h = std::max(120.0f, avail.y * 0.55f);
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(p, ImVec2(p.x + avail.x, p.y + video_h), IM_COL32(7, 7, 7, 255), 2.0f);
  dl->AddRect(p, ImVec2(p.x + avail.x, p.y + video_h), IM_COL32(70, 70, 70, 255), 2.0f);

  const ImVec2 headline_size = ImGui::CalcTextSize(headline);
  const ImVec2 detail_size = ImGui::CalcTextSize(detail);
  const float center_x = p.x + avail.x * 0.5f;
  const float center_y = p.y + video_h * 0.5f;

  dl->AddText(ImVec2(center_x - headline_size.x * 0.5f, center_y - 16.0f),
              IM_COL32(208, 208, 208, 255), headline);
  dl->AddText(ImVec2(center_x - detail_size.x * 0.5f, center_y + 6.0f),
              IM_COL32(145, 145, 145, 255), detail);
  ImGui::Dummy(ImVec2(avail.x, video_h));
}

static std::string format_time(double seconds) {
  const int minutes = static_cast<int>(seconds) / 60;
  const double remaining = seconds - minutes * 60;
  char buffer[64];
  std::snprintf(buffer, sizeof(buffer), "%02d:%06.3f", minutes, remaining);
  return buffer;
}

static std::string camera_label(VisionStreamType stream) {
  switch (stream) {
    case VISION_STREAM_ROAD: return "Road Camera";
    case VISION_STREAM_DRIVER: return "Driver Camera";
    case VISION_STREAM_WIDE_ROAD: return "Wide Road Camera";
    default: return "Camera";
  }
}

static cabana::ReplaySource *replay_source_for(cabana::Source *src) {
  return dynamic_cast<cabana::ReplaySource *>(src);
}

static const Replay *replay_for(cabana::Source *src) {
  if (auto *replay_src = replay_source_for(src)) {
    return replay_src->replay();
  }
  return nullptr;
}

static uint8_t clamp_color(int value) {
  return static_cast<uint8_t>(std::clamp(value, 0, 255));
}

class VideoStreamRenderer {
public:
  void sync(cabana::Source *src, bool enabled) {
    if (src != bound_source_ || enabled != enabled_) {
      resetClient();
      available_streams_.clear();
      if (src != bound_source_) {
        requested_stream_ = VISION_STREAM_ROAD;
      }
      bound_source_ = src;
      enabled_ = enabled;
    }

    if (!src || !enabled || src->liveStreaming()) {
      return;
    }

    available_streams_ = VisionIpcClient::getAvailableStreams("camerad", false);
    if (!available_streams_.empty() && available_streams_.count(requested_stream_) == 0) {
      requested_stream_ = *available_streams_.begin();
      resetClient();
    }

    if (!client_) {
      client_ = std::make_unique<VisionIpcClient>("camerad", requested_stream_, true);
    }
    if (!client_->connected && !client_->connect(false)) {
      return;
    }

    VisionIpcBufExtra extra = {};
    VisionBuf *frame = client_->recv(&extra, 5);
    while (VisionBuf *next = client_->recv(&extra, 0)) {
      frame = next;
    }
    if (!frame) {
      return;
    }

    const uint64_t frame_id = frame->get_frame_id();
    if (has_frame_ && frame_id == last_frame_id_ &&
        frame_width_ == static_cast<int>(frame->width) &&
        frame_height_ == static_cast<int>(frame->height)) {
      return;
    }

    uploadFrame(frame);
  }

  void setRequestedStream(VisionStreamType stream) {
    if (requested_stream_ != stream) {
      requested_stream_ = stream;
      resetClient();
    }
  }

  bool hasFrame() const { return has_frame_; }
  bool hasAvailableStreams() const { return !available_streams_.empty(); }
  VisionStreamType requestedStream() const { return requested_stream_; }
  const std::set<VisionStreamType> &availableStreams() const { return available_streams_; }
  GLuint texture() const { return texture_; }
  int frameWidth() const { return frame_width_; }
  int frameHeight() const { return frame_height_; }

private:
  void resetClient() {
    client_.reset();
    has_frame_ = false;
    last_frame_id_ = std::numeric_limits<uint64_t>::max();
  }

  void ensureTexture() {
    if (texture_ != 0) {
      return;
    }

    glGenTextures(1, &texture_);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  void uploadFrame(VisionBuf *frame) {
    ensureTexture();

    frame_width_ = frame->width;
    frame_height_ = frame->height;
    rgba_.resize(static_cast<size_t>(frame_width_) * frame_height_ * 4);
    convertNv12ToRgba(*frame, rgba_);

    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frame_width_, frame_height_, 0, GL_RGBA,
                 GL_UNSIGNED_BYTE, rgba_.data());
    glBindTexture(GL_TEXTURE_2D, 0);

    has_frame_ = true;
    last_frame_id_ = frame->get_frame_id();
  }

  static void convertNv12ToRgba(const VisionBuf &frame, std::vector<uint8_t> &rgba) {
    for (int y = 0; y < static_cast<int>(frame.height); ++y) {
      const uint8_t *y_row = frame.y + static_cast<size_t>(y) * frame.stride;
      const uint8_t *uv_row = frame.uv + static_cast<size_t>(y / 2) * frame.stride;
      uint8_t *rgba_row = rgba.data() + static_cast<size_t>(y) * frame.width * 4;

      for (int x = 0; x < static_cast<int>(frame.width); ++x) {
        const int yy = std::max(0, static_cast<int>(y_row[x]) - 16);
        const int u = static_cast<int>(uv_row[(x / 2) * 2]) - 128;
        const int v = static_cast<int>(uv_row[(x / 2) * 2 + 1]) - 128;

        const int r = (298 * yy + 409 * v + 128) >> 8;
        const int g = (298 * yy - 100 * u - 208 * v + 128) >> 8;
        const int b = (298 * yy + 516 * u + 128) >> 8;

        rgba_row[x * 4 + 0] = clamp_color(r);
        rgba_row[x * 4 + 1] = clamp_color(g);
        rgba_row[x * 4 + 2] = clamp_color(b);
        rgba_row[x * 4 + 3] = 255;
      }
    }
  }

  const cabana::Source *bound_source_ = nullptr;
  bool enabled_ = false;
  VisionStreamType requested_stream_ = VISION_STREAM_ROAD;
  std::unique_ptr<VisionIpcClient> client_;
  std::set<VisionStreamType> available_streams_;
  GLuint texture_ = 0;
  int frame_width_ = 0;
  int frame_height_ = 0;
  bool has_frame_ = false;
  uint64_t last_frame_id_ = std::numeric_limits<uint64_t>::max();
  std::vector<uint8_t> rgba_;
};

static VideoStreamRenderer g_video_stream;

static void render_camera_tabs(VideoStreamRenderer &renderer) {
  if (renderer.availableStreams().size() <= 1) {
    return;
  }

  for (VisionStreamType stream : renderer.availableStreams()) {
    if (stream >= VISION_STREAM_MAX) {
      continue;
    }

    if (stream != *renderer.availableStreams().begin()) {
      ImGui::SameLine();
    }
    const bool selected = renderer.requestedStream() == stream;
    if (ImGui::Selectable(camera_label(stream).c_str(), selected, 0, ImVec2(0, 0))) {
      renderer.setRequestedStream(stream);
    }
  }
  ImGui::Separator();
}

static void draw_alert_overlay(const Replay *replay, double seconds,
                               const ImVec2 &image_min, const ImVec2 &image_max) {
  if (!replay) {
    return;
  }

  const auto alert = replay->findAlertAtTime(seconds);
  if (!alert.has_value()) {
    return;
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  const ImVec2 padding(10.0f, 10.0f);
  const float max_width = std::max(80.0f, (image_max.x - image_min.x) - padding.x * 2.0f);
  std::string text = alert->text1;
  if (!alert->text2.empty()) {
    text += "\n" + alert->text2;
  }

  const ImVec2 text_size = ImGui::CalcTextSize(text.c_str(), nullptr, false, max_width);
  const ImVec2 rect_min(image_min.x + padding.x, image_min.y + padding.y);
  const ImVec2 rect_max(rect_min.x + max_width, rect_min.y + text_size.y + 12.0f);
  dl->AddRectFilled(rect_min, rect_max, kTimelineColors[static_cast<int>(alert->type)], 4.0f);
  dl->AddText(ImVec2(rect_min.x + 6.0f, rect_min.y + 6.0f), IM_COL32(255, 255, 255, 255), text.c_str());
}

static void render_video_frame(VideoStreamRenderer &renderer, cabana::Source *src, const Replay *replay) {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float max_width = std::max(1.0f, avail.x);
  const float max_height = std::max(kVideoMinHeight, avail.y * kVideoPreferredFraction);
  const float aspect = renderer.frameHeight() > 0 ? (float)renderer.frameWidth() / renderer.frameHeight() : 1.0f;

  ImVec2 image_size(max_width, std::min(max_height, max_width / std::max(0.1f, aspect)));
  if (image_size.y < kVideoMinHeight) {
    image_size.y = kVideoMinHeight;
    image_size.x = std::min(max_width, image_size.y * aspect);
  }

  const float x_offset = (max_width - image_size.x) * 0.5f;
  if (x_offset > 0.0f) {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + x_offset);
  }

  const ImVec2 image_min = ImGui::GetCursorScreenPos();
  ImGui::Image(static_cast<ImTextureID>(renderer.texture()), image_size);
  const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);

  if (ImGui::IsItemHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Left) && src) {
    src->pause(!src->isPaused());
  }

  if (src && src->isPaused()) {
    const char *label = "PAUSED";
    const ImVec2 text_size = ImGui::CalcTextSize(label);
    ImGui::GetWindowDrawList()->AddText(
        ImVec2(image_min.x + (image_size.x - text_size.x) * 0.5f,
               image_min.y + (image_size.y - text_size.y) * 0.5f),
        IM_COL32(235, 235, 235, 255), label);
  }

  draw_alert_overlay(replay, src ? src->currentSec() : 0.0, image_min, image_max);
}

static void render_scrub_bar(cabana::Source *src, const Replay *replay, const cabana::AppState &st) {
  const ImVec2 tl_pos = ImGui::GetCursorScreenPos();
  const float tl_w = std::max(20.0f, ImGui::GetContentRegionAvail().x);
  const ImVec2 rect_min = tl_pos;
  const ImVec2 rect_max(tl_pos.x + tl_w, tl_pos.y + kTimelineHeight);
  const float rect_w = rect_max.x - rect_min.x;
  ImDrawList *dl = ImGui::GetWindowDrawList();
  dl->AddRectFilled(rect_min, rect_max, IM_COL32(38, 38, 38, 255), 3.0f);
  if (replay && st.max_sec > st.min_sec) {
    auto fill_range = [&](double start, double end, ImU32 color) {
      const double span = std::max(0.001, st.max_sec - st.min_sec);
      const float x1 = rect_min.x + static_cast<float>((start - st.min_sec) / span) * rect_w;
      const float x2 = rect_min.x + static_cast<float>((end - st.min_sec) / span) * rect_w;
      dl->AddRectFilled(ImVec2(std::clamp(x1, rect_min.x, rect_max.x), rect_min.y),
                        ImVec2(std::clamp(std::max(x1 + 1.0f, x2), rect_min.x, rect_max.x), rect_max.y),
                        color, 0.0f);
    };

    for (const auto &entry : *replay->getTimeline()) {
      fill_range(entry.start_time, entry.end_time, kTimelineColors[static_cast<int>(entry.type)]);
    }
  }

  float marker_x = rect_min.x;
  if (st.max_sec > st.min_sec) {
    marker_x = rect_min.x + static_cast<float>((st.current_sec - st.min_sec) / (st.max_sec - st.min_sec)) * rect_w;
  }
  marker_x = std::clamp(marker_x, rect_min.x, rect_max.x);
  dl->AddLine(ImVec2(marker_x, rect_min.y - 2.0f), ImVec2(marker_x, rect_max.y + 2.0f),
              IM_COL32(248, 248, 248, 255), 2.0f);

  ImGui::InvisibleButton("##video_timeline", ImVec2(rect_w, kTimelineHeight));
  if (src && (ImGui::IsItemClicked(ImGuiMouseButton_Left) || ImGui::IsItemActive())) {
    const float ratio = std::clamp((ImGui::GetIO().MousePos.x - rect_min.x) / rect_w, 0.0f, 1.0f);
    src->seekTo(st.min_sec + (st.max_sec - st.min_sec) * ratio);
  }

  if (ImGui::IsItemHovered() && st.max_sec > st.min_sec) {
    const float ratio = std::clamp((ImGui::GetIO().MousePos.x - rect_min.x) / rect_w, 0.0f, 1.0f);
    const double hover_sec = st.min_sec + (st.max_sec - st.min_sec) * ratio;
    std::string tooltip = format_time(hover_sec);
    if (replay) {
      if (const auto alert = replay->findAlertAtTime(hover_sec); alert.has_value() && !alert->text1.empty()) {
        tooltip += "\n" + alert->text1;
      }
    }
    ImGui::SetTooltip("%s", tooltip.c_str());
  }
}

static void render_speed_control(cabana::Source *src, float current_speed) {
  if (!src) {
    ImGui::BeginDisabled();
  }

  char label[32];
  std::snprintf(label, sizeof(label), "%.2gx", current_speed);
  if (ImGui::BeginCombo("##video_speed", label)) {
    for (float speed : kPlaybackSpeeds) {
      const bool selected = std::fabs(speed - current_speed) < 0.0001f;
      char choice[32];
      std::snprintf(choice, sizeof(choice), "%.2gx", speed);
      if (ImGui::Selectable(choice, selected) && src) {
        src->setSpeed(speed);
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }

  if (!src) {
    ImGui::EndDisabled();
  }
}

}  // namespace

void video() {
  ImGui::Begin("Video");

  auto &st = cabana::app_state();
  auto *src = app() ? app()->source() : nullptr;
  const auto *replay = replay_for(src);
  const bool video_enabled = app() ? app()->videoEnabled() : true;

  g_video_stream.sync(src, video_enabled);

  if (!st.route_name.empty()) {
    render_meta_row("Route", st.route_name);
    if (!st.car_fingerprint.empty()) {
      render_meta_row("Fingerprint", st.car_fingerprint);
    }
  } else {
    render_meta_row("Route", st.route_loading ? "Loading route..." : "No route loaded");
  }

  if (!st.route_load_error.empty()) {
    ImGui::TextColored(ImVec4(0.85f, 0.35f, 0.35f, 1.0f), "load failed");
  }

  ImGui::Separator();

  if (!src) {
    draw_empty_video_state(st.route_loading ? "Loading route..." : "Video unavailable",
                           st.route_loading ? "Waiting for replay data" : "Open a route to view video");
  } else if (src->liveStreaming()) {
    draw_empty_video_state("Video unavailable", "Live CAN streaming is wired, live video is still pending");
  } else if (!video_enabled) {
    draw_empty_video_state("Video disabled", "Replay started with --no-vipc");
  } else if (!g_video_stream.hasAvailableStreams()) {
    draw_empty_video_state("Waiting for camera stream", "Replay video is loading");
  } else {
    render_camera_tabs(g_video_stream);
    if (g_video_stream.hasFrame()) {
      render_video_frame(g_video_stream, src, replay);
    } else {
      draw_empty_video_state("Waiting for first frame", "Camera stream connected but no frame has arrived yet");
    }
  }

  ImGui::Spacing();
  render_scrub_bar(src, replay, st);
  ImGui::Spacing();

  if (icon_button("rewind", "rewind") && src) {
    src->seekTo(src->currentSec() - 1.0);
  }
  ImGui::SameLine();
  if (icon_button("playpause", st.paused ? "play" : "pause") && src) {
    src->pause(!st.paused);
  }
  ImGui::SameLine();
  if (icon_button("ff", "fast-forward") && src) {
    src->seekTo(src->currentSec() + 1.0);
  }
  ImGui::SameLine();
  if (icon_button("skip", "skip-end") && src) {
    if (src->liveStreaming()) {
      src->pause(false);
    }
    src->seekTo(st.max_sec + 1.0);
  }
  ImGui::SameLine();
  ImGui::Text("%s / %s", format_time(st.current_sec).c_str(), format_time(st.max_sec).c_str());

  ImGui::SameLine();
  if (replay) {
    if (icon_button("loop", replay->loop() ? "repeat" : "repeat-1")) {
      const_cast<Replay *>(replay)->setLoop(!replay->loop());
    }
    ImGui::SameLine();
  }

  render_speed_control(src, st.speed);
  ImGui::SameLine();
  if (icon_button("info", "info-circle")) {
    cabana::tools_windows::requestRouteInfo();
  }

  ImGui::End();
}

}  // namespace panes
}  // namespace cabana
