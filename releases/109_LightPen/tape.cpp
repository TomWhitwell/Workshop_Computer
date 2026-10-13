// The shared tape buffer. At file scope, not inside the card object, so the
// 168KB is obvious in the map file.

#include "tape.h"

namespace lp {

Tape gTape;

} // namespace lp
