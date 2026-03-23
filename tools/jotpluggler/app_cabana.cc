#include "tools/jotpluggler/app_internal.h"

#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <future>
#include <optional>
#include <string_view>
#include <tuple>
#include <unordered_map>
#include <unordered_set>

namespace {

constexpr float kSplitterThickness = 10.0f;
constexpr float kMinMessagesWidth = 210.0f;
constexpr float kMinCenterWidth = 240.0f;
constexpr float kMinRightWidth = 260.0f;
constexpr float kMinTopHeight = 140.0f;
constexpr float kMinBottomHeight = 120.0f;
constexpr std::array<std::array<uint8_t, 3>, 8> kSignalHighlightColors = {{
  {102, 86, 169},
  {69, 137, 255},
  {55, 171, 112},
  {232, 171, 44},
  {198, 89, 71},
  {92, 155, 181},
  {134, 172, 79},
  {150, 112, 63},
}};

const fs::path &cabana_repo_root() {
  static const fs::path root = []() {
#ifdef JOTP_REPO_ROOT
    return fs::path(JOTP_REPO_ROOT);
#else
    return fs::current_path();
#endif
  }();
  return root;
}

bool parse_can_message_path(std::string_view path,
                            std::string_view *service,
                            int *bus,
                            std::string_view *message,
                            std::string_view *signal) {
  if (path.empty() || path.front() != '/') {
    return false;
  }
  size_t a = path.find('/', 1);
  if (a == std::string_view::npos) return false;
  size_t b = path.find('/', a + 1);
  if (b == std::string_view::npos) return false;
  size_t c = path.find('/', b + 1);
  if (c == std::string_view::npos) return false;
  size_t d = path.find('/', c + 1);
  if (d != std::string_view::npos) return false;

  *service = path.substr(1, a - 1);
  if (*service != "can" && *service != "sendcan") {
    return false;
  }
  try {
    *bus = std::stoi(std::string(path.substr(a + 1, b - a - 1)));
  } catch (...) {
    return false;
  }
  *message = path.substr(b + 1, c - b - 1);
  *signal = path.substr(c + 1);
  return !message->empty() && !signal->empty();
}

std::optional<CanServiceKind> parse_can_service_kind(std::string_view service) {
  if (service == "can") return CanServiceKind::Can;
  if (service == "sendcan") return CanServiceKind::Sendcan;
  return std::nullopt;
}

const char *can_service_name(CanServiceKind service) {
  return service == CanServiceKind::Can ? "can" : "sendcan";
}

std::string format_can_address(uint32_t address) {
  char text[32];
  std::snprintf(text, sizeof(text), "0x%X", address);
  return text;
}

std::string can_message_key(CanServiceKind service, uint8_t bus, uint32_t address) {
  return "/" + std::string(can_service_name(service)) + "/" + std::to_string(bus) + "/" + format_can_address(address);
}

std::string sanitize_filename_component(std::string_view text) {
  std::string out;
  out.reserve(text.size());
  for (char c : text) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_') {
      out.push_back(c);
    } else {
      out.push_back('_');
    }
  }
  return out.empty() ? "untitled" : out;
}

fs::path cabana_export_dir() {
  const char *home = std::getenv("HOME");
  fs::path root = home != nullptr ? fs::path(home) : fs::current_path();
  const fs::path downloads = root / "Downloads";
  return (fs::exists(downloads) ? downloads : root) / "jotpluggler_exports";
}

std::string csv_escape(std::string_view text) {
  std::string out;
  out.reserve(text.size() + 2);
  out.push_back('"');
  for (char c : text) {
    if (c == '"') out.push_back('"');
    out.push_back(c);
  }
  out.push_back('"');
  return out;
}

std::string payload_hex(const std::string &data) {
  static constexpr char kHex[] = "0123456789ABCDEF";
  std::string out;
  out.reserve(data.size() * 2);
  for (unsigned char byte : data) {
    out.push_back(kHex[byte >> 4]);
    out.push_back(kHex[byte & 0xF]);
  }
  return out;
}

fs::path cabana_export_path(const AppSession &session,
                            const CabanaMessageSummary &message,
                            std::string_view kind) {
  const std::string route_part = sanitize_filename_component(session.route_name.empty() ? "stream" : session.route_name);
  char filename[256];
  std::snprintf(filename, sizeof(filename), "%s_%s_bus%d_0x%X_%.*s.csv",
                sanitize_filename_component(message.name).c_str(),
                sanitize_filename_component(message.service).c_str(),
                message.bus,
                message.address,
                static_cast<int>(kind.size()),
                kind.data());
  return cabana_export_dir() / route_part / filename;
}

std::optional<dbc::Database> load_active_dbc(const AppSession &session) {
  const std::string &dbc_name = !session.dbc_override.empty() ? session.dbc_override : session.route_data.dbc_name;
  if (dbc_name.empty()) {
    return std::nullopt;
  }
  for (const fs::path &candidate : {
         cabana_repo_root() / "opendbc" / "dbc" / (dbc_name + ".dbc"),
         cabana_repo_root() / "tools" / "jotpluggler" / "generated_dbcs" / (dbc_name + ".dbc"),
       }) {
    if (fs::exists(candidate)) {
      return std::optional<dbc::Database>(std::in_place, candidate);
    }
  }
  return std::nullopt;
}

struct DbcNameLookup {
  std::unordered_map<std::string, uint32_t> unique_name_to_address;
};

struct BitBehaviorStats {
  double ones_ratio = 0.0;
  double flip_ratio = 0.0;
  size_t samples = 0;
};

struct BinaryMatrixLayout {
  size_t byte_count = 0;
  std::vector<std::vector<int>> cell_signals;
  std::vector<bool> is_msb;
  std::vector<bool> is_lsb;
  size_t overlapping_cells = 0;
};

bool signal_contains_bit(const CabanaSignalSummary &signal, size_t byte_index, int bit_index);
void sync_cabana_selection(AppSession *session, UiState *state);
void clear_similar_bit_results(UiState *state);

DbcNameLookup build_dbc_name_lookup(const std::optional<dbc::Database> &db) {
  DbcNameLookup out;
  if (!db.has_value()) {
    return out;
  }
  std::unordered_set<std::string> duplicates;
  for (const auto &[address, message] : db->messages()) {
    auto [it, inserted] = out.unique_name_to_address.emplace(message.name, address);
    if (!inserted) {
      duplicates.insert(message.name);
      out.unique_name_to_address.erase(it);
    }
  }
  for (const std::string &name : duplicates) {
    out.unique_name_to_address.erase(name);
  }
  return out;
}

bool contains_case_insensitive(std::string_view haystack, std::string_view needle) {
  if (needle.empty()) {
    return true;
  }
  const std::string hay = lowercase(haystack);
  const std::string ndl = lowercase(needle);
  return hay.find(ndl) != std::string::npos;
}

const CanMessageData *find_message_data(const AppSession &session, const CabanaMessageSummary &message) {
  const std::optional<CanServiceKind> service = parse_can_service_kind(message.service);
  if (!service.has_value()) {
    return nullptr;
  }
  const CanMessageData key{.id = CanMessageId{*service, static_cast<uint8_t>(message.bus), message.address}};
  auto it = std::lower_bound(session.route_data.can_messages.begin(),
                             session.route_data.can_messages.end(),
                             key,
                             [](const CanMessageData &a, const CanMessageData &b) {
                               return std::make_tuple(a.id.service, a.id.bus, a.id.address)
                                    < std::make_tuple(b.id.service, b.id.bus, b.id.address);
                             });
  if (it == session.route_data.can_messages.end()
      || it->id.service != key.id.service
      || it->id.bus != key.id.bus
      || it->id.address != key.id.address) {
    return nullptr;
  }
  return &*it;
}

void open_cabana_signal_editor(const AppSession &session,
                               UiState *state,
                               const CabanaMessageSummary &message,
                               const CabanaSignalSummary &signal) {
  const std::optional<dbc::Database> db = load_active_dbc(session);
  const dbc::Message *dbc_message = db.has_value() ? db->message(message.address) : nullptr;
  if (dbc_message == nullptr) {
    state->error_text = "No active DBC message available for editing";
    state->open_error_popup = true;
    return;
  }
  auto it = std::find_if(dbc_message->signals.begin(), dbc_message->signals.end(), [&](const dbc::Signal &dbc_signal) {
    return dbc_signal.name == signal.name;
  });
  if (it == dbc_message->signals.end()) {
    state->error_text = "Signal not found in active DBC";
    state->open_error_popup = true;
    return;
  }

  CabanaSignalEditorState &editor = state->cabana_signal_editor;
  editor.open = true;
  editor.loaded = true;
  editor.creating = false;
  editor.message_root = message.root_path;
  editor.message_name = message.name;
  editor.service = message.service;
  editor.bus = message.bus;
  editor.message_address = message.address;
  editor.original_signal_name = it->name;
  editor.signal_name = it->name;
  editor.start_bit = it->start_bit;
  editor.size = it->size;
  editor.factor = it->factor;
  editor.offset = it->offset;
  editor.min = it->min;
  editor.max = it->max;
  editor.is_signed = it->is_signed;
  editor.is_little_endian = it->is_little_endian;
  editor.type = static_cast<int>(it->type);
  editor.multiplex_value = it->multiplex_value;
  editor.receiver_name = it->receiver_name;
  editor.unit = it->unit;
}

void open_cabana_new_signal_editor(const AppSession &session,
                                   UiState *state,
                                   const CabanaMessageSummary &message,
                                   int byte_index,
                                   int bit_index) {
  const std::optional<dbc::Database> db = load_active_dbc(session);
  const dbc::Message *dbc_message = db.has_value() ? db->message(message.address) : nullptr;
  if (dbc_message == nullptr) {
    state->error_text = "No active DBC message available for creating a signal";
    state->open_error_popup = true;
    return;
  }

  std::string base_name = "bit_" + std::to_string(byte_index) + "_" + std::to_string(bit_index);
  std::string signal_name = base_name;
  int suffix = 2;
  auto exists = [&](std::string_view candidate) {
    return std::any_of(dbc_message->signals.begin(), dbc_message->signals.end(), [&](const dbc::Signal &signal) {
      return signal.name == candidate;
    });
  };
  while (exists(signal_name)) {
    signal_name = base_name + "_" + std::to_string(suffix++);
  }

  CabanaSignalEditorState &editor = state->cabana_signal_editor;
  editor.open = true;
  editor.loaded = true;
  editor.creating = true;
  editor.message_root = message.root_path;
  editor.message_name = message.name;
  editor.service = message.service;
  editor.bus = message.bus;
  editor.message_address = message.address;
  editor.original_signal_name.clear();
  editor.signal_name = signal_name;
  editor.start_bit = byte_index * 8 + bit_index;
  editor.size = 1;
  editor.factor = 1.0;
  editor.offset = 0.0;
  editor.min = 0.0;
  editor.max = 1.0;
  editor.is_signed = false;
  editor.is_little_endian = true;
  editor.type = static_cast<int>(dbc::Signal::Type::Normal);
  editor.multiplex_value = 0;
  editor.receiver_name = "XXX";
  editor.unit.clear();
}

const CabanaMessageSummary *find_selected_message(const AppSession &session, const UiState &state) {
  auto it = std::find_if(session.cabana_messages.begin(), session.cabana_messages.end(), [&](const CabanaMessageSummary &message) {
    return message.root_path == state.cabana.selected_message_root;
  });
  return it == session.cabana_messages.end() ? nullptr : &*it;
}

const CabanaMessageSummary *find_message_by_root(const AppSession &session, std::string_view root_path) {
  auto it = std::find_if(session.cabana_messages.begin(), session.cabana_messages.end(), [&](const CabanaMessageSummary &message) {
    return message.root_path == root_path;
  });
  return it == session.cabana_messages.end() ? nullptr : &*it;
}

void select_cabana_message(AppSession *session, UiState *state, std::string_view root_path) {
  state->cabana.selected_message_root.assign(root_path);
  if (std::find(state->cabana.open_message_roots.begin(), state->cabana.open_message_roots.end(), root_path)
      == state->cabana.open_message_roots.end()) {
    state->cabana.open_message_roots.emplace_back(root_path);
  }
  state->cabana.signal_filter[0] = '\0';
  state->cabana.chart_signal_paths.clear();
  sync_cabana_selection(session, state);
}

void close_cabana_message_tab(AppSession *session, UiState *state, std::string_view root_path) {
  auto &roots = state->cabana.open_message_roots;
  auto it = std::find(roots.begin(), roots.end(), root_path);
  if (it == roots.end()) {
    return;
  }
  const bool closing_selected = state->cabana.selected_message_root == root_path;
  const size_t index = static_cast<size_t>(it - roots.begin());
  roots.erase(it);
  if (!closing_selected) {
    return;
  }
  if (roots.empty()) {
    state->cabana.selected_message_root.clear();
    state->cabana.chart_signal_paths.clear();
    state->cabana.has_bit_selection = false;
    clear_similar_bit_results(state);
    return;
  }
  const size_t next_index = std::min(index, roots.size() - 1);
  select_cabana_message(session, state, roots[next_index]);
}

bool similar_bit_results_match_selection(const UiState &state) {
  return state.cabana.has_bit_selection
      && state.cabana.similar_bits_source_root == state.cabana.selected_message_root
      && state.cabana.similar_bits_source_byte == state.cabana.selected_bit_byte
      && state.cabana.similar_bits_source_bit == state.cabana.selected_bit_index;
}

