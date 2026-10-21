#include "valor/sim/feed.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <map>

namespace valor::sim {

namespace {

bool starts(const std::string& s, const char* p) { return s.rfind(p, 0) == 0; }

}  // namespace

std::vector<U> Mem::entities() const {
    std::vector<U> out;
    U e = U(u32(G_ENTITIES));
    while (e && out.size() < 1024) {
        out.push_back(e);
        e = U(u32(e + 4));
    }
    return out;
}

bool Functions::load(const std::string& root) {
    for (const char* t : {"exe", "level"}) {
        std::ifstream f(root + "/build/cmake/gen/game/" + t + "_table.c");
        if (!f) return false;
        std::string line;
        while (std::getline(f, line)) {
            size_t p = line.find("{0x");
            if (p == std::string::npos) continue;
            char* end = nullptr;
            unsigned long v = std::strtoul(line.c_str() + p + 3, &end, 16);
            if (end && *end == 'u') starts.push_back(U(v) | 0x80000000u);
        }
    }
    std::sort(starts.begin(), starts.end());
    return true;
}

U Functions::of(U addr) const {
    auto it = std::upper_bound(starts.begin(), starts.end(), addr);
    if (it == starts.begin()) return 0;
    return *(it - 1);
}

bool Feed::load(const std::string& root, const std::string& log_path) {
    if (!fns.load(root)) return false;
    std::ifstream f(log_path);
    if (!f) return false;
    std::string line, level;
    // a level's first ticks are logged before its mark: lines of ticks 0 and 1 after the level's
    // later ticks are the next level's ("+" until its mark)
    I last_tick = 0;
    auto key = [&](I tick) {
        if (tick <= 1 && last_tick > 1) return std::string("+");
        last_tick = std::max(last_tick, tick);
        return level;
    };
    auto adopt = [&](auto& map, const std::string& from) {
        for (auto it = map.begin(); it != map.end();) {
            if (it->first.first == from) {
                map[{level, it->first.second}] = it->second;
                it = map.erase(it);
            } else
                ++it;
        }
    };
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        char c = line[0];
        if (c == 'r') {
            long long vb, tick, val;
            unsigned ra;
            if (std::sscanf(line.c_str(), "rand %lld %lld: %X %lld", &vb, &tick, &ra, &val) == 4 && starts(line, "rand ")) {
                rand[{key(tick), tick}].push_back({fns.of(ra), ra, val, vb});
                continue;
            }
            int port, role, src;
            long long v;
            if (starts(line, "role ") && std::sscanf(line.c_str(), "role %lld: %d %d %d %lld", &tick, &port, &role, &src, &v) == 5) {
                roles[{key(tick), tick}].insert({{port, role, src}, v});  // the tick's own poll first
                continue;
            }
            int mm, ml;
            if (starts(line, "replay: mark mission ") &&
                std::sscanf(line.c_str(), "replay: mark mission %d %d at vblank %lld", &mm, &ml, &vb) == 3) {
                level = std::to_string(mm) + ":" + std::to_string(ml);
                marks.push_back({vb, level});
                for (const char* from : {"", "+"}) adopt(rand, from), adopt(roles, from), adopt(pad, from);
                last_tick = 0;
            }
        } else if (c == 'i' && starts(line, "input: game set pad 1 to analog mode")) {
            if (!level.empty()) analog.insert(level);
        } else if (c == 'i' && starts(line, "idlef ")) {
            // frame_wait_vblank: a frame's idle work starts
            idle[{level, std::atoll(line.c_str() + 6)}].emplace_back();
        } else if (c == 'i' && starts(line, "idle2 ")) {
            auto& fr = idle[{level, std::atoll(line.c_str() + 6)}];
            if (fr.empty()) fr.emplace_back();
            fr.back().push_back(-1);
        } else if (c == 'i' && starts(line, "idle ")) {
            long long t, st;
            if (std::sscanf(line.c_str(), "idle %lld: %lld", &t, &st) == 2 || std::sscanf(line.c_str(), "idle %lld %lld", &t, &st) == 2) {
                auto& fr = idle[{level, t}];
                if (fr.empty()) fr.emplace_back();
                fr.back().push_back(st);
            }
        } else if (c == 'i' && starts(line, "input ")) {
            long long v, t;
            unsigned a, b, cc, d;
            if (std::sscanf(line.c_str(), "input %lld %lld: %X %X %X %X", &v, &t, &a, &b, &cc, &d) == 6)
                pad.insert({{key(t), t}, {U(a), U(b), U(cc), U(d)}});  // paused frames poll again
        }
    }
    return true;
}

I Feed::role_alt(const std::string& level, I tick, int port, int role) const {
    auto it = roles.find({level, tick});
    if (it == roles.end()) return 0;
    auto r = it->second.find({port, role, 2});
    return r == it->second.end() ? 0 : r->second;
}

I Feed::role(const std::string& level, I tick, int port, int role) const {
    auto it = roles.find({level, tick});
    if (it == roles.end()) return 0;
    auto a = it->second.find({port, role, 0});
    I v = a == it->second.end() ? 0 : a->second;
    if (v) return v;
    auto b = it->second.find({port, role, 1});
    return b == it->second.end() ? 0 : b->second;
}

void Rand::start_tick(I tick) {
    tick_ = tick;
    auto it = feed_.rand.find({level_, tick});
    left = it == feed_.rand.end() ? std::vector<Draw>{} : it->second;
}

