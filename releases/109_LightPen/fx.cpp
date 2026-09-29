#include "fx.h"

namespace lp {

// File scope, not a member: 40KB on the card object would be 40KB the other
// modes' engines also pay for in cache locality, and main() already keeps the
// card static for exactly this reason.
int16_t gFx[kFxLen];

} // namespace lp
