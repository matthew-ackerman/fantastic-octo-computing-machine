// Simple compile-time configuration parameters
#pragma once

// Minimum width/height of the ship's clickable/selection bounding box in pixels
#define UI_MIN_BBOX_PX 10

// Exponential zoom constant (per wheel step):
// zoom *= exp(UI_ZOOM_LAMBDA_PER_STEP * steps)
#define UI_ZOOM_LAMBDA_PER_STEP 0.0953101798f /* ~= ln(1.1) */

// Physics constants
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Constant ship thrust acceleration in pixels per second^2 (world units)
#define PHYS_ACCEL_PX_S2 100.0f

// Boot sequence default path (can be overridden by game.json paths.boot_sequence)
#define UI_BOOT_SEQUENCE_PATH "boot_sequence/boot_sequence.json"

// HUD panel defaults
#define UI_HUD_WIDTH_DEFAULT 340
#define UI_HUD_PAD_DEFAULT 8

#define UI_HUD_BG_R 20
#define UI_HUD_BG_G 24
#define UI_HUD_BG_B 28
#define UI_HUD_BG_A 220

#define UI_HUD_BORDER_R 80
#define UI_HUD_BORDER_G 170
#define UI_HUD_BORDER_B 255
#define UI_HUD_BORDER_A 255

#define UI_HUD_TEXT_R 235
#define UI_HUD_TEXT_G 235
#define UI_HUD_TEXT_B 235
#define UI_HUD_TEXT_A 255
