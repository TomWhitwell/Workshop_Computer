#include "telexio.h"

// Utility
#include "i2c_multi.h"
#include <stdio.h>

// TELEXI 
#include "TELEXi/AnalogReader.h"

// TELEXO
#include "TELEXo/Oscillator.h"
#include "TELEXo/CVOutput.h"
#include "TELEXo/TriggerOutput.h"

// TELEXI and TELEXO
#include "TELEXo/Quantizer.h"       // the one from TELEXO is a superset  
#include "TELEXo/TxHelper.h"        // the one from TELEXO is a superset
#include "TELEXo/telex.h"           

#include "Oracle.h"

// declarations for i2c
static PIO pio = pio0;
static uint i2c_pin_ = 0;
static uint8_t tx_buffer[64] = {0};
static uint8_t rx_buffer[64] = {0};
static uint8_t ringbuffer_telexi[256] = {0};
static uint8_t ringbuffer_telexo[256] = {0};
static volatile uint8_t telexi_readhead = 0;
static volatile uint8_t telexo_readhead = 0;
static volatile uint8_t telexi_writehead = 0;
static volatile uint8_t telexo_writehead = 0;
static volatile bool stop_pending = false;
static volatile uint8_t stop_bytes = 0;
static volatile uint8_t address = 0;
static volatile uint handler_index = 0;
static volatile bool is_read = false;


// declarations for TELEXI
static AnalogReader* analogReaders[8];
static Quantizer* quant[8];

static volatile int inputValue[8];
static volatile int quantizedValue[8];
static volatile int quantizedNote[8];

static volatile uint8_t activeInput = 0;
static volatile uint8_t activeMode = 0;


// shared between cores
static volatile uint32_t now_millis;

#if BENCHMARK
// benchmarking
#include <cinttypes>
static volatile uint32_t benchmark_measure = 0;
static volatile uint32_t benchmark_max = 0;
static volatile bool benchmark_overrun = false;
static volatile bool benchmark_expose = false;
static volatile uint32_t expose_input = 0;
#endif


// TELEX objects
Oscillator* osc[4];
TriggerOutput* triggerOutputs[4]; // Note! the first two are used, the second two are useless on computercard but retained for compatibility
CVOutput* cvOutputs[4];


//
// i2c functions
//

void InitialisePIO(){
    i2c_multi_init(pio, i2c_pin_);
    i2c_multi_enable_address(I2C_TELEXI_ADDRESS);
    i2c_multi_enable_address(I2C_TELEXO_ADDRESS);

    i2c_multi_set_receive_handler(i2c_receive_handler);
    i2c_multi_set_request_handler(i2c_request_handler);
    i2c_multi_set_stop_handler(i2c_stop_handler);
    i2c_multi_set_write_buffer(tx_buffer);
}

void __not_in_flash_func(i2c_receive_handler)(const uint8_t data, const bool is_address) {
    if (is_address) {
        address = data;
        handler_index = 0;
        is_read = false;
    } else {
        is_read = true;
        rx_buffer[handler_index++] = data;
    }
}

void __not_in_flash_func(i2c_request_handler)(const uint8_t address) {
    is_read = false;
    if (address == I2C_TELEXI_ADDRESS) // only TELEXI replies to requests
    {
        // place response in tx_buffer
        uint16_t shiftReady = 0;

        // activeMode and activeInput are guarded in DecodeIO
        switch(activeMode) {
            case 1:
            shiftReady = (uint16_t)quantizedValue[activeInput];
            break;
            
            case 2:
            shiftReady = (uint16_t)quantizedNote[activeInput];
            break;
    
            default:
            shiftReady = (uint16_t)inputValue[activeInput];
            break;
        }
    
        // send the puppy as a pair of bytes
        tx_buffer[0] = shiftReady >> 8;
        tx_buffer[1] = shiftReady & 0xFF;

        #if BENCHMARK
          expose_input = shiftReady;
          benchmark_expose = true;
        #endif 
    }
}