void clear_similar_bit_results(UiState *state) {
  state->cabana.similar_bits_source_root.clear();
  state->cabana.similar_bits_source_byte = -1;
  state->cabana.similar_bits_source_bit = -1;
  state->cabana.similar_bit_matches.clear();
}

void poll_similar_bit_search(UiState *state) {
  if (!state->cabana.similar_bits_loading || !state->cabana.similar_bit_future.valid()) {
    return;
  }
  using namespace std::chrono_literals;
  if (state->cabana.similar_bit_future.wait_for(0ms) != std::future_status::ready) {
    return;
  }
  std::vector<CabanaSimilarBitMatch> matches = state->cabana.similar_bit_future.get();
  state->cabana.similar_bits_loading = false;
  if (similar_bit_results_match_selection(*state)) {
    state->cabana.similar_bit_matches = std::move(matches);
  } else {
    clear_similar_bit_results(state);
  }
}

void sync_cabana_selection(AppSession *session, UiState *state) {
  poll_similar_bit_search(state);
  if (!state->cabana_mode_initialized) {
    state->cabana.camera_view = sidebar_preview_camera_view(*session);
    state->cabana_mode_initialized = true;
  }
  auto &open_roots = state->cabana.open_message_roots;
  open_roots.erase(std::remove_if(open_roots.begin(), open_roots.end(), [&](const std::string &root_path) {
                     return find_message_by_root(*session, root_path) == nullptr;
                   }),
                   open_roots.end());
  if (session->cabana_messages.empty()) {
    state->cabana.selected_message_root.clear();
    state->cabana.open_message_roots.clear();
    state->cabana.chart_signal_paths.clear();
    state->cabana.has_bit_selection = false;
    clear_similar_bit_results(state);
    return;
  }
  const CabanaMessageSummary *selected = find_selected_message(*session, *state);
  if (selected == nullptr) {
    state->cabana.selected_message_root.clear();
    state->cabana.chart_signal_paths.clear();
    state->cabana.has_bit_selection = false;
    clear_similar_bit_results(state);
    return;
  }

  std::unordered_set<std::string> allowed;
  for (const CabanaSignalSummary &signal : selected->signals) {
    allowed.insert(signal.path);
  }
  state->cabana.chart_signal_paths.erase(
    std::remove_if(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(),
                   [&](const std::string &path) { return !allowed.count(path); }),
    state->cabana.chart_signal_paths.end());

}

std::string format_cabana_time(double seconds) {
  seconds = std::max(0.0, seconds);
  const int total = static_cast<int>(seconds);
  const int minutes = total / 60;
  const int secs = total % 60;
  char text[32];
  std::snprintf(text, sizeof(text), "%02d:%02d", minutes, secs);
  return text;
}

void draw_cabana_panel_title(const char *title, std::string_view subtitle = {}) {
  app_push_bold_font();
  ImGui::TextUnformatted(title);
  app_pop_bold_font();
  if (!subtitle.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("%.*s", static_cast<int>(subtitle.size()), subtitle.data());
  }
  ImGui::Spacing();
}

bool draw_cabana_bottom_tab(const char *id, const char *label, bool active, float width) {
  ImGui::PushStyleColor(ImGuiCol_Button, active ? color_rgb(255, 255, 255) : color_rgb(239, 242, 246));
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, active ? color_rgb(255, 255, 255) : color_rgb(245, 247, 250));
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_rgb(255, 255, 255));
  const bool clicked = ImGui::Button((std::string(label) + id).c_str(), ImVec2(width, 26.0f));
  ImGui::PopStyleColor(3);
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(active ? color_rgb(191, 197, 205) : color_rgb(210, 216, 222)));
  if (active) {
    draw->AddLine(ImVec2(rect.Min.x + 1.0f, rect.Max.y), ImVec2(rect.Max.x - 1.0f, rect.Max.y),
                  ImGui::GetColorU32(color_rgb(255, 255, 255)), 2.0f);
  }
  return clicked;
}

void draw_cabana_detail_tab_strip(UiState *state) {
  const float strip_h = 30.0f;
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 4.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::BeginChild("##cabana_detail_bottom_tabs", ImVec2(0.0f, strip_h), false, ImGuiWindowFlags_NoScrollbar);
  const ImVec2 pos = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const ImRect rect(pos, ImVec2(pos.x + size.x, pos.y + size.y));
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddLine(ImVec2(rect.Min.x, rect.Min.y + 1.0f), ImVec2(rect.Max.x, rect.Min.y + 1.0f),
                ImGui::GetColorU32(color_rgb(207, 212, 219)));
  ImGui::SetCursorPosX(8.0f);
  if (draw_cabana_bottom_tab("##msg", "Msg", state->cabana.detail_tab == 0, 72.0f)) {
    state->cabana.detail_tab = 0;
  }
  ImGui::SameLine(0.0f, 4.0f);
  if (draw_cabana_bottom_tab("##logs", "Logs", state->cabana.detail_tab == 1, 76.0f)) {
    state->cabana.detail_tab = 1;
  }
  ImGui::EndChild();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar();
}

void draw_cabana_message_tabs(AppSession *session, UiState *state) {
  auto &roots = state->cabana.open_message_roots;
  if (roots.empty()) {
    return;
  }
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(244, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_message_tabs", ImVec2(0.0f, 32.0f), false, ImGuiWindowFlags_HorizontalScrollbar);
  for (size_t i = 0; i < roots.size(); ++i) {
    const CabanaMessageSummary *message = find_message_by_root(*session, roots[i]);
    if (message == nullptr) {
      continue;
    }
    if (i > 0) ImGui::SameLine(0.0f, 4.0f);
    const bool active = state->cabana.selected_message_root == roots[i];
    const std::string label = message->name + "###cabana_tab_" + roots[i];
    ImGui::PushStyleColor(ImGuiCol_Button, active ? color_rgb(255, 255, 255) : color_rgb(236, 239, 243));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, color_rgb(249, 250, 252));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, color_rgb(255, 255, 255));
    if (ImGui::Button(label.c_str(), ImVec2(0.0f, 24.0f))) {
      select_cabana_message(session, state, roots[i]);
    }
    ImGui::PopStyleColor(3);
    const ImRect tab_rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
    ImDrawList *draw = ImGui::GetWindowDrawList();
    draw->AddRect(tab_rect.Min, tab_rect.Max, ImGui::GetColorU32(active ? color_rgb(188, 194, 202) : color_rgb(207, 213, 220)));
    if (active) {
      draw->AddLine(ImVec2(tab_rect.Min.x + 1.0f, tab_rect.Max.y), ImVec2(tab_rect.Max.x - 1.0f, tab_rect.Max.y),
                    ImGui::GetColorU32(color_rgb(255, 255, 255)), 2.0f);
    }
    ImGui::SameLine(0.0f, 0.0f);
    const std::string close_id = "x##cabana_tab_close_" + roots[i];
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 20.0f);
    if (ImGui::SmallButton(close_id.c_str())) {
      close_cabana_message_tab(session, state, roots[i]);
      --i;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
}

void draw_cabana_welcome_panel() {
  const ImVec2 avail = ImGui::GetContentRegionAvail();
  const float center_x = ImGui::GetCursorPosX() + avail.x * 0.5f;
  ImGui::Dummy(ImVec2(0.0f, std::max(28.0f, avail.y * 0.18f)));
  app_push_bold_font();
  const char *title = "CABANA";
  const float title_w = ImGui::CalcTextSize(title).x;
  ImGui::SetCursorPosX(std::max(0.0f, center_x - title_w * 0.5f));
  ImGui::TextUnformatted(title);
  app_pop_bold_font();
  ImGui::Spacing();
  const char *hint = "<-Select a message to view details";
  const float hint_w = ImGui::CalcTextSize(hint).x;
  ImGui::SetCursorPosX(std::max(0.0f, center_x - hint_w * 0.5f));
  ImGui::TextDisabled("%s", hint);
  ImGui::Spacing();
  const std::array<std::pair<const char *, const char *>, 3> shortcuts = {{
    {"Pause", "Space"},
    {"Help", "F1"},
    {"Find Signal", "Ctrl/Cmd+F"},
  }};
  for (const auto &[label, key] : shortcuts) {
    const float row_w = 160.0f;
    ImGui::SetCursorPosX(std::max(0.0f, center_x - row_w * 0.5f));
    ImGui::TextDisabled("%s", label);
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::BeginDisabled();
    ImGui::SmallButton(key);
    ImGui::EndDisabled();
  }
}

void draw_splitter_line(const ImRect &rect, bool hovered) {
  ImDrawList *draw_list = ImGui::GetWindowDrawList();
  const ImU32 color = hovered ? IM_COL32(112, 128, 144, 255) : IM_COL32(194, 198, 204, 255);
  if (rect.GetWidth() > rect.GetHeight()) {
    const float y = (rect.Min.y + rect.Max.y) * 0.5f;
    draw_list->AddLine(ImVec2(rect.Min.x, y), ImVec2(rect.Max.x, y), color, hovered ? 2.0f : 1.0f);
  } else {
    const float x = (rect.Min.x + rect.Max.x) * 0.5f;
    draw_list->AddLine(ImVec2(x, rect.Min.y), ImVec2(x, rect.Max.y), color, hovered ? 2.0f : 1.0f);
  }
}

void draw_vertical_splitter(const char *id,
                            float height,
                            float min_left,
                            float max_left,
                            float *left_width) {
  const ImVec2 size(kSplitterThickness, height);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  if (ImGui::IsItemActive()) {
    *left_width = std::clamp(*left_width + ImGui::GetIO().MouseDelta.x, min_left, max_left);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

void draw_right_splitter(const char *id,
                         float height,
                         float min_right,
                         float max_right,
                         float *right_width) {
  const ImVec2 size(kSplitterThickness, height);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
  }
  if (ImGui::IsItemActive()) {
    *right_width = std::clamp(*right_width - ImGui::GetIO().MouseDelta.x, min_right, max_right);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

void draw_horizontal_splitter(const char *id,
                              float width,
                              float min_top,
                              float max_top,
                              float *top_height) {
  const ImVec2 size(width, kSplitterThickness);
  ImGui::InvisibleButton(id, size);
  const bool hovered = ImGui::IsItemHovered() || ImGui::IsItemActive();
  if (hovered) {
    ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
  }
  if (ImGui::IsItemActive()) {
    *top_height = std::clamp(*top_height + ImGui::GetIO().MouseDelta.y, min_top, max_top);
  }
  draw_splitter_line(ImRect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax()), hovered);
}

std::optional<double> cabana_message_value_at_time(const AppSession &session, std::string_view path, double tracker_time) {
  const RouteSeries *series = app_find_route_series(session, std::string(path));
  if (series == nullptr || series->times.empty() || series->values.empty()) {
    return std::nullopt;
  }
  return app_sample_xy_value_at_time(series->times, series->values, false, tracker_time);
}

std::string cabana_value_label(const AppSession &session, std::string_view path, double tracker_time) {
  const auto value = cabana_message_value_at_time(session, path, tracker_time);
  const auto format_it = session.route_data.series_formats.find(std::string(path));
  const auto enum_it = session.route_data.enum_info.find(std::string(path));
  if (!value.has_value() || format_it == session.route_data.series_formats.end()) {
    return "--";
  }
  return format_display_value(*value,
                              format_it->second,
                              enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second);
}

bool export_raw_can_csv(const AppSession &session,
                        const CabanaMessageSummary &message,
                        std::string *error,
                        fs::path *output_path) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    if (error != nullptr) *error = "No raw CAN frames available";
    return false;
  }

  const fs::path output = cabana_export_path(session, message, "raw");
  fs::create_directories(output.parent_path());
  std::ofstream out(output);
  if (!out.is_open()) {
    if (error != nullptr) *error = "Failed to open raw CSV";
    return false;
  }

  out << "mono_time,bus_time,dt_ms,data_hex\n";
  for (size_t i = 0; i < message_data->samples.size(); ++i) {
    const CanFrameSample &sample = message_data->samples[i];
    out << sample.mono_time << ','
        << sample.bus_time << ',';
    if (i > 0) {
      out << 1000.0 * (sample.mono_time - message_data->samples[i - 1].mono_time);
    }
    out << ',' << csv_escape(payload_hex(sample.data)) << '\n';
  }

  if (!out.good()) {
    if (error != nullptr) *error = "Failed while writing raw CSV";
    return false;
  }
  if (output_path != nullptr) *output_path = output;
  return true;
}

