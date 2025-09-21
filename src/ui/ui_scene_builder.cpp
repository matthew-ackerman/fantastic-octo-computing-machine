#include "ui_scene_builder.h"
#include "object_ui_factory.h"

std::vector<std::unique_ptr<ObjectSelectable>>
build_ui_scene(SDL_Renderer* ren, Camera* cam, const std::vector<std::unique_ptr<Object>>& objs)
{
    std::vector<std::unique_ptr<ObjectSelectable>> out;
    out.reserve(objs.size());
    for (const auto& obj : objs) {
        auto ui = make_ui_for_object(ren, cam, *obj);
        if (ui) out.push_back(std::move(ui));
    }
    return out;
}

