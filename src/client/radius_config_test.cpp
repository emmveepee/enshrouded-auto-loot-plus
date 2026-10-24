#include "radius_config.h"
#include <cstdio>

int main() {
    double radius = 3;
    struct Case { const char* text; bool accepted; double expected; } cases[] = {
        {"radiusMeters=15\n", true, 15},
        {" radiusMeters = 4.5 # slider\r\n", true, 4.5},
        {"radiusMeters=", false, 4.5},
        {"radiusMeters=nan", false, 4.5},
        {"radiusMeters=inf", false, 4.5},
        {"radiusMeters=1e9999", false, 4.5},
        {"radiusMeters=6junk", false, 4.5},
        {"# radiusMeters=40", false, 4.5},
        {"radiusMetersOther=40", false, 4.5},
        {"radiusMeters=0", true, 1},
        {"radiusMeters=999", true, 50},
        {"radiusMeters=3 ; restored", true, 3},
    };
    for (const auto& c : cases) {
        const bool accepted = loot_config::ParseRadiusLine(c.text, radius);
        if (accepted != c.accepted || radius != c.expected) {
            std::printf("FAIL: %s accepted=%d radius=%g\n", c.text, accepted, radius);
            return 1;
        }
    }
    std::puts("PASS: radius parsing, bounds, invalid/partial-write retention (12 cases)");
}
