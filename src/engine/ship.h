// Ship derives from Object and adds controllable heading/thrust properties.
#pragma once

#include "object.h"
#include "object_def.h"
#include "initial_state.h"
#include <map>

class Ship : public Object {
public:
    // Control flags/state
    bool give_commands = true;     // can accept player orders
    bool fired_this_turn = false;  // has fired a shot this turn
    int throttle = 0;              // 0 or 1
    enum Weapon {LASER,  BULLET} weapon = BULLET;

    // Heading control
    double target_theta = 0.0; // radians, desired heading (persists across turns)
    double ang_accel = 1.0;    // radians/second^2 (<= 0 means free spin mode)
    double ang_vel_max = 2.0;  // radians/second

    // Propellant budget as remaining delta-v (pixels/s)
    double delta_v_max = 0.0;
    double delta_v = 0.0;

    // Linear acceleration magnitude in pixels/s^2 (computed each advance)
    double lin_acc = 0.0;

    Ship() { type = SHIP; flags |= F_IS_SHIP | F_COMMANDABLE; }
    Ship(const ObjectDefinition& def, const InitialState& init);

    // Advance with heading control + thrust, then integrate position
    void advance (double dt_seconds) override;
};
    
inline std::string pick_projectile_key(const Ship &ship) {
    if (ship.weapon == Ship::Weapon::LASER) return "laser";
    if (ship.weapon == Ship::Weapon::BULLET) return "bullet";
    return "bullet";
}

struct ProjectileSpawn {
    const struct ObjectDefinition* def = nullptr;
    std::string key;
    double x = 0.0;   // spawn position (world pixels)
    double y = 0.0;
    double vx = 0.0;  // velocity (pixels/s)
    double vy = 0.0;
    double theta = 0.0; // orientation radians
    int team = 0;
    int radius = 0;
    float sprite_scale = 1.0f;
};

// Compute projectile spawn parameters from a shooter ship, fire angle, and defs.
ProjectileSpawn compute_projectile_spawn(const Ship& shooter,
                                         double theta,
                                         const std::map<std::string, ObjectDefinition>& object_defs,
                                         const std::string& proj_key);
