#include "ship.h"
#include "config.h"
#include "object_def.h"
#include "initial_state.h"

#include <cmath>
#include <map>
#include <cstdint>

#include "our_debug.h"
#include "errors.h"

void
Ship::advance (double dt_seconds)
{
    // Update angular state: steer toward target if ang_accel > 0, else free spin
    if (ang_accel <= 0.0) {
        theta = (float)((long double)theta + (long double)ang_vel * (long double)dt_seconds);
    } else {
        auto norm_pi = [] (long double a) {
            while (a >  M_PI) a -= 2.0L*M_PI;
            while (a < -M_PI) a += 2.0L*M_PI;
            return a;
        };
        long double err = norm_pi((long double)target_theta - (long double)theta);
        if (fabsl(err) < 1e-6L) {
            ang_vel = 0.0;
            theta = (float)target_theta;
        } else {
            long double av = (long double)ang_vel;
            long double amax = (long double)ang_accel;
            long double stop_dist = (fabsl(av)*fabsl(av)) / (2.0L*amax);
            long double acc = 0.0L;
            if (stop_dist >= fabsl(err)) {
                acc = (av > 0.0L ? -amax : (av < 0.0L ? amax : (err > 0.0L ? amax : -amax)));
            } else {
                acc = (err > 0.0L) ? amax : -amax;
            }
            av += acc * (long double)dt_seconds;
            long double vmax = (long double)ang_vel_max;
            if (av >  vmax) av =  vmax;
            if (av < -vmax) av = -vmax;
            long double th = (long double)theta + av * (long double)dt_seconds;
            long double new_err = norm_pi((long double)target_theta - th);
            if ((err > 0 && new_err < 0) || (err < 0 && new_err > 0)) { th = (long double)target_theta; av = 0.0L; }
            theta = (float)th;
            ang_vel = (double)av;
        }
    }

    // Apply thrust: dv = a * dt using theta (snapshot at turn start)
    long double ax = 0.0L, ay = 0.0L;
    if (throttle) {
        long double a = (long double)PHYS_ACCEL_PX_S2; // pixels/s^2
        long double need_dv = a * (long double)dt_seconds; // pixels/s
        long double frac = 0.0L;
        if (need_dv > 0.0L && delta_v > 0.0) {
            long double avail = (long double)delta_v;
            frac = (avail >= need_dv) ? 1.0L : (avail / need_dv);
        }
        if (frac > 0.0L) {
            ax = (a * frac) * std::cos((long double)theta);
            ay = (a * frac) * std::sin((long double)theta);
            delta_v -= (double)(need_dv * frac);
            if (delta_v < 0.0) delta_v = 0.0;
        }
        long double dvx = ax * (long double)dt_seconds * (long double)FP_ONE;
        long double dvy = ay * (long double)dt_seconds * (long double)FP_ONE;
        long double nvx = (long double)vx + dvx;
        long double nvy = (long double)vy + dvy;
        if (nvx < (long double)INT64_MIN) nvx = (long double)INT64_MIN;
        if (nvx > (long double)INT64_MAX) nvx = (long double)INT64_MAX;
        if (nvy < (long double)INT64_MIN) nvy = (long double)INT64_MIN;
        if (nvy > (long double)INT64_MAX) nvy = (long double)INT64_MAX;
        vx = (int64_t) llroundl(nvx);
        vy = (int64_t) llroundl(nvy);
    }

    // Record linear acceleration magnitude (pixels/s^2)
    lin_acc = std::sqrt((double)(ax*ax + ay*ay));

    // Integrate position with constant acceleration: x += vx*dt + 0.5*a*dt^2
    long double dx = (long double)vx * (long double)dt_seconds;
    long double dy = (long double)vy * (long double)dt_seconds;
    long double dp_x = 0.5L * ax * (long double)dt_seconds * (long double)dt_seconds * (long double)FP_ONE;
    long double dp_y = 0.5L * ay * (long double)dt_seconds * (long double)dt_seconds * (long double)FP_ONE;
    long double nx = (long double)x + dx + dp_x;
    long double ny = (long double)y + dy + dp_y;
    if (nx < (long double)INT64_MIN) nx = (long double)INT64_MIN;
    if (nx > (long double)INT64_MAX) nx = (long double)INT64_MAX;
    if (ny < (long double)INT64_MIN) ny = (long double)INT64_MIN;
    if (ny > (long double)INT64_MAX) ny = (long double)INT64_MAX;
    x = (int64_t) llroundl(nx);
    y = (int64_t) llroundl(ny);
}