void __not_in_flash_func(i2c_stop_handler)(const uint8_t length) {

    if (is_read)
    {
        // data received,
        stop_bytes = length;
        stop_pending = true;

        if (length == 1 && address == I2C_TELEXI_ADDRESS) {
            // this kind of message requires a response
            TxIO io = TxHelper::DecodeIO(rx_buffer[0]);
            activeInput = io.Port;
            activeMode =  io.Mode;
        } else {
            // messages from the protocol are at most 4 bytes, so we just write 4 to the ringbuffer
            if (address == I2C_TELEXI_ADDRESS) {
                ringbuffer_telexi[telexi_writehead] = length;
                ringbuffer_telexi[telexi_writehead+1] = rx_buffer[0];
                ringbuffer_telexi[telexi_writehead+2] = rx_buffer[1];
                ringbuffer_telexi[telexi_writehead+3] = rx_buffer[2];
                ringbuffer_telexi[telexi_writehead+4] = rx_buffer[3]; 
                telexi_writehead = telexi_writehead + 8; // note! buffer 256 char and counter uint8_t
            }
            if (address == I2C_TELEXO_ADDRESS) {
                ringbuffer_telexo[telexo_writehead] = length;
                ringbuffer_telexo[telexo_writehead+1] = rx_buffer[0];
                ringbuffer_telexo[telexo_writehead+2] = rx_buffer[1];
                ringbuffer_telexo[telexo_writehead+3] = rx_buffer[2];
                ringbuffer_telexo[telexo_writehead+4] = rx_buffer[3]; 
                telexo_writehead = telexo_writehead + 8; // note! buffer 256 char and counter uint8_t
            }
        }
    }
    else 
    {
        // no need to process anything; 
        // it was a request (probably) or bus error (maybe)
        stop_pending = false;
    }

}


//
//  SHARED, NEEDED FOR BOTH TELEXI and TELEXO
//

TelexIO::TelexIO()
{
    // Constructor - Runs on Core 0.
}

void TelexIO::core1()
{
    TelexIO* self = (TelexIO*)ThisPtr();
    self->SlowProcessingCore();
}

void TelexIO::StartupAnimation()
{
    // Light bottom right LED if CV outputs have not been calibrated
		LedOn(5, !CVOutsCalibrated());

    for (int i = 0; i < 6; i++)
    {
        LedOn(i);
        sleep_ms(50);
    }

    for (int i = 0; i < 6; i++)
    {
        LedOff(i);
        sleep_ms(50);
    }
}

void __not_in_flash_func(TelexIO::SlowProcessingCore)()
{
    // This code runs on Core 1.
    //
    // - initialise i2c on core 1
    // - run the main TELEXI-loop & decode i2c for both TELEXI&O

    InitialisePIO();

    StartupAnimation();

    // for intra-loop timer:
    uint32_t nextInputRead = time_us_32();

    while (true)  // Core 1 processing
    { 
        // execute at 1khz:
        uint32_t now = time_us_32();    
        if ((int32_t)(now - nextInputRead) >= 0) 
        {
            nextInputRead += 1000;
            now_millis = to_ms_since_boot(get_absolute_time());

            // read knob values and CV in
            telexIread();

            // update the leds for the CV output. 
            // we will tolerate non-atomicity here
            LedBrightness(0, cvOutputs[0]->UpdateLED()<<4 );
            LedBrightness(1, cvOutputs[1]->UpdateLED()<<4 );
            LedBrightness(2, cvOutputs[2]->UpdateLED()<<4 );
            LedBrightness(3, cvOutputs[3]->UpdateLED()<<4 );

            #if BENCHMARK
                if (benchmark_overrun) {
                    for (int i = 0; i<6; i++) {LedOn(i);} 
                }
                if (benchmark_expose) {
                    // print debug info and reset all
                    if (benchmark_max<benchmark_measure) benchmark_max=benchmark_measure;
                    printf("B %lu  M %lu  V %lu\n",benchmark_measure,benchmark_max,expose_input);
                    benchmark_measure = 0;
                    benchmark_expose  = false;
                    benchmark_overrun = false;
                }
            #endif
        }
        
        // parse the command for TELEXI
        telexIParse();

        if (oracle.state == QuestionAsked) {   
            #if BENCHMARK
              uint32_t thinktime1 = time_us_32();
            #endif 
            
            // answer core 0
            oracle_think();
            
            #if BENCHMARK
              uint32_t thinktime2 = time_us_32();
              printf("thinking %" PRIu32 "\n",thinktime2-thinktime1);
            #endif
        }
        
        tight_loop_contents();
    }
}

