// File reading for the loaders (the core reads the user's extracted disc data at run time).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace valor {

bool read_file(const std::string& path, std::vector<uint8_t>& out);

}  // namespace valor
