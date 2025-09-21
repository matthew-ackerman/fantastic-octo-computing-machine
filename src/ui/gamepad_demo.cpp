// Standalone SDL2 GameController demo (not integrated with the main app)
// Shows how to interpret inputs from a gamepad via SDL_GameController.

#include <SDL.h>
#include <SDL_gamecontroller.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>
#include <cmath>

static const char* axis_name(SDL_GameControllerAxis a) {
    const char* s = SDL_GameControllerGetStringForAxis(a);
    return s ? s : "unknown_axis";
}

static const char* button_name(SDL_GameControllerButton b) {
    const char* s = SDL_GameControllerGetStringForButton(b);
    return s ? s : "unknown_button";
}

static void print_controller_overview(int index) {
    const char* name = SDL_GameControllerNameForIndex(index);
    SDL_JoystickGUID guid = SDL_JoystickGetDeviceGUID(index);
    char guid_str[64];
    SDL_JoystickGetGUIDString(guid, guid_str, sizeof(guid_str));
    std::printf("[info] controller index=%d name=\"%s\" guid=%s\n",
               index, name ? name : "(null)", guid_str);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // Optional: Create a tiny window so some platforms keep focus and events flowing.
    SDL_Window* win = SDL_CreateWindow("Gamepad Demo (SDL_GameController)", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 640, 200, SDL_WINDOW_SHOWN);
    if (!win) {
        std::fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    std::printf("[junk] gamepad demo starting\n");
    int num = SDL_NumJoysticks();
    std::printf("[junk] joysticks=%d\n", num);
    for (int i = 0; i < num; ++i) {
        if (SDL_IsGameController(i)) print_controller_overview(i);
    }

    SDL_GameController* ctrl = nullptr;
    int open_index = -1;
    for (int i = 0; i < num; ++i) {
        if (SDL_IsGameController(i)) {
            ctrl = SDL_GameControllerOpen(i);
            if (ctrl) { open_index = i; break; }
        }
    }
    if (!ctrl) {
        std::fprintf(stderr, "[warn] No SDL GameController-compatible device found. Plug one in.\n");
    } else {
        SDL_Joystick* js = SDL_GameControllerGetJoystick(ctrl);
        SDL_JoystickID jid = js ? SDL_JoystickInstanceID(js) : -1;
        const char* map = SDL_GameControllerMapping(ctrl);
        std::printf("[info] opened controller index=%d instance_id=%d name=\"%s\"\n",
                   open_index, (int)jid, SDL_GameControllerName(ctrl));
        if (map) std::printf("[info] mapping: %s\n", map);
    }

    // Noisy dump mode: we will dump current state frequently regardless of change

    bool running = true;
    Uint32 last_poll = SDL_GetTicks();
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT: running = false; break;
                case SDL_KEYDOWN:
                    if (e.key.keysym.sym == SDLK_ESCAPE) running = false;
                    break;
                case SDL_CONTROLLERDEVICEADDED: {
                    int which = e.cdevice.which;
                    std::printf("EV add idx=%d\n", which);
                    if (!ctrl && SDL_IsGameController(which)) {
                        ctrl = SDL_GameControllerOpen(which);
                        if (ctrl) {
                            open_index = which;
                            std::printf("EV open idx=%d name=\"%s\"\n", which, SDL_GameControllerName(ctrl));
                        }
                    }
                    break;
                }
                case SDL_CONTROLLERDEVICEREMOVED: {
                    SDL_JoystickID which = e.cdevice.which;
                    std::printf("EV remove id=%d\n", (int)which);
                    if (ctrl) {
                        SDL_Joystick* js = SDL_GameControllerGetJoystick(ctrl);
                        if (js && SDL_JoystickInstanceID(js) == which) {
                            SDL_GameControllerClose(ctrl);
                            ctrl = nullptr; open_index = -1;
                        }
                    }
                    break;
                }
                case SDL_CONTROLLERAXISMOTION: {
                    SDL_GameControllerAxis a = (SDL_GameControllerAxis)e.caxis.axis;
                    Sint16 v = e.caxis.value; // -32768..32767
                    double norm = (v >= 0) ? (double)v/32767.0 : (double)v/32768.0;
                    std::printf("EV axis %s=%d (%.3f)\n", axis_name(a), (int)v, norm);
                    break;
                }
                case SDL_CONTROLLERBUTTONDOWN:
                case SDL_CONTROLLERBUTTONUP: {
                    SDL_GameControllerButton b = (SDL_GameControllerButton)e.cbutton.button;
                    bool down = (e.type == SDL_CONTROLLERBUTTONDOWN);
                    std::printf("EV button %s=%s\n", button_name(b), down ? "DOWN" : "UP");
                    break;
                }
                default: break;
            }
        }

        // Periodic poll to dump current state (noisy)
        if (SDL_GetTicks() - last_poll >= 16) { // ~60 Hz
            last_poll = SDL_GetTicks();
            if (!ctrl) {
                std::printf("JUNK no_controller ts=%u\n", last_poll);
            } else {
                // Read left/right stick axes
                auto norm_axis = [](Sint16 v)->double { return (v >= 0) ? (double)v/32767.0 : (double)v/32768.0; };
                double lx = norm_axis(SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTX));
                double ly = norm_axis(SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_LEFTY));
                double rx = norm_axis(SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTX));
                double ry = norm_axis(SDL_GameControllerGetAxis(ctrl, SDL_CONTROLLER_AXIS_RIGHTY));
                // Invert Y so up is +Y for theta
                ly = -ly; ry = -ry;
                // Compute polar
                auto polar = [](double x, double y){
                    double d = std::sqrt(x*x + y*y);
                    double th = (d > 1e-6) ? std::atan2(y, x) : 0.0; // radians, [-pi,pi]
                    if (th < 0.0) th += 2.0*M_PI; // [0,2pi)
                    if (d > 1.0) d = 1.0; // clamp
                    return std::pair<double,double>(th, d);
                };
                auto L = polar(lx, ly);
                auto R = polar(rx, ry);
                std::printf("STATE ts=%u L: th=%.3f d=%.3f | R: th=%.3f d=%.3f\n",
                            last_poll, L.first, L.second, R.first, R.second);
            }
        }

        SDL_Delay(1);
    }

    if (ctrl) SDL_GameControllerClose(ctrl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
