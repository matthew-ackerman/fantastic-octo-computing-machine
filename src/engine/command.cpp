#include "command.h"
#include "planet.h"

#include <cmath>

static inline bool same_target(const Command& a, const Command& b) {
    if (a.ship && b.ship) return a.ship == b.ship;
    return a.uid != 0 && b.uid != 0 && a.uid == b.uid;
}

void queue_command(const Command& c, std::vector<Command>& command_stack) {
    // Prevent duplicate/conflicting commands per ship within a turn
    if (c.type == Command::Type::FIRE) {
        for (const auto& ex : command_stack) {
            if (ex.type == Command::Type::FIRE && same_target(ex, c)) return; // already queued
        }
        command_stack.push_back(c);
        return;
    }
    // HEADING/THROTTLE: last one wins
    for (auto it = command_stack.begin(); it != command_stack.end(); ++it) {
        if (it->type == c.type && same_target(*it, c)) { *it = c; return; }
    }
    command_stack.push_back(c);
}

static std::string pick_proj_for(const Command& c) {
    if (!c.key.empty()) return c.key;
    if (c.ship) {
        // Minimal default: choose by weapon
        return (c.ship->weapon == Ship::Weapon::LASER) ? std::string("laser") : std::string("bullet");
    }
    return std::string("bullet");
}

void apply_commands(std::vector<Command>& command_stack,
                    std::vector<std::unique_ptr<Object>>& objs,
                    std::map<std::string, ObjectDefinition>& object_defs)
{
    for (const auto& c : command_stack) {
        if (!c.ship) continue; // invalid target
        switch (c.type) {
            case Command::Type::THROTTLE: {
                c.ship->throttle = (int)std::lround(c.a);
            } break;
            case Command::Type::HEADING: {
                c.ship->target_theta = c.a;
            } break;
            case Command::Type::FIRE: {
                std::string pkey = pick_proj_for(c);
                auto ps = compute_projectile_spawn(*c.ship, c.a, object_defs, pkey);

                // Offset spawn by shooter radius using def->radius if available
                double shooter_r = 0.0;
                if (c.ship->def && c.ship->def->radius > 0.0) shooter_r = c.ship->def->radius;
                double sx = c.ship->x_pixels();
                double sy = c.ship->y_pixels();
                double spawn_x = sx + std::cos(c.a) * shooter_r;
                double spawn_y = sy + std::sin(c.a) * shooter_r;

                // Build InitialState for projectile
                InitialState init;
                init.object = pkey;
                init.x = (float)spawn_x; init.y = (float)spawn_y; init.has_x = true; init.has_y = true;
                init.vx = (float)ps.vx; init.vy = (float)ps.vy; init.has_vx = true; init.has_vy = true;
                init.theta = (float)ps.theta; init.has_theta = true;
                init.team = ps.team;
                init.has_give_commands = true; init.give_commands = false;
                init.has_ang_vel = true; init.ang_vel = 0.0f;
                init.has_target_theta = true; init.target_theta = (float)ps.theta;

                // Create engine object and add to world
                if (ps.def) {
                    if (ps.def->type == "ship") objs.push_back(std::make_unique<Ship>(*ps.def, init));
                    else if (ps.def->type == "planet") objs.push_back(std::make_unique<Planet>(*ps.def, init));
                    else objs.push_back(std::make_unique<Object>(*ps.def, init));
                }
                c.ship->fired_this_turn = true;
            } break;
        }
    }
    command_stack.clear();
}
