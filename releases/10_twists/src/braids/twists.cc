//#pragma GCC optimize ("O0")

// Copyright 2012 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
// 
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
// 
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
// 
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// Ported to Music Thing Computer by Tom Waters using code from Chris Johnson's Reverb card

#include <algorithm>
#include <queue>
#include <cmath>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/adc.h"

#include "stmlib/utils/dsp.h"

#include "braids/drivers/gate_input.h"
#include "braids/drivers/switch.h"
#include "braids/drivers/dac.h"
#include "braids/drivers/cv_out.h"
#include "braids/envelope.h"
#include "braids/macro_oscillator.h"
#include "braids/quantizer.h"
#include "braids/signature_waveshaper.h"
#include "braids/vco_jitter_source.h"
#include "braids/ui.h"
#include "braids/usb_worker.h"
#include "braids/quantizer_scales.h"
#include "braids/resources.h"
#include "braids/midi_message.h"

 #include "braids/drivers/calibration.h"


#define PIN_PULSE1_OUT      8
#define PIN_PULSE2_OUT      9
#define PIN_MUX_LOGIC_A     24
#define PIN_MUX_LOGIC_B     25
#define PIN_MUX_OUT_X       28
#define PIN_MUX_OUT_Y       29

using namespace braids;
using namespace std;
using namespace stmlib;

const size_t kNumBlocks = 4;
const size_t kBlockSize = 24;

MacroOscillator osc;
Envelope envelope;
Dac dac;
GateInput gate_input;
Quantizer quantizer;
SignatureWaveshaper ws;
VcoJitterSource jitter_source;
Ui ui;
UsbWorker usbWorker;
CvOut cvOut;
Calibration calibration;

uint8_t current_scale = 0xff;
size_t current_sample;
volatile size_t playback_block;
volatile size_t render_block;
int16_t audio_samples[kNumBlocks][kBlockSize];
uint8_t sync_samples[kNumBlocks][kBlockSize];

bool trigger_detected_flag;
volatile bool trigger_flag;
uint16_t trigger_delay;

queue<MIDIMessage> midi_messages;
volatile bool midi_active = false;
volatile bool midi_note_on = false;
volatile bool midi_note_off = false;
volatile uint8_t midi_notes_on = 0;
volatile uint8_t midi_note = 0;

volatile uint8_t mxPos = 0; // external multiplexer value
volatile int32_t knobssm[4] = {0,0,0,0};
volatile int32_t cvsm[2] = {0,0};
volatile uint16_t knobs[4] = {0,0,0,0}; // 0-4095
volatile uint16_t cv[2] = {0,0}; // -2047 - 2048
volatile uint16_t audio_in[2] = {2048, 2048};

float slope = 0;
float intercept = 0;

void __not_in_flash_func(timer_callback)() {
  // Now, get ADC data from this cycle
	// Should return immediately since this IRQ handler called only when ADC FIFO has data
	uint16_t adc = adc_fifo_get_blocking();

  dac.Write(audio_samples[playback_block][current_sample]);
  
  bool trigger_detected = gate_input.raised();
  sync_samples[playback_block][current_sample] = trigger_detected;
  trigger_detected_flag = trigger_detected_flag | trigger_detected;

  current_sample = current_sample + 1;
  if (current_sample >= kBlockSize) {
    current_sample = 0;
    playback_block = (playback_block + 1) % kNumBlocks;
  }

  auto zz = static_cast<int16_t>(adc + (((adc + 0x200) >> 10) << 3));
  auto delta = static_cast<int16_t>(adc - (0x01ff + 0));
  if(!(delta & 0x01ff)) zz += 4;

  switch(adc_get_selected_input()) 
	{
    case 0:
    	// ~100Hz LPF on CV input
		  cvsm[mxPos % 2] = (7 * (cvsm[mxPos % 2]) + 16 * zz) >> 3;
      cv[mxPos % 2] = cvsm[mxPos % 2] >> 4;
    break;
    case 1:
      audio_in[1] = 4095 - zz;
      break;
    case 2:
      audio_in[0] = 4095 - zz;
      break;
    case 3:
      // Each knob sampled at 48kHz/4 = 12kHz
		  // Then IIR filter with time constant ~128 samples, so around 100Hz
		  knobssm[mxPos] = (127 * (knobssm[mxPos]) + 16 * adc) >> 7;
      knobs[mxPos] = knobssm[mxPos] >> 4;

      mxPos = (mxPos + 1) & 0x03;
      bool logic_a = (mxPos & 1) ? true : false;
      bool logic_b = (mxPos >> 1) ? true : false; 
    	gpio_put(PIN_MUX_LOGIC_A, logic_a);
		  gpio_put(PIN_MUX_LOGIC_B, logic_b);
      
      if (trigger_detected_flag) {
        trigger_delay = settings.trig_delay() ? (1 << settings.trig_delay()) : 0;
        ++trigger_delay;
        trigger_detected_flag = false;
      }
      if (trigger_delay) {
        --trigger_delay;
        if (trigger_delay == 0) {
          trigger_flag = true;
        }
      }

      break;
  }
}

