#include "buttons_loader.h"
#include "json_interface.h"
#include "our_debug.h"
#include "errors.h"

#include <fstream>
#include <cstdio>
#include <cstring>

static inline uint8_t
clamp_byte (int v) { if (v < 0) return 0; if (v > 255) return 255; return (uint8_t) v; }

static bool parse_rgba_array (json_object *arr, RGBA &out)
{
  if (!arr || !json_object_is_type (arr, json_type_array)) return false;
  int n = (int) json_object_array_length (arr);
  if (n < 3) return false;
  int rr = 0, gg = 0, bb = 0, aa = 255;
  json_object *jv = nullptr;
  jv = json_object_array_get_idx (arr, 0); if (jv) rr = json_object_get_int (jv);
  jv = json_object_array_get_idx (arr, 1); if (jv) gg = json_object_get_int (jv);
  jv = json_object_array_get_idx (arr, 2); if (jv) bb = json_object_get_int (jv);
  if (n >= 4) { jv = json_object_array_get_idx (arr, 3); if (jv) aa = json_object_get_int (jv); }
  out.r = clamp_byte (rr); out.g = clamp_byte (gg); out.b = clamp_byte (bb); out.a = clamp_byte (aa);
  return true;
}

bool load_button_defs_from_ui (const char *ui_config_path, std::map<std::string, ButtonDef> &out, std::string *err)
{
  out.clear();
  JsonDoc doc = JsonDoc::from_file(ui_config_path, err);
  if (!doc.valid()) { const char* msg = err ? err->c_str() : "ui.json not found or invalid"; CRASH(ExitCode::LOADING_ERROR, "%s", msg); }
  json_object* root = doc.get();
  if (!json_object_is_type(root, json_type_object)) { if (err) *err = "ui.json must be an object"; CRASH(ExitCode::LOADING_ERROR, "%s", err?err->c_str():"ui.json must be an object"); }
  json_object* buttons = nullptr;
  if (!json_object_object_get_ex(root, "buttons", &buttons) || !json_object_is_type(buttons, json_type_object)) {
    if (err) *err = "ui.json missing 'buttons' object"; CRASH(ExitCode::LOADING_ERROR, "%s", err?err->c_str():"ui.json missing 'buttons' object");
  }
  json_object_object_foreach(buttons, k, v) {
    if (!json_object_is_type(v, json_type_object)) continue;
    ButtonDef bd;
    // Optional label text
    json_object* jt = nullptr; if (json_object_object_get_ex(v, "text", &jt) && json_object_is_type(jt, json_type_string)) bd.text = json_object_get_string(jt);
    // Styles per state, support either RGBA array or name
    json_object_object_foreach(v, state_k, state_v) {
      if (strcmp(state_k, "text") == 0) continue;
      if (!json_object_is_type(state_v, json_type_object)) continue;
      ButtonStyle st;
      json_object* sname = nullptr; if (json_object_object_get_ex(state_v, "name", &sname) && json_object_is_type(sname, json_type_string)) { st.has_color_name = true; st.color_name = json_object_get_string(sname); }
      json_object* sarr = nullptr; if (json_object_object_get_ex(state_v, "RGBA", &sarr)) { st.has_rgba = parse_rgba_array(sarr, st.rgba); }
      if (st.has_color_name || st.has_rgba) bd.by_state[state_k] = st;
    }
    out[k] = std::move(bd);
  }
  return !out.empty();
}
