// Simple SDL2 UI: clickable circle and a dummy menu with tiny bitmap text.
// C-like style with light C++ organization.

#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <cmath>
#include <cstdio>
#include <vector>
#include <memory>
#include <algorithm>
#include <string>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <random>
#include <cstdlib>
#include <sstream>
#include <functional>
// Sockets (POSIX)
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <unistd.h>

#include "errors.h"
#include "our_debug.h"
#include "config.h"

#include "save_loader.h"

#include "object_def.h"
#include "object_loader.h"

#include "buttons_loader.h" //Let's rename this 'ui_loader' and move other ui cfg in there.
#include "config_loader.h"
#include "ui_config_loader.h"


#include "ship.h"
#include "planet.h"
#include "physics.h"

#include "record.h"
#include "scene_loader.h"
#include "object_selectable.h"
#include "draw_utils.h"
#include "camera.h"
#include "object_ui_factory.h"
#include "ui_scene_builder.h"
#include "engine/initial_state.h"
#include "engine/command.h"
            
// Title comes from config/game.json; keep legacy macro unused
#define GAME_TITLE "VIRTUAL IMPULSE"

// Camera helpers are provided by ui/camera.h

static inline bool file_exists(const std::string& p) {
    std::ifstream f(p, std::ios::binary);
    return f.good();
}

// Utility: clamp
static inline int iclamp(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }

// SDL_ttf-backed text
static TTF_Font* g_font_small = nullptr;
static TTF_Font* g_font_medium = nullptr;
static TTF_Font* g_font_large = nullptr;
static bool g_ttf_ready = false;

static void draw_text(SDL_Renderer* r, int x, int y, const char* text, int scale) {
    if (!g_ttf_ready || (!g_font_small && !g_font_medium && !g_font_large)) return;
    TTF_Font* font = g_font_medium ? g_font_medium : (g_font_small ? g_font_small : g_font_large);
    if (scale <= 1 && g_font_small) font = g_font_small;
    else if (scale >= 3 && g_font_large) font = g_font_large;
    SDL_Color color{240, 240, 240, 255};
    SDL_Surface* surf = TTF_RenderUTF8_Blended(font, text, color);
    if (!surf) { return; }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(r, surf);
    if (!tex) { SDL_FreeSurface(surf); return; }
    SDL_Rect dst{ x, y, surf->w, surf->h };
    SDL_RenderCopy(r, tex, nullptr, &dst);
    SDL_DestroyTexture(tex);
    SDL_FreeSurface(surf);
}

// ObjectSelectable is provided by ui/object_selectable.h

// Simple UI Button with callback
struct Button {
    SDL_Rect rect{0,0,0,0};
    std::string label;
    std::function<void()> on_click;
    bool enabled = true;
    //Look at line ~263. ButtonState should be here
    ButtonStates state;
};

// Back-compat alias until full rename lands everywhere
struct MenuPanel : public UIElement {
    const ObjectSelectable* anchor;
    Camera* cam;
    const bool* is_animating;
    bool visible;
    SDL_Rect btn_center {0,0,0,0};

    bool did_center = false;

    Record* record = nullptr; 

    const bool* suppressed = nullptr; // optional external hide flag

    MenuPanel(const ObjectSelectable* a, Camera* c, const bool* anim, Record* rec, const bool* hide)
        : anchor(a), cam(c), is_animating(anim), visible(false), record(rec), suppressed(hide) {}

    void draw(SDL_Renderer* ren) override {
        visible = anchor && anchor->selected; // tie visibility to selection
        if (suppressed && *suppressed) visible = false;
        if (!visible) return;

        const int panel_w = 180; 
        const int panel_h = 80;

        int sx, sy; 
        world_to_screen(*anchor->cam, (float)anchor->object->x_pixels(), (float)anchor->object->y_pixels(), sx, sy);

        int x = sx + anchor->r + 16;
        int y = sy - panel_h/2;

        //I think we should probably increase this eventually, or get geometry from the system.

        // Clamp to window-ish area assuming 800x600
        x = iclamp(x, 8, 800 - panel_w - 8);
        y = iclamp(y, 8, 600 - panel_h - 8);

        //Where are these colors set? We shold move them out to a config file.
        SDL_Rect bg { x, y, panel_w, panel_h };
        SDL_SetRenderDrawColor(ren, 25, 25, 30, 240);
        SDL_RenderFillRect(ren, &bg);

        SDL_SetRenderDrawColor(ren, 80, 170, 255, 255);
        SDL_RenderDrawRect(ren, &bg);

        // Text
        SDL_SetRenderDrawColor(ren, 240, 240, 240, 255);
        draw_text(ren, x + 10, y + 10, "MENU", 2);

        // Center button
        btn_center = { x + 10, y + 35, 120, 26 };
        SDL_SetRenderDrawColor(ren, 35, 45, 60, 255);
        SDL_RenderFillRect(ren, &btn_center);
        SDL_SetRenderDrawColor(ren, 120, 180, 255, 255);
        SDL_RenderDrawRect(ren, &btn_center);
        SDL_SetRenderDrawColor(ren, 220, 230, 240, 255);
        draw_text(ren, btn_center.x + 8, btn_center.y + 6, "CENTER", 2);

        // (Popup contains only Center now)
    }
    bool handle_mouse_down(int mx, int my) {
        if (!anchor || !visible) return false;
        if (mx >= btn_center.x && mx < btn_center.x + btn_center.w &&
            my >= btn_center.y && my < btn_center.y + btn_center.h) {
            if (cam && anchor->object) { cam->cx = (float)anchor->object->x_pixels(); cam->cy = (float)anchor->object->y_pixels(); }
            did_center = true;
            return true;
        }
        // no other interactive elements here
        return false;
    }
};



struct Game {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool running = true;
    Camera cam;

    // Game mode/state machine
    enum class Mode { MENU = 0, SINGLE, HOST_WAIT, CLIENT_SEARCH, ARCADE };
    Mode mode = Mode::MENU;

    std::vector<std::unique_ptr<ObjectSelectable>> obj_sels;
    std::vector<std::unique_ptr<Object>> objs; // engine objects (canonical)
    std::unique_ptr<MenuPanel> menu;
    Record record;
    std::vector<Button> ui_buttons; // rebuilt each frame in draw()

    ObjectSelectable* selected = nullptr;
    ObjectSelectable* centered = nullptr;

    std::map<std::string, ObjectDefinition> object_defs;

    float game_time = 0.0f; // seconds

    // End Turn button rect computed per-frame in draw; keep a copy for hit test
    SDL_Rect btn_end_turn {0,0,0,0};
    SDL_Rect btn_quit {0,0,0,0};

    // removed SEC+ controls
    SDL_Rect btn_fire {0,0,0,0};
    bool armed_fire = false;
    SDL_Rect btn_newhdg {0,0,0,0};
    bool armed_newhdg = false;
    SDL_Rect btn_accel {0,0,0,0};
    SDL_Rect btn_save {0,0,0,0};
    SDL_Rect btn_next {0,0,0,0};
    SDL_Rect btn_prev {0,0,0,0};

    // Main menu buttons
    SDL_Rect btn_menu_single {0,0,0,0};
    SDL_Rect btn_menu_host {0,0,0,0};
    SDL_Rect btn_menu_connect {0,0,0,0};
    SDL_Rect btn_menu_watch {0,0,0,0};
    SDL_Rect btn_menu_arcade {0,0,0,0};
    SDL_Rect btn_back {0,0,0,0};

    // Simple networking (TCP, non-blocking)
    int listen_fd = -1;     // for host
    int socket_fd = -1;     // for connected/connecting socket
    uint16_t net_port = 55555;
    std::string client_target_ip = "127.0.0.1";

    struct FirePreview {
        bool active = false;
        double theta = 0.0;   // firing direction (radians)
        double t = 0.0;       // flight time used for markers
        // Shooter center
        double sx = 0.0, sy = 0.0;
        // Enemy facing line origin+dir
        double ex = 0.0, ey = 0.0;
        double edirx = 0.0, ediry = 0.0;
        // Marker points at t on enemy line (no accel, with accel)
        double p1x = 0.0, p1y = 0.0;
        double p2x = 0.0, p2y = 0.0;
    } fire_preview;

    // Turn animation state
    bool animating = false;
    int anim_frames_left = 0;
    float anim_dt_per_frame = 0.05f; // 1/20 s
    float anim_accum = 0.0f;
    Uint32 last_tick_ms = 0;
    std::mt19937 rng{std::random_device{}()};
    std::vector<std::unique_ptr<ObjectSelectable>> pending_add;

    //Should probably move this over to Button
    std::map<std::string, ButtonStates> button_colors;

    // Replay control
    bool input_enabled = true;
    bool replay_active = false;
    bool replay_paused = false;
    bool replay_single_step = false;
    Record replay_record;
    size_t replay_turn_idx = 0;
    size_t replay_cmd_idx = 0;
    bool replay_ui_visible = false;

    // Popup suppression: hide popup when other controls are used
    bool popup_suppressed = false;

    // Replay control UI
    SDL_Rect btn_replay_play {0,0,0,0};
    SDL_Rect btn_replay_pause{0,0,0,0};
    SDL_Rect btn_replay_rew  {0,0,0,0};
    SDL_Rect btn_replay_step {0,0,0,0};

    // Arcade controller state
    SDL_GameController* arcade_ctrl = nullptr;
    bool arcade_fire_down_prev = false;
    bool arcade_connected = false;

    // arcade helpers declared later in-class


    //???
    ObjectSelectable* find_uid(uint64_t id) {
        for (auto& sp : obj_sels) if (sp->uid == id) return sp.get();
        return nullptr;
    }

    //???
    static bool parse_kv_u64(const std::string& s, const char* key, uint64_t& out) {
        size_t p = s.find(std::string(key) + "="); if (p == std::string::npos) return false;
        p += std::strlen(key) + 1; char* endp = nullptr; unsigned long long v = std::strtoull(s.c_str() + p, &endp, 10);
        if (endp == s.c_str() + p) return false; out = (uint64_t)v; return true;
    }
    static bool parse_kv_double(const std::string& s, const char* key, double& out) {
        size_t p = s.find(std::string(key) + "="); if (p == std::string::npos) return false;
        p += std::strlen(key) + 1; char* endp = nullptr; double v = std::strtod(s.c_str() + p, &endp);
        if (endp == s.c_str() + p) return false; out = v; return true;
    }

    // Command system: queued actions applied at turn boundary
    std::vector<Command> command_stack;

    // Bridge to command system API
    void queue_command(const Command& c) { ::queue_command(c, command_stack); }
    void execute_commands() {
        ::apply_commands(command_stack, objs, object_defs);
        rebuild_ui_preserve_camera();
    }

    // Rebuild UI wrappers but preserve camera state (zoom/center)
    void rebuild_ui_preserve_camera() {
        float z = cam.zoom; float cx = cam.cx; float cy = cam.cy;
        obj_sels = build_ui_scene(renderer, &cam, objs);
        cam.zoom = z; cam.cx = cx; cam.cy = cy;
    }

