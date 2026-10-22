// The campaign and the shell's rules (campaign.h), from SHELL.BIN.
#include "valor/sim/campaign.h"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace valor::sim {

namespace {
// g_campaign (campaign_init 0x80060C7C): mission index, level count
constexpr int kSlot[8][2] = {{0, 3}, {1, 4}, {2, 4}, {3, 3}, {4, 4}, {6, 3}, {7, 3}, {8, 3}};
// level_rating's kill requirements (SHELL 0x8003BA40), per slot and campaign level
constexpr I kKills[8][4] = {{21, 49, 40, 9999}, {5, 9, 35, 17},  {29, 24, 37, 25}, {68, 64, 51, 9999},
                            {49, 40, 46, 43},   {49, 32, 30, 51}, {45, 53, 26, 26}, {45, 53, 26, 26}};
// g_passwords (0x8007B680), in their order (shell.md, Passwords)
const char* const kWords[61] = {
    "DENNISMODE", "SPRECHEN",   "BRADALARM",  "TRACERON",  "ICOSIDODEC", "GOBLUE",     "BADCOPSHOW", "MOSTMEDALS",
    "INVASION",   "BIGGRETA",   "DASBOOT",    "STUKA",     "KOMET",      "TWOSIXTWO",  "MISSLEAGE",  "VICTORYDAY",
    "COOLCHICK",  "AJRULES",    "DWIGALLERY", "DWIMOHTEAM", "RETTUNG",   "ZERSTOREN",  "BOOTSINKT",  "SENFGAS",
    "SCHWERES",   "SICHERUNG",  "EINSICKERN", "GESAMTHEIT", "GUNTHER",   "JIMMY",      "ROBBIE",     "MANON",
    "ULF",        "HELMUT",     "HEINRICH",   "WECANDOIT", "EVILKURT",   "INTHENAVY",  "TIREDJOHAN", "DASBOOTY",
    "MANLYMAN",   "SCARYKARL",  "HOODDOWN",   "ROCKETMAN", "ARTCRITIC",  "COOLSHADES", "HOODUP",     "BIGFATMAN",
    "FINESTHOUR", "HERRZOMBIE", "BEACHBALL",  "WOOFWOOF",  "IKEANDKEN",  "NUTCRACKER", "SSPIELBERG", "PAYBACK",
    "MACOOCOO",   "SPYSCHOOL",  "SMPSMDMILK", "URLINGAMBE", "CAPTAINDYE"};
const char* const kCheats[8] = {"multiplayer power-ups", "the other language's sounds", "silly alarms", "wireframe world",
                                "faster refire",         "larger bullets",             "unlimited ammo", "no damage"};
constexpr size_t G_LEVEL_ROWS = 0x18C;
}  // namespace

int Campaign::slot_mission(int s) { return kSlot[s][0]; }
int Campaign::slot_levels(int s) { return kSlot[s][1]; }

size_t Campaign::row(int slot, int level) const { return G_LEVEL_ROWS + size_t(slot_mission(slot)) * 0x98 + size_t(level) * 0x24; }
I Campaign::best_rating(int slot, int level) const { return r16(row(slot, level) + 0x1A); }
bool Campaign::unlocked(int slot, int level) const { return r32(row(slot, level) + 0x20) != 0; }
bool Campaign::mission_complete(int slot) const { return r32(G_LEVEL_ROWS + size_t(slot_mission(slot)) * 0x98 + 0x94) != 0; }

Campaign::Campaign() : game(0x780, 0) {
    // game_defaults (0x80060B0C)
    w32(0x9C, 5);
    w32(0x80, 4);
    w32(0x88, 4);
    game[0x113] = 1;
    game[0x128] = 1;
    w32(0xE4, 1), w32(0xE0, 1);
    w32(0xF8, 1), w32(0xF4, 1);
    w32(0x108, 1), w32(0x10C, 1);
    new_game("noname");
}

