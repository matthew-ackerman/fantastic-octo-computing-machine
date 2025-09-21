#pragma once
#include <string>
#include <vector>

#include "engine/initial_state.h"

// Loads a simple JSON array of objects: [{"image":"...","x":N,"y":N}, ...]
// Returns true on success; on failure, returns false and leaves out empty.
bool load_save_file (const char *path, std::vector<InitialState> &out, std::string *err = nullptr);