    static bool point_in(const SDL_Rect& r, int x, int y) {
        return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
    }

    void save_current_state() {
        std::ofstream out("new_save.json");
        if (out.is_open()) {
            out << "[\n";
            out << std::setprecision(10);
            for (size_t i = 0; i < obj_sels.size(); ++i) {
                const auto& sp = obj_sels[i];
                if (!sp->object) continue;
                if (i) out << ",\n";
                out << "  {\n";
                out << "    \"object\": \"" << (sp->object_key.empty()?"ship1":sp->object_key) << "\",\n";
                out << "    \"x\": " << sp->object->x_pixels() << ",\n";
                out << "    \"y\": " << sp->object->y_pixels() << ",\n";
                out << "    \"vx\": " << ((double)sp->object->vx / (double)Object::FP_ONE) << ",\n";
                out << "    \"vy\": " << ((double)sp->object->vy / (double)Object::FP_ONE) << ",\n";
                out << "    \"theta\": " << sp->object->theta << ",\n";
                int team = sp->object->team;
                bool give_cmd = false;
                double ang_vel = sp->object->ang_vel;
                double target_theta = sp->object->theta;
                int throttle = 0;
                bool dead = sp->object->dead;
                double delta_v = 0.0;
                if (sp->object->type == Object::SHIP) {
                    if (auto sh = dynamic_cast<Ship*>(sp->object)) {
                        team = sh->team; give_cmd = sh->give_commands; ang_vel = sh->ang_vel;
                        target_theta = sh->target_theta; throttle = sh->throttle; dead = sh->dead; delta_v = sh->delta_v;
                    }
                }
                out << "    \"team\": " << team << ",\n";
                out << "    \"give_commands\": " << (give_cmd?"true":"false") << ",\n";
                out << "    \"ang_vel\": " << ang_vel << ",\n";
                out << "    \"target_theta\": " << target_theta << ",\n";
                out << "    \"throttle\": " << throttle << ",\n";
                out << "    \"dead\": " << (dead?"true":"false") << ",\n";
                out << "    \"delta_v\": " << delta_v << "\n";
                out << "  }";
            }
            out << "\n]\n";
        }
        DBG("SAVE clicked -> wrote new_save.json (open=%d)", (int)out.is_open());
    }

    void apply_replay_command(const std::string& cmd) {
        // Mirror replayed command into current record so it is saved on exit
        record.add(cmd);
        if (cmd.rfind("THROTTLE", 0) == 0) {
            uint64_t uid=0; double v=0.0; if (!parse_kv_u64(cmd, "uid", uid)) return; if (!parse_kv_double(cmd, "value", v)) return;
            Command c; c.type = Command::Type::THROTTLE; c.uid = uid; c.a = v; if (auto* s = find_uid(uid)) { if (s->object && s->object->type == Object::SHIP) c.ship = dynamic_cast<Ship*>(s->object); } queue_command(c);
            return;
        }
        if (cmd.rfind("HEADING", 0) == 0) {
            uint64_t uid=0; double th=0.0; if (!parse_kv_u64(cmd, "uid", uid)) return; if (!parse_kv_double(cmd, "theta", th)) return;
            Command c; c.type = Command::Type::HEADING; c.uid = uid; c.a = th; if (auto* s = find_uid(uid)) { if (s->object && s->object->type == Object::SHIP) c.ship = dynamic_cast<Ship*>(s->object); } queue_command(c);
            return;
        }
        if (cmd.rfind("FIRE", 0) == 0) {
            uint64_t uid=0; double th=0.0; if (!parse_kv_u64(cmd, "uid", uid)) return; if (!parse_kv_double(cmd, "theta", th)) return;
            Command c; c.type = Command::Type::FIRE; c.uid = uid; c.a = th; if (auto* s = find_uid(uid)) { if (s->object && s->object->type == Object::SHIP) { auto sp = dynamic_cast<Ship*>(s->object); c.ship = sp; c.key = pick_projectile_key(*sp); } } queue_command(c);
            return;
        }
        // END_TURN handled in driver
    }

    void step_replay_command_once() {
        if (replay_turn_idx >= replay_record.turns.size()) { replay_active = false; input_enabled = true; return; }
        const auto& turn = replay_record.turns[replay_turn_idx];
        if (replay_cmd_idx >= turn.commands.size()) { replay_active = false; input_enabled = true; return; }
        const std::string& c = turn.commands[replay_cmd_idx++];
        if (c == "END_TURN") {
            record.add("END_TURN");
            record.start_turn();
            // Apply queued commands for this turn before animation
            execute_commands();
            // no-op to snapshot state if needed; engine objects already hold state
            animating = true; anim_frames_left = 20; anim_dt_per_frame = 1.0f / 20.0f; anim_accum = 0.0f; fire_preview.active = false;
            ++replay_turn_idx; replay_cmd_idx = 0;
        } else {
            apply_replay_command(c);
        }
    }

    void advance_one_frame() {
        // One simulation substep of a turn
        for (auto& o : objs) o->advance(anim_dt_per_frame);
        // Bullet-ship collisions using UI wrappers for radius, engine objects for truth
        std::vector<Object*> rm_bullets;
        std::vector<Object*> rm_obj_sels;
        for (size_t i = 0; i < obj_sels.size(); ++i) {
            Object* oi = obj_sels[i]->object;
            if (!oi || oi->dead) continue;
            bool is_bullet_i = (oi->type == Object::PROJECTILE);
            for (size_t j = 0; j < obj_sels.size(); ++j) {
                if (i == j) continue;
                Object* oj = obj_sels[j]->object; if (!oj || oj->dead) continue;
                bool is_ship_j = (oj->type == Object::SHIP);
                if (is_bullet_i && is_ship_j) {
                    if (!can_collide(*oi, *oj)) continue;
                    double dx = oi->x_pixels() - oj->x_pixels();
                    double dy = oi->y_pixels() - oj->y_pixels();
                    double R = (double)obj_sels[j]->r;
                    if (dx*dx + dy*dy <= R*R) {
                        rm_bullets.push_back(oi);
                        if (auto* sh = dynamic_cast<Ship*>(oj)) {
                            auto debris = ::spawn_debris_for(*sh, sh->team, rng);
                            for (const auto& d : debris) {
                                auto itdef = object_defs.find(d.key);
                                if (itdef != object_defs.end()) {
                                    const auto& ddef = itdef->second;
                                    InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                                    if (ddef.type == "ship") objs.push_back(std::make_unique<Ship>(ddef, init));
                                    else if (ddef.type == "planet") objs.push_back(std::make_unique<Planet>(ddef, init));
                                    else objs.push_back(std::make_unique<Object>(ddef, init));
                                }
                            }
                        }
                        rm_obj_sels.push_back(oj);
                        break;
                    }
                }
            }
        }
        if (!rm_bullets.empty() || !rm_obj_sels.empty()) {
            auto remove_ptr = [&](Object* p){
                for (size_t k = 0; k < objs.size(); ++k) if (objs[k].get() == p) { objs.erase(objs.begin() + k); return; }
            };
            for (auto* p : rm_bullets) remove_ptr(p);
            for (auto* p : rm_obj_sels) remove_ptr(p);
        }
        // Rebuild UI from engine state after changes
        rebuild_ui_preserve_camera();
        // If centered ship was destroyed (removed), re-center to first of player's team
        if (centered) {
            bool found = false;
            for (auto& sp : obj_sels) if (sp.get() == centered) { found = true; break; }
            if (!found) centered = nullptr;
        }
        if (!centered) {
            ObjectSelectable* fallback = nullptr;
            for (auto& sp : obj_sels) {
                if (sp->object && sp->object->type == Object::SHIP) { auto sh = dynamic_cast<Ship*>(sp->object); if (sh && sh->team == 0 && sh->give_commands && !sh->dead) { fallback = sp.get(); break; } }
            }
            centered = fallback;
            selected = fallback;
            if (menu) menu->anchor = selected;
        }
        game_time += anim_dt_per_frame;
        if (--anim_frames_left <= 0) {
            animating = false;
            anim_accum = 0.0f;
            // Collision check at end of step: mark obj_sels dead if circles overlap (only commandable obj_sels)
            const float INF = 1e30f;
            // float minx = -INF, miny = -INF, maxx = INF, maxy = INF; // unused
            std::vector<Object*> rm_objs;
            for (size_t i = 0; i < obj_sels.size(); ++i) {
                for (size_t j = i + 1; j < obj_sels.size(); ++j) {
                    auto& A = *obj_sels[i];
                    auto& B = *obj_sels[j];
                    if (!A.object || !B.object) continue;
                    if (!can_collide(*A.object, *B.object)) continue;
                    auto AS = (A.object->type == Object::SHIP) ? dynamic_cast<Ship*>(A.object) : nullptr;
                    auto BS = (B.object->type == Object::SHIP) ? dynamic_cast<Ship*>(B.object) : nullptr;
                    if (!AS || !BS) continue; // only ship-ship
                    double dx = A.object->x_pixels() - B.object->x_pixels();
                    double dy = A.object->y_pixels() - B.object->y_pixels();
                    double R = (double)A.r + (double)B.r;
                    if (dx*dx + dy*dy <= R*R) {
                        std::fprintf(stderr, "[collide] ship idx=%zu <-> ship idx=%zu\n", i, j);
                        // Spawn debris for both and mark for removal
                        {
                            auto debrisA = ::spawn_debris_for(*AS, AS->team, rng);
                            for (const auto& d : debrisA) {
                                auto itdef = object_defs.find(d.key);
                                if (itdef != object_defs.end()) {
                                    const ObjectDefinition& ddef = itdef->second;
                                    InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                                    if (ddef.type == "ship") objs.push_back(std::make_unique<Ship>(ddef, init));
                                    else if (ddef.type == "planet") objs.push_back(std::make_unique<Planet>(ddef, init));
                                    else objs.push_back(std::make_unique<Object>(ddef, init));
                                }
                            }
                            auto debrisB = ::spawn_debris_for(*BS, BS->team, rng);
                            for (const auto& d : debrisB) {
                                auto itdef = object_defs.find(d.key);
                                if (itdef != object_defs.end()) {
                                    const ObjectDefinition& ddef = itdef->second;
                                    InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                                    if (ddef.type == "ship") objs.push_back(std::make_unique<Ship>(ddef, init));
                                    else if (ddef.type == "planet") objs.push_back(std::make_unique<Planet>(ddef, init));
                                    else objs.push_back(std::make_unique<Object>(ddef, init));
                                }
                            }
                        }
                        rm_objs.push_back(A.object);
                        rm_objs.push_back(B.object);
                    }
                }
            }
            if (!rm_objs.empty()) {
                auto remove_ptr = [&](Object* p){ for (size_t k = 0; k < objs.size(); ++k) if (objs[k].get() == p) { objs.erase(objs.begin() + k); return; } };
                for (auto* p : rm_objs) remove_ptr(p);
                rebuild_ui_preserve_camera();
            }
            // If centered ship was removed, re-center
            if (centered) {
                bool found = false;
                for (auto& sp : obj_sels) if (sp.get() == centered) { found = true; break; }
                if (!found) centered = nullptr;
            }
            if (!centered) {
                ObjectSelectable* fallback = nullptr;
                for (auto& sp : obj_sels) {
                    if (sp->object && sp->object->type == Object::SHIP) { auto sh = dynamic_cast<Ship*>(sp->object); if (sh && sh->team == 0 && sh->give_commands && !sh->dead) { fallback = sp.get(); break; } }
                }
                centered = fallback;
                selected = fallback;
                if (menu) menu->anchor = selected;
            }
            // Reset per-turn states at the beginning of the next turn; heading target persists
            for (auto& o : objs) { if (auto sh = dynamic_cast<Ship*>(o.get())) { sh->throttle = 0; sh->fired_this_turn = false; } }
        }
    }