void Campaign::new_game(const std::string& name) {
    slot_level = {};  // campaign_init
    std::memset(&game[4], 0, 11);
    std::string n = name.substr(0, 10);
    for (size_t k = 0; k < n.size(); k++) game[4 + k] = uint8_t(std::toupper(uint8_t(n[k])));
    std::memcpy(&game[0], "MOH", 4);
    std::memcpy(&game[0x77C], "MOH", 4);
    w32(0x9C, 5);
    level_exit = 4;
    w32(0x78, 0x7F);
    w32(0x7C, 0x1F);
    w32(0x94, 0), w32(0x98, 0);
    for (size_t o = 0x10; o <= 0x3C; o += 4) w32(o, 0);
    w32(0xD0, 0), w32(0xD4, 0), w32(0xD8, 0);
    for (int ms = 0; ms < 10; ms++) {
        size_t b = G_LEVEL_ROWS + size_t(ms) * 0x98;
        w32(b + 0x90, 0), w32(b + 0x94, 0);
        for (int l = 0; l < 4; l++) {
            size_t rr = b + size_t(l) * 0x24;
            for (int k = 0; k < 13; k++) w16(rr + 2 * size_t(k), -1);
            w16(rr + 0x1A, -1);
            w32(rr + 0x1C, l == 0 ? 0x800000 : 0);
            w32(rr + 0x20, 0);
        }
    }
    w32(G_LEVEL_ROWS + 0x20, 1);  // MSN1 LVL1
    for (size_t o = 0x40; o <= 0x70; o += 4) w32(o, 0);
    w32(0xA0, 0), w32(0xA4, 0), w32(0xA8, 0), w32(0xAC, 0), w32(0x74, 0);
    rating_slot = rating_level = 0;
}

std::string Campaign::codename() const {
    std::string s;
    for (size_t k = 4; k < 15 && game[k]; k++) s += char(game[k]);
    return s;
}

I Campaign::percent() const {
    I n = 0;
    for (int s = 0; s < 8; s++)
        for (int l = 0; l < 4; l++)
            if (r16(G_LEVEL_ROWS + size_t(slot_mission(s)) * 0x98 + size_t(l) * 0x24 + 0x1A) >= 0) n++;
    return std::min<I>(n * 100 / 24, 99);
}

void Campaign::level_begin(int slot, int level, int* msn, int* lvl) {
    if (slot == 6) slot = 7;
    w32(0xA8, slot);
    slot_level[size_t(slot)] = level;
    rating_slot = slot, rating_level = level;
    w32(0x98, level);
    if (slot == 7 && level == 2) {
        // MSN9 has LVL1, LVL2 and LVL4: the third row copied to the fourth
        w32(0x98, 3);
        std::memmove(&game[row(7, 3)], &game[row(7, 2)], 0x24);
    }
    w32(0x94, slot_mission(slot));
    if (r16(G_LEVEL_ROWS + size_t(r32(0x94)) * 0x98 + size_t(r32(0x98)) * 0x24 + 0x1A) == -1) w32(0xD8, r32(0xD8) & 3);
    level_exit = 4;
    if (msn) *msn = int(r32(0x94)) + 1;
    if (lvl) *lvl = int(r32(0x98)) + 1;
}

I Campaign::rate(const LevelResults& r) {
    I rating = 0;
    if (r.primary_complete && U(r.kills()) >= U(kKills[rating_slot][rating_level])) {
        rating = 1;
        if (double(r.health) / double(0x800000) > 0.75) rating = 2;
    }
    new_rating = rating;
    return rating;
}

// medals_check (0x80062C6C).
I Campaign::medals_check(bool committed) const {
    I news = 0, medals_n = 0, missions_done = 0, all_two = 0;
    for (int s = 0; s < 8; s++) {
        if (s == 6) continue;
        I two = 0;
        for (int l = 0; l < slot_levels(s); l++) {
            I v = (s != rating_slot || l != rating_level || committed) ? r16(row(s, l) + 0x1A) : new_rating;
            if (v == 2) two++;
        }
        bool done_now = (s == rating_slot && rating_level + 1 >= slot_levels(s)) || mission_complete(s);
        if (done_now) missions_done++;
        if (two + 1 >= slot_levels(s) && done_now) {
            medals_n++;
            if (!(medals() & (I(1) << s))) news |= I(1) << s;
            if (two >= slot_levels(s)) all_two++;
        }
    }
    bool msn9 = (rating_level == 2 && rating_slot == 7) || mission_complete(7);
    if (missions_done >= 7 && msn9) {
        I md = medals();
        if (!(md & 0x200)) news |= 0x200;
        if (medals_n >= 4 && !(md & 0x400)) news |= 0x400;
        if (medals_n >= 7 && !(md & 0x800)) news |= 0x800;
        if ((all_two >= 7 || r32(0x74)) && !(md & 0x100)) news |= 0x100;
    }
    return news;
}

