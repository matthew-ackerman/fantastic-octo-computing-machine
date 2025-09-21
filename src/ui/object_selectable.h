#pragma once

#include <SDL.h>
#include <memory>
#include <string>

#include "camera.h"
#include "object.h"
#include "planet.h"

struct UIElement {
    virtual ~UIElement() = default;
    virtual void draw(SDL_Renderer* r) = 0;
    virtual void on_mouse_down(int mx, int my) { (void)mx; (void)my; }
};

struct ObjectSelectable : public UIElement {
    Object* object = nullptr; // non-owning reference to engine object
    int r = 48;
    bool selected = false;
    SDL_Texture* tex = nullptr;
    int w = 0, h = 0;
    Camera* cam = nullptr;
    SDL_Renderer* renderer_handle = nullptr;
    float sprite_scale = 1.0f;
    std::string object_key;
    uint64_t uid = 0;

    bool logged_large_sprite = false;
    bool logged_large_bbox = false;

    std::unique_ptr<Planet> planet;

    ObjectSelectable(SDL_Renderer* ren, Camera* camera, Object& obj);
    ~ObjectSelectable() override;

    void draw(SDL_Renderer* ren) override;

    //Seems like game engine logic.
    bool hit(int mx, int my) const;
    void draw_bbox(SDL_Renderer* ren) const;
};

using ShipSelectable = ObjectSelectable; // back-compat alias
