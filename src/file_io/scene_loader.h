#pragma once

#include <memory>
#include <string>
#include <vector>
#include <map>

#include "object_def.h"
#include "object.h"
#include "engine/initial_state.h"

// Load a scene from a save JSON and build UI-ready objects.
// - save_path: path to save.json
// - object_defs: canonical object definitions loaded from objects.json
// - ren/cam: rendering context used by ObjectSelectable
// - out: filled with created ObjectSelectable instances
// Returns true on success; on failure, returns false and sets err.
bool load_scene_objects(const char* save_path,
                        const std::map<std::string, ObjectDefinition>& object_defs,
                        std::vector<std::unique_ptr<Object>>& out,
                        std::string* err = nullptr);
