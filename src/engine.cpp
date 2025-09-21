// Headless engine binary: load object defs + save, read commands on stdin, step simulation.

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "errors.h"
#include "object_def.h"
#include "file_io/object_loader.h"
#include "file_io/save_loader.h"
#include "file_io/scene_loader.h"
#include "file_io/config_loader.h"
#include "engine/object.h"
#include "engine/ship.h"
#include "engine/planet.h"
#include "engine/command.h"
#include "physics.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>

#include "stream_io/tcp_protocol.h"
#include "stream_io/server.h"
#include "file_io/hash_utils.h"
#include <set>

namespace engine_main {

static double g_min_time_step = 1.0/64.0;

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

struct World {
    std::map<std::string, ObjectDefinition> defs;
    std::vector<std::unique_ptr<Object>> objs;
    std::vector<Command> command_stack;
    std::mt19937 rng{std::random_device{}()};
    std::map<uint64_t, Ship*> uid_to_ship;
    uint64_t next_uid = 1;
    std::string defs_hash;
};

static void rebuild_uid_map_stable(World& w) {
    // Preserve existing UIDs for surviving Ship* pointers; assign new UIDs to newcomers.
    std::map<Ship*, uint64_t> old_by_ptr;
    for (const auto& kv : w.uid_to_ship) if (kv.second) old_by_ptr[kv.second] = kv.first;
    std::map<uint64_t, Ship*> new_map;
    for (auto& o : w.objs) {
        if (auto sh = dynamic_cast<Ship*>(o.get())) {
            auto it = old_by_ptr.find(sh);
            if (it != old_by_ptr.end()) {
                new_map[it->second] = sh;
            } else {
                new_map[w.next_uid++] = sh;
            }
        }
    }
    w.uid_to_ship.swap(new_map);
}

static Ship* find_ship(World& w, uint64_t uid) {
    auto it = w.uid_to_ship.find(uid); if (it == w.uid_to_ship.end()) return nullptr; return it->second;
}

static void remove_object_ptr(std::vector<std::unique_ptr<Object>>& objs, Object* p) {
    for (size_t i = 0; i < objs.size(); ++i) if (objs[i].get() == p) { objs.erase(objs.begin() + i); return; }
}

static void step_world(World& w, double dt) {
    for (auto& o : w.objs) o->advance(dt);
    // Projectile-ship collisions
    std::vector<Object*> rm_bullets; std::vector<Object*> rm_ships;
    for (size_t i = 0; i < w.objs.size(); ++i) {
        Object* oi = w.objs[i].get();
        if (!oi || oi->dead) continue;
        bool is_bullet_i = (oi->type == Object::PROJECTILE);
        for (size_t j = 0; j < w.objs.size(); ++j) {
            if (i == j) continue;
            Object* oj = w.objs[j].get();
            if (!oj || oj->dead) continue;
            bool is_ship_j = (oj->type == Object::SHIP);
            if (is_bullet_i && is_ship_j) {
                if (!can_collide(*oi, *oj)) continue;
                double dx = oi->x_pixels() - oj->x_pixels();
                double dy = oi->y_pixels() - oj->y_pixels();
                double R = oj->def ? oj->def->radius : 0.0;
                if (dx*dx + dy*dy <= R*R) {
                    rm_bullets.push_back(oi);
                    if (auto* sh = dynamic_cast<Ship*>(oj)) {
                        auto debris = ::spawn_debris_for(*sh, sh->team, w.rng);
                        for (const auto& d : debris) {
                            auto itdef = w.defs.find(d.key);
                            if (itdef != w.defs.end()) {
                                const auto& ddef = itdef->second;
                                InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                                if (ddef.type == "ship") w.objs.push_back(std::make_unique<Ship>(ddef, init));
                                else if (ddef.type == "planet") w.objs.push_back(std::make_unique<Planet>(ddef, init));
                                else w.objs.push_back(std::make_unique<Object>(ddef, init));
                            }
                        }
                    }
                    rm_ships.push_back(oj);
                    break;
                }
            }
        }
    }
    for (auto* p : rm_bullets) remove_object_ptr(w.objs, p);
    for (auto* p : rm_ships) remove_object_ptr(w.objs, p);
}

static void end_of_turn_cleanup(World& w) {
    // Ship-ship overlap -> both destroyed with debris
    std::vector<Object*> rm;
    for (size_t i = 0; i < w.objs.size(); ++i) {
        for (size_t j = i + 1; j < w.objs.size(); ++j) {
            auto* A = w.objs[i].get();
            auto* B = w.objs[j].get();
            if (!A || !B) continue;
            if (A->type != Object::SHIP || B->type != Object::SHIP) continue;
            if (!can_collide(*A, *B)) continue;
            double dx = A->x_pixels() - B->x_pixels();
            double dy = A->y_pixels() - B->y_pixels();
            double RA = A->def ? A->def->radius : 0.0;
            double RB = B->def ? B->def->radius : 0.0;
            double R = RA + RB;
            if (dx*dx + dy*dy <= R*R) {
                if (auto* AS = dynamic_cast<Ship*>(A)) {
                    auto debrisA = ::spawn_debris_for(*AS, AS->team, w.rng);
                    for (const auto& d : debrisA) {
                        auto itdef = w.defs.find(d.key);
                        if (itdef != w.defs.end()) {
                            const auto& ddef = itdef->second;
                            InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                            if (ddef.type == "ship") w.objs.push_back(std::make_unique<Ship>(ddef, init));
                            else if (ddef.type == "planet") w.objs.push_back(std::make_unique<Planet>(ddef, init));
                            else w.objs.push_back(std::make_unique<Object>(ddef, init));
                        }
                    }
                }
                if (auto* BS = dynamic_cast<Ship*>(B)) {
                    auto debrisB = ::spawn_debris_for(*BS, BS->team, w.rng);
                    for (const auto& d : debrisB) {
                        auto itdef = w.defs.find(d.key);
                        if (itdef != w.defs.end()) {
                            const auto& ddef = itdef->second;
                            InitialState init; init.object = d.key; init.x = (float)d.x; init.y = (float)d.y; init.vx = (float)d.vx; init.vy = (float)d.vy; init.team = d.team; init.has_x = true; init.has_y = true; init.has_vx = true; init.has_vy = true; { double ang = std::atan2(d.vy, d.vx); init.theta = (float)ang; init.has_theta = true; } init.has_give_commands = true; init.give_commands = false; init.has_ang_vel = true; init.ang_vel = (float)d.ang_vel;
                            if (ddef.type == "ship") w.objs.push_back(std::make_unique<Ship>(ddef, init));
                            else if (ddef.type == "planet") w.objs.push_back(std::make_unique<Planet>(ddef, init));
                            else w.objs.push_back(std::make_unique<Object>(ddef, init));
                        }
                    }
                }
                rm.push_back(A); rm.push_back(B);
            }
        }
    }
    for (auto* p : rm) remove_object_ptr(w.objs, p);
    // Reset per-turn states
    for (auto& o : w.objs) if (auto sh = dynamic_cast<Ship*>(o.get())) { sh->throttle = 0; sh->fired_this_turn = false; }
}

static void handle_command_line(World& w, const std::string& line) {
    if (line.empty()) return;
    if (line[0] == '#') return;
    if (line == "END_TURN") {
        apply_commands(w.command_stack, w.objs, w.defs);
        double min_dt = (g_min_time_step > 0.0 ? g_min_time_step : 1.0/64.0);
        int steps = (int)std::ceil(1.0 / min_dt);
        if (steps < 1) steps = 1;
        const double dt = 1.0 / (double)steps;
        for (int i = 0; i < steps; ++i) step_world(w, dt);
        end_of_turn_cleanup(w);
        rebuild_uid_map_stable(w);
        std::fprintf(stderr, "[engine] end turn; objs=%zu ships=%zu\n", w.objs.size(), w.uid_to_ship.size());
        return;
    }
    if (line.rfind("STATE", 0) == 0) {
        // Optional: STATE ALL
        if (line.find("ALL") != std::string::npos) {
            std::cout << "# OBJECTS" << std::endl;
            for (const auto& o : w.objs) {
                const char* t = (o->type == Object::SHIP ? "ship" : (o->type == Object::PLANET ? "planet" : (o->type == Object::PROJECTILE ? "projectile" : "body")));
                std::cout << "type=" << t
                          << " x=" << o->x_pixels()
                          << " y=" << o->y_pixels()
                          << " vx=" << (double)o->vx / (double)Object::FP_ONE
                          << " vy=" << (double)o->vy / (double)Object::FP_ONE
                          << " theta=" << o->theta
                          << " team=" << o->team
                          << std::endl;
            }
        } else {
            // Default: ships only
            std::cout << "# SHIPS" << std::endl;
            for (const auto& [uid, ship] : w.uid_to_ship) {
                std::cout << "uid=" << uid
                          << " x=" << ship->x_pixels()
                          << " y=" << ship->y_pixels()
                          << " vx=" << (double)ship->vx / (double)Object::FP_ONE
                          << " vy=" << (double)ship->vy / (double)Object::FP_ONE
                          << " theta=" << ship->theta
                          << " team=" << ship->team
                          << " throttle=" << ship->throttle
                          << std::endl;
            }
        }
        return;
    }
    if (line.rfind("THROTTLE", 0) == 0) {
        uint64_t uid=0; double v=0.0;
        if (!parse_kv_u64(line, "uid", uid)) { std::fprintf(stderr, "ERR missing uid in THROTTLE\n"); return; }
        if (!parse_kv_double(line, "value", v)) { std::fprintf(stderr, "ERR missing value in THROTTLE\n"); return; }
        Command c; c.type = Command::Type::THROTTLE; c.uid = uid; c.a = v;
        if (auto* s = find_ship(w, uid)) c.ship = s; else { std::fprintf(stderr, "ERR unknown uid=%llu\n", (unsigned long long)uid); return; }
        queue_command(c, w.command_stack); return;
    }
    if (line.rfind("HEADING", 0) == 0) {
        uint64_t uid=0; double th=0.0;
        if (!parse_kv_u64(line, "uid", uid)) { std::fprintf(stderr, "ERR missing uid in HEADING\n"); return; }
        if (!parse_kv_double(line, "theta", th)) { std::fprintf(stderr, "ERR missing theta in HEADING\n"); return; }
        Command c; c.type = Command::Type::HEADING; c.uid = uid; c.a = th;
        if (auto* s = find_ship(w, uid)) c.ship = s; else { std::fprintf(stderr, "ERR unknown uid=%llu\n", (unsigned long long)uid); return; }
        queue_command(c, w.command_stack); return;
    }
    if (line.rfind("FIRE", 0) == 0) {
        uint64_t uid=0; double th=0.0;
        if (!parse_kv_u64(line, "uid", uid)) { std::fprintf(stderr, "ERR missing uid in FIRE\n"); return; }
        if (!parse_kv_double(line, "theta", th)) { std::fprintf(stderr, "ERR missing theta in FIRE\n"); return; }
        Command c; c.type = Command::Type::FIRE; c.uid = uid; c.a = th;
        if (auto* s = find_ship(w, uid)) { c.ship = s; c.key = pick_projectile_key(*s); } else { std::fprintf(stderr, "ERR unknown uid=%llu\n", (unsigned long long)uid); return; }
        queue_command(c, w.command_stack); return;
    }
    std::fprintf(stderr, "ERR unknown command: %s\n", line.c_str());
}

static void print_ship_index(World& w) {
    std::cout << "# SHIPS" << std::endl;
    for (const auto& [uid, ship] : w.uid_to_ship) {
        std::cout << "uid=" << uid
                  << " x=" << ship->x_pixels()
                  << " y=" << ship->y_pixels()
                  << " theta=" << ship->theta
                  << " team=" << ship->team
                  << std::endl;
    }
}

// server moved to stream_io/server.{h,cpp}

} // namespace engine_main

