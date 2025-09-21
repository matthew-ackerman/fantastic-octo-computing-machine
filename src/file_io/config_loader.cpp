#include "config_loader.h"
#include "our_debug.h"
#include "errors.h"
#include "json_interface.h"

#include <fstream>
#include <cstdio>

static GameConfig g_cfg;
static bool g_cfg_set = false;

// Model implementing JsonInterface to minimize loader boilerplate
struct GameConfigModel : public JsonInterface {
    GameConfig cfg; // initialized with defaults before parse

    bool from_json(const JsonView& root, std::string* err) override {
        if (!root.is_object()) { if (err) *err = "Top-level must be an object"; return false; }

        // Paths block
        JsonView jpaths; if (root.get_view("paths", jpaths) && jpaths.is_object()) {
            (void)get_json_value(jpaths, "assets", &cfg.paths.assets);
            (void)get_json_value(jpaths, "images", &cfg.paths.images);
            (void)get_json_value(jpaths, "saves", &cfg.paths.saves);
            (void)get_json_value(jpaths, "config", &cfg.paths.config);
            (void)get_json_value(jpaths, "boot_sequence", &cfg.paths.boot_sequence);
        }

        // Networking block with legacy fallback
        JsonView jnet; if (root.get_view("net", jnet) && jnet.is_object()) {
            (void)get_json_value(jnet, "port", &cfg.net_port);
        } else {
            (void)get_json_value(root, "net_port", &cfg.net_port);
        }

        // Engine timing
        (void)get_json_value(root, "min_time_step", &cfg.min_time_step);

        return true;
    }
};

bool load_game_config(const char* path, GameConfig& out, std::string* err)
{
    // Initialize model with existing defaults from 'out', then overlay JSON
    GameConfigModel model; model.cfg = out;
    if (!JsonInterface::load_file(path, model, err)) {
        const char* msg = (err && !err->empty()) ? err->c_str() : "Failed to load game config";
        CRASH(ExitCode::LOADING_ERROR, "%s: %s", msg, path ? path : "<null>");
    }

    out = model.cfg;
    // Defaults for missing paths
    if (out.paths.assets.empty()) out.paths.assets = "assets";
    if (out.paths.images.empty()) out.paths.images = "graphics";
    if (out.paths.saves.empty()) out.paths.saves = "saves";
    if (out.paths.config.empty()) out.paths.config = "config";
    if (out.paths.boot_sequence.empty()) out.paths.boot_sequence = "boot_sequence/boot_sequence.json";

    if (out.min_time_step <= 0.0) out.min_time_step = 1.0/64.0;

    DBG("game config: paths.assets=%s images=%s saves=%s config=%s boot=%s net.port=%d min_dt=%.6f",
        out.paths.assets.c_str(), out.paths.images.c_str(), out.paths.saves.c_str(), out.paths.config.c_str(), out.paths.boot_sequence.c_str(), out.net_port, out.min_time_step);
    return true;
}

void set_global_game_config(const GameConfig& cfg){ g_cfg = cfg; g_cfg_set = true; }
const GameConfig* get_global_game_config(){ return g_cfg_set ? &g_cfg : nullptr; }
