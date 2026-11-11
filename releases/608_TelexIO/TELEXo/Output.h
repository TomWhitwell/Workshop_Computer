/*
 * TELEXo Eurorack Module
 * (c) 2016, 2017 Brendon Cassidy
 * MIT License
 */
 
#pragma once
#ifndef Output_h
#define Output_h

#define MAXTIME 4294967295

class TelexIO;

class Output
{
  public:
  
    Output(TelexIO& telex, int output, int led);

    // virtual functions
    virtual void Kill() = 0;   
    
  protected:
  
    TelexIO& _telex;
    int _output = -1;
    int _led = -1;
    bool _hasLed = false;
    

  private:
 

};

#endif