int main(int argc, char** argv) {
    using namespace engine_main;
    if (argc < 3) {
        std::fprintf(stderr, "Usage: %s <objects.json> <save.json>\n", argv[0]);
        return LOADING_ERROR;
    }
    const char* objects_path = argv[1];
    const char* save_path = argv[2];

    World world;
    std::string err;
    if (!load_object_defs(objects_path, world.defs, &err)) {
        std::fprintf(stderr, "FATAL: failed to load object defs: %s\n", err.c_str());
        return LOADING_ERROR;
    }
    world.defs_hash = hash_file_fnv1a64(objects_path);
    err.clear();
    if (!load_scene_objects(save_path, world.defs, world.objs, &err)) {
        std::fprintf(stderr, "FATAL: failed to load save: %s\n", err.c_str());
        return LOADING_ERROR;
    }

    rebuild_uid_map_stable(world);
    std::fprintf(stderr, "[engine] loaded: objs=%zu ships=%zu\n", world.objs.size(), world.uid_to_ship.size());
    print_ship_index(world);

    // Load game.json to get network port and paths
    GameConfig cfg; std::string cfg_err; (void)load_game_config("config/game.json", cfg, &cfg_err);
    int port = cfg.net_port;
    g_min_time_step = (cfg.min_time_step > 0.0 ? cfg.min_time_step : 1.0/64.0);

    bool use_stdin = (argc >= 4 && std::string(argv[3]) == "--stdin");
    if (!use_stdin) {
        // Default: multi-client server mode
        ServerCallbacks cbs;
        cbs.step_world_dt = [&](double dt){ step_world(world, dt); };
        cbs.apply_queued_commands = [&](){ apply_commands(world.command_stack, world.objs, world.defs); };
        cbs.rebuild_uid_map = [&](){ rebuild_uid_map_stable(world); };
        cbs.end_of_turn_cleanup = [&](){ end_of_turn_cleanup(world); };
        cbs.find_ship_by_uid = [&](uint64_t uid){ return find_ship(world, uid); };
        cbs.build_state_json = [&](bool all){ return tcp_protocol::build_state_json(world.uid_to_ship, world.objs, world.defs_hash, all); };
        cbs.queue_command = [&](const Command& c){ queue_command(c, world.command_stack); };
        cbs.get_defs_hash = [&](){ return world.defs_hash; };
        cbs.get_required_teams = [&](){ std::vector<int> out; std::set<int> st; for (const auto& kv : world.uid_to_ship) if (kv.second) st.insert(kv.second->team); out.assign(st.begin(), st.end()); return out; };
        run_engine_server(port, g_min_time_step, cbs);
    } else {
        // Stdin mode for quick tests (e.g., cat engine_test.txt | ./main_engine ... --stdin)
        std::string line;
        while (std::getline(std::cin, line)) handle_command_line(world, line);
    }
    return 0;
}
