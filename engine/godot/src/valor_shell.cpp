#include "valor_shell.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <filesystem>
#include <fstream>

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/variant/packed_int32_array.hpp>

#include "valor/io.h"

namespace godot {

void ValorShell::_bind_methods() {
    ClassDB::bind_method(D_METHOD("new_game", "codename"), &ValorShell::new_game);
    ClassDB::bind_method(D_METHOD("get_codename"), &ValorShell::get_codename);
    ClassDB::bind_method(D_METHOD("get_slots"), &ValorShell::get_slots);
    ClassDB::bind_method(D_METHOD("get_next"), &ValorShell::get_next);
    ClassDB::bind_method(D_METHOD("begin_level", "slot", "level"), &ValorShell::begin_level);
    ClassDB::bind_method(D_METHOD("get_game"), &ValorShell::get_game);
    ClassDB::bind_method(D_METHOD("set_game", "game"), &ValorShell::set_game);
    ClassDB::bind_method(D_METHOD("commit", "results", "advance"), &ValorShell::commit);
    ClassDB::bind_method(D_METHOD("rate", "results"), &ValorShell::rate);
    ClassDB::bind_method(D_METHOD("get_medals"), &ValorShell::get_medals);
    ClassDB::bind_method(D_METHOD("get_cheats_unlocked"), &ValorShell::get_cheats_unlocked);
    ClassDB::bind_method(D_METHOD("get_cheats_on"), &ValorShell::get_cheats_on);
    ClassDB::bind_method(D_METHOD("set_cheats_on", "bits"), &ValorShell::set_cheats_on);
    ClassDB::bind_method(D_METHOD("password", "word"), &ValorShell::password);
    ClassDB::bind_method(D_METHOD("load_briefing", "data_dir", "mission", "level"), &ValorShell::load_briefing);
    ClassDB::bind_method(D_METHOD("save", "dir"), &ValorShell::save);
    ClassDB::bind_method(D_METHOD("list_saves", "dir"), &ValorShell::list_saves);
    ClassDB::bind_method(D_METHOD("load", "path"), &ValorShell::load);
    ClassDB::bind_method(D_METHOD("get_error"), &ValorShell::get_error);
}

void ValorShell::new_game(const String& codename) { campaign_.new_game(codename.utf8().get_data()); }
String ValorShell::get_codename() const { return String(campaign_.codename().c_str()); }

Array ValorShell::get_slots() const {
    using valor::sim::Campaign;
    Array out;
    for (int s = 0; s < Campaign::kSlots; s++) {
        if (!Campaign::slot_on_disc(s)) continue;
        Dictionary d;
        d["slot"] = s;
        d["mission"] = Campaign::slot_mission(s) + 1;
        d["complete"] = campaign_.mission_complete(s);
        Array levels;
        for (int l = 0; l < Campaign::slot_levels(s); l++) {
            Dictionary ld;
            ld["level"] = l;
            ld["dir"] = (s == 7 && l == 2) ? 4 : l + 1;
            ld["unlocked"] = campaign_.unlocked(s, l);
            ld["best"] = int64_t(campaign_.best_rating(s, l));
            levels.push_back(ld);
        }
        d["levels"] = levels;
        out.push_back(d);
    }
    return out;
}

Dictionary ValorShell::get_next() const {
    Dictionary d;
    int s = int(campaign_.game[0xA8]);
    d["slot"] = s;
    d["level"] = int64_t(campaign_.slot_level[size_t(s)]);
    return d;
}

Dictionary ValorShell::begin_level(int slot, int level) {
    int m = 1, l = 1;
    campaign_.level_begin(slot, level, &m, &l);
    Dictionary d;
    d["mission"] = m;
    d["level"] = l;
    return d;
}

PackedByteArray ValorShell::get_game() const {
    PackedByteArray b;
    b.resize(int64_t(campaign_.game.size()));
    memcpy(b.ptrw(), campaign_.game.data(), campaign_.game.size());
    return b;
}

void ValorShell::set_game(const PackedByteArray& b) {
    if (b.size() != int64_t(campaign_.game.size())) return;
    memcpy(campaign_.game.data(), b.ptr(), campaign_.game.size());
}

namespace {
valor::sim::LevelResults results_of(const Dictionary& rd) {
    valor::sim::LevelResults r;
    r.exit_code = int64_t(rd.get("exit", 1));
    r.objectives = int64_t(rd.get("objectives", 0));
    r.done = int64_t(rd.get("done", 0));
    r.primary_complete = bool(rd.get("primary_complete", false));
    r.health = int64_t(rd.get("health", 0));
    PackedInt32Array ammo = rd.get("ammo", PackedInt32Array()), wk = rd.get("weapon_kills", PackedInt32Array()),
                     st = rd.get("stats", PackedInt32Array());
    for (int k = 0; k < 13 && k < ammo.size(); k++) r.ammo[size_t(k)] = ammo[k], r.weapon_kills[size_t(k)] = k < wk.size() ? wk[k] : 0;
    for (int k = 0; k < int(r.stats.size()) && k < st.size(); k++) r.stats[size_t(k)] = st[k];
    return r;
}
}  // namespace

int64_t ValorShell::rate(const Dictionary& rd) { return campaign_.rate(results_of(rd)); }

Dictionary ValorShell::commit(const Dictionary& rd, bool advance) {
    valor::sim::LevelResults r = results_of(rd);
    Dictionary out;
    valor::sim::I rating = (r.exit_code == 0 || r.exit_code == 3) ? campaign_.rate(r) : -1;
    out["rating"] = int64_t(rating);
    out["medals"] = int64_t(campaign_.progress_commit(r, advance));
    return out;
}

Dictionary ValorShell::password(const String& word) {
    std::string what;
    int64_t i = campaign_.password(word.utf8().get_data(), &what);
    Dictionary d;
    d["index"] = i;
    d["text"] = String(what.c_str());
    return d;
}

namespace {
// An 8-bit (or 4-bit) TIM with its CLUT into RGBA8 (formats.md, TIM).
bool tim_rgba(const uint8_t* t, size_t n, int& w, int& h, std::vector<uint8_t>& rgba) {
    auto u32 = [&](size_t o) { return uint32_t(t[o] | t[o + 1] << 8 | t[o + 2] << 16 | uint32_t(t[o + 3]) << 24); };
    auto u16 = [&](size_t o) { return uint32_t(t[o] | t[o + 1] << 8); };
    if (n < 20 || u32(0) != 0x10) return false;
    uint32_t flag = u32(4), bpp = flag & 3;
    size_t o = 8;
    std::vector<uint16_t> clut;
    if (flag & 8) {
        uint32_t len = u32(o), cw = u16(o + 8), ch = u16(o + 10);
        for (uint32_t i = 0; i < cw * ch && o + 12 + 2 * i + 1 < n; i++) clut.push_back(uint16_t(u16(o + 12 + 2 * i)));
        o += len;
    }
    uint32_t len = u32(o), ww = u16(o + 8);
    h = int(u16(o + 10));
    w = int(bpp == 0 ? ww * 4 : bpp == 1 ? ww * 2 : ww);
    const uint8_t* px = t + o + 12;
    size_t avail = std::min<size_t>(len - 12, n - o - 12);
    rgba.assign(size_t(w) * size_t(h) * 4, 0);
    for (int i = 0; i < w * h; i++) {
        uint32_t c;
        if (bpp == 1) {
            if (size_t(i) >= avail) break;
            c = px[i] < clut.size() ? clut[px[i]] : 0;
        } else if (bpp == 0) {
            if (size_t(i / 2) >= avail) break;
            uint32_t ix = (px[i / 2] >> ((i & 1) * 4)) & 15;
            c = ix < clut.size() ? clut[ix] : 0;
        } else {
            if (size_t(2 * i + 1) >= avail) break;
            c = uint32_t(px[2 * i] | px[2 * i + 1] << 8);
        }
        rgba[size_t(i) * 4] = uint8_t((c & 31) << 3);
        rgba[size_t(i) * 4 + 1] = uint8_t(((c >> 5) & 31) << 3);
        rgba[size_t(i) * 4 + 2] = uint8_t(((c >> 10) & 31) << 3);
        rgba[size_t(i) * 4 + 3] = 255;
    }
    return true;
}
}  // namespace

Array ValorShell::load_briefing(const String& data_dir, int mission, int level) {
    Array pages;
    std::string path = std::string(data_dir.utf8().get_data()) + "/SCR2/MBRIEF/M" + std::to_string(mission) + "/" + std::to_string(level) + ".RSC";
    std::vector<uint8_t> d;
    if (!valor::read_file(path, d) || d.size() < 0x48) {
        error_ = String(("no briefing " + path).c_str());
        return pages;
    }
    auto u32 = [&](size_t o) { return uint32_t(d[o] | d[o + 1] << 8 | d[o + 2] << 16 | uint32_t(d[o + 3]) << 24); };
    uint32_t count = u32(0x40);
    // RSC: name[0x40], count, ?, then {name[0x40], index, size, offset, 0}
    std::map<std::string, std::pair<uint32_t, uint32_t>> tims;
    for (uint32_t i = 0; i < count && 0x48 + i * 0x50 + 0x50 <= d.size(); i++) {
        size_t e = 0x48 + i * 0x50;
        std::string name(reinterpret_cast<const char*>(&d[e]), strnlen(reinterpret_cast<const char*>(&d[e]), 0x40));
        tims[name.substr(name.rfind('\\') + 1)] = {u32(e + 0x48), u32(e + 0x44)};
    }
    for (int p = 1; p <= 3; p++) {
        auto l = tims.find(std::to_string(p) + "l.tim"), r = tims.find(std::to_string(p) + "r.tim");
        if (l == tims.end() || r == tims.end()) continue;
        int lw, lh, rw, rh;
        std::vector<uint8_t> a, b;
        if (!tim_rgba(&d[l->second.first], l->second.second, lw, lh, a) || !tim_rgba(&d[r->second.first], r->second.second, rw, rh, b)) continue;
        int W = lw + rw, H = std::max(lh, rh);
        PackedByteArray px;
        px.resize(int64_t(W) * H * 4);
        uint8_t* q = px.ptrw();
        memset(q, 0, size_t(px.size()));
        for (int y = 0; y < H; y++) {
            if (y < lh) memcpy(q + (size_t(y) * W) * 4, &a[size_t(y) * lw * 4], size_t(lw) * 4);
            if (y < rh) memcpy(q + (size_t(y) * W + lw) * 4, &b[size_t(y) * rw * 4], size_t(rw) * 4);
        }
        pages.push_back(Image::create_from_data(W, H, false, Image::FORMAT_RGBA8, px));
    }
    return pages;
}

String ValorShell::save(const String& dir) {
    namespace fs = std::filesystem;
    std::string d = dir.utf8().get_data();
    std::error_code ec;
    fs::create_directories(d, ec);
    // the card's naming: BASLUS-00974 + the codename's first 6 letters + 1 + the highest number
    std::string base = campaign_.codename().substr(0, 6);
    base.resize(6, ' ');
    base = "BASLUS-00974" + base;
    int n = 0;
    for (auto& e : fs::directory_iterator(d, ec)) {
        std::string f = e.path().filename().string();
        if (f.rfind(base, 0) == 0 && f.size() > base.size() + 4) n = std::max(n, std::atoi(f.c_str() + base.size()) + 1);
    }
    std::string path = d + "/" + base + std::to_string(n) + ".sav";
    std::vector<uint8_t> file = campaign_.save_file();
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(file.data()), std::streamsize(file.size()));
    if (!out) {
        error_ = String(("can't write " + path).c_str());
        return "";
    }
    return String(path.c_str());
}

Array ValorShell::list_saves(const String& dir) const {
    namespace fs = std::filesystem;
    Array out;
    std::error_code ec;
    for (auto& e : fs::directory_iterator(std::string(dir.utf8().get_data()), ec)) {
        if (e.path().extension() != ".sav") continue;
        std::vector<uint8_t> f;
        if (!valor::read_file(e.path().string(), f)) continue;
        valor::sim::Campaign c;
        std::string err;
        if (!c.load_file(f, &err)) continue;
        Dictionary d;
        d["path"] = String(e.path().string().c_str());
        d["title"] = String(c.save_title().c_str());
        out.push_back(d);
    }
    return out;
}

bool ValorShell::load(const String& path) {
    std::vector<uint8_t> f;
    std::string err;
    if (!valor::read_file(path.utf8().get_data(), f) || !campaign_.load_file(f, &err)) {
        error_ = String(("can't load: " + err).c_str());
        return false;
    }
    return true;
}

}  // namespace godot