// mission_all_two_stars (0x80062B48): every level of the slot at two stars, its cheat not yet open.
bool Campaign::mission_all_two_stars(bool committed) const {
    bool all = true;
    for (int l = 0; l < slot_levels(rating_slot); l++) {
        I v = (l != rating_level || committed) ? r16(row(rating_slot, l) + 0x1A) : new_rating;
        if (v != 2) all = false;
    }
    if (!all) return false;
    I bit = rating_slot >= 7 ? rating_slot - 1 : rating_slot;
    return !(cheats_unlocked() & (I(1) << bit));
}

I Campaign::progress_commit(const LevelResults& r, bool advance) {
    level_exit = r.exit_code;
    if (level_exit != 0 && level_exit != 3) return 0;
    int s = int(rating_slot);
    size_t block = G_LEVEL_ROWS + size_t(slot_mission(s)) * 0x98;
    if (rating_level + 1 >= slot_levels(s)) w32(block + 0x94, 1);
    // carry_to_next_level (0x800625BC)
    if (rating_level + 1 < slot_levels(s)) {
        size_t nr = row(s, int(rating_level + 1));
        for (int k = 0; k < 13; k++) w16(nr + 2 * size_t(k), r.ammo[size_t(k)]);
        w32(nr + 0x1C, r.health);
    }
    // career_add (0x80062694)
    const auto& st = r.stats;
    auto add = [&](size_t o, I v) { w32(o, r32(o) + v); };
    add(0x10, st[0]);
    add(0x18, st[2]);
    add(0x14, st[1]);
    add(0x20, st[4]);
    add(0x1C, st[0x1A]);
    add(0x24, st[6]);
    add(0x28, st[7] - st[0x11]);
    add(0x2C, st[0x11]);
    add(0x30, st[0x13] + st[0x15] + st[0x17] + st[0x19]);
    add(0x34, st[0xD] + st[0xF]);
    add(0x38, st[0x12] + st[0x14] + st[0x16] + st[0x18]);
    add(0x3C, st[0xC] + st[0xE]);
    size_t rr = row(s, int(rating_level));
    if (r16(rr + 0x1A) < new_rating) w16(rr + 0x1A, new_rating);
    for (int k = 0; k < 13; k++) add(0x40 + 4 * size_t(k), r.weapon_kills[size_t(k)]);
    w32(0xA0, 1);
    // medals_award (0x800628A4)
    I news = medals_check(true);
    if (news) {
        w32(0xD0, medals() | news);
        if (news & 0x800) w32(0xD4, cheats_unlocked() | 0x80);
        if (news & 0x100) w32(0x78, r32(0x78) | 0x0FE00000);
    }
    // mission_rewards (0x8006294C)
    if (mission_all_two_stars(true)) w32(0xD4, cheats_unlocked() | (I(1) << (s >= 7 ? s - 1 : s)));
    if (rating_level + 1 >= slot_levels(s)) {
        w32(block + 0x90, 1);
        static const I chars[8] = {0, 0x180, 0x1E00, 0x2000, 0xC000, 0xF0000, 0, 0x100000};
        w32(0x78, r32(0x78) | chars[s]);
        if (s == 3) w32(0x7C, r32(0x7C) | 0x20);
        if (s == 7) w32(0x7C, r32(0x7C) | 0x40);
    }
    if (advance) {
        // campaign_advance (0x80062498)
        int cs = int(r32(0xA8));
        I lv = ++slot_level[size_t(cs)];
        if (lv >= slot_levels(cs)) {
            int ns = cs + 1 == 6 ? cs + 2 : cs + 1;
            if (ns >= 8) {
                cs = 7;
                slot_level[7] = slot_levels(7) - 1;
                lv = slot_level[7];
            } else
                cs = ns, slot_level[size_t(cs)] = 0, lv = 0;
            w32(0xA8, cs);
        }
        w32(0xAC, slot_level[size_t(cs)]);
        w32(row(cs, int(lv)) + 0x20, 1);
    } else {
        int ns = s;
        I nl = rating_level + 1;
        if (nl >= slot_levels(s)) nl = 0, ns = s == 5 ? 7 : s + 1;
        if (ns < 8) w32(row(ns, int(nl)) + 0x20, 1);
    }
    level_exit = 5;
    return news;
}

