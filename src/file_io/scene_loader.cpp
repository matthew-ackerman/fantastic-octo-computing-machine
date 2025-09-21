#include "scene_loader.h"
#include "save_loader.h"
#include "our_debug.h"

#include "ship.h"
#include "planet.h"

#include <cmath>

static std::string pick_image(const InitialState& s, const std::map<std::string, ObjectDefinition>& defs){
    std::string base = s.image;
    if (!s.object.empty()) {
        auto it = defs.find(s.object);
        if (it != defs.end() && !it->second.image.empty()) base = it->second.image;
    }
    return base;
}

static std::string key_from_spec_or_image(const InitialState& s, const std::string& img){
    std::string key = !s.object.empty() ? s.object : img;
    if (key.find('/') != std::string::npos) key = key.substr(key.find_last_of('/')+1);
    if (key.find('.') != std::string::npos) key = key.substr(0, key.find_last_of('.'));
    return key;
}

bool load_scene_objects(const char* save_path,
                        const std::map<std::string, ObjectDefinition>& object_defs,
                        std::vector<std::unique_ptr<Object>>& out,
                        std::string* err)
{
    out.clear();
    std::vector<InitialState> specs;
    std::string lerr;
    if (!load_save_file(save_path, specs, &lerr)) { if (err) *err = lerr; return false; }

    for (const auto& s : specs) {
        std::string img = pick_image(s, object_defs);
        std::string key = key_from_spec_or_image(s, img);
        const ObjectDefinition* def = nullptr;
        auto it = object_defs.find(key);
        if (it != object_defs.end()) def = &it->second;

        // Create appropriate engine object via new constructors
        std::unique_ptr<Object> obj;
        if (def && def->type == "ship") obj = std::make_unique<Ship>(*def, s);
        else if (def && def->type == "planet") obj = std::make_unique<Planet>(*def, s);
        else obj = std::make_unique<Object>(*def, s);
        out.push_back(std::move(obj));
    }
    return true;
}
