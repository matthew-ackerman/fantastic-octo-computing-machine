#pragma once
#include <SDL.h>
#include <cmath>

static inline void draw_pixel(SDL_Renderer* r, int x, int y) {
    SDL_RenderDrawPoint(r, x, y);
}

static inline void draw_circle_outline(SDL_Renderer* r, int cx, int cy, int radius) {
    int x = radius;
    int y = 0;
    int err = 0;
    while (x >= y) {
        draw_pixel(r, cx + x, cy + y);
        draw_pixel(r, cx + y, cy + x);
        draw_pixel(r, cx - y, cy + x);
        draw_pixel(r, cx - x, cy + y);
        draw_pixel(r, cx - x, cy - y);
        draw_pixel(r, cx - y, cy - x);
        draw_pixel(r, cx + y, cy - x);
        draw_pixel(r, cx + x, cy - y);
        y += 1;
        if (err <= 0) err += 2*y + 1;
        if (err > 0) { x -= 1; err -= 2*x + 1; }
    }
}

static inline void draw_circle_filled(SDL_Renderer* r, int cx, int cy, int radius) {
    for (int dy = -radius; dy <= radius; ++dy) {
        int y = cy + dy;
        int dx = (int)std::floor(std::sqrt((double)(radius*radius - dy*dy)));
        int x1 = cx - dx;
        int x2 = cx + dx;
        SDL_RenderDrawLine(r, x1, y, x2, y);
    }
}

static inline void draw_circle_outline_clipped(SDL_Renderer* r,
                                               int cx, int cy, int radius,
                                               int screen_w, int screen_h) {
    if (radius <= 0) return;
    const long long R2 = (long long)radius * (long long)radius;
    int x_min = std::max(0, cx - radius);
    int x_max = std::min(screen_w - 1, cx + radius);
    for (int x = x_min; x <= x_max; ++x) {
        long long dx = (long long)x - (long long)cx;
        long long inside = R2 - dx*dx;
        if (inside < 0) continue;
        int dy = (int)std::floor(std::sqrt((double)inside));
        int y1 = cy - dy;
        int y2 = cy + dy;
        if (y1 >= 0 && y1 < screen_h) SDL_RenderDrawPoint(r, x, y1);
        if (y2 >= 0 && y2 < screen_h) SDL_RenderDrawPoint(r, x, y2);
    }
    int y_min = std::max(0, cy - radius);
    int y_max = std::min(screen_h - 1, cy + radius);
    for (int y = y_min; y <= y_max; ++y) {
        long long dy = (long long)y - (long long)cy;
        long long inside = R2 - dy*dy;
        if (inside < 0) continue;
        int dx = (int)std::floor(std::sqrt((double)inside));
        int x1 = cx - dx;
        int x2 = cx + dx;
        if (x1 >= 0 && x1 < screen_w) SDL_RenderDrawPoint(r, x1, y);
        if (x2 >= 0 && x2 < screen_w) SDL_RenderDrawPoint(r, x2, y);
    }
}