uint32_t GetUniqueId() {
  uint8_t unique_id[8]; // 64 bits = 8 bytes
  flash_get_unique_id(unique_id);
  
  // return last 32 bits
  return (unique_id[4] << 24) | (unique_id[5] << 16) | (unique_id[6] << 8) | unique_id[7];
}

// midi on then off? or off and not on
void USBMIDICallback(MIDIMessage message) {
  uint8_t engine_channel = settings.GetValue(SETTING_MIDICHANNEL_ENGINE);
  uint8_t out1_channel = settings.GetValue(SETTING_MIDICHANNEL_OUT1);
  uint8_t out2_channel = settings.GetValue(SETTING_MIDICHANNEL_OUT2);
  float note_volts = (static_cast<float>(message.note) - 48.0) / 12.0;

  switch(message.command) {
    case MIDIMessage::NoteOn:
      if(message.velocity > 0) {
        if(engine_channel == 0 || message.channel == engine_channel) {
          midi_messages.push(message);
        }

        if(out1_channel == 0 || message.channel == out1_channel) {
          cvOut.SetFloat(0, note_volts);
          gpio_put(PIN_PULSE1_OUT, false);
        }
        if(out2_channel == 0 || message.channel == out2_channel) {
          cvOut.SetFloat(1, note_volts);
          gpio_put(PIN_PULSE2_OUT, false);
        }
      }
      break;
    case MIDIMessage::NoteOff:
      if(engine_channel == 0 || message.channel == engine_channel) {
        midi_messages.push(message);
      }
      if(out1_channel == 0 || message.channel == out1_channel) {
        gpio_put(PIN_PULSE1_OUT, true);
      }
      if(out2_channel == 0 || message.channel == out2_channel) {
        gpio_put(PIN_PULSE2_OUT, true);
      }        
      break;
  }
}

void RunUSBWorker() {
  multicore_lockout_victim_init();
  usbWorker.Run();
}

void Init() {
  // Pulse
  gpio_init(PIN_PULSE1_OUT);
  gpio_set_dir(PIN_PULSE1_OUT, GPIO_OUT);
  gpio_put(PIN_PULSE1_OUT, true);
  gpio_init(PIN_PULSE2_OUT);
  gpio_set_dir(PIN_PULSE2_OUT, GPIO_OUT);
  gpio_put(PIN_PULSE2_OUT, true);

  multicore_lockout_victim_init();
  settings.Init();
  ui.Init();
  gate_input.Init();
  dac.Init();
  osc.Init();
  quantizer.Init();
  
  for (size_t i = 0; i < kNumBlocks; ++i) {
    fill(&audio_samples[i][0], &audio_samples[i][kBlockSize], 0);
    fill(&sync_samples[i][0], &sync_samples[i][kBlockSize], 0);
  }
  playback_block = kNumBlocks / 2;
  render_block = 0;
  current_sample = 0;
  
  envelope.Init();
  ws.Init(GetUniqueId());
  jitter_source.Init();

  usbWorker.Init(&USBMIDICallback);
  multicore_launch_core1(RunUSBWorker);

  gpio_init(PIN_MUX_LOGIC_A);
  gpio_set_dir(PIN_MUX_LOGIC_A, GPIO_OUT);
  gpio_init(PIN_MUX_LOGIC_B);
  gpio_set_dir(PIN_MUX_LOGIC_B, GPIO_OUT);

  adc_init();
  adc_gpio_init(PIN_MUX_OUT_X);
  adc_gpio_init(PIN_MUX_OUT_Y);

  adc_set_round_robin(0x0F);
	adc_fifo_setup(true, true, 1, false, false);
	adc_set_clkdiv((48000000 / 96000) - 1); // 96KHz interrupt
	irq_set_exclusive_handler(ADC_IRQ_FIFO, timer_callback);
	adc_irq_set_enabled(true);
	irq_set_enabled(ADC_IRQ_FIFO, true);
	adc_run(true);

  calibration.init();
}

