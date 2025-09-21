#include "record.h"

#include <fstream>
#include <sstream>
#include <json-c/json.h>

void
Record::start_match ()
{
    turns.clear();
    cur_turn = -1;
}

void
Record::start_turn ()
{
    RecordTurn t;
    t.index = (int)turns.size();
    turns.push_back(std::move(t));
    cur_turn = (int)turns.size() - 1;
}

void
Record::add (const std::string &cmd)
{
    if (cur_turn < 0) {
        start_turn();
    }
    turns[(size_t)cur_turn].commands.push_back(cmd);
}

bool
Record::save_json (const std::string &path) const
{
    std::ofstream out (path, std::ios::binary);
    if (!out) return false;
    out << "{\n";
    out << "  \"random_seed\": " << random_seed << ",\n";
    out << "  \"turns\": [\n";
    for (size_t i = 0; i < turns.size(); ++i) {
        const auto& t = turns[i];
        out << "    { \"index\": " << t.index << ", \"commands\": [";
        for (size_t j = 0; j < t.commands.size(); ++j) {
            // Escape quotes minimally
            std::string s = t.commands[j];
            std::ostringstream esc;
            for (char c : s) {
                if (c == '"') esc << "\\\"";
                else if (c == '\\') esc << "\\\\";
                else if (c == '\n') esc << "\\n";
                else esc << c;
            }
            out << '\"' << esc.str() << '\"';
            if (j + 1 < t.commands.size()) out << ", ";
        }
        out << "] }";
        if (i + 1 < turns.size()) out << ",";
        out << "\n";
    }
    out << "  ]\n}\n";
    return true;
}

bool
Record::load_json (const std::string &path, std::string *err)
{
    json_object *root = json_object_from_file (path.c_str ());
    if (!root) { if (err) *err = "Failed to open or parse record file"; return false; }
    // random_seed (optional)
    json_object* seed = nullptr;
    if (json_object_object_get_ex (root, "random_seed", &seed) && json_object_is_type (seed, json_type_int)) {
        random_seed = (uint32_t) json_object_get_int64 (seed);
    } else {
        random_seed = 0;
    }
    json_object* turns_arr = nullptr;
    if (!json_object_object_get_ex (root, "turns", &turns_arr) || !json_object_is_type (turns_arr, json_type_array)) {
        if (err) *err = "Missing 'turns' array";
        json_object_put (root);
        return false;
    }
    turns.clear();
    cur_turn = -1;
    int len = (int) json_object_array_length (turns_arr);
    for (int i = 0; i < len; ++i) {
        json_object *t = json_object_array_get_idx (turns_arr, i);
        if (!t || !json_object_is_type (t, json_type_object)) continue;
        RecordTurn rt;
        rt.index = i;
        json_object *cmds = nullptr;
        if (json_object_object_get_ex (t, "commands", &cmds) && json_object_is_type (cmds, json_type_array)) {
            int clen = (int) json_object_array_length (cmds);
            rt.commands.reserve((size_t)clen);
            for (int j = 0; j < clen; ++j) {
                json_object *s = json_object_array_get_idx (cmds, j);
                if (!s || !json_object_is_type (s, json_type_string)) continue;
                rt.commands.emplace_back (json_object_get_string (s));
            }
        }
        turns.push_back (std::move (rt));
    }
    cur_turn = turns.empty() ? -1 : (int)turns.size() - 1;
    json_object_put (root);
    return true;
}
