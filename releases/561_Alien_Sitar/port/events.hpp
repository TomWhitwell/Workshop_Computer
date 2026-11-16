#pragma once
#include "controller.hpp"
namespace computer {
struct Events {
    bool previousPluck = false, previousDamp = false;
    Position previousPosition = Middle;
    bool pluck = false, damp = false;
    void sample(bool p, bool d, Position z);
};
} // namespace computer