    struct Stick { double theta=0.0; double mag=0.0; };

    void open_first_controller() {
        if (arcade_ctrl) return;
        int num = SDL_NumJoysticks();
        for (int i = 0; i < num; ++i) {
            if (SDL_IsGameController(i)) {
                arcade_ctrl = SDL_GameControllerOpen(i);
                if (arcade_ctrl) { arcade_connected = true; break; }
            }
        }
    }
    static double norm_axis(Sint16 v){ return (v >= 0) ? (double)v/32767.0 : (double)v/32768.0; }
    static Stick stick_polar(double x, double y){ Stick s; double d=std::sqrt(x*x+y*y); if (d<1e-6){s.theta=0.0;s.mag=0.0;} else { s.theta=std::atan2(y,x); s.mag=d>1.0?1.0:d; } return s; }

    void poll_arcade_input_and_apply(ObjectSelectable* player) {
        if (!player) return;
        if (!arcade_ctrl) { open_first_controller(); return; }
        double lx = norm_axis(SDL_GameControllerGetAxis(arcade_ctrl, SDL_CONTROLLER_AXIS_LEFTX));
        double ly = -norm_axis(SDL_GameControllerGetAxis(arcade_ctrl, SDL_CONTROLLER_AXIS_LEFTY));
        double rx = norm_axis(SDL_GameControllerGetAxis(arcade_ctrl, SDL_CONTROLLER_AXIS_RIGHTX));
        double ry = -norm_axis(SDL_GameControllerGetAxis(arcade_ctrl, SDL_CONTROLLER_AXIS_RIGHTY));
        Stick L = stick_polar(lx, ly);
        Stick R = stick_polar(rx, ry);
        const double dead = 0.25;
        // Queue desired heading as a command when the stick is active
        if (L.mag > dead) {
            if (player->object && player->object->type == Object::SHIP) { Command c; c.type = Command::Type::HEADING; c.uid = player->uid; c.ship = dynamic_cast<Ship*>(player->object); c.a = L.theta; queue_command(c); }
        }
        // Queue throttle each tick (last-wins) based on left shoulder
        bool accel = SDL_GameControllerGetButton(arcade_ctrl, SDL_CONTROLLER_BUTTON_LEFTSHOULDER);
        {
            if (player->object && player->object->type == Object::SHIP) { auto sh = dynamic_cast<Ship*>(player->object); Command c; c.type = Command::Type::THROTTLE; c.uid = player->uid; c.ship = sh; c.a = (accel && sh->delta_v > 0.0) ? 1.0 : 0.0; queue_command(c); }
        }
        // Queue FIRE only on X button edge, using right-stick direction if available
        bool fire_down = SDL_GameControllerGetButton(arcade_ctrl, SDL_CONTROLLER_BUTTON_X);
        if (fire_down && !arcade_fire_down_prev) {
            if (player->object && player->object->type == Object::SHIP) { auto sh = dynamic_cast<Ship*>(player->object); double theta = (R.mag > dead) ? R.theta : sh->theta; Command c; c.type = Command::Type::FIRE; c.uid = player->uid; c.ship = sh; c.a = theta; c.key = pick_projectile_key(*sh); queue_command(c); }
        }
        arcade_fire_down_prev = fire_down;
    }

    void advance_arcade_frame(double dt_seconds) {
        // Use command system: apply queued commands, then integrate
        execute_commands();
        for (auto& o : objs) o->advance(dt_seconds);
        std::vector<Object*> rm_bullets; std::vector<Object*> rm_obj_sels;
        for (size_t i = 0; i < obj_sels.size(); ++i) {
            if (!obj_sels[i]->object || obj_sels[i]->object->dead) continue;
            bool is_bullet_i = (obj_sels[i]->object && obj_sels[i]->object->type == Object::PROJECTILE);
            for (size_t j = 0; j < obj_sels.size(); ++j) {
                if (i == j) continue; if (!obj_sels[j]->object || obj_sels[j]->object->dead) continue;
                bool is_ship_j = (obj_sels[j]->object->type == Object::SHIP);
                if (is_bullet_i && is_ship_j) {
                    if (!can_collide(*obj_sels[i]->object, *obj_sels[j]->object)) continue;
                    double dx = obj_sels[i]->object->x_pixels() - obj_sels[j]->object->x_pixels();
                    double dy = obj_sels[i]->object->y_pixels() - obj_sels[j]->object->y_pixels();
                    double R = (double)obj_sels[j]->r;
                    if (dx*dx + dy*dy <= R*R) {
                        rm_bullets.push_back(obj_sels[i]->object);
                        if (auto sh = dynamic_cast<Ship*>(obj_sels[j]->object)) {
                            auto debris = ::spawn_debris_for(*sh, sh->team, rng);
                            for (const auto& d : debris) {
                                const ObjectDefinition* ddef = nullptr; auto itdef2 = object_defs.find(d.key); if (itdef2 != object_defs.end()) ddef = &itdef2->second;
                                if (ddef) {
                                    InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                                    if (ddef->type == "ship") objs.push_back(std::make_unique<Ship>(*ddef, init));
                                    else if (ddef->type == "planet") objs.push_back(std::make_unique<Planet>(*ddef, init));
                                    else objs.push_back(std::make_unique<Object>(*ddef, init));
                                }
                            }
                        }
                        rm_obj_sels.push_back(obj_sels[j]->object);
                        break;
                    }
                }
            }
        }
        if (!rm_bullets.empty() || !rm_obj_sels.empty()) {
            auto remove_ptr = [&](Object* p){ for (size_t k = 0; k < objs.size(); ++k) if (objs[k].get() == p) { objs.erase(objs.begin() + k); return; } };
            for (auto* p : rm_bullets) remove_ptr(p);
            for (auto* p : rm_obj_sels) remove_ptr(p);
            rebuild_ui_preserve_camera();
        }
        std::vector<Object*> rm;
        for (size_t i = 0; i < obj_sels.size(); ++i) {
            for (size_t j = i + 1; j < obj_sels.size(); ++j) {
                auto& A = *obj_sels[i]; auto& B = *obj_sels[j];
                if (!A.object || !B.object) continue;
                if (!can_collide(*A.object, *B.object)) continue;
                auto AS = (A.object->type == Object::SHIP) ? dynamic_cast<Ship*>(A.object) : nullptr;
                auto BS = (B.object->type == Object::SHIP) ? dynamic_cast<Ship*>(B.object) : nullptr;
                if (!AS || !BS) continue;
                double dx = A.object->x_pixels() - B.object->x_pixels();
                double dy = A.object->y_pixels() - B.object->y_pixels();
                double R = (double)A.r + (double)B.r;
                if (dx*dx + dy*dy <= R*R) {
                    auto debrisA = ::spawn_debris_for(*AS, AS->team, rng);
                    auto debrisB = ::spawn_debris_for(*BS, BS->team, rng);
                    for (const auto& d : debrisA) {
                        const ObjectDefinition* ddef = nullptr; auto itdef2 = object_defs.find(d.key); if (itdef2 != object_defs.end()) ddef = &itdef2->second;
                        if (ddef) {
                            InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                            if (ddef->type == "ship") objs.push_back(std::make_unique<Ship>(*ddef, init));
                            else if (ddef->type == "planet") objs.push_back(std::make_unique<Planet>(*ddef, init));
                            else objs.push_back(std::make_unique<Object>(*ddef, init));
                        }
                    }
                    for (const auto& d : debrisB) {
                        const ObjectDefinition* ddef = nullptr; auto itdef2 = object_defs.find(d.key); if (itdef2 != object_defs.end()) ddef = &itdef2->second;
                        if (ddef) {
                            InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                            if (ddef->type == "ship") objs.push_back(std::make_unique<Ship>(*ddef, init));
                            else if (ddef->type == "planet") objs.push_back(std::make_unique<Planet>(*ddef, init));
                            else objs.push_back(std::make_unique<Object>(*ddef, init));
                        }
                    }
                    rm.push_back(A.object); rm.push_back(B.object);
                }
            }
        }
        if (!rm.empty()) { auto remove_ptr = [&](Object* p){ for (size_t k = 0; k < objs.size(); ++k) if (objs[k].get() == p) { objs.erase(objs.begin() + k); return; } }; for (auto* p : rm) remove_ptr(p); rebuild_ui_preserve_camera(); }
    }

    void reset_to_initial_state() {
        // Reload obj_sels using scene loader (no JSON in main)
        obj_sels.clear();
        objs.clear();
        std::string err;
        std::string savePath = "save.json";
        if (!file_exists(savePath)) {
            if (const GameConfig* gc = get_global_game_config()) {
                if (!gc->paths.saves.empty()) {
                    std::string cand = gc->paths.saves + "/save.json";
                    if (file_exists(cand)) savePath = cand;
                }
            }
        }
        if (load_scene_objects(savePath.c_str(), object_defs, objs, &err)) {
            rebuild_ui_preserve_camera();
        }
        select_first_player_ship();
        game_time = 0.0f;
        animating = false; anim_accum = 0.0f; anim_frames_left = 0; fire_preview.active = false;
    }
    void drive_replay() {
        if (!replay_active) return;
        if (mode != Mode::SINGLE) return;
        // Gamely queued commands until we need to animate
        while (!animating) {
            if (replay_turn_idx >= replay_record.turns.size()) {
                // finished
                replay_active = false; input_enabled = true;
                break;
            }
            const auto& turn = replay_record.turns[replay_turn_idx];
            if (replay_cmd_idx >= turn.commands.size()) {
                // No explicit END_TURN; stop
                replay_active = false; input_enabled = true; break;
            }
            const std::string& c = turn.commands[replay_cmd_idx++];
            if (c == "END_TURN") {
                record.add("END_TURN");
                record.start_turn();
                // Apply queued commands for this turn, then start animation (same as button)
                execute_commands();
        // engine objects already hold authoritative state
                animating = true; anim_frames_left = 20; anim_dt_per_frame = 1.0f / 20.0f; anim_accum = 0.0f; fire_preview.active = false;
                // Move to next turn
                ++replay_turn_idx; replay_cmd_idx = 0;
                break;
            } else {
                apply_replay_command(c);
                // continue to process next command of this turn
            }
        }
    }

