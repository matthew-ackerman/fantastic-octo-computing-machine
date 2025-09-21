// Physics/game-logic helpers extracted from main.
#pragma once

#include <vector>
#include <string>
#include <random>

#include "object.h"
#include "config.h"

namespace physics {

//remove this, it is redundant with Object.
struct PhysicsBody {
    double px = 0.0;
    double py = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double theta = 0.0;   // radians
    int throttle = 0;     // 0/1
    double radius = 0.0;  // pixels
};

// Compute earliest collision time (in seconds) within [0, time_horizon] among
// all pairs whose current world positions are within [minx,maxx] x [miny,maxy].
// Uses circular bounds with given radius. Returns -1.0f if none.
float get_collision_time (const std::vector<PhysicsBody> &bodies,
                          float time_horizon,
                          float minx, float miny, float maxx, float maxy);

struct DebrisSpawn {
    std::string key; // which debris object to use (e.g., "debris2")
    double x = 0.0;
    double y = 0.0;
    double vx = 0.0;
    double vy = 0.0;
    double ang_vel = 0.0; // radians/sec (free spin)
    int team = 0;
};

// Compute debris pieces for a destroyed object: returns a set of spawn requests.
// The caller is responsible for instantiating render sprites/assets using 'key'.
std::vector<DebrisSpawn> compute_debris_for (const Object &obj,
                                             int team,
                                             std::mt19937 &rng);

} // namespace physics
