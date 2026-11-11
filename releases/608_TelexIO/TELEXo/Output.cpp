/*
 * TELEXo Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
#include "Output.h"
#include "telexio.h"

/*
 * Initialize and Output and its LED 
 */
Output::Output(TelexIO& telex, int output, int led)
    : _telex(telex),
      _output(output),
      _led(led),
      _hasLed(true)
{
}

