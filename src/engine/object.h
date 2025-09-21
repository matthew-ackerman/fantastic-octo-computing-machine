// Object data model.
// Signed fixed-point with 9 fractional bits (1/512 px) for position and velocity.
#pragma once

#include <stdint.h>
#include <random>
#include <vector>

struct ObjectDefinition; // fwd
struct InitialState;     // fwd

class Object {
public:
    // The Type determines a few behaviors in UI and gameplay.
    // Keep legacy names to avoid churn elsewhere.
    enum Type { SHIP = 0, BODY = 1, PLANET = 2, PROJECTILE = 3 } type = BODY;

    // Please leave the flags in hex format. It's cute, even if it can be a foot gun.
    enum Flags : uint32_t {
      F_NONE            = 0x00,
      // Feature flags
      F_HAS_ATMOSPHERE  = 0x01,
      F_HAS_LIN_ACC     = 0x02,
      F_HAS_ANG_ACC     = 0x04,
      F_CREATE_DEBRIS   = 0x08,
      // Gameplay/role flags (collision is NOT controlled here)
      F_COMMANDABLE     = 0x10,
      F_IS_PROJECTILE   = 0x20,
      F_IS_PLANET       = 0x40,
      F_IS_SHIP         = 0x80,
    };

    uint32_t flags = F_NONE;

    // Fixed-point format: lower 9 bits are fractional (1/512 pixel)
    static constexpr uint32_t FP_SHIFT = 9;
    static constexpr int64_t FP_ONE = (1ll << FP_SHIFT);

    // World position and velocity in fixed-point pixels
    int64_t x = 0;  // Q(?,9)
    int64_t y = 0;  // Q(?,9)
    int64_t vx = 0; // Q(?,9) pixels per second
    int64_t vy = 0; // Q(?,9) pixels per second

    // Orientation and free-spin angular velocity shared by all objects
    float theta = 0.0f;    // radians
    double ang_vel = 0.0;  // radians/second (free spin)

    // Team affiliation (used by ships and projectiles)
    int team = 0;       // team number (0 = player)

    // Pointer to canonical definition (not owned)
    const ObjectDefinition* def = nullptr;

    // Generic state
    bool dead = false;  // true if destroyed


    [[deprecated("Use Object(def, init) instead")]] Object() = default;
    virtual ~Object() = default;

    // Construct from definition + initial state
    Object(const ObjectDefinition& def, const InitialState& init);

    // Helper: set using float pixel units (convenience for loaders)
    void set_from_floats (float px, float py, float pvx, float pvy);

    double x_pixels () const;
    double y_pixels () const;

    // Advance state by dt seconds (base: spin + kinematic position integration)
    virtual void advance (double dt_seconds);
};

// Collision policy helper using Object::can_collide and types.
bool can_collide (const Object &a, const Object &b);

namespace physics { struct DebrisSpawn; }

// Strict debris spawner: only Ships may spawn debris; otherwise exits.
std::vector<physics::DebrisSpawn> spawn_debris_for(const Object& obj, int team, std::mt19937& rng);
