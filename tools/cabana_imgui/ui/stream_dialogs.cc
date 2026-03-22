#include "ui/stream_dialogs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#include "imgui.h"

#include "app/application.h"
#include "core/app_state.h"
#include "sources/panda_source.h"
#include "sources/socketcan_source.h"
#include "tools/cabana/panda.h"

namespace cabana {
namespace stream_dialogs {

namespace {

constexpr size_t kPathBufferSize = 2048;

enum class StreamTab {
  Replay = 0,
  Panda,
  SocketCan,
  Device,
};

struct OpenStreamState {
  bool requested = false;
  bool open = false;
  bool focus_route = false;
  StreamTab tab = StreamTab::Replay;
  bool replay_auto = false;
  bool replay_no_vipc = false;
  bool replay_qcam = false;
  bool replay_ecam = false;
  bool replay_dcam = false;
  bool device_use_zmq = true;
  int panda_serial_index = 0;
  int socketcan_device_index = 0;
  std::array<BusConfig, 3> panda_buses = {};
  std::vector<std::string> panda_serials;
  std::vector<std::string> socketcan_devices;
  char route[kPathBufferSize] = {};
  char data_dir[kPathBufferSize] = {};
  char dbc_path[kPathBufferSize] = {};
  char zmq_address[kPathBufferSize] = "127.0.0.1";
  std::string error;
};

OpenStreamState g_open_stream;

void copy_text(char *buffer, size_t size, const std::string &value) {
  std::snprintf(buffer, size, "%s", value.c_str());
}

std::string trim_copy(const char *value) {
  std::string text = value ? value : "";
  auto not_space = [](unsigned char c) { return !std::isspace(c); };
  text.erase(text.begin(), std::find_if(text.begin(), text.end(), not_space));
  text.erase(std::find_if(text.rbegin(), text.rend(), not_space).base(), text.end());
  return text;
}

std::vector<std::string> socketcan_devices() {
  std::vector<std::string> devices;
#ifdef __linux__
  namespace fs = std::filesystem;
  const fs::path root("/sys/class/net");
  std::error_code ec;
  if (!fs::exists(root, ec) || ec) {
    return devices;
  }
  for (const auto &entry : fs::directory_iterator(root, ec)) {
    if (ec || !entry.is_directory()) continue;
    std::FILE *fp = std::fopen((entry.path() / "type").c_str(), "r");
    if (!fp) continue;
    int type = 0;
    const int ok = std::fscanf(fp, "%d", &type);
    std::fclose(fp);
    if (ok == 1 && type == 280) {
      devices.push_back(entry.path().filename().string());
    }
  }
  std::sort(devices.begin(), devices.end());
#endif
  return devices;
}

void refresh_sources() {
  auto &state = g_open_stream;
  state.panda_serials = Panda::list();
  if (state.panda_serial_index >= (int)state.panda_serials.size()) {
    state.panda_serial_index = 0;
  }
  state.socketcan_devices = socketcan_devices();
  if (state.socketcan_device_index >= (int)state.socketcan_devices.size()) {
    state.socketcan_device_index = 0;
  }
}

void open_requested_modal() {
  auto &state = g_open_stream;
  if (!state.requested) {
    return;
  }

  state.requested = false;
  state.open = true;
  state.focus_route = true;
  state.error.clear();
  state.tab = StreamTab::Replay;
  state.replay_auto = false;
  state.replay_no_vipc = false;
  state.replay_qcam = false;
  state.replay_ecam = false;
  state.replay_dcam = false;
  state.device_use_zmq = true;
  copy_text(state.route, sizeof(state.route), "");
  copy_text(state.data_dir, sizeof(state.data_dir), "");
  copy_text(state.zmq_address, sizeof(state.zmq_address), "127.0.0.1");

  const auto &app_st = cabana::app_state();
  if (!app_st.active_dbc_file.empty()) {
    copy_text(state.dbc_path, sizeof(state.dbc_path), app_st.active_dbc_file);
  } else if (!app_st.recent_dbc_files.empty()) {
    copy_text(state.dbc_path, sizeof(state.dbc_path), app_st.recent_dbc_files.front());
  } else {
    state.dbc_path[0] = '\0';
  }

  refresh_sources();
  ImGui::OpenPopup("Open Stream");
}

bool open_replay_stream() {
  auto &state = g_open_stream;
  std::string route = trim_copy(state.route);
  if (route.empty()) {
    state.error = "Route is required.";
    return false;
  }

  uint32_t flags = 0;
  if (state.replay_no_vipc) flags |= REPLAY_FLAG_NO_VIPC;
  if (state.replay_qcam) flags |= REPLAY_FLAG_QCAMERA;
  if (state.replay_ecam) flags |= REPLAY_FLAG_ECAM;
  if (state.replay_dcam) flags |= REPLAY_FLAG_DCAM;

  if (!app()) {
    state.error = "Application is not available.";
    return false;
  }

  app()->openRoute(route, trim_copy(state.data_dir), flags, state.replay_auto);
  const std::string dbc_path = trim_copy(state.dbc_path);
  if (!dbc_path.empty()) {
    app()->openDbcFile(dbc_path);
  }
  return true;
}

bool open_panda_stream() {
  auto &state = g_open_stream;
  PandaSourceConfig config = {};
  if (!state.panda_serials.empty() && state.panda_serial_index < (int)state.panda_serials.size()) {
    config.serial = state.panda_serials[state.panda_serial_index];
  }
  config.bus_config.assign(state.panda_buses.begin(), state.panda_buses.end());
  return app() && app()->openPandaStream(config, trim_copy(state.dbc_path));
}

bool open_socketcan_stream() {
  auto &state = g_open_stream;
  if (state.socketcan_devices.empty()) {
    state.error = "No SocketCAN devices were found.";
    return false;
  }
  SocketCanSourceConfig config = {.device = state.socketcan_devices[state.socketcan_device_index]};
  return app() && app()->openSocketCanStream(config, trim_copy(state.dbc_path));
}

bool open_device_stream() {
  auto &state = g_open_stream;
  DeviceSourceConfig config = {
    .use_zmq = state.device_use_zmq,
    .address = trim_copy(state.zmq_address).empty() ? "127.0.0.1" : trim_copy(state.zmq_address),
  };
  return app() && app()->openDeviceStream(config, trim_copy(state.dbc_path));
}

bool submit_open_stream() {
  auto &state = g_open_stream;
  state.error.clear();

  bool ok = false;
  switch (state.tab) {
    case StreamTab::Replay:
      ok = open_replay_stream();
      break;
    case StreamTab::Panda:
      ok = open_panda_stream();
      break;
    case StreamTab::SocketCan:
      ok = open_socketcan_stream();
      break;
    case StreamTab::Device:
      ok = open_device_stream();
      break;
  }

  if (!ok && state.error.empty()) {
    state.error = cabana::app_state().route_load_error.empty() ? "Failed to open stream." : cabana::app_state().route_load_error;
  }
  if (ok) {
    state.open = false;
    ImGui::CloseCurrentPopup();
  }
  return ok;
}

template <size_t N>
void render_speed_combo(const char *label, int *value, const std::array<uint32_t, N> &choices) {
  if (ImGui::BeginCombo(label, std::to_string(*value).c_str())) {
    for (uint32_t choice : choices) {
      const bool selected = *value == (int)choice;
      if (ImGui::Selectable(std::to_string(choice).c_str(), selected)) {
        *value = choice;
      }
      if (selected) {
        ImGui::SetItemDefaultFocus();
      }
    }
    ImGui::EndCombo();
  }
}

}  // namespace

void requestOpen() {
  g_open_stream.requested = true;
}

void render() {
  open_requested_modal();
  auto &state = g_open_stream;
  if (!state.open) {
    return;
  }

  ImGui::SetNextWindowSize(ImVec2(820, 0), ImGuiCond_Appearing);
  if (!ImGui::BeginPopupModal("Open Stream", &state.open,
                              ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoSavedSettings)) {
    return;
  }

  bool submit_requested = false;
  if (ImGui::BeginTabBar("##open_stream_tabs")) {
    if (ImGui::BeginTabItem("Replay")) {
      state.tab = StreamTab::Replay;
      if (state.focus_route) {
        ImGui::SetKeyboardFocusHere();
        state.focus_route = false;
      }
      submit_requested |= ImGui::InputText("Route", state.route, sizeof(state.route),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::SameLine();
      if (ImGui::Button("Demo")) {
        copy_text(state.route, sizeof(state.route), DEMO_ROUTE);
      }
      submit_requested |= ImGui::InputText("Data Dir", state.data_dir, sizeof(state.data_dir),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::Checkbox("Auto Source", &state.replay_auto);
      ImGui::SameLine();
      ImGui::Checkbox("No VIPC", &state.replay_no_vipc);
      ImGui::SameLine();
      ImGui::Checkbox("QCam", &state.replay_qcam);
      ImGui::SameLine();
      ImGui::Checkbox("ECam", &state.replay_ecam);
      ImGui::SameLine();
      ImGui::Checkbox("DCam", &state.replay_dcam);
      ImGui::EndTabItem();
    }

    if (ImGui::BeginTabItem("Panda")) {
      state.tab = StreamTab::Panda;
      if (ImGui::Button("Refresh Pandas")) {
        refresh_sources();
      }
      if (state.panda_serials.empty()) {
        ImGui::TextDisabled("No panda found.");
      } else if (ImGui::BeginCombo("Serial", state.panda_serials[state.panda_serial_index].c_str())) {
        for (int i = 0; i < (int)state.panda_serials.size(); ++i) {
          const bool selected = i == state.panda_serial_index;
          if (ImGui::Selectable(state.panda_serials[i].c_str(), selected)) {
            state.panda_serial_index = i;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }

      for (int bus = 0; bus < (int)state.panda_buses.size(); ++bus) {
        ImGui::PushID(bus);
        ImGui::Separator();
        ImGui::Text("Bus %d", bus);
        render_speed_combo("CAN Speed (kbps)", &state.panda_buses[bus].can_speed_kbps, kCanSpeeds);
        ImGui::Checkbox("CAN-FD", &state.panda_buses[bus].can_fd);
        render_speed_combo("Data Speed (kbps)", &state.panda_buses[bus].data_speed_kbps, kCanDataSpeeds);
        ImGui::PopID();
      }
      ImGui::EndTabItem();
    }

#ifdef __linux__
    if (SocketCanSource::available() && ImGui::BeginTabItem("SocketCAN")) {
      state.tab = StreamTab::SocketCan;
      if (ImGui::Button("Refresh Devices")) {
        refresh_sources();
      }
      if (state.socketcan_devices.empty()) {
        ImGui::TextDisabled("No SocketCAN devices found.");
      } else if (ImGui::BeginCombo("Device", state.socketcan_devices[state.socketcan_device_index].c_str())) {
        for (int i = 0; i < (int)state.socketcan_devices.size(); ++i) {
          const bool selected = i == state.socketcan_device_index;
          if (ImGui::Selectable(state.socketcan_devices[i].c_str(), selected)) {
            state.socketcan_device_index = i;
          }
          if (selected) {
            ImGui::SetItemDefaultFocus();
          }
        }
        ImGui::EndCombo();
      }
      ImGui::EndTabItem();
    }
#endif

    if (ImGui::BeginTabItem("Device")) {
      state.tab = StreamTab::Device;
      int device_mode = state.device_use_zmq ? 1 : 0;
      ImGui::RadioButton("MSGQ", &device_mode, 0);
      ImGui::SameLine();
      ImGui::RadioButton("ZMQ", &device_mode, 1);
      state.device_use_zmq = device_mode == 1;
      ImGui::BeginDisabled(!state.device_use_zmq);
      submit_requested |= ImGui::InputText("ZMQ Address", state.zmq_address, sizeof(state.zmq_address),
                                           ImGuiInputTextFlags_EnterReturnsTrue);
      ImGui::EndDisabled();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::Separator();
  submit_requested |= ImGui::InputText("DBC File", state.dbc_path, sizeof(state.dbc_path),
                                       ImGuiInputTextFlags_EnterReturnsTrue);
  if (!state.error.empty()) {
    ImGui::TextColored(ImVec4(0.95f, 0.38f, 0.38f, 1.0f), "%s", state.error.c_str());
  }

  if (submit_requested || ImGui::Button("Open", ImVec2(120, 0))) {
    submit_open_stream();
  }
  ImGui::SameLine();
  if (ImGui::Button("Cancel", ImVec2(120, 0))) {
    state.open = false;
    state.error.clear();
    ImGui::CloseCurrentPopup();
  }

  ImGui::EndPopup();
}

}  // namespace stream_dialogs
}  // namespace cabana
