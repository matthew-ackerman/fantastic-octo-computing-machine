#include "object_selectable.h"
#include "object_def.h"
#include "errors.h"
#include "draw_utils.h"
#include "planet.h"

#include <SDL_image.h>
#include <cmath>
#include <cstdio>

static uint64_t g_next_ship_uid = 1ULL;

ObjectSelectable::ObjectSelectable(SDL_Renderer* ren, Camera* camera, Object& obj)
    : object(&obj), cam(camera), renderer_handle(ren) {
    uid = g_next_ship_uid++;
    const ObjectDefinition* def = obj.def;
    const char* path = def ? def->image.c_str() : nullptr;
    if (!path || !*path) {
        std::fprintf(stderr, "FATAL: missing image for object key=%s\n", def ? def->key.c_str() : "<null>");
        std::exit(LOADING_ERROR);
    }
    tex = IMG_LoadTexture(ren, path);
    if (!tex) {
        std::fprintf(stderr, "FATAL: IMG_LoadTexture failed for '%s': %s\n", path, IMG_GetError());
        std::fflush(stderr);
        std::exit(LOADING_ERROR);
    }
    SDL_QueryTexture(tex, nullptr, nullptr, &w, &h);
    r = h / 2;

    // Visual scale and radius from def when present
    if (def) {
        if (def->rescale != 1.0) sprite_scale = (float)def->rescale;
        if (def->radius > 0.0) r = (int)std::lround(def->radius);
        else if (def->rescale != 1.0) r = (int)std::lround((h * 0.5) * sprite_scale);
        object_key = def->key;
    }
}

ObjectSelectable::~ObjectSelectable() {
    if (tex) SDL_DestroyTexture(tex);
    tex = nullptr;
}

void ObjectSelectable::draw(SDL_Renderer* ren) {
    if (tex) {
        int sw = (int)std::round(w * cam->zoom * sprite_scale);
        int sh = (int)std::round(h * cam->zoom * sprite_scale);
        if (sw < 1) sw = 1; if (sh < 1) sh = 1;
        if (!logged_large_sprite && (sw > 10000 || sh > 10000)) {
            logged_large_sprite = true;
        }
        int sx, sy; world_to_screen(*cam, (float)object->x_pixels(), (float)object->y_pixels(), sx, sy);
        SDL_Rect dst { sx - sw/2, sy - sh/2, sw, sh };
        double angle_deg = (double)((M_PI/2.0) - object->theta) * (180.0 / M_PI);
        SDL_RenderCopyEx(ren, tex, nullptr, &dst, angle_deg, nullptr, SDL_FLIP_NONE);
    } else {
        SDL_SetRenderDrawColor(ren, 200, 60, 60, 255);
        int sx, sy; world_to_screen(*cam, (float)object->x_pixels(), (float)object->y_pixels(), sx, sy);
        draw_circle_filled(ren, sx, sy, 20);
    }
}

bool ObjectSelectable::hit(int mx, int my) const {
    int sx, sy; world_to_screen(*cam, (float)object->x_pixels(), (float)object->y_pixels(), sx, sy);
    long long R = (long long)std::llround((double)r * cam->zoom);
    if (R < 1) R = 1;
    long long dx = (long long)mx - (long long)sx;
    long long dy = (long long)my - (long long)sy;
    return (dx*dx + dy*dy) <= (R*R);
}

void ObjectSelectable::draw_bbox(SDL_Renderer* ren) const {
    int sx, sy; world_to_screen(*cam, (float)object->x_pixels(), (float)object->y_pixels(), sx, sy);
    int R = (int)std::llround((double)r * cam->zoom);
    if (R < 1) R = 1;
    if (object->team == 0) { if (selected) SDL_SetRenderDrawColor(ren, 100, 200, 255, 255); else SDL_SetRenderDrawColor(ren, 60, 120, 255, 255); }
    else { if (selected) SDL_SetRenderDrawColor(ren, 255, 160, 160, 255); else SDL_SetRenderDrawColor(ren, 255, 100, 100, 255); }
    const int max_small = 2048;
    if (R <= max_small) draw_circle_outline(ren, sx, sy, R);
    else { const_cast<ObjectSelectable*>(this)->logged_large_bbox = true; draw_circle_outline_clipped(ren, sx, sy, R, cam->screen_w, cam->screen_h); }
    if (planet && planet->atmosphere.enabled) {
        double ar_world = planet->atmosphere.radius;
        int Ra = (int)std::llround(ar_world * cam->zoom);
        if (Ra > 0) {
            SDL_SetRenderDrawColor(ren, 80, 160, 255, 200);
            const int max_small_a = 2048;
            if (Ra <= max_small_a) draw_circle_outline(ren, sx, sy, Ra);
            else draw_circle_outline_clipped(ren, sx, sy, Ra, cam->screen_w, cam->screen_h);
        }
    }
}