void __not_in_flash_func(TelexIO::ProcessSample)()
{
    // Runs on Core 0 at 48 kHz.

    #if BENCHMARK
      uint32_t measure_now = time_us_32();  
    #endif

    static uint32_t now_previous = 0;

    int32_t valueL = static_cast<int32_t>(cvOutputs[0]->Update());
    int32_t valueR = static_cast<int32_t>(cvOutputs[1]->Update());
    int32_t valueCV1 = static_cast<int32_t>(cvOutputs[2]->Update());
    int32_t valueCV2 = static_cast<int32_t>(cvOutputs[3]->Update());

    AudioOut1(static_cast<int16_t>((valueL*3413)>>15)); // 2048 = 6V
    AudioOut2(static_cast<int16_t>((valueR*3413)>>15)); // 2048 = 6V
    CVOut1Millivolts((valueCV1 * 10000) >> 15); // >>14 plus a scale adjustment (10V=32768 internally)
    CVOut2Millivolts((valueCV2 * 10000) >> 15);

    // update trigger outputs at 1khz
    if (now_previous != now_millis) {
        triggerOutputs[0]->Update(now_millis);
        triggerOutputs[1]->Update(now_millis);
        now_previous = now_millis;
    }
    else if (oracle.state == None) // if we are waiting for an answer, dont update the core
    {
        // parse the comand for TELEXO
        telexOParse();
    }
    else if (oracle.state == QuestionAnswered) {
        // answer received!
        oracle_answer();
    }
    
    if (PulseIn1RisingEdge()) cvOutputs[0]->TriggerEnvelope();
    if (PulseIn2RisingEdge()) cvOutputs[1]->TriggerEnvelope();

    #if BENCHMARK
        uint32_t time_measured = time_us_32()-measure_now;
        if (benchmark_measure < time_measured) benchmark_measure = time_measured; // worst case scenario
        if (time_measured > 15) benchmark_overrun = true;
    #endif
}


//
// FUNCTIONS FOR TELEXI
//

void TelexIO::InitTelexI()
{
    // open serial line only if benchmarking
    #if BENCHMARK
      stdio_init_all(); // for printf to serial
    #endif

    // initalisation of TELEXI code
    
    for (int i = 0; i < 8; i++)
    {
        analogReaders[i] = new AnalogReader(*this, i, false); // flag i>3 signifies CV IN
        quant[i] = new Quantizer(0);

        inputValue[i] = 0;
        quantizedValue[i] = 0;
        quantizedNote[i] = 0;
    }

}

void __not_in_flash_func(TelexIO::telexIread)()
{
    for (int p = 0; p < 8; p++)
    {
        inputValue[p] = analogReaders[p]->Read();

        QuantizeResponse response = quant[p]->Quantize(inputValue[p]);

        quantizedValue[p] = response.Value;
        quantizedNote[p] = response.Note;
    }
}

int __not_in_flash_func(TelexIO::telexIinput)(int address)
{   // used in the AnalogReader class

    switch (address)
    {
        // PARAM inputs
        case 0:
            return KnobVal(Main) << 2;
        case 1:
            return KnobVal(X) << 2;
        case 2:
            return KnobVal(Y) << 2;
        case 3:
            return (SwitchVal() == Middle) ? 0 : (SwitchVal() == Up) ? 1 : 2;
        // CV-IN inputs
        case 4:
            return mv2analogread(AudioIn1Millivolts());
        case 5:
            return mv2analogread(AudioIn2Millivolts());
        case 6:
            return mv2analogread(CVIn1Millivolts());
        case 7:
            return mv2analogread(CVIn2Millivolts());
        default:
            return 0;
            break;
    }
}

