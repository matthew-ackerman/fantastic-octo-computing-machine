// Implementation depends on json-c, but only in this translation unit.

#include "stream_io/tcp_protocol.h"

#include "file_io/json_interface.h" // JsonDoc, JsonView helpers

#include "engine/object.h"
#include "engine/ship.h"

#include <json-c/json.h>

using std::string;

namespace tcp_protocol {

static inline string json_stringify_and_nl(json_object* o){
    const char* s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    string line = s; line.push_back('\n');
    return line;
}

std::string build_state_json(const std::map<uint64_t, Ship*>& uid_to_ship,
                             const std::vector<std::unique_ptr<Object>>& objs,
                             const std::string& defs_hash,
                             bool include_all)
{
    json_object* root = json_object_new_object();
    json_object_object_add(root, "type", json_object_new_string("state"));
    if (!defs_hash.empty()) json_object_object_add(root, "defs_hash", json_object_new_string(defs_hash.c_str()));

    json_object* ships = json_object_new_array();
    for (const auto& kv : uid_to_ship) {
        uint64_t uid = kv.first; const Ship* s = kv.second;
        if (!s) continue;
        json_object* js = json_object_new_object();
        json_object_object_add(js, "uid", json_object_new_int64((long long)uid));
        json_object_object_add(js, "x", json_object_new_double(s->x_pixels()));
        json_object_object_add(js, "y", json_object_new_double(s->y_pixels()));
        json_object_object_add(js, "vx", json_object_new_double((double)s->vx / (double)Object::FP_ONE));
        json_object_object_add(js, "vy", json_object_new_double((double)s->vy / (double)Object::FP_ONE));
        json_object_object_add(js, "theta", json_object_new_double(s->theta));
        json_object_object_add(js, "team", json_object_new_int(s->team));
        json_object_object_add(js, "throttle", json_object_new_int(s->throttle));
        json_object_object_add(js, "delta_v", json_object_new_double(s->delta_v));
        json_object_object_add(js, "acc", json_object_new_double(s->lin_acc));
        if (s->def && !s->def->key.empty()) json_object_object_add(js, "object", json_object_new_string(s->def->key.c_str()));
        json_object_array_add(ships, js);
    }
    json_object_object_add(root, "ships", ships);

    if (include_all) {
        json_object* arr = json_object_new_array();
        for (const auto& up : objs) {
            const Object* o = up.get(); if (!o) continue;
            json_object* jo = json_object_new_object();
            const char* t = (o->type == Object::SHIP ? "ship" : (o->type == Object::PLANET ? "planet" : (o->type == Object::PROJECTILE ? "projectile" : "body")));
            json_object_object_add(jo, "type", json_object_new_string(t));
            json_object_object_add(jo, "x", json_object_new_double(o->x_pixels()));
            json_object_object_add(jo, "y", json_object_new_double(o->y_pixels()));
            json_object_object_add(jo, "vx", json_object_new_double((double)o->vx / (double)Object::FP_ONE));
            json_object_object_add(jo, "vy", json_object_new_double((double)o->vy / (double)Object::FP_ONE));
            json_object_object_add(jo, "theta", json_object_new_double(o->theta));
            json_object_object_add(jo, "team", json_object_new_int(o->team));
            if (o->def && !o->def->key.empty()) json_object_object_add(jo, "object", json_object_new_string(o->def->key.c_str()));
            json_object_array_add(arr, jo);
        }
        json_object_object_add(root, "objects", arr);
    }

    string out = json_stringify_and_nl(root);
    json_object_put(root);
    return out;
}

std::string build_reply(const char* type, const char* msg)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string(type ? type : ""));
    if (msg) json_object_object_add(o, "msg", json_object_new_string(msg));
    string out = json_stringify_and_nl(o);
    json_object_put(o);
    return out;
}

std::string build_joined_reply(const std::string& defs_hash, const bool* match_ptr)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("joined"));
    json_object_object_add(o, "msg", json_object_new_string("ok"));
    if (!defs_hash.empty()) json_object_object_add(o, "defs_hash", json_object_new_string(defs_hash.c_str()));
    if (match_ptr) json_object_object_add(o, "match", json_object_new_boolean(*match_ptr));
    string out = json_stringify_and_nl(o);
    json_object_put(o);
    return out;
}

bool parse_client_message(const std::string& line, ClientMsg& out, std::string* err)
{
    out = ClientMsg{};
    JsonDoc doc(json_tokener_parse(line.c_str()));
    if (!doc.valid()) { if (err) *err = "bad json"; return false; }
    JsonView root(doc.get());
    if (!root.is_object()) { if (err) *err = "bad json"; return false; }

    std::string type;
    if (!root.get_string("type", type)) { if (err) *err = "missing type"; return false; }

    if (type == "join") {
        out.type = ClientMsgType::Join;
        (void)root.get_string("defs_hash", out.defs_hash);
        JsonView t; if (root.get_view("team", t)) out.team = json_object_get_int(t.p);
        return true;
    }
    if (type == "state_req") {
        out.type = ClientMsgType::StateReq;
        (void)root.get_string("scope", out.scope);
        return true;
    }
    if (type == "cmd") {
        out.type = ClientMsgType::Cmd;
        if (!root.get_string("cmd", out.cmd.name)) { if (err) *err = "missing cmd"; return false; }
        // uid can be int or double in prior protocol; always read as double then cast.
        JsonView vuid; if (!root.get_view("uid", vuid)) { if (err) *err = "missing uid"; return false; }
        out.cmd.uid = (uint64_t)json_object_get_double(vuid.p);
        if (out.cmd.name == "THROTTLE") {
            if (!root.get_view("value", vuid)) { if (err) *err = "missing value"; return false; }
            out.cmd.value = json_object_get_double(vuid.p);
        } else if (out.cmd.name == "HEADING" || out.cmd.name == "FIRE") {
            if (!root.get_view("theta", vuid)) { if (err) *err = "missing theta"; return false; }
            out.cmd.theta = json_object_get_double(vuid.p);
        } else {
            if (err) *err = "unknown cmd";
            return false;
        }
        return true;
    }
    if (type == "end_turn") {
        out.type = ClientMsgType::EndTurn;
        JsonView v; if (root.get_view("wait", v)) out.wait = json_object_get_double(v.p);
        return true;
    }

    out.type = ClientMsgType::Unknown;
    if (err) *err = "unknown type";
    return false;
}

