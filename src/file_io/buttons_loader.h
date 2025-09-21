// Parse button colors from JSON.
#pragma once

#include <cstdint>
#include <string>
#include <map>

struct RGBA { uint8_t r = 0, g = 0, b = 0, a = 255; };

struct ButtonStyle {
  bool has_color_name = false;
  bool has_rgba = false;
  std::string color_name; // if has_color_name
  RGBA rgba;              // if has_rgba
};

struct ButtonDef {
  std::string text; // label text (e.g., "End <e>")
  std::map<std::string, ButtonStyle> by_state; // e.g., "active", "disabled"
};

// buttons.json format (top-level object):
// {
//   "quit": { "RGBA": [50,35,35,255] },
//   "end_turn": { "RGBA": [35,45,60,255] }
// }
// Returns true on success; false fills err if provided.
bool load_button_defs_from_ui (const char *ui_config_path,
                               std::map<std::string, ButtonDef> &out,
                               std::string *err = nullptr);