void __not_in_flash_func(TelexIO::telexIParse)(){
    if (telexi_readhead != telexi_writehead){
        
        uint8_t* message = &ringbuffer_telexi[telexi_readhead];
        uint8_t length   = message[0]; 
        
        TxResponse response = TxHelper::Parse(message+1,length);
        telexIActOnCommand(response.Command, response.Output, response.Value);
    
        telexi_readhead = telexi_readhead + 8; // we use slots of lenght 8
    }
}


void __not_in_flash_func(TelexIO::telexIActOnCommand)(uint8_t cmd, uint8_t out, int16_t value){
// taken from telexi.ino

  uint8_t outHelper = out;
  if (outHelper>3) return; // safety

  // act on your commands
  switch (cmd) {

    case TI_IN_SCALE:
      outHelper += 4;
      [[fallthrough]];
    case TI_PARAM_SCALE:
      quant[outHelper]->SetScale(value);
      break;

    case TI_IN_TOP:
      outHelper += 4;
      [[fallthrough]];
    case TI_PARAM_TOP:
      analogReaders[outHelper]->SetTop(value);
      break;

    case TI_IN_BOT:
      outHelper += 4;
      [[fallthrough]];
    case TI_PARAM_BOT:
      analogReaders[outHelper]->SetBottom(value);
      break;

    case TI_IN_CALIB:  
      outHelper += 4;  
      [[fallthrough]];
    case TI_PARAM_CALIB:
      analogReaders[outHelper]->Calibrate(value);
      break;    

    case TI_STORE:
      //saveCalibrationData();
      break;

    case TI_RESET:
      //resetCalibrationData();
      break;

    default:
        break;

  }
}


//
// FUNCTIONS FOR TELEXO
//

void TelexIO::InitTelexO()
{
    // Initalisation of TelexO code

    // initialise outputs
    triggerOutputs[0]= new TriggerOutput(*this,0,4); // trigger output L
    triggerOutputs[1]= new TriggerOutput(*this,1,5); // trigger output L
    triggerOutputs[2]= new TriggerOutput(*this,9,9); // dummy!
    triggerOutputs[3]= new TriggerOutput(*this,9,9); // dummy!
    cvOutputs[0] = new CVOutput(*this,0,0); // Audio L
    cvOutputs[1] = new CVOutput(*this,1,1); // Audio R
    cvOutputs[2] = new CVOutput(*this,2,2); // CV L
    cvOutputs[3] = new CVOutput(*this,3,3); // CV R
    cvOutputs[0]->ReferenceTriggers(triggerOutputs, 4); 
    cvOutputs[1]->ReferenceTriggers(triggerOutputs, 4);
    cvOutputs[2]->ReferenceTriggers(triggerOutputs, 4);
    cvOutputs[3]->ReferenceTriggers(triggerOutputs, 4);

    #if STRESSTEST
    // write sensible defaults for stress testing
    for (int i = 0; i < 4; ++i) {
        cvOutputs[i]->SetValue(0xFFF0);
        cvOutputs[i]->SetLog(0);
        cvOutputs[i]->SetFrequency(500);
        cvOutputs[i]->SetWaveform(350);
    }
    #endif
}

void __not_in_flash_func(TelexIO::telexOParse)(){
    if (telexo_readhead != telexo_writehead){
        
        uint8_t* message = &ringbuffer_telexo[telexo_readhead];
        uint8_t length   = message[0]; 
        
        TxResponse response = TxHelper::Parse(message+1,length);
        telexOActOnCommand(response.Command, response.Output, response.Value);
    
        telexo_readhead = telexo_readhead + 8; // we use slots of lenght 8
    }
}

