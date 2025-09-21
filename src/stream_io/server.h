// Engine server for multi-client TCP control + state streaming
#pragma once

#include <cstdint>

class Ship;   // forward declare

#include <functional>
#include <string>
#include <vector>

struct ServerCallbacks {
    std::function<void(double)> step_world_dt;    // step simulation by dt
    std::function<void()> apply_queued_commands;  // apply queued commands to world
    std::function<void(const struct Command&)> queue_command; // enqueue command
    std::function<void()> rebuild_uid_map;        // rebuild uid_to_ship
    std::function<void()> end_of_turn_cleanup;    // end-of-turn cleanup
    std::function<Ship*(uint64_t)> find_ship_by_uid; // resolve Ship* for a UID
    std::function<std::string(bool)> build_state_json; // build state json line, include_all
    std::function<std::string()> get_defs_hash;   // return current defs hash
    std::function<std::vector<int>()> get_required_teams; // list of required teams
};

// Runs a TCP server on loopback at the given port, stepping the simulation
// with min_time_step seconds while no client has a turn due, and pausing
// stepping as soon as any client reaches its next_turn_time. A client ends
// its turn by sending an end_turn message with a wait (seconds) until their
// next turn is due again.
// Blocks until the server socket is closed or a fatal error occurs.
void run_engine_server(int port, double min_time_step, const ServerCallbacks& cb);