bool export_decoded_can_csv(const AppSession &session,
                            const CabanaMessageSummary &message,
                            std::string *error,
                            fs::path *output_path) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    if (error != nullptr) *error = "No raw CAN frames available";
    return false;
  }
  if (message.signals.empty()) {
    if (error != nullptr) *error = "No decoded signals for this message";
    return false;
  }

  std::vector<const CabanaSignalSummary *> export_signals;
  std::vector<const RouteSeries *> export_series;
  std::vector<const SeriesFormat *> export_formats;
  std::vector<const EnumInfo *> export_enums;
  export_signals.reserve(message.signals.size());
  export_series.reserve(message.signals.size());
  export_formats.reserve(message.signals.size());
  export_enums.reserve(message.signals.size());

  for (const CabanaSignalSummary &signal : message.signals) {
    const RouteSeries *series = app_find_route_series(session, signal.path);
    if (series == nullptr) {
      continue;
    }
    export_signals.push_back(&signal);
    export_series.push_back(series);
    auto format_it = session.route_data.series_formats.find(signal.path);
    export_formats.push_back(format_it == session.route_data.series_formats.end() ? nullptr : &format_it->second);
    auto enum_it = session.route_data.enum_info.find(signal.path);
    export_enums.push_back(enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second);
  }

  if (export_series.empty()) {
    if (error != nullptr) *error = "No decoded signal data available";
    return false;
  }

  const fs::path output = cabana_export_path(session, message, "decoded");
  fs::create_directories(output.parent_path());
  std::ofstream out(output);
  if (!out.is_open()) {
    if (error != nullptr) *error = "Failed to open decoded CSV";
    return false;
  }

  out << "mono_time,bus_time,dt_ms,data_hex";
  for (const CabanaSignalSummary *signal : export_signals) {
    out << ',' << csv_escape(signal->name);
  }
  out << '\n';

  for (size_t i = 0; i < message_data->samples.size(); ++i) {
    const CanFrameSample &sample = message_data->samples[i];
    out << sample.mono_time << ','
        << sample.bus_time << ',';
    if (i > 0) {
      out << 1000.0 * (sample.mono_time - message_data->samples[i - 1].mono_time);
    }
    out << ',' << csv_escape(payload_hex(sample.data));
    for (size_t j = 0; j < export_series.size(); ++j) {
      out << ',';
      const std::optional<double> value = app_sample_xy_value_at_time(
        export_series[j]->times, export_series[j]->values, false, sample.mono_time);
      if (value.has_value()) {
        out << csv_escape(export_formats[j] != nullptr
                            ? format_display_value(*value, *export_formats[j], export_enums[j])
                            : std::to_string(*value));
      }
    }
    out << '\n';
  }

  if (!out.good()) {
    if (error != nullptr) *error = "Failed while writing decoded CSV";
    return false;
  }
  if (output_path != nullptr) *output_path = output;
  return true;
}

size_t closest_can_sample_index(const CanMessageData &message, double tracker_time) {
  if (message.samples.empty()) {
    return 0;
  }
  auto it = std::lower_bound(message.samples.begin(), message.samples.end(), tracker_time,
                             [](const CanFrameSample &sample, double time) {
                               return sample.mono_time < time;
                             });
  if (it == message.samples.begin()) {
    return 0;
  }
  if (it == message.samples.end()) {
    return message.samples.size() - 1;
  }
  const size_t upper = static_cast<size_t>(it - message.samples.begin());
  const size_t lower = upper - 1;
  return std::abs(message.samples[upper].mono_time - tracker_time) < std::abs(message.samples[lower].mono_time - tracker_time)
    ? upper
    : lower;
}

uint8_t can_bit(const std::string &data, size_t byte_index, int bit_index) {
  if (byte_index >= data.size() || bit_index < 0 || bit_index > 7) {
    return 0;
  }
  return (static_cast<uint8_t>(data[byte_index]) >> bit_index) & 0x1;
}

BitBehaviorStats bit_behavior_stats(const CanMessageData &message, size_t byte_index, int bit_index) {
  BitBehaviorStats stats;
  if (message.samples.empty()) {
    return stats;
  }
  size_t ones = 0;
  size_t flips = 0;
  uint8_t prev = can_bit(message.samples.front().data, byte_index, bit_index);
  ones += prev;
  for (size_t i = 1; i < message.samples.size(); ++i) {
    const uint8_t bit = can_bit(message.samples[i].data, byte_index, bit_index);
    ones += bit;
    flips += bit != prev;
    prev = bit;
  }
  stats.samples = message.samples.size();
  stats.ones_ratio = static_cast<double>(ones) / static_cast<double>(stats.samples);
  stats.flip_ratio = stats.samples > 1 ? static_cast<double>(flips) / static_cast<double>(stats.samples - 1) : 0.0;
  return stats;
}

size_t can_message_payload_width(const CanMessageData &message) {
  size_t width = 0;
  for (const CanFrameSample &sample : message.samples) {
    width = std::max(width, sample.data.size());
  }
  return width;
}

size_t cabana_signal_byte_count(const CabanaMessageSummary &message) {
  size_t width = 0;
  for (const CabanaSignalSummary &signal : message.signals) {
    if (!signal.has_bit_range) {
      continue;
    }
    width = std::max(width, static_cast<size_t>(std::max(signal.msb / 8, signal.lsb / 8) + 1));
  }
  return width;
}

BinaryMatrixLayout build_binary_matrix_layout(const CabanaMessageSummary &message, size_t byte_count) {
  BinaryMatrixLayout layout;
  layout.byte_count = byte_count;
  layout.cell_signals.resize(byte_count * 8);
  layout.is_msb.assign(byte_count * 8, false);
  layout.is_lsb.assign(byte_count * 8, false);
  for (size_t i = 0; i < message.signals.size(); ++i) {
    const CabanaSignalSummary &signal = message.signals[i];
    if (!signal.has_bit_range) {
      continue;
    }
    for (size_t byte = 0; byte < byte_count; ++byte) {
      for (int bit = 0; bit < 8; ++bit) {
        if (signal_contains_bit(signal, byte, bit)) {
          layout.cell_signals[byte * 8 + static_cast<size_t>(bit)].push_back(static_cast<int>(i));
        }
      }
    }
    const size_t msb_byte = static_cast<size_t>(signal.msb / 8);
    const size_t lsb_byte = static_cast<size_t>(signal.lsb / 8);
    if (msb_byte < byte_count) {
      layout.is_msb[msb_byte * 8 + static_cast<size_t>(signal.msb & 7)] = true;
    }
    if (lsb_byte < byte_count) {
      layout.is_lsb[lsb_byte * 8 + static_cast<size_t>(signal.lsb & 7)] = true;
    }
  }
  for (std::vector<int> &signals : layout.cell_signals) {
    std::stable_sort(signals.begin(), signals.end(), [&](int a, int b) {
      return message.signals[static_cast<size_t>(a)].size > message.signals[static_cast<size_t>(b)].size;
    });
    if (signals.size() > 1) {
      ++layout.overlapping_cells;
    }
  }
  return layout;
}

bool cell_has_signal(const BinaryMatrixLayout &layout, int byte_index, int bit_index, int signal_index) {
  if (byte_index < 0 || bit_index < 0 || bit_index > 7) {
    return false;
  }
  if (static_cast<size_t>(byte_index) >= layout.byte_count) {
    return false;
  }
  const std::vector<int> &signals = layout.cell_signals[static_cast<size_t>(byte_index) * 8 + static_cast<size_t>(bit_index)];
  return std::find(signals.begin(), signals.end(), signal_index) != signals.end();
}

std::vector<float> compute_bit_flip_heat(const CanMessageData &message,
                                         size_t byte_count,
                                         bool live_mode,
                                         size_t tracker_index) {
  std::vector<float> heat(byte_count * 8, 0.0f);
  if (message.samples.size() < 2 || byte_count == 0) {
    return heat;
  }

  size_t begin = 0;
  size_t end = message.samples.size();
  if (live_mode) {
    end = std::min(message.samples.size(), tracker_index + 1);
    const size_t window = 96;
    begin = end > window ? end - window : 0;
  }
  if (end <= begin + 1) {
    return heat;
  }

  std::vector<uint32_t> flip_counts(byte_count * 8, 0);
  uint32_t max_count = 1;
  std::string prev = message.samples[begin].data;
  for (size_t i = begin + 1; i < end; ++i) {
    const std::string &current = message.samples[i].data;
    for (size_t byte = 0; byte < byte_count; ++byte) {
      const uint8_t before = byte < prev.size() ? static_cast<uint8_t>(prev[byte]) : 0;
      const uint8_t after = byte < current.size() ? static_cast<uint8_t>(current[byte]) : 0;
      const uint8_t diff = before ^ after;
      if (diff == 0) {
        continue;
      }
      for (int bit = 0; bit < 8; ++bit) {
        if ((diff & (1u << bit)) == 0) {
          continue;
        }
        uint32_t &count = flip_counts[byte * 8 + static_cast<size_t>(bit)];
        ++count;
        max_count = std::max(max_count, count);
      }
    }
    prev = current;
  }

  for (size_t i = 0; i < flip_counts.size(); ++i) {
    if (flip_counts[i] == 0) {
      continue;
    }
    const float frac = static_cast<float>(flip_counts[i]) / static_cast<float>(max_count);
    heat[i] = std::sqrt(frac);
  }
  return heat;
}

bool signal_charted(const UiState &state, std::string_view path) {
  return std::find(state.cabana.chart_signal_paths.begin(), state.cabana.chart_signal_paths.end(), path)
      != state.cabana.chart_signal_paths.end();
}

ImU32 signal_fill_color(size_t index, float alpha_scale, bool emphasized) {
  const auto &rgb = kSignalHighlightColors[index % kSignalHighlightColors.size()];
  const float alpha = emphasized ? std::clamp(0.34f + alpha_scale * 0.38f, 0.34f, 0.78f)
                                 : std::clamp(0.14f + alpha_scale * 0.28f, 0.14f, 0.48f);
  return ImGui::GetColorU32(color_rgb(rgb, alpha));
}

ImU32 signal_border_color(size_t index, bool emphasized) {
  const auto &rgb = kSignalHighlightColors[index % kSignalHighlightColors.size()];
  return ImGui::GetColorU32(color_rgb(rgb[0], rgb[1], rgb[2], emphasized ? 0.95f : 0.78f));
}

void draw_cell_hatching(ImDrawList *draw, const ImRect &rect, ImU32 color, float spacing) {
  for (float x = rect.Min.x - rect.GetHeight(); x < rect.Max.x; x += spacing) {
    const ImVec2 a(std::max(rect.Min.x, x), std::min(rect.Max.y, rect.Min.y + (rect.Min.x - x) + rect.GetHeight()));
    const ImVec2 b(std::min(rect.Max.x, x + rect.GetHeight()), std::max(rect.Min.y, rect.Max.y - (rect.Max.x - x)));
    draw->AddLine(a, b, color, 1.0f);
  }
}

bool signal_contains_bit(const CabanaSignalSummary &signal, size_t byte_index, int bit_index) {
  if (!signal.has_bit_range || bit_index < 0 || bit_index > 7) {
    return false;
  }
  const int msb_byte = signal.msb / 8;
  const int lsb_byte = signal.lsb / 8;
  if (msb_byte == lsb_byte) {
    return static_cast<int>(byte_index) == msb_byte
        && bit_index >= (signal.lsb & 7)
        && bit_index <= (signal.msb & 7);
  }
  for (int i = msb_byte, step = signal.is_little_endian ? -1 : 1;; i += step) {
    const int hi = i == msb_byte ? (signal.msb & 7) : 7;
    const int lo = i == lsb_byte ? (signal.lsb & 7) : 0;
    if (static_cast<int>(byte_index) == i && bit_index >= lo && bit_index <= hi) {
      return true;
    }
    if (i == lsb_byte) {
      return false;
    }
  }
}

std::vector<std::pair<const CabanaSignalSummary *, ImU32>> highlighted_signals(const CabanaMessageSummary &message, const UiState &state) {
  std::vector<std::pair<const CabanaSignalSummary *, ImU32>> out;
  for (const std::string &path : state.cabana.chart_signal_paths) {
    for (size_t i = 0; i < message.signals.size(); ++i) {
      const CabanaSignalSummary &signal = message.signals[i];
      if (signal.path != path || !signal.has_bit_range) {
        continue;
      }
      out.push_back({&signal, signal_fill_color(i, 0.5f, true)});
      break;
    }
  }
  return out;
}

void draw_signal_overlay_legend(const std::vector<std::pair<const CabanaSignalSummary *, ImU32>> &highlighted) {
  if (highlighted.empty()) {
    return;
  }
  app_push_bold_font();
  ImGui::TextUnformatted("Signals");
  app_pop_bold_font();
  for (size_t i = 0; i < highlighted.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 12.0f);
    ImGui::ColorButton(("##cabana_signal_color_" + std::to_string(i)).c_str(),
                       ImGui::ColorConvertU32ToFloat4(highlighted[i].second),
                       ImGuiColorEditFlags_NoTooltip,
                       ImVec2(10.0f, 10.0f));
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextUnformatted(highlighted[i].first->name.c_str());
    ImGui::SameLine(0.0f, 6.0f);
    ImGui::TextDisabled("[%d|%d]", highlighted[i].first->start_bit, highlighted[i].first->size);
  }
  ImGui::Spacing();
}

bool cabana_bit_selected(const UiState &state, size_t byte_index, int bit_index) {
  return state.cabana.has_bit_selection
      && state.cabana.selected_bit_byte == static_cast<int>(byte_index)
      && state.cabana.selected_bit_index == bit_index;
}

std::vector<const CabanaSignalSummary *> selected_bit_signals(const CabanaMessageSummary &message, const UiState &state) {
  std::vector<const CabanaSignalSummary *> out;
  if (!state.cabana.has_bit_selection) {
    return out;
  }
  for (const CabanaSignalSummary &signal : message.signals) {
    if (signal_contains_bit(signal,
                            static_cast<size_t>(state.cabana.selected_bit_byte),
                            state.cabana.selected_bit_index)) {
      out.push_back(&signal);
    }
  }
  return out;
}

