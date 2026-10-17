// User settings: loaded from and saved to saves/settings.ini (key = value lines).
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "runtime.h"
#include "settings.h"

namespace psx {

namespace {

const std::filesystem::path FILE_PATH = "saves/settings.ini";

std::string trim(const std::string &s) {
    size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

std::string aspect_name(double aspect) {
    if (aspect == 0) return "window";
    static const struct { int w, h; } known[] = {{4, 3}, {16, 9}, {16, 10}, {21, 9}, {32, 9}};
    for (auto k : known)
        if (std::abs(aspect - double(k.w) / k.h) < 1e-6) return std::to_string(k.w) + ":" + std::to_string(k.h);
    char buf[32];
    snprintf(buf, sizeof buf, "%.4f", aspect);
    return buf;
}

bool parse_aspect(const std::string &s, double &aspect) {
    double w, h;
    if (s == "window") aspect = 0;
    else if (sscanf(s.c_str(), "%lf:%lf", &w, &h) == 2 && w > 0 && h > 0) aspect = w / h;
    else if (double v = atof(s.c_str()); v > 0) aspect = v;
    else return false;
    return true;
}

void settings_load() {
    std::ifstream f(FILE_PATH);
    std::string line;
    while (std::getline(f, line)) {
        auto eq = line.find('=');
        if (line.empty() || line[0] == '#' || eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq)), value = trim(line.substr(eq + 1));
        if (key == "scale") config.scale = std::clamp(atoi(value.c_str()), 0, 16);
        else if (key == "aspect") parse_aspect(value, config.aspect);
        else if (key == "fov") config.fov = std::clamp(atof(value.c_str()), 0.0, 150.0);
        else if (key == "dither") config.dither = value == "true";
        else if (key == "smooth_scaling") config.smooth_scaling = value == "true";
        else if (key == "fullscreen") config.fullscreen = value == "true";
        else if (key == "volume") config.volume = std::clamp(atoi(value.c_str()), 0, 200);
        else if (key == "overlay") config.overlay = std::clamp(atoi(value.c_str()), 0, 2);
    }
}

void settings_save() {
    if (config.test_run) return;
    std::filesystem::create_directories(FILE_PATH.parent_path());
    std::ofstream f(FILE_PATH);
    f << "# Valor settings (also editable in game: F1)\n";
    f << "scale = " << config.scale << "  # 0 = fit the display, 1 = original software renderer\n";
    f << "aspect = " << aspect_name(config.aspect) << "\n";
    f << "fov = " << config.fov << "  # horizontal degrees, 0 = original\n";
    f << "dither = " << (config.dither ? "true" : "false") << "\n";
    f << "smooth_scaling = " << (config.smooth_scaling ? "true" : "false") << "\n";
    f << "fullscreen = " << (config.fullscreen ? "true" : "false") << "\n";
    f << "volume = " << config.volume << "\n";
    f << "overlay = " << config.overlay << "\n";
}

}  // namespace psx
