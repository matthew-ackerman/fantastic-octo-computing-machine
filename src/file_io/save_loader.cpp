// JSON-based save loader
#include "save_loader.h"
#include "our_debug.h"
#include "errors.h"
#include "engine/initial_state.h"
#include "json_interface.h"

#include <fstream>
#include <cstdio>

static bool parse_initial_state(const JsonView& v, InitialState& spec, std::string* err) {
    if (!v.is_object()) { if (err) *err = "each item must be an object"; return false; }
    // Strings
    (void)get_json_value(v, "image", &spec.image);
    (void)get_json_value(v, "object", &spec.object);
    // Floats + presence flags
    if (float f; get_json_value(v, "x", &f)) { spec.x = f; spec.has_x = true; }
    if (float f; get_json_value(v, "y", &f)) { spec.y = f; spec.has_y = true; }
    if (float f; get_json_value(v, "vx", &f)) { spec.vx = f; spec.has_vx = true; }
    if (float f; get_json_value(v, "vy", &f)) { spec.vy = f; spec.has_vy = true; }
    if (float f; get_json_value(v, "theta", &f)) { spec.theta = f; spec.has_theta = true; }
    // Int + bool optional
    (void)get_json_value(v, "team", &spec.team);
    if (bool b; get_json_value(v, "give_commands", &b)) { spec.has_give_commands = true; spec.give_commands = b; }
    if (float f; get_json_value(v, "ang_vel", &f)) { spec.has_ang_vel = true; spec.ang_vel = f; }
    if (float f; get_json_value(v, "target_theta", &f)) { spec.has_target_theta = true; spec.target_theta = f; }
    if (int i; get_json_value(v, "throttle", &i)) { spec.has_throttle = true; spec.throttle = i; }
    if (bool b; get_json_value(v, "dead", &b)) { spec.has_dead = true; spec.dead = b; }
    if (float f; get_json_value(v, "delta_v", &f)) { spec.has_delta_v = true; spec.delta_v = f; }
    // Basic validation
    if (!(spec.has_x && spec.has_y && spec.has_vx && spec.has_vy && spec.has_theta)) { if (err) *err = "Missing required kinematics (x,y,vx,vy,theta)"; return false; }
    return true;
}

bool
load_save_file (const char *path, std::vector<InitialState> &out, std::string *err)
{
    DBG("load_save_file(%s)", path ? path : "<null>");
    out.clear();
    JsonDoc doc = JsonDoc::from_file(path, err);
    if (!doc.valid()) { const char* msg = err ? err->c_str() : "Cannot open or parse save file"; CRASH(ExitCode::LOADING_ERROR, "%s: %s", msg, path ? path : "<null>"); }
    JsonView root(doc.get());
    if (!root.is_array()) { if (err) *err = "save file must be a JSON array"; CRASH(ExitCode::LOADING_ERROR, "%s", err?err->c_str():"save file must be a JSON array"); }

    size_t n = root.length();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        JsonView it = root.index(i);
        InitialState spec; std::string lerr;
        if (!parse_initial_state(it, spec, &lerr)) { if (err) *err = lerr; CRASH(ExitCode::LOADING_ERROR, "Invalid save entry at index %zu: %s", i, lerr.c_str()); }
        out.push_back(std::move(spec));
    }
    DBG("load_save_file parsed %zu specs", out.size());
    return true;
}
