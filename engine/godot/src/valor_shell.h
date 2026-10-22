// ValorShell: the front end's rules for Godot's menus (shell.gd): the campaign (sim/campaign.h:
// slots, levels, ratings, medals, passwords), the briefings' pages from the disc, and the saves as
// files holding what the original's memory card file holds.
#pragma once

#include <godot_cpp/classes/image.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/array.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include "valor/sim/campaign.h"

namespace godot {

class ValorShell : public RefCounted {
    GDCLASS(ValorShell, RefCounted)

public:
    void new_game(const String& codename);
    String get_codename() const;
    // The campaign's slots for the mission select: [{slot, mission (1-based), complete, levels:
    // [{level (index), dir (the LVL number), unlocked, best}]}], MSN8 left out.
    Array get_slots() const;
    // The slot and level CONTINUE plays: {slot, level}.
    Dictionary get_next() const;
    // level_begin: {mission, level} (the directories, 1-based) of the slot's level.
    Dictionary begin_level(int slot, int level);
    PackedByteArray get_game() const;
    void set_game(const PackedByteArray& game);
    // After the level (ValorGame.get_level_results): the rating, then progress_commit; {rating,
    // medals (the new ones' bits), stats...}.
    Dictionary commit(const Dictionary& results, bool advance);
    int64_t rate(const Dictionary& results);  // level_rating: 0..2
    int64_t get_medals() const { return campaign_.medals(); }
    int64_t get_cheats_unlocked() const { return campaign_.cheats_unlocked(); }
    int64_t get_cheats_on() const { return campaign_.cheats_on(); }
    void set_cheats_on(int64_t v) { campaign_.set_cheats_on(v); }
    // Options > PASSWORD: {index (-1 unknown, -2 refused), text}.
    Dictionary password(const String& word);
    // The briefing's pages (data\scr2\mbrief\mM\L.rsc: three pages, each a left and a right TIM,
    // 256 x 240 each, side by side): 512 x 240 images.
    Array load_briefing(const String& data_dir, int mission, int level);
    // Saves: one file per save in `dir` (Godot's user data folder, never the recordings' saves/),
    // the memory card file's 0x2000 bytes. list_saves: [{path, title}].
    String save(const String& dir);
    Array list_saves(const String& dir) const;
    bool load(const String& path);
    String get_error() const { return error_; }

protected:
    static void _bind_methods();

private:
    valor::sim::Campaign campaign_;
    String error_;
};

}  // namespace godot
