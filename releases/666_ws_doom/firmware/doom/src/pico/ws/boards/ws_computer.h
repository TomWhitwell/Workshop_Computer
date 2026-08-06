// Board definition for the Music Thing Modular Workshop System Computer
// program card. Identical to a stock Pico except for conservative flash
// timing: rp2040-doom overclocks the system to 270MHz, and the stock
// divider of 2 would run the card's flash at 135MHz — beyond what many
// flash chips can do. Divider 4 gives 67.5MHz, safe for anything.

#ifndef _BOARDS_WS_COMPUTER_H
#define _BOARDS_WS_COMPUTER_H

#define PICO_FLASH_SPI_CLKDIV 4

#include "boards/pico.h"

#endif