const uint16_t bit_reduction_masks[] = {
    0xc000,
    0xe000,
    0xf000,
    0xf800,
    0xff00,
    0xfff0,
    0xffff };

const uint16_t decimation_factors[] = { 24, 12, 6, 4, 3, 2, 1 };

int16_t clamp(int16_t val, int16_t min, int16_t max) {
  if(val > max) return max;
  if(val < min) return min;
  return val;
}

bool last_trigger_from_midi = false;
void __not_in_flash_func(RenderBlock)() {
  static int16_t previous_pitch = 0;
  static uint16_t gain_lp;

  envelope.Update(
      settings.GetValue(SETTING_AD_ATTACK) * 8,
      settings.GetValue(SETTING_AD_DECAY) * 8);
  uint32_t ad_value = envelope.Render(last_trigger_from_midi);
  
  osc.set_shape(settings.shape());

  int16_t timbre = clamp(knobs[1] + (audio_in[0] - 2048), 0, 4095);;
  timbre = timbre << 3;
  int16_t color = clamp(knobs[2] + (audio_in[1] - 2048), 0, 4095);
  color = color << 3;
  osc.set_parameters(timbre, color);

  if(!midi_messages.empty()) {
    MIDIMessage midi_message = midi_messages.front();
    midi_messages.pop();

    last_trigger_from_midi = false;
    if(midi_message.command == MIDIMessage::NoteOn) {
      midi_note_on = true;
      midi_active = true;
      last_trigger_from_midi = true;
      midi_note = midi_message.note;
      midi_notes_on++;      
    } else if(midi_message.command == MIDIMessage::NoteOff) {
      midi_notes_on--;
      if(midi_notes_on == 0) {
          midi_note_off = true;
      }
    }
  }
  
  // pitch is (pitchV * 12.0 + 60) * 128

  // CV in = 0 - 4095 =  +6v -6v
  // cv_in_lookup is a lookup of Computer cv to the values braids expects
  // x = (12v * 12st * 128)
  // cv_pitch = ((4095 - cv[0]) * x) - (x / 2)
  // an alternative close approximation would be (((4095 - cv[0]) / 2) * 9) - 9216

  // CV Pot = 0 - 4095 = -4v +4v
  int32_t pot_pitch = (knobs[0] * 3) - 6144;

  // if we're using midi, react to the latest message
  int32_t pitch = (int32_t)(((slope * cv[0] + intercept) * 12.0 + 48.0) * 128.0);
  if(midi_active) {
    pitch += ((midi_note - 48) * 128);
    if(pot_pitch < -1000) {
      pitch += pot_pitch + 1000;
    } else if(pot_pitch > 1000) {
      pitch += pot_pitch - 1000;
    }
  } 
  else {
    pitch += pot_pitch;
  }

  //pitch = settings.adc_to_pitch(pitch);

  // Check if the pitch has changed to cause an auto-retrigger
  int32_t pitch_delta = pitch - previous_pitch;
  if (settings.data().auto_trig &&
      (pitch_delta >= 0x40 || -pitch_delta >= 0x40)) {
    trigger_detected_flag = true;
  }
  previous_pitch = pitch;
  pitch += jitter_source.Render(settings.vco_drift());
  pitch += ad_value * settings.GetValue(SETTING_AD_FM) >> 7;
  
  if (pitch > 16383) {
    pitch = 16383;
  } else if (pitch < 0) {
    pitch = 0;
  }
  
  if (settings.vco_flatten()) {
    pitch = Interpolate88(lut_vco_detune, pitch << 2);
  }
  osc.set_pitch(pitch + settings.pitch_transposition());

  if (trigger_flag || midi_note_on) {
    osc.Strike();
    envelope.Trigger(ENV_SEGMENT_ATTACK);
    trigger_flag = false;
    midi_note_on = false;
  }
  else if(midi_note_off) {
    midi_note_off = false;
    envelope.Trigger(ENV_SEGMENT_DECAY);
  }
  
  uint8_t* sync_buffer = sync_samples[render_block];
  int16_t* render_buffer = audio_samples[render_block];
  
  if (settings.GetValue(SETTING_AD_VCA) != 0
    || settings.GetValue(SETTING_AD_TIMBRE) != 0
    || settings.GetValue(SETTING_AD_COLOR) != 0
    || settings.GetValue(SETTING_AD_FM) != 0) {
    memset(sync_buffer, 0, kBlockSize);
  }
  
  osc.Render(sync_buffer, render_buffer, kBlockSize);
  
  // Copy to DAC buffer with sample rate and bit reduction applied.
  int16_t held_sample = 0;
  size_t decimation_factor = decimation_factors[settings.data().sample_rate];
  uint16_t bit_mask = bit_reduction_masks[settings.data().resolution];
  int32_t gain = settings.GetValue(SETTING_AD_VCA) ? ad_value : 65535;
  uint16_t signature = settings.signature() * settings.signature() * 4095;

  for (size_t i = 0; i < kBlockSize; ++i) {

    if ((i % decimation_factor) == 0) {
      held_sample = render_buffer[i] & bit_mask;
    }
    int16_t sample = held_sample * gain_lp >> 16;
    gain_lp += (gain - gain_lp) >> 4;
    int16_t warped = ws.Transform(sample);
    render_buffer[i] = Mix(sample, warped, signature);
    render_buffer[i] = (-render_buffer[i] + 32768) >> 5;
  }

  render_block = (render_block + 1) % kNumBlocks;
}

