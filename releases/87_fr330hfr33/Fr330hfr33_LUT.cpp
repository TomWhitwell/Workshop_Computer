#include "Fr330hfr33_LUT.h"

// One cycle with a repeated endpoint. The audio core interpolates between
// entries, so the table remains small while phase stays full 32-bit precision.
const int16_t Fr330hfr33SawLut[65] = {
    -2048, -1984, -1920, -1856, -1792, -1728, -1664, -1600,
    -1536, -1472, -1408, -1344, -1280, -1216, -1152, -1088,
    -1024,  -960,  -896,  -832,  -768,  -704,  -640,  -576,
     -512,  -448,  -384,  -320,  -256,  -192,  -128,   -64,
        0,    64,   128,   192,   256,   320,   384,   448,
      512,   576,   640,   704,   768,   832,   896,   960,
     1024,  1088,  1152,  1216,  1280,  1344,  1408,  1472,
     1536,  1600,  1664,  1728,  1792,  1856,  1920,  1984,
     2047
};
