#pragma once
#include <cerrno>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <algorithm>

namespace loot_config {
// Same key and 1–50 metre bounds as Auto Loot Plus's F8 slider. Invalid or
// incomplete writes retain the last valid setting rather than resetting it.
inline bool ParseRadiusLine(const char* line, double& radius) {
    while (*line == ' ' || *line == '\t') ++line;
    constexpr char key[] = "radiusMeters";
    if (std::strncmp(line, key, sizeof(key) - 1)) return false;
    line += sizeof(key) - 1;
    while (*line == ' ' || *line == '\t') ++line;
    if (*line++ != '=') return false;
    errno = 0;
    char* end = nullptr;
    const double value = std::strtod(line, &end);
    if (end == line || errno == ERANGE || !std::isfinite(value)) return false;
    while (*end == ' ' || *end == '\t' || *end == '\r' || *end == '\n') ++end;
    if (*end && *end != '#' && *end != ';') return false;
    radius = std::clamp(value, 1.0, 50.0);
    return true;
}
}
