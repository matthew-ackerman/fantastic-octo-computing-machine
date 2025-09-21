#include "object_loader.h"

#include <filesystem>
#include <iostream>
#include <cstdio>
#include <fstream>
#include <json-c/json.h>

#include "our_debug.h"
#include "config_loader.h"
#include "errors.h"
#include "json_interface.h"


namespace fs = std::filesystem;

static bool
exists_and_abs (std::string &path, const std::string &base_dir)
{
  fs::path p (path);
  if (!base_dir.empty () && !p.has_parent_path ()) p = fs::path (base_dir) / p;
  std::error_code ec;
  if (!p.is_absolute ()) p = fs::absolute (p, ec);
  if (!fs::exists (p)) return false;
  path = p.string ();
  return true;
}

bool
load_object_defs (const char *path, std::map<std::string, ObjectDefinition> &out, std::string *err)
{
  DBG ("load_object_defs(%s)", path ? path : "<null>");
  out.clear ();
  JsonDoc doc = JsonDoc::from_file(path, err);
  if (!doc.valid()) { const char* msg = err ? err->c_str() : "objects.json not found or invalid"; CRASH(ExitCode::LOADING_ERROR, "%s: %s", msg, path ? path : "<null>"); }
  json_object* root = doc.get();
  if (!json_object_is_type (root, json_type_object)) { if (err) *err = "Top-level JSON must be an object"; CRASH(ExitCode::LOADING_ERROR, "%s", err?err->c_str():"Top-level JSON must be an object"); }

  std::string top_image_path;
  {
    json_object *tmpTop = nullptr;
    if (json_object_object_get_ex (root, "image_path", &tmpTop) && json_object_is_type (tmpTop, json_type_string))
      top_image_path = json_object_get_string (tmpTop);
  }

  json_object_object_foreach (root, k, v) {
    if (!json_object_is_type (v, json_type_object)) continue;
    ObjectDefinition def;
    JsonView item(v);
    if (!get_json_value(item, "type", &def.type) || def.type.empty()) {
      CRASH(ExitCode::LOADING_ERROR, "[objects] missing required 'type' for key %s", k);
    }

    (void)get_json_value(item, "image", &def.image);
    (void)get_json_value(item, "image_path", &def.image_path);

    (void)get_json_value(item, "give_commands", &def.give_commands);
    (void)get_json_value(item, "ang_accel", &def.ang_accel);
    (void)get_json_value(item, "ang_vel_max", &def.ang_vel_max);
    (void)get_json_value(item, "radius", &def.radius);
    (void)get_json_value(item, "delta_v", &def.delta_v);
    (void)get_json_value(item, "initial_velocity", &def.initial_velocity);
    (void)get_json_value(item, "additional_velocity", &def.additional_velocity);
    (void)get_json_value(item, "rescale", &def.rescale);
    (void)get_json_value(item, "atmosphere_depth", &def.atmosphere_depth);

    // Resolve image now if present
    if (!def.image.empty ()) {
      std::string p = def.image;
      std::string baseDir = def.image_path.empty () ? top_image_path : def.image_path;
      if (baseDir.empty()) {
        if (const GameConfig* gc = get_global_game_config()) {
          if (!gc->paths.images.empty()) baseDir = gc->paths.images;
        }
      }
      if (!exists_and_abs (p, baseDir)) {
        std::cerr << "FATAL: object '" << k << "' image not found: " << p << "\n";
        std::exit (ExitCode::LOADING_ERROR);
      }
      def.image = p;
    }
    def.key = k;
    out[k] = def;
  }

  DBG ("load_object_defs done: %zu entries", out.size ());
  return true;
}