std::vector<CabanaSimilarBitMatch> find_similar_bits_from_snapshot(const std::vector<CabanaMessageSummary> &messages,
                                                                   const std::vector<CanMessageData> &can_messages,
                                                                   const CabanaMessageSummary &source_message,
                                                                   const CanMessageData &source_data,
                                                                   size_t source_byte,
                                                                   int source_bit) {
  const BitBehaviorStats target = bit_behavior_stats(source_data, source_byte, source_bit);
  std::vector<CabanaSimilarBitMatch> matches;
  for (const CabanaMessageSummary &message : messages) {
    const std::optional<CanServiceKind> service = parse_can_service_kind(message.service);
    if (!service.has_value()) continue;
    const CanMessageData key{.id = CanMessageId{*service, static_cast<uint8_t>(message.bus), message.address}};
    auto it = std::lower_bound(can_messages.begin(), can_messages.end(), key, [](const CanMessageData &a, const CanMessageData &b) {
      return std::make_tuple(a.id.service, a.id.bus, a.id.address)
           < std::make_tuple(b.id.service, b.id.bus, b.id.address);
    });
    if (it == can_messages.end()
        || it->id.service != key.id.service
        || it->id.bus != key.id.bus
        || it->id.address != key.id.address
        || it->samples.size() < 2) {
      continue;
    }
    for (size_t byte = 0; byte < can_message_payload_width(*it); ++byte) {
      for (int bit = 0; bit < 8; ++bit) {
        if (message.root_path == source_message.root_path
            && static_cast<int>(byte) == static_cast<int>(source_byte)
            && bit == source_bit) {
          continue;
        }
        const BitBehaviorStats stats = bit_behavior_stats(*it, byte, bit);
        if (stats.samples < 2) continue;
        const double ones_diff = std::abs(stats.ones_ratio - target.ones_ratio);
        const double flip_diff = std::abs(stats.flip_ratio - target.flip_ratio);
        matches.push_back({
          .message_root = message.root_path,
          .label = message.name,
          .bus = message.bus,
          .address = message.address,
          .byte_index = static_cast<int>(byte),
          .bit_index = bit,
          .score = ones_diff * 0.65 + flip_diff * 0.35,
          .ones_ratio = stats.ones_ratio,
          .flip_ratio = stats.flip_ratio,
        });
      }
    }
  }
  std::sort(matches.begin(), matches.end(), [](const CabanaSimilarBitMatch &a, const CabanaSimilarBitMatch &b) {
    return std::tie(a.score, a.label, a.byte_index, a.bit_index)
         < std::tie(b.score, b.label, b.byte_index, b.bit_index);
  });
  if (matches.size() > 12) {
    matches.resize(12);
  }
  return matches;
}

void draw_bit_selection_panel(AppSession *session, const CabanaMessageSummary &message, UiState *state) {
  poll_similar_bit_search(state);
  if (!state->cabana.has_bit_selection) {
    return;
  }
  app_push_bold_font();
  ImGui::Text("Selected Bit: B%d.%d", state->cabana.selected_bit_byte, state->cabana.selected_bit_index);
  app_pop_bold_font();
  ImGui::SameLine();
  if (ImGui::SmallButton("Clear")) {
    state->cabana.has_bit_selection = false;
    clear_similar_bit_results(state);
    return;
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(state->cabana.similar_bits_loading);
  if (ImGui::SmallButton("Find Similar Bits")) {
    const CanMessageData *message_data = find_message_data(*session, message);
    if (message_data != nullptr) {
      state->cabana.similar_bit_matches.clear();
      state->cabana.similar_bits_source_root = message.root_path;
      state->cabana.similar_bits_source_byte = state->cabana.selected_bit_byte;
      state->cabana.similar_bits_source_bit = state->cabana.selected_bit_index;
      state->cabana.similar_bits_loading = true;
      const std::vector<CabanaMessageSummary> messages = session->cabana_messages;
      const std::vector<CanMessageData> can_messages = session->route_data.can_messages;
      const CanMessageData source_data = *message_data;
      const size_t source_byte = static_cast<size_t>(state->cabana.selected_bit_byte);
      const int source_bit = state->cabana.selected_bit_index;
      state->cabana.similar_bit_future = std::async(std::launch::async, [messages, can_messages, message, source_data, source_byte, source_bit]() {
        return find_similar_bits_from_snapshot(messages, can_messages, message, source_data, source_byte, source_bit);
      });
    }
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::SmallButton("Create Signal...")) {
    open_cabana_new_signal_editor(*session,
                                  state,
                                  message,
                                  state->cabana.selected_bit_byte,
                                  state->cabana.selected_bit_index);
  }
  const auto overlaps = selected_bit_signals(message, *state);
  if (overlaps.empty()) {
    ImGui::TextDisabled("No decoded signals cover this bit.");
  } else {
    ImGui::TextDisabled("Signals covering this bit:");
    for (size_t i = 0; i < overlaps.size(); ++i) {
      if (i > 0) ImGui::SameLine(0.0f, 8.0f);
      if (ImGui::SmallButton(overlaps[i]->name.c_str())) {
        state->cabana.chart_signal_paths = {overlaps[i]->path};
      }
    }
  }

  if (state->cabana.similar_bits_loading && similar_bit_results_match_selection(*state)) {
    ImGui::Spacing();
    ImGui::TextDisabled("Searching similar bits...");
  } else if (similar_bit_results_match_selection(*state) && !state->cabana.similar_bit_matches.empty()) {
    ImGui::Spacing();
    ImGui::TextDisabled("Similar bits:");
    if (ImGui::BeginTable("##cabana_similar_bits", 5,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) {
      ImGui::TableSetupColumn("Message", ImGuiTableColumnFlags_WidthStretch, 2.2f);
      ImGui::TableSetupColumn("Bit", ImGuiTableColumnFlags_WidthFixed, 58.0f);
      ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 58.0f);
      ImGui::TableSetupColumn("1s", ImGuiTableColumnFlags_WidthFixed, 52.0f);
      ImGui::TableSetupColumn("Flip", ImGuiTableColumnFlags_WidthFixed, 56.0f);
      ImGui::TableHeadersRow();
      for (const CabanaSimilarBitMatch &match : state->cabana.similar_bit_matches) {
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const std::string label = match.label + "##" + match.message_root + "_" + std::to_string(match.byte_index) + "_" + std::to_string(match.bit_index);
        if (ImGui::Selectable(label.c_str(), false, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          select_cabana_message(session, state, match.message_root);
          state->cabana.has_bit_selection = true;
          state->cabana.selected_bit_byte = match.byte_index;
          state->cabana.selected_bit_index = match.bit_index;
        }
        ImGui::TableNextColumn();
        ImGui::Text("B%d.%d", match.byte_index, match.bit_index);
        ImGui::TableNextColumn();
        ImGui::Text("%.3f", match.score);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", 100.0 * match.ones_ratio);
        ImGui::TableNextColumn();
        ImGui::Text("%.0f%%", 100.0 * match.flip_ratio);
      }
      ImGui::EndTable();
    }
  }
  ImGui::Spacing();
}

void draw_payload_bytes(std::string_view data, const std::string *prev_data = nullptr) {
  app_push_mono_font();
  for (size_t i = 0; i < data.size(); ++i) {
    if (i > 0) ImGui::SameLine(0.0f, 6.0f);
    const bool changed = prev_data != nullptr
                      && i < prev_data->size()
                      && static_cast<unsigned char>((*prev_data)[i]) != static_cast<unsigned char>(data[i]);
    if (changed) {
      ImGui::PushStyleColor(ImGuiCol_Text, color_rgb(199, 74, 59));
    }
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(data[i]));
    ImGui::TextUnformatted(hex);
    if (changed) {
      ImGui::PopStyleColor();
    }
  }
  app_pop_mono_font();
}

void draw_payload_preview_boxes(const char *id, std::string_view data, const std::string *prev_data, float max_width) {
  constexpr float kByteW = 17.0f;
  constexpr float kByteH = 16.0f;
  constexpr float kGap = 2.0f;
  const size_t capacity = std::max<size_t>(1, static_cast<size_t>((max_width + kGap) / (kByteW + kGap)));
  const size_t visible = std::min(data.size(), capacity);
  const bool truncated = visible < data.size();
  const float ellipsis_w = truncated ? 10.0f : 0.0f;
  const float width = std::max(18.0f, visible * (kByteW + kGap) - (visible > 0 ? kGap : 0.0f) + ellipsis_w);
  ImGui::InvisibleButton(id, ImVec2(width, kByteH));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  app_push_mono_font();
  for (size_t i = 0; i < visible; ++i) {
    const unsigned char after = static_cast<unsigned char>(data[i]);
    const bool has_prev = prev_data != nullptr && i < prev_data->size();
    const unsigned char before = has_prev ? static_cast<unsigned char>((*prev_data)[i]) : after;
    ImU32 fill = ImGui::GetColorU32(color_rgb(235, 238, 242));
    if (has_prev && after != before) {
      fill = ImGui::GetColorU32(after > before ? color_rgb(205, 223, 255) : color_rgb(251, 218, 212));
    }
    const float x0 = rect.Min.x + static_cast<float>(i) * (kByteW + kGap);
    const ImRect box(ImVec2(x0, rect.Min.y), ImVec2(x0 + kByteW, rect.Min.y + kByteH));
    draw->AddRectFilled(box.Min, box.Max, fill, 2.0f);
    draw->AddRect(box.Min, box.Max, ImGui::GetColorU32(color_rgb(198, 204, 212)), 2.0f);
    char hex[4];
    std::snprintf(hex, sizeof(hex), "%02X", after);
    const ImVec2 text_size = ImGui::CalcTextSize(hex);
    draw->AddText(ImGui::GetFont(),
                  ImGui::GetFontSize(),
                  ImVec2(box.Min.x + (box.GetWidth() - text_size.x) * 0.5f,
                         box.Min.y + (box.GetHeight() - text_size.y) * 0.5f - 1.0f),
                  ImGui::GetColorU32(color_rgb(52, 58, 66)),
                  hex);
  }
  if (truncated) {
    draw->AddText(ImVec2(rect.Max.x - 9.0f, rect.Min.y - 1.0f),
                  ImGui::GetColorU32(color_rgb(122, 129, 138)),
                  "...");
  }
  app_pop_mono_font();
}

void draw_signal_sparkline(const AppSession &session,
                          const UiState &state,
                          std::string_view signal_path,
                          bool selected) {
  const RouteSeries *series = app_find_route_series(session, std::string(signal_path));
  const float width = std::max(96.0f, ImGui::GetColumnWidth() - 12.0f);
  const ImVec2 size(width, 24.0f);
  if (series == nullptr || series->times.size() < 2 || series->times.size() != series->values.size()) {
    ImGui::Dummy(size);
    return;
  }

  const std::string id = "##spark_" + std::string(signal_path);
  ImGui::InvisibleButton(id.c_str(), size);
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImU32 bg = ImGui::GetColorU32(selected ? color_rgb(234, 241, 252) : color_rgb(246, 248, 250));
  const ImU32 border = ImGui::GetColorU32(selected ? color_rgb(102, 140, 214) : color_rgb(209, 214, 220));
  const ImU32 line = ImGui::GetColorU32(selected ? color_rgb(69, 137, 255) : color_rgb(111, 126, 146));
  const ImU32 tracker = ImGui::GetColorU32(color_rgb(214, 93, 64));
  draw->AddRectFilled(rect.Min, rect.Max, bg, 4.0f);
  draw->AddRect(rect.Min, rect.Max, border, 4.0f);

  double x_min = series->times.front();
  double x_max = series->times.back();
  if (state.has_shared_range) {
    const double overlap_min = std::max(x_min, state.x_view_min);
    const double overlap_max = std::min(x_max, state.x_view_max);
    if (overlap_max > overlap_min) {
      x_min = overlap_min;
      x_max = overlap_max;
    }
  }
  if (x_max <= x_min) {
    return;
  }

  constexpr int kSamples = 40;
  std::array<double, kSamples> sampled = {};
  std::array<bool, kSamples> valid = {};
  bool found = false;
  double y_min = 0.0;
  double y_max = 0.0;
  for (int i = 0; i < kSamples; ++i) {
    const double t = kSamples == 1 ? x_min : x_min + (x_max - x_min) * static_cast<double>(i) / static_cast<double>(kSamples - 1);
    const std::optional<double> value = app_sample_xy_value_at_time(series->times, series->values, false, t);
    if (!value.has_value() || !std::isfinite(*value)) {
      continue;
    }
    sampled[static_cast<size_t>(i)] = *value;
    valid[static_cast<size_t>(i)] = true;
    if (!found) {
      y_min = y_max = *value;
      found = true;
    } else {
      y_min = std::min(y_min, *value);
      y_max = std::max(y_max, *value);
    }
  }
  if (!found) {
    return;
  }
  if (y_max <= y_min) {
    const double pad = std::max(0.1, std::abs(y_min) * 0.1);
    y_min -= pad;
    y_max += pad;
  } else {
    const double pad = (y_max - y_min) * 0.12;
    y_min -= pad;
    y_max += pad;
  }

  const float left = rect.Min.x + 4.0f;
  const float right = rect.Max.x - 4.0f;
  const float top = rect.Min.y + 4.0f;
  const float bottom = rect.Max.y - 4.0f;
  std::array<ImVec2, kSamples> points = {};
  int point_count = 0;
  for (int i = 0; i < kSamples; ++i) {
    if (!valid[static_cast<size_t>(i)]) {
      if (point_count > 1) {
        draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);
      }
      point_count = 0;
      continue;
    }
    const float x = left + (right - left) * static_cast<float>(i) / static_cast<float>(kSamples - 1);
    const float frac = static_cast<float>((sampled[static_cast<size_t>(i)] - y_min) / (y_max - y_min));
    const float y = bottom - (bottom - top) * std::clamp(frac, 0.0f, 1.0f);
    points[static_cast<size_t>(point_count++)] = ImVec2(x, y);
  }
  if (point_count > 1) {
    draw->AddPolyline(points.data(), point_count, line, 0, selected ? 2.0f : 1.5f);
  }

  if (state.has_tracker_time && state.tracker_time >= x_min && state.tracker_time <= x_max) {
    const float marker_x = left + (right - left) * static_cast<float>((state.tracker_time - x_min) / (x_max - x_min));
    draw->AddLine(ImVec2(marker_x, top), ImVec2(marker_x, bottom), tracker, 1.5f);
  }
}

