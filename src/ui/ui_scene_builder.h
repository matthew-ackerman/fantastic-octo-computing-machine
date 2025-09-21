#pragma once

#include <memory>
#include <vector>

#include <SDL.h>

#include "camera.h"
#include "object.h"
#include "object_selectable.h"

// Build UI-wrappers for engine objects using the UI factory.
std::vector<std::unique_ptr<ObjectSelectable>>
build_ui_scene(SDL_Renderer* ren, Camera* cam, const std::vector<std::unique_ptr<Object>>& objs);

