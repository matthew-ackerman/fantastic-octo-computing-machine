#include "ui_config_loader.h"
#include "our_debug.h"
#include "json_interface.h"
#include "errors.h"

#include <fstream>
#include <cstdio>

bool load_ui_config(const char* path, UIConfig& out, std::string* err)
{
    std::ifstream f(path, std::ios::binary);
    if (!f.good()) {
        if (err) *err = "ui config file not found";
        CRASH(ExitCode::LOADING_ERROR, "ui config file not found: %s", path ? path : "<null>");
    }
    JsonDoc doc = JsonDoc::from_file(path, err);
    if (!doc.valid()) {
        const char* msg = (err && !err->empty()) ? err->c_str() : "Failed to parse ui config";
        CRASH(ExitCode::LOADING_ERROR, "%s", msg);
    }
    JsonView root(doc.get());

    // App-level fields moved from game.json
    (void)get_json_value(root, "title", &out.title);
    JsonView jwindow; if (root.get_view("window", jwindow) && jwindow.is_object()) {
        (void)get_json_value(jwindow, "w", &out.window_w);
        (void)get_json_value(jwindow, "h", &out.window_h);
        (void)get_json_value(jwindow, "fullscreen", &out.fullscreen);
    }
    (void)get_json_value(root, "fps_cap", &out.fps_cap);

    JsonView fonts; if (root.get_view("fonts", fonts) && fonts.is_object()) {
        (void)get_json_value(fonts, "path", &out.font_path);
        (void)get_json_value(fonts, "small", &out.font_small);
        (void)get_json_value(fonts, "medium", &out.font_medium);
        (void)get_json_value(fonts, "large", &out.font_large);
    }

    // HUD parsing
    auto parse_rgba = [](const JsonView& arr, unsigned char& r, unsigned char& g, unsigned char& b, unsigned char& a){
        if (!arr.is_array() || arr.length() < 3) return;
        JsonView v0 = arr.index(0), v1 = arr.index(1), v2 = arr.index(2);
        r = (unsigned char)json_object_get_int(v0.p);
        g = (unsigned char)json_object_get_int(v1.p);
        b = (unsigned char)json_object_get_int(v2.p);
        if (arr.length() >= 4) { JsonView v3 = arr.index(3); a = (unsigned char)json_object_get_int(v3.p); }
    };
    JsonView hud; if (root.get_view("hud", hud) && hud.is_object()) {
        JsonView v;
        if (hud.get_view("bg", v)) parse_rgba(v, out.hud_bg_r, out.hud_bg_g, out.hud_bg_b, out.hud_bg_a);
        if (hud.get_view("border", v)) parse_rgba(v, out.hud_border_r, out.hud_border_g, out.hud_border_b, out.hud_border_a);
        if (hud.get_view("text", v)) parse_rgba(v, out.hud_text_r, out.hud_text_g, out.hud_text_b, out.hud_text_a);
        (void)get_json_value(hud, "pad", &out.hud_pad);
        (void)get_json_value(hud, "width", &out.hud_width);
    }

    // Atmosphere color (optional)
    JsonView jatm; if (root.get_view("atmosphere", jatm) && jatm.is_object()) {
        JsonView jc; if (jatm.get_view("color", jc)) parse_rgba(jc, out.atmo_r, out.atmo_g, out.atmo_b, out.atmo_a);
        else if (jatm.get_view("RGBA", jc)) parse_rgba(jc, out.atmo_r, out.atmo_g, out.atmo_b, out.atmo_a);
    }

    // Named colors map: { "colors": { "white": [r,g,b,a], ... } }
    JsonView jcolors; if (root.get_view("colors", jcolors) && jcolors.is_object()) {
        json_object_object_foreach(jcolors.p, k, v) {
            UIConfig::ColorDef cd{}; JsonView arr(v);
            if (!arr.is_array()) continue; unsigned char r=235,g=235,b=235,a=255; parse_rgba(arr, r,g,b,a); cd.r=r; cd.g=g; cd.b=b; cd.a=a; out.named_colors[k] = cd;
        }
    }

    // Menu spec: { "menu": { "area":[x,y,w,h], "fill":"vertical", "buttons":["end_turn","quit"] } }
    JsonView jmenu; if (root.get_view("menu", jmenu) && jmenu.is_object()) {
        JsonView jarea; if (jmenu.get_view("area", jarea) && jarea.is_array() && jarea.length() >= 4) {
            out.menu.x = json_object_get_int(json_object_array_get_idx(jarea.p, 0));
            out.menu.y = json_object_get_int(json_object_array_get_idx(jarea.p, 1));
            out.menu.w = json_object_get_int(json_object_array_get_idx(jarea.p, 2));
            out.menu.h = json_object_get_int(json_object_array_get_idx(jarea.p, 3));
        }
        (void)get_json_value(jmenu, "fill", &out.menu.fill);
        JsonView jbtns; if (jmenu.get_view("buttons", jbtns) && jbtns.is_array()) {
            size_t n = jbtns.length(); out.menu.buttons.clear(); out.menu.buttons.reserve(n);
            for (size_t i = 0; i < n; ++i) {
                JsonView it = jbtns.index(i); std::string s; if (it.is_object()) { (void)get_json_value(it, "key", &s); } else if (json_object_is_type(it.p, json_type_string)) { s = json_object_get_string(it.p); }
                if (!s.empty()) out.menu.buttons.push_back(std::move(s));
            }
        }
    }

    DBG("ui config: title=%s w=%d h=%d fullscreen=%d fps=%d font_path=%s sizes=%d/%d/%d hud.w=%d hud.pad=%d",
        out.title.c_str(), out.window_w, out.window_h, (int)out.fullscreen, out.fps_cap,
        out.font_path.c_str(), out.font_small, out.font_medium, out.font_large,
        out.hud_width, out.hud_pad);
    return true;
}
