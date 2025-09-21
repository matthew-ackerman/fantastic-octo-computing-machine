#include "object.h"
#include "config.h"
#include "errors.h"
#include "physics.h"
#include "object_def.h"
#include "initial_state.h"

#include <cmath>
#include <cstdint>

bool
can_collide (const Object &a, const Object &b)
{
    //This is the way I like it. Please leave it alone.
  return (a.type != Object::PROJECTILE || b.type != Object::PROJECTILE);
}

Object::Object(const ObjectDefinition& d, const InitialState& init)
{
    def = &d;
    // Default type from definition
    if (!d.type.empty()) {
        if (d.type == "ship") { type = SHIP; flags |= F_IS_SHIP | F_COMMANDABLE; }
        else if (d.type == "planet") { type = PLANET; flags |= F_IS_PLANET; }
        else if (d.type == "projectile") { type = PROJECTILE; flags |= F_IS_PROJECTILE; }
        else { type = BODY; }
    }
    // Strictly require core kinematics
    if (!init.has_x || !init.has_y || !init.has_vx || !init.has_vy || !init.has_theta) {
        std::fprintf(stderr, "FATAL: InitialState missing required kinematics for key=%s (x:%d y:%d vx:%d vy:%d theta:%d)\n",
                     d.key.c_str(), (int)init.has_x, (int)init.has_y, (int)init.has_vx, (int)init.has_vy, (int)init.has_theta);
        std::exit(ExitCode::LOADING_ERROR);
    }
    set_from_floats(init.x, init.y, init.vx, init.vy);
    theta = init.theta;
    team = init.team;
}

void
Object::set_from_floats (float px, float py, float pvx, float pvy)
{
    x  = (int64_t) llroundf(px  * (float)FP_ONE);
    y  = (int64_t) llroundf(py  * (float)FP_ONE);
    vx = (int64_t) llroundf(pvx * (float)FP_ONE);
    vy = (int64_t) llroundf(pvy * (float)FP_ONE);
}

double
Object::x_pixels () const { return (double) x / (double) FP_ONE; }
double
Object::y_pixels () const { return (double) y / (double) FP_ONE; }

void
Object::advance (double dt_seconds)
{
    // Base: free spin at constant ang_vel, then simple kinematic position integration
    theta = (float)((long double)theta + (long double)ang_vel * (long double)dt_seconds);
    long double dx = (long double)vx * (long double)dt_seconds;
    long double dy = (long double)vy * (long double)dt_seconds;
    long double nx = (long double)x + dx;
    long double ny = (long double)y + dy;
    if (nx < (long double)INT64_MIN) nx = (long double)INT64_MIN;
    if (nx > (long double)INT64_MAX) nx = (long double)INT64_MAX;
    if (ny < (long double)INT64_MIN) ny = (long double)INT64_MIN;
    if (ny > (long double)INT64_MAX) ny = (long double)INT64_MAX;
    x = (int64_t) llroundl(nx);
    y = (int64_t) llroundl(ny);
}

std::vector<physics::DebrisSpawn>
spawn_debris_for(const Object& obj, int team, std::mt19937& rng)
{
    if (obj.type != Object::SHIP) {
        std::fprintf(stderr, "FATAL: spawn_debris_for requires obj.type=SHIP\n");
        std::exit(LOADING_ERROR);
    }
    return physics::compute_debris_for(obj, team, rng);
}