bool __not_in_flash_func(DoCalibration)() {
  UIMode mode = ui.GetMode();
  if(mode == UIMode::CALIBRATION1 || mode == UIMode::CALIBRATION2) {

    if(mode == UIMode::CALIBRATION2) {
      float sumX = 0;
      float sumY = 0;
      float sumX2 = 0;
      float sumY2 = 0;
      float sumProduct = 0;

      int numPoints = 0;
      float data[11000][2];
      for(float f = -5.0f; f < 5.1f; f += 0.01f) {
        cvOut.Set(0, calibration.voltageToCalibratedOut(0, f));
        sleep_ms(10);
        uint t = 0;
        for(int i=0; i<50; i++) {
          t += cv[0];
        }

        data[numPoints][0] = (float)(t / 50.0);
        data[numPoints][1] = f;

        sumX += data[numPoints][0];
        sumY += data[numPoints][1];
        sumX2 += data[numPoints][0] * data[numPoints][0];
        sumY2 += data[numPoints][1] * data[numPoints][1];
        sumProduct += data[numPoints][0] * data[numPoints][1];
        numPoints++;
      }
      cvOut.Set(0, calibration.voltageToCalibratedOut(0, 0.0f));

      float a = numPoints * sumProduct - sumX * sumY;
      float b = numPoints * sumX2 - (sumX * sumX);
      float c = numPoints * sumY2 - (sumY * sumY);
      float correlationCoefficient = a / sqrt(b * c);

      float meanX = sumX / numPoints;
      float meanY = sumY / numPoints;

      float sumDevX2 = 0;
      float sumDevY2 = 0;
      for(int i = 0; i < numPoints; i++) {
        sumDevX2 += (data[i][0] - meanX) * (data[i][0] - meanX) ;
        sumDevY2  += (data[i][1] - meanY) * (data[i][1] - meanY) ;
      }

      float stdDevX = sqrt(sumDevX2 / (numPoints - 1));
      float stdDevY = sqrt(sumDevY2 / (numPoints - 1));
      slope = correlationCoefficient * (stdDevY  / stdDevX);
      intercept = meanY - slope * meanX;
      
      // done
      calibration.writeCVInCalibration(slope, intercept);
      ui.SetMode(UIMode::PLAY);
    }
    return true;
  } 
  
  return false;
}

uint32_t last = 0;
int main() {
  //stdio_init_all();
  Init();

  calibration.readCVInCalibration(&slope, &intercept);
  while (1) {
    if (current_scale != settings.GetValue(SETTING_QUANTIZER_SCALE)) {
      current_scale = settings.GetValue(SETTING_QUANTIZER_SCALE);
      quantizer.Configure(scales[current_scale]);
    }

    if(!DoCalibration()) {
      while (render_block != playback_block) {
        RenderBlock();
      }
    }
    
    uint32_t now = to_ms_since_boot(get_absolute_time());
    if(now != last) {
      last = now;
      ui.Poll(now, knobs[3]);
    }
  }
}
