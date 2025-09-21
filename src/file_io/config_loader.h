// Loads game configuration from config/game.json with safe defaults.
#pragma once

#include <string>

struct GameConfigPaths {
    std::string assets;        // assets root (e.g., assets)
    std::string images;        // images root (e.g., graphics)
    std::string saves;         // saves dir (e.g., saves)
    std::string config;        // config dir root (e.g., config)
    std::string boot_sequence; // path to boot sequence JSON
};

struct GameConfig {
    GameConfigPaths paths{}; // optional; provides roots/paths
    double min_time_step = 1.0/64.0; // seconds; engine physics max step size
    int net_port = 55555; // TCP listen/connect port for engine/ui
};

// Reads config/game.json if present; fills out with defaults otherwise.
// Returns true on success; on failure, returns false and sets err, but still
// populates 'out' with defaults where possible.
bool load_game_config(const char* path, GameConfig& out, std::string* err = nullptr);

// Optional global access so loaders can consult defaults (e.g., images path).
void set_global_game_config(const GameConfig& cfg);
// Returns nullptr if not set.
const GameConfig* get_global_game_config();
