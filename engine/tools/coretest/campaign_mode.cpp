// coretest --campaign: the shell's rules (sim/campaign.h) run through a whole campaign with
// perfect results, then with poor ones, and the passwords: what each step changes in the save data.
#include <cstdio>
#include <string>

#include "valor/sim/campaign.h"

using namespace valor::sim;

int run_campaign(int, char**) {
    int round_trip_bad = 0;
    for (int pass = 0; pass < 2; pass++) {
        Campaign c;
        c.new_game("tester");
        std::printf("%s campaign, codename %s\n", pass ? "a poor" : "a perfect", c.codename().c_str());
        for (int n = 0; n < 30; n++) {
            int msn, lvl;
            c.level_begin_continue(&msn, &lvl);
            LevelResults r;
            r.exit_code = 0;
            r.primary_complete = true;
            r.health = pass ? 0x200000 : 0x800000;  // 32.0 or 128.0
            r.stats[4] = pass ? 0 : 99;               // kills
            I rating = c.rate(r);
            I slot = c.rating_slot, lv = c.rating_level;
            I medals = c.progress_commit(r, true);
            // saved here and loaded (after a power cycle): CONTINUE starts the same level on the
            // same save data as playing straight on
            Campaign on = c, back;
            std::string err;
            bool loaded = back.load_file(c.save_file(), &err);
            int m1, l1, m2, l2;
            on.level_begin_continue(&m1, &l1);
            if (loaded) back.level_begin_continue(&m2, &l2);
            bool same = loaded && m1 == m2 && l1 == l2 && on.game == back.game;
            if (!same) round_trip_bad++;
            std::printf("  slot %lld level %lld = MSN%d LVL%d: rating %lld, new medals %03llX; save %s%s\n", (long long)slot,
                        (long long)lv, msn, lvl, (long long)rating, (unsigned long long)medals, c.save_title().c_str(),
                        same ? "" : (" ; SAVE/LOAD DIFFERS " + (loaded ? std::string("") : err)).c_str());
            if (slot == 7 && lv == 2) break;
        }
        std::printf("  medals %03llX, cheats unlocked %02llX\n", (unsigned long long)c.medals(), (unsigned long long)c.cheats_unlocked());
    }
    Campaign c;
    for (const char* w : {"INVASION", "badcopshow", "URLINGAMBE", "SMPSMDMILK", "URLINGAMBE", "NOTAWORD", "CAPTAINDYE"}) {
        std::string what;
        I i = c.password(w, &what);
        std::printf("password %-10s -> %lld %s\n", w, (long long)i, what.c_str());
    }
    std::string err;
    Campaign d;
    bool ok = d.load_file(c.save_file(), &err);
    std::printf("save and load: %s, %s, cheats unlocked %02llX\n", ok ? "ok" : err.c_str(), d.save_title().c_str(),
                (unsigned long long)d.cheats_unlocked());
    std::printf("save, load, continue against playing on: %s\n", round_trip_bad ? "DIFFERS" : "the same at every level");
    return round_trip_bad ? 1 : 0;
}
