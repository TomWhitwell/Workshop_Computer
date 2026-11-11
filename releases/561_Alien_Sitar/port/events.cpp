#include "events.hpp"
namespace computer {
void Events::sample(bool p, bool d, Position z) {
    pluck |= (p && !previousPluck) || (z == Down && previousPosition != Down);
    damp |= d && !previousDamp;
    previousPluck = p;
    previousDamp = d;
    previousPosition = z;
}
} // namespace computer