// -------------------- Client-side builders --------------------

std::string build_join(const char* name, const char* defs_hash_opt, int team)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("join"));
    if (name) json_object_object_add(o, "name", json_object_new_string(name));
    if (defs_hash_opt && defs_hash_opt[0]) json_object_object_add(o, "defs_hash", json_object_new_string(defs_hash_opt));
    json_object_object_add(o, "team", json_object_new_int(team));
    string out = json_stringify_and_nl(o); json_object_put(o); return out;
}

std::string build_state_req(const char* scope)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("state_req"));
    if (scope) json_object_object_add(o, "scope", json_object_new_string(scope));
    string out = json_stringify_and_nl(o); json_object_put(o); return out;
}

std::string build_cmd(const char* cmd, uint64_t uid, double value_or_theta, bool is_theta)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("cmd"));
    if (cmd) json_object_object_add(o, "cmd", json_object_new_string(cmd));
    json_object_object_add(o, "uid", json_object_new_int64((long long)uid));
    if (is_theta) json_object_object_add(o, "theta", json_object_new_double(value_or_theta));
    else json_object_object_add(o, "value", json_object_new_double(value_or_theta));
    string out = json_stringify_and_nl(o); json_object_put(o); return out;
}

std::string build_end_turn(double wait_seconds)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "type", json_object_new_string("end_turn"));
    json_object_object_add(o, "wait", json_object_new_double(wait_seconds));
    string out = json_stringify_and_nl(o); json_object_put(o); return out;
}

bool parse_state_objects(const std::string& line, std::vector<NetObjectView>& out_objects, std::string* defs_hash_out)
{
    out_objects.clear(); if (defs_hash_out) defs_hash_out->clear();
    JsonDoc doc(json_tokener_parse(line.c_str())); if (!doc.valid()) return false; JsonView root(doc.get()); if (!root.is_object()) return false;
    std::string type; if (!root.get_string("type", type)) return false; if (type != "state") return false;
    if (defs_hash_out) (void)root.get_string("defs_hash", *defs_hash_out);

    auto parse_items = [&](json_object* arr, const char* force_type){
        if (!arr || !json_object_is_type(arr, json_type_array)) return;
        size_t n = json_object_array_length(arr);
        out_objects.reserve(out_objects.size() + n);
        for (size_t i = 0; i < n; ++i) {
            json_object* it = json_object_array_get_idx(arr, i);
            if (!it || !json_object_is_type(it, json_type_object)) continue;
            NetObjectView o{}; json_object* v=nullptr;
            if (force_type) o.type = force_type; else if (json_object_object_get_ex(it, "type", &v) && json_object_is_type(v, json_type_string)) o.type = json_object_get_string(v); else o.type = "ship";
            if (json_object_object_get_ex(it, "object", &v) && json_object_is_type(v, json_type_string)) o.object_key = json_object_get_string(v);
            if (json_object_object_get_ex(it, "uid", &v)) o.uid = (uint64_t)json_object_get_int64(v);
            if (json_object_object_get_ex(it, "team", &v)) o.team = json_object_get_int(v);
            if (json_object_object_get_ex(it, "throttle", &v)) o.throttle = json_object_get_int(v);
            if (json_object_object_get_ex(it, "delta_v", &v)) o.delta_v = json_object_get_double(v);
            if (json_object_object_get_ex(it, "acc", &v)) o.acc = json_object_get_double(v);
            if (json_object_object_get_ex(it, "x", &v)) o.x = json_object_get_double(v);
            if (json_object_object_get_ex(it, "y", &v)) o.y = json_object_get_double(v);
            if (json_object_object_get_ex(it, "vx", &v)) o.vx = json_object_get_double(v);
            if (json_object_object_get_ex(it, "vy", &v)) o.vy = json_object_get_double(v);
            if (json_object_object_get_ex(it, "theta", &v)) o.theta = json_object_get_double(v);
            out_objects.push_back(std::move(o));
        }
    };

    json_object* jships=nullptr; json_object* jobjs=nullptr;
    if (json_object_object_get_ex(root.p, "ships", &jships)) parse_items(jships, "ship");
    if (json_object_object_get_ex(root.p, "objects", &jobjs)) parse_items(jobjs, nullptr);
    return true;
}

bool parse_joined(const std::string& line, std::string* defs_hash_out, bool* has_match_out, bool* match_out)
{
    if (defs_hash_out) defs_hash_out->clear();
    if (has_match_out) *has_match_out=false;
    if (match_out) *match_out=false;
    JsonDoc doc(json_tokener_parse(line.c_str())); if (!doc.valid()) return false; JsonView root(doc.get()); if (!root.is_object()) return false;
    std::string type; if (!root.get_string("type", type)) return false; if (type != "joined") return false;
    if (defs_hash_out) (void)root.get_string("defs_hash", *defs_hash_out);
    JsonView v; if (root.get_view("match", v)) { if (has_match_out) *has_match_out=true; if (match_out) *match_out = json_object_get_boolean(v.p); }
    return true;
}

} // namespace tcp_protocol
