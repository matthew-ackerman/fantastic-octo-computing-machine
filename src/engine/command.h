// Command queue and execution helpers
#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <map>
#include <memory>

#include "ship.h"
#include "object_def.h"
#include "initial_state.h"

struct Command {
    enum class Type { THROTTLE, HEADING, FIRE } type{Type::THROTTLE};
    double a = 0.0;         // payload: throttle (0/1) or theta (radians)
    double b = 0.0;         // reserved
    std::string key;        // projectile key for FIRE; may be empty to auto-pick

    Ship* ship = nullptr;   // target ship (live pointer when queued)
    uint64_t uid = 0;       // stable id for save/replay
};

// Queue semantics:
// - FIRE: at most one per-ship per turn (ignore duplicates)
// - HEADING/THROTTLE: last one wins for the same ship
void queue_command(const Command& c, std::vector<Command>& command_stack);

// Apply queued commands to the engine world (objs). Spawns projectiles into objs.
// After application, the stack is cleared.
void apply_commands(std::vector<Command>& command_stack,
                    std::vector<std::unique_ptr<Object>>& objs,
                    std::map<std::string, ObjectDefinition>& object_defs);
