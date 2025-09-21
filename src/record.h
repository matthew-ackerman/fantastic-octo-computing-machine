#pragma once

#include <string>
#include <vector>
#include <cstdint>

struct RecordTurn {
    int index = -1;
    std::vector<std::string> commands; // ordered, raw command strings
};

// Minimal command recorder for per-turn inputs
struct Record {
    uint32_t random_seed = 0;
    std::vector<RecordTurn> turns;
    int cur_turn = -1;

    // Begin a new match (clears previous state)
    void start_match ();

    // Start a new turn (finalizes the previous turn implicitly)
    void start_turn ();

    // Append a raw command to the current turn
    void add (const std::string &cmd);

    // Save as simple JSON to the given path; returns true on success
    bool save_json (const std::string &path) const;

    // Load from JSON file created by save_json
    bool load_json (const std::string &path, std::string *err);
};
