#pragma once

#include <string>
#include <unordered_map>
#include <vector>

struct UIConfig {
    std::string font_path; // relative or absolute
    int font_small = 12;
    int font_medium = 18;
    int font_large = 28;
    // Window + app metadata
    std::string title = "Virtual Impulse - UI Prototype";
    int window_w = 800;
    int window_h = 600;
    bool fullscreen = false;
    int fps_cap = 60;
    // HUD
    int hud_width = 340;
    int hud_pad = 8;
    unsigned char hud_bg_r=20, hud_bg_g=24, hud_bg_b=28, hud_bg_a=220;
    unsigned char hud_border_r=80, hud_border_g=170, hud_border_b=255, hud_border_a=255;
    unsigned char hud_text_r=235, hud_text_g=235, hud_text_b=235, hud_text_a=255;
    // Atmosphere overlay color
    unsigned char atmo_r=120, atmo_g=170, atmo_b=255, atmo_a=170;

    struct ColorDef { unsigned char r=235,g=235,b=235,a=255; };
    std::unordered_map<std::string, ColorDef> named_colors; // from ui.json colors{}

    struct MenuSpec {
        int x=0,y=0,w=300,h=0; // h==0 -> full height minus margins
        std::string fill = "vertical"; // vertical|horizontal
        std::vector<std::string> buttons; // button keys order
    } menu;
};

// Loads UI config from config/ui.json. On failure, returns false and may fill err,
// but 'out' remains with sane defaults.
bool load_ui_config(const char* path, UIConfig& out, std::string* err = nullptr);
