#pragma once
#include <string>
#include <map>
#include "object_def.h"
#include "errors.h"

// Fields that can occur in the json file; presence booleans indicate whether
// the value was specified in JSON (vs. default-initialized).
struct ObjectReadBuffer {
    std::string image;      // absolute path after resolving
    std::string image_path; // optional base directory
    std::string type;       // required: "ship", "planet", "projectile", "body"

    // Ship-related
    bool has_give_commands = false; bool give_commands = true;
    bool has_ang_accel = false; double ang_accel = 0.0;
    bool has_ang_vel_max = false; double ang_vel_max = 0.0;
    bool has_delta_v = false; double delta_v = 0.0;

    // Common visuals/physics
    bool has_rescale = false; double rescale = 1.0; // sprite scale
    bool has_radius = false; double radius = 0.0;   // pixels

    // Projectile-related
    bool has_initial_velocity = false; double initial_velocity = 0.0;     // pixels/s
    bool has_additional_velocity = false; double additional_velocity = 0.0; // pixels/s

    // Planet-related
    bool has_atmosphere_depth = false; double atmosphere_depth = 0.0; // pixels
};

bool load_object_defs (const char *path,
                       std::map<std::string, ObjectDefinition> &out,
                       std::string *err = nullptr);
