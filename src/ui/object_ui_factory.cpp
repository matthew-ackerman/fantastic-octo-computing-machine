#include "object_ui_factory.h"
#include "planet.h"
#include "ship.h"
// no longer constructing UI from InitialState; using live object reference

#include <cmath>

std::unique_ptr<ObjectSelectable>
make_ui_for_object(SDL_Renderer* ren, Camera* cam, const Object& obj)
{
    const ObjectDefinition* def = obj.def;
    if (!def) return nullptr;

    auto ui = std::make_unique<ObjectSelectable>(ren, cam, const_cast<Object&>(obj));

    if (obj.type == Object::PLANET) {
        double rx = (def->radius > 0.0) ? def->radius : (ui->h * 0.5) * ui->sprite_scale;
        double depth = def->atmosphere_depth;
        ui->planet = std::make_unique<Planet>((double)obj.x_pixels(), (double)obj.y_pixels(), rx, depth);
    }
    ui->object_key = def->key;
    return ui;
}
