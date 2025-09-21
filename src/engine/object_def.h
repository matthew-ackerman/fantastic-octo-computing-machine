// Canonical object definition shared by all instances of that object key.
#pragma once

#include <string>

struct ObjectDefinition {
  std::string key;          // object key (map key)
  std::string type;         // "ship", "planet", "projectile", "body"
  std::string image;        // resolved absolute image path
  std::string image_path;   // optional base directory (original)

  // Common visuals/physics
  double rescale = 1.0;     // sprite scale (visual)
  double radius = 0.0;      // visual/physics radius in pixels

  // Ship controls
  bool give_commands = true;
  double ang_accel = 1.0;
  double ang_vel_max = 2.0;
  double delta_v = 0.0;     // propellant budget (pixels/s)

  // Projectile parameters
  double initial_velocity = 0.0;     // pixels/s
  double additional_velocity = 0.0;  // pixels/s

  // Planet atmosphere
  double atmosphere_depth = 0.0;     // pixels
};