ImU32 mix_color(ImU32 a, ImU32 b, float t) {
  const ImVec4 av = ImGui::ColorConvertU32ToFloat4(a);
  const ImVec4 bv = ImGui::ColorConvertU32ToFloat4(b);
  return ImGui::GetColorU32(ImVec4(av.x + (bv.x - av.x) * t,
                                   av.y + (bv.y - av.y) * t,
                                   av.z + (bv.z - av.z) * t,
                                   av.w + (bv.w - av.w) * t));
}

void draw_can_heatmap(const CanMessageData &message,
                      const std::vector<std::pair<const CabanaSignalSummary *, ImU32>> &highlighted,
                      double tracker_time) {
  const size_t byte_count = can_message_payload_width(message);
  if (message.samples.empty() || byte_count == 0) {
    return;
  }

  app_push_bold_font();
  ImGui::TextUnformatted("History Heatmap");
  app_pop_bold_font();
  ImGui::TextDisabled("aggregated over all frames");
  ImGui::Spacing();

  const size_t row_count = byte_count * 8;
  const float avail_w = ImGui::GetContentRegionAvail().x;
  const float label_w = 42.0f;
  const float row_h = std::clamp(160.0f / std::max<float>(1.0f, static_cast<float>(row_count)), 10.0f, 16.0f);
  const float grid_h = row_h * static_cast<float>(row_count);
  const float grid_w = std::max(120.0f, avail_w - label_w - 8.0f);
  const int columns = std::max(1, std::min<int>(std::min<size_t>(220, message.samples.size()), static_cast<int>(grid_w / 4.0f)));
  const float cell_w = grid_w / static_cast<float>(columns);
  const size_t tracker_index = closest_can_sample_index(message, tracker_time);

  ImGui::InvisibleButton("##cabana_heatmap", ImVec2(label_w + grid_w, grid_h + 4.0f));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  const ImVec2 grid_min(rect.Min.x + label_w, rect.Min.y);
  ImDrawList *draw = ImGui::GetWindowDrawList();
  const ImU32 grid_bg = ImGui::GetColorU32(color_rgb(246, 247, 249));
  const ImU32 low = ImGui::GetColorU32(color_rgb(234, 238, 243));
  const ImU32 high = ImGui::GetColorU32(color_rgb(69, 116, 201));
  const ImU32 border = ImGui::GetColorU32(color_rgb(204, 209, 214));
  draw->AddRectFilled(ImVec2(grid_min.x, rect.Min.y), ImVec2(grid_min.x + grid_w, rect.Min.y + grid_h), grid_bg, 4.0f);

  for (size_t row = 0; row < row_count; ++row) {
    const size_t byte_index = row / 8;
    const int bit_index = 7 - static_cast<int>(row % 8);
    const float y0 = rect.Min.y + static_cast<float>(row) * row_h;
    const float y1 = y0 + row_h;
    if (row % 8 == 0) {
      const std::string label = "B" + std::to_string(byte_index);
      draw->AddText(ImVec2(rect.Min.x, y0 + 1.0f), ImGui::GetColorU32(color_rgb(92, 100, 112)), label.c_str());
      if (row > 0) {
        draw->AddLine(ImVec2(rect.Min.x, y0), ImVec2(grid_min.x + grid_w, y0), border, 1.0f);
      }
    }
    for (int col = 0; col < columns; ++col) {
      const size_t start = (message.samples.size() * static_cast<size_t>(col)) / static_cast<size_t>(columns);
      const size_t end = std::max(start + 1,
                                  (message.samples.size() * static_cast<size_t>(col + 1)) / static_cast<size_t>(columns));
      size_t ones = 0;
      for (size_t i = start; i < std::min(end, message.samples.size()); ++i) {
        ones += can_bit(message.samples[i].data, byte_index, bit_index);
      }
      const float frac = static_cast<float>(ones) / static_cast<float>(std::max<size_t>(1, std::min(end, message.samples.size()) - start));
      ImU32 color = mix_color(low, high, frac);
      for (const auto &[signal, signal_color] : highlighted) {
        if (signal_contains_bit(*signal, byte_index, bit_index)) {
          color = mix_color(color, signal_color, 0.65f);
          break;
        }
      }
      const float x0 = grid_min.x + static_cast<float>(col) * cell_w;
      const float x1 = x0 + cell_w + 0.5f;
      draw->AddRectFilled(ImVec2(x0, y0), ImVec2(x1, y1), color);
    }
  }

  const float tracker_x = grid_min.x + cell_w * ((static_cast<float>(tracker_index) + 0.5f) * static_cast<float>(columns)
                                                  / static_cast<float>(std::max<size_t>(1, message.samples.size())));
  draw->AddLine(ImVec2(tracker_x, rect.Min.y), ImVec2(tracker_x, rect.Min.y + grid_h),
                ImGui::GetColorU32(color_rgb(36, 42, 50, 0.9f)), 2.0f);
  draw->AddRect(ImVec2(grid_min.x, rect.Min.y), ImVec2(grid_min.x + grid_w, rect.Min.y + grid_h), border, 4.0f);
}

