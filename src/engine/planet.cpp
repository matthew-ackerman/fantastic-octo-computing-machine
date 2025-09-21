#include "planet.h"
#include "errors.h"

#include <cmath>
#include <cstdio>

Atmosphere::Atmosphere ()
  : enabled (false), radius (0.0), surface_density (0.0), scale_height (0.0)
{
}

Planet::Planet ()
  : radius_pixels (0.0)
{
  type = PLANET;
  flags |= F_IS_PLANET;
}

Planet::Planet (double px, double py, double r_pixels)
  : radius_pixels (r_pixels)
{
  type = PLANET;
  flags |= F_IS_PLANET;
  set_from_floats ((float) px, (float) py, 0.0f, 0.0f);
}

Planet::Planet (double px, double py, double r_pixels, double atmosphere_depth)
  : radius_pixels (r_pixels)
{
  type = PLANET;
  flags |= F_IS_PLANET;
  set_from_floats ((float) px, (float) py, 0.0f, 0.0f);
  atmosphere.enabled = (atmosphere_depth > 0.0);
  atmosphere.radius = r_pixels + atmosphere_depth;
}

Planet::Planet (const ObjectDefinition& def, const InitialState& init)
  : Object(def, init), radius_pixels(0.0)
{
  if (def.type != "planet") {
    std::fprintf(stderr, "FATAL: Planet constructed with non-planet definition key=%s type=%s\n", def.key.c_str(), def.type.c_str());
    std::exit(ExitCode::LOADING_ERROR);
  }
  type = PLANET; flags |= F_IS_PLANET;
  // radius from def; if not provided, derive from sprite later in UI
  radius_pixels = (def.radius > 0.0) ? def.radius : 0.0;
  if (def.atmosphere_depth > 0.0) {
    atmosphere.enabled = true;
    atmosphere.radius = radius_pixels + def.atmosphere_depth;
  }
}

void
Planet::advance (double dt_seconds)
{
  // For now, planets use base Object kinematics only (no thrust/steering).
  // This keeps behavior simple until atmosphere/gravity are defined.
  (void) dt_seconds; // advance handled by base if needed; planets are static by default
}