I Rand::draw(const std::vector<U>& functions) {
    for (size_t i = 0; i < left.size(); i++) {
        if (std::find(functions.begin(), functions.end(), left[i].fn) != functions.end()) {
            I v = left[i].value;
            left.erase(left.begin() + i);
            return v;
        }
    }
    misses.push_back({tick_, functions});
    return 0;
}

I Rand::draw(std::initializer_list<U> functions) { return draw(std::vector<U>(functions)); }

const std::vector<U>& FN(const char* name) {
    static const std::map<std::string, std::vector<U>> fn = {
        {"scripts", {0x8003F8F0, 0x8005959C, 0x8005989C}},
        {"hit_reaction", {0x80052084, 0x80072244, 0x800727B4, 0x80072EEC}},
        {"enemy_weapon", {0x8005163C}},
        {"enemy_aim", {0x80055198}},
        {"bullet", {0x80045A78}},
        {"pellet", {0x80045F14}},
        {"fire_gun", {0x8007D5C8}},
        {"player_hit", {0x8007C1D0}},
        {"enemy_hit", {0x80055864}},
        {"player_death", {0x80079D10}},
        {"helmet", {0x80051200}},
        {"mounted_gun", {0x8005DA24}},
        {"grenade", {0x80045808, 0x80044120, 0x80043284, 0x80044470}},
        {"explosion", {0x800719C0}},
        {"alarm", {0x8005EA54}},
        {"casing", {0x80088040, 0x80087FA0}},
        {"hit", {0x80043E38, 0x80043AD4, 0x800523E4}},
        {"sound", {0x80041CE8, 0x80041B04, 0x80058420}},
    };
    static const std::vector<U> none;
    auto it = fn.find(name);
    return it == fn.end() ? none : it->second;
}

// ---------------------------------------------------------------- the state trace

namespace {

struct Json {
    const char* p;
    void ws() {
        while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') p++;
    }
    bool num(I& v) {
        ws();
        char* end = nullptr;
        long long x = std::strtoll(p, &end, 10);
        if (end == p) return false;
        p = end;
        v = x;
        return true;
    }
    bool list(std::vector<I>& out) {
        ws();
        if (*p != '[') return false;
        p++;
        ws();
        if (*p == ']') return p++, true;
        for (;;) {
            I v;
            if (!num(v)) return false;
            out.push_back(v);
            ws();
            if (*p == ',') {
                p++;
                continue;
            }
            if (*p == ']') return p++, true;
            return false;
        }
    }
    bool key(std::string& k) {
        ws();
        if (*p != '"') return false;
        const char* s = ++p;
        while (*p && *p != '"') p++;
        k.assign(s, p);
        if (*p) p++;
        ws();
        if (*p != ':') return false;
        p++;
        return true;
    }
    // Skips any value.
    void skip() {
        ws();
        int depth = 0;
        bool str = false;
        for (; *p; p++) {
            if (str) {
                if (*p == '\\') p++;
                else if (*p == '"') str = false;
                continue;
            }
            if (*p == '"') str = true;
            else if (*p == '[' || *p == '{') depth++;
            else if (*p == ']' || *p == '}') {
                if (depth == 0) return;
                depth--;
                if (depth == 0) {
                    p++;
                    return;
                }
            } else if (*p == ',' && depth == 0)
                return;
        }
    }
};

bool parse_state(const std::string& line, StateLine& s) {
    Json j{line.c_str()};
    j.ws();
    if (*j.p != '{') return false;
    j.p++;
    for (;;) {
        j.ws();
        if (*j.p == '}') return true;
        std::string k;
        if (!j.key(k)) return false;
        if (k == "tick") {
            if (!j.num(s.tick)) return false;
        } else if (k == "vblank") {
            if (!j.num(s.vblank)) return false;
        } else if (k == "player") {
            s.has_player = true;
            if (!j.list(s.player)) return false;
        } else if (k == "ents") {
            j.ws();
            if (*j.p != '[') return false;
            j.p++;
            j.ws();
            if (*j.p == ']')
                j.p++;
            else
                for (;;) {
                    s.ents.emplace_back();
                    if (!j.list(s.ents.back())) return false;
                    j.ws();
                    if (*j.p == ',') {
                        j.p++;
                        continue;
                    }
                    if (*j.p == ']') {
                        j.p++;
                        break;
                    }
                    return false;
                }
        } else
            j.skip();
        j.ws();
        if (*j.p == ',') j.p++;
    }
}

}  // namespace

std::vector<StateLine> read_trace(const std::string& path) {
    std::vector<StateLine> out;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        StateLine s;
        if (!parse_state(line, s)) break;
        out.push_back(std::move(s));
    }
    return out;
}

std::string state_json(const StateLine& s) {
    std::string o = "{\"tick\":" + std::to_string(s.tick) + ",\"vblank\":" + std::to_string(s.vblank);
    auto list = [&](const std::vector<I>& v) {
        o += '[';
        for (size_t i = 0; i < v.size(); i++) o += (i ? "," : "") + std::to_string(v[i]);
        o += ']';
    };
    if (s.has_player) {
        o += ",\"player\":";
        list(s.player);
    }
    o += ",\"ents\":[";
    for (size_t i = 0; i < s.ents.size(); i++) {
        if (i) o += ',';
        list(s.ents[i]);
    }
    o += "]}";
    return o;
}

}  // namespace valor::sim