void draw_can_frame_view(const CanMessageData &message,
                         AppSession *session,
                         const CabanaMessageSummary &summary,
                         UiState *state,
                         double tracker_time) {
  if (message.samples.empty()) {
    draw_cabana_panel_title("Binary View");
    ImGui::TextDisabled("No raw CAN frames available.");
    return;
  }
  const size_t sample_index = closest_can_sample_index(message, tracker_time);
  const CanFrameSample &sample = message.samples[sample_index];
  const CanFrameSample *prev = sample_index > 0 ? &message.samples[sample_index - 1] : nullptr;
  const size_t byte_count = std::max(can_message_payload_width(message), cabana_signal_byte_count(summary));
  const BinaryMatrixLayout layout = build_binary_matrix_layout(summary, byte_count);
  const std::vector<float> heat = compute_bit_flip_heat(message, byte_count, state->cabana.heatmap_live_mode, sample_index);
  const auto highlighted = highlighted_signals(summary, *state);

  draw_cabana_panel_title("Binary View");
  ImGui::TextDisabled("tracker %.3fs", tracker_time);
  ImGui::SameLine();
  ImGui::TextDisabled("frame %.3fs", sample.mono_time);
  ImGui::SameLine();
  ImGui::TextDisabled("bytes %zu", sample.data.size());
  ImGui::SameLine();
  ImGui::TextDisabled("bus %d", summary.bus);
  if (sample.bus_time != 0) {
    ImGui::SameLine();
    ImGui::TextDisabled("bus_time %u", sample.bus_time);
  }
  if (prev != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("dt %.1f ms", 1000.0 * (sample.mono_time - prev->mono_time));
  }
  if (layout.overlapping_cells > 0) {
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextColored(color_rgb(174, 115, 38), "%zu overlapping cell%s",
                       layout.overlapping_cells, layout.overlapping_cells == 1 ? "" : "s");
  }
  ImGui::Spacing();
  draw_signal_overlay_legend(highlighted);

  const float footer_reserve = state->cabana.has_bit_selection ? 165.0f : 48.0f;
  const float matrix_height = std::max(180.0f, ImGui::GetContentRegionAvail().y - footer_reserve);
  const float index_w = 30.0f;
  const float data_w = std::max(42.0f, (ImGui::GetContentRegionAvail().x - index_w - 12.0f) / 9.0f);
  const float bit_w = data_w;
  const float hex_w = data_w;
  const float row_h = 36.0f;
  const ImU32 base_bg = ImGui::GetColorU32(color_rgb(248, 249, 251));
  const ImU32 heat_high = ImGui::GetColorU32(color_rgb(72, 117, 202));
  const ImU32 cell_border = ImGui::GetColorU32(color_rgb(206, 211, 218, 0.55f));
  const ImU32 text_color = ImGui::GetColorU32(color_rgb(30, 34, 41));
  const ImU32 marker_color = ImGui::GetColorU32(color_rgb(92, 99, 110));
  const ImU32 invalid_hatch = ImGui::GetColorU32(color_rgb(182, 188, 196, 0.85f));
  const ImU32 overlap_hatch = ImGui::GetColorU32(color_rgb(68, 72, 78, 0.45f));
  const ImU32 selection_border = ImGui::GetColorU32(color_rgb(33, 38, 44, 0.95f));
  const ImU32 hover_border = ImGui::GetColorU32(color_rgb(33, 38, 44, 0.35f));

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(0.0f, 0.0f));
  if (ImGui::BeginTable("##cabana_binary_matrix", 10,
                        ImGuiTableFlags_SizingFixedFit |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_NoPadInnerX |
                          ImGuiTableFlags_NoPadOuterX,
                        ImVec2(0.0f, matrix_height))) {
    ImGui::TableSetupColumn("##byte", ImGuiTableColumnFlags_WidthFixed, index_w);
    for (int i = 0; i < 8; ++i) {
      ImGui::TableSetupColumn(("##bit" + std::to_string(i)).c_str(), ImGuiTableColumnFlags_WidthFixed, bit_w);
    }
    ImGui::TableSetupColumn("##hex", ImGuiTableColumnFlags_WidthFixed, hex_w);

    for (size_t byte = 0; byte < byte_count; ++byte) {
      ImGui::TableNextRow(0, row_h);

      ImGui::TableNextColumn();
      {
        const ImRect cell(ImGui::GetCursorScreenPos(),
                          ImVec2(ImGui::GetCursorScreenPos().x + ImGui::GetColumnWidth(),
                                 ImGui::GetCursorScreenPos().y + row_h));
        ImGui::Dummy(ImVec2(ImGui::GetColumnWidth(), row_h));
        ImDrawList *draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(cell.Min, cell.Max, ImGui::GetColorU32(color_rgb(244, 246, 249)));
        draw->AddRect(cell.Min, cell.Max, cell_border);
        const std::string label = std::to_string(byte);
        const ImVec2 text_size = ImGui::CalcTextSize(label.c_str());
        draw->AddText(ImVec2(cell.Min.x + (cell.GetWidth() - text_size.x) * 0.5f,
                             cell.Min.y + (cell.GetHeight() - text_size.y) * 0.5f),
                      ImGui::GetColorU32(color_rgb(84, 92, 103)),
                      label.c_str());
      }

      for (int bit = 7; bit >= 0; --bit) {
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(byte * 16 + static_cast<size_t>(bit)));
        ImGui::InvisibleButton("##cabana_binary_bit", ImVec2(ImGui::GetColumnWidth(), row_h));
        const bool hovered = ImGui::IsItemHovered();
        if (ImGui::IsItemClicked() && byte < sample.data.size()) {
          state->cabana.has_bit_selection = true;
          state->cabana.selected_bit_byte = static_cast<int>(byte);
          state->cabana.selected_bit_index = bit;
        }
        const ImRect cell(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList *draw = ImGui::GetWindowDrawList();
        const size_t cell_index = byte * 8 + static_cast<size_t>(bit);
        const bool valid = byte < sample.data.size();
        const float heat_alpha = cell_index < heat.size() ? heat[cell_index] : 0.0f;
        const std::vector<int> &cell_signals = layout.cell_signals[cell_index];
        draw->AddRectFilled(cell.Min, cell.Max, mix_color(base_bg, heat_high, heat_alpha * 0.55f));

        if (valid && !cell_signals.empty()) {
          for (int signal_index : cell_signals) {
            const CabanaSignalSummary &signal = summary.signals[static_cast<size_t>(signal_index)];
            const bool emphasized = signal_charted(*state, signal.path);
            const bool draw_left = !cell_has_signal(layout, static_cast<int>(byte), bit + 1, signal_index);
            const bool draw_right = !cell_has_signal(layout, static_cast<int>(byte), bit - 1, signal_index);
            const bool draw_top = !cell_has_signal(layout, static_cast<int>(byte) - 1, bit, signal_index);
            const bool draw_bottom = !cell_has_signal(layout, static_cast<int>(byte) + 1, bit, signal_index);
            ImRect inner = cell;
            inner.Min.x += draw_left ? 3.0f : 1.0f;
            inner.Max.x -= draw_right ? 3.0f : 1.0f;
            inner.Min.y += draw_top ? 2.0f : 1.0f;
            inner.Max.y -= draw_bottom ? 2.0f : 1.0f;
            draw->AddRectFilled(inner.Min, inner.Max, signal_fill_color(static_cast<size_t>(signal_index), heat_alpha, emphasized), 2.0f);
            const ImU32 border_color = signal_border_color(static_cast<size_t>(signal_index), emphasized);
            const float thickness = emphasized ? 2.0f : 1.0f;
            if (draw_left) draw->AddLine(ImVec2(inner.Min.x, inner.Min.y), ImVec2(inner.Min.x, inner.Max.y), border_color, thickness);
            if (draw_right) draw->AddLine(ImVec2(inner.Max.x, inner.Min.y), ImVec2(inner.Max.x, inner.Max.y), border_color, thickness);
            if (draw_top) draw->AddLine(ImVec2(inner.Min.x, inner.Min.y), ImVec2(inner.Max.x, inner.Min.y), border_color, thickness);
            if (draw_bottom) draw->AddLine(ImVec2(inner.Min.x, inner.Max.y), ImVec2(inner.Max.x, inner.Max.y), border_color, thickness);
          }
          if (cell_signals.size() > 1) {
            draw_cell_hatching(draw, cell, overlap_hatch, 6.0f);
          }
        } else if (!valid) {
          draw->AddRectFilled(cell.Min, cell.Max, ImGui::GetColorU32(color_rgb(241, 243, 246)));
          draw_cell_hatching(draw, cell, invalid_hatch, 7.0f);
        }

        draw->AddRect(cell.Min, cell.Max, cell_border);
        if (valid) {
          app_push_mono_font();
          const char bit_text[2] = {static_cast<char>(can_bit(sample.data, byte, bit) ? '1' : '0'), '\0'};
          const ImVec2 text_size = ImGui::CalcTextSize(bit_text);
          draw->AddText(ImGui::GetFont(),
                        ImGui::GetFontSize(),
                        ImVec2(cell.Min.x + (cell.GetWidth() - text_size.x) * 0.5f,
                               cell.Min.y + (cell.GetHeight() - text_size.y) * 0.5f - 1.0f),
                        text_color,
                        bit_text);
          app_pop_mono_font();
        }
        if (layout.is_msb[cell_index] || layout.is_lsb[cell_index]) {
          const char marker[2] = {layout.is_msb[cell_index] ? 'M' : 'L', '\0'};
          draw->AddText(ImVec2(cell.Max.x - 11.0f, cell.Max.y - 14.0f), marker_color, marker);
        }
        if (cabana_bit_selected(*state, byte, bit)) {
          draw->AddRect(cell.Min, cell.Max, selection_border, 0.0f, 0, 2.0f);
        } else if (hovered) {
          draw->AddRect(cell.Min, cell.Max, hover_border, 0.0f, 0, 1.0f);
        }
        ImGui::PopID();
      }

      ImGui::TableNextColumn();
      {
        ImGui::Dummy(ImVec2(ImGui::GetColumnWidth(), row_h));
        const ImRect cell(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
        ImDrawList *draw = ImGui::GetWindowDrawList();
        float byte_heat = 0.0f;
        for (int bit = 0; bit < 8; ++bit) {
          byte_heat = std::max(byte_heat, heat[byte * 8 + static_cast<size_t>(bit)]);
        }
        draw->AddRectFilled(cell.Min, cell.Max, mix_color(base_bg, heat_high, byte_heat * 0.5f));
        draw->AddRect(cell.Min, cell.Max, cell_border);
        if (byte < sample.data.size()) {
          app_push_mono_font();
          char hex[4];
          std::snprintf(hex, sizeof(hex), "%02X", static_cast<unsigned char>(sample.data[byte]));
          const ImVec2 text_size = ImGui::CalcTextSize(hex);
          draw->AddText(ImGui::GetFont(),
                        ImGui::GetFontSize(),
                        ImVec2(cell.Min.x + (cell.GetWidth() - text_size.x) * 0.5f,
                               cell.Min.y + (cell.GetHeight() - text_size.y) * 0.5f - 1.0f),
                        text_color,
                        hex);
          app_pop_mono_font();
        } else {
          draw_cell_hatching(draw, cell, invalid_hatch, 7.0f);
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();

  ImGui::Spacing();
  draw_bit_selection_panel(session, summary, state);
}

void draw_empty_panel(const char *title, const char *message) {
  draw_cabana_panel_title(title);
  ImGui::TextDisabled("%s", message);
}

void draw_cabana_toolbar_button(const char *label, bool enabled, const std::function<void()> &on_click) {
  ImGui::BeginDisabled(!enabled);
  if (ImGui::Button(label)) {
    on_click();
  }
  ImGui::EndDisabled();
}

bool message_has_overlaps(const CabanaMessageSummary &message, const CanMessageData *message_data) {
  const size_t byte_count = std::max(message_data == nullptr ? 0 : can_message_payload_width(*message_data),
                                     cabana_signal_byte_count(message));
  if (byte_count == 0) {
    return false;
  }
  return build_binary_matrix_layout(message, byte_count).overlapping_cells > 0;
}

void draw_cabana_warning_banner(const std::vector<std::string> &warnings) {
  if (warnings.empty()) {
    return;
  }
  const float height = 28.0f + std::max(0.0f, (static_cast<float>(warnings.size()) - 1.0f) * 16.0f);
  ImGui::InvisibleButton("##cabana_warning_banner", ImVec2(ImGui::GetContentRegionAvail().x, height));
  const ImRect rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax());
  ImDrawList *draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(rect.Min, rect.Max, ImGui::GetColorU32(color_rgb(251, 245, 229)), 4.0f);
  draw->AddRect(rect.Min, rect.Max, ImGui::GetColorU32(color_rgb(221, 191, 121)), 4.0f, 0, 1.0f);
  draw->AddText(ImVec2(rect.Min.x + 10.0f, rect.Min.y + 6.0f),
                ImGui::GetColorU32(color_rgb(164, 106, 28)),
                "!");
  float y = rect.Min.y + 5.0f;
  for (const std::string &warning : warnings) {
    draw->AddText(ImVec2(rect.Min.x + 24.0f, y),
                  ImGui::GetColorU32(color_rgb(109, 82, 34)),
                  warning.c_str());
    y += 16.0f;
  }
}

void draw_detail_toolbar(AppSession *session,
                         UiState *state,
                         const CabanaMessageSummary &message,
                         const CanMessageData *message_data) {
  const std::string meta = message.service + " bus " + std::to_string(message.bus)
                         + (message.has_address ? "  " + format_can_address(message.address) : std::string());
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.0f, 6.0f));
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 247, 250));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(209, 214, 220));
  ImGui::BeginChild("##cabana_detail_toolbar", ImVec2(0.0f, 40.0f), true);
  app_push_bold_font();
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted(message.name.c_str());
  app_pop_bold_font();
  ImGui::SameLine(0.0f, 8.0f);
  ImGui::TextDisabled("%s", meta.c_str());
  if (message.frequency_hz > 0.0) {
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%.1f Hz", message.frequency_hz);
  }
  if (message_data != nullptr) {
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%zu frames", message_data->samples.size());
  }

  ImGui::SameLine(std::max(340.0f, ImGui::GetWindowContentRegionMax().x * 0.48f));
  ImGui::TextUnformatted("Heatmap:");
  ImGui::SameLine(0.0f, 6.0f);
  if (ImGui::RadioButton("Live", state->cabana.heatmap_live_mode)) {
    state->cabana.heatmap_live_mode = true;
  }
  ImGui::SameLine(0.0f, 8.0f);
  if (ImGui::RadioButton("All", !state->cabana.heatmap_live_mode)) {
    state->cabana.heatmap_live_mode = false;
  }
  ImGui::SameLine(0.0f, 14.0f);
  draw_cabana_toolbar_button("Edit DBC...", true, [&]() {
    state->dbc_editor.open = true;
    state->dbc_editor.loaded = false;
  });
  ImGui::SameLine(0.0f, 6.0f);
  draw_cabana_toolbar_button("Export Raw CSV", message_data != nullptr, [&]() {
    fs::path output_path;
    std::string error;
    if (export_raw_can_csv(*session, message, &error, &output_path)) {
      state->status_text = "Exported raw CSV " + output_path.filename().string();
    } else {
      state->status_text = error;
    }
  });
  ImGui::SameLine(0.0f, 6.0f);
  draw_cabana_toolbar_button("Export Decoded CSV", message_data != nullptr, [&]() {
    fs::path output_path;
    std::string error;
    if (export_decoded_can_csv(*session, message, &error, &output_path)) {
      state->status_text = "Exported decoded CSV " + output_path.filename().string();
    } else {
      state->status_text = error;
    }
  });
  const std::string dbc_text = session->route_data.dbc_name.empty() ? "DBC: Auto / none"
                                                                    : "DBC: " + session->route_data.dbc_name;
  const float right_w = ImGui::CalcTextSize(dbc_text.c_str()).x + 10.0f;
  ImGui::SameLine(std::max(0.0f, ImGui::GetWindowContentRegionMax().x - right_w));
  ImGui::TextDisabled("%s", dbc_text.c_str());
  ImGui::EndChild();
  ImGui::PopStyleColor(2);
  ImGui::PopStyleVar(2);
}

