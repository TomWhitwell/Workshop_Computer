/*
 * ResponsiveAnalogRead.h
 * Arduino library for eliminating noise in analogRead inputs without decreasing responsiveness
 *
 * Copyright (c) 2016 Damien Clarke
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.  
 */
 
#ifndef RESPONSIVE_ANALOG_READ_H
#define RESPONSIVE_ANALOG_READ_H

#include <Arduino.h>

class ResponsiveAnalogRead
{
  public:

    // pin - the pin to read
    // sleepEnable - enabling sleep will cause values to take less time to stop changing and potentially stop changing more abruptly,
    //   where as disabling sleep will cause values to ease into their correct position smoothly
    // snapMultiplier - a value from 0 to 1 that controls the amount of easing
    //   increase this to lessen the amount of easing (such as 0.1) and make the responsive values more responsive
    //   but doing so may cause more noise to seep through if sleep is not enabled
    
    ResponsiveAnalogRead(){};  //default constructor must be followed by call to begin function
    ResponsiveAnalogRead(int pin, bool sleepEnable, float snapMultiplier = 0.01){
        begin(pin, sleepEnable, snapMultiplier);
    };

    void begin(int pin, bool sleepEnable, float snapMultiplier = 0.01);  // use with default constructor to initialize 
    
    inline int getValue() { return responsiveValue; } // get the responsive value from last update
    inline int getRawValue() { return rawValue; } // get the raw analogRead() value from last update
    inline bool hasChanged() { return responsiveValueHasChanged; } // returns true if the responsive value has changed during the last update
    inline bool isSleeping() { return sleeping; } // returns true if the algorithm is currently in sleeping mode
    void update(); // updates the value by performing an analogRead() and calculating a responsive value based off it
    void update(int rawValueRead); // updates the value accepting a value and calculating a responsive value based off it

    void setSnapMultiplier(float newMultiplier);
    inline void enableSleep() { sleepEnable = true; }
    inline void disableSleep() { sleepEnable = false; }
    inline void enableEdgeSnap() { edgeSnapEnable = true; }
    // edge snap ensures that values at the edges of the spectrum (0 and 1023) can be easily reached when sleep is enabled
    inline void disableEdgeSnap() { edgeSnapEnable = false; }
    inline void setActivityThreshold(float newThreshold) { activityThreshold = newThreshold; }
    // the amount of movement that must take place to register as activity and start moving the output value. Defaults to 4.0
    inline void setAnalogResolution(int resolution) { analogResolution = resolution; }
    // if your ADC is something other than 10bit (1024), set that here

  private:
    int pin;
    int analogResolution = 1024;
    float snapMultiplier;
    bool sleepEnable;
    float activityThreshold = 4.0;
    bool edgeSnapEnable = true;

    float smoothValue=0.0;
    unsigned long lastActivityMS;
    float errorEMA = 0.0;
    bool sleeping = false;

    int rawValue = 0;
    int responsiveValue = 0;
    int prevResponsiveValue = 0;
    bool responsiveValueHasChanged = false;

    int getResponsiveValue(int newValue);
    float snapCurve(float x);
};

/*
 * ResponsiveAnalogRead.cpp
 * Arduino library for eliminating noise in analogRead inputs without decreasing responsiveness
 *
 * Copyright (c) 2016 Damien Clarke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */


void ResponsiveAnalogRead::begin(int pin, bool sleepEnable, float snapMultiplier){
    pinMode(pin, INPUT ); // ensure button pin is an input
    digitalWrite(pin, LOW ); // ensure pullup is off on button pin
    
    this->pin = pin;
    this->sleepEnable = sleepEnable;
    setSnapMultiplier(snapMultiplier);
    
    
}


void ResponsiveAnalogRead::update()
{
  rawValue = analogRead(pin);
  this->update(rawValue);
}

void ResponsiveAnalogRead::update(int rawValueRead)
{
  rawValue = rawValueRead;
  prevResponsiveValue = responsiveValue;
  responsiveValue = getResponsiveValue(rawValue);
  responsiveValueHasChanged = responsiveValue != prevResponsiveValue;
}

int ResponsiveAnalogRead::getResponsiveValue(int newValue)
{
  // if sleep and edge snap are enabled and the new value is very close to an edge, drag it a little closer to the edges
  // This'll make it easier to pull the output values right to the extremes without sleeping,
  // and it'll make movements right near the edge appear larger, making it easier to wake up
  if(sleepEnable && edgeSnapEnable) {
    if(newValue < activityThreshold) {
      newValue = (newValue * 2) - activityThreshold;
    } else if(newValue > analogResolution - activityThreshold) {
      newValue = (newValue * 2) - analogResolution + activityThreshold;
    }
  }

  // get difference between new input value and current smooth value
  unsigned int diff = abs(newValue - smoothValue);

  // measure the difference between the new value and current value
  // and use another exponential moving average to work out what
  // the current margin of error is
  errorEMA += ((newValue - smoothValue) - errorEMA) * 0.4;

  // if sleep has been enabled, sleep when the amount of error is below the activity threshold
  if(sleepEnable) {
    // recalculate sleeping status
    sleeping = abs(errorEMA) < activityThreshold;
  }

  // if we're allowed to sleep, and we're sleeping
  // then don't update responsiveValue this loop
  // just output the existing responsiveValue
  if(sleepEnable && sleeping) {
    return (int)smoothValue;
  }

  // use a 'snap curve' function, where we pass in the diff (x) and get back a number from 0-1.
  // We want small values of x to result in an output close to zero, so when the smooth value is close to the input value
  // it'll smooth out noise aggressively by responding slowly to sudden changes.
  // We want a small increase in x to result in a much higher output value, so medium and large movements are snappy and responsive,
  // and aren't made sluggish by unnecessarily filtering out noise. A hyperbola (f(x) = 1/x) curve is used.
  // First x has an offset of 1 applied, so x = 0 now results in a value of 1 from the hyperbola function.
  // High values of x tend toward 0, but we want an output that begins at 0 and tends toward 1, so 1-y flips this up the right way.
  // Finally the result is multiplied by 2 and capped at a maximum of one, which means that at a certain point all larger movements are maximally snappy

  // then multiply the input by SNAP_MULTIPLER so input values fit the snap curve better.
  float snap = snapCurve(diff * snapMultiplier);

  // when sleep is enabled, the emphasis is stopping on a responsiveValue quickly, and it's less about easing into position.
  // If sleep is enabled, add a small amount to snap so it'll tend to snap into a more accurate position before sleeping starts.
  if(sleepEnable) {
    snap = (snap*0.5) + 0.5;
  }

  // calculate the exponential moving average based on the snap
  smoothValue += (newValue - smoothValue) * snap;

  // ensure output is in bounds
  if(smoothValue < 0.0) {
    smoothValue = 0.0;
  } else if(smoothValue > analogResolution - 1) {
    smoothValue = analogResolution - 1;
  }

  // expected output is an integer
  return (int)smoothValue;
}

float ResponsiveAnalogRead::snapCurve(float x)
{
  float y = 1.0 / (x + 1.0);
  y = (1.0 - y) * 2.0;
  if(y > 1.0) {
    return 1.0;
  }
  return y;
}

void ResponsiveAnalogRead::setSnapMultiplier(float newMultiplier)
{
  if(newMultiplier > 1.0) {
    newMultiplier = 1.0;
  }
  if(newMultiplier < 0.0) {
    newMultiplier = 0.0;
  }
  snapMultiplier = newMultiplier;
}


#endif
