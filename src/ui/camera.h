#pragma once

struct Camera {
    float zoom = 1.0f;   // screen pixels per world pixel
    float cx = 0.0f;     // world center x
    float cy = 0.0f;     // world center y (up positive)
    int screen_w = 800;  // screen width in pixels
    int screen_h = 600;  // screen height in pixels
};

static inline void world_to_screen(const Camera& cam, float wx, float wy, int& sx, int& sy) {
    sx = (int)std::lround((wx - cam.cx) * cam.zoom + cam.screen_w * 0.5f);
    sy = (int)std::lround(cam.screen_h * 0.5f - (wy - cam.cy) * cam.zoom);
}

static inline void screen_to_world(const Camera& cam, int sx, int sy, float& wx, float& wy) {
    wx = (float)((sx - cam.screen_w * 0.5f) / cam.zoom + cam.cx);
    wy = (float)((cam.screen_h * 0.5f - sy) / cam.zoom + cam.cy);
}