void Campaign::run_password(int i) {
    if (i < 8)
        w32(0xD4, cheats_unlocked() | (I(1) << i));
    else if (i < 16)
        w32(G_LEVEL_ROWS + size_t(slot_mission(i - 8)) * 0x98 + 0x90, 1);
    else if (i < 20) {
        // the DreamWorks slide shows: nothing kept
    } else if (i < 28) {
        if (i - 20 != 6) w32(row(i - 20, 0) + 0x20, 1);
    } else if (i < 56)
        w32(0x78, r32(0x78) | (I(1) << (i - 28)));
    else if (i < 58)
        w32(0x7C, r32(0x7C) | (I(1) << (i - 56 + 5)));
    else if (i == 60)
        w32(0x74, 1);
}

I Campaign::password(const std::string& word, std::string* what) {
    std::string w;
    for (char c : word) w += char(std::toupper(uint8_t(c)));
    int i = -1;
    for (int k = 0; k < 61; k++)
        if (w == kWords[k]) i = k;
    if (i < 0) return -1;
    std::string out;
    if (i == 60) {
        bool rated = false;
        for (int s = 0; s < 8; s++)
            for (int l = 0; l < 4; l++) rated |= r16(G_LEVEL_ROWS + size_t(slot_mission(s)) * 0x98 + size_t(l) * 0x24 + 0x1A) >= 0;
        if (rated) return -2;
        out = "health carries over from level to level";
    } else if (i == 59) {
        if (!smpsmdmilk) return -2;
        for (int k = 0; k < 58; k++) run_password(k);
        out = "everything unlocked";
    } else if (i == 58) {
        smpsmdmilk = true;
        out = "armed";
    } else if (i < 8)
        out = std::string("cheat unlocked: ") + kCheats[i];
    else if (i < 16)
        out = "mission film unlocked";
    else if (i < 20)
        out = "the DreamWorks gallery (not reproduced)";
    else if (i < 28)
        out = "mission " + std::to_string(slot_mission(i - 20) + 1) + " unlocked";
    else
        out = "multiplayer unlock";
    if (i != 58 && i != 59) run_password(i);
    if (what) *what = out;
    return i;
}

std::string Campaign::save_title() const {
    std::string n = codename();
    n.resize(10, ' ');
    return "MOH-" + n + " (" + std::to_string(percent()) + "%)";
}

std::vector<uint8_t> Campaign::save_file() const {
    std::vector<uint8_t> f(0x2000, 0);
    f[0] = 'S', f[1] = 'C', f[2] = 0x13, f[3] = 1;
    // the title in Shift-JIS full-width (0x800462CC's conversion of ASCII)
    std::string t = save_title();
    size_t o = 4;
    for (char c : t) {
        if (o + 2 > 0x44) break;
        uint16_t sj;
        if (c == ' ')
            sj = 0x8140;
        else if (c >= '0' && c <= '9')
            sj = uint16_t(0x824F + (c - '0'));
        else if (c >= 'A' && c <= 'Z')
            sj = uint16_t(0x8260 + (c - 'A'));
        else if (c == '-')
            sj = 0x817C;
        else if (c == '(')
            sj = 0x8169;
        else if (c == ')')
            sj = 0x816A;
        else if (c == '%')
            sj = 0x8193;
        else
            sj = 0x8140;
        f[o] = uint8_t(sj >> 8), f[o + 1] = uint8_t(sj);
        o += 2;
    }
    std::copy(game.begin(), game.end(), f.begin() + 0x200);
    return f;
}

bool Campaign::load_file(const std::vector<uint8_t>& f, std::string* err) {
    if (f.size() < 0x200 + 0x780) {
        *err = "too short";
        return false;
    }
    auto moh = [&](size_t o) { return std::toupper(f[o]) == 'M' && std::toupper(f[o + 1]) == 'O' && std::toupper(f[o + 2]) == 'H'; };
    I lvl = I(int32_t(f[0x200 + 0x98] | f[0x200 + 0x99] << 8 | f[0x200 + 0x9A] << 16 | uint32_t(f[0x200 + 0x9B]) << 24));
    if (!moh(0x200) || !moh(0x200 + 0x77C) || lvl >= 8) {
        *err = "not a Medal of Honor save";
        return false;
    }
    std::copy(f.begin() + 0x200, f.begin() + 0x200 + 0x780, game.begin());
    // load_apply (0x8006EB8C)
    level_exit = 5;
    rating_slot = r32(0xA8);
    rating_level = r32(0xAC);
    slot_level = {};
    if (rating_slot >= 0 && rating_slot < 8) slot_level[size_t(rating_slot)] = rating_level;
    return true;
}

}  // namespace valor::sim