Ship::Ship(const ObjectDefinition& def, const InitialState& init)
  : Object(def, init)
{
    if (def.type != "ship") {
        std::fprintf(stderr, "FATAL: Ship constructed with non-ship definition key=%s type=%s\n", def.key.c_str(), def.type.c_str());
        std::exit(ExitCode::LOADING_ERROR);
    }
    type = SHIP; flags |= F_IS_SHIP | F_COMMANDABLE;

    // Defaults from definition
    give_commands = def.give_commands;
    ang_accel = def.ang_accel;
    ang_vel_max = def.ang_vel_max;
    delta_v_max = def.delta_v;

    // Required initial parameters (crumb enforcement)
    if (!init.has_ang_vel) { std::fprintf(stderr, "FATAL: InitialState missing ang_vel for Ship\n"); std::exit(ExitCode::LOADING_ERROR); }

    ang_vel = init.ang_vel;
    if (!init.has_delta_v) delta_v=delta_v_max;
    else delta_v = init.delta_v;
    theta = init.theta;
    target_theta = theta;

    // Overrides from initial state
    if (init.has_give_commands) give_commands = init.give_commands;
    if (init.has_target_theta) target_theta = init.target_theta;
    if (init.has_throttle) throttle = init.throttle;
    if (init.has_dead) dead = init.dead;
}


//  ??

ProjectileSpawn
compute_projectile_spawn(const Ship& shooter,
                         double theta,
                         const std::map<std::string, ObjectDefinition>& object_defs,
                         const std::string& proj_key)
{
    ProjectileSpawn out{};
    // Resolve projectile definition and parameters
    auto objIt = object_defs.find(proj_key);
    out.def = (objIt != object_defs.end()) ? &objIt->second : nullptr;
    out.key = proj_key;
    double svx = (double)shooter.vx / (double)Object::FP_ONE;
    double svy = (double)shooter.vy / (double)Object::FP_ONE;
    double cs = std::cos(theta), sn = std::sin(theta);
    double bvx = 0.0, bvy = 0.0;
    double speed = 50.0;
    if (out.def && out.def->initial_velocity != 0.0) speed = out.def->initial_velocity;
    bool is_laser = (proj_key == "laser");
    if (!is_laser && out.def && out.def->additional_velocity != 0.0) {
        bvx = svx + out.def->additional_velocity * cs;
        bvy = svy + out.def->additional_velocity * sn;
    } else if (is_laser) {
        bvx = speed * cs; bvy = speed * sn;
    } else {
        // Back-compat: use initial_velocity as additional if no additional specified
        bvx = svx + speed * cs; bvy = svy + speed * sn;
    }
    double sx = shooter.x_pixels();
    double sy = shooter.y_pixels();
    double spawn_x = sx + std::cos(theta) * (double)0; // radius unknown to compute offset later; caller may offset by its own bbox
    double spawn_y = sy + std::sin(theta) * (double)0;

    // Fill out
    out.x = spawn_x; out.y = spawn_y;
    out.vx = bvx; out.vy = bvy;
    out.theta = theta;
    out.team = shooter.team;
    if (out.def) {
        if (out.def->rescale != 1.0) out.sprite_scale = (float)out.def->rescale;
        if (out.def->radius > 0.0) out.radius = (int)std::lround(out.def->radius);
    }
    return out;
}