void __not_in_flash_func(TelexIO::telexOActOnCommand)(uint8_t cmd, uint8_t out, int16_t value){
  // taken from telexo.ino
  
  // zero-adjust the output number
  uint8_t targetOutput = out;

  //if (targetOutput < 0) return;  //always true
  if (targetOutput > 3) return;    //safety
   
#ifdef DEBUG
  Serial.printf("Action: %d, Output: %d, Value: %d\n", cmd, targetOutput, value);
#endif

  unsigned long ms = millis();
  
  switch(cmd) {
    
    case TO_CV_SET:
      // set the value directly - no slew
      cvOutputs[targetOutput]->SetValue(value << 1);
      break;
    
    case TO_CV:     
      // set the target value and slew to it
      cvOutputs[targetOutput]->TargetValue(value << 1); 
      break;  

    case TO_CV_SLEW:
      // set the slew value
      cvOutputs[targetOutput]->SetSlew(value, 0);
      break;
      
    case TO_CV_SLEW_S:
      // set the slew value
      cvOutputs[targetOutput]->SetSlew(value, 1);
      break;
    
    case TO_CV_SLEW_M:
      // set the slew value
      cvOutputs[targetOutput]->SetSlew(value, 2);
      break;

    case TO_CV_OFF:
      // set the offset
      cvOutputs[targetOutput]->SetOffset(value << 1);
      break;

    case TO_CV_QT:
      // Set Pulse Time Format Trigger
      cvOutputs[targetOutput]->TargetQuantizedValue(value);
      break;
      
    case TO_CV_QT_SET:
      // Set Pulse Time Format Trigger
      cvOutputs[targetOutput]->SetQuantizedValue(value);
      break;

    case TO_CV_N:
      // Set Pulse Time Format Trigger
      cvOutputs[targetOutput]->TargetNote(value);
      break;
      
    case TO_CV_N_SET:
      // Set Pulse Time Format Trigger
      cvOutputs[targetOutput]->SetNote(value);
      break;
      
    case TO_CV_SCALE:
      // Set Pulse Time Format Trigger
      cvOutputs[targetOutput]->SetQuantizationScale(value);
      break;
      
    case TO_CV_LOG:
      // Enable Logarithmic Transformationn
      cvOutputs[targetOutput]->SetLog(value);
      break;

    case TO_OSC:
        //cvOutputs[targetOutput]->TargetVOct(value);
        oracle_ask(cmd,targetOutput,value);
        break;

    case TO_OSC_SET:
        //cvOutputs[targetOutput]->SetVOct(value);
        oracle_ask(cmd,targetOutput,value);
        break;

    case TO_OSC_QT:
        //cvOutputs[targetOutput]->TargetQuantizedVOct(value);
        oracle_ask(cmd,targetOutput,value);
        break;

    case TO_OSC_QT_SET:
        //cvOutputs[targetOutput]->SetQuantizedVOct(value);
        oracle_ask(cmd,targetOutput,value);
        break;
      
    case TO_OSC_FQ:
      // 
      cvOutputs[targetOutput]->TargetFrequency(value);
      break;
 
    case TO_OSC_FQ_SET: 
      cvOutputs[targetOutput]->SetFrequency(value);
      break;

    case TO_OSC_N:
      cvOutputs[targetOutput]->TargetOscNote(value);
      break;
      
    case TO_OSC_N_SET:
      cvOutputs[targetOutput]->SetOscNote(value);
      break;
           
    case TO_OSC_LFO:
      // 
      cvOutputs[targetOutput]->TargetLFO(value);
      break;
      
    case TO_OSC_LFO_SET:
      // 
      cvOutputs[targetOutput]->SetLFO(value);
      break;
      
    case TO_OSC_SYNC:
      // 
      cvOutputs[targetOutput]->Sync();
      break;
      
    case TO_OSC_PHASE:
      // 
      cvOutputs[targetOutput]->SetPhaseOffset(value);
      break;
      
    case TO_OSC_WAVE:
      // 
      cvOutputs[targetOutput]->SetWaveform(value);
      break;
      
    case TO_OSC_WIDTH:
      // 
      cvOutputs[targetOutput]->SetWidth(value);
      break;
      
    case TO_OSC_RECT:
      // 
      cvOutputs[targetOutput]->SetRectify(value);
      break;
      
    case TO_OSC_SCALE:
      // 
      cvOutputs[targetOutput]->SetOscQuantizationScale(value);
      break;
      
    case TO_OSC_SLEW:
      // 
      cvOutputs[targetOutput]->SetFrequencySlew(value, 0);
      break;
      
    case TO_OSC_SLEW_S:
      // 
      cvOutputs[targetOutput]->SetFrequencySlew(value, 1);
      break;
      
    case TO_OSC_SLEW_M:
      // 
      cvOutputs[targetOutput]->SetFrequencySlew(value, 2);
      break;
      
    case TO_OSC_CYC:
      // 
      cvOutputs[targetOutput]->TargetCycle(value, 0);
      break;
      
    case TO_OSC_CYC_S:
      // 
      cvOutputs[targetOutput]->TargetCycle(value, 1);
      break;
      
    case TO_OSC_CYC_M:
      // 
      cvOutputs[targetOutput]->TargetCycle(value, 2);
      break;
      
    case TO_OSC_CYC_SET:
      // 
      cvOutputs[targetOutput]->SetCycle(value, 0);
      break;
      
    case TO_OSC_CYC_S_SET:
      // 
      cvOutputs[targetOutput]->SetCycle(value, 1);
      break;
      
    case TO_OSC_CYC_M_SET:
      // 
      cvOutputs[targetOutput]->SetCycle(value, 2);
      break;

    case TO_OSC_CTR:
      //
      cvOutputs[targetOutput]->SetCenter(value << 1);
      break;

    case TO_ENV_ACT:
      // 
      cvOutputs[targetOutput]->SetEnvelopeMode(value);
      break;
      
    case TO_ENV_ATT:
      // 
      cvOutputs[targetOutput]->SetAttack(value, 0);
      break;
      
    case TO_ENV_ATT_S:
      // 
      cvOutputs[targetOutput]->SetAttack(value, 1);
      break;
      
    case TO_ENV_ATT_M:
      // 
      cvOutputs[targetOutput]->SetAttack(value, 2);
      break;

    case TO_ENV_DEC:
      // 
      cvOutputs[targetOutput]->SetDecay(value, 0);
      break;

    case TO_ENV_DEC_S:
      // 
      cvOutputs[targetOutput]->SetDecay(value, 1);
      break;

    case TO_ENV_DEC_M:
      // 
      cvOutputs[targetOutput]->SetDecay(value, 2);
      break;
      
    case TO_ENV_TRIG:
      // 
      cvOutputs[targetOutput]->TriggerEnvelope();
      break;
      
    case TO_ENV_EOR:
      // 
      cvOutputs[targetOutput]->SetEOR(value - 1);
      break;
      
    case TO_ENV_EOC:
      // 
      cvOutputs[targetOutput]->SetEOC(value - 1);
      break;

    case TO_ENV_LOOP:
      // 
      cvOutputs[targetOutput]->SetLoop(value);
      break;

    case TO_TR:
      // Set Trigger Value
      triggerOutputs[targetOutput]->SetState(value > 0);    
      break;

    case TO_TR_TOG:
      // Toggle Trigger State
      triggerOutputs[targetOutput]->ToggleState();    
      break;

    case TO_TR_TIME:
       // Set Pulse Time for Trigger
      triggerOutputs[targetOutput]->SetTime(value, 0);    
      break;
      
    case TO_TR_TIME_S:
       // Set Pulse Time for Trigger
      triggerOutputs[targetOutput]->SetTime(value, 1);    
      break;
      
    case TO_TR_TIME_M:
       // Set Pulse Time for Trigger
      triggerOutputs[targetOutput]->SetTime(value, 2);    
      break;

    case TO_TR_PULSE:
      // Pulse the Trigger
      triggerOutputs[targetOutput]->Pulse();
      break;

    case TO_TR_POL:
       // Set the Trigger's Polarity
      triggerOutputs[targetOutput]->SetPolarity(value != 0); 
      break;

    case TO_TR_PULSE_DIV:
      // Set Clock Divider
      triggerOutputs[targetOutput]->SetDivision(value);
      break;

    case TO_TR_M_MUL:
      // Set Clock Divider
      triggerOutputs[targetOutput]->SetMultiplier(value);
      break;

    case TO_TR_M_ACT:
      // Set Clock Divider
      triggerOutputs[targetOutput]->SetMetro(value, ms);
      break;

    case TO_TR_M:
       // Set Pulse Time for TR Metro
      triggerOutputs[targetOutput]->SetMetroTime(value, 0);    
      break;
      
    case TO_TR_M_S:
       // Set Pulse Time for TR Metro
      triggerOutputs[targetOutput]->SetMetroTime(value, 1);    
      break;
      
    case TO_TR_M_M:
       // Set Pulse Time for TR Metro
      triggerOutputs[targetOutput]->SetMetroTime(value, 2);    
      break;
        
    case TO_TR_M_BPM:
       // Set Pulse Time for TR Metro
      triggerOutputs[targetOutput]->SetMetroTime(value, 3);    
      break;
              
    case TO_TR_M_SYNC:
       // Sync Pulse Time for TR Metro
      triggerOutputs[targetOutput]->Sync();    
      break;

    case TO_M_ACT:
      // Set Clock Divider
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetro(value, ms);
      break;

    case TO_M:
       // Set Pulse Time for TR Metro
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetroTime(value, 0);    
      break;
      
    case TO_M_S:
       // Set Pulse Time for TR Metro
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetroTime(value, 1);    
      break;
      
    case TO_M_M:
       // Set Pulse Time for TR Metro
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetroTime(value, 2);    
      break;
        
    case TO_M_BPM:
       // Set Pulse Time for TR Metro
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetroTime(value, 3);    
      break;

    case TO_M_COUNT:
       // Set Count for M Repeats
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->SetMetroCount(value); 
      break;
             
    case TO_M_SYNC:
       // Sync Pulse Time for TR Metro
      for (int w = 0; w < 4; w++)
        triggerOutputs[w]->Sync(ms);
      break;
        
    case TO_TR_WIDTH:
       // Set Pulse Time for TR Metro
      triggerOutputs[targetOutput]->SetWidth(value);    
      break;

    case TO_TR_M_COUNT:
       // Set Count for M Repeats
      triggerOutputs[targetOutput]->SetMetroCount(value);    
      break;

    case TO_TR_PULSE_MUTE:
       // Mute/Unmute the appropriate output
      triggerOutputs[targetOutput]->SetMute(value == 1);  
      break;
      
    case TO_KILL:
      for(int w=0; w<4; w++){
        
        // Kill Each Trigger
        triggerOutputs[w]->Kill();
        
        // stop all slewwing
        cvOutputs[w]->Kill();
        
      } 
      break;

    case TO_TR_INIT:
       // initialize the TR Output
       triggerOutputs[targetOutput]->Reset();
      break;

    case TO_CV_INIT:
       // initialize the CV Output
       cvOutputs[targetOutput]->Reset();
      break;

    case TO_INIT:
       // initialize all TR and CV Outputs
       for(int w=0; w<4; w++){
          triggerOutputs[w]->Reset();
          cvOutputs[w]->Reset();
       }
      break;

    case TO_CV_CALIB:
      // turns the current value and offest into a permanent offset
      // writeCalibrationValue(targetOutput, cvOutputs[targetOutput]->Calibrate());
      break;
      
    case TO_CV_RESET:
      // returns the permanent offset to 0
      cvOutputs[targetOutput]->ResetCalibration();
      // writeCalibrationValue(targetOutput, 0);
      break;
      
    case TO_ENV:
      // gates the envelope on and off
      cvOutputs[targetOutput]->SetENV(value);
      break;
        
  }

}
