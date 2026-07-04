/* SPDX-FileCopyrightText: 2026 Usedesktop Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * BlendGym UI state exporter.
 */

#include <cctype>
#include <cstring>
#include <initializer_list>
#include <ios>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>

#include "DNA_ID.h"
#include "DNA_screen_types.h"
#include "DNA_workspace_types.h"

#include "BLI_fileops.hh"
#include "BLI_path_utils.hh"
#include "BLI_rect.hh"
#include "BLI_serialize.hh"
#include "BLI_string.hh"
#include "BLI_string_ref.hh"
#include "BLI_time.hh"
#include "BLI_utildefines.hh"
#include "BLI_vector.hh"

#include "BKE_appdir.hh"
#include "BKE_context.hh"
#include "BKE_global.hh"

#include "BLO_writefile.hh"

#include "RNA_access.hh"

#include "WM_types.hh"

#include "BG_ui_state.hh"
#include "interface_intern.hh"

namespace blender::blendgym {
namespace {

using ui::ButtonType;
using ui::BUT_DISABLED;
using ui::BUT_INACTIVE;
using ui::UI_HIDDEN;
using ui::UI_SCROLLED;

constexpr const char *BLENDGYM_STATE_DUMP_ENV = "BLENDGYM_STATE_DUMP";
constexpr const char *BLENDGYM_STATE_FILE_ENV = "BLENDGYM_STATE_FILE";
constexpr const char *BLENDGYM_DISPATCH_FILE_ENV = "BLENDGYM_DISPATCH_FILE";
constexpr const char *BLENDGYM_COMMAND_FILE_ENV = "BLENDGYM_COMMAND_FILE";
constexpr double CAPTURE_PASS_GAP_SECONDS = 0.05;

struct RlActionSnapshot {
  std::string catalog_id;
  std::string instance_id;
  std::string kind;
  std::string label;
  std::string block_name;
  std::string button_type;
  std::string operator_id;
  std::string rna_struct;
  std::string rna_property;
  int rna_index = -1;
  int op_context = 0;
  int block_flags = 0;
  int button_flags = 0;
  bool visible = true;
  bool enabled = false;
  std::string disabled_info;
  rcti bbox_px = {};
  rcti bbox_win_px = {};
};

struct RlStateBuffer {
  Vector<RlActionSnapshot> actions;
  double last_capture_time = 0.0;
  int64_t pass_id = 0;
  std::string last_command_id;
};

struct RlCommand {
  std::string command_id;
  std::string command;
  std::string checkpoint_file;
  std::string response_file;
  std::string state_id;
};

RlStateBuffer &state_buffer()
{
  static RlStateBuffer buffer;
  return buffer;
}

int64_t next_dispatch_event_id()
{
  static int64_t event_id = 0;
  event_id++;
  return event_id;
}

bool state_dump_enabled()
{
  const char *value = BLI_getenv(BLENDGYM_STATE_DUMP_ENV);
  if (value == nullptr || value[0] == '\0') {
    return false;
  }
  return !(STREQ(value, "0") || STREQ(value, "false") || STREQ(value, "FALSE"));
}

std::string id_name_or_empty(const ID *id)
{
  if (id == nullptr || id->name[0] == '\0') {
    return "";
  }
  return id->name + 2;
}

std::string string_ref_to_std(const StringRef value)
{
  if (value.size() == 0) {
    return "";
  }
  return std::string(value.data(), value.size());
}

std::string sanitize_id_part(const std::string &value)
{
  std::string result;
  result.reserve(value.size());
  bool last_was_separator = false;

  for (const unsigned char c : value) {
    if (std::isalnum(c)) {
      result.push_back(char(std::tolower(c)));
      last_was_separator = false;
    }
    else if (!last_was_separator) {
      result.push_back('_');
      last_was_separator = true;
    }
  }

  while (!result.empty() && result.front() == '_') {
    result.erase(result.begin());
  }
  while (!result.empty() && result.back() == '_') {
    result.pop_back();
  }

  return result.empty() ? "unknown" : result;
}

const char *button_type_name(const ButtonType type)
{
  switch (type) {
    case ButtonType::But:
      return "button";
    case ButtonType::Row:
      return "row";
    case ButtonType::Text:
      return "text";
    case ButtonType::TextBox:
      return "textbox";
    case ButtonType::Menu:
      return "menu";
    case ButtonType::ButMenu:
      return "button_menu";
    case ButtonType::Num:
      return "number";
    case ButtonType::NumSlider:
      return "number_slider";
    case ButtonType::Toggle:
      return "toggle";
    case ButtonType::ToggleN:
      return "toggle_n";
    case ButtonType::IconToggle:
      return "icon_toggle";
    case ButtonType::IconToggleN:
      return "icon_toggle_n";
    case ButtonType::ButToggle:
      return "button_toggle";
    case ButtonType::Checkbox:
      return "checkbox";
    case ButtonType::CheckboxN:
      return "checkbox_n";
    case ButtonType::Color:
      return "color";
    case ButtonType::Tab:
      return "tab";
    case ButtonType::Popover:
      return "popover";
    case ButtonType::Scroll:
      return "scroll";
    case ButtonType::Block:
      return "block";
    case ButtonType::Label:
      return "label";
    case ButtonType::KeyEvent:
      return "key_event";
    case ButtonType::HsvCube:
      return "hsv_cube";
    case ButtonType::Pulldown:
      return "pulldown";
    case ButtonType::Roundbox:
      return "roundbox";
    case ButtonType::ColorBand:
      return "color_band";
    case ButtonType::Unitvec:
      return "unit_vector";
    case ButtonType::Curve:
      return "curve";
    case ButtonType::CurveProfile:
      return "curve_profile";
    case ButtonType::ListBox:
      return "list_box";
    case ButtonType::ListRow:
      return "list_row";
    case ButtonType::HsvCircle:
      return "hsv_circle";
    case ButtonType::TrackPreview:
      return "track_preview";
    case ButtonType::SearchMenu:
      return "search_menu";
    case ButtonType::Extra:
      return "extra";
    case ButtonType::PreviewTile:
      return "preview_tile";
    case ButtonType::HotkeyEvent:
      return "hotkey_event";
    case ButtonType::Image:
      return "image";
    case ButtonType::Histogram:
      return "histogram";
    case ButtonType::Waveform:
      return "waveform";
    case ButtonType::Vectorscope:
      return "vectorscope";
    case ButtonType::Progress:
      return "progress";
    case ButtonType::NodeSocket:
      return "node_socket";
    case ButtonType::Sepr:
      return "separator";
    case ButtonType::SeprLine:
      return "separator_line";
    case ButtonType::SeprSpacer:
      return "separator_spacer";
    case ButtonType::Grip:
      return "grip";
    case ButtonType::Decorator:
      return "decorator";
    case ButtonType::ViewItem:
      return "view_item";
  }
  return "unknown";
}

std::string action_kind_for_button(const ui::Button &but)
{
  if (but.optype != nullptr) {
    return "operator";
  }
  if (but.rnapoin.type != nullptr && but.rnaprop != nullptr) {
    return "rna";
  }
  if (but.type == ButtonType::Tab) {
    return "workspace_or_tab";
  }
  return "ui";
}

void append_json_string(std::string &out, const std::string &value)
{
  out.push_back('"');
  for (const unsigned char c : value) {
    switch (c) {
      case '\\':
        out += "\\\\";
        break;
      case '"':
        out += "\\\"";
        break;
      case '\b':
        out += "\\b";
        break;
      case '\f':
        out += "\\f";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (c < 0x20) {
          const char hex[] = "0123456789abcdef";
          out += "\\u00";
          out.push_back(hex[c >> 4]);
          out.push_back(hex[c & 0x0f]);
        }
        else {
          out.push_back(char(c));
        }
        break;
    }
  }
  out.push_back('"');
}

void append_json_key(std::string &out, const char *key)
{
  append_json_string(out, key);
  out += ": ";
}

void append_json_key_string(std::string &out, const char *key, const std::string &value)
{
  append_json_key(out, key);
  append_json_string(out, value);
}

void append_json_string_array(std::string &out, const std::initializer_list<const char *> values)
{
  out += "[";
  int index = 0;
  for (const char *value : values) {
    if (index > 0) {
      out += ", ";
    }
    append_json_string(out, value ? value : "");
    index++;
  }
  out += "]";
}

void append_json_key_int(std::string &out, const char *key, const int value)
{
  append_json_key(out, key);
  out += std::to_string(value);
}

void append_json_key_bool(std::string &out, const char *key, const bool value)
{
  append_json_key(out, key);
  out += value ? "true" : "false";
}

void append_json_key_double(std::string &out, const char *key, const double value)
{
  append_json_key(out, key);
  out += std::to_string(value);
}

void resolve_blendgym_filepath(char filepath[FILE_MAX], const char *env_key, const char *filename)
{
  const char *override_filepath = BLI_getenv(env_key);
  if (override_filepath != nullptr && override_filepath[0] != '\0') {
    BLI_strncpy(filepath, override_filepath, FILE_MAX);
    return;
  }
  BLI_path_join(filepath, FILE_MAX, BKE_tempdir_base(), "blendgym", "state", filename);
}

std::optional<std::string> lookup_string(const io::serialize::DictionaryValue &dict,
                                         const StringRef key)
{
  const std::optional<StringRefNull> value = dict.lookup_str(key);
  if (!value.has_value()) {
    return std::nullopt;
  }
  return std::string(value->c_str());
}

std::optional<RlCommand> read_pending_command_from_file(const char *filepath)
{
  if (filepath == nullptr || filepath[0] == '\0' || !BLI_exists(filepath)) {
    return std::nullopt;
  }

  blender::fstream file(filepath, std::ios::in);
  if (!file.is_open()) {
    return std::nullopt;
  }

  io::serialize::JsonFormatter formatter;
  std::unique_ptr<io::serialize::Value> root = formatter.deserialize(file);
  if (!root) {
    return std::nullopt;
  }

  const io::serialize::DictionaryValue *dict = root->as_dictionary_value();
  if (dict == nullptr) {
    return std::nullopt;
  }

  RlCommand command;
  command.command_id = lookup_string(*dict, "command_id").value_or("");
  command.command = lookup_string(*dict, "command").value_or("");
  command.checkpoint_file = lookup_string(*dict, "checkpoint_file").value_or("");
  command.response_file = lookup_string(*dict, "response_file").value_or("");
  command.state_id = lookup_string(*dict, "state_id").value_or("");

  if (command.command_id.empty() || command.command.empty()) {
    return std::nullopt;
  }
  return command;
}

std::string build_command_response_json(const RlCommand &command,
                                        const bool ok,
                                        const std::string &message)
{
  std::string out;
  out += "{\n";
  append_json_key_string(out, "schema", "blendgym.command_response.v0");
  out += ",\n";
  append_json_key_string(out, "command_id", command.command_id);
  out += ",\n";
  append_json_key_string(out, "command", command.command);
  out += ",\n";
  append_json_key_bool(out, "ok", ok);
  out += ",\n";
  append_json_key_double(out, "timestamp", BLI_time_now_seconds());
  out += ",\n";
  append_json_key_string(out, "state_id", command.state_id);
  out += ",\n";
  append_json_key_string(out, "checkpoint_file", command.checkpoint_file);
  out += ",\n";
  append_json_key_string(out, "message", message);
  out += "\n}\n";
  return out;
}

void write_command_response(const RlCommand &command, const bool ok, const std::string &message)
{
  if (command.response_file.empty()) {
    return;
  }
  BLI_file_ensure_parent_dir_exists(command.response_file.c_str());
  blender::fstream file(command.response_file, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    return;
  }
  file << build_command_response_json(command, ok, message);
}

bool write_blend_checkpoint(const RlCommand &command, std::string &r_message)
{
  if (command.checkpoint_file.empty()) {
    r_message = "missing checkpoint_file";
    return false;
  }
  if (G_MAIN == nullptr) {
    r_message = "G_MAIN is null";
    return false;
  }

  BLI_file_ensure_parent_dir_exists(command.checkpoint_file.c_str());

  BlendFileWriteParams params{};
  params.remap_mode = BLO_WRITE_PATH_REMAP_RELATIVE;
  params.use_save_versions = false;
  params.use_save_as_copy = true;

  const bool ok = BLO_write_file(G_MAIN, command.checkpoint_file.c_str(), G.fileflags, &params, nullptr);
  r_message = ok ? "checkpoint written" : "BLO_write_file failed";
  return ok;
}

void process_pending_command_if_needed(const bContext *C)
{
  if (!state_dump_enabled() || C == nullptr) {
    return;
  }

  const char *command_filepath = BLI_getenv(BLENDGYM_COMMAND_FILE_ENV);
  const std::optional<RlCommand> command = read_pending_command_from_file(command_filepath);
  if (!command.has_value()) {
    return;
  }

  RlStateBuffer &buffer = state_buffer();
  if (buffer.last_command_id == command->command_id) {
    return;
  }
  buffer.last_command_id = command->command_id;

  bool ok = false;
  std::string message;
  if (command->command == "snapshot") {
    ok = write_blend_checkpoint(*command, message);
  }
  else {
    message = "unsupported command";
  }
  write_command_response(*command, ok, message);
}

void replace_or_append_action(RlStateBuffer &buffer, RlActionSnapshot action)
{
  for (RlActionSnapshot &existing : buffer.actions) {
    if (existing.instance_id == action.instance_id) {
      existing = std::move(action);
      return;
    }
  }
  buffer.actions.append(std::move(action));
}

void begin_new_capture_pass_if_needed(RlStateBuffer &buffer)
{
  const double now = BLI_time_now_seconds();
  if (buffer.last_capture_time == 0.0 ||
      (now - buffer.last_capture_time) > CAPTURE_PASS_GAP_SECONDS)
  {
    buffer.actions.clear();
    buffer.pass_id++;
  }
  buffer.last_capture_time = now;
}

std::string catalog_id_for_action(const RlActionSnapshot &action)
{
  if (!action.operator_id.empty()) {
    return "blender.operator." + sanitize_id_part(action.operator_id);
  }
  if (!action.rna_struct.empty() && !action.rna_property.empty()) {
    return "blender.rna." + sanitize_id_part(action.rna_struct) + "." +
           sanitize_id_part(action.rna_property);
  }
  return "blender.ui." + sanitize_id_part(action.button_type) + "." + sanitize_id_part(action.label);
}

std::string verifier_contract_kind_for_action(const RlActionSnapshot &action)
{
  if (action.button_type == "tab") {
    return "workspace_tab";
  }
  if (!action.operator_id.empty()) {
    return "operator_button";
  }
  if (!action.rna_property.empty()) {
    if (ELEM(action.button_type,
             "toggle",
             "toggle_n",
             "icon_toggle",
             "icon_toggle_n",
             "button_toggle",
             "checkbox",
             "checkbox_n",
             "row"))
    {
      return "rna_toggle";
    }
    if (ELEM(action.button_type, "text", "textbox")) {
      return "text_input";
    }
    return "rna_property";
  }
  if (ELEM(action.button_type, "menu", "button_menu", "pulldown", "popover", "search_menu")) {
    return "menu_button";
  }
  if (ELEM(action.button_type, "text", "textbox")) {
    return "text_input";
  }
  return "ui_button";
}

void append_verifier_primitives(std::string &out, const std::string &contract_kind)
{
  if (contract_kind == "workspace_tab") {
    append_json_string_array(
        out, {"catalog_known", "visible", "enabled", "hit_bbox", "dispatch_ui", "workspace_active"});
    return;
  }
  if (contract_kind == "menu_button") {
    append_json_string_array(
        out, {"catalog_known", "visible", "enabled", "hit_bbox", "dispatch_ui", "popup_opened"});
    return;
  }
  if (contract_kind == "operator_button") {
    append_json_string_array(out,
                             {"catalog_known",
                              "visible",
                              "enabled",
                              "hit_bbox",
                              "dispatch_operator",
                              "operator_called"});
    return;
  }
  if (contract_kind == "rna_toggle") {
    append_json_string_array(
        out, {"catalog_known", "visible", "enabled", "hit_bbox", "dispatch_rna", "rna_value_changed"});
    return;
  }
  if (contract_kind == "rna_property") {
    append_json_string_array(
        out, {"catalog_known", "visible", "enabled", "hit_bbox", "dispatch_rna", "rna_value_changed"});
    return;
  }
  if (contract_kind == "text_input") {
    append_json_string_array(
        out, {"catalog_known", "visible", "enabled", "focus_changed", "text_value_changed"});
    return;
  }
  append_json_string_array(
      out, {"catalog_known", "visible", "enabled", "hit_bbox", "dispatch_ui", "state_changed"});
}

void append_verifier_contract_json(std::string &out, const RlActionSnapshot &action)
{
  const std::string contract_kind = verifier_contract_kind_for_action(action);
  out += "{\n        ";
  append_json_key_string(out, "kind", contract_kind);
  out += ",\n        \"required_primitives\": ";
  append_verifier_primitives(out, contract_kind);
  out += "\n      }";
}

std::string instance_id_for_action(const bContext *C,
                                   const ARegion *region,
                                   const ui::Block &block,
                                   const ui::Button &but,
                                   const RlActionSnapshot &action)
{
  const WorkSpace *workspace = CTX_wm_workspace(C);
  const bScreen *screen = CTX_wm_screen(C);
  const ScrArea *area = CTX_wm_area(C);
  const int button_index = block.but_index(&but);

  std::string id = "blender.ui_instance.";
  id += sanitize_id_part(id_name_or_empty(workspace ? &workspace->id : nullptr));
  id += ".";
  id += sanitize_id_part(id_name_or_empty(screen ? &screen->id : nullptr));
  id += ".area_";
  id += std::to_string(area ? area->spacetype : -1);
  id += ".region_";
  id += std::to_string(region ? region->regiontype : -1);
  id += ".block_";
  id += sanitize_id_part(block.name);
  id += ".button_";
  id += std::to_string(button_index);
  id += ".";
  id += sanitize_id_part(action.catalog_id);
  return id;
}

RlActionSnapshot action_from_button(const bContext *C,
                                    const ARegion *region,
                                    const ui::Block *block,
                                    const ui::Button *but,
                                    const rcti &rect)
{
  RlActionSnapshot action;
  action.label = string_ref_to_std(ui::button_drawstr_without_sep_char(but));
  action.block_name = block->name;
  action.button_type = button_type_name(but->type);
  action.kind = action_kind_for_button(*but);
  action.block_flags = block->flag;
  action.button_flags = int(but->flag);
  action.enabled = ui::button_is_interactive(but, false) &&
                   !(but->flag & (BUT_DISABLED | BUT_INACTIVE | UI_HIDDEN | UI_SCROLLED));
  action.disabled_info = but->disabled_info ? but->disabled_info : "";
  action.bbox_px = rect;
  action.bbox_win_px = rect;
  if (region != nullptr) {
    BLI_rcti_translate(&action.bbox_win_px, region->winrct.xmin, region->winrct.ymin);
  }

  if (but->optype != nullptr) {
    action.operator_id = but->optype->idname ? but->optype->idname : "";
    action.op_context = int(but->opcontext);
  }
  if (but->rnapoin.type != nullptr) {
    action.rna_struct = RNA_struct_identifier(but->rnapoin.type);
  }
  if (but->rnaprop != nullptr) {
    action.rna_property = RNA_property_identifier(but->rnaprop);
    action.rna_index = but->rnaindex;
  }

  action.catalog_id = catalog_id_for_action(action);
  action.instance_id = instance_id_for_action(C, region, *block, *but, action);
  return action;
}

RlActionSnapshot action_from_afterfunc(const char *label,
                                       const char *operator_id,
                                       const int op_context,
                                       const char *rna_struct,
                                       const char *rna_property)
{
  RlActionSnapshot action;
  action.label = label ? label : "";
  action.operator_id = operator_id ? operator_id : "";
  action.op_context = op_context;
  action.rna_struct = rna_struct ? rna_struct : "";
  action.rna_property = rna_property ? rna_property : "";
  action.rna_index = -1;
  action.kind = !action.operator_id.empty() ? "operator" :
                                            (!action.rna_property.empty() ? "rna" : "ui");
  action.button_type = "afterfunc";
  action.visible = false;
  action.enabled = true;
  action.catalog_id = catalog_id_for_action(action);
  action.instance_id = "";
  return action;
}

std::string build_state_json(const bContext *C, const RlStateBuffer &buffer)
{
  const WorkSpace *workspace = CTX_wm_workspace(C);
  const bScreen *screen = CTX_wm_screen(C);
  const ScrArea *area = CTX_wm_area(C);
  const ARegion *region = CTX_wm_region_popup(C) ? CTX_wm_region_popup(C) : CTX_wm_region(C);

  std::string out;
  out.reserve(4096 + buffer.actions.size() * 768);
  out += "{\n";
  out += "  ";
  append_json_key_string(out, "schema", "blendgym.ui_state.v0");
  out += ",\n  ";
  append_json_key_int(out, "capture_pass_id", int(buffer.pass_id));
  out += ",\n  ";
  append_json_key_string(out,
                         "workspace",
                         id_name_or_empty(workspace ? &workspace->id : nullptr));
  out += ",\n  ";
  append_json_key_string(out, "screen", id_name_or_empty(screen ? &screen->id : nullptr));
  out += ",\n  ";
  append_json_key_int(out, "area_space_type", area ? area->spacetype : -1);
  out += ",\n  ";
  append_json_key_int(out, "region_type", region ? region->regiontype : -1);
  out += ",\n  ";
  append_json_key_int(out, "region_alignment", region ? region->alignment : -1);
  out += ",\n  ";
  append_json_key_int(out, "visible_action_count", int(buffer.actions.size()));
  out += ",\n  \"visible_actions\": [\n";

  for (const int64_t i : buffer.actions.index_range()) {
    const RlActionSnapshot &action = buffer.actions[i];
    out += "    {\n      ";
    append_json_key_string(out, "catalog_id", action.catalog_id);
    out += ",\n      ";
    append_json_key_string(out, "instance_id", action.instance_id);
    out += ",\n      ";
    append_json_key_string(out, "kind", action.kind);
    out += ",\n      ";
    append_json_key_string(out, "label", action.label);
    out += ",\n      ";
    append_json_key_string(out, "button_type", action.button_type);
    out += ",\n      ";
    append_json_key_bool(out, "visible", action.visible);
    out += ",\n      ";
    append_json_key_bool(out, "enabled", action.enabled);
    out += ",\n      ";
    append_json_key_string(out, "disabled_info", action.disabled_info);
    out += ",\n      ";
    append_json_key_string(out, "block_name", action.block_name);
    out += ",\n      ";
    append_json_key_int(out, "block_flags", action.block_flags);
    out += ",\n      ";
    append_json_key_int(out, "button_flags", action.button_flags);
    out += ",\n      ";
    append_json_key_string(out, "operator_id", action.operator_id);
    out += ",\n      ";
    append_json_key_int(out, "op_context", action.op_context);
    out += ",\n      ";
    append_json_key_string(out, "rna_struct", action.rna_struct);
    out += ",\n      ";
    append_json_key_string(out, "rna_property", action.rna_property);
    out += ",\n      ";
    append_json_key_int(out, "rna_index", action.rna_index);
    out += ",\n      \"verifier_contract\": ";
    append_verifier_contract_json(out, action);
    out += ",\n      \"bbox_px\": {";
    append_json_key_int(out, "xmin", action.bbox_px.xmin);
    out += ", ";
    append_json_key_int(out, "ymin", action.bbox_px.ymin);
    out += ", ";
    append_json_key_int(out, "xmax", action.bbox_px.xmax);
    out += ", ";
    append_json_key_int(out, "ymax", action.bbox_px.ymax);
    out += "},\n      \"bbox_win_px\": {";
    append_json_key_int(out, "xmin", action.bbox_win_px.xmin);
    out += ", ";
    append_json_key_int(out, "ymin", action.bbox_win_px.ymin);
    out += ", ";
    append_json_key_int(out, "xmax", action.bbox_win_px.xmax);
    out += ", ";
    append_json_key_int(out, "ymax", action.bbox_win_px.ymax);
    out += "}\n    }";
    out += (i + 1 == buffer.actions.size()) ? "\n" : ",\n";
  }

  out += "  ]\n";
  out += "}\n";
  return out;
}

std::string build_dispatch_event_json(const bContext *C,
                                      const char *phase,
                                      const RlActionSnapshot &action,
                                      const bool operator_called,
                                      const bool rna_updated)
{
  const WorkSpace *workspace = CTX_wm_workspace(C);
  const bScreen *screen = CTX_wm_screen(C);
  const ScrArea *area = CTX_wm_area(C);
  const ARegion *region = CTX_wm_region_popup(C) ? CTX_wm_region_popup(C) : CTX_wm_region(C);
  const std::string contract_kind = verifier_contract_kind_for_action(action);

  std::string out;
  out.reserve(2048);
  out += "{\n  ";
  append_json_key_string(out, "schema", "blendgym.dispatch_event.v0");
  out += ",\n  ";
  append_json_key_int(out, "event_id", int(next_dispatch_event_id()));
  out += ",\n  ";
  append_json_key_double(out, "timestamp", BLI_time_now_seconds());
  out += ",\n  ";
  append_json_key_string(out, "phase", phase ? phase : "");
  out += ",\n  ";
  append_json_key_string(out,
                         "workspace",
                         id_name_or_empty(workspace ? &workspace->id : nullptr));
  out += ",\n  ";
  append_json_key_string(out, "screen", id_name_or_empty(screen ? &screen->id : nullptr));
  out += ",\n  ";
  append_json_key_int(out, "area_space_type", area ? area->spacetype : -1);
  out += ",\n  ";
  append_json_key_int(out, "region_type", region ? region->regiontype : -1);
  out += ",\n  ";
  append_json_key_string(out, "catalog_id", action.catalog_id);
  out += ",\n  ";
  append_json_key_string(out, "instance_id", action.instance_id);
  out += ",\n  ";
  append_json_key_string(out, "kind", action.kind);
  out += ",\n  ";
  append_json_key_string(out, "label", action.label);
  out += ",\n  ";
  append_json_key_string(out, "button_type", action.button_type);
  out += ",\n  ";
  append_json_key_bool(out, "enabled", action.enabled);
  out += ",\n  ";
  append_json_key_string(out, "operator_id", action.operator_id);
  out += ",\n  ";
  append_json_key_int(out, "op_context", action.op_context);
  out += ",\n  ";
  append_json_key_string(out, "rna_struct", action.rna_struct);
  out += ",\n  ";
  append_json_key_string(out, "rna_property", action.rna_property);
  out += ",\n  ";
  append_json_key_int(out, "rna_index", action.rna_index);
  out += ",\n  ";
  append_json_key_bool(out, "operator_called", operator_called);
  out += ",\n  ";
  append_json_key_bool(out, "rna_updated", rna_updated);
  out += ",\n  \"verifier_contract\": ";
  append_verifier_contract_json(out, action);
  out += ",\n  \"bbox_px\": {";
  append_json_key_int(out, "xmin", action.bbox_px.xmin);
  out += ", ";
  append_json_key_int(out, "ymin", action.bbox_px.ymin);
  out += ", ";
  append_json_key_int(out, "xmax", action.bbox_px.xmax);
  out += ", ";
  append_json_key_int(out, "ymax", action.bbox_px.ymax);
  out += "},\n  \"bbox_win_px\": {";
  append_json_key_int(out, "xmin", action.bbox_win_px.xmin);
  out += ", ";
  append_json_key_int(out, "ymin", action.bbox_win_px.ymin);
  out += ", ";
  append_json_key_int(out, "xmax", action.bbox_win_px.xmax);
  out += ", ";
  append_json_key_int(out, "ymax", action.bbox_win_px.ymax);
  out += "}\n}\n";
  return out;
}

void write_dispatch_event_json(const bContext *C,
                               const char *phase,
                               const RlActionSnapshot &action,
                               const bool operator_called,
                               const bool rna_updated)
{
  if (!state_dump_enabled()) {
    return;
  }

  char filepath[FILE_MAX];
  resolve_blendgym_filepath(filepath, BLENDGYM_DISPATCH_FILE_ENV, "latest_dispatch_event.json");
  BLI_file_ensure_parent_dir_exists(filepath);

  blender::fstream file(filepath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    return;
  }
  file << build_dispatch_event_json(C, phase, action, operator_called, rna_updated);
}

void write_state_json(const bContext *C, const RlStateBuffer &buffer)
{
  char filepath[FILE_MAX];
  resolve_blendgym_filepath(filepath, BLENDGYM_STATE_FILE_ENV, "latest_ui_state.json");
  BLI_file_ensure_parent_dir_exists(filepath);

  blender::fstream file(filepath, std::ios::out | std::ios::trunc);
  if (!file.is_open()) {
    return;
  }
  file << build_state_json(C, buffer);
}

}  // namespace

void ui_state_capture_visible_button(const bContext *C,
                                     const ARegion *region,
                                     const ui::Block *block,
                                     const ui::Button *but,
                                     const rcti &rect)
{
  if (!state_dump_enabled() || C == nullptr || region == nullptr || block == nullptr ||
      but == nullptr)
  {
    return;
  }

  process_pending_command_if_needed(C);

  RlStateBuffer &buffer = state_buffer();
  begin_new_capture_pass_if_needed(buffer);
  replace_or_append_action(buffer, action_from_button(C, region, block, but, rect));
  write_state_json(C, buffer);
}

void ui_state_process_pending_commands(const bContext *C)
{
  process_pending_command_if_needed(C);
}

void ui_state_record_button_dispatch_requested(const bContext *C, const ui::Button *but)
{
  if (!state_dump_enabled() || C == nullptr || but == nullptr || but->block == nullptr) {
    return;
  }

  const ARegion *region = CTX_wm_region_popup(C) ? CTX_wm_region_popup(C) : CTX_wm_region(C);
  rcti rect = {};
  if (region != nullptr) {
    button_to_pixelrect(&rect, region, but->block, but);
  }
  const RlActionSnapshot action = action_from_button(C, region, but->block, but, rect);
  write_dispatch_event_json(C, "button_dispatch_requested", action, false, false);
}

void ui_state_record_afterfunc_dispatch(const bContext *C,
                                        const char *label,
                                        const char *operator_id,
                                        const int op_context,
                                        const char *rna_struct,
                                        const char *rna_property,
                                        const bool operator_called,
                                        const bool rna_updated)
{
  if (!state_dump_enabled() || C == nullptr) {
    return;
  }
  const RlActionSnapshot action = action_from_afterfunc(
      label, operator_id, op_context, rna_struct, rna_property);
  write_dispatch_event_json(
      C, "afterfunc_dispatched", action, operator_called, rna_updated);
}

}  // namespace blender::blendgym
