// ui_main: networked UI client that connects to main_engine over TCP,
// sends commands, and renders state.

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <fstream>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <deque>

#include "file_io/json_interface.h"
#include "file_io/hash_utils.h"
#include "stream_io/tcp_protocol.h"

#include "file_io/config_loader.h"
#include "file_io/object_loader.h"
#include "file_io/ui_config_loader.h"
#include "file_io/buttons_loader.h"
#include "ui/menu.h"

#include "object_def.h"
#include "config.h"
#include "ui/camera.h"
#include "draw_utils.h"

#include <unordered_map>
static std::unordered_map<std::string, SDL_Color> g_named_colors;
static SDL_Color color_of(const std::string& name) {
    auto it = g_named_colors.find(name);
    if (it != g_named_colors.end()) return it->second;
    return SDL_Color{235,235,235,255};
}

struct ObjectView {
    std::string type;       // "ship", "planet", "projectile", "body"
    std::string object_key; // object definition key
    uint64_t uid = 0;       // ships may have a uid; others 0
    int team = 0;
    int throttle = 0;       // ships only
    double x = 0, y = 0;    // world px
    double vx = 0, vy = 0;  // px/s
    double theta = 0;       // radians
    double delta_v = 0;     // px/s (ships)
    double acc = 0;         // px/s^2 (ships)
};

struct WorldView {
    std::vector<ObjectView> objects;
};

// hash_file_fnv1a64 provided by file_io/hash_utils.h

static int connect_loopback(int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { std::perror("socket"); return -1; }
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); addr.sin_port = htons((uint16_t)port);
    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) < 0) { std::perror("connect"); ::close(fd); return -1; }
    int flags = fcntl(fd, F_GETFL, 0); if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return fd;
}

static inline void send_line(int fd, const std::string& s) { (void)::send(fd, s.c_str(), s.size(), 0); }

static void send_join(int fd, const char* defs_hash) { send_line(fd, tcp_protocol::build_join("ui", defs_hash, 0)); }
static void request_state(int fd, const char* scope) { send_line(fd, tcp_protocol::build_state_req(scope)); }
static void send_cmd(int fd, const char* cmd, uint64_t uid, double value_or_theta, bool is_theta) { send_line(fd, tcp_protocol::build_cmd(cmd, uid, value_or_theta, is_theta)); }
static void send_end_turn(int fd) { send_line(fd, tcp_protocol::build_end_turn(1.0)); }

// helper removed; we parse directly where needed

static void net_poll_and_enqueue(int fd, std::string& buf, std::deque<std::vector<ObjectView>>& queue) {
    char tmp[4096];
    while (true) {
        ssize_t n = ::recv(fd, tmp, sizeof(tmp), 0);
        if (n <= 0) break;
        buf.append(tmp, tmp + n);
    }
    size_t pos;
    while ((pos = buf.find('\n')) != std::string::npos) {
        std::string line = buf.substr(0, pos);
        buf.erase(0, pos + 1);
        if (line.empty()) continue;
        std::string defs_hash; bool has_match=false; bool match=false; bool handled=false;
        std::vector<tcp_protocol::NetObjectView> tmp;
        if (tcp_protocol::parse_state_objects(line, tmp, &defs_hash)) {
            WorldView vtmp; vtmp.objects.reserve(tmp.size());
            for (const auto& it : tmp) {
                ObjectView o{}; o.type = it.type; o.object_key = it.object_key; o.uid = it.uid; o.team = it.team; o.throttle = it.throttle; o.x = it.x; o.y = it.y; o.vx = it.vx; o.vy = it.vy; o.theta = it.theta; o.delta_v = it.delta_v; o.acc = it.acc; vtmp.objects.push_back(std::move(o));
            }
            queue.push_back(std::move(vtmp.objects));
            handled = true;
        } else if (tcp_protocol::parse_joined(line, &defs_hash, &has_match, &match)) {
            std::fprintf(stderr, "[ui] joined engine; defs_hash=%s%s%s\n",
                         !defs_hash.empty()?defs_hash.c_str():"<none>",
                         has_match?" match=":"",
                         has_match?(match?"true":"false"):"");
            handled = true;
        }
        (void)handled;
    }
}

