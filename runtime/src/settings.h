// User settings persistence (see settings.cpp).
#pragma once
#include <filesystem>
#include <string>

namespace psx {

void settings_load();  // before command-line flags, which override it
void settings_save();
// The same from and to another file (recordings keep the settings they were made with).
void settings_load(const std::filesystem::path &path);
void settings_write(const std::filesystem::path &path);
std::string aspect_name(double aspect);
bool parse_aspect(const std::string &s, double &aspect);

}  // namespace psx
