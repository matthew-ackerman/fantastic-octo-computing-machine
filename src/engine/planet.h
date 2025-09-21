// Simple planet model derived from Object, with a placeholder atmosphere.
#pragma once

#include "object.h"
#include "object_def.h"
#include "initial_state.h"

class Atmosphere
{
public:
  bool enabled;
  double radius;           // atmosphere radius (pixels)
  double surface_density;  // density at surface (arbitrary units)
  double scale_height;     // scale height (pixels)

  Atmosphere ();
};

class Planet : public Object
{
public:
  double radius_pixels; // visual/physics radius for the planet itself
  Atmosphere atmosphere; // placeholder subsystem

  Planet ();
  explicit Planet (double px, double py, double r_pixels);
  Planet (double px, double py, double r_pixels, double atmosphere_depth);
  Planet (const ObjectDefinition& def, const InitialState& init);

  void advance (double dt_seconds) override;
};
