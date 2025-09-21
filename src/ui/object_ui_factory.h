#pragma once

#include <memory>

#include <SDL.h>

#include "camera.h"
#include "object.h"
#include "object_def.h"
#include "object_selectable.h"

// Creates a UI-selectable wrapper for a live engine Object by reference.
// - Uses def->image for texture and def->rescale/def->radius for visuals.
// - Does NOT copy or own the object; reads state directly from it.
// - If the object is a planet, initializes an auxiliary UI Planet for atmosphere rendering.
// Returns nullptr if definition is missing or texture load fails.
std::unique_ptr<ObjectSelectable>
make_ui_for_object(SDL_Renderer* ren, Camera* cam, const Object& obj);