static uint64_t pick_initial_selected(const WorldView& view) {
    if (view.objects.empty()) return 0;
    for (const auto& s : view.objects) if (s.type == "ship" && s.team == 0) return s.uid;
    for (const auto& s : view.objects) if (s.type == "ship") return s.uid;
    return 0;
}

static ObjectView* find_ship(WorldView& v, uint64_t uid) {
    for (auto& s : v.objects) {
        if (s.uid == uid && s.type == "ship") return &s;
    }
    return nullptr;
}

static bool display_boot(SDL_Renderer* ren, TTF_Font* hud_font, SDL_Color hud_text, int window_w, int window_h) {
    (void)hud_text; (void)window_w; (void)window_h;
    std::string jerr;
    JsonDoc doc = JsonDoc::from_file(UI_BOOT_SEQUENCE_PATH, &jerr);
    if (!doc.valid()) { return true; }
    JsonView root(doc.get());
    if (!root.is_object()) { return true; }
    JsonView arr; if (!root.get_view("boot_sequence", arr) || !arr.is_array()) { return true; }
    struct Seg {
        std::string text;
        SDL_Color color;
        bool bold=false;
        bool elapsed=false;
        std::string fmt;
    };
    struct Entry { double delay=0.0; std::vector<Seg> segs; };
    std::vector<Entry> entries;
    size_t n = arr.length();
    entries.reserve(n);
    for (size_t i=0;i<n;++i){
        JsonView it = arr.index(i);
        if (!it.is_object()) continue;
        Entry e;
        (void)get_json_value(it, "delay", &e.delay);
        JsonView jsegs; if (it.get_view("segments", jsegs) && jsegs.is_array()){
            size_t m=jsegs.length();
            for (size_t k=0;k<m;++k){
                JsonView si=jsegs.index(k);
                if (!si.is_object()) continue;
                Seg sg; sg.color = color_of("white");
                std::string sval;
                if (get_json_value(si, "text", &sval)) sg.text = std::move(sval);
                if (get_json_value(si, "color", &sval)) sg.color = color_of(sval);
                if (get_json_value(si, "modifiers", &sval)) { if (sval.find("bold")!=std::string::npos) sg.bold=true; }
                if (get_json_value(si, "cmd", &sval)) {
                    std::string cmd = std::move(sval);
                    if (cmd == "elapsed") {
                        sg.elapsed = true;
                        std::string fmt;
                        if (get_json_value(si, "format", &fmt)) {
                            sg.fmt = std::move(fmt);
                        } else if (!sg.text.empty()) {
                            sg.fmt = sg.text; sg.text.clear();
                        }
                    }
                }
                e.segs.push_back(std::move(sg));
            }
        }
        entries.push_back(std::move(e));
    }
    // doc RAII handles put

    // Draw entries at cumulative delays; skip on any key
    Uint32 start = SDL_GetTicks(); bool skip=false; int lh = 18; if (hud_font) lh = TTF_FontHeight(hud_font);
    std::vector<Entry> shown;
    SDL_StartTextInput();
    for (const auto& e : entries) {
        if (skip) break;
        double target = e.delay * 1000.0; // milliseconds
        while (!skip) { Uint32 now=SDL_GetTicks(); if ((double)(now - start) >= target) break; SDL_Event ev; while (SDL_PollEvent(&ev)) { if (ev.type==SDL_QUIT) { skip=true; break; } if (ev.type==SDL_KEYDOWN || ev.type==SDL_MOUSEBUTTONDOWN || ev.type==SDL_TEXTINPUT) { skip=true; break; } } SDL_Delay(5); }
        if (skip) break;
        // Add this line to the set of shown lines
        shown.push_back(e);
        // Draw background and all accumulated lines stacked vertically
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
        int y = 50;
        for (const auto& line : shown) {
            int x = 40;
            for (const auto& sg : line.segs) {
                if (!hud_font) break;
                if (sg.bold) TTF_SetFontStyle(hud_font, TTF_STYLE_BOLD);
                else TTF_SetFontStyle(hud_font, TTF_STYLE_NORMAL);
                std::string to_render = sg.text;
                if (sg.elapsed) { char buf[128]; const char* fmt = nullptr; if (!sg.fmt.empty()) fmt = sg.fmt.c_str(); else if (!sg.text.empty()) fmt = sg.text.c_str(); if (!fmt) fmt = "%0.3f"; std::snprintf(buf, sizeof(buf), fmt, line.delay); to_render = buf; }
                SDL_Surface* s = TTF_RenderUTF8_Blended(hud_font, to_render.c_str(), sg.color);
                if (!s) continue;
                SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
                SDL_Rect dst{x,y,s->w,s->h};
                SDL_RenderCopy(ren, t, nullptr, &dst);
                x += s->w;
                SDL_DestroyTexture(t);
                SDL_FreeSurface(s);
            }
            y += lh;
        }
        SDL_RenderPresent(ren);
    }
    // If animation was skipped, render remaining lines so final prompt is visible
    if (shown.size() < entries.size()) {
        for (size_t i = shown.size(); i < entries.size(); ++i) shown.push_back(entries[i]);
        SDL_SetRenderDrawColor(ren, 0, 0, 0, 255); SDL_RenderClear(ren);
        int y = 50;
        for (const auto& line : shown) {
            int x = 40;
            for (const auto& sg : line.segs) {
                if (!hud_font) break;
                if (sg.bold) TTF_SetFontStyle(hud_font, TTF_STYLE_BOLD);
                else TTF_SetFontStyle(hud_font, TTF_STYLE_NORMAL);
                std::string to_render = sg.text;
                if (sg.elapsed) { char buf[128]; const char* fmt = nullptr; if (!sg.fmt.empty()) fmt = sg.fmt.c_str(); else if (!sg.text.empty()) fmt = sg.text.c_str(); if (!fmt) fmt = "%0.3f"; std::snprintf(buf, sizeof(buf), fmt, line.delay); to_render = buf; }
                SDL_Surface* s = TTF_RenderUTF8_Blended(hud_font, to_render.c_str(), sg.color);
                if (!s) continue;
                SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
                SDL_Rect dst{x,y,s->w,s->h};
                SDL_RenderCopy(ren, t, nullptr, &dst);
                x += s->w;
                SDL_DestroyTexture(t);
                SDL_FreeSurface(s);
            }
            y += lh;
        }
        SDL_RenderPresent(ren);
    }
    TTF_SetFontStyle(hud_font, TTF_STYLE_NORMAL);
    SDL_StopTextInput();

    // Wait for a definitive key choice matching on-screen prompt
    SDL_Event ev; bool decided=false; bool proceed=false;
    while (!decided) {
        if (SDL_WaitEvent(&ev) == 0) continue;
        if (ev.type == SDL_QUIT) { decided=true; proceed=false; break; }
        if (ev.type == SDL_KEYDOWN) {
            SDL_Keycode k = ev.key.keysym.sym;
            if (k == SDLK_y) { proceed = true; decided = true; }
            else { proceed = false; decided = true; }
        }
    }
    return proceed;
}

