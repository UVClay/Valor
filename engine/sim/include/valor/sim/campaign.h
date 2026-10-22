// The front end's campaign state and its rules (docs/re/shell.md: The campaign, Results and
// recording them, Passwords and cheats, Medals, The save; levels.md: Results): the save data
// (`g_game`, 0x780 bytes) and the shell's own variables, with the SHELL.BIN functions that change
// them ported. A host draws the menus; this decides what they do.
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "valor/sim/common.h"

namespace valor::sim {

// What a finished level hands to the shell (LEVEL's g_level_end and the statistics in the exe's
// data, combat.md Statistics), read from the engine when the level ends.
struct LevelResults {
    I exit_code = 0;               // g_level_exit: 0 / 3 finished, 1 quit
    I objectives = 0, done = 0;    // g_level_end +0, +1
    bool primary_complete = false;  // g_level_end +0xC
    I health = 0;                  // g_level_end +8, 16.16
    std::array<I, 13> ammo{};      // g_level_end +0x10 + 4 k: the carried ammo per weapon
    std::array<I, 13> weapon_kills{};
    std::array<I, 0x1B> stats{};   // 0x80039CA8.. (s32 each): shots, hits, wounds, melee, kills, ...
    I shots() const { return stats[0]; }
    I hits() const { return stats[1]; }
    I wounds() const { return stats[2]; }
    I kills() const { return stats[4]; }
    I head_hits() const { return stats[6]; }
};

class Campaign {
public:
    // A shell freshly booted: game_defaults (0x80060B0C) then new_game (0x80061F00).
    Campaign();
    std::vector<uint8_t> game;          // g_game
    std::array<I, 8> slot_level{};      // g_campaign[s].level (the slot's next level)
    I rating_slot = 0, rating_level = 0;  // g_rating_slot, g_rating_level
    I new_rating = -1;                  // g_new_rating (level_rating's)
    I level_exit = 4;                   // g_level_exit
    bool smpsmdmilk = false;            // 0x8007BA62

    static constexpr int kSlots = 8;
    static int slot_mission(int s);     // g_campaign[s].mission (index; MSN = index + 1)
    static int slot_levels(int s);      // g_campaign[s].count
    static bool slot_on_disc(int s) { return s != 6; }

    void new_game(const std::string& codename);  // new_game: progress reset, MSN1 LVL1 open
    std::string codename() const;

    // The level row of a slot's level (g_game+0x18C + mission 0x98 + level 0x24).
    size_t row(int slot, int level) const;
    I best_rating(int slot, int level) const;    // -1 never completed
    bool unlocked(int slot, int level) const;    // row +0x20
    bool mission_complete(int slot) const;       // block +0x94
    I medals() const { return r32(0xD0); }
    I cheats_unlocked() const { return r32(0xD4); }
    I cheats_on() const { return r32(0xD8); }
    void set_cheats_on(I v) { w32(0xD8, v & cheats_unlocked()); }
    I percent() const;  // the save's title: levels rated over 24, at most 99

    // level_begin(1) (0x80062108) for the slot's level (the mission select or CONTINUE): the save
    // data's mission and level, slot 7's third level as LVL4 (its row copied), cheats but 0 and 1
    // off for a level never completed. The mission and level directories (1-based) out.
    void level_begin(int slot, int level, int* msn, int* lvl);
    void level_begin_continue(int* msn, int* lvl) { level_begin(I(r32(0xA8)), slot_level[size_t(r32(0xA8))], msn, lvl); }

    // After the level: level_rating (0x8007344C), then progress_commit (0x800622D8: the mission's
    // done flag, carry_to_next_level, career_add, medals_award, mission_rewards, campaign_advance or
    // the next level unlocked). Returns the medals newly earned.
    I rate(const LevelResults& r);
    I progress_commit(const LevelResults& r, bool advance);

    // Options > PASSWORD (password_enter 0x80048658): the entry's index, -1 unknown, -2 refused.
    // Its effect applied; what it does in words out.
    I password(const std::string& word, std::string* what);

    // The memory card file (shell.md, The save): 0x2000 bytes, `SC`, the title, g_game at +0x200.
    std::vector<uint8_t> save_file() const;
    std::string save_title() const;
    bool load_file(const std::vector<uint8_t>& file, std::string* err);  // load_screen_update + load_apply

private:
    I r32(size_t o) const { return I(int32_t(game[o] | game[o + 1] << 8 | game[o + 2] << 16 | uint32_t(game[o + 3]) << 24)); }
    void w32(size_t o, I v) {
        for (int k = 0; k < 4; k++) game[o + size_t(k)] = uint8_t(U(v) >> (8 * k));
    }
    I r16(size_t o) const { return I(int16_t(game[o] | game[o + 1] << 8)); }
    void w16(size_t o, I v) { game[o] = uint8_t(v), game[o + 1] = uint8_t(U(v) >> 8); }
    I medals_check(bool committed) const;
    bool mission_all_two_stars(bool committed) const;
    void run_password(int idx);
};

}  // namespace valor::sim
