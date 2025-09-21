// Initial state for spawning or loading objects from saves.
#pragma once

#include <string>

struct InitialState {
    std::string image;   // deprecated; image comes from object defs
    std::string object;  // optional object key
    float x = 0.0f;  // world pixels
    float y = 0.0f;  // world pixels
    float vx = 0.0f; // pixels per second
    float vy = 0.0f; // pixels per second
    float theta = 0.0f; // radians
    int team = 0;    // team number

    // Optional advanced overrides
    bool has_give_commands = false; bool give_commands = true;
    bool has_ang_vel = false; float ang_vel = 0.0f;
    bool has_target_theta = false; float target_theta = 0.0f;
    bool has_throttle = false; int throttle = 0;
    bool has_dead = false; bool dead = false;
    bool has_delta_v = false; float delta_v = 0.0f;

    // Presence flags for core kinematics
    bool has_x = false;
    bool has_y = false;
    bool has_vx = false;
    bool has_vy = false;
    bool has_theta = false;
};