void draw_messages_panel(AppSession *session, UiState *state) {
  size_t signal_count = 0;
  for (const CabanaMessageSummary &message : session->cabana_messages) {
    signal_count += message.signals.size();
  }
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_messages_header", ImVec2(0.0f, 34.0f), false, ImGuiWindowFlags_NoScrollbar);
  app_push_bold_font();
  ImGui::Text("Messages: %zu", session->cabana_messages.size());
  app_pop_bold_font();
  ImGui::SameLine(0.0f, 10.0f);
  ImGui::TextDisabled("%zu decoded signals", signal_count);
  const float clear_w = 38.0f;
  ImGui::SameLine(std::max(0.0f, ImGui::GetWindowContentRegionMax().x - clear_w - 128.0f));
  if (ImGui::SmallButton("Clear")) {
    state->cabana.message_filter[0] = '\0';
  }
  ImGui::SameLine(0.0f, 8.0f);
  ImGui::Checkbox("Suppress Signals", &state->cabana.suppress_defined_signals);
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::Spacing();
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::InputTextWithHint("##cabana_message_filter", "Filter messages...", state->cabana.message_filter.data(),
                           state->cabana.message_filter.size());
  ImGui::Spacing();

  if (session->cabana_messages.empty()) {
    ImGui::TextDisabled("No CAN messages in this route.");
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
  if (ImGui::BeginTable("##cabana_messages", 6,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollX |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_BordersOuterH |
                          ImGuiTableFlags_SizingFixedFit,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 148.0f);
    ImGui::TableSetupColumn("Bus", ImGuiTableColumnFlags_WidthFixed, 36.0f);
    ImGui::TableSetupColumn("Addr", ImGuiTableColumnFlags_WidthFixed, 64.0f);
    ImGui::TableSetupColumn("Hz", ImGuiTableColumnFlags_WidthFixed, 46.0f);
    ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 50.0f);
    ImGui::TableSetupColumn("Bytes", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableHeadersRow();

    const std::string filter = trim_copy(state->cabana.message_filter.data());
    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(session->cabana_messages.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const CabanaMessageSummary &message = session->cabana_messages[static_cast<size_t>(i)];
        char addr_buf[16] = "--";
        if (message.has_address) {
          std::snprintf(addr_buf, sizeof(addr_buf), "0x%03X", message.address);
        }
        if (!filter.empty()
            && !contains_case_insensitive(message.name, filter)
            && !contains_case_insensitive(message.service, filter)
            && !contains_case_insensitive(addr_buf, filter)
            && !contains_case_insensitive(std::to_string(message.bus), filter)) {
          continue;
        }
        if (state->cabana.suppress_defined_signals && !message.signals.empty()) {
          continue;
        }

        ImGui::TableNextRow(0, 20.0f);
        ImGui::TableNextColumn();
        const bool selected = state->cabana.selected_message_root == message.root_path;
        if (ImGui::Selectable((message.name + "##" + message.root_path).c_str(), selected,
                              ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          select_cabana_message(session, state, message.root_path);
        }

        ImGui::TableNextColumn();
        ImGui::Text("%c%d", message.service == "sendcan" ? 'S' : 'C', message.bus);
        ImGui::TableNextColumn();
        ImGui::TextUnformatted(addr_buf);
        ImGui::TableNextColumn();
        if (message.frequency_hz > 0.0) {
          ImGui::Text("%.1f", message.frequency_hz);
        } else {
          ImGui::TextDisabled("--");
        }
        ImGui::TableNextColumn();
        ImGui::Text("%zu", message.sample_count);
        ImGui::TableNextColumn();
        const CanMessageData *message_data = find_message_data(*session, message);
        if (message_data != nullptr && !message_data->samples.empty()) {
          const CanFrameSample &last = message_data->samples.back();
          const CanFrameSample *prev = message_data->samples.size() > 1 ? &message_data->samples[message_data->samples.size() - 2] : nullptr;
          draw_payload_preview_boxes(("##msg_bytes_" + message.root_path).c_str(), last.data, prev == nullptr ? nullptr : &prev->data, 92.0f);
        } else {
          ImGui::TextDisabled("--");
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}

void draw_signal_toolbar(AppSession *session, UiState *state, const CabanaMessageSummary &message) {
  const size_t charted = state->cabana.chart_signal_paths.size();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_signals_header", ImVec2(0.0f, 34.0f), false, ImGuiWindowFlags_NoScrollbar);
  app_push_bold_font();
  ImGui::Text("Signals: %zu", message.signals.size());
  app_pop_bold_font();
  if (charted > 0) {
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%zu charted", charted);
  }
  ImGui::SameLine(std::max(220.0f, ImGui::GetWindowContentRegionMax().x - 188.0f));
  ImGui::SetNextItemWidth(180.0f);
  ImGui::InputTextWithHint("##cabana_signal_filter", "Filter Signal", state->cabana.signal_filter.data(),
                           state->cabana.signal_filter.size());
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

void draw_signal_selection_table(AppSession *session, UiState *state, const CabanaMessageSummary &message) {
  draw_signal_toolbar(session, state, message);
  if (message.signals.empty()) {
    ImGui::TextDisabled("No decoded signals for this message.");
    return;
  }
  const std::string filter = trim_copy(state->cabana.signal_filter.data());
  size_t visible_count = 0;
  for (const CabanaSignalSummary &signal : message.signals) {
    if (filter.empty() || contains_case_insensitive(signal.name, filter)) {
      ++visible_count;
    }
  }
  if (visible_count == 0) {
    ImGui::TextDisabled("No signals match this filter.");
    return;
  }

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 3.0f));
  if (ImGui::BeginTable("##cabana_signals", 6,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_ScrollY,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupColumn("Chart", ImGuiTableColumnFlags_WidthFixed, 42.0f);
    ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthStretch, 1.9f);
    ImGui::TableSetupColumn("Bits", ImGuiTableColumnFlags_WidthFixed, 66.0f);
    ImGui::TableSetupColumn("Trend", ImGuiTableColumnFlags_WidthFixed, 152.0f);
    ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("Edit", ImGuiTableColumnFlags_WidthFixed, 54.0f);
    ImGui::TableHeadersRow();
    for (const CabanaSignalSummary &signal : message.signals) {
      if (!filter.empty() && !contains_case_insensitive(signal.name, filter)) {
        continue;
      }
      const bool selected = std::find(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(), signal.path)
                         != state->cabana.chart_signal_paths.end();
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      bool checked = selected;
      const std::string checkbox_id = "##chart_" + signal.path;
      if (ImGui::Checkbox(checkbox_id.c_str(), &checked)) {
        if (checked) {
          state->cabana.chart_signal_paths.push_back(signal.path);
        } else {
          state->cabana.chart_signal_paths.erase(
            std::remove(state->cabana.chart_signal_paths.begin(), state->cabana.chart_signal_paths.end(), signal.path),
            state->cabana.chart_signal_paths.end());
        }
      }
      ImGui::TableNextColumn();
      ImGui::PushID(signal.path.c_str());
      ImGui::ColorButton("##sig_color",
                         ImGui::ColorConvertU32ToFloat4(signal_fill_color(std::hash<std::string>{}(signal.path), 0.55f, selected)),
                         ImGuiColorEditFlags_NoTooltip,
                         ImVec2(10.0f, 10.0f));
      ImGui::SameLine(0.0f, 6.0f);
      if (ImGui::Selectable((signal.name + "##name").c_str(), selected, ImGuiSelectableFlags_SpanAllColumns)) {
        state->cabana.chart_signal_paths = {signal.path};
      }
      ImGui::PopID();
      ImGui::TableNextColumn();
      if (signal.has_bit_range) {
        ImGui::TextDisabled("%d|%d", signal.start_bit, signal.size);
      } else {
        ImGui::TextDisabled("--");
      }
      ImGui::TableNextColumn();
      draw_signal_sparkline(*session, *state, signal.path, selected);
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(cabana_value_label(*session, signal.path, state->tracker_time).c_str());
      ImGui::TableNextColumn();
      if (ImGui::SmallButton(("Edit##" + signal.path).c_str())) {
        open_cabana_signal_editor(*session, state, message, signal);
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}

void draw_logs_toolbar(const AppSession &session,
                       UiState *state,
                       const CabanaMessageSummary &message,
                       bool can_show_signal_mode,
                       bool show_signal_mode) {
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_logs_toolbar", ImVec2(0.0f, 34.0f), false, ImGuiWindowFlags_NoScrollbar);
  ImGui::AlignTextToFramePadding();
  ImGui::TextUnformatted("Display:");
  ImGui::SameLine(0.0f, 6.0f);
  ImGui::BeginDisabled(!can_show_signal_mode);
  if (ImGui::RadioButton("Signal", show_signal_mode)) {
    state->cabana.logs_hex_mode = false;
  }
  ImGui::EndDisabled();
  ImGui::SameLine(0.0f, 8.0f);
  if (ImGui::RadioButton("Hex", !show_signal_mode)) {
    state->cabana.logs_hex_mode = true;
  }

  const std::string signal_path = !state->cabana.chart_signal_paths.empty() ? state->cabana.chart_signal_paths.front() : std::string();
  if (can_show_signal_mode) {
    const size_t slash = signal_path.find_last_of('/');
    const std::string signal_name = slash == std::string::npos ? signal_path : signal_path.substr(slash + 1);
    ImGui::SameLine(0.0f, 12.0f);
    ImGui::TextDisabled("%s", signal_name.c_str());
    if (show_signal_mode) {
      static constexpr const char *kOps[] = {">", "=", "!=", "<"};
      ImGui::SameLine(0.0f, 10.0f);
      ImGui::SetNextItemWidth(54.0f);
      ImGui::Combo("##cabana_logs_cmp", &state->cabana.logs_filter_compare, kOps, IM_ARRAYSIZE(kOps));
      ImGui::SameLine(0.0f, 6.0f);
      ImGui::SetNextItemWidth(96.0f);
      ImGui::InputTextWithHint("##cabana_logs_value", "value", state->cabana.logs_filter_value.data(),
                               state->cabana.logs_filter_value.size());
    }
  }

  const float export_w = 76.0f;
  ImGui::SameLine(std::max(0.0f, ImGui::GetWindowContentRegionMax().x - export_w));
  if (ImGui::SmallButton("Export")) {
    fs::path output_path;
    std::string error;
    const bool ok = show_signal_mode ? export_decoded_can_csv(session, message, &error, &output_path)
                                     : export_raw_can_csv(session, message, &error, &output_path);
    state->status_text = ok ? ("Exported " + output_path.filename().string()) : error;
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

void draw_message_history(const AppSession &session, UiState *state, const CabanaMessageSummary &message) {
  const CanMessageData *message_data = find_message_data(session, message);
  if (message_data == nullptr || message_data->samples.empty()) {
    ImGui::TextDisabled("No frame history available.");
    return;
  }

  const std::string signal_path = !state->cabana.chart_signal_paths.empty()
    ? state->cabana.chart_signal_paths.front()
    : std::string();
  const RouteSeries *series = signal_path.empty() ? nullptr : app_find_route_series(session, signal_path);
  const auto format_it = signal_path.empty() ? session.route_data.series_formats.end() : session.route_data.series_formats.find(signal_path);
  const auto enum_it = signal_path.empty() ? session.route_data.enum_info.end() : session.route_data.enum_info.find(signal_path);
  const size_t current_index = closest_can_sample_index(*message_data, state->tracker_time);
  const bool can_show_signal_mode = series != nullptr;
  const bool show_signal_mode = can_show_signal_mode && !state->cabana.logs_hex_mode;

  draw_logs_toolbar(session, state, message, can_show_signal_mode, show_signal_mode);

  const bool have_filter = show_signal_mode && state->cabana.logs_filter_value[0] != '\0';
  const double filter_value = have_filter ? std::strtod(state->cabana.logs_filter_value.data(), nullptr) : 0.0;
  auto passes_filter = [&](double value) {
    if (!have_filter) return true;
    switch (state->cabana.logs_filter_compare) {
      case 0: return value > filter_value;
      case 1: return value == filter_value;
      case 2: return value != filter_value;
      case 3: return value < filter_value;
      default: return true;
    }
  };

  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(5.0f, 3.0f));
  const int columns = (!show_signal_mode && series != nullptr) ? 4 : 3;
  if (ImGui::BeginTable("##cabana_history", columns,
                        ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_ScrollY |
                          ImGuiTableFlags_SizingStretchProp |
                          ImGuiTableFlags_BordersInnerV |
                          ImGuiTableFlags_BordersOuterH |
                          ImGuiTableFlags_NoPadOuterX,
                        ImGui::GetContentRegionAvail())) {
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, 96.0f);
    ImGui::TableSetupColumn("dt", ImGuiTableColumnFlags_WidthFixed, 72.0f);
    if (show_signal_mode) {
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    } else {
      ImGui::TableSetupColumn("Data", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    }
    if (!show_signal_mode && series != nullptr) {
      ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed, 108.0f);
    }
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;
    clipper.Begin(static_cast<int>(message_data->samples.size()));
    while (clipper.Step()) {
      for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; ++i) {
        const CanFrameSample &sample = message_data->samples[static_cast<size_t>(i)];
        const CanFrameSample *prev = i > 0 ? &message_data->samples[static_cast<size_t>(i - 1)] : nullptr;
        std::optional<double> value;
        if (series != nullptr) {
          value = app_sample_xy_value_at_time(series->times, series->values, false, sample.mono_time);
          if (show_signal_mode && (!value.has_value() || !passes_filter(*value))) {
            continue;
          }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool selected = static_cast<size_t>(i) == current_index;
        char label[32];
        std::snprintf(label, sizeof(label), "%.3f##frame_%d", sample.mono_time, i);
        if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          state->tracker_time = sample.mono_time;
          state->has_tracker_time = true;
        }

        ImGui::TableNextColumn();
        if (prev != nullptr) {
          ImGui::Text("%.1fms", 1000.0 * (sample.mono_time - prev->mono_time));
        } else {
          ImGui::TextDisabled("--");
        }

        ImGui::TableNextColumn();
        if (show_signal_mode) {
          if (value.has_value()) {
            if (format_it != session.route_data.series_formats.end()) {
              ImGui::TextUnformatted(format_display_value(*value,
                                                          format_it->second,
                                                          enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second).c_str());
            } else {
              ImGui::Text("%.4f", *value);
            }
          } else {
            ImGui::TextDisabled("--");
          }
        } else {
          draw_payload_bytes(sample.data, prev == nullptr ? nullptr : &prev->data);
        }

        if (!show_signal_mode && series != nullptr) {
          ImGui::TableNextColumn();
          if (value.has_value()) {
            if (format_it != session.route_data.series_formats.end()) {
              ImGui::TextUnformatted(format_display_value(*value,
                                                          format_it->second,
                                                          enum_it == session.route_data.enum_info.end() ? nullptr : &enum_it->second).c_str());
            } else {
              ImGui::Text("%.4f", *value);
            }
          } else {
            ImGui::TextDisabled("--");
          }
        }
      }
    }
    ImGui::EndTable();
  }
  ImGui::PopStyleVar();
}

void draw_detail_panel(AppSession *session, UiState *state, const CabanaMessageSummary &message, float top_height) {
  const CanMessageData *message_data = find_message_data(*session, message);
  draw_cabana_message_tabs(session, state);
  draw_detail_toolbar(session, state, message, message_data);
  std::vector<std::string> warnings;
  if (message_has_overlaps(message, message_data)) {
    warnings.push_back("One or more decoded signals overlap in the binary view.");
  }
  draw_cabana_warning_banner(warnings);
  ImGui::Spacing();

  const float bottom_tabs_h = 30.0f;
  ImGui::BeginChild("##cabana_detail_content", ImVec2(0.0f, std::max(0.0f, ImGui::GetContentRegionAvail().y - bottom_tabs_h)), false);
  if (state->cabana.detail_tab == 0) {
    ImGui::BeginChild("##cabana_msg_top", ImVec2(0.0f, top_height), false);
    if (message_data != nullptr) {
      draw_can_frame_view(*message_data, session, message, state, state->tracker_time);
    } else {
      draw_empty_panel("Binary View", "No raw CAN frames available for this message.");
    }
    ImGui::EndChild();
    draw_horizontal_splitter("##cabana_detail_splitter", ImGui::GetContentRegionAvail().x, kMinTopHeight,
                             std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight),
                             &state->cabana.detail_top_height);
    ImGui::BeginChild("##cabana_signals_bottom", ImVec2(0.0f, 0.0f), false);
    draw_signal_selection_table(session, state, message);
    ImGui::EndChild();
  } else {
    if (message_data != nullptr) {
      draw_can_heatmap(*message_data, highlighted_signals(message, *state), state->tracker_time);
      ImGui::Spacing();
    }
    draw_message_history(*session, state, message);
  }
  ImGui::EndChild();
  draw_cabana_detail_tab_strip(state);
}

void draw_video_panel(AppSession *session, UiState *state, float height) {
  const auto &views = camera_view_specs();
  std::vector<const CameraViewSpec *> available_views;
  available_views.reserve(views.size());
  for (const CameraViewSpec &spec : views) {
    if (!(session->route_data.*(spec.route_member)).entries.empty()) {
      available_views.push_back(&spec);
    }
  }

  draw_cabana_panel_title("Video");

  if (available_views.empty()) {
    ImGui::BeginChild("##cabana_video_empty", ImVec2(0.0f, height), false);
    ImGui::TextDisabled("No camera streams available.");
    ImGui::EndChild();
    return;
  }

  if (std::none_of(available_views.begin(), available_views.end(), [&](const CameraViewSpec *spec) {
        return spec->view == state->cabana.camera_view;
      })) {
    state->cabana.camera_view = available_views.front()->view;
  }

  auto short_label = [](const CameraViewSpec &spec) {
    switch (spec.view) {
      case CameraViewKind::Road: return "Road";
      case CameraViewKind::Driver: return "Driver";
      case CameraViewKind::WideRoad: return "Wide";
      case CameraViewKind::QRoad: return "qRoad";
    }
    return "Cam";
  };

  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_video_header", ImVec2(0.0f, 32.0f), false, ImGuiWindowFlags_NoScrollbar);
  app_push_bold_font();
  ImGui::TextUnformatted("Video");
  app_pop_bold_font();
  ImGui::SameLine(0.0f, 10.0f);
  for (size_t i = 0; i < available_views.size(); ++i) {
    const CameraViewSpec &spec = *available_views[i];
    if (i > 0) ImGui::SameLine(0.0f, 4.0f);
    const float width = spec.view == CameraViewKind::Driver ? 66.0f : 58.0f;
    if (draw_cabana_bottom_tab(("##video_" + std::to_string(i)).c_str(),
                               short_label(spec),
                               state->cabana.camera_view == spec.view,
                               width)) {
      state->cabana.camera_view = spec.view;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  const CameraViewSpec &active_spec = camera_view_spec(state->cabana.camera_view);
  CameraFeedView *feed = session->pane_camera_feeds[static_cast<size_t>(active_spec.view)].get();
  if (feed != nullptr && state->has_tracker_time) {
    feed->update(state->tracker_time);
  }
  if (feed == nullptr) {
    ImGui::TextDisabled("Camera unavailable");
    return;
  }

  const float controls_h = 58.0f;
  feed->drawSized(ImVec2(ImGui::GetContentRegionAvail().x, std::max(0.0f, height - controls_h)),
                  session->async_route_loading,
                  true);

  const double current = state->has_tracker_time ? state->tracker_time : session->route_data.x_min;
  const double total = session->route_data.has_time_range ? session->route_data.x_max : current;
  double slider_value = current;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(247, 248, 250));
  ImGui::PushStyleColor(ImGuiCol_Border, color_rgb(209, 214, 220));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 5.0f));
  ImGui::BeginChild("##cabana_video_controls", ImVec2(0.0f, controls_h), true, ImGuiWindowFlags_NoScrollbar);
  const float button_w = 24.0f;
  if (ImGui::Button("|<", ImVec2(button_w, 0.0f))) {
    step_tracker(state, -1.0);
  }
  ImGui::SameLine(0.0f, 4.0f);
  if (ImGui::Button(state->playback_playing ? "||" : ">", ImVec2(button_w, 0.0f))) {
    state->playback_playing = !state->playback_playing;
  }
  ImGui::SameLine(0.0f, 4.0f);
  if (ImGui::Button(">|", ImVec2(button_w, 0.0f))) {
    step_tracker(state, 1.0);
  }
  ImGui::SameLine(0.0f, 8.0f);
  ImGui::TextDisabled("%s / %s", format_cabana_time(current).c_str(), format_cabana_time(total).c_str());
  if (session->route_data.has_time_range) {
    ImGui::SetNextItemWidth(-1.0f);
    if (ImGui::SliderScalar("##cabana_video_slider",
                            ImGuiDataType_Double,
                            &slider_value,
                            &session->route_data.x_min,
                            &session->route_data.x_max,
                            "")) {
      state->tracker_time = slider_value;
      state->has_tracker_time = true;
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(2);
}

void draw_chart_panel(AppSession *session, UiState *state, const CabanaMessageSummary *message) {
  const size_t chart_count = state->cabana.chart_signal_paths.size();
  ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(245, 246, 248));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(6.0f, 4.0f));
  ImGui::BeginChild("##cabana_charts_header", ImVec2(0.0f, 30.0f), false, ImGuiWindowFlags_NoScrollbar);
  app_push_bold_font();
  ImGui::Text("Charts: %zu", chart_count);
  app_pop_bold_font();
  if (chart_count > 0) {
    ImGui::SameLine(0.0f, 10.0f);
    ImGui::TextDisabled("%s", chart_count == 1 ? "1 selected signal" : "selected signals");
    const float clear_w = 44.0f;
    ImGui::SameLine(std::max(0.0f, ImGui::GetWindowContentRegionMax().x - clear_w));
    if (ImGui::SmallButton("Clear")) {
      state->cabana.chart_signal_paths.clear();
    }
  }
  ImGui::EndChild();
  ImGui::PopStyleVar();
  ImGui::PopStyleColor();

  if (message == nullptr || state->cabana.chart_signal_paths.empty()) {
    ImGui::BeginChild("##cabana_chart_empty", ImVec2(0.0f, 0.0f), false);
    ImGui::TextDisabled("Select one or more signals to chart.");
    ImGui::EndChild();
    return;
  }

  Pane pane;
  pane.kind = PaneKind::Plot;
  pane.title = message->name;
  for (const std::string &path : state->cabana.chart_signal_paths) {
    Curve curve;
    curve.name = path;
    curve.color = app_next_curve_color(pane);
    pane.curves.push_back(std::move(curve));
  }
  ImGui::BeginChild("##cabana_chart_plot", ImVec2(0.0f, 0.0f), false);
  draw_plot(*session, &pane, state);
  ImGui::EndChild();
}

}  // namespace

void rebuild_cabana_messages(AppSession *session) {
  std::vector<CabanaMessageSummary> messages;
  const std::optional<dbc::Database> db = load_active_dbc(*session);
  const DbcNameLookup dbc_lookup = build_dbc_name_lookup(db);
  std::unordered_map<std::string, std::vector<std::string>> signal_paths_by_key;

  for (const std::string &path : session->route_data.paths) {
    std::string_view service_text;
    std::string_view message_name;
    std::string_view signal_name;
    int bus = -1;
    if (!parse_can_message_path(path, &service_text, &bus, &message_name, &signal_name)) {
      continue;
    }
    const std::optional<CanServiceKind> service = parse_can_service_kind(service_text);
    if (!service.has_value()) {
      continue;
    }
    auto addr_it = dbc_lookup.unique_name_to_address.find(std::string(message_name));
    if (addr_it == dbc_lookup.unique_name_to_address.end()) {
      continue;
    }
    signal_paths_by_key[can_message_key(*service, static_cast<uint8_t>(bus), addr_it->second)].push_back(path);
  }

  messages.reserve(session->route_data.can_messages.size());
  for (const CanMessageData &message_data : session->route_data.can_messages) {
    const dbc::Message *dbc_message = db.has_value() ? db->message(message_data.id.address) : nullptr;
    const std::string key = can_message_key(message_data.id.service, message_data.id.bus, message_data.id.address);
    CabanaMessageSummary message{
      .root_path = key,
      .service = can_service_name(message_data.id.service),
      .name = dbc_message != nullptr ? dbc_message->name : format_can_address(message_data.id.address),
      .bus = static_cast<int>(message_data.id.bus),
      .address = message_data.id.address,
      .has_address = true,
      .sample_count = message_data.samples.size(),
    };
    auto signal_it = signal_paths_by_key.find(key);
    if (signal_it != signal_paths_by_key.end()) {
      std::unordered_map<std::string, const dbc::Signal *> signals_by_name;
      if (dbc_message != nullptr) {
        for (const dbc::Signal &signal : dbc_message->signals) {
          signals_by_name.emplace(signal.name, &signal);
        }
      }
      message.signals.reserve(signal_it->second.size());
      for (std::string &path : signal_it->second) {
        const size_t slash = path.find_last_of('/');
        const std::string name = slash == std::string::npos ? path : path.substr(slash + 1);
        CabanaSignalSummary signal_summary{
          .path = std::move(path),
          .name = name,
        };
        auto dbc_signal = signals_by_name.find(name);
        if (dbc_signal != signals_by_name.end()) {
          signal_summary.start_bit = dbc_signal->second->start_bit;
          signal_summary.msb = dbc_signal->second->msb;
          signal_summary.lsb = dbc_signal->second->lsb;
          signal_summary.size = dbc_signal->second->size;
          signal_summary.is_little_endian = dbc_signal->second->is_little_endian;
          signal_summary.has_bit_range = true;
        }
        message.signals.push_back(std::move(signal_summary));
      }
    }
    if (message_data.samples.size() > 1
        && message_data.samples.back().mono_time > message_data.samples.front().mono_time) {
      message.frequency_hz = static_cast<double>(message_data.samples.size() - 1)
                           / (message_data.samples.back().mono_time - message_data.samples.front().mono_time);
    }
    messages.push_back(std::move(message));
  }

  std::sort(messages.begin(), messages.end(), [](const CabanaMessageSummary &a, const CabanaMessageSummary &b) {
    return std::make_tuple(a.service, a.bus, a.has_address ? 0 : 1, a.address, a.name)
         < std::make_tuple(b.service, b.bus, b.has_address ? 0 : 1, b.address, b.name);
  });
  session->cabana_messages = std::move(messages);
}

void draw_cabana_mode(AppSession *session, const UiMetrics &ui, UiState *state) {
  sync_cabana_selection(session, state);

  ImGui::SetNextWindowPos(ImVec2(ui.content_x, ui.content_y));
  ImGui::SetNextWindowSize(ImVec2(ui.content_w, ui.content_h));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4.0f, 4.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(5.0f, 3.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4.0f, 2.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0.0f);
  ImGui::PushStyleVar(ImGuiStyleVar_GrabRounding, 0.0f);
  ImGui::PushStyleColor(ImGuiCol_WindowBg, color_rgb(238, 240, 243));
  const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_NoMove |
                                 ImGuiWindowFlags_NoResize |
                                 ImGuiWindowFlags_NoSavedSettings;
  if (ImGui::Begin("##cabana_mode_host", nullptr, flags)) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    auto begin_panel = [&](const char *id, const ImVec2 &size) {
      ImGui::PushStyleColor(ImGuiCol_ChildBg, color_rgb(255, 255, 255));
      ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8.0f, 8.0f));
      return ImGui::BeginChild(id, size, false, ImGuiWindowFlags_NoScrollbar);
    };
    auto end_panel = [&]() {
      ImGui::EndChild();
      ImGui::PopStyleVar();
      ImGui::PopStyleColor();
    };

    const CabanaMessageSummary *message = find_selected_message(*session, *state);
    const float min_center_width = message != nullptr ? kMinCenterWidth : 140.0f;
    float messages_width = std::clamp(state->cabana.messages_width, kMinMessagesWidth,
                                      std::max(kMinMessagesWidth, avail.x - min_center_width - kMinRightWidth - 2.0f * kSplitterThickness));
    float right_width = std::clamp(state->cabana.right_width, kMinRightWidth,
                                   std::max(kMinRightWidth, avail.x - messages_width - min_center_width - 2.0f * kSplitterThickness));
    const float max_messages = std::max(kMinMessagesWidth, avail.x - right_width - min_center_width - 2.0f * kSplitterThickness);
    messages_width = std::min(messages_width, max_messages);
    const float center_width = std::max(min_center_width, avail.x - messages_width - right_width - 2.0f * kSplitterThickness);
    right_width = avail.x - messages_width - center_width - 2.0f * kSplitterThickness;
    state->cabana.messages_width = messages_width;
    state->cabana.right_width = right_width;

    const ImVec2 origin = ImGui::GetCursorPos();
    begin_panel("##cabana_messages_panel", ImVec2(messages_width, avail.y));
    draw_messages_panel(session, state);
    end_panel();

    const float center_height = avail.y;
    ImGui::SetCursorPos(ImVec2(origin.x + messages_width, origin.y));
    draw_vertical_splitter("##cabana_left_splitter", avail.y, kMinMessagesWidth,
                           std::max(kMinMessagesWidth, avail.x - min_center_width - right_width - 2.0f * kSplitterThickness),
                           &state->cabana.messages_width);
    ImGui::SetCursorPos(ImVec2(origin.x + messages_width + kSplitterThickness, origin.y));
    begin_panel("##cabana_detail_panel", ImVec2(center_width, center_height));
    if (message == nullptr) {
      draw_cabana_welcome_panel();
    } else {
      state->cabana.detail_top_height = std::clamp(state->cabana.detail_top_height, kMinTopHeight,
                                                   std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight - kSplitterThickness));
      draw_detail_panel(session, state, *message, state->cabana.detail_top_height);
    }
    end_panel();
    ImGui::SetCursorPos(ImVec2(origin.x + messages_width + kSplitterThickness + center_width, origin.y));
    draw_right_splitter("##cabana_right_splitter", avail.y, kMinRightWidth,
                        std::max(kMinRightWidth, avail.x - state->cabana.messages_width - kMinCenterWidth - 2.0f * kSplitterThickness),
                        &state->cabana.right_width);

    ImGui::SetCursorPos(ImVec2(origin.x + messages_width + center_width + 2.0f * kSplitterThickness, origin.y));
    begin_panel("##cabana_right_panel", ImVec2(right_width, center_height));
    const float right_avail_y = ImGui::GetContentRegionAvail().y;
    state->cabana.right_top_height = std::clamp(state->cabana.right_top_height, kMinTopHeight,
                                                std::max(kMinTopHeight, right_avail_y - kMinBottomHeight - kSplitterThickness));
    draw_video_panel(session, state, state->cabana.right_top_height);
    draw_horizontal_splitter("##cabana_right_hsplit", ImGui::GetContentRegionAvail().x, kMinTopHeight,
                             std::max(kMinTopHeight, ImGui::GetContentRegionAvail().y - kMinBottomHeight),
                             &state->cabana.right_top_height);
    draw_chart_panel(session, state, message);
    end_panel();
    ImGui::SetCursorPos(ImVec2(origin.x, origin.y));
    ImGui::Dummy(avail);
  }
  ImGui::End();
  ImGui::PopStyleColor();
  ImGui::PopStyleVar(6);
}
