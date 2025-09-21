// TCP protocol helpers for the headless engine.
// Intentionally json-c free in the header to keep main free of JSON deps.

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>

// Forward declarations to avoid leaking engine internals and json headers
class Object;
class Ship;

namespace tcp_protocol {

enum class ClientMsgType { Unknown, Join, StateReq, Cmd, EndTurn };

struct ClientCmd {
    std::string name;    // "THROTTLE", "HEADING", "FIRE"
    uint64_t uid = 0;
    double value = 0.0;  // for THROTTLE
    double theta = 0.0;  // for HEADING/FIRE
};

struct ClientMsg {
    ClientMsgType type = ClientMsgType::Unknown;
    std::string scope;       // for state_req
    std::string defs_hash;   // for join
    ClientCmd cmd;           // for cmd
    double wait = 0.0;       // for end_turn (seconds)
    int team = -1;           // for join
};

// Build a JSON line (newline-terminated) describing current state.
// include_all: if true, include non-ship objects in an "objects" array.
std::string build_state_json(const std::map<uint64_t, Ship*>& uid_to_ship,
                             const std::vector<std::unique_ptr<Object>>& objs,
                             const std::string& defs_hash,
                             bool include_all);

// Build a small reply {"type": type, "msg": msg}\n
std::string build_reply(const char* type, const char* msg);

// Build a joined reply; if match_ptr is non-null, include {"match": <bool>}.
std::string build_joined_reply(const std::string& defs_hash, const bool* match_ptr);

// Parse a client JSON line into a typed message. Returns false on parse/validation error.
bool parse_client_message(const std::string& line, ClientMsg& out, std::string* err = nullptr);

// UI/client-side helpers -------------------------------------------------

// Builders for client->engine requests
std::string build_join(const char* name, const char* defs_hash_opt, int team);
std::string build_state_req(const char* scope);
std::string build_cmd(const char* cmd, uint64_t uid, double value_or_theta, bool is_theta);
std::string build_end_turn(double wait_seconds);

// Parsed object view used by UI when reading state messages
struct NetObjectView {
    std::string type;       // ship/planet/projectile/body
    std::string object_key; // def key
    uint64_t uid = 0;       // ships only
    int team = 0;
    int throttle = 0;       // ships only
    double x = 0, y = 0;
    double vx = 0, vy = 0;
    double theta = 0;
    double delta_v = 0;
    double acc = 0;
};

// Parse a single JSON line with type=="state"; fills objects and optional defs_hash.
bool parse_state_objects(const std::string& line, std::vector<NetObjectView>& out_objects, std::string* defs_hash_out = nullptr);

// Parse a single JSON line with type=="joined"; returns defs_hash and optional match flag if present.
bool parse_joined(const std::string& line, std::string* defs_hash_out, bool* has_match_out, bool* match_out);

} // namespace tcp_protocol
