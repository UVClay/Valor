// User settings persistence (see settings.cpp).
#pragma once
#include <string>

namespace psx {

void settings_load();  // before command-line flags, which override it
void settings_save();
std::string aspect_name(double aspect);
bool parse_aspect(const std::string &s, double &aspect);

}  // namespace psx