int main(int argc, char** argv) {
    GameConfig cfg; std::string err; (void)load_game_config("config/game.json", cfg, &err);
    int port = cfg.net_port;
    // Load UI settings (title, window size, fps)
    UIConfig uicfg; std::string uerr; (void)load_ui_config("config/ui.json", uicfg, &uerr);

    const char* objects_path = nullptr;
    if (argc >= 2) objects_path = argv[1];
    if (!objects_path) {
        std::fprintf(stderr, "Usage: %s <objects.json>\n", argv[0]);
        return 1;
    }

    // Load object definitions for rendering and compute checksum for handshake
    std::map<std::string, ObjectDefinition> object_defs;
    std::string oerr;
    if (!load_object_defs(objects_path, object_defs, &oerr)) {
        std::fprintf(stderr, "FATAL: load_object_defs failed: %s\n", oerr.c_str());
        return 1;
    }
    std::string defs_hash = hash_file_fnv1a64(objects_path);

    int fd = connect_loopback(port);
    if (fd < 0) return 1;
    send_join(fd, defs_hash.c_str());
    request_state(fd, "all");

    if (SDL_Init(SDL_INIT_VIDEO) != 0) { std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError()); return 1; }
    SDL_Window* win = SDL_CreateWindow(uicfg.title.c_str(), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, uicfg.window_w, uicfg.window_h, SDL_WINDOW_SHOWN);
    if (!win) { std::fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError()); return 1; }
    SDL_Renderer* ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!ren) { std::fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError()); return 1; }

    Camera cam; cam.screen_w = uicfg.window_w; cam.screen_h = uicfg.window_h; cam.zoom = 1.0f; cam.cx = 0.0f; cam.cy = 0.0f;
    WorldView view; std::string nb; uint64_t selected = 0;
    std::deque<std::vector<ObjectView>> frame_queue;
    // Texture cache by object key
    std::map<std::string, SDL_Texture*> tex_cache;
    // HUD panel setup (font + colors from config/ui.json if present)
    TTF_Init();
    TTF_Font* hud_font = nullptr;
    SDL_Color hud_bg={UI_HUD_BG_R,UI_HUD_BG_G,UI_HUD_BG_B,UI_HUD_BG_A},
              hud_border{UI_HUD_BORDER_R,UI_HUD_BORDER_G,UI_HUD_BORDER_B,UI_HUD_BORDER_A},
              hud_text{UI_HUD_TEXT_R,UI_HUD_TEXT_G,UI_HUD_TEXT_B,UI_HUD_TEXT_A};
    int hud_width = UI_HUD_WIDTH_DEFAULT;
    int hud_pad = UI_HUD_PAD_DEFAULT;
    auto parse_rgba_array = [](json_object* arr, SDL_Color& out){ if (arr && json_object_is_type(arr, json_type_array) && json_object_array_length(arr)>=3) { out.r=(uint8_t)json_object_get_int(json_object_array_get_idx(arr,0)); out.g=(uint8_t)json_object_get_int(json_object_array_get_idx(arr,1)); out.b=(uint8_t)json_object_get_int(json_object_array_get_idx(arr,2)); out.a=(json_object_array_length(arr)>=4)?(uint8_t)json_object_get_int(json_object_array_get_idx(arr,3)):255; }};
    SDL_Color atmosphere_color{120,170,255,170};
    {
        // named colors map from ui.json
        g_named_colors.clear();
        for (const auto& kv : uicfg.named_colors) {
            g_named_colors[kv.first] = SDL_Color{ kv.second.r, kv.second.g, kv.second.b, kv.second.a };
        }
        if (!uerr.empty()) std::fprintf(stderr, "[ui] %s\n", uerr.c_str());
        if (!uicfg.font_path.empty()) hud_font = TTF_OpenFont(uicfg.font_path.c_str(), uicfg.font_small > 0 ? uicfg.font_small : 14);
        if (!hud_font) std::fprintf(stderr, "[ui] no TTF font opened; set fonts.path in config/ui.json\n");
        // Parse optional HUD colors from config/ui.json directly
        json_object* root = json_object_from_file("config/ui.json");
        if (root && json_object_is_type(root, json_type_object)) {
            json_object* jhud=nullptr;
            if (json_object_object_get_ex(root, "hud", &jhud) && json_object_is_type(jhud, json_type_object)) {
                auto get_rgba = [&](const char* key, SDL_Color& out){ json_object* c=nullptr; if (json_object_object_get_ex(jhud, key, &c)) parse_rgba_array(c,out); };
                get_rgba("bg", hud_bg); get_rgba("border", hud_border); get_rgba("text", hud_text);
                json_object* jp=nullptr; if (json_object_object_get_ex(jhud, "pad", &jp)) hud_pad = json_object_get_int(jp);
                json_object* jw=nullptr; if (json_object_object_get_ex(jhud, "width", &jw)) hud_width = json_object_get_int(jw);
            }
            // Optional atmosphere color at top-level: { "atmosphere": { "color": [r,g,b,a] } }
            json_object* jatm=nullptr;
            if (json_object_object_get_ex(root, "atmosphere", &jatm) && json_object_is_type(jatm, json_type_object)) {
                json_object* jc=nullptr; if (json_object_object_get_ex(jatm, "color", &jc)) parse_rgba_array(jc, atmosphere_color);
                if (!jc && json_object_object_get_ex(jatm, "RGBA", &jc)) parse_rgba_array(jc, atmosphere_color);
            }
        }
        if (root) json_object_put(root);
    }

    struct HUDPanel {
        struct Style { SDL_Color bg, border, text; int pad = 8; int width = 340; } style;
        SDL_Renderer* ren = nullptr;
        TTF_Font* font = nullptr; // not owned

        void draw_text(int x, int y, const char* txt) const {
            if (!font) return;
            SDL_Surface* s = TTF_RenderUTF8_Blended(font, txt, style.text);
            if (!s) return;
            SDL_Texture* t = SDL_CreateTextureFromSurface(ren, s);
            SDL_Rect dst{x,y,s->w,s->h};
            SDL_RenderCopy(ren, t, nullptr, &dst);
            SDL_DestroyTexture(t);
            SDL_FreeSurface(s);
        }

        void draw(const ObjectView& ov, int x, int y) const {
            // Compute strings with unit conversions
            char line[160]; int ty = y + style.pad;
            int panel_h = style.pad + 4*18 + style.pad; // add acc line
            if (ov.type == std::string("ship")) panel_h += 18; // extra line for throttle/dv/theta
            SDL_Rect r{ x, y, style.width, panel_h };
            SDL_SetRenderDrawColor(ren, style.bg.r, style.bg.g, style.bg.b, style.bg.a); SDL_RenderFillRect(ren, &r);
            SDL_SetRenderDrawColor(ren, style.border.r, style.border.g, style.border.b, style.border.a); SDL_RenderDrawRect(ren, &r);

            std::snprintf(line, sizeof(line), "type=%s key=%s team=%d uid=%llu",
                          ov.type.c_str(), ov.object_key.c_str(), ov.team, (unsigned long long)ov.uid);
            draw_text(x + style.pad, ty, line); ty += 18;
            double x_km = ov.x / 1000.0, y_km = ov.y / 1000.0;
            std::snprintf(line, sizeof(line), "x=%.2f km   y=%.2f km", x_km, y_km);
            draw_text(x + style.pad, ty, line); ty += 18;
            double vx_kms = ov.vx / 1000.0, vy_kms = ov.vy / 1000.0;
            std::snprintf(line, sizeof(line), "vx=%.3f km/s   vy=%.3f km/s", vx_kms, vy_kms);
            draw_text(x + style.pad, ty, line); ty += 18;
            std::snprintf(line, sizeof(line), "acc=%.3f px/s^2", ov.acc);
            draw_text(x + style.pad, ty, line); ty += 18;
            if (ov.type == std::string("ship")) {
                double dv_kms = ov.delta_v / 1000.0;
                std::snprintf(line, sizeof(line), "\xCE\x94v=%.3f km/s   \xCE\xB8=%.3f rad   thr=%d", dv_kms, ov.theta, ov.throttle);
                draw_text(x + style.pad, ty, line); ty += 18;
            }
        }
    } hud;
    hud.ren = ren; hud.font = hud_font; hud.style = { hud_bg, hud_border, hud_text, hud_pad, hud_width };

    // Optional boot sequence before connecting (skippable by any key press)
    if (!display_boot(ren, hud_font, hud_text, uicfg.window_w, uicfg.window_h)) {
        // User chose to exit
        SDL_DestroyRenderer(ren);
        SDL_DestroyWindow(win);
        TTF_Quit();
        SDL_Quit();
        return 0;
    }

    // Object radius lookup (world px) with fallback
    auto radius_for = [&](const std::string& key) -> double {
        auto it = object_defs.find(key);
        if (it != object_defs.end() && it->second.radius > 0.0) return it->second.radius;
        return 16.0; // fallback radius in world pixels
    };

    // Build config-driven menu on the right side
    std::map<std::string, ButtonDef> button_defs; std::string bErr;
    (void)load_button_defs_from_ui("config/ui.json", button_defs, &bErr);
    std::unordered_map<std::string, SDL_Color> btn_bg;
    auto resolve_style = [&](const ButtonStyle& st){ if (st.has_color_name) return color_of(st.color_name); if (st.has_rgba) return SDL_Color{ st.rgba.r, st.rgba.g, st.rgba.b, st.rgba.a }; return SDL_Color{80,120,160,255}; };
    for (const auto& kv : button_defs) {
        auto it = kv.second.by_state.find("active");
        if (it != kv.second.by_state.end()) btn_bg[kv.first] = resolve_style(it->second);
    }
    Menu menu;
    // Use ui.json menu spec (defaults: anchor top-right)
    int mx = uicfg.menu.x, my = uicfg.menu.y, mw = (uicfg.menu.w>0?uicfg.menu.w:180), mh = (uicfg.menu.h>0?uicfg.menu.h:(uicfg.window_h - 20));
    if (mx == 0 && mw > 0) mx = uicfg.window_w - mw - 10; if (my == 0) my = 10;
    menu.set_area(mx, my, mw, mh);
    Menu::FillOrder fo = (uicfg.menu.fill == "horizontal" ? Menu::FillOrder::LeftToRight : Menu::FillOrder::TopToBottom);
    menu.set_fill(fo);
    menu.set_button_size(mw, 36);
    menu.set_gap(8);
    menu.set_colors(btn_bg, hud_text);
    auto add_button_by_key = [&](const std::string& key){
        auto it = button_defs.find(key); if (it == button_defs.end()) return;
        MenuButton b; b.key = key; b.text_tmpl = it->second.text;
        if (key == "end_turn") b.on_click = [&](){ send_end_turn(fd); };
        else if (key == "quit") b.on_click = [&](){ SDL_Event e; e.type = SDL_QUIT; SDL_PushEvent(&e); };
        else if (key == "fire") b.on_click = [&](){ if (auto* sv = find_ship(view, selected)) { int mx,my; SDL_GetMouseState(&mx,&my); float wx,wy; screen_to_world(cam,mx,my,wx,wy); double th = std::atan2((double)wy - sv->y, (double)wx - sv->x); send_cmd(fd, "FIRE", selected, th, true); } };
        else if (key == "next_ship") b.on_click = [&](){ /* TODO: next ship */ };
        else if (key == "previous_ship") b.on_click = [&](){ /* TODO: previous ship */ };
        else if (key == "save") b.on_click = [&](){ std::fprintf(stderr, "[ui] save not implemented yet\n"); };
        menu.add_button(std::move(b));
    };
    if (!uicfg.menu.buttons.empty()) { for (const auto& k : uicfg.menu.buttons) add_button_by_key(k); }
    else { add_button_by_key("end_turn"); add_button_by_key("quit"); }

    bool running = true;
    std::string buf;
    int fps = (uicfg.fps_cap > 0 ? uicfg.fps_cap : 60);
    double frame_dt = 1.0 / (double)fps;
    Uint32 last_ticks = SDL_GetTicks();
    double accum = 0.0;

    auto select_at = [&](int mx, int my){
        int best_uid = 0; double best_d2 = 0.0;
        double cx, cy; screen_to_world(cam, mx, my, (float&)cx, (float&)cy);
        for (const auto& s : view.objects) {
            if (s.type != std::string("ship")) continue;
            double R = radius_for(s.object_key);
            double d2 = (s.x - cx)*(s.x - cx) + (s.y - cy)*(s.y - cy);
            if (d2 <= R*R) {
                if (!best_uid || d2 < best_d2) { best_uid = s.uid; best_d2 = d2; }
            }
        }
        // Only select if inside some ship's radius
        if (best_uid) selected = best_uid;
    };

    auto theta_to_mouse = [&](const ObjectView& s){ int mx,my; SDL_GetMouseState(&mx,&my); float wx,wy; screen_to_world(cam,mx,my,wx,wy); return std::atan2((double)wy - s.y, (double)wx - s.x); };

    while (running) {
        net_poll_and_enqueue(fd, buf, frame_queue);
        // Update current view at most once per frame_dt
        Uint32 now = SDL_GetTicks();
        double dt = (now - last_ticks) / 1000.0;
        last_ticks = now;
        accum += dt;
        if (accum + 1e-6 >= frame_dt && !frame_queue.empty()) {
            accum -= frame_dt;
            view.objects = std::move(frame_queue.front());
            frame_queue.pop_front();
        }
        if (selected == 0) selected = pick_initial_selected(view);

        SDL_Event e; while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) running = false;
            if (e.type == SDL_MOUSEWHEEL) {
                if (e.wheel.y != 0) { float factor = std::exp(UI_ZOOM_LAMBDA_PER_STEP * (float)e.wheel.y); cam.zoom *= factor; if (cam.zoom < 1e-8f) cam.zoom = 1e-8f; if (cam.zoom > 1e8f) cam.zoom = 1e8f; }
            }
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (!menu.handle_click(e.button.x, e.button.y)) select_at(e.button.x, e.button.y);
            }
            if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_RETURN || e.key.keysym.sym == SDLK_e) { send_end_turn(fd); }
                if (selected) {
                    ObjectView* sv = find_ship(view, selected);
                    if (sv) {
                        if (e.key.keysym.sym == SDLK_t) { int new_thr = sv->throttle ? 0 : 1; send_cmd(fd, "THROTTLE", selected, (double)new_thr, false); }
                        if (e.key.keysym.sym == SDLK_h) { double th = theta_to_mouse(*sv); send_cmd(fd, "HEADING", selected, th, true); }
                        if (e.key.keysym.sym == SDLK_f) { double th = theta_to_mouse(*sv); send_cmd(fd, "FIRE", selected, th, true); }
                    }
                }
            }
        }

        // Center camera on selected ship if any
        if (auto* sv = find_ship(view, selected)) { cam.cx = (float)sv->x; cam.cy = (float)sv->y; }

        SDL_SetRenderDrawColor(ren, 10, 12, 16, 255);
        SDL_RenderClear(ren);
        for (const auto& s : view.objects) {
            int sx, sy; world_to_screen(cam, (float)s.x, (float)s.y, sx, sy);
            SDL_Texture* tex = nullptr;
            double scale = 1.0;
            if (!s.object_key.empty()) {
                auto it = tex_cache.find(s.object_key);
                if (it == tex_cache.end()) {
                    auto defit = object_defs.find(s.object_key);
                    if (defit != object_defs.end() && !defit->second.image.empty()) {
                        SDL_Texture* t = IMG_LoadTexture(ren, defit->second.image.c_str());
                        if (t) { tex_cache[s.object_key] = t; tex = t; scale = defit->second.rescale; }
                    }
                } else { tex = it->second; auto defit2 = object_defs.find(s.object_key); if (defit2 != object_defs.end()) scale = defit2->second.rescale; }
            }
            if (tex) {
                int tw=0, th=0; SDL_QueryTexture(tex, nullptr, nullptr, &tw, &th);
                // Scale by object rescale and camera zoom
                int dw = (int)std::lround((double)tw * scale * cam.zoom);
                int dh = (int)std::lround((double)th * scale * cam.zoom);
                if (dw <= 0) dw = 1; if (dh <= 0) dh = 1;
                SDL_Rect dst{ sx - dw/2, sy - dh/2, dw, dh };
                double deg = s.theta * 180.0 / M_PI;
                SDL_Point center{ dw/2, dh/2 };
                SDL_RenderCopyEx(ren, tex, nullptr, &dst, deg, &center, SDL_FLIP_NONE);
            } else {
                int R = (int)std::lround(radius_for(s.object_key) * cam.zoom);
                if (R < 2) R = 2;
                SDL_SetRenderDrawColor(ren, s.team==0?120:200, s.team==0?220:140, s.team==0?255:140, 255);
                draw_circle_filled(ren, sx, sy, R);
                // Heading line
                int hx = (int)std::lround(sx + std::cos(s.theta) * (R + 10));
                int hy = (int)std::lround(sy + std::sin(s.theta) * (R + 10));
                SDL_SetRenderDrawColor(ren, 255, 255, 255, 200);
                SDL_RenderDrawLine(ren, sx, sy, hx, hy);
            }
            // Atmosphere overlay for planets
            if (s.type == std::string("planet")) {
                auto it = object_defs.find(s.object_key);
                if (it != object_defs.end() && it->second.atmosphere_depth > 0.0) {
                    double atmo_r_world = it->second.radius + it->second.atmosphere_depth;
                    int Ra = (int)std::lround(atmo_r_world * cam.zoom);
                    SDL_SetRenderDrawColor(ren, atmosphere_color.r, atmosphere_color.g, atmosphere_color.b, atmosphere_color.a);
                    draw_circle_outline_clipped(ren, sx, sy, Ra, cam.screen_w, cam.screen_h);
                }
            }
            if (s.uid == selected) { SDL_SetRenderDrawColor(ren, 255, 220, 120, 255); int selR = (int)std::lround(radius_for(s.object_key) * cam.zoom) + 4; draw_circle_outline(ren, sx, sy, selR); }
        }
        if (auto* sv = find_ship(view, selected)) { hud.draw(*sv, 10, 10); }
        menu.draw(ren, hud_font);

        SDL_RenderPresent(ren);
    }

    // Cleanup textures
    for (auto& kv : tex_cache) if (kv.second) SDL_DestroyTexture(kv.second);
    if (hud_font) TTF_CloseFont(hud_font);
    TTF_Quit();
    ::close(fd);
    SDL_Quit();
    return 0;
}