    bool start_watch_record(const std::string& path) {
        std::string err;
        if (!replay_record.load_json(path, &err)) {
            std::fprintf(stderr, "[replay] failed to load %s: %s\n", path.c_str(), err.c_str());
            return false;
        }
        // Seed PRNG to recorded seed for deterministic debris, etc.
        rng.seed(replay_record.random_seed);
        std::fprintf(stderr, "[replay] using random_seed=%u\n", replay_record.random_seed);
        // During replay, treat all obj_sels as non-commandable
        for (auto& sp : obj_sels) { if (auto sh = (sp->object && sp->object->type == Object::SHIP) ? dynamic_cast<Ship*>(sp->object) : nullptr) sh->give_commands = false; }
        // Start a fresh recording that mirrors the replayed events
        record.start_match();
        record.random_seed = replay_record.random_seed;
        record.start_turn();
        replay_ui_visible = true;
        replay_active = true;
        input_enabled = false;
        replay_turn_idx = 0; replay_cmd_idx = 0;
        // Center camera to first player ship for consistency
        select_first_player_ship();
        std::fprintf(stderr, "[replay] watching %zu turns\n", replay_record.turns.size());
        return true;
    }

    bool init(const char* title, int w, int h) {
        DBG("Game::init title=%s w=%d h=%d", title ? title : "<null>", w, h);
        if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
            std::fprintf(stderr, "SDL_Init Error: %s\n", SDL_GetError());
            return false;
        }
        DBG("SDL_Init ok");
        if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
            std::fprintf(stderr, "IMG_Init PNG Error: %s\n", IMG_GetError());
            return false;
        }
        DBG("IMG_Init PNG ok");
        if (TTF_Init() == 0) {
            // Load fonts via config/ui.json
            UIConfig uicfg; std::string uerr;
            (void)load_ui_config("config/ui.json", uicfg, &uerr);
            if (!uerr.empty()) std::fprintf(stderr, "[ui-config] %s\n", uerr.c_str());
            auto resolve_font = [&](const std::string& p){
                if (p.empty()) return p;
                std::ifstream f(p, std::ios::binary); if (f.good()) return p;
                if (const GameConfig* gc = get_global_game_config()) {
                    if (!gc->paths.assets.empty()) {
                        std::string cand = gc->paths.assets + "/" + p;
                        std::ifstream g(cand, std::ios::binary); if (g.good()) return cand;
                    }
                }
                return p; // return as-is; will fail below
            };
            std::string fontPath = resolve_font(uicfg.font_path);
            if (!fontPath.empty()) {
                g_font_small = TTF_OpenFont(fontPath.c_str(), uicfg.font_small);
                g_font_medium = TTF_OpenFont(fontPath.c_str(), uicfg.font_medium);
                g_font_large = TTF_OpenFont(fontPath.c_str(), uicfg.font_large);
            }
            if (g_font_small || g_font_medium || g_font_large) {
                g_ttf_ready = true;
                std::fprintf(stderr, "[ttf] font loaded: %s (small=%p, med=%p, large=%p)\n",
                             fontPath.c_str(), (void*)g_font_small, (void*)g_font_medium, (void*)g_font_large);
            } else {
                std::fprintf(stderr, "[ttf] no TTF font opened; set fonts.path in config/ui.json\n");
            }
        } else {
            std::fprintf(stderr, "[ttf] TTF_Init failed: %s (fallback to bitmap)\n", TTF_GetError());
        }
        window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_SHOWN);
        if (!window) {
            std::fprintf(stderr, "SDL_CreateWindow Error: %s\n", SDL_GetError());
            return false;
        }
        DBG("SDL_CreateWindow ok");
        renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer) {
            std::fprintf(stderr, "SDL_CreateRenderer Error: %s\n", SDL_GetError());
            return false;
        }
        DBG("SDL_CreateRenderer ok");

        cam.screen_w = w; cam.screen_h = h; cam.cx = 0.0f; cam.cy = 0.0f; cam.zoom = 1.0f;
        // Load object definitions
        {
            std::string err;
            std::string objPath = "objects.json";
            if (!file_exists(objPath)) {
                if (const GameConfig* gc = get_global_game_config()) {
                    if (!gc->paths.assets.empty()) {
                        std::string cand = gc->paths.assets + "/objects.json";
                        if (file_exists(cand)) objPath = cand;
                    }
                }
            }
            bool ok = load_object_defs (objPath.c_str(), object_defs, &err);
            if (!ok) {
                std::fprintf(stderr, "FATAL: failed to load object defs from %s: %s\n", objPath.c_str(), err.c_str());
                std::exit(ExitCode::LOADING_ERROR);
            }
        }
        DBG("object_defs loaded: %zu", object_defs.size());

        // Load button colors from config/ui.json
        {
            std::string errc;
            if (load_button_colors_from_ui("config/ui.json", button_colors, &errc)) {
                std::fprintf (stderr, "[ui] loaded %zu button colors (ui.json)\n", button_colors.size());
            } else {
                std::fprintf (stderr, "[ui] no button colors loaded: %s\n", errc.c_str());
            }
        }

        // Load obj_sels from save.json via scene loader (no JSON in main)
        std::string err;
        std::string savePath = "save.json";
        if (!file_exists(savePath)) {
            if (const GameConfig* gc = get_global_game_config()) {
                if (!gc->paths.saves.empty()) {
                    std::string cand = gc->paths.saves + "/save.json";
                    if (file_exists(cand)) savePath = cand;
                }
            }
        }
        // Load engine objects into member 'objs'
        if (load_scene_objects(savePath.c_str(), object_defs, objs, &err)) {
            rebuild_ui_preserve_camera();
        } else {
            // Fallback: two sample obj_sels if loading failed
            InitialState a; a.image = "ship1.png"; a.x = 0.0f; a.y = 0.0f; a.vx = 0.0f; a.vy = 0.0f; a.theta = 0.0f; a.has_x = a.has_y = a.has_vx = a.has_vy = a.has_theta = true; a.has_ang_vel = true; a.ang_vel = 0.0f; a.has_delta_v = true; a.delta_v = 0.0f;
            InitialState b; b.image = "ship1.png"; b.x = 300.0f; b.y = 0.0f; b.vx = 0.0f; b.vy = 0.0f; b.theta = 0.0f; b.has_x = b.has_y = b.has_vx = b.has_vy = b.has_theta = true; b.has_ang_vel = true; b.ang_vel = 0.0f; b.has_delta_v = true; b.delta_v = 0.0f;
            const ObjectDefinition* defA = nullptr; auto itA = object_defs.find(a.image); if (itA != object_defs.end()) defA = &itA->second;
            if (defA) {
                if (defA->type == "ship") objs.push_back(std::make_unique<Ship>(*defA, a));
                else if (defA->type == "planet") objs.push_back(std::make_unique<Planet>(*defA, a));
                else objs.push_back(std::make_unique<Object>(*defA, a));
            }
            const ObjectDefinition* defB = nullptr; auto itB = object_defs.find(b.image); if (itB != object_defs.end()) defB = &itB->second;
            if (defB) {
                if (defB->type == "ship") objs.push_back(std::make_unique<Ship>(*defB, b));
                else if (defB->type == "planet") objs.push_back(std::make_unique<Planet>(*defB, b));
                else objs.push_back(std::make_unique<Object>(*defB, b));
            }
            rebuild_ui_preserve_camera();
        }

        // Menu follows current selection (none initially)
        menu = std::make_unique<MenuPanel>(nullptr, &cam, &animating, &record, &popup_suppressed);
        last_tick_ms = SDL_GetTicks();
        mode = Mode::MENU; // start at main menu
        return true;
    }

    // Networking helpers
    static void set_nonblocking(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags >= 0) fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
    void start_host() {
        if (listen_fd != -1) return;
        listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd < 0) { std::perror("socket"); return; }
        int yes = 1; setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_addr.s_addr = INADDR_ANY; addr.sin_port = htons(net_port);
        if (bind(listen_fd, (sockaddr*)&addr, sizeof(addr)) < 0) { std::perror("bind"); ::close(listen_fd); listen_fd = -1; return; }
        if (listen(listen_fd, 1) < 0) { std::perror("listen"); ::close(listen_fd); listen_fd = -1; return; }
        set_nonblocking(listen_fd);
    }
    void stop_host() {
        if (listen_fd != -1) { ::close(listen_fd); listen_fd = -1; }
    }
    void poll_host_accept() {
        if (listen_fd == -1) return;
        sockaddr_in cli{}; socklen_t len = sizeof(cli);
        int fd = ::accept(listen_fd, (sockaddr*)&cli, &len);
        if (fd >= 0) {
            set_nonblocking(fd);
            socket_fd = fd;
            std::fprintf(stderr, "[net] client connected from %s:%d\n", inet_ntoa(cli.sin_addr), ntohs(cli.sin_port));
            // Transition to game for now
            mode = Mode::SINGLE;
            stop_host();
        }
    }
    void start_client() {
        if (socket_fd != -1) return;
        socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (socket_fd < 0) { std::perror("socket"); socket_fd = -1; return; }
        set_nonblocking(socket_fd);
        sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(net_port);
        if (::inet_pton(AF_INET, client_target_ip.c_str(), &addr.sin_addr) != 1) { std::fprintf(stderr, "[net] invalid IP\n"); ::close(socket_fd); socket_fd = -1; return; }
        int r = ::connect(socket_fd, (sockaddr*)&addr, sizeof(addr));
        if (r == 0) {
            std::fprintf(stderr, "[net] connected immediately to host\n");
            mode = Mode::SINGLE;
        } else {
            if (errno == EINPROGRESS) {
                // keep polling
            } else {
                std::perror("connect"); ::close(socket_fd); socket_fd = -1;
            }
        }
    }
    void poll_client_connect() {
        if (socket_fd == -1) return;
        // Check connection completion
        int err = 0; socklen_t len = sizeof(err);
        if (getsockopt(socket_fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) return;
        if (err == 0) {
            std::fprintf(stderr, "[net] client connected to host\n");
            mode = Mode::SINGLE;
        } else if (err != EINPROGRESS) {
            // connection failed; keep trying? For now, leave socket and keep waiting.
        }
    }
    void close_socket() {
        if (socket_fd != -1) { ::close(socket_fd); socket_fd = -1; }
    }

    // removed: spawn_debris_for is now free function in object.{h,cpp}

    // Utility: center/select first eligible player ship and update menu anchor
    void select_first_player_ship() {
        centered = nullptr;
        selected = nullptr;
        for (auto& sp : obj_sels) {
            if (sp->object && sp->object->type == Object::SHIP) {
                auto sh = dynamic_cast<Ship*>(sp->object);
                if (sh && sh->team == 0 && sh->give_commands && !sh->dead) {
                    centered = sp.get();
                    selected = sp.get();
                    break;
                }
            }
        }
        if (menu) menu->anchor = selected;
    }

    // Helpers: click handling extracted from handle_event
    bool click_inside(const SDL_Rect& r, int x, int y) const { return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h; }

    bool handle_precheck_quit(int mx, int my) {
        if (mode == Mode::SINGLE || mode == Mode::ARCADE) {
            DBG("Pre-check QUIT: rect x=%d y=%d w=%d h=%d (mx=%d my=%d)", btn_quit.x, btn_quit.y, btn_quit.w, btn_quit.h, mx, my);
            if (click_inside(btn_quit, mx, my)) {
                DBG("QUIT clicked (pre-check) -> exit");
                running = false;
                return true;
            }
        }
        return false;
    }

    bool handle_back_button(int mx, int my) {
        if (mode != Mode::SINGLE) {
            DBG("non-SINGLE mode click; back button rect x=%d y=%d w=%d h=%d", btn_back.x, btn_back.y, btn_back.w, btn_back.h);
            if (click_inside(btn_back, mx, my)) {
                DBG("BACK clicked -> go to MENU");
                stop_host();
                close_socket();
                mode = Mode::MENU;
                return true;
            }
        }
        return false;
    }

    bool handle_main_menu_clicks(int mx, int my) {
        if (mode != Mode::MENU) return false;
        if (click_inside(btn_menu_single, mx, my)) {
            mode = Mode::SINGLE;
            record.start_match();
            record.start_turn();
            {
                std::random_device rd; uint32_t seed = rd(); rng.seed(seed); record.random_seed = seed; std::fprintf(stderr, "[seed] random_seed=%u\n", seed);
            }
            return true;
        }
        if (click_inside(btn_menu_host, mx, my)) { mode = Mode::HOST_WAIT; start_host(); return true; }
        if (click_inside(btn_menu_connect, mx, my)) { mode = Mode::CLIENT_SEARCH; start_client(); return true; }
        if (click_inside(btn_menu_watch, mx, my)) {
            mode = Mode::SINGLE;
            if (!start_watch_record("record.json")) mode = Mode::MENU;
            return true;
        }
        if (click_inside(btn_menu_arcade, mx, my)) { mode = Mode::ARCADE; select_first_player_ship(); open_first_controller(); return true; }
        return false;
    }

    bool handle_arcade_clicks(int mx, int my) {
        if (mode != Mode::ARCADE) return false;
        DBG("ARCADE click; quit rect x=%d y=%d w=%d h=%d", btn_quit.x, btn_quit.y, btn_quit.w, btn_quit.h);
        if (click_inside(btn_quit, mx, my)) {
            DBG("QUIT clicked (arcade) -> exit");
            popup_suppressed = true;
            running = false;
            return true;
        }
        return false;
    }

    bool handle_replay_controls_clicks(int mx, int my) {
        if (!replay_ui_visible) return false;
        DBG("Replay UI visible; checking replay controls");
        if (click_inside(btn_replay_rew, mx, my)) {
            rng.seed(replay_record.random_seed);
            reset_to_initial_state();
            for (auto& sp : obj_sels) { if (auto sh = (sp->object && sp->object->type == Object::SHIP) ? dynamic_cast<Ship*>(sp->object) : nullptr) sh->give_commands = false; }
            replay_turn_idx = 0; replay_cmd_idx = 0; replay_paused = true; animating = false; fire_preview.active = false; replay_active = true; input_enabled = false;
            return true;
        }
        if (click_inside(btn_replay_play, mx, my)) { replay_active = true; replay_paused = false; input_enabled = false; return true; }
        if (click_inside(btn_replay_pause, mx, my)) { replay_paused = true; return true; }
        if (click_inside(btn_replay_step, mx, my)) {
            if (animating) replay_single_step = true; else step_replay_command_once();
            return true;
        }
        return false;
    }

    bool handle_menu_panel_click(int mx, int my) {
        if (menu && menu->handle_mouse_down(mx, my)) {
            DBG("menu->handle_mouse_down consumed click");
            if (menu->did_center) { centered = selected; menu->did_center = false; }
            return true;
        }
        return false;
    }

    bool handle_right_click_actions(const SDL_MouseButtonEvent& btn) {
        if (btn.button != SDL_BUTTON_RIGHT) return false;
        if (armed_fire && !animating && centered) {
            auto centered_sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr;
            if (centered_sh && !centered_sh->dead && centered_sh->team == 0 && centered_sh->give_commands && !centered_sh->fired_this_turn) {
                auto inside = [&](const SDL_Rect& r){ return click_inside(r, btn.x, btn.y); };
                if (!(inside(btn_end_turn) || inside(btn_quit) || inside(btn_fire) || inside(btn_newhdg))) {
                    float wx, wy; screen_to_world(cam, btn.x, btn.y, wx, wy);
                    double sx = centered->object->x_pixels(); double sy = centered->object->y_pixels();
                    double dx = (double)wx - sx; double dy = (double)wy - sy; double theta = std::atan2(dy, dx);
                    Command c; c.type = Command::Type::FIRE; c.uid = centered->uid; c.ship = centered_sh; c.a = theta; c.key = pick_projectile_key(*centered_sh);
                    queue_command(c);
                    armed_fire = false; fire_preview.active = false;
                    std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(6);
                    ss << "FIRE uid=" << centered->uid << " theta=" << theta << " wx=" << wx << " wy=" << wy; record.add(ss.str());
                    return true;
                }
            }
        } else if (armed_newhdg && !animating && centered) {
            auto centered_sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr;
            if (centered_sh && !centered_sh->dead && centered_sh->team == 0) {
                auto inside = [&](const SDL_Rect& r){ return click_inside(r, btn.x, btn.y); };
                if (!(inside(btn_end_turn) || inside(btn_quit) || inside(btn_fire) || inside(btn_newhdg))) {
                    float wx, wy; screen_to_world(cam, btn.x, btn.y, wx, wy);
                    double sx = centered->object->x_pixels(); double sy = centered->object->y_pixels();
                    double dx = (double)wx - sx; double dy = (double)wy - sy; double theta = std::atan2(dy, dx);
                    Command c; c.type = Command::Type::HEADING; c.uid = centered->uid; c.ship = centered_sh; c.a = theta; queue_command(c);
                    armed_newhdg = false;
                    std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(6);
                    ss << "HEADING uid=" << centered->uid << " theta=" << theta; record.add(ss.str());
                    return true;
                }
            }
        }
        return false;
    }

    void maybe_build_fire_preview_leftclick(int mx, int my) {
        if (!(armed_fire && centered && centered->object && !centered->object->dead)) return;
        ObjectSelectable* hit = nullptr; int best_d2 = 0, best_sx = 0, best_sy = 0;
        for (auto& s : obj_sels) {
            if (!s->hit(mx, my)) continue; int sx_i, sy_i; world_to_screen(cam, (float)s->object->x_pixels(), (float)s->object->y_pixels(), sx_i, sy_i);
            int dx_i = mx - sx_i; int dy_i = my - sy_i; int d2 = dx_i*dx_i + dy_i*dy_i;
            if (!hit || d2 < best_d2 || (d2 == best_d2 && (sy_i < best_sy || (sy_i == best_sy && sx_i < best_sx)))) { hit = s.get(); best_d2 = d2; best_sx = sx_i; best_sy = sy_i; }
        }
        if (!hit || hit == centered) return;
        auto centered_sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr;
        auto hit_sh = (hit->object && hit->object->type == Object::SHIP) ? dynamic_cast<Ship*>(hit->object) : nullptr;
        if (!(centered_sh && hit_sh && hit_sh->team != centered_sh->team)) return;
        float wx, wy; screen_to_world(cam, mx, my, wx, wy);
        double sx = centered->object->x_pixels(); double sy = centered->object->y_pixels();
        double dx = (double)wx - sx; double dy = (double)wy - sy; double theta = std::atan2(dy, dx);
        double cs = std::cos(theta), sn = std::sin(theta);
        std::string pkey = pick_projectile_key(*centered_sh);
        auto bit = object_defs.find(pkey);
        double speed = 50.0; if (bit != object_defs.end() && bit->second.initial_velocity != 0.0) speed = bit->second.initial_velocity;
        double svx = (double)centered_sh->vx / (double)Object::FP_ONE; double svy = (double)centered_sh->vy / (double)Object::FP_ONE;
        double bvx = 0.0, bvy = 0.0;
        if (pkey == "laser") { bvx = speed * cs; bvy = speed * sn; }
        else if (bit != object_defs.end() && bit->second.additional_velocity != 0.0) { bvx = svx + bit->second.additional_velocity * cs; bvy = svy + bit->second.additional_velocity * sn; }
        else { bvx = svx + speed * cs; bvy = svy + speed * sn; }
        double ex = hit->object->x_pixels(); double ey = hit->object->y_pixels();
        double evx = (double)hit_sh->vx / (double)Object::FP_ONE; double evy = (double)hit_sh->vy / (double)Object::FP_ONE;
        double r0x = sx - ex, r0y = sy - ey; double vrelx = bvx - evx, vrely = bvy - evy; double vrel2 = vrelx*vrelx + vrely*vrely; double t = 0.0;
        if (vrel2 > 1e-9) { t = -(r0x*vrelx + r0y*vrely) / vrel2; if (t < 0.0) t = 0.0; }
        double etheta = hit_sh ? hit_sh->theta : 0.0; double ecs = std::cos(etheta), esn = std::sin(etheta);
        double p1x = ex + evx * t; double p1y = ey + evy * t; double a = (double)PHYS_ACCEL_PX_S2; double ax = a * ecs, ay = a * esn;
        double p2x = ex + evx * t + 0.5 * ax * t * t; double p2y = ey + evy * t + 0.5 * ay * t * t;
        fire_preview.active = true; fire_preview.theta = theta; fire_preview.t = t; fire_preview.sx = sx; fire_preview.sy = sy; fire_preview.ex = ex; fire_preview.ey = ey; fire_preview.edirx = ecs; fire_preview.ediry = esn; fire_preview.p1x = p1x; fire_preview.p1y = p1y; fire_preview.p2x = p2x; fire_preview.p2y = p2y;
    }

    bool handle_end_turn_click(int mx, int my) {
        DBG("Check END_TURN: rect x=%d y=%d w=%d h=%d animating=%d replay_ui=%d", btn_end_turn.x, btn_end_turn.y, btn_end_turn.w, btn_end_turn.h, (int)animating, (int)replay_ui_visible);
        if (click_inside(btn_end_turn, mx, my)) {
            if (replay_ui_visible) return true;
            if (!animating) {
                popup_suppressed = true; execute_commands(); animating = true; anim_frames_left = 20; anim_dt_per_frame = 1.0f / 20.0f; anim_accum = 0.0f; fire_preview.active = false; record.add("END_TURN"); DBG("END_TURN clicked -> animating start"); record.start_turn();
            }
            return true;
        }
        return false;
    }

    bool handle_fire_toggle_click(int mx, int my) {
        DBG("Check FIRE: rect x=%d y=%d w=%d h=%d", btn_fire.x, btn_fire.y, btn_fire.w, btn_fire.h);
        if (click_inside(btn_fire, mx, my)) {
            if (!animating && centered) { auto sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr; if (sh && !sh->dead && !sh->fired_this_turn) { armed_fire = !armed_fire; popup_suppressed = true; DBG("FIRE toggle -> %d", (int)armed_fire); } }
            return true;
        }
        return false;
    }

    bool handle_new_heading_toggle_click(int mx, int my) {
        DBG("Check NEWHDG: rect x=%d y=%d w=%d h=%d", btn_newhdg.x, btn_newhdg.y, btn_newhdg.w, btn_newhdg.h);
        if (click_inside(btn_newhdg, mx, my)) {
            if (!animating && centered) { auto sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr; if (sh && !sh->dead && sh->team == 0) { armed_newhdg = !armed_newhdg; if (armed_newhdg) armed_fire = false; popup_suppressed = true; DBG("NEWHDG toggle -> %d", (int)armed_newhdg); } }
            return true;
        }
        return false;
    }

    bool handle_accel_toggle_click(int mx, int my) {
        DBG("Check ACCEL: rect x=%d y=%d w=%d h=%d", btn_accel.x, btn_accel.y, btn_accel.w, btn_accel.h);
        if (click_inside(btn_accel, mx, my)) {
            if (!animating && centered) { auto sh = (centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr; if (sh && !sh->dead && sh->give_commands && sh->delta_v > 0.0) { int new_val = sh->throttle ? 0 : 1; Command c; c.type = Command::Type::THROTTLE; c.uid = centered->uid; c.ship = sh; c.a = (double)new_val; queue_command(c); std::ostringstream ss; ss.setf(std::ios::fixed); ss.precision(3); ss << "THROTTLE uid=" << centered->uid << " value=" << new_val; record.add(ss.str()); DBG("ACCEL toggled -> %d", new_val); popup_suppressed = true; } }
            return true;
        }
        return false;
    }

    bool handle_save_click(int mx, int my) {
        DBG("Check SAVE: rect x=%d y=%d w=%d h=%d", btn_save.x, btn_save.y, btn_save.w, btn_save.h);
        if (click_inside(btn_save, mx, my)) { popup_suppressed = true; save_current_state(); return true; }
        return false;
    }

    bool handle_next_ship_click(int mx, int my) {
        DBG("Check NEXT: rect x=%d y=%d w=%d h=%d", btn_next.x, btn_next.y, btn_next.w, btn_next.h);
        if (click_inside(btn_next, mx, my)) {
            if (!obj_sels.empty()) {
                popup_suppressed = true; size_t idx = 0; if (centered) { for (size_t i = 0; i < obj_sels.size(); ++i) if (obj_sels[i].get() == centered) { idx = i; break; } idx = (idx + 1) % obj_sels.size(); }
                centered = obj_sels[idx].get(); selected = centered; if (menu) menu->anchor = selected; DBG("NEXT clicked -> idx advanced");
            }
            return true;
        }
        return false;
    }

    bool handle_prev_ship_click(int mx, int my) {
        DBG("Check PREV: rect x=%d y=%d w=%d h=%d", btn_prev.x, btn_prev.y, btn_prev.w, btn_prev.h);
        if (click_inside(btn_prev, mx, my)) {
            if (!obj_sels.empty()) {
                popup_suppressed = true; size_t idx = 0; if (centered) { for (size_t i = 0; i < obj_sels.size(); ++i) if (obj_sels[i].get() == centered) { idx = i; break; } idx = (idx + obj_sels.size() - 1) % obj_sels.size(); } else { idx = (obj_sels.size() + obj_sels.size() - 1) % obj_sels.size(); }
                centered = obj_sels[idx].get(); selected = centered; if (menu) menu->anchor = selected; DBG("PREV clicked -> idx decremented");
            }
            return true;
        }
        return false;
    }

    bool handle_quit_single_click(int mx, int my) {
        DBG("Check QUIT: rect x=%d y=%d w=%d h=%d", btn_quit.x, btn_quit.y, btn_quit.w, btn_quit.h);
        if (click_inside(btn_quit, mx, my)) { DBG("QUIT clicked (single) -> exit"); popup_suppressed = true; running = false; return true; }
        return false;
    }

    // Aggregate HUD button hit-tests in priority order
    bool handle_hud_clicks(int mx, int my) {
        if (handle_end_turn_click(mx, my)) return true;
        if (handle_fire_toggle_click(mx, my)) return true;
        if (handle_new_heading_toggle_click(mx, my)) return true;
        if (handle_accel_toggle_click(mx, my)) return true;
        if (handle_save_click(mx, my)) return true;
        if (handle_next_ship_click(mx, my)) return true;
        if (handle_prev_ship_click(mx, my)) return true;
        if (handle_quit_single_click(mx, my)) return true;
        return false;
    }

    void handle_select_or_clear(int mx, int my) {
        if (!armed_fire && !armed_newhdg) popup_suppressed = false;
        ObjectSelectable* hit = nullptr; int best_d2 = 0, best_sx = 0, best_sy = 0;
        for (auto& s : obj_sels) {
            if (!s->hit(mx, my)) continue;
            int sx, sy; world_to_screen(cam, (float)s->object->x_pixels(), (float)s->object->y_pixels(), sx, sy);
            int dx = mx - sx; int dy = my - sy; int d2 = dx*dx + dy*dy;
            if (!hit || d2 < best_d2 || (d2 == best_d2 && (sy < best_sy || (sy == best_sy && sx < best_sx)))) { hit = s.get(); best_d2 = d2; best_sx = sx; best_sy = sy; }
        }
        if (hit) { for (auto& s : obj_sels) s->selected = (s.get() == hit); selected = hit; }
        else { for (auto& s : obj_sels) s->selected = false; selected = nullptr; }
        menu->anchor = selected;
    }

    void on_mouse_wheel(const SDL_MouseWheelEvent& wheel) {
        DBG("MOUSEWHEEL y=%d zoom(before)=%.6f", wheel.y, (double)cam.zoom);
        if (wheel.y != 0) {
            float delta = (float)wheel.y; float factor = std::exp(UI_ZOOM_LAMBDA_PER_STEP * delta); cam.zoom *= factor;
            if (cam.zoom < 1e-8f) cam.zoom = 1e-8f; if (cam.zoom > 1e8f) cam.zoom = 1e8f; DBG("zoom(after)=%.6f", (double)cam.zoom);
        }
    }

    void on_mouse_button_down(const SDL_MouseButtonEvent& btn) {
        DBG("MOUSEDOWN x=%d y=%d button=%d", btn.x, btn.y, (int)btn.button);
        if (handle_precheck_quit(btn.x, btn.y)) return;
        if (handle_back_button(btn.x, btn.y)) return;
        if (handle_main_menu_clicks(btn.x, btn.y)) return;
        if (mode == Mode::ARCADE) { (void)handle_arcade_clicks(btn.x, btn.y); return; }
        if (mode != Mode::SINGLE) return;
        if (handle_replay_controls_clicks(btn.x, btn.y)) return;
        if (handle_menu_panel_click(btn.x, btn.y)) return;
        if (handle_right_click_actions(btn)) return;
        if (btn.button == SDL_BUTTON_LEFT) { maybe_build_fire_preview_leftclick(btn.x, btn.y); }
        if (handle_hud_clicks(btn.x, btn.y)) return;
        handle_select_or_clear(btn.x, btn.y);
    }

    void handle_event(const SDL_Event& e) {
        DBG("event type=%d mode=%d", (int)e.type, (int)mode);
        switch (e.type) {
            case SDL_QUIT:
                DBG("SDL_QUIT received -> exit");
                running = false;
                break;
            case SDL_MOUSEBUTTONDOWN:
                on_mouse_button_down(e.button);
                break;
            case SDL_MOUSEWHEEL:
                on_mouse_wheel(e.wheel);
                break;
            default: break;
        }
    }

    void draw() {
        SDL_SetRenderDrawColor(renderer, 10, 12, 16, 255);
        SDL_RenderClear(renderer);

        if (mode == Mode::MENU || mode == Mode::HOST_WAIT || mode == Mode::CLIENT_SEARCH) {
            // Simple centered title
            SDL_SetRenderDrawColor(renderer, 235, 235, 245, 255);

            const char* title = GAME_TITLE;

            int tw = 6*3*(int)std::strlen(title);
            draw_text(renderer, (cam.screen_w - tw)/2, 80, title, 3);

            // Buttons
            int bw = 260, bh = 44;
            int x = (cam.screen_w - bw)/2;
            int y = 180;

            if (mode == Mode::MENU) {
                btn_menu_single = { x, y, bw, bh };
                btn_menu_host   = { x, y+bh+16, bw, bh };
                btn_menu_connect= { x, y+(bh+16)*2, bw, bh };
                btn_menu_watch  = { x, y+(bh+16)*3, bw, bh };
                btn_menu_arcade = { x, y+(bh+16)*4, bw, bh };
                SDL_SetRenderDrawColor(renderer, 35, 60, 90, 255); SDL_RenderFillRect(renderer, &btn_menu_single);
                SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255); SDL_RenderDrawRect(renderer, &btn_menu_single);
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255); draw_text(renderer, x+20, y+12, "SINGLE PLAYER", 2);

                SDL_SetRenderDrawColor(renderer, 35, 45, 60, 255); SDL_RenderFillRect(renderer, &btn_menu_host);
                SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255); SDL_RenderDrawRect(renderer, &btn_menu_host);
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255); draw_text(renderer, x+54, y+bh+16+12, "HOST GAME", 2);

                SDL_SetRenderDrawColor(renderer, 35, 45, 60, 255); SDL_RenderFillRect(renderer, &btn_menu_connect);
                SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255); SDL_RenderDrawRect(renderer, &btn_menu_connect);
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255); draw_text(renderer, x+72, y+(bh+16)*2+12, "CONNECT", 2);

                SDL_SetRenderDrawColor(renderer, 35, 45, 60, 255); SDL_RenderFillRect(renderer, &btn_menu_watch);
                SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255); SDL_RenderDrawRect(renderer, &btn_menu_watch);
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255); draw_text(renderer, x+36, y+(bh+16)*3+12, "WATCH RECORD", 2);
                SDL_SetRenderDrawColor(renderer, 35, 45, 60, 255); SDL_RenderFillRect(renderer, &btn_menu_arcade);
                SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255); SDL_RenderDrawRect(renderer, &btn_menu_arcade);
                SDL_SetRenderDrawColor(renderer, 230, 240, 255, 255); draw_text(renderer, x+52, y+(bh+16)*4+12, "ARCADE MODE", 2);
                btn_back = {0,0,0,0};
            } else if (mode == Mode::HOST_WAIT) {
                const char* msg = "Waiting for client to connect...";
                int mw = 6*2*(int)std::strlen(msg);
                SDL_SetRenderDrawColor(renderer, 235, 235, 245, 255);
                draw_text(renderer, (cam.screen_w - mw)/2, 180, msg, 2);
                btn_back = { x, 260, bw, bh };
                SDL_SetRenderDrawColor(renderer, 50, 40, 40, 255); SDL_RenderFillRect(renderer, &btn_back);
                SDL_SetRenderDrawColor(renderer, 200, 140, 140, 255); SDL_RenderDrawRect(renderer, &btn_back);
                SDL_SetRenderDrawColor(renderer, 245, 235, 235, 255); draw_text(renderer, x+96, 260+12, "BACK", 2);
            } else if (mode == Mode::CLIENT_SEARCH) {
                const char* msg = "Searching for host...";
                int mw = 6*2*(int)std::strlen(msg);
                SDL_SetRenderDrawColor(renderer, 235, 235, 245, 255);
                draw_text(renderer, (cam.screen_w - mw)/2, 180, msg, 2);
                btn_back = { x, 260, bw, bh };
                SDL_SetRenderDrawColor(renderer, 50, 40, 40, 255); SDL_RenderFillRect(renderer, &btn_back);
                SDL_SetRenderDrawColor(renderer, 200, 140, 140, 255); SDL_RenderDrawRect(renderer, &btn_back);
                SDL_SetRenderDrawColor(renderer, 245, 235, 235, 255); draw_text(renderer, x+96, 260+12, "BACK", 2);
            }
            SDL_RenderPresent(renderer);
            return;
        }

        // Game (single-player) drawing
        if (centered && centered->object) {
            cam.cx = (float)centered->object->x_pixels();
            cam.cy = (float)centered->object->y_pixels();
        }

        // Base pass: sprites
        for (auto& s : obj_sels) s->draw(renderer);
        // Overlay pass: bounding boxes always on top
        for (auto& s : obj_sels) s->draw_bbox(renderer);

        // FIRE preview overlays
        if (fire_preview.active && centered) {
            // Red ray from shooter along theta
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
            double L = 50000.0;
            double x1 = fire_preview.sx;
            double y1 = fire_preview.sy;
            double x2 = x1 + std::cos(fire_preview.theta) * L;
            double y2 = y1 + std::sin(fire_preview.theta) * L;
            int sx1, sy1, sx2, sy2; world_to_screen(cam, (float)x1, (float)y1, sx1, sy1); world_to_screen(cam, (float)x2, (float)y2, sx2, sy2);
            SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);

            // Enemy facing direction line
            SDL_SetRenderDrawColor(renderer, 180, 180, 255, 200);
            double ex = fire_preview.ex, ey = fire_preview.ey;
            double ex2 = ex + fire_preview.edirx * L;
            double ey2 = ey + fire_preview.ediry * L;
            int esx1, esy1, esx2, esy2; world_to_screen(cam, (float)ex, (float)ey, esx1, esy1); world_to_screen(cam, (float)ex2, (float)ey2, esx2, esy2);
            SDL_RenderDrawLine(renderer, esx1, esy1, esx2, esy2);

            // Markers for predicted enemy positions
            SDL_SetRenderDrawColor(renderer, 255, 220, 120, 255);
            int m1x, m1y; world_to_screen(cam, (float)fire_preview.p1x, (float)fire_preview.p1y, m1x, m1y);
            draw_circle_filled(renderer, m1x, m1y, 4);

            SDL_SetRenderDrawColor(renderer, 140, 255, 160, 255);
            int m2x, m2y; world_to_screen(cam, (float)fire_preview.p2x, (float)fire_preview.p2y, m2x, m2y);
            draw_circle_filled(renderer, m2x, m2y, 4);
        }
        // UI
        menu->draw(renderer);

        // Info panel for centered ship (top-left below clock)
        if (centered && centered->object) {
            double px = centered->object->x_pixels();
            double py = centered->object->y_pixels();
            double pvx = (double)centered->object->vx / (double)Object::FP_ONE;
            double pvy = (double)centered->object->vy / (double)Object::FP_ONE;
            double th = (double)centered->object->theta;

            double av = centered->object->ang_vel;
            double tgt = th;
            int throttle_val = 0;
            double dv_val = 0.0;
            if (centered->object->type == Object::SHIP) {
                if (auto sh = dynamic_cast<Ship*>(centered->object)) {
                    tgt = sh->target_theta;
                    throttle_val = sh->throttle;
                    dv_val = sh->delta_v;
                }
            }

            char line1[64], line2[64], line3[64], line4[32], line5[48], line6[48], line7[48];
            std::snprintf(line1, sizeof(line1), "X=%.3f Y=%.3f", px, py);
            std::snprintf(line2, sizeof(line2), "VX=%.3f VY=%.3f", pvx, pvy);
            auto norm_0_2pi = [](double a){ double t = 2.0 * M_PI; a = std::fmod(a, t); if (a < 0) a += t; return a; };
            double thn = norm_0_2pi(th);
            double tgtn = norm_0_2pi(tgt);
            std::snprintf(line3, sizeof(line3), "TH=%.3f", thn);
            std::snprintf(line5, sizeof(line5), "AV=%.3f rad/s", av);
            std::snprintf(line6, sizeof(line6), "TGT=%.3f rad", tgtn);
            std::snprintf(line4, sizeof(line4), "THR=%d", throttle_val);

            std::snprintf(line7, sizeof(line7), "DV=%.3f", dv_val);

            int panel_w = 320, panel_h = 160;
            SDL_Rect info { 10, 40, panel_w, panel_h };
            SDL_SetRenderDrawColor(renderer, 20, 24, 28, 220);
            SDL_RenderFillRect(renderer, &info);
            SDL_SetRenderDrawColor(renderer, 80, 170, 255, 255);
            SDL_RenderDrawRect(renderer, &info);

            SDL_SetRenderDrawColor(renderer, 235, 235, 235, 255);
            draw_text(renderer, info.x + 10, info.y + 10, line1, 2);
            draw_text(renderer, info.x + 10, info.y + 32, line2, 2);
            draw_text(renderer, info.x + 10, info.y + 54, line3, 2);
            draw_text(renderer, info.x + 10, info.y + 76, line4, 2);
            draw_text(renderer, info.x + 10, info.y + 98, line5, 2);
            draw_text(renderer, info.x + 10, info.y + 120, line6, 2);
            draw_text(renderer, info.x + 10, info.y + 142, line7, 2);
        }

        // Replay controls bar at top
        if (replay_ui_visible) {
            int bar_h = 36; int pad = 8; int bw = 120; int space = 10;
            SDL_Rect bar { 0, 0, cam.screen_w, bar_h };
            SDL_SetRenderDrawColor(renderer, 18, 18, 22, 230);
            SDL_RenderFillRect(renderer, &bar);
            // Layout buttons
            int x = pad; int y = 4;
            btn_replay_rew   = { x, y, bw, bar_h - 8 }; x += bw + space;
            btn_replay_play  = { x, y, bw, bar_h - 8 }; x += bw + space;
            btn_replay_pause = { x, y, bw, bar_h - 8 }; x += bw + space;
            btn_replay_step  = { x, y, bw, bar_h - 8 };
            // Draw buttons
            auto draw_btn = [&](const SDL_Rect& r, int cr, int cg, int cb, const char* label){
                SDL_SetRenderDrawColor(renderer, cr, cg, cb, 255); SDL_RenderFillRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 180, 200, 255, 255); SDL_RenderDrawRect(renderer, &r);
                SDL_SetRenderDrawColor(renderer, 235, 240, 245, 255); draw_text(renderer, r.x + 10, r.y + 10, label, 2);
            };
            draw_btn(btn_replay_rew, 50, 40, 60, "REWIND");
            draw_btn(btn_replay_play, replay_paused ? 40 : 25, replay_paused ? 80 : 120, 45, "PLAY");
            draw_btn(btn_replay_pause, replay_paused ? 90 : 45, 45, 35, "PAUSE");
            draw_btn(btn_replay_step, 45, 60, 90, "+1 FRAME");
        } else {
            btn_replay_rew = btn_replay_play = btn_replay_pause = btn_replay_step = SDL_Rect{0,0,0,0};
        }

        // HUD: clock at top center (adjusted under bar)
        char buf[32];
        int total_centis = (int)std::round(game_time * 100.0f);
        if (total_centis < 0) total_centis = 0;
        int cs = total_centis % 100; int total_secs = total_centis / 100;
        int s = total_secs % 60; int total_mins = total_secs / 60;
        int m = total_mins % 60; int h = (total_mins / 60) % 100; // wrap at 99
        std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%02d", h, m, s, cs);
        SDL_SetRenderDrawColor(renderer, 240, 240, 240, 255);
        int text_w = 6*2* (int)std::strlen(buf); // rough estimate; each char 6*scale wide
        int tx = (cam.screen_w - text_w)/2;
        int ty = replay_ui_visible ? 8 : 8; // still at top; bar behind
        draw_text(renderer, tx, ty, buf, 2);

        // Two strips at bottom: command strip and game strip
        int pad = 10;
        int bh = 36;
        int strip_gap = 6;
        // Command strip (bottom row)
        SDL_Rect command_strip { 0, cam.screen_h - (bh*1 + pad*2), cam.screen_w, bh + pad };
        auto set_fill = [&](const char* key, const char* state, int dr,int dg,int db,int da){
            auto it = button_colors.find(key);
            if (it != button_colors.end()) {
                auto jt = it->second.by_state.find(state);
                if (jt != it->second.by_state.end()) { SDL_SetRenderDrawColor(renderer, jt->second.r, jt->second.g, jt->second.b, jt->second.a); return; }
                auto ka = it->second.by_state.find("active");
                if (ka != it->second.by_state.end()) { SDL_SetRenderDrawColor(renderer, ka->second.r, ka->second.g, ka->second.b, ka->second.a); return; }
            }
            SDL_SetRenderDrawColor(renderer, dr,dg,db,da);
        };
        set_fill("command_strip_bg", "active", 22, 26, 28, 220);
        SDL_RenderFillRect(renderer, &command_strip);
        // Layout: New Heading, Accel (Fire moved to top-right)
        int x = pad; int y = command_strip.y + (command_strip.h - bh)/2;
        int bw_hdg = 140, bw_accel = 120, space = 10;
        auto centered_sh = (centered && centered->object && centered->object->type == Object::SHIP) ? dynamic_cast<Ship*>(centered->object) : nullptr;
        bool hdg_enabled = (!animating && centered_sh && !centered_sh->dead && centered_sh->team == 0 && centered_sh->give_commands);
        bool accel_enabled = hdg_enabled && centered_sh->delta_v > 0.0;
        // New Heading
        btn_newhdg = { x, y, bw_hdg, bh }; x += bw_hdg + space;
        if (!hdg_enabled) set_fill("new_heading", "disabled", 30, 40, 30, 200);
        else if (armed_newhdg) SDL_SetRenderDrawColor(renderer, 40, 90, 40, 255);
        else set_fill("new_heading", "active", 35, 60, 35, 255);
        SDL_RenderFillRect(renderer, &btn_newhdg);
        if (!hdg_enabled) SDL_SetRenderDrawColor(renderer, 90, 120, 90, 200);
        else if (armed_newhdg) SDL_SetRenderDrawColor(renderer, 160, 255, 160, 255);
        else SDL_SetRenderDrawColor(renderer, 120, 200, 120, 255);
        SDL_RenderDrawRect(renderer, &btn_newhdg);
        SDL_SetRenderDrawColor(renderer, 230, 240, 230, 255);
        draw_text(renderer, btn_newhdg.x + 6, btn_newhdg.y + 10, armed_newhdg ? "NEW HEADING*" : "NEW HEADING", 2);
        // Accel
        btn_accel = { x, y, bw_accel, bh };
        // Determine accel display: respect any queued THROTTLE command for this ship
        bool accel_on = (centered_sh && centered_sh->throttle != 0);
        if (centered) {
            for (const auto& c : command_stack) {
                if (c.type == Command::Type::THROTTLE && c.uid == centered->uid) { accel_on = (std::llround(c.a) != 0); break; }
            }
        }
        if (!accel_enabled) set_fill("accel", "disabled", 28, 34, 28, 200);
        else if (accel_on) SDL_SetRenderDrawColor(renderer, 40, 85, 55, 255);
        else set_fill("accel", "active", 35, 60, 45, 255);
        SDL_RenderFillRect(renderer, &btn_accel);
        if (!accel_enabled) SDL_SetRenderDrawColor(renderer, 80, 120, 90, 200);
        else if (accel_on) SDL_SetRenderDrawColor(renderer, 150, 255, 200, 255);
        else SDL_SetRenderDrawColor(renderer, 120, 255, 180, 255);
        SDL_RenderDrawRect(renderer, &btn_accel);
        SDL_SetRenderDrawColor(renderer, 220, 240, 230, 255);
        draw_text(renderer, btn_accel.x + 8, btn_accel.y + 10, (!accel_enabled ? "ACCEL N/A" : (accel_on ? "ACCEL ON" : "ACCEL OFF")), 2);

        // Game strip (row above)
        SDL_Rect game_strip { 0, command_strip.y - (bh + strip_gap), cam.screen_w, bh + pad };
        set_fill("game_strip_bg", "active", 24, 22, 28, 220);
        SDL_RenderFillRect(renderer, &game_strip);
        // Layout from right to left: Quit, End Turn, Save, Next, Prev
        int gx = game_strip.x + game_strip.w - pad;
        int gy = game_strip.y + (game_strip.h - bh)/2;
        int bw_end = 160, bw_quit = 100, bw_fire = 100, bw_save = 100, bw_lr = 140;
        // Quit (rightmost)
        gx -= bw_quit; btn_quit = { gx, gy, bw_quit, bh }; gx -= 10;
        set_fill("quit", "active", 50, 35, 35, 255);
        SDL_RenderFillRect(renderer, &btn_quit);
        SDL_SetRenderDrawColor(renderer, 200, 120, 120, 255);
        SDL_RenderDrawRect(renderer, &btn_quit);
        SDL_SetRenderDrawColor(renderer, 240, 230, 230, 255);
        draw_text(renderer, btn_quit.x + 14, btn_quit.y + 10, "QUIT", 2);
        // Fire (top-right area)
        bool fire_enabled = (!animating && centered_sh && !centered_sh->dead && centered_sh->team == 0 && centered_sh->give_commands && !centered_sh->fired_this_turn);
        gx -= bw_fire; btn_fire = { gx, gy, bw_fire, bh }; gx -= 10;
        if (!fire_enabled) set_fill("fire", "disabled", 40, 30, 30, 200);
        else if (armed_fire) SDL_SetRenderDrawColor(renderer, 90, 40, 40, 255);
        else set_fill("fire", "active", 60, 35, 35, 255);
        SDL_RenderFillRect(renderer, &btn_fire);
        if (!fire_enabled) SDL_SetRenderDrawColor(renderer, 120, 90, 90, 200);
        else if (armed_fire) SDL_SetRenderDrawColor(renderer, 255, 160, 160, 255);
        else SDL_SetRenderDrawColor(renderer, 200, 120, 120, 255);
        SDL_RenderDrawRect(renderer, &btn_fire);
        SDL_SetRenderDrawColor(renderer, 240, 230, 230, 255);
        draw_text(renderer, btn_fire.x + 18, btn_fire.y + 10, armed_fire ? "FIRE*" : "FIRE", 2);
        // End Turn
        gx -= bw_end; btn_end_turn = { gx, gy, bw_end, bh }; gx -= 10;
        set_fill("end_turn", "active", 35, 45, 60, 255);
        SDL_RenderFillRect(renderer, &btn_end_turn);
        SDL_SetRenderDrawColor(renderer, 120, 180, 255, 255);
        SDL_RenderDrawRect(renderer, &btn_end_turn);
        SDL_SetRenderDrawColor(renderer, 220, 230, 240, 255);
        draw_text(renderer, btn_end_turn.x + 10, btn_end_turn.y + 10, "END TURN", 2);
        // Save
        gx -= bw_save; btn_save = { gx, gy, bw_save, bh }; gx -= 10;
        set_fill("save", "active", 35, 35, 60, 255);
        SDL_RenderFillRect(renderer, &btn_save);
        SDL_SetRenderDrawColor(renderer, 120, 120, 255, 255);
        SDL_RenderDrawRect(renderer, &btn_save);
        SDL_SetRenderDrawColor(renderer, 230, 230, 245, 255);
        draw_text(renderer, btn_save.x + 18, btn_save.y + 10, "SAVE", 2);
        // Next
        gx -= bw_lr; btn_next = { gx, gy, bw_lr, bh }; gx -= 10;
        set_fill("next_ship", "active", 45, 35, 45, 255);
        SDL_RenderFillRect(renderer, &btn_next);
        SDL_SetRenderDrawColor(renderer, 200, 120, 200, 255);
        SDL_RenderDrawRect(renderer, &btn_next);
        SDL_SetRenderDrawColor(renderer, 245, 230, 245, 255);
        draw_text(renderer, btn_next.x + 8, btn_next.y + 10, "NEXT SHIP", 2);
        // Prev
        gx -= bw_lr; btn_prev = { gx, gy, bw_lr, bh };
        set_fill("previous_ship", "active", 45, 45, 35, 255);
        SDL_RenderFillRect(renderer, &btn_prev);
        SDL_SetRenderDrawColor(renderer, 200, 200, 120, 255);
        SDL_RenderDrawRect(renderer, &btn_prev);
        SDL_SetRenderDrawColor(renderer, 245, 245, 230, 255);
        draw_text(renderer, btn_prev.x + 2, btn_prev.y + 10, "PREVIOUS SHIP", 2);

        // Aiming guide when FIRE is armed: red line from shooter toward mouse
        if (mode == Mode::SINGLE && armed_fire && centered && !animating && centered->object) {
            int mx, my; SDL_GetMouseState(&mx, &my);
            float wx, wy; screen_to_world(cam, mx, my, wx, wy);
            double sx = centered->object->x_pixels();
            double sy = centered->object->y_pixels();
            double theta = std::atan2((double)wy - sy, (double)wx - sx);
            double L = 50000.0;
            double x2 = sx + std::cos(theta) * L;
            double y2 = sy + std::sin(theta) * L;
            int sx1, sy1, sx2, sy2; world_to_screen(cam, (float)sx, (float)sy, sx1, sy1); world_to_screen(cam, (float)x2, (float)y2, sx2, sy2);
            SDL_SetRenderDrawColor(renderer, 255, 60, 60, 255);
            SDL_RenderDrawLine(renderer, sx1, sy1, sx2, sy2);
        }

        SDL_RenderPresent(renderer);
    }

    void run_loop() {
        DBG("Game::run_loop enter");
        while (running) {
            SDL_Event e;
        while (SDL_PollEvent(&e)) handle_event(e);
            // Hot-plug controller events in Arcade mode
            if (mode == Mode::ARCADE) {
                SDL_Event ev;
                while (SDL_PollEvent(&ev)) {
                    if (ev.type == SDL_QUIT) running = false;
                    if (ev.type == SDL_CONTROLLERDEVICEADDED) { if (!arcade_ctrl) open_first_controller(); }
                    if (ev.type == SDL_CONTROLLERDEVICEREMOVED) {
                        if (arcade_ctrl) {
                            SDL_Joystick* js = SDL_GameControllerGetJoystick(arcade_ctrl);
                            if (js && SDL_JoystickInstanceID(js) == ev.cdevice.which) { SDL_GameControllerClose(arcade_ctrl); arcade_ctrl = nullptr; arcade_connected = false; }
                        }
                    }
                }
            }
            // Networking polling on non-game screens
            if (mode == Mode::HOST_WAIT) poll_host_accept();
            if (mode == Mode::CLIENT_SEARCH) poll_client_connect();
            // Animate turn progression (only in game)
            Uint32 now = SDL_GetTicks();
            float dt = (now - last_tick_ms) / 1000.0f;
            if (mode == Mode::ARCADE) {
                poll_arcade_input_and_apply(centered);
                static float accum = 0.0f;
                accum += dt;
                const float step = 1.0f / 20.0f;
                while (accum + 1e-6f >= step) { accum -= step; advance_arcade_frame(step); }
                last_tick_ms = now;
                draw();
                continue;
            }
            
            last_tick_ms = now;
            last_tick_ms = now;
            // Drive replay when idle and not paused
            if (mode == Mode::SINGLE && replay_active && !replay_paused) {
                drive_replay();
            }
            if (mode == Mode::SINGLE && animating) {
                if (!(replay_active && replay_paused)) {
                    anim_accum += dt;
                    while (animating && anim_accum + 1e-6f >= anim_dt_per_frame) {
                        anim_accum -= anim_dt_per_frame;
                        advance_one_frame();
                        if (!animating) break; // end-of-turn finished
                    }
                } else if (replay_single_step && animating) {
                    // Single-step one frame when paused
                    advance_one_frame();
                    replay_single_step = false;
                }
            }
            draw();
        }
    }

    void shutdown() {
        DBG("Game::shutdown");
        // Persist record to disk for replay/debugging
        (void)record.save_json("record.json");
        if (g_font_small) { TTF_CloseFont(g_font_small); g_font_small = nullptr; }
        if (g_font_medium) { TTF_CloseFont(g_font_medium); g_font_medium = nullptr; }
        if (g_font_large) { TTF_CloseFont(g_font_large); g_font_large = nullptr; }
        if (g_ttf_ready) { TTF_Quit(); g_ttf_ready = false; }
        stop_host();
        close_socket();
        menu.reset();
        obj_sels.clear();
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
        IMG_Quit();
        SDL_Quit();
    }
};

// Let's move this over to object.cpp, also, I don't see what it needs to be ObjectSelectable rather than Object


int main(int, char**) {
    // Load basic game config (title, window size)
    GameConfig cfg; std::string cfg_err;
    (void)load_game_config("config/game.json", cfg, &cfg_err);
    if (!cfg_err.empty()) std::fprintf(stderr, "[config] %s\n", cfg_err.c_str());
    set_global_game_config(cfg);
    Game game;
    if (!game.init(cfg.title.c_str(), iclamp(cfg.window_w, 320, 8192), iclamp(cfg.window_h, 240, 8192))) return 1;
    game.run_loop();
    game.shutdown();
    return 0;
}
